#!/usr/bin/env python3
"""Probe the seven revised cold biomes at their actual GPU-generated RKM sites.

RKM checks cover nominal placement cells, not encounters along an arbitrary path.
Optional reference shaders isolate changes from the previously saved recipes.
Figures show real GLSL samples, not game screenshots or collision guarantees.
"""
from __future__ import annotations
import argparse
import json
import math
from pathlib import Path
import sys

import numpy as np

from biome_test_support import BIOME_NAMES
from check_biomes import BiomeProbe
from check_biome_landforms import (HEIGHT_PROBE, DensityProbe, SCENE_OPTIONS,
    height_grid, density_metrics, check_targeted_pipeline, site_recipe, source_fingerprint)
from check_gpu import FieldRunner
from gl_headless import GL, GLUnavailable

# biome, label, RKM, salt, expected signed centre-height addition when isolated.
FEATURES=(
    (0,'ice_massif',.25,111,(400.,610.)),
    (0,'ice_rift',.5,109,(-350.,-200.)),
    (1,'frozen_mountain',.5,211,(400.,580.)),
    (1,'underground_cave',2.,209,None),
    (2,'tundra_peak',1.,321,(795.,805.)),
    (2,'twenty_metre_rock',.5,322,None),
    (3,'taiga_peak',.5,421,(695.,705.)),
    (3,'ancient_river',1.,422,(-80.,-20.)),
    (4,'lake_basin',1.,510,(-18.,-5.)),
    (4,'bog_hole',.25,511,None),
    (5,'rocky_hole',.25,5112,None),
    (6,'terraced_mountain',.5,611,(615.,625.)),
    (6,'deep_chasm',.25,613,(-805.,-795.)),
)

SITE_SOURCE=r'''
layout(local_size_x=8,local_size_y=8) in;
struct ProbeSample { vec4 terrain; vec4 volume; vec4 samples; vec4 shape; };
layout(std430,binding=6) writeonly buffer ProbeOutput { ProbeSample outputValues[]; };
uniform uint uProbeSize;
uniform vec2 uProbeOrigin;
uniform float uProbeSpacing;
void main() {
    uvec2 i=gl_GlobalInvocationID.xy;
    if(any(greaterThanEqual(i,uvec2(uProbeSize)))) return;
    vec2 p=uProbeOrigin+vec2(i)*uProbeSpacing;
    LandSite s=landRkmSite(p,RATE,SALT);
    vec2 a=landRkmSite(s.center+vec2(1,0),RATE,SALT).q;
    vec2 b=landRkmSite(s.center+vec2(0,1),RATE,SALT).q;
    float legacy=landSite(s.center,LEGACY).mask;
    outputValues[i.x+i.y*uProbeSize]=ProbeSample(vec4(s.center,s.radius,s.mask),
        vec4(a,b),vec4(s.variant,legacy,0,0),vec4(0));
}
'''


def anchors(gl,biome,rate,salt,seed):
    legacy,_,_=site_recipe(biome)
    source=SITE_SOURCE.replace('RATE',str(float(rate))).replace('SALT',str(salt)+'u').replace('LEGACY',legacy)
    probe=BiomeProbe(gl,source=source)
    spacing=1000./rate
    options=dict(biome=biome,size=6,origin=(-2.5*spacing,-2.5*spacing),spacing=spacing,uSeed=seed)
    result=probe.run(**options)
    assert result['raw']==probe.run(**options)['raw'],('RKM placement not repeatable',biome,salt)
    changed=probe.run(**dict(options,uSeed=seed+1))
    assert result['raw']!=changed['raw'],('RKM placement ignores seed',biome,salt)
    data=np.asarray(result['values']).reshape(-1,16)
    assert len(np.unique(data[:,:2],axis=0))==36,('Placement cells collapse',biome,salt)
    expected=np.stack(np.meshgrid(np.arange(-3,3),np.arange(-3,3)),axis=-1).reshape(-1,2)
    assert np.array_equal(np.floor(data[:,:2]/spacing).astype(int),expected)
    # Cardinal perturbations recover the GPU's rotated local axes.
    return sorted(data,key=lambda row:float(np.dot(row[:2],row[:2])))


def point(probe,biome,xy,seed):
    return height_grid(probe,biome,tuple(xy),0.,size=1,seed=seed,**SCENE_OPTIONS)[0,0]


def local_position(site,x,z):
    return site[:2]+x*np.array((site[4],site[6]))+z*np.array((site[5],site[7]))


def select_site(height,reference,biome,sites,seed,expected):
    candidates=[]
    for site in sites:
        h=float(point(height,biome,site[:2],seed)[0])
        before=float(point(reference,biome,site[:2],seed)[0]) if reference else None
        delta=h-before if reference else None
        candidates.append((site,h,before,delta))
        if expected and reference and expected[0]<=delta<=expected[1]:return candidates[-1]
        if expected is None and site[9]==0.:return candidates[-1]
    if expected and reference:
        raise AssertionError(('No isolated feature reaches requested height/depth',biome,expected,[r[3] for r in candidates]))
    return candidates[0]


def feature_volume(height,density,biome,label,site,seed):
    center=site[:2].copy()
    extent=75.
    if label=='ice_rift':
        x=-.18*site[2];v=site[8]
        z=18.*math.sin(x/120.+v*6.2831853)+8.*math.sin(x/57.+v*2.)
        center=local_position(site,x,z);extent=90.
    elif label=='bog_hole':center=local_position(site,15.,0.);extent=120.
    elif label=='rocky_hole':center=local_position(site,25.,0.);extent=140.
    elif label=='twenty_metre_rock':extent=45.
    origin=tuple(center-extent)
    spacing=2*extent/96.
    h=height_grid(height,biome,origin,spacing,size=97,seed=seed,**SCENE_OPTIONS)[:,:,0]
    low=float(h.min())-75.;high=float(h.max())+80.
    sample=density.sample(biome,origin,spacing,low,(high-low)/192.,size=97,depth=193,seed=seed,**SCENE_OPTIONS)
    metrics=density_metrics(sample)
    if label=='twenty_metre_rock':
        y=sample['min_y']+np.arange(sample['density'].shape[1])*sample['step_y']
        top=np.max(np.where(sample['density']>0,y[None,:,None],-1.e20),axis=1)
        extra=top-sample['columns'][:,:,0]
        maximum=float(extra.max())
        assert 18.0<maximum<22.0,('20 m boulder clipped or missing',maximum)
        metrics['maximum_height_above_relief_m']=maximum
    else:
        raw_y={'ice_rift':-42.,'underground_cave':-42.,'bog_hole':-56.,'rocky_hole':-44.}[label]
        ground=float(point(height,biome,center,seed)[0])
        chamber=density.sample(biome,tuple(center),0.,ground+raw_y*.35,0.,size=1,depth=1,seed=seed,**SCENE_OPTIONS)
        chamber_value=float(chamber['density'][0,0,0])
        assert chamber_value<-.05,('Advertised underground chamber centre remains solid',biome,label,chamber_value)
        metrics['chamber_centre_density']=chamber_value
        assert metrics['solid_removed_samples']>0,('Underground excavation absent',biome,label)
        assert metrics['columns_with_open_roof_clearance_at_least_1_8m']>0,('Cave has no sampled opening to exterior',biome,label,metrics)
    return sample,metrics


def render(output,profiles,sections):
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    fig,axes=plt.subplots(7,2,figsize=(13,20),constrained_layout=True)
    primaries=('ice_massif','frozen_mountain','tundra_peak','taiga_peak','lake_basin','rocky_hole','deep_chasm')
    secondaries=('ice_rift','underground_cave','twenty_metre_rock','ancient_river','bog_hole','rocky_hole','terraced_mountain')
    for biome in range(7):
        for column,label in enumerate((primaries[biome],secondaries[biome])):
            ax=axes[biome,column]
            if column==1 and label in sections:
                sample=sections[label];d=sample['density'];mid=d.shape[0]//2
                x=np.arange(d.shape[2])*sample['spacing']
                y=sample['min_y']+np.arange(d.shape[1])*sample['step_y']
                base=float(sample['columns'][mid,d.shape[2]//2,0]);y-=base
                ax.contourf(x-x.mean(),y,d[mid],levels=[float(d.min())-1,0,float(d.max())+1],colors=['#eff6fc','#566878'])
                ax.plot(x-x.mean(),sample['columns'][mid,:,0]-base,color='#c96c21',lw=1,label='2D relief')
                ax.set_title(f'{BIOME_NAMES[biome]} — {label} (density section)')
            else:
                p=profiles[label];x=np.linspace(-p['extent'],p['extent'],len(p['after']))
                base=p['baseline']
                ax.plot(x,p['after']-base,color='#255673',label='Revised')
                if p['before'] is not None:ax.plot(x,p['before']-base,color='#bd792e',label='Previous',ls='--')
                ax.set_title(f'{BIOME_NAMES[biome]} — {label}')
            ax.set_xlabel('Horizontal metres');ax.set_ylabel('Relative height (m)')
            ax.set_aspect('equal',adjustable='datalim');ax.grid(alpha=.2)
            handles,_=ax.get_legend_handles_labels()
            if handles:ax.legend(fontsize=8)
    fig.suptitle('ENDURANCE — 7 revised cold biomes / actual GLSL probes\nEqual physical X/Y scale; forced biomes; not gameplay or performance certification',fontsize=14)
    fig.savefig(output/'cold_biome_profiles.png',dpi=135)
    plt.close(fig)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--reference-shaders',type=Path)
    parser.add_argument('--output',type=Path)
    parser.add_argument('--seed',type=int,default=1337)
    args=parser.parse_args()
    records=[];profiles={};sections={}
    fingerprints={b:source_fingerprint(b) for b in range(7)}
    try:
        with GL() as gl:
            print(f'OpenGL {gl.version}; {gl.renderer}',flush=True)
            height=BiomeProbe(gl,source=HEIGHT_PROBE)
            reference=BiomeProbe(gl,directory=args.reference_shaders,source=HEIGHT_PROBE) if args.reference_shaders else None
            density=DensityProbe(gl);fields=FieldRunner(gl)
            for biome,label,rate,salt,expected in FEATURES:
                sites=anchors(gl,biome,rate,salt,args.seed)
                site,h,before,delta=select_site(height,reference,biome,sites,args.seed,expected)
                record={'biome':biome,'name':BIOME_NAMES[biome],'feature':label,'rkm':rate,
                    'nominal_cell_spacing_m':1000./rate,'anchor':site[:2].tolist(),
                    'height_m':h,'reference_height_m':before,'height_change_m':delta,
                    'expected_isolated_change_m':expected,'placement_cells_probed':36,
                    'selection':'nearest anchor with isolated target contribution (when reference supplied)',
                    'source_sha256':fingerprints[biome]}
                extent=max(180.,site[2]*1.1)
                origin=tuple(site[:2]-extent)
                after=height_grid(height,biome,origin,2*extent/128.,size=129,seed=args.seed,**SCENE_OPTIONS)[:,:,0]
                prior=height_grid(reference,biome,origin,2*extent/128.,size=129,seed=args.seed,**SCENE_OPTIONS)[:,:,0] if reference else None
                profiles[label]={'after':after[64], 'before':None if prior is None else prior[64],
                    'extent':extent,'baseline':before if before is not None else h}
                if label in ('ice_rift','underground_cave','twenty_metre_rock','bog_hole','rocky_hole'):
                    sample,metrics=feature_volume(height,density,biome,label,site,args.seed)
                    record['volume']=metrics
                    record['pipeline']=check_targeted_pipeline(fields,density,sample,biome,args.seed)
                    sections[label]=sample
                else:
                    # Compare true production accumulation and shared LOD samples
                    # around a surface point, even when this feature has no 3D band.
                    sample=density.sample(biome,tuple(site[:2]-4),1.,h-4.,1.,size=9,depth=9,seed=args.seed,**SCENE_OPTIONS)
                    record['pipeline']=check_targeted_pipeline(fields,density,sample,biome,args.seed)
                assert source_fingerprint(biome)==fingerprints[biome],('Sources changed during measurement',biome)
                records.append(record)
                change=f'{delta:+.2f} m' if delta is not None else 'reference omitted'
                print(f'PASS {biome:02d} {label}: {rate:g} RKM, {change}',flush=True)
                if args.output:
                    args.output.mkdir(parents=True,exist_ok=True)
                    (args.output/'cold_biome_revision.json').write_text(json.dumps({'seed':args.seed,'renderer':gl.renderer,
                        'reference_shaders':str(args.reference_shaders) if args.reference_shaders else None,
                        'note':'Targeted diagnostic sites selected among 36 cells per feature, not frequency estimates. RKM = inverse nominal cell spacing, not guaranteed path encounters. Heights at detailScale=.35; forced biomes; no gameplay certification.',
                        'records':records},indent=2)+'\n')
            if reference:
                options=dict(SCENE_OPTIONS,uHeightAmplitude=0.)
                new=height_grid(height,5,(-1000.,-1000.),31.25,size=65,seed=args.seed,**options)[:,:,0]
                old=height_grid(reference,5,(-1000.,-1000.),31.25,size=65,seed=args.seed,**options)[:,:,0]
                assert np.allclose(new,2*old,atol=.0001,rtol=1e-6),('Rocky tundra relief is not doubled',float(np.max(np.abs(new-2*old))))
                print('PASS RockyTundra: exact doubled biome relief at 4,225 points',flush=True)
            assert all(source_fingerprint(b)==fingerprints[b] for b in range(7)), 'Sources changed during measurement'
            if args.output:render(args.output,profiles,sections)
    except GLUnavailable as error:
        print(f'SKIP {error}',file=sys.stderr);return 77
    return 0

if __name__=='__main__':raise SystemExit(main())
