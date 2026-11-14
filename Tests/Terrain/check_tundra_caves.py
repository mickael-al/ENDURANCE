#!/usr/bin/env python3
"""Validate rare Tundra caves against an untouched shader-tree snapshot.

The GPU executes the production recipes. Supply --reference-shaders pointing to
the prior Asset/Shader/ProceduralTerrain directory (e.g. a git archive snapshot).
No noise, placement, or cavity geometry is reproduced on the CPU.
"""
from pathlib import Path
import argparse
import hashlib
import json
import re

import numpy as np
from scipy import ndimage

from biome_test_support import biome_recipe_source
from check_biomes import BiomeProbe, profile_probe_source
from check_biome_landforms import (
    DensityProbe, SCENE_OPTIONS, check_targeted_pipeline, site_recipe,
    source_fingerprint,
)
from check_gpu import FieldRunner
from gl_headless import GL


HEADER = r'''
layout(local_size_x=8,local_size_y=8) in;
struct ProbeSample { vec4 terrain; vec4 volume; vec4 samples; vec4 shape; };
layout(std430,binding=6) writeonly buffer ProbeOutput { ProbeSample outputValues[]; };
uniform uint uProbeSize;
uniform vec2 uProbeOrigin;
uniform float uProbeSpacing;
'''

CANDIDATES = HEADER + r'''
void main() {
 uvec2 i=gl_GlobalInvocationID.xy;
 if(any(greaterThanEqual(i,uvec2(uProbeSize)))) return;
 vec2 p=uProbeOrigin+vec2(i)*uProbeSpacing;
 LandSite s=SITE_CALL;
 p=s.center;s=SITE_CALL;
 float presence=tundraCavePresence(s);
 outputValues[i.x+i.y*uProbeSize]=ProbeSample(
  vec4(p,s.mask,presence),vec4(s.radius,s.variant,0,0),vec4(0),vec4(0));
}
'''

BROAD = HEADER + r'''
void main() {
 uvec2 i=gl_GlobalInvocationID.xy;
 if(any(greaterThanEqual(i,uvec2(uProbeSize)))) return;
 vec2 p=uProbeOrigin+vec2(i)*uProbeSpacing;
 FieldColumn c=fieldProbeColumn(p);
 vec4 a,b;
 for(uint j=0u;j<4u;++j) {
  a[j]=fieldProbeDensity(vec3(p.x,c.terrain.x-float(j)*16.0,p.y),c);
  b[j]=fieldProbeDensity(vec3(p.x,c.terrain.x-64.0-float(j)*16.0,p.y),c);
 }
 outputValues[i.x+i.y*uProbeSize]=ProbeSample(c.terrain,a,b,c.blend);
}
'''


def values(probe, **options):
    return np.asarray(probe.run(**options)['values'],dtype=np.float32).reshape(options['size'],options['size'],16)


class WithoutCaves(DensityProbe):
    """Omit the new call only; keep every existing Tundra 3D construction."""
    def program_for(self, biome):
        assert biome == 2
        if biome not in self.programs:
            directory=self.directory/'biomes'/'Tundra'
            relief=biome_recipe_source(directory/'relief.C.glsl')
            volume=biome_recipe_source(directory/'volume.C.glsl')
            volume=re.sub(r'^#version[^\n]*\n','',volume,count=1)
            volume,count=re.subn(
                r'target\s*=\s*tundraCarveCaves\(p,\s*column,\s*site,\s*target\);',
                '/* New cave carving omitted for preservation regression. */',volume)
            assert count == 1, 'Update cave omission adapter after entrypoint change'
            self.programs[biome]=self.gl.compile(
                [(0x91B9,relief+volume+profile_probe_source()+self.main_source)],
                'Tundra with new cave call omitted')
        return self.programs[biome]


def find_candidates(gl, seed):
    call,spacing,_=site_recipe(2)
    size=61
    origin=(-size//2*spacing+.5*spacing,)*2
    rows=values(BiomeProbe(gl,source=CANDIDATES.replace('SITE_CALL',call)),
                biome=2,size=size,origin=origin,spacing=spacing,uSeed=seed).reshape(-1,16)
    active=rows[:,2]>.99
    eligible=active&(rows[:,3]>.99)
    candidates=rows[eligible]
    candidates=candidates[np.argsort(np.sum(candidates[:,:2]**2,axis=1))]
    assert len(candidates)>=3, 'Too few fully eligible candidates to test real entrances'
    assert 0 < eligible.mean() < .15, 'Ground cave population is no longer scarce'
    assert np.all(rows[~active,3]==0), 'Caves enabled outside existing active land sites'
    return candidates,dict(cells_sampled=len(rows),existing_active_sites=int(active.sum()),
        fully_eligible_caves=int(eligible.sum()),eligible_cell_fraction=float(eligible.mean()),
        tested_selection='Three nearest fully eligible candidates, without geometry filtering')


def sample_cave(actual, baseline, omitted, candidate, seed):
    extent=450.;size=151;spacing=2*extent/(size-1)
    origin=tuple(np.asarray(candidate[:2])-extent)
    columns=actual.sample(2,origin,spacing,0.,1.,size=size,depth=1,seed=seed,**SCENE_OPTIONS)['columns']
    step=5.
    low=float(np.floor((columns[:,:,0].min()-115.)/step)*step)
    high=float(np.ceil((columns[:,:,0].max()+65.)/step)*step)
    depth=int(round((high-low)/step))+1
    assert depth<300, 'Unexpectedly large local terrain span; inspect before increasing memory'
    options=dict(biome=2,origin=origin,spacing=spacing,min_y=low,step_y=step,
                 size=size,depth=depth,seed=seed,**SCENE_OPTIONS)
    after=actual.sample(**options);before=baseline.sample(**options)
    legacy=omitted.sample(**options)
    assert np.array_equal(before['columns'],after['columns']), 'Caves changed existing 2D columns or band'
    assert np.array_equal(before['density'],legacy['density']), 'Omitting caves does not restore exact previous Tundra density'
    assert np.array_equal(before['columns'],legacy['columns'])
    return after,before


def cave_metrics(after_sample,before_sample):
    after=after_sample['density'];before=before_sample['density']
    difference=after-before
    assert np.max(difference)<=0., 'Caves add solid or alter existing sculptures positively'
    removed=(before>0)&(after<0)
    solid=after>0;air=after<0
    above=np.maximum.accumulate(solid[:,::-1,:],axis=1)[:,::-1,:]
    below=np.maximum.accumulate(solid,axis=1)
    roofed=removed&above&below
    assert roofed.sum()>100, 'Excavation lacks a substantial chamber with roof and floor'
    labels,_=ndimage.label(air,structure=ndimage.generate_binary_structure(3,1))
    exterior=np.unique(labels[(before<-.5)&air]);exterior=exterior[exterior>0]
    connected=roofed&np.isin(labels,exterior)
    fraction=float(connected.sum()/roofed.sum())
    assert fraction>.95, 'Roofed cave chamber is sealed off from preexisting exterior air'
    # Boundary equality establishes compact support using the actual old field.
    for axis in (0,2):
        assert np.array_equal(np.take(after,[0,-1],axis=axis),np.take(before,[0,-1],axis=axis)), 'Cave reaches the sampling footprint edge'
    assert np.array_equal(after[:,[0,-1],:],before[:,[0,-1],:]), 'Cave reaches vertical sampling limits'
    runs=np.zeros_like(air,dtype=np.int16)
    for j in range(air.shape[1]):
        runs[:,j,:]=np.where(air[:,j,:],runs[:,j-1,:]+1 if j else 1,0)
    clearance=float(runs[roofed].max(initial=0)*after_sample['step_y'])
    assert clearance>=20., 'Cave lacks useful vertical chamber clearance'
    voxel=after_sample['spacing']**2*after_sample['step_y']
    return dict(removed_samples=int(removed.sum()),roof_and_floor_samples=int(roofed.sum()),
        connected_roofed_fraction=fraction,removed_volume_m3=float(removed.sum()*voxel),
        sampled_maximum_roofed_clearance_m=clearance,
        unchanged_density_fraction=float(np.mean(after==before)),
        horizontal_spacing_m=after_sample['spacing'],vertical_spacing_m=after_sample['step_y'],
        legacy_density_restored_exactly_when_caves_omitted=True),removed,roofed


def broad_preservation(gl,baseline_directory,seed):
    options=dict(biome=2,size=385,origin=(-24000.,-24000.),spacing=125.,uSeed=seed,**SCENE_OPTIONS)
    current=BiomeProbe(gl,source=BROAD)
    prior=BiomeProbe(gl,directory=baseline_directory,source=BROAD)
    after=values(current,**options);before=values(prior,**options)
    assert np.array_equal(after[:,:,:4],before[:,:,:4]), 'Broad-map terrain or band changed'
    assert np.array_equal(after[:,:,12:],before[:,:,12:]), 'Broad-map blend controls changed'
    delta=after[:,:,4:12]-before[:,:,4:12]
    assert np.max(delta)<=0, 'Broad-map cave change adds solid'
    changed=np.any(delta!=0,axis=2)
    assert 0 < changed.mean() < .025, 'Actual cave carving is missing or too widespread'
    assert np.array_equal(after,values(current,**options)), 'Tundra cave field is not deterministic'
    # Compare the omitted entrypoint against the old tree over all eight depths.
    omitted=WithoutCaves(gl);omitted.main_source=BROAD
    omitted_data=values(omitted,**options)
    assert np.array_equal(omitted_data,before), 'Legacy Tundra changed outside the new carving call'
    return dict(area_km2=2304.,columns_sampled=int(changed.size),
        sampled_depths_below_surface_m=list(range(0,128,16)),
        altered_column_fraction=float(changed.mean()),exactly_unchanged_column_fraction=float((~changed).mean()),
        columns_and_controls_unchanged_exactly=True,legacy_3d_unchanged_exactly=True)


def marine_preservation(gl,baseline_directory):
    current=BiomeProbe(gl,source=BROAD)
    prior=BiomeProbe(gl,directory=baseline_directory,source=BROAD)
    records=[]
    for seed in (1337,17):
        options=dict(biome=25,size=129,origin=(-1700.25,1300.75),spacing=29.5,uSeed=seed,**SCENE_OPTIONS)
        after=values(current,**options);before=values(prior,**options)
        assert np.array_equal(after,before), 'Extracting shared cavern helper altered Marine density'
        records.append(dict(seed=seed,columns_tested=129**2,density_samples_tested=129**2*8,bit_identical=True))
    return records


def render(path,samples):
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    from matplotlib.colors import ListedColormap
    fig,axes=plt.subplots(len(samples),3,figsize=(15,4.5*len(samples)),squeeze=False,constrained_layout=True)
    for row,(seed,after_sample,before_sample,removed,roofed) in enumerate(samples):
        after=after_sample['density'];before=before_sample['density']
        z=int(np.argmax(roofed.sum(axis=(1,2))))
        n,depth,_=after.shape
        x=after_sample['origin'][0]+np.arange(n)*after_sample['spacing']
        y=after_sample['min_y']+np.arange(depth)*after_sample['step_y']
        zs=after_sample['origin'][1]+np.arange(n)*after_sample['spacing']
        occupied=np.argwhere(roofed[z])
        low=max(y[0],y[occupied[:,0].min()]-35.)
        high=min(y[-1],y[occupied[:,0].max()]+65.)
        for ax,d,title in zip(axes[row,:2],(before[z],after[z]),('Previous Tundra','Tundra with ground cave')):
            ax.imshow(d>0,origin='lower',extent=[x[0],x[-1],y[0],y[-1]],
                      cmap=ListedColormap(['#263e51','#cbd8d5']),aspect='equal')
            ax.contour(x,y,d,levels=[0],colors='#6c959a',linewidths=.6)
            ax.set_ylim(low,high);ax.set_title(f'{title} — seed {seed}')
            ax.set_xlabel('World X (m)');ax.set_ylabel('World height (m)')
        axes[row,2].imshow(removed.any(axis=1),origin='lower',extent=[x[0],x[-1],zs[0],zs[-1]],
                           cmap=ListedColormap(['#263e51','#a7c9c5']),aspect='equal')
        axes[row,2].set_title('Actual excavated footprint, all heights')
        axes[row,2].set_xlabel('World X (m)');axes[row,2].set_ylabel('World Z (m)')
    fig.suptitle('Rare Tundra ground caves — production GLSL density, no vertical exaggeration',fontsize=13)
    fig.savefig(path,dpi=110);plt.close(fig)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--reference-shaders',required=True,type=Path)
    parser.add_argument('--output',required=True,type=Path)
    args=parser.parse_args();args.output.mkdir(parents=True,exist_ok=True)
    assert (args.reference_shaders/'biomes/Tundra/volume.C.glsl').is_file()
    fingerprint=source_fingerprint(2)
    before_source=''.join(biome_recipe_source(args.reference_shaders/'biomes/Tundra'/name)
                          for name in ('relief.C.glsl','volume.C.glsl'))
    with GL() as gl:
        actual=DensityProbe(gl);baseline=DensityProbe(gl);baseline.directory=args.reference_shaders
        omitted=WithoutCaves(gl);records=[];pictures=[]
        for seed in (1337,17):
            broad=broad_preservation(gl,args.reference_shaders,seed)
            candidates,population=find_candidates(gl,seed)
            caves=[]
            for index,candidate in enumerate(candidates[:3]):
                after,before=sample_cave(actual,baseline,omitted,candidate,seed)
                metrics,removed,roofed=cave_metrics(after,before)
                record=dict(center=candidate[:2].tolist(),radius=float(candidate[4]),variant=float(candidate[5]),**metrics)
                if index==0:
                    wall=np.where(roofed,np.abs(after['density']),np.inf)
                    z,j,x=np.unravel_index(np.argmin(wall),wall.shape)
                    target=dict(after,density=after['density'][z:z+1,j:j+1,x:x+1],
                        columns=after['columns'][z:z+1,x:x+1],
                        origin=(after['origin'][0]+x*after['spacing'],after['origin'][1]+z*after['spacing']),
                        min_y=after['min_y']+j*after['step_y'])
                    assert target['density'][0,0,0]<0 and before['density'][z,j,x]>0
                    record['pipeline']=check_targeted_pipeline(FieldRunner(gl),actual,target,2,seed)
                    record['pipeline']['excavated_wall_reference_density']=float(target['density'][0,0,0])
                    pictures.append((seed,after,before,removed,roofed))
                caves.append(record)
                print(json.dumps(dict(seed=seed,cave=index,**record)),flush=True)
            records.append(dict(seed=seed,broad_preservation=broad,population=population,caves=caves))
        marine=marine_preservation(gl,args.reference_shaders)
        assert source_fingerprint(2)==fingerprint, 'Production source changed during validation'
        report=dict(renderer=gl.renderer,source_sha256=fingerprint,
            prior_tundra_source_sha256=hashlib.sha256(before_source.encode()).hexdigest(),
            records=records,marine_bit_preservation=marine,
            note='Actual production GLSL; six nearest fully eligible caves across two seeds. Exact old 2D controls and legacy 3D, carve-only density, roof/floor, exterior connectivity, broad-map scarcity, and cavity-wall production/LOD checks. Diagnostic geometry sampling does not certify player collision or Windows GPU performance.')
        render(args.output/'tundra_caves.png',pictures)
        (args.output/'tundra_caves.json').write_text(json.dumps(report,indent=2)+'\n')
        print('PASS Tundra cave preservation, connectivity, scarcity, Marine and production/LOD checks',flush=True)
    return 0


if __name__=='__main__':
    raise SystemExit(main())
