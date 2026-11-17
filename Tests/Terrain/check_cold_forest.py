#!/usr/bin/env python3
"""Measure ColdForest's actual GLSL rivers, descending pits and porous walls.

Production helpers execute on OpenGL; CPU code supplies spatial sampling and
independent dimensional/monotonicity limits, never replacement terrain noise.
"""
from pathlib import Path
import argparse
import json
import math
import re

import numpy as np
from scipy.spatial import cKDTree
from scipy.stats import qmc

from biome_test_support import biome_recipe_source, packed_biome_cells
from check_biomes import profile_probe_source
from check_biome_landforms import (DensityProbe, SCENE_OPTIONS,
    check_targeted_pipeline, source_fingerprint)
from check_gpu import SHADERS, FieldRunner
from field_batch_runner import default_uniforms
from gl_headless import GL

PIT_RADIUS = 10000.
PIT_SPACING = 50000.
PIT_EMPTY_RADIUS = .70


HEADER = r'''
layout(local_size_x=8,local_size_y=8) in;
layout(std430,binding=9) readonly buffer ProbeInput { vec4 queries[]; };
layout(std430,binding=6) writeonly buffer ProbeOutput { vec4 result[]; };
uniform uint uCount;
'''

SITES = r'''
void main() {
 uint i=gl_GlobalInvocationID.x+gl_GlobalInvocationID.y*gl_NumWorkGroups.x*8u;
 if(i>=uCount)return;
 ivec2 cell=ivec2(queries[i].xy);
 ColdForestPitSite s=coldForestPitInCell(cell);
 result[4u*i]=vec4(s.center,coldForestPitEligibility(s),coldForestRegionMask(s.center));
 result[4u*i+1u]=vec4(float(s.key&65535u),float(s.key>>16u),cell);
 result[4u*i+2u]=vec4(COLD_FOREST_PIT_RADIUS,COLD_FOREST_PIT_DEPTH,
                     COLD_FOREST_PIT_SPACING,COLD_FOREST_PORE_SCALE);
 result[4u*i+3u]=vec4(0.);
}
'''

MASKS = r'''
void main() {
 uint i=gl_GlobalInvocationID.x+gl_GlobalInvocationID.y*gl_NumWorkGroups.x*8u;
 if(i>=uCount)return;
 vec2 p=queries[i].xz;
 result[4u*i]=vec4(coldForestRegionMask(p),coldForestRiverMask(p),
                  coldForestRiverChannelMask(p),coldForestRiverDistance(p));
 for(uint j=1u;j<4u;++j)result[4u*i+j]=vec4(0.);
}
'''

GEOMETRY = r'''
void main() {
 uint i=gl_GlobalInvocationID.x+gl_GlobalInvocationID.y*gl_NumWorkGroups.x*8u;
 if(i>=uCount)return;
 vec3 p=queries[i].xyz;
 ColdForestPitSite s=coldForestPitSite(p.xz);
 vec3 q=vec3(p.x-s.center.x,p.y,p.z-s.center.y);
 float radial=length(q.xz)/COLD_FOREST_PIT_RADIUS;
 float fraction=coldForestPitVoidFraction(radial);
 result[4u*i]=vec4(coldForestPitDepth(p.xz,s),fraction,
                  coldForestPoreField(q,s.key,fraction),radial);
 result[4u*i+1u]=vec4(s.center,0.,0.);
 result[4u*i+2u]=vec4(0.);result[4u*i+3u]=vec4(0.);
}
'''

WORLD = r'''
void main() {
 uint i=gl_GlobalInvocationID.x+gl_GlobalInvocationID.y*gl_NumWorkGroups.x*8u;
 if(i>=uCount)return;
 vec3 p=queries[i].xyz;
 FieldColumn c=fieldProbeColumn(p.xz);
 ColdForestPitSite s=coldForestPitSite(p.xz);
 float depth=coldForestPitDepth(p.xz,s);
 p.y+=c.terrain.x;
 float delta=fieldVolume(p,c);
 result[4u*i]=vec4(fieldProbeDensity(p,c),c.terrain.x-p.y,delta,depth);
 result[4u*i+1u]=vec4(c.terrain.x,c.terrain.w,c.blend.w,p.y);
 result[4u*i+2u]=vec4(fieldVolumeEnvelope(c),coldForestPitEligibility(s),
                   length(p.xz-s.center)/COLD_FOREST_PIT_RADIUS);
 result[4u*i+3u]=vec4(coldForestRegionMask(p.xz),coldForestRiverMask(p.xz),0.,0.);
}
'''

MIXED = r'''
void main() {
 uint i=gl_GlobalInvocationID.x+gl_GlobalInvocationID.y*gl_NumWorkGroups.x*8u;
 if(i>=uCount)return;
 vec3 p=queries[i].xyz;
 FieldColumn c=fieldProbeColumn(p.xz);
 const uint cell=22u; // BorealForest7 -> ColdForest8 above the marine slice.
 uvec4 mapping=fieldBiomeCells[cell];
 c.terrain.yz=vec2((2.+queries[i].w)*.25,.25);
 c.blend.x=.5;c.detail=uvec4(mapping.x|(cell<<20u),mapping.yz,0u);
 float sw=fieldSurfaceWeight(c,8u),vw=fieldVolumeWeight(c,8u);
 c.terrain.w*=sw;
 c=fieldUnpackColumn(fieldPackColumn(c,c.terrain.w/uDetailScale));
 p.y+=c.terrain.x;
 c.blend.w=0.;float a=fieldVolume(p,c);
 c.blend.w=.93;float b=fieldVolume(p,c);
 float base=c.terrain.x-p.y;
 vec2 envelope=fieldVolumeEnvelope(c);
 float wanted=base+vw*b;
 float actual=base;
 if(c.terrain.w>.0002&&p.y>=envelope.x&&p.y<=envelope.y)actual+=vw*b;
 result[4u*i]=vec4(a,b,wanted,actual);
 result[4u*i+1u]=vec4(sw,vw,c.terrain.w,base);
 result[4u*i+2u]=vec4(envelope,p.y,coldForestPitEligibility(coldForestPitSite(p.xz)));
 result[4u*i+3u]=vec4(0.);
}
'''

# The previous recipe's substrate and small-scale noise, with its old river
# wall factor set to one and its localized crescent mound omitted.
HEIGHT = r'''
float oldUpperSubstrate(vec2 p) {
 vec2 q=noiseRotate2D(landWarp(p,3500.,260.,801u),vec2(.6,.8));
 float axis=q.x*.0021+.7*noiseSimplex2D(q*vec2(.00021,.00065),uSeed^802u);
 float wall=smoothstep(.18,.78,abs(sin(axis)));
 float ridges=landRidges(q*vec2(.0009,.0015),803u);
 float region=.5+.5*noiseSimplex2D(q*.00016,uSeed^804u);
 float depth=240.+160.*region+95.*ridges*ridges;
 float detail=landFbm(q*vec2(.00048,.00024),805u)*24.;
 detail+=noiseSimplex2D(q*.007,uSeed^806u)*1.8*wall;
 return -108.+depth+detail;
}
void main() {
 uint i=gl_GlobalInvocationID.x+gl_GlobalInvocationID.y*gl_NumWorkGroups.x*8u;
 if(i>=uCount)return;
 vec2 p=queries[i].xz;
 vec4 relief=fieldRelief(p);
 result[4u*i]=vec4(coldForestBaseHeight(p),oldUpperSubstrate(p),
                  coldForestGroundHeight(p),coldForestRiverMask(p));
 result[4u*i+1u]=vec4(coldForestRegionMask(p),coldForestRiverChannelMask(p),
                     coldForestRiverDistance(p),0.);
 result[4u*i+2u]=relief;
 FieldColumn c=fieldProbeColumn(p);
 result[4u*i+3u]=vec4(c.terrain.x,c.terrain.w,c.blend.w,0.);
}
'''


class PointProbe:
    def __init__(self, gl, source):
        self.gl = gl
        directory = SHADERS / 'biomes' / 'ColdForest'
        relief = biome_recipe_source(directory / 'relief.C.glsl')
        volume = re.sub(r'^#version[^\n]*\n', '',
            biome_recipe_source(directory / 'volume.C.glsl'), count=1)
        self.program = gl.compile([(0x91B9,
            relief + volume + profile_probe_source() + HEADER + source)],
            'ColdForest measurement')
        self.inputs = gl.buffer(0)
        self.output = gl.buffer(0)
        self.mapping = gl.buffer(packed_biome_cells())

    def run(self, queries, seed=1337, **options):
        queries = np.asarray(queries, dtype='<f4').reshape(-1, 4)
        gl = self.gl
        count = len(queries)
        uniforms = default_uniforms(64,
            **({'uBiomeOverride': 8} | SCENE_OPTIONS | options))
        uniforms.update(uSeed=('1ui', seed), uCount=('1ui', count))
        for name, args in uniforms.items():
            gl.uniform(self.program, name, *args)
        gl.resize(self.inputs, queries.tobytes())
        gl.bind(self.inputs, 9)
        gl.resize(self.output, bytes(count * 64))
        gl.bind(self.output, 6)
        gl.bind(self.mapping, 7)
        gl.dispatch_extent(self.program, 8, math.ceil(count / 8))
        data = np.frombuffer(gl.read(self.output, count * 64),
            dtype='<f4').copy().reshape(count, 16)
        assert np.isfinite(data).all(), 'Nonfinite ColdForest sample'
        gl.check('ColdForest measurement')
        return data

    def chunks(self, queries, seed=1337, **options):
        return np.concatenate([self.run(queries[i:i+65536], seed=seed, **options)
            for i in range(0, len(queries), 65536)])


def query_points(center, radius, angle, height):
    radius, angle, height = np.broadcast_arrays(radius, angle, height)
    return np.column_stack((center[0] + radius.ravel() * np.cos(angle.ravel()),
        height.ravel(), center[1] + radius.ravel() * np.sin(angle.ravel()),
        np.zeros(radius.size)))


def height_checks(gl):
    probe=PointProbe(gl,HEIGHT)
    unit=qmc.Sobol(2,scramble=True,seed=82379).random_base2(16)
    queries=np.zeros((len(unit),4));queries[:,[0,2]]=(unit-.5)*512000.
    records=[]
    for seed in (1337,97,98765):
        r=probe.run(queries,seed=seed)
        # Different inlining/FMA paths in the software driver can change the
        # noise coordinates by a float ULP across this 512-km domain.
        assert np.allclose(r[:,0],r[:,1],rtol=0.,atol=.004), \
            'Original broad substrate or fine soil noise changed'
        assert np.all(r[:,2]<=r[:,0]+.004),'River mask raises the substrate'
        assert np.allclose(r[:,2],r[:,8],rtol=0.,atol=.004), \
            'Relief adds unexpected mounds above the new river ground'
        inactive=r[:,3]==0.
        assert inactive.any() and (r[:,3]>.99).any(),'Mask lacks plain and full-river regions'
        assert np.allclose(r[inactive,2],r[inactive,0],rtol=0.,atol=.004),'Inactive rivers change the soil'
        assert np.all((r[:,3]>=0.)&(r[:,3]<=1.))
        assert np.all((r[:,4]>=0.)&(r[:,4]<=1.))
        assert np.allclose(r[:,3],r[:,4]*r[:,5],atol=1e-6), \
            'Large region no longer weights river channels'
        records.append(dict(seed=seed,samples=len(r),soil_substrate_max_error_raw=float(abs(r[:,0]-r[:,1]).max()),
            unchanged_soil_samples=int(inactive.sum()),
            excavated_samples=int((r[:,2]<r[:,0]-.004).sum()),
            maximum_river_depth_at_scale_035_m=float((r[:,0]-r[:,2]).max()*.35),
            active_region_fraction=float((r[:,4]>.5).mean()),
            full_region_fraction=float((r[:,4]==1.).mean())))
    return records


def get_sites(gl,seed):
    cells=np.asarray([(x,z,0.,0.) for z in range(-12,13) for x in range(-12,13)])
    probe=PointProbe(gl,SITES)
    data=probe.run(cells,seed=seed)
    assert np.array_equal(data,probe.run(cells,seed=seed)), 'Placement is nondeterministic'
    assert np.allclose(data[:,8:12],[PIT_RADIUS,800.,PIT_SPACING,240.]), \
        'Requested initial physical pit settings changed'
    offset=data[:,:2]-cells[:,:2]*PIT_SPACING
    assert np.all((offset>=.25*PIT_SPACING)&(offset<=.75*PIT_SPACING)), \
        'Pit centers escaped the central half of their cells'
    nearest=cKDTree(data[:,:2]).query(data[:,:2],k=2)[0][:,1]
    assert nearest.min()>=.5*PIT_SPACING-.1,'Adjacent candidate centers can coincide'
    assert np.all(np.isin(data[:,2],(0.,1.))),'Site eligibility is not whole-site binary'
    accepted=data[data[:,2]==1.]
    assert len(accepted)>=8,('Too few accepted pits in 1250km region',seed,len(accepted))
    # Dense coverage of entire disks catches misses between the inexpensive
    # admission samples and verifies the analytical river exclusion bound.
    angles=np.linspace(0.,2*np.pi,129,endpoint=False)
    radii=np.linspace(0.,PIT_RADIUS,41)
    radial,angle=np.meshgrid(radii,angles)
    queries=np.concatenate([query_points(s[:2],radial,angle,0.) for s in accepted])
    masks=PointProbe(gl,MASKS).chunks(queries,seed=seed)
    assert np.all(masks[:,1:3]==0.),'An accepted pit disk intersects a river bank'
    minimum=float(masks[:,0].min())
    assert minimum>=.8-.005,('Accepted pit leaves its broad region',minimum)
    return accepted,dict(seed=seed,cells=len(cells),accepted_pits=len(accepted),
        nominal_area_per_candidate_km2=(PIT_SPACING/1000.)**2,candidate_min_spacing_m=float(nearest.min()),
        full_disk_samples=len(queries),minimum_region_mask=minimum,
        maximum_channel_mask=float(masks[:,2].max()))


def geometry_checks(gl,sites,seed):
    probe=PointProbe(gl,GEOMETRY)
    angles=np.linspace(0.,2*np.pi,97,endpoint=False)
    radii=np.linspace(0.,1.1*PIT_RADIUS,111)
    radial,angle=np.meshgrid(radii,angles)
    data=probe.chunks(np.concatenate([query_points(s[:2],radial,angle,0.)
        for s in sites[:4]]),seed=seed)
    depth=data[:,0];r=data[:,3]
    assert np.all((depth>=0.)&(depth<=800.01)), 'Depth exceeds conical pit bounds'
    assert np.all(depth[r>=1.]<.001),'Pit extends beyond 20000m diameter'
    assert np.allclose(depth[r<.00001],800.,atol=.01),'Central pit depth is not 800m'
    near_rim=(r>=.99)&(r<1.)
    assert np.max(depth[near_rim])<1.,'Rim has an abrupt vertical entry'
    assert np.allclose(data[:,1],np.clip((1.-r)/(1.-PIT_EMPTY_RADIUS),0.,1.),atol=1e-6), \
        'Void fraction no longer has a 70-percent empty core and peripheral gradient'
    assert np.all(data[r<=PIT_EMPTY_RADIUS,1]==1.),'The inner 70 percent retains pore stone'
    # atan-based spirals must join across the negative-X angular seam.
    seam=[]
    for site in sites[:4]:
        pairs=np.concatenate([query_points(site[:2],np.linspace(.05,.95,19)*PIT_RADIUS,a,0.)
            for a in (np.pi-.00001,-np.pi+.00001)])
        values=probe.run(pairs,seed=seed)[:,0].reshape(2,-1)
        seam.append(float(np.max(abs(values[0]-values[1]))))
    assert max(seam)<.1,('Spiral angular seam has a height jump',seam)
    return dict(diameter_m=2.*PIT_RADIUS,central_depth_m=800.,empty_core_radius_m=PIT_EMPTY_RADIUS*PIT_RADIUS,geometry_samples=len(data),
        maximum_depth_m=float(depth.max()),near_rim_maximum_depth_m=float(depth[near_rim].max()),
        angular_seam_depth_difference_max_m=max(seam),pore_scale_m=240.)


def porosity_checks(gl,sites,seed):
    probe=PointProbe(gl,GEOMETRY)
    unit=qmc.Sobol(2,scramble=True,seed=48779).random_base2(11)
    chosen=sites[np.linspace(0,len(sites)-1,min(32,len(sites))).astype(int)]
    records=[]
    for radial in (0.,.35,.69,.70,.75,.80,.85,.90,.95,1.):
        queries=np.concatenate([query_points(s[:2],radial*PIT_RADIUS,
            unit[:,0]*2*np.pi,-unit[:,1]*800.) for s in chosen])
        data=probe.chunks(queries,seed=seed)
        measured=float((data[:,2]<0.).mean());target=float(np.clip((1.-radial)/(1.-PIT_EMPTY_RADIUS),0.,1.))
        assert abs(measured-target)<.035,('Perlin void gradient misses target',radial,measured,target)
        if radial<=PIT_EMPTY_RADIUS:assert np.all(data[:,2]<0.),'The inner 70 percent retains stone'
        if radial==1.:assert np.all(data[:,2]>0.),'Rim pore field removes stone'
        records.append(dict(radius_fraction=radial,target_void_fraction=target,
            measured_void_fraction=measured,samples=len(data)))
    assert all(a['measured_void_fraction']>=b['measured_void_fraction']
        for a,b in zip(records,records[1:])), 'Radial porosity is not monotonic in aggregate'
    return dict(sites=len(chosen),radial_bins=records,
        note='Isolated pore field before conical clipping; finite individual pits vary.')


def world_checks(gl,sites,seed,scale):
    probe=PointProbe(gl,WORLD)
    geometry=PointProbe(gl,GEOMETRY)
    unit=qmc.Sobol(3,scramble=True,seed=74983).random_base2(11)
    chosen=sites[np.linspace(0,len(sites)-1,8).astype(int)]
    queries=np.concatenate([query_points(s[:2],.995*PIT_RADIUS*np.sqrt(unit[:,0]),
        unit[:,1]*2*np.pi,0.) for s in chosen])
    depth=geometry.run(queries,seed=seed)[:,0]
    queries[:,1]=-depth*np.tile(.02+.96*unit[:,2],len(chosen))
    data=probe.run(queries,seed=seed,uDetailScale=scale)
    assert np.all(data[:,2]<=.002),'Pit volume adds stone'
    assert np.all(data[:,0]<=data[:,1]+.002),'Pit density raises the ground'
    wanted=data[:,1]+data[:,2]
    assert np.allclose(data[:,0],wanted,atol=.02,rtol=2e-5), \
        'Production envelope skips a requested excavation'
    carved=(data[:,0]<-1.)&(data[:,1]>1.)
    assert carved.any(),'No actual open cavity below the original ground'
    assert np.all((data[carved,7]>=data[carved,8])&(data[carved,7]<=data[carved,9])), \
        'Carved samples escape the production band'
    core=(data[:,11]<=PIT_EMPTY_RADIUS)&(data[:,1]>1.)&(depth+queries[:,1]>1.)
    assert core.any() and np.all(data[core,0]<0.),'Actual 3D leaves rock inside the empty 70-percent core'
    # The entire axis is open to the sky; below the conical floor the old
    # substrate remains. Test this in production, not only the pore helper.
    centers=np.concatenate([query_points(s[:2],0.,0.,np.array((-810.,-799.,-600.,-300.,-1.,1.,100.)))
        for s in chosen])
    c=probe.run(centers,seed=seed,uDetailScale=scale)
    axis_air=(centers[:,1]>-800.)
    assert np.all(c[axis_air,0]<0.),'Central shaft retains a roof or a solid plug'
    assert np.all(c[~axis_air,0]>0.),'Cone drills deeper than 800m'
    outside=np.concatenate([query_points(s[:2],r,unit[:128,1]*2*np.pi,y)
        for s in chosen for r,y in ((1.001*PIT_RADIUS,-10.),(1.2*PIT_RADIUS,-250.),(.5*PIT_RADIUS,-850.),(.5*PIT_RADIUS,100.))])
    out=probe.run(outside,seed=seed,uDetailScale=scale)
    assert np.allclose(out[:,0],out[:,1],atol=.02), \
        'Excavation escapes the finite pit or modifies exterior air'
    disabled=probe.run(queries[:256],seed=seed,uDetailScale=scale,uFieldMode=1)
    assert np.all(disabled[:,2]==0.),'Field-disabled mode retains pit excavation'
    index=np.flatnonzero(carved)[len(np.flatnonzero(carved))//2]
    position=queries[index,:3].copy();position[1]=data[index,7]
    return dict(detail_scale=scale,samples=len(data),carved_air_samples=int(carved.sum()),
        maximum_band_density_error=float(np.max(abs(data[:,0]-wanted))),
        maximum_world_band_m=float(data[:,5].max()),
        guaranteed_empty_core_samples=int(core.sum()),
        open_axis_samples=int(axis_air.sum()),protected_floor_samples=int((~axis_air).sum()),
        exterior_unchanged_samples=len(outside)),position


def seam_checks(gl):
    probe=PointProbe(gl,WORLD)
    queries=[]
    for axis in (0,1):
        for cell in (-5,-2,-1,0,1,3,7):
            for other in (-15731.,4173.,19751.):
                for side in (-.02,.02):
                    xz=[cell*PIT_SPACING+side,other]
                    if axis:xz.reverse()
                    queries.append((xz[0],-100.,xz[1],0.))
    data=probe.run(queries)
    assert np.all(data[:,2]==0.),'Pit excavation leaks across placement grid seams'
    assert np.all(data[:,3]==0.),'Pit footprint extends to its cell boundary'
    return dict(samples=len(queries),zero_density_delta=True)


def mixed_checks(gl):
    cells=np.asarray([(x,z,0.,0.) for z in range(-12,13) for x in range(-12,13)])
    data=PointProbe(gl,SITES).run(cells)
    accepted=data[data[:,2]==1.][:6];rejected=data[data[:,2]==0.][:6]
    assert len(accepted)==len(rejected)==6
    sites=np.concatenate((accepted,rejected))
    unit=qmc.Sobol(3,scramble=True,seed=33179).random_base2(9)
    queries=np.concatenate([query_points(s[:2],.95*PIT_RADIUS*np.sqrt(unit[:,0]),
        unit[:,1]*2*np.pi,-unit[:,2]*800.) for s in sites])
    probe=PointProbe(gl,MIXED);records=[]
    for scale in (.2,.35,1.):
        for ratio in (.35,.5,.65):
            q=queries.copy();q[:,3]=ratio
            r=probe.run(q,uBiomeOverride=-1,uDetailScale=scale,
                uBandOffset=0.,uBandOffsetVariation=0.)
            assert np.all((r[:,4]>0.)&(r[:,4]<1.)), 'Mixed branch not exercised'
            assert np.allclose(r[:,4],r[:,5],atol=1e-6), 'Surface and volume weights disagree'
            assert np.array_equal(r[:,0].view('u4'),r[:,1].view('u4')), \
                'Neighbour pilot changes this biome pit'
            assert np.all(r[:,1]<=.002),'Mixed pit adds stone'
            assert np.allclose(r[:,2],r[:,3],atol=.02,rtol=2e-5), \
                'Mixed production envelope clips an active pit'
            assert np.all(r[r[:,11]==0.,1]==0.), 'Dirty pilot creates a pit at a rejected site'
            records.append(dict(detail_scale=scale,biome_weight=float(r[0,4]),samples=len(r),
                pilot_independent=True,maximum_band_density_error=float(abs(r[:,2]-r[:,3]).max()),
                changed_samples=int((r[:,1]<-.01).sum())))
    return dict(cases=records,global_offset_m=0.,
        note='Constructed mixed columns use actual matrix weights/storage; the neighbour has zero band and an unrelated pilot.')


def render_figure(gl,path,report,sites,seed):
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    from render_biome_landforms import isosurface,draw_projected_mesh
    center=sites[np.argmin(np.sum(sites[:,:2]**2,axis=1)),:2]
    spacing=100.;size=261;extent=spacing*(size-1)
    axis=(np.arange(size)-(size-1)*.5)*spacing
    xx,zz=np.meshgrid(center[0]+axis,center[1]+axis)
    queries=np.column_stack((xx.ravel(),np.zeros(xx.size),zz.ravel(),np.zeros(xx.size)))
    height=PointProbe(gl,HEIGHT).run(queries,seed=seed).reshape(size,size,16)
    low=float(height[:,:,12].min()-820.);high=float(height[:,:,12].max()+20.)
    sample=DensityProbe(gl).sample(8,(float(xx[0,0]),float(zz[0,0])),spacing,
        low,(high-low)/96.,size=size,depth=97,seed=seed,**SCENE_OPTIONS)
    fig=plt.figure(figsize=(14,10),constrained_layout=True)
    gs=fig.add_gridspec(2,2)
    ax=fig.add_subplot(gs[0,0]);draw_projected_mesh(ax,sample,isosurface(sample))
    ax.set_title('Gouffre de 20 km : densité GLSL réelle',fontsize=11)
    geometry=PointProbe(gl,GEOMETRY).run(queries,seed=seed).reshape(size,size,16)
    ax=fig.add_subplot(gs[0,1]);im=ax.imshow(-geometry[:,:,0],origin='lower',cmap='terrain',
        extent=(-extent/2000.,extent/2000.,-extent/2000.,extent/2000.))
    ax.set_xlabel('km');ax.set_ylabel('km');ax.set_title('Enveloppe conique et spirale avant porosité')
    fig.colorbar(im,ax=ax,label='Profondeur (m)')
    row=size//2
    ax=fig.add_subplot(gs[1,0]);ax.imshow(sample['density'][row]>0.,origin='lower',
        extent=(-extent/2.,extent/2.,low,high),cmap='gray_r',interpolation='nearest',aspect='auto')
    ax.plot(axis,height[row,:,12],color='#b47734',lw=.9,label='Sol avant gouffre')
    ax.plot(axis,height[row,:,12]-geometry[row,:,0],color='#5a8794',lw=.9,label='Fond conique')
    ax.set_xlabel('Distance au centre (m)');ax.set_ylabel('Altitude (m)')
    ax.set_title('Coupe réelle : blanc = vide, noir = solide');ax.legend(fontsize=8)
    ax=fig.add_subplot(gs[1,1]);bins=report['porosity']['radial_bins']
    ax.plot([r['radius_fraction']*PIT_RADIUS/1000. for r in bins],
        [r['measured_void_fraction']*100. for r in bins],'o-',label='Perlin GLSL mesuré')
    ax.plot([0,PIT_EMPTY_RADIUS*PIT_RADIUS/1000.,PIT_RADIUS/1000.],[100,100,0],'--',label='Cible')
    ax.set_xlabel('Distance au centre (km)');ax.set_ylabel('Vide (%)')
    ax.set_title('Gradient radial avant découpe par le cône');ax.grid(alpha=.2);ax.legend()
    fig.suptitle('Forêt froide — gouffre spiralé et parois poreuses\n'
        'Reconstruction diagnostique, pas une capture du jeu.',fontsize=14)
    fig.savefig(path,dpi=120);plt.close(fig)
    return dict(seed=seed,center_xz_m=center.tolist(),horizontal_extent_m=extent,
        horizontal_samples=size,vertical_samples=97,spacing_m=spacing)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,default=Path('docs/biome-validation/cold_forest.json'))
    parser.add_argument('--no-render',action='store_true')
    parser.add_argument('--render-only',action='store_true')
    args=parser.parse_args();fingerprint=source_fingerprint(8)
    if args.render_only:
        report=json.loads(args.output.read_text())
        assert report['source_sha256']==fingerprint,'Report source fingerprint is stale'
        with GL() as gl:
            sites,_=get_sites(gl,1337)
            report['figure']=render_figure(gl,args.output.with_suffix('.png'),report,sites,1337)
        args.output.write_text(json.dumps(report,indent=2)+'\n')
        return
    with GL() as gl:
        report=dict(renderer=gl.renderer,source_sha256=fingerprint)
        print('Checking preserved ground and downward regional river mask...',flush=True)
        report['ground']=height_checks(gl)
        print('Checking 50-km cells and whole-pit regional/river exclusion...',flush=True)
        sites,first=get_sites(gl,1337);_,second=get_sites(gl,97)
        report['placement']=[first,second]
        print('Checking conical envelope, spiral seam and radial Perlin void...',flush=True)
        report['geometry']=geometry_checks(gl,sites,1337)
        report['porosity']=porosity_checks(gl,sites,1337)
        report['placement_seams']=seam_checks(gl)
        report['world']=[]
        for scale in (.2,.35,1.):
            print(f'Checking actual field integration at scale {scale}...',flush=True)
            metrics,position=world_checks(gl,sites,1337,scale);report['world'].append(metrics)
            if scale==.35:
                density=DensityProbe(gl)
                sample=density.sample(8,(float(position[0]-8),float(position[2]-8)),2.,
                    float(position[1]-8),2.,size=9,depth=9,seed=1337,**SCENE_OPTIONS)
                report['targeted_pipeline']=check_targeted_pipeline(FieldRunner(gl),density,sample,8,1337)
        print('Checking mixed controls and column bounds...',flush=True)
        report['mixed']=mixed_checks(gl)
        args.output.parent.mkdir(parents=True,exist_ok=True)
        if not args.no_render:
            print('Reconstructing the actual GLSL density...',flush=True)
            report['figure']=render_figure(gl,args.output.with_suffix('.png'),report,sites,1337)
        assert source_fingerprint(8)==fingerprint,'Sources changed during validation; rerun'
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report,indent=2))


if __name__=='__main__':main()
