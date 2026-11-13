#!/usr/bin/env python3
"""Measure actual 3D polar walls, not just heightfield roughness.

A test-only second compilation omits the wall delta, retaining identical columns,
wide bands, shelves and caves. Cross-sections and production LOD checks sample
real GLSL. No performance or player collision claims are made.
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
from check_biome_landforms import (HEIGHT_PROBE, height_grid, DensityProbe,
    SCENE_OPTIONS, check_targeted_pipeline, source_fingerprint)
from check_gpu import FieldRunner
from gl_headless import GL

SECTION=r'''
layout(local_size_x=8,local_size_y=8) in;
struct ProbeSample { vec4 terrain; vec4 volume; vec4 samples; vec4 shape; };
layout(std430,binding=6) writeonly buffer ProbeOutput { ProbeSample outputValues[]; };
uniform uint uProbeSize;
uniform vec2 uProbeOrigin;
uniform float uProbeSpacing;
uniform float uProbeMinY;
uniform float uProbeStepY;
void main() {
    uvec2 i=gl_GlobalInvocationID.xy;
    if(any(greaterThanEqual(i,uvec2(uProbeSize))))return;
    float across=(float(i.x)-float(uProbeSize-1u)*.5)*uProbeSpacing;
    vec2 xz=uProbeOrigin+AXIS*across;
    vec3 p=vec3(xz.x,uProbeMinY+float(i.y)*uProbeStepY,xz.y);
    FieldColumn c=fieldProbeColumn(xz);
    LandSite rift=RIFT_CALL;
    float cut=polarRiftMask(rift);
    float depth=polarRiftDepth(rift)*(uDetailScale/.35);
    float rim=c.terrain.x+c.blend.y+cut*depth;
    float delta=polarRiftWallDelta(p,c,rift);
    float density=fieldProbeDensity(p,c);
    outputValues[i.x+i.y*uProbeSize]=ProbeSample(
        vec4(c.terrain.x,c.terrain.w,rim,depth),
        vec4(density,delta,polarRiftWallCoverage(rift),cut),
        vec4(xz,rift.mask,(rim-p.y)/depth),c.blend);
}
'''

class NoWallProbe(BiomeProbe):
    def program_for(self,biome):
        if biome not in self.programs:
            directory=self.directory/'biomes'/BIOME_NAMES[biome]
            relief=biome_recipe_source(directory/'relief.C.glsl')
            volume=biome_recipe_source(directory/'volume.C.glsl')
            volume=re.sub(r'^#version[^\n]*\n','',volume,count=1)
            line='float wallDelta = polarRiftWallDelta(p, column, rift);'
            assert volume.count(line)==1,'Update omission adapter after recipe change'
            volume=volume.replace(line,'float wallDelta = 0.0; // Diagnostic only.')
            self.programs[biome]=self.gl.compile([(0x91B9,relief+volume+profile_probe_source()+self.main_source)],'PolarIce without wall displacement')
        return self.programs[biome]


def wall_positions(density,x):
    # Innermost solid-to-air crossing on each side of the central opening.
    center=len(x)//2
    result=np.full((len(density),2),np.nan)
    for y,row in enumerate(density):
        if row[center]>=0:continue
        for side,indices in enumerate((np.arange(center-1,-1,-1),np.arange(center+1,len(x)))):
            for j in indices:
                if row[j]>=0:
                    k=j+1 if side==0 else j-1
                    t=row[j]/(row[j]-row[k])
                    result[y,side]=x[j]+(x[k]-x[j])*t
                    break
    return result


def render(path,x,y,before,after,positions,oldpositions):
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    from matplotlib.colors import ListedColormap
    fig,axes=plt.subplots(1,3,figsize=(12,5),constrained_layout=True)
    for ax,d,title in zip(axes[:2],(before,after),('Same field, wall deformation off','Actual 3D wall deformation')):
        ax.imshow(d>0,origin='lower',extent=[x[0],x[-1],y[0],y[-1]],
                  cmap=ListedColormap(['#192d43','#bedce7']),interpolation='nearest',aspect='equal')
        ax.contour(x,y,d,levels=[0],colors=['#477f9e'],linewidths=.6)
        ax.set_title(title);ax.set_xlabel('Across rift (m)');ax.set_ylabel('World height (m)')
    ax=axes[2]
    for side in range(2):
        ax.plot(oldpositions[:,side],y,color='#a9b8c1',label='Before' if side==0 else None)
        ax.plot(positions[:,side],y,color='#1e6b9b',label='After' if side==0 else None)
    ax.set_title('Wall position with depth');ax.set_xlabel('Across rift (m)');ax.set_ylabel('World height (m)')
    ax.grid(alpha=.2);ax.legend()
    fig.suptitle('Production GLSL cross-section: unchanged opening and floor; lateral ledges and recesses\nNo textures, no vertical exaggeration; diagnostic, not an in-game screenshot',fontsize=11)
    fig.savefig(path,dpi=100)
    plt.close(fig)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--seed',type=int,default=1337)
    args=parser.parse_args();args.output.mkdir(parents=True,exist_ok=True)
    fingerprint=source_fingerprint(0)
    with GL() as gl:
        placement=next(p for p in placements(0) if p.variable=='rift')
        rows,_=anchor_probe(gl,0,placement,args.seed,size=6)
        height_source=HEIGHT_PROBE.replace('vec4(0),c.blend',
            f'vec4(float(polarRiftBranchCount({placement.call()})),0,0,0),c.blend')
        height=BiomeProbe(gl,source=height_source)
        chosen=None
        # Isolate a broad unbranched opening: when two cuts join, the innermost
        # crossing can jump to another wall and overstate noise displacement.
        # The separate variation and coverage fixtures exercise branched cuts.
        for site in rows:
            if not .45<site[8]<.9:continue
            column=height_grid(height,0,tuple(site[:2]),0,size=1,seed=args.seed,**SCENE_OPTIONS)[0,0]
            if column[0]<700 and column[8]==0:
                chosen=(site,column);break
        assert chosen is not None,'No suitable diagnostic site'
        site,column=chosen
        axis=np.array((site[5],site[7]));axis/=np.linalg.norm(axis)
        source=SECTION.replace('AXIS',f'vec2({axis[0]:.9g},{axis[1]:.9g})').replace('RIFT_CALL',placement.call('xz'))
        actual=BiomeProbe(gl,source=source);baseline=NoWallProbe(gl,source=source)
        # Width encompasses the main cut and both walls; height includes
        # complete surface transitions outside both the opening and floor.
        nominal_depth=700.+200.*site[8]
        extent=800.;size=385
        low=float(column[0]-100.);high=float(column[0]+nominal_depth+100.)
        options=dict(biome=0,size=size,origin=tuple(site[:2]),spacing=2*extent/(size-1),
            min_y=low,step_y=(high-low)/(size-1),uSeed=args.seed,**SCENE_OPTIONS)
        a=actual.run(**options);b=baseline.run(**options)
        assert a['raw']==actual.run(**options)['raw'],'3D wall noise is not deterministic'
        aa=np.asarray(a['values'],dtype=np.float32).reshape(size,size,16)
        bb=np.asarray(b['values'],dtype=np.float32).reshape(size,size,16)
        after=aa[:,:,4];before=bb[:,:,4];difference=after-before
        assert np.array_equal(aa[:,:,:4],bb[:,:,:4]),'Wall-only comparison changed columns'
        y=np.linspace(low,high,size);x=np.linspace(-extent,extent,size)
        fraction=aa[:,:,11]
        middle=(fraction>.20)&(fraction<.80)
        outside=(fraction<=.04)|(fraction>=.96)
        assert np.max(np.abs(difference[outside]))<.001,'Opening or floor changed'
        outside_band=np.abs(y[:,None]-aa[:,:,0]-aa[:,:,13])>=aa[:,:,1]*.5
        assert np.max(np.abs(difference[outside_band]),initial=0)<.001,'Sculpture escaped its volume band'
        # A signed geometry change far from the old 201.6m band is the key
        # regression: height-noise added around the floor cannot pass this.
        changed=(after>0)!=(before>0)
        far=np.abs(y[:,None]-aa[:,:,0])>202.
        assert int(np.sum(changed&middle&far))>50,'No deep lateral wall displacement'
        # A heightfield has one vertical solid/air transition. Count columns
        # with multiple transitions strictly inside the deep wall section.
        vertical_changes=((after[:-1]>0)!=(after[1:]>0))&middle[:-1]&middle[1:]
        overhang_columns=int(np.sum(vertical_changes.sum(axis=0)>=2))
        assert overhang_columns>2,('No sampled overhangs',overhang_columns)
        positions=wall_positions(after,x);oldpositions=wall_positions(before,x)
        offset=positions-oldpositions
        central_rows=(np.nanmedian(fraction,axis=1)>.2)&(np.nanmedian(fraction,axis=1)<.8)
        measured=np.abs(offset[central_rows]);measured=measured[np.isfinite(measured)]
        assert measured.size and measured.max()>8.,('Wall shift too small',measured)
        # Actual dispatch + exact shared LOD points, targeted where geometry
        # changes beyond the old band. The cube is sampled independently.
        candidate=np.argwhere(changed&middle&far)
        j,i=candidate[len(candidate)//2]
        world=aa[j,i,8:10]
        sample=DensityProbe(gl).sample(0,tuple(world-4),1.,float(y[j]-4),1.,size=9,depth=9,seed=args.seed,**SCENE_OPTIONS)
        pipeline=check_targeted_pipeline(FieldRunner(gl),DensityProbe(gl),sample,0,args.seed)
        result=dict(seed=args.seed,renderer=gl.renderer,source_sha256=fingerprint,
            anchor=site[:2].tolist(),variant=float(site[8]),branches=int(column[8]),sampling_m=[float(x[1]-x[0]),float(y[1]-y[0])],
            maximum_measured_lateral_shift_m=float(measured.max()),median_measured_lateral_shift_m=float(np.median(measured)),
            overhang_columns=overhang_columns,deep_changed_samples=int(np.sum(changed&middle&far)),
            maximum_band_m=float(aa[:,:,1].max()),opening_floor_max_difference=float(np.max(np.abs(difference[outside]))),
            pipeline=pipeline,note='One targeted vertical section through production GLSL; not a gameplay or performance certification. Baseline omits only the wall delta and keeps identical columns and other 3D geometry.')
        assert source_fingerprint(0)==fingerprint,'Sources changed during measurement'
        (args.output/'polar_rift_volume.json').write_text(json.dumps(result,indent=2)+'\n')
        render(args.output/'polar_rift_volume.png',x,y,before,after,positions,oldpositions)
        print(json.dumps(result,indent=2),flush=True)
    return 0

if __name__=='__main__':raise SystemExit(main())
