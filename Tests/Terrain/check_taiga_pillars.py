#!/usr/bin/env python3
"""Measure Taiga cylinders using the actual GLSL recipes on headless OpenGL.

--reference-shaders must name the pre-pillar ProceduralTerrain snapshot. The
reference is never approximated by deleting parts of the new implementation.
The diagnostic mesh is extracted from sampled density, not a hand-made model.
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
from check_biomes import BiomeProbe, profile_probe_source
from check_biome_landforms import (DensityProbe, SCENE_OPTIONS,
    check_targeted_pipeline, source_fingerprint)
from check_gpu import SHADERS, FieldRunner
from field_batch_runner import default_uniforms
from gl_headless import GL
from render_biome_landforms import isosurface, draw_projected_mesh


HEADER = r'''
layout(local_size_x=8,local_size_y=8) in;
struct ProbeSample { vec4 a; vec4 b; vec4 c; vec4 d; };
layout(std430,binding=6) writeonly buffer ProbeOutput { ProbeSample outputValues[]; };
uniform uint uProbeSize;
uniform vec2 uProbeOrigin;
uniform float uProbeSpacing;
'''
HEIGHT = HEADER + r'''
void main() {
 uvec2 i=gl_GlobalInvocationID.xy;
 if(any(greaterThanEqual(i,uvec2(uProbeSize)))) return;
 vec2 p=uProbeOrigin+vec2(i)*uProbeSpacing;
 FieldColumn c=fieldProbeColumn(p);
 outputValues[i.x+i.y*uProbeSize]=ProbeSample(c.terrain,fieldRelief(p),c.blend,vec4(0));
}
'''
UNION = HEADER + r'''
void main() {
 uvec2 i=gl_GlobalInvocationID.xy;
 if(any(greaterThanEqual(i,uvec2(uProbeSize)))) return;
 vec2 p=uProbeOrigin+vec2(i)*uProbeSpacing;
 FieldColumn c=fieldProbeColumn(p);
 const float offsets[8]=float[8](-1000.,-300.,-30.,-1.,1.,150.,500.,1000.);
 vec4 a,b;
 for(uint j=0u;j<8u;++j) {
  float d=fieldProbeDensity(vec3(p.x,c.terrain.x+offsets[j],p.y),c);
  if(j<4u) a[j]=d; else b[j-4u]=d;
 }
 outputValues[i.x+i.y*uProbeSize]=ProbeSample(c.terrain,a,b,c.blend);
}
'''

POINT_HEADER = r'''
layout(local_size_x=8,local_size_y=8) in;
layout(std430,binding=9) readonly buffer ProbeInput { vec4 queries[]; };
layout(std430,binding=10) readonly buffer ProbeSites { vec4 sites[]; };
layout(std430,binding=6) writeonly buffer ProbeOutput { vec4 result[]; };
uniform uint uCount;
TaigaPillarSite probeSite(uint i) {
 vec4 a=sites[3u*i],b=sites[3u*i+1u];
 uint key=uint(b.z)|(uint(b.w)<<16u);
 return TaigaPillarSite(a.xy,key,a.z,a.w,b.x,b.y);
}
'''
SITES = POINT_HEADER + r'''
void main() {
 uint i=gl_GlobalInvocationID.x+gl_GlobalInvocationID.y*gl_NumWorkGroups.x*8u;
 if(i>=uCount) return;
 ivec2 bin=ivec2(queries[i].xy); int j=int(queries[i].z);
 for(uint k=0u;k<4u;++k) result[4u*i+k]=vec4(0);
 if(j>=taigaPillarBinCount(bin)) return;
 TaigaPillarSite s=taigaPillarInBin(bin,j);
 result[4u*i]=vec4(s.center,s.radius,s.height);
 result[4u*i+1u]=vec4(s.tilt,s.azimuth,float(s.key&65535u),float(s.key>>16u));
 FieldColumn c=fieldProbeColumn(s.center);
 result[4u*i+2u]=vec4(c.terrain.x,c.terrain.w,1.,taigaBaseRelief(s.center).x*uDetailScale);
 result[4u*i+3u]=vec4(bin,j,0);
}
'''
GEOMETRY = POINT_HEADER + r'''
void main() {
 uint i=gl_GlobalInvocationID.x+gl_GlobalInvocationID.y*gl_NumWorkGroups.x*8u;
 if(i>=uCount) return;
 vec4 q=queries[i]; uint id=uint(q.w);
 TaigaPillarSite s=probeSite(id); float anchor=sites[3u*id+2u].x;
 float d=taigaPillarGeometry(q.xyz,s,anchor);
 result[4u*i]=vec4(d,taigaPillarEnvelope(taigaPillarLocal(q.xyz,s,anchor),s),
     taigaPillarLength(s),taigaPillarAnchorHeight(s));
 for(uint k=1u;k<4u;++k) result[4u*i+k]=vec4(0);
}
'''
WORLD = POINT_HEADER + r'''
void main() {
 uint i=gl_GlobalInvocationID.x+gl_GlobalInvocationID.y*gl_NumWorkGroups.x*8u;
 if(i>=uCount) return;
 vec4 q=queries[i]; uint id=uint(q.w);
 TaigaPillarSite s=probeSite(id); float anchor=sites[3u*id+2u].x;
 FieldColumn c=fieldProbeColumn(q.xz);
 float base=c.terrain.x-q.y;
 float g=taigaPillarGeometry(q.xyz,s,anchor);
 result[4u*i]=vec4(fieldProbeDensity(q.xyz,c),base,g,c.terrain.x);
 result[4u*i+1u]=vec4(fieldProbeVolume(c).xyz,c.terrain.w);
 result[4u*i+2u]=vec4(fieldVolume(q.xyz,c),0,0,0);
 result[4u*i+3u]=vec4(0);
}
'''


class PointProbe:
    def __init__(self, gl, source):
        self.gl=gl
        d=SHADERS/'biomes'/'Taiga'
        relief=biome_recipe_source(d/'relief.C.glsl')
        volume=re.sub(r'^#version[^\n]*\n','',biome_recipe_source(d/'volume.C.glsl'),count=1)
        self.program=gl.compile([(0x91B9,relief+volume+profile_probe_source()+source)],'Taiga point validation')
        self.inputs=gl.buffer(0);self.sites=gl.buffer(0);self.output=gl.buffer(0)
        self.mapping=gl.buffer(packed_biome_cells())

    def run(self, queries, sites=None, seed=1337, **options):
        queries=np.asarray(queries,dtype='<f4').reshape(-1,4)
        if sites is None:sites=np.zeros((1,3,4),dtype='<f4')
        gl=self.gl;count=len(queries)
        uniforms=default_uniforms(64,uBiomeOverride=3,**(SCENE_OPTIONS|options))
        uniforms.update(uSeed=('1ui',seed),uCount=('1ui',count))
        for name,args in uniforms.items():gl.uniform(self.program,name,*args)
        gl.resize(self.inputs,queries.tobytes());gl.bind(self.inputs,9)
        gl.resize(self.sites,np.asarray(sites,dtype='<f4').tobytes());gl.bind(self.sites,10)
        gl.resize(self.output,bytes(count*64));gl.bind(self.output,6)
        gl.bind(self.mapping,7)
        gl.dispatch_extent(self.program,8,math.ceil(count/8))
        data=np.frombuffer(gl.read(self.output,count*64),dtype='<f4').copy().reshape(count,16)
        assert np.isfinite(data).all(),'Nonfinite Taiga geometry sample'
        gl.check('Taiga point validation')
        return data


def values(probe, **options):
    r=probe.run(biome=3,**options)
    return np.asarray(r['values'],dtype=np.float32).reshape(r['size'],r['size'],16)


def height_preservation(gl, reference):
    after=BiomeProbe(gl,source=HEIGHT);before=BiomeProbe(gl,directory=reference,source=HEIGHT)
    count=0;records=[]
    for seed,origin,scale in ((1337,(-30000.,-30000.),.35),(97,(-17000.,6000.),.2),
                              (98765,(12000.,-13000.),1.0)):
        options=dict(size=193,origin=origin,spacing=317.,uSeed=seed,**(SCENE_OPTIONS|{'uDetailScale':scale}))
        a=values(after,**options);b=values(before,**options)
        for component,label in ((0,'packed world height'),(4,'raw h'),(6,'recipe offset'),(7,'pilot')):
            assert np.array_equal(a[:,:,component].view('u4'),b[:,:,component].view('u4')),(label,'changed',seed)
        count+=193**2
        records.append(dict(seed=seed,origin_m=origin,spacing_m=317.,size=193,detail_scale=scale))
    # Coarse positions at several levels: the new operation must only add.
    union=BiomeProbe(gl,source=UNION);added=0;union_count=0
    for seed in (1337,97,98765):
        a=values(union,size=97,origin=(-16000.,-16000.),spacing=347.,uSeed=seed,**SCENE_OPTIONS)
        base=-np.asarray((-1000.,-300.,-30.,-1.,1.,150.,500.,1000.))
        d=a[:,:,4:12]
        assert np.all(d>=base-2.e-3),('Original terrain was carved',seed,float(np.min(d-base)))
        added+=int(np.count_nonzero(d>base+.1));union_count+=d.size
    assert added>0,'No added pillars at all'
    return dict(bit_exact_height_samples=count,maps=records,addition_only_samples=union_count,
                materially_added_density_samples=added)


def layout_metrics():
    source=(SHADERS/'biomes/Taiga/pillar_sites.glsl').read_text()
    body=re.search(r'TAIGA_PILLAR_POINTS\[\d+\].*?\((.*?)\);',source,re.S).group(1)
    packed=np.asarray([int(x,16) for x in re.findall(r'0x([0-9a-fA-F]+)u',body)],dtype=np.uint32)
    points=np.column_stack((packed&65535,packed>>16)).astype(float)*(48000./65536.)
    tree=cKDTree(points,boxsize=48000.)
    distances,ids=tree.query(points,k=2);nearest=distances[:,1]
    assert nearest.min()>=1000. and nearest.max()<=1500.,('Nearest foot spacing',nearest.min(),nearest.max())
    v=points[ids[:,1]]-points;v=(v+24000.)%48000.-24000.
    angles=np.arctan2(v[:,1],v[:,0])
    # A square/hexagonal arrangement has a strong 4th/6th angular harmonic.
    harmonics={str(k):float(abs(np.mean(np.exp(1j*k*angles)))) for k in (2,4,6,8)}
    assert max(harmonics.values())<.15,('Directional lattice signature',harmonics)
    return dict(points_per_tile=len(points),tile_period_m=48000.,
        nearest_foot_distance_min_m=float(nearest.min()),nearest_foot_distance_max_m=float(nearest.max()),
        nearest_direction_harmonics=harmonics),points


def get_sites(gl,seed,**options):
    queries=[(x,z,j,0) for z in range(-6,7) for x in range(-6,7) for j in range(3)]
    out=PointProbe(gl,SITES).run(queries,seed=seed,**options)
    return out[out[:,10]>.5,:12].reshape(-1,3,4)


def geometry_parameters(site):
    center=site[0,:2].astype(float);r,h=map(float,site[0,2:]);tilt,azimuth=map(float,site[1,:2])
    st,ct=np.sin(tilt),np.cos(tilt);sa,ca=np.sin(azimuth),np.cos(azimuth)
    axis=np.array((st*ca,ct,st*sa))
    side=np.array((-sa,0.,ca));other=np.cross(axis,side)
    # Axial length chosen so the highest point on the tilted top rim is h.
    length=(h-r*st)/ct
    foot=np.array((center[0],float(site[2,0]),center[1]))
    return foot,r,h,axis,side,other,length


def world_points(site,local):
    foot,r,h,axis,side,other,length=geometry_parameters(site)
    return foot+local[:,0,None]*r*side+local[:,1,None]*length*axis+local[:,2,None]*r*other


def geometry_checks(gl,sites,seed):
    probe=PointProbe(gl,GEOMETRY)
    # Distinct directions/tilts, including narrow/wide and low/high columns.
    choice=np.unique(np.r_[np.linspace(0,len(sites)-1,16).astype(int),
        np.argmin(sites[:,1,0]),np.argmax(sites[:,1,0]),np.argmax(sites[:,0,2]),np.argmax(sites[:,0,3])])
    chosen=sites[choice]
    q=qmc.Sobol(3,scramble=True,seed=713918).random_base2(17)
    theta=q[:,0]*2*np.pi;radius=np.sqrt(q[:,1])
    local=np.column_stack((radius*np.cos(theta),q[:,2],radius*np.sin(theta)))
    records=[]
    for i,s in enumerate(chosen):
        xyz=world_points(s,local);query=np.column_stack((xyz,np.full(len(xyz),i)))
        actual=probe.run(query,chosen,seed=seed);d=actual[:,0];void=float(np.mean(d<0))
        assert .295<=void<=.305,('Per-cylinder void fraction',i,void)
        foot,r,h,axis,side,other,length=geometry_parameters(s)
        assert 100<=r<=200 and 500<=h<=1000
        assert 0<=s[1,0]<=np.pi/4 and 0<=s[1,1]<=2*np.pi
        assert np.isclose(length*axis[1]+r*np.sin(s[1,0]),h,atol=.001)
        assert np.allclose(actual[:,2],length,rtol=0,atol=.001),'Actual axial length differs from vertical-height oracle'
        # Separately compiled shader programs may reassociate internal noise
        # arithmetic by a few FP32 ULPs; require sub-millimetre anchoring.
        anchor_error=float(np.max(np.abs(actual[:,3]-s[2,0])))
        assert anchor_error<.001,('Rigid foot anchor differs from unchanged terrain',anchor_error)
        records.append(dict(center_xz_m=s[0,:2].tolist(),diameter_m=2*r,
            vertical_height_m=h,axial_length_m=float(length),tilt_degrees=float(np.degrees(s[1,0])),
            azimuth_degrees=float(np.degrees(s[1,1])),isolated_void_fraction=void,
            anchor_height_error_m=anchor_error))
    # Points beyond cylinder side or either cap must never contain stone.
    theta=np.linspace(0,2*np.pi,97,endpoint=False)
    outside=[]
    for y in (-.01,.25,.75,1.01):
        radius=1.01 if 0<y<1 else .8
        outside.append(np.column_stack((radius*np.cos(theta),np.full(len(theta),y),radius*np.sin(theta))))
    outside=np.concatenate(outside)
    for i,s in enumerate(chosen):
        xyz=world_points(s,outside)
        d=probe.run(np.column_stack((xyz,np.full(len(xyz),i))),chosen,seed=seed)[:,0]
        assert np.all(d<0),('Stone escapes perfect cylinder',i,float(d.max()))
        # Side surface is a circle in a plane orthogonal to the requested axis;
        # independently transformed points must be on the actual GLSL envelope.
        rim=np.column_stack((np.cos(theta),np.full(len(theta),.5),np.sin(theta)))
        xyz=world_points(s,rim)
        envelope=probe.run(np.column_stack((xyz,np.full(len(xyz),i))),chosen,seed=seed)[:,1]
        assert np.max(np.abs(envelope))<.01,('Diameter/orientation is not a rigid cylinder',i,float(abs(envelope).max()))
    return dict(uniform_volume_samples_per_column=len(local),tested_columns=records,
        void_fraction_min=min(r['isolated_void_fraction'] for r in records),
        void_fraction_max=max(r['isolated_void_fraction'] for r in records),
        exterior_samples=len(chosen)*len(outside)),chosen


def world_checks(gl,sites,seed,detail_scale=.35):
    probe=PointProbe(gl,WORLD)
    q=qmc.Sobol(3,scramble=True,seed=944317).random_base2(11)
    theta=q[:,0]*2*np.pi;radius=np.sqrt(q[:,1])*.99
    local=np.column_stack((radius*np.cos(theta),q[:,2],radius*np.sin(theta)))
    queries=[]
    for i,s in enumerate(sites):
        xyz=world_points(s,local)
        queries.append(np.column_stack((xyz,np.full(len(xyz),i))))
    queries=np.concatenate(queries)
    a=probe.run(queries,sites,seed=seed,uDetailScale=detail_scale)
    assert np.all(a[:,0]>=a[:,1]-2.e-3),'Production union removes original ground'
    above=(a[:,1]<-1.)&(a[:,2]>1.)
    assert above.any(),'No pillar volume above actual ground'
    missing=above&(a[:,0]<=0)
    assert not missing.any(),('Actual envelope clips pillar stone',int(missing.sum()),
        queries[missing][:4].tolist(),a[missing][:4].tolist())
    envelope_ok=(queries[:,1]>=a[:,5])&(queries[:,1]<=a[:,6])
    assert np.all(envelope_ok[above]),'Pillar outside conservative field envelope'
    # Choose a true new surface neighbourhood, for actual accumulation and LOD.
    candidates=np.flatnonzero(above)
    target=candidates[np.argmin(a[candidates,2])]
    position=queries[target,:3]
    pipeline=None
    if detail_scale==SCENE_OPTIONS['uDetailScale']:
        density=DensityProbe(gl)
        sample=density.sample(3,(float(position[0]-8),float(position[2]-8)),2.,
            float(position[1]-8),2.,size=9,depth=9,seed=seed,**SCENE_OPTIONS)
        pipeline=check_targeted_pipeline(FieldRunner(gl),density,sample,3,seed)
    return dict(detail_scale=detail_scale,world_samples=len(queries),above_ground_stone_samples=int(above.sum()),
        clipped_stone_samples=int(missing.sum()),site_anchor_height_range_m=[float(sites[:,2,0].min()),float(sites[:,2,0].max())],
        site_local_relief_range_m=[float(sites[:,2,3].min()),float(sites[:,2,3].max())],
        maximum_actual_world_band_m=float(a[:,7].max()),
        targeted_pipeline=pipeline)


def mesh_sample(gl,site,seed,spacing=12.):
    foot,r,h,axis,side,other,length=geometry_parameters(site)
    points=np.array([foot,foot+axis*length]);pad=r+24.
    low=points.min(axis=0)-pad;high=points.max(axis=0)+pad
    width=max(high[0]-low[0],high[2]-low[2]);size=int(np.ceil(width/spacing))+1
    center=(low+high)*.5;origin=(float(center[0]-width/2),float(center[2]-width/2))
    depth=int(np.ceil((high[1]-low[1])/spacing))+1
    x=origin[0]+np.arange(size)*spacing;z=origin[1]+np.arange(size)*spacing
    y=low[1]+np.arange(depth)*spacing
    zz,yy,xx=np.meshgrid(z,y,x,indexing='ij')
    queries=np.column_stack((xx.ravel(),yy.ravel(),zz.ravel(),np.zeros(xx.size)))
    probe=PointProbe(gl,GEOMETRY)
    chunks=[probe.run(queries[i:i+131072],site[None],seed=seed)[:,0]
            for i in range(0,len(queries),131072)]
    return dict(density=np.concatenate(chunks).reshape(size,depth,size),
        columns=np.full((size,size,4),foot[1]),origin=origin,spacing=spacing,
        min_y=float(low[1]),step_y=spacing)


def render(path,gl,sites,seed,points,geometry):
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    fig=plt.figure(figsize=(16,10),constrained_layout=True)
    gs=fig.add_gridspec(2,4,height_ratios=(2.,1.))
    ids=[int(np.argmin(sites[:,1,0])),int(np.argmin(abs(sites[:,1,0]-.38))),
         int(np.argmax(sites[:,1,0])),int(np.argmax(sites[:,0,2]))]
    for panel,i in enumerate(ids):
        site=sites[i];sample=mesh_sample(gl,site,seed)
        triangles=isosurface(sample);ax=fig.add_subplot(gs[0,panel])
        draw_projected_mesh(ax,sample,triangles,close=False)
        ax.set_title(f'Ø {2*site[0,2]:.0f} m · H {site[0,3]:.0f} m\n'
                     f'inclinaison {np.degrees(site[1,0]):.1f}° · azimut {np.degrees(site[1,1]):.0f}°',fontsize=10)
    ax=fig.add_subplot(gs[1,:2]);visible=points[(points[:,0]<12000)&(points[:,1]<12000)]
    ax.scatter(visible[:,0]/1000,visible[:,1]/1000,s=10,color='#426677')
    ax.set_aspect('equal');ax.set_xlabel('km');ax.set_ylabel('km');ax.set_title('Pieds : extrait 12 × 12 km, sans grille locale')
    ax=fig.add_subplot(gs[1,2:]);records=geometry['tested_columns']
    ax.bar(np.arange(len(records)),[100*r['isolated_void_fraction'] for r in records],color='#426677')
    ax.axhline(30,color='#a24137',lw=1);ax.set_ylim(28,32);ax.set_ylabel('Vide mesuré (%)');ax.set_xlabel('Cylindre indépendant')
    ax.set_title('Perlin 3D : fraction de vide dans le cylindre complet')
    fig.suptitle('Taïga — volumes GLSL réels, enveloppes cylindriques inclinées\n'
        'Reconstruction diagnostique ; cylindres isolés avant union au terrain. Pas une capture du jeu.',fontsize=13)
    fig.savefig(path,dpi=145);plt.close(fig)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--reference-shaders',required=True,type=Path)
    parser.add_argument('--output',type=Path,default=Path('docs/biome-validation'))
    parser.add_argument('--no-render',action='store_true')
    args=parser.parse_args();args.output.mkdir(parents=True,exist_ok=True)
    seed=1337;gl=GL();fingerprint=source_fingerprint(3)
    report=dict(renderer=gl.renderer,source_sha256=fingerprint,seed=seed,
        reference_shaders=str(args.reference_shaders),
        notes=['Distances mean nearest neighbouring foot centers, including tile seams.',
               '30% void refers to the full cylinder; existing ground fills buried/intersecting voids.',
               'Height is maximum world-vertical extent above the foot center, not axial length.',
               'Offline OpenGL probes and reconstructed meshes do not certify Windows GPU speed or gameplay.'])
    print('Checking unchanged height and addition-only union...',flush=True)
    report['height_and_union']=height_preservation(gl,args.reference_shaders)
    report['placement'],points=layout_metrics()
    print('Sampling rigid cylinder geometry and porosity...',flush=True)
    sites=get_sites(gl,seed)
    report['geometry'],chosen=geometry_checks(gl,sites,seed)
    print('Checking actual surface anchoring, envelopes and LOD...',flush=True)
    # Include highest/lowest anchors in addition to shape extremes.
    test_sites=np.concatenate((chosen,sites[[np.argmin(sites[:,2,0]),np.argmax(sites[:,2,0]),np.argmax(sites[:,2,3])]]))
    report['world']=world_checks(gl,test_sites,seed)
    report['world_other_detail_scales']=[]
    for scale in (.2,1.):
        scaled=get_sites(gl,seed,uDetailScale=scale)
        indices=[int(np.argmin(np.linalg.norm(scaled[:,0,:2]-s[0,:2],axis=1))) for s in chosen]
        indices+=list(map(int,(np.argmin(scaled[:,2,0]),np.argmax(scaled[:,2,0]),np.argmax(scaled[:,2,3]))))
        report['world_other_detail_scales'].append(world_checks(gl,scaled[indices],seed,scale))
    if not args.no_render:
        print('Rendering sampled 3D columns...',flush=True)
        render(args.output/'taiga_pillars.png',gl,chosen,seed,points,report['geometry'])
    assert source_fingerprint(3)==fingerprint,'Sources changed during measurement; rerun validation'
    (args.output/'taiga_pillars.json').write_text(json.dumps(report,indent=2)+'\n')
    gl.close();print(json.dumps(report,indent=2))


if __name__=='__main__':main()
