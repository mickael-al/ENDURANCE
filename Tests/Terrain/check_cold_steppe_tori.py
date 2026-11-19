#!/usr/bin/env python3
"""Measure ColdSteppe's actual GLSL torus envelopes, pores and field integration.

Samples execute the production shader helpers. Geometry oracles below are only
the mathematical torus dimensions and requested limits, not copied shader code.
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
from render_biome_landforms import isosurface, draw_projected_mesh


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
 SteppeTorusSite s=steppeTorusInCell(cell);
 result[4u*i]=vec4(s.center,steppeTorusEligibility(s),steppeMountainPresence(s.center));
 result[4u*i+1u]=vec4(float(s.key&65535u),float(s.key>>16u),cell);
 result[4u*i+2u]=vec4(STEPPE_TORUS_RADIUS,STEPPE_TORUS_TUBE_RADIUS,
                     STEPPE_TORUS_BURIED_FRACTION,STEPPE_TORUS_SPACING);
 result[4u*i+3u]=vec4(0);
}
'''

GEOMETRY = r'''
void main() {
 uint i=gl_GlobalInvocationID.x+gl_GlobalInvocationID.y*gl_NumWorkGroups.x*8u;
 if(i>=uCount)return;
 vec3 p=queries[i].xyz;
 SteppeTorusSite s=steppeTorusSite(p.xz);
 vec3 q=steppeTorusLocal(p,s,0.);
 result[4u*i]=vec4(steppeTorusGeometry(p,s,0.),steppeTorusEnvelope(q),q.y,
                  steppeMountainPresence(p.xz));
 result[4u*i+1u]=vec4(q,steppeTorusFootprint(p.xz,s));
 result[4u*i+2u]=vec4(s.center,float(s.key&65535u),float(s.key>>16u));
 result[4u*i+3u]=vec4(0);
}
'''

WORLD = r'''
void main() {
 uint i=gl_GlobalInvocationID.x+gl_GlobalInvocationID.y*gl_NumWorkGroups.x*8u;
 if(i>=uCount)return;
 vec3 p=queries[i].xyz;
 SteppeTorusSite s=steppeTorusSite(p.xz);
 FieldColumn c=fieldProbeColumn(p.xz);
 p.y+=c.terrain.x;
 float base=c.terrain.x-p.y;
 result[4u*i]=vec4(fieldProbeDensity(p,c),base,
                  steppeTorusGeometry(p,s,c.terrain.x),c.terrain.x);
 result[4u*i+1u]=vec4(fieldProbeVolume(c).xyz,c.terrain.w);
 result[4u*i+2u]=vec4(fieldVolume(p,c),c.blend.w,steppeMountainPresence(p.xz),p.y);
 result[4u*i+3u]=vec4(s.center,0.,0.);
}
'''

PRESENCE = r'''
void main() {
 uint i=gl_GlobalInvocationID.x+gl_GlobalInvocationID.y*gl_NumWorkGroups.x*8u;
 if(i>=uCount)return;
 vec2 p=queries[i].xz;
 result[4u*i]=vec4(steppeMountainPresence(p),0.,0.,0.);
 for(uint j=1u;j<4u;++j)result[4u*i+j]=vec4(0.);
}
'''

MIXED = r'''
void main() {
 uint i=gl_GlobalInvocationID.x+gl_GlobalInvocationID.y*gl_NumWorkGroups.x*8u;
 if(i>=uCount)return;
 vec3 p=queries[i].xyz;
 FieldColumn c=fieldProbeColumn(p.xz);
 // Actual matrix edge between ColdSteppe (6) and BorealForest (7), above
 // the marine slice. The synthetic neighbour contributes no volume band.
 const uint cell=21u;
 uvec4 mapping=fieldBiomeCells[cell];
 c.terrain.yz=vec2((1.+queries[i].w)*.25,.25);
 c.blend.x=.5;
 c.detail=uvec4(mapping.x|(cell<<20u),mapping.yz,0u);
 float surfaceWeight=fieldSurfaceWeight(c,6u);
 float volumeWeight=fieldVolumeWeight(c,6u);
 c.terrain.w*=surfaceWeight;
 c=fieldUnpackColumn(fieldPackColumn(c,c.terrain.w/uDetailScale));
 p.y+=c.terrain.x;
 float base=c.terrain.x-p.y;
 c.blend.w=0.; float zeroPilot=fieldVolume(p,c);
 c.blend.w=.93; float dirtyPilot=fieldVolume(p,c);
 vec2 envelope=fieldVolumeEnvelope(c);
 float wanted=base+volumeWeight*dirtyPilot;
 float actual=base;
 if(c.terrain.w>.0002 && p.y>=envelope.x && p.y<=envelope.y)
  actual+=volumeWeight*dirtyPilot;
 result[4u*i]=vec4(actual,wanted,zeroPilot,dirtyPilot);
 result[4u*i+1u]=vec4(surfaceWeight,volumeWeight,c.terrain.w,base);
 result[4u*i+2u]=vec4(envelope,p.y,steppeTorusEligibility(steppeTorusSite(p.xz)));
 result[4u*i+3u]=vec4(0.);
}
'''


class PointProbe:
    def __init__(self, gl, source):
        self.gl = gl
        directory = SHADERS / 'biomes' / 'ColdSteppe'
        relief = biome_recipe_source(directory / 'relief.C.glsl')
        volume = re.sub(r'^#version[^\n]*\n', '',
            biome_recipe_source(directory / 'volume.C.glsl'), count=1)
        self.program = gl.compile([(0x91B9,
            relief + volume + profile_probe_source() + HEADER + source)],
            'ColdSteppe torus measurement')
        self.inputs = gl.buffer(0)
        self.output = gl.buffer(0)
        self.mapping = gl.buffer(packed_biome_cells())

    def run(self, queries, seed=1337, **options):
        queries = np.asarray(queries, dtype='<f4').reshape(-1, 4)
        gl = self.gl
        count = len(queries)
        uniforms = default_uniforms(64,
            **({'uBiomeOverride':6} | SCENE_OPTIONS | options))
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
        assert np.isfinite(data).all(), 'Nonfinite ColdSteppe torus sample'
        gl.check('ColdSteppe torus measurement')
        return data


def query_points(center, radius, angle, height):
    radius, angle, height = np.broadcast_arrays(radius, angle, height)
    return np.column_stack((center[0] + radius.ravel() * np.cos(angle.ravel()),
        height.ravel(), center[1] + radius.ravel() * np.sin(angle.ravel()),
        np.zeros(radius.size)))


def get_sites(gl, seed):
    cells = np.asarray([(x, z, 0., 0.) for z in range(-12, 13)
                        for x in range(-12, 13)])
    probe = PointProbe(gl, SITES)
    result = probe.run(cells, seed=seed)
    repeated = probe.run(cells, seed=seed)
    assert np.array_equal(result, repeated), 'Placement is not deterministic'
    assert np.allclose(result[:, 8:12], [1000., 50., .25, 10000.]), \
        'Requested physical torus dimensions changed'
    offset = result[:, :2] - cells[:, :2] * 10000.
    assert np.all((offset >= 2500.) & (offset <= 7500.)), \
        'Torus centers escaped central-half cell jitter'
    nearest = cKDTree(result[:, :2]).query(result[:, :2], k=2)[0][:, 1]
    assert nearest.min() >= 5000. - .03, 'Neighbor rings are too close'
    assert np.all(np.isin(result[:, 2], (0., 1.))), \
        'Eligibility must be constant for each whole ring'
    accepted = result[result[:, 2] == 1.]
    assert len(accepted) >= 8, 'No usable plain rings in a 250 km region'
    # Check the whole disk, not only the sparse acceptance samples. This catches
    # a ring crossing a mountain tongue between two eligibility probes.
    angles = np.linspace(0., 2. * np.pi, 193, endpoint=False)
    radii = np.arange(0., 1050.1, 70.)
    r, a = np.meshgrid(radii, angles)
    checks = np.concatenate([query_points(site[:2], r, a, 0.) for site in accepted])
    presence = PointProbe(gl, PRESENCE)
    chunks = [presence.run(checks[i:i + 65536], seed=seed)[:, 0]
              for i in range(0, len(checks), 65536)]
    maximum = float(np.max(np.concatenate(chunks)))
    assert maximum == 0., ('Accepted ring overlaps a mountain region', maximum)
    return accepted, dict(seed=seed,placement_cells=len(cells),accepted_plain_sites=len(accepted),
        nominal_area_per_candidate_km2=100.,candidate_center_min_spacing_m=float(nearest.min()),
        accepted_disk_presence_samples=len(checks),maximum_mountain_presence=maximum)


def envelope_checks(gl, sites, seed):
    probe = PointProbe(gl, GEOMETRY)
    theta = np.linspace(0., 2. * np.pi, 73, endpoint=False)
    phi = np.linspace(0., 2. * np.pi, 37, endpoint=False)
    theta, phi = np.meshgrid(theta, phi)
    radius = 1000. + 50. * np.cos(phi)
    height = 25. + 50. * np.sin(phi)
    errors = []
    for site in sites[:4]:
        boundary = probe.run(query_points(site[:2], radius, theta, height), seed=seed)
        errors.append(float(np.max(np.abs(boundary[:, 1]))))
        assert errors[-1] < .025, ('Circular torus envelope/dimensions differ', errors[-1])
        assert np.max(np.abs(boundary[:, 2] - (height.ravel() - 25.))) < .0001
        exterior = np.concatenate([
            query_points(site[:2], 0., 0., 25.),
            query_points(site[:2], 949., theta.ravel(), 25.),
            query_points(site[:2], 1051., theta.ravel(), 25.),
            query_points(site[:2], 1000., theta.ravel(), -26.),
            query_points(site[:2], 1000., theta.ravel(), 76.)])
        outside = probe.run(exterior, seed=seed)
        assert np.all(outside[:, 0] < 0.) and np.all(outside[:, 1] > 0.), \
            'Stone escapes the requested torus envelope or fills its central hole'
    return dict(major_radius_m=1000.,tube_diameter_m=100.,inner_radius_m=950.,outer_radius_m=1050.,
        envelope_bottom_relative_ground_m=-25.,envelope_top_relative_ground_m=75.,
        measured_surface_error_max_m=max(errors),tested_sites=min(4,len(sites)))


def porosity_checks(gl, sites, seed):
    probe = PointProbe(gl, GEOMETRY)
    unit = qmc.Sobol(2, scramble=True, seed=79399).random_base2(12)
    angles = unit[:, 0] * 2. * np.pi
    selected = sites[np.linspace(0, len(sites) - 1, 32).astype(int)]
    records = []
    for fraction in (.02, .10, .25, .50, .75, .90, .98):
        y = -25. + fraction * 100.
        half_width = np.sqrt(50.**2 - (y - 25.)**2)
        # Uniform area in the horizontal annulus, not a biased uniform radius.
        low, high = 1000. - half_width, 1000. + half_width
        radius = np.sqrt(low**2 + unit[:, 1] * (high**2 - low**2))
        queries = np.concatenate([query_points(s[:2], radius, angles, y) for s in selected])
        result = probe.run(queries, seed=seed)
        assert np.all(result[:, 1] <= .025), 'Porosity points left the torus envelope'
        measured = float(np.mean(result[:, 0] > 0.))
        expected = .30 + .50 * fraction
        assert abs(measured - expected) < .03, \
            ('Porosity does not follow the requested Y solid fraction', fraction, measured, expected)
        records.append(dict(relative_tube_height=fraction,target_solid_fraction=expected,
            measured_solid_fraction=measured,samples=len(queries)))
    assert all(a['measured_solid_fraction'] < b['measured_solid_fraction']
               for a, b in zip(records, records[1:])), 'Mean solidity does not increase with height'
    return dict(sites=len(selected),slices=records,
        note='Measured on the isolated torus before union: existing ground fills its buried pores.')


def world_checks(gl, sites, seed, scale):
    probe = PointProbe(gl, WORLD)
    unit = qmc.Sobol(3, scramble=True, seed=18517).random_base2(11)
    # Torus cross-section area sampling; azimuthal weights affect statistics,
    # but do not affect these pointwise geometry/band invariants.
    cross_radius = 49.5 * np.sqrt(unit[:, 0])
    phi = unit[:, 1] * 2. * np.pi
    theta = unit[:, 2] * 2. * np.pi
    radius = 1000. + cross_radius * np.cos(phi)
    height = 25. + cross_radius * np.sin(phi)
    selected = sites[np.linspace(0, len(sites) - 1, 8).astype(int)]
    queries = np.concatenate([query_points(s[:2], radius, theta, height) for s in selected])
    result = probe.run(queries, seed=seed, uDetailScale=scale)
    assert np.all(result[:, 0] >= result[:, 1] - .002), 'Tori remove the original terrain'
    above = (result[:, 1] < -1.) & (result[:, 2] > 1.)
    assert above.any(), 'No actual torus stone is added above ground'
    missing = above & (result[:, 0] <= 0.)
    assert not missing.any(), ('World volume clips torus stone', int(missing.sum()))
    inside_band = (result[:, 11] >= result[:, 5]) & (result[:, 11] <= result[:, 6])
    assert np.all(inside_band[above]), 'Added stone lies outside the conservative column band'
    # The centre of every ring stays terrain with empty air above it.
    center_queries = np.concatenate([query_points(s[:2], 0., 0., np.asarray((-1.,1.,75.,150.)))
                                     for s in selected])
    centers = probe.run(center_queries, seed=seed, uDetailScale=scale)
    assert np.allclose(centers[:, 0], centers[:, 1], atol=.002), 'The central opening is filled by 3D'
    outside_queries = np.concatenate([query_points(s[:2], radial, theta[:128], y)
        for s in selected for radial, y in ((1051.,25.),(1120.,25.),(949.,25.),
                                           (880.,25.),(1000.,76.),(1000.,150.))])
    exterior = probe.run(outside_queries, seed=seed, uDetailScale=scale)
    assert np.all(exterior[:,0] < 0.), 'Density fade creates stone outside the torus envelope'
    # A field-disabled scene must not retain the added geometry.
    disabled = probe.run(queries[:256], seed=seed, uDetailScale=scale, uFieldMode=1)
    assert np.all(disabled[:, 8] == 0.), 'Field-disabled mode still adds tori'
    candidates = np.flatnonzero(above)
    target = candidates[np.argmin(result[candidates, 2])]
    world = queries[target, :3].copy()
    world[1] += result[target, 3]
    return dict(detail_scale=scale,samples=len(queries),above_ground_stone_samples=int(above.sum()),
        clipped_stone_samples=int(missing.sum()),maximum_world_band_m=float(result[:,7].max()),
        exterior_air_samples=len(outside_queries),
        actual_ground_height_range_m=[float(result[:,3].min()),float(result[:,3].max())]), world


def cell_seams(gl):
    probe = PointProbe(gl, WORLD)
    queries = []
    for axis in (0,1):
        for cell in (-5,-2,-1,0,1,3,7):
            for other in (-15731.,4173.,19751.):
                for side in (-.02,.02):
                    xz = [cell * 10000. + side, other]
                    if axis:
                        xz.reverse()
                    queries.append((xz[0],25.,xz[1],0.))
    values = probe.run(queries)
    assert np.all(values[:,2] < 0.), 'A torus crosses its placement cell edge'
    assert np.all(values[:,8] <= .001), 'Torus density leaks onto a placement seam'
    return dict(samples=len(queries),minimum_clearance_to_torus_surface_m=float(-values[:,2].max()))


def mixed_checks(gl):
    cells=[(x,z,0.,0.) for z in range(-6,7) for x in range(-6,7)]
    all_sites=PointProbe(gl,SITES).run(cells)
    accepted=all_sites[all_sites[:,2]==1.][:6]
    rejected=all_sites[all_sites[:,2]==0.][:6]
    assert len(accepted)==len(rejected)==6
    unit=qmc.Sobol(3,scramble=True,seed=71293).random_base2(10)
    cross=49.*np.sqrt(unit[:,0]);phi=unit[:,1]*2*np.pi
    radius=1000.+cross*np.cos(phi);height=25.+cross*np.sin(phi)
    theta=unit[:,2]*2*np.pi
    selected=np.concatenate((accepted,rejected))
    probe=PointProbe(gl,MIXED)
    records=[]
    for scale in (.2,.35,1.):
        for climate_ratio in (.35,.5,.65):
            queries=np.concatenate([query_points(s[:2],radius,theta,height) for s in selected])
            queries[:,3]=climate_ratio
            values=probe.run(queries,uBiomeOverride=-1,uDetailScale=scale,
                             uBandOffset=0.,uBandOffsetVariation=0.)
            assert np.allclose(values[:,4],values[:,5],atol=1.e-6), \
                'Mixed surface and volume weights disagree'
            assert np.all((values[:,4]>0.)&(values[:,4]<1.)), 'Mixed branch was not exercised'
            assert np.array_equal(values[:,2],values[:,3]), \
                'Blended neighbour pilot controls the ColdSteppe ring'
            active=values[:,11]>.5
            assert np.all(values[active,0]>=values[active,7]-.002), 'Mixed torus carves the plain'
            expected_stone=active&(values[:,7]<-1.)&(values[:,1]>.01)
            assert expected_stone.any(), 'No visible mixed-biome torus samples'
            missing=expected_stone&(values[:,0]<=0.)
            assert not missing.any(), ('Mixed envelope clips visible torus stone',scale,climate_ratio,int(missing.sum()))
            # Rejected sites may retain copied caves but must not add a torus,
            # even though the intentionally dirty neighbour pilot is near one.
            assert np.all(values[~active,3]<=.002), 'Dirty blended pilot creates a ring in mountains'
            records.append(dict(detail_scale=scale,surface_and_volume_weight=float(values[0,4]),
                samples=len(queries),expected_above_ground_stone_samples=int(expected_stone.sum()),
                clipped_stone_samples=int(missing.sum()),dirty_pilot_maximum_delta_error=float(abs(values[:,2]-values[:,3]).max()),
                minimum_band_m=float(values[:,6].min()),maximum_band_m=float(values[:,6].max())))
    return dict(seed=1337,accepted_sites=6,rejected_sites=6,global_offset_m=0.,
        note='Constructed mixed columns use actual matrix/weight/storage helpers; the neighbour contributes zero band and an unrelated pilot. Full automatic multi-biome generation is covered by the general pipeline suite.',
        cases=records)


def render(path, gl, sites, seed, report):
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    center = sites[np.argmin(np.sum(sites[:,:2]**2,axis=1)),:2]
    probe = PointProbe(gl, GEOMETRY)
    spacing = 10.
    x = np.arange(-1100.,1100.1,spacing)
    z = x.copy()
    y = np.arange(-35.,85.1,4.)
    zz, yy, xx = np.meshgrid(z,y,x,indexing='ij')
    queries = np.column_stack((xx.ravel()+center[0],yy.ravel(),
                              zz.ravel()+center[1],np.zeros(xx.size)))
    values = np.concatenate([probe.run(queries[i:i+65536],seed=seed)[:,0]
        for i in range(0,len(queries),65536)])
    sample = dict(density=values.reshape(len(z),len(y),len(x)),
        columns=np.zeros((len(z),len(x),4)),origin=(float(center[0]-1100.),float(center[1]-1100.)),
        spacing=spacing,min_y=float(y[0]),step_y=4.)
    fig = plt.figure(figsize=(15,9),constrained_layout=True)
    gs = fig.add_gridspec(2,3,height_ratios=(1.1,1.))
    ax = fig.add_subplot(gs[0,:2])
    draw_projected_mesh(ax,sample,isosurface(sample))
    ax.set_title('Anneau isolé : véritable densité GLSL avant union au sol',fontsize=11)
    ax = fig.add_subplot(gs[0,2])
    records=report['porosity']['slices']
    ys=[r['relative_tube_height']*100 for r in records]
    ax.plot([r['measured_solid_fraction']*100 for r in records],ys,'o-',label='GLSL mesuré')
    ax.plot([r['target_solid_fraction']*100 for r in records],ys,'--',label='Cible')
    ax.set_xlabel('Solide (%)');ax.set_ylabel('Hauteur dans le tube (%)')
    ax.set_title('Perlin 3D : moyenne sur 32 anneaux',fontsize=10)
    ax.grid(alpha=.2);ax.legend(fontsize=9)
    # Actual combined terrain + torus volume, then expressed relative to each
    # column's ground for a readable transverse section through two ring limbs.
    line_x=np.arange(-1100.,1100.1,5.)
    line_y=np.arange(-35.,85.1,1.)
    yy,xx=np.meshgrid(line_y,line_x,indexing='ij')
    section_queries=np.column_stack((xx.ravel()+center[0],yy.ravel(),
        np.full(xx.size,center[1]),np.zeros(xx.size)))
    section=PointProbe(gl,WORLD).run(section_queries,seed=seed)[:,0].reshape(len(line_y),len(line_x))
    ax=fig.add_subplot(gs[1,:2])
    ax.contourf(line_x,line_y,section,levels=[float(section.min())-1.,0.,float(section.max())+1.],
        colors=['#eef5f8','#586b76'])
    ax.axhline(0.,color='#c88938',lw=1.)
    ax.set_xlabel('Distance au centre (m)');ax.set_ylabel('Y relatif au sol (m)')
    ax.set_title('Coupe de la densité totale : sol conservé, tube −25 à +75 m')
    ax=fig.add_subplot(gs[1,2])
    ax.scatter(sites[:,0]/1000.,sites[:,1]/1000.,s=7,color='#426677')
    ax.scatter(center[0]/1000.,center[1]/1000.,s=40,facecolors='none',edgecolors='#c88938')
    ax.set_aspect('equal');ax.set_xlabel('km');ax.set_ylabel('km')
    ax.set_title('Sites acceptés dans les plaines',fontsize=10)
    fig.suptitle('Steppe froide — mesures du GLSL réel\nReconstruction diagnostique ; pas une capture du jeu.',fontsize=14)
    fig.savefig(path,dpi=140);plt.close(fig)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=Path('docs/biome-validation/cold_steppe_tori.json'))
    parser.add_argument('--no-render', action='store_true')
    parser.add_argument('--mixed-only',action='store_true',help='Append mixed-column checks to a matching existing report')
    args = parser.parse_args()
    fingerprint = source_fingerprint(6)
    if args.mixed_only:
        report=json.loads(args.output.read_text())
        assert report['source_sha256']==fingerprint, 'Existing report is stale; rerun the full suite'
        with GL() as gl:
            report['mixed_columns']=mixed_checks(gl)
        assert source_fingerprint(6)==fingerprint
        args.output.write_text(json.dumps(report,indent=2)+'\n')
        print(json.dumps(report['mixed_columns'],indent=2))
        return
    with GL() as gl:
        print('Checking randomized placement and whole-ring plain eligibility...', flush=True)
        sites, placement = get_sites(gl, 1337)
        other, second_placement = get_sites(gl, 97)
        report = dict(renderer=gl.renderer,source_sha256=fingerprint,
            placement=[placement,second_placement])
        print('Checking mathematical torus envelope and Perlin Y gradient...', flush=True)
        report['envelope'] = envelope_checks(gl, sites, 1337)
        report['porosity'] = porosity_checks(gl, sites, 1337)
        report['placement_seams'] = cell_seams(gl)
        report['mixed_columns'] = mixed_checks(gl)
        report['world'] = []
        for scale in (.2,.35,1.):
            print(f'Checking field integration at detailScale={scale}...', flush=True)
            metrics, position = world_checks(gl, sites, 1337, scale)
            report['world'].append(metrics)
            if scale == .35:
                density = DensityProbe(gl)
                sample = density.sample(6,(float(position[0]-8),float(position[2]-8)),2.,
                    float(position[1]-8),2.,size=9,depth=9,seed=1337,**SCENE_OPTIONS)
                report['targeted_pipeline'] = check_targeted_pipeline(FieldRunner(gl),density,sample,6,1337)
        if not args.no_render:
            print('Rendering actual GLSL torus and ground union...',flush=True)
            args.output.parent.mkdir(parents=True,exist_ok=True)
            render(args.output.with_suffix('.png'),gl,sites,1337,report)
        assert source_fingerprint(6) == fingerprint, 'Sources changed during validation; rerun'
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
