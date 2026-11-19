#!/usr/bin/env python3
"""Inspect independent rift shapes and larger-scale 3D noise on actual GLSL.

All atlas panels use the same local orientation: a rotation alone cannot pass
for a different silhouette. The noise comparison changes only the scale constant.
"""
from __future__ import annotations
import argparse
import json
from pathlib import Path
import re
import numpy as np
from biome_test_support import biome_recipe_source, BIOME_NAMES
from biome_placement_probe import placements, anchor_probe
from check_biomes import BiomeProbe, profile_probe_source
from check_biome_landforms import source_fingerprint
from gl_headless import GL

SHAPE_SOURCE=r'''
layout(local_size_x=8,local_size_y=8) in;
struct ProbeSample { vec4 terrain; vec4 volume; vec4 samples; vec4 shape; };
layout(std430,binding=6) writeonly buffer ProbeOutput { ProbeSample outputValues[]; };
uniform uint uProbeSize;
uniform vec2 uProbeOrigin;
uniform float uProbeSpacing;
void main() {
 uvec2 i=gl_GlobalInvocationID.xy;
 if(any(greaterThanEqual(i,uvec2(uProbeSize))))return;
 vec2 p=uSphereCenter.xz;
 LandSite s=RIFT_CALL;
 s.q=uProbeOrigin+vec2(i)*uProbeSpacing;
 s.mask=1.-smoothstep(.64,1.,length(s.q)/s.radius);
 int count=polarRiftBranchCount(s);
 outputValues[i.x+i.y*uProbeSize]=ProbeSample(
  vec4(polarRiftMask(s),polarRiftWallCoverage(s),float(count),s.radius),
  count>0?polarRiftBranchLayout(s,0):vec4(0),
  count>1?polarRiftBranchLayout(s,1):vec4(0),
  count>2?polarRiftBranchLayout(s,2):vec4(0));
}
'''

NOISE_SOURCE=r'''
layout(local_size_x=8,local_size_y=8) in;
struct ProbeSample { vec4 terrain; vec4 volume; vec4 samples; vec4 shape; };
layout(std430,binding=6) writeonly buffer ProbeOutput { ProbeSample outputValues[]; };
uniform uint uProbeSize;
uniform float uProbeSpacing;
void main() {
 uvec2 i=gl_GlobalInvocationID.xy;
 if(any(greaterThanEqual(i,uvec2(uProbeSize))))return;
 vec2 p=uSphereCenter.xz;
 LandSite s=RIFT_CALL;
 // Many horizontal phases, vertical samples every four metres.
 s.q=vec2(float(i.y)*131.7,float(i.y)*83.1);
 vec3 position=vec3(p.x,float(i.x)*uProbeSpacing-512.,p.y);
 vec2 warp=polarRiftWallWarp(position,s);
 outputValues[i.x+i.y*uProbeSize]=ProbeSample(vec4(warp,0,0),vec4(0),vec4(0),vec4(0));
}
'''

class OriginalNoiseScaleProbe(BiomeProbe):
    def program_for(self,biome):
        if biome not in self.programs:
            d=self.directory/'biomes'/BIOME_NAMES[biome]
            relief=biome_recipe_source(d/'relief.C.glsl')
            volume=re.sub(r'^#version[^\n]*\n','',biome_recipe_source(d/'volume.C.glsl'),count=1)
            combined,count=re.subn(r'(const float POLAR_RIFT_WALL_NOISE_SCALE\s*=\s*)[\d.]+',r'\g<1>1.0',relief+volume)
            assert count>0,'Noise scale parameter missing'
            self.programs[biome]=self.gl.compile([(0x91B9,combined+profile_probe_source()+self.main_source)],'PolarIce original wall noise scale')
        return self.programs[biome]


def noise_measurements(gl,placement,site,seed):
    source=NOISE_SOURCE.replace('RIFT_CALL',placement.call())
    current=BiomeProbe(gl,source=source);old=OriginalNoiseScaleProbe(gl,source=source)
    opts=dict(biome=0,size=257,spacing=4.,uSeed=seed,uSphereCenter=(float(site[0]),0.,float(site[1])))
    after=np.asarray(current.run(**opts)['values']).reshape(257,257,16)[:,:,:2]
    before=np.asarray(old.run(**opts)['values']).reshape(257,257,16)[:,:,:2]
    gradient_before=float(np.sqrt(np.mean(np.diff(before,axis=1)**2)))
    gradient_after=float(np.sqrt(np.mean(np.diff(after,axis=1)**2)))
    rms_before=np.sqrt(np.mean(before**2,axis=(0,1)))
    rms_after=np.sqrt(np.mean(after**2,axis=(0,1)))
    assert gradient_after<gradient_before*.60,('Noise still too concentrated',gradient_before,gradient_after)
    assert np.all((rms_after/rms_before>.75)&(rms_after/rms_before<1.25)),('Noise amplitude changed',rms_before,rms_after)
    return {'vertical_gradient_rms_before':gradient_before,'vertical_gradient_rms_after':gradient_after,
        'gradient_ratio':gradient_after/gradient_before,'displacement_rms_before_m':rms_before.tolist(),
        'displacement_rms_after_m':rms_after.tolist()},before,after


def render(directory,records,images,before,after):
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    from PIL import Image
    fig,axes=plt.subplots(3,4,figsize=(11,8.8),constrained_layout=True)
    for ax,rec,data in zip(axes.flat,records,images):
        ax.imshow(data,origin='lower',extent=[-1.1,1.1,-1.1,1.1],cmap='Blues',vmin=0,vmax=1)
        ax.set_title(f"Seed {rec['seed']} · {rec['branches']} branch(es)",fontsize=10)
        ax.set_xlabel('Local X / radius');ax.set_ylabel('Local Z / radius')
    fig.suptitle('Actual GLSL rift contours — 12 distinct sites, all in the same local orientation\nBranch count, side, origin, length and trunk curvature vary independently',fontsize=12)
    path=directory/'polar_rift_shapes.png';fig.savefig(path,dpi=90);plt.close(fig)
    with Image.open(path) as img:img.convert('RGB').quantize(colors=128).save(path,optimize=True)
    fig,axes=plt.subplots(2,1,figsize=(10,4.8),constrained_layout=True)
    y=np.arange(257)*4.-512.
    for k,ax in enumerate(axes):
        ax.plot(y,before[32,:,k],color='#9c6b43',alpha=.8,label='Previous scale')
        ax.plot(y,after[32,:,k],color='#236486',label='Scale x2.5')
        ax.set_xlabel('World height (m)');ax.set_ylabel(('Along','Across')[k]+' displacement (m)');ax.grid(alpha=.2);ax.legend()
    fig.suptitle('Actual 3D noise displacement: broader features, unchanged amplitude settings')
    fig.savefig(directory/'polar_rift_noise_scale.png',dpi=95);plt.close(fig)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args();args.output.mkdir(parents=True,exist_ok=True)
    fingerprint=source_fingerprint(0)
    with GL() as gl:
        place=next(p for p in placements(0) if p.variable=='rift')
        probe=BiomeProbe(gl,source=SHAPE_SOURCE.replace('RIFT_CALL',place.call()))
        all_records=[];candidates={k:[] for k in range(4)};histogram={k:0 for k in range(4)}
        for seed in (1337,17):
            rows,_=anchor_probe(gl,0,place,seed,size=8)
            for site in rows:
                opts=dict(biome=0,size=1,origin=(0.,0.),spacing=0.,uSeed=seed,
                    uSphereCenter=(float(site[0]),0.,float(site[1])))
                a=probe.run(**opts)
                assert a['raw']==probe.run(**opts)['raw'],'Shape parameters are not deterministic'
                data=np.asarray(a['values']);count=int(data[2]);assert 0<=count<=3
                histogram[count]+=1
                layouts=data[4:4+count*4].reshape(-1,4)
                record={'seed':seed,'anchor':site[:2].tolist(),'radius_m':float(site[2]),
                    'branches':count,'layouts':layouts.tolist(),'variant':float(site[8])}
                all_records.append(record);candidates[count].append((record,site))
        assert all(histogram[k]>=4 for k in range(4)),('Insufficient topology diversity',histogram)
        assert histogram[0]/sum(histogram.values())>.25,('Too many mandatory forks',histogram)
        layouts=np.concatenate([np.asarray(r['layouts']).reshape(-1,4) for r in all_records if r['branches']])
        assert np.any(layouts[:,1]<0)&np.any(layouts[:,1]>0),'Branches all point the same way'
        assert np.any(layouts[:,2]<0)&np.any(layouts[:,2]>0),'Branches all on same side'
        assert np.ptp(layouts[:,0])>.3 and np.ptp(layouts[:,1])>.5,'Branch layout remains fixed'
        selected=[];images=[]
        # Three examples per branch count, spread across sites. No rotation is
        # used to manufacture visual differences between these local contours.
        for k in range(3):
            for count in range(4):
                record,site=candidates[count][k]
                extent=1.1*site[2];size=193
                raw=probe.run(biome=0,size=size,origin=(-extent,-extent),spacing=2*extent/(size-1),
                    uSeed=record['seed'],uSphereCenter=(float(site[0]),0.,float(site[1])))
                shape=np.asarray(raw['values']).reshape(size,size,16)[:,:,0]
                assert 0<=shape.min() and shape.max()<=1.00001 and (shape>.5).sum()>100
                assert not any(np.array_equal(shape,other) for other in images),'Duplicate silhouette'
                selected.append(record);images.append(shape)
        measurements,before,after=noise_measurements(gl,place,np.asarray(selected[0]['anchor']),selected[0]['seed'])
        assert source_fingerprint(0)==fingerprint,'Sources changed during measurement'
        result={'renderer':gl.renderer,'source_sha256':fingerprint,'sites_measured':len(all_records),
            'branch_count_histogram':histogram,'noise_scale':measurements,'atlas_sites':selected,
            'note':'Actual GLSL shapes shown in local coordinates to remove trivial placement rotations. Branch counts describe generated geometry; broad neighbouring cuts may merge. Noise comparison changes only its scale constant; not an in-game screenshot or performance benchmark.'}
        (args.output/'polar_rift_variation.json').write_text(json.dumps(result,indent=2)+'\n')
        render(args.output,selected,images,before,after)
        print(json.dumps({'sites':len(all_records),'histogram':histogram,'noise':measurements},indent=2),flush=True)
    return 0

if __name__=='__main__':raise SystemExit(main())
