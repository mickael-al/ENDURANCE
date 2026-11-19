#!/usr/bin/env python3
"""Validate sparse large Perlin cavities on the actual PolarIce mountain field.

The comparison omits only polarMassifCarve in a diagnostic compilation. Columns,
wide bands, rifts and shelves remain identical. Figures sample actual GLSL.
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
from check_biome_landforms import (DensityProbe, DENSITY_PROBE, SCENE_OPTIONS,
    check_targeted_pipeline, source_fingerprint)
from check_gpu import FieldRunner
from gl_headless import GL


class OmitCaves:
    def program_for(self, biome):
        if biome not in self.programs:
            directory=self.directory/'biomes'/BIOME_NAMES[biome]
            relief=biome_recipe_source(directory/'relief.C.glsl')
            volume=re.sub(r'^#version[^\n]*\n','',biome_recipe_source(directory/'volume.C.glsl'),count=1)
            line='finalTarget = polarMassifCarve(p, column, massif, massifCaves, finalTarget);'
            assert volume.count(line)==1,'Update omission adapter after recipe change'
            volume=volume.replace(line,'// Diagnostic: omit only mountain cavities.')
            self.programs[biome]=self.gl.compile([(0x91B9,relief+volume+profile_probe_source()+self.main_source)],'PolarIce without massif cavities')
        return self.programs[biome]


class Sweep(DensityProbe):
    def __init__(self,gl,source):
        super().__init__(gl)
        self.main_source=source


class BaselineSweep(OmitCaves,Sweep): pass
class BaselineSection(OmitCaves,BiomeProbe): pass


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
 vec2 xz=uProbeOrigin+vec2(across,0);
 vec3 p=vec3(xz.x,uProbeMinY+float(i.y)*uProbeStepY,xz.y);
 FieldColumn c=fieldProbeColumn(xz);
 outputValues[i.x+i.y*uProbeSize]=ProbeSample(c.terrain,
  vec4(fieldProbeDensity(p,c),p.y,xz),vec4(0),c.blend);
}
'''


def render(path,x,y,before,after):
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    from matplotlib.colors import ListedColormap
    fig,axes=plt.subplots(1,2,figsize=(12,5),constrained_layout=True)
    for ax,d,title in zip(axes,(before,after),('Same field, mountain cavities off','Sparse broad Perlin cavities')):
        ax.imshow(d>0,origin='lower',extent=[x[0],x[-1],y[0],y[-1]],
            cmap=ListedColormap(['#192d43','#bedce7']),interpolation='nearest',aspect='equal')
        ax.contour(x,y,d,levels=[0],colors=['#477f9e'],linewidths=.7)
        ax.set_title(title);ax.set_xlabel('Horizontal distance (m)');ax.set_ylabel('World height (m)')
    fig.suptitle('Production GLSL section through a sampled mountain cavity\nNo textures or vertical exaggeration; diagnostic, not an in-game screenshot',fontsize=11)
    fig.savefig(path,dpi=100);plt.close(fig)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--seed',type=int,default=1337)
    args=parser.parse_args();args.output.mkdir(parents=True,exist_ok=True)
    fingerprint=source_fingerprint(0)
    with GL() as gl:
        massif=next(p for p in placements(0) if p.variable=='massif')
        rift=next(p for p in placements(0) if p.variable=='rift')
        source=DENSITY_PROBE.replace('FieldColumn c=fieldProbeColumn(xz);',
            'FieldColumn c=fieldProbeColumn(xz);\nLandSite m='+massif.call('xz')+';\nLandSite r='+rift.call('xz')+';')
        source=source.replace('fieldProbeVolume(c).x,c.blend.w','polarMassifCaveSupport(m,r),polarMassifCaveDepth(m)')
        source=source.replace('float y=uProbeMinY+float(j)*uProbeStepY;',
            'float y=c.terrain.x+c.blend.y+uProbeMinY+float(j)*uProbeStepY;')
        actual=Sweep(gl,source);baseline=BaselineSweep(gl,source)
        sites,_=anchor_probe(gl,0,massif,args.seed,size=6)
        records=[];selected=None;total_solid=total_eligible=total_removed=total_roof=total_open=0
        for site in sites[:6]:
            extent=float(site[2])*1.1;size=129;depth=39;step_y=12.;min_y=-432.
            opts=dict(biome=0,origin=tuple(site[:2]-extent),spacing=2*extent/(size-1),
                min_y=min_y,step_y=step_y,size=size,depth=depth,seed=args.seed,**SCENE_OPTIONS)
            a=actual.sample(**opts);b=baseline.sample(**opts)
            after=a['density'];before=b['density'];columns=a['columns']
            assert np.array_equal(columns,b['columns']),'Omission changed columns'
            assert np.max(after-before)<.001,'Mountain cavities added solid'
            removed=(after<0)&(before>0)
            support=columns[:,:,2];limit=columns[:,:,3]
            protected=support<=.08
            assert np.max(np.abs((after-before)[protected[:,None,:].repeat(depth,axis=1)]),initial=0)<.001,'Protected crest/skirts/rift changed'
            below=-min_y-np.arange(depth)*step_y
            root=below[None,:,None]>=limit[:,None,:]
            assert np.max(np.abs((after-before)[root]),initial=0)<.001,'Mountain root changed'
            outside=np.abs(below)[None,:,None]>=columns[:,None,:,1]*.5
            assert np.max(np.abs((after-before)[outside]),initial=0)<.001,'Cavity escaped its volume band'
            above=np.maximum.accumulate((after>0)[:,::-1,:],axis=1)[:,::-1,:]
            roofed=removed&above
            surface=int(np.flatnonzero(below==0)[0])
            openings=(after[:,surface,:]<-1.)&(before[:,surface,:]>=-.001)&(before[:,-1,:]<0)
            eligible=(support[:,None,:]>.08)&(below[None,:,None]>0)&(below[None,:,None]<limit[:,None,:])
            neligible=int(((before>0)&eligible).sum())
            nsolid=int((before>0).sum());nremoved=int(removed.sum())
            assert nremoved/max(neligible,1)<.10,'Mountain became too porous'
            total_solid+=nsolid;total_eligible+=neligible;total_removed+=nremoved;total_roof+=int(roofed.sum());total_open+=int(openings.sum())
            record=dict(anchor=site[:2].tolist(),variant=float(site[8]),solid_samples=nsolid,
                eligible_flank_solid_samples=neligible,removed_samples=nremoved,
                removed_fraction=nremoved/max(nsolid,1),eligible_flank_removed_fraction=nremoved/max(neligible,1),
                horizontal_sampling_m=float(opts['spacing']),
                roofed_removed_samples=int(roofed.sum()),surface_open_columns=int(openings.sum()),
                maximum_excavation_below_surface_m=float(np.max(np.broadcast_to(below[None,:,None],removed.shape)[removed],initial=0)),
                protected_columns=int(protected.sum()),maximum_band_m=float(columns[:,:,1].max()))
            records.append(record)
            print(json.dumps(record),flush=True)
            if roofed.any() and (selected is None or int(roofed.sum())>selected[0]):
                z,j,x=np.argwhere(roofed)[len(np.argwhere(roofed))//2]
                world=np.asarray((opts['origin'][0]+x*opts['spacing'],opts['origin'][1]+z*opts['spacing']))
                y=float(columns[z,x,0]+min_y+j*step_y)
                selected=(int(roofed.sum()),world,y,opts,after.copy())
        assert total_removed>100 and total_roof>20 and total_open>5,('Missing sparse 3D cavities/openings',total_removed,total_roof,total_open)
        assert selected is not None
        _,world,y,opts,previous=selected
        assert np.array_equal(previous,actual.sample(**opts)['density']),'Cavities are not deterministic'
        probe=DensityProbe(gl)
        sample=probe.sample(0,tuple(world-4),1.,y-4,1.,size=9,depth=9,seed=args.seed,**SCENE_OPTIONS)
        pipeline=check_targeted_pipeline(FieldRunner(gl),probe,sample,0,args.seed)
        section=BiomeProbe(gl,source=SECTION);old_section=BaselineSection(gl,source=SECTION)
        size=385;extent=700.;low=y-260.;high=y+520.
        options=dict(biome=0,size=size,origin=tuple(world),spacing=2*extent/(size-1),
            min_y=low,step_y=(high-low)/(size-1),uSeed=args.seed,**SCENE_OPTIONS)
        aa=np.asarray(section.run(**options)['values']).reshape(size,size,16)
        bb=np.asarray(old_section.run(**options)['values']).reshape(size,size,16)
        render(args.output/'polar_massif_caves.png',np.linspace(-extent,extent,size),np.linspace(low,high,size),bb[:,:,4],aa[:,:,4])
        assert source_fingerprint(0)==fingerprint,'Sources changed during test'
        result=dict(seed=args.seed,renderer=gl.renderer,source_sha256=fingerprint,
            sites_measured=len(records),solid_samples=total_solid,eligible_flank_solid_samples=total_eligible,removed_samples=total_removed,
            removed_fraction=total_removed/total_solid,eligible_flank_removed_fraction=total_removed/total_eligible,roofed_removed_samples=total_roof,
            surface_open_columns=total_open,sites=records,pipeline=pipeline,
            figure_center=[float(world[0]),y,float(world[1])],
            note='Six nearest active massif sites, surface-relative sample layers every 12m; horizontal sampling depends on the site radius. The diagnostic baseline omits only the final mountain cavity CSG. Removed fraction is of this sampled near-surface solid, not total mountain volume. The eligible-flank fraction excludes protected columns and samples outside the allowed excavation depth. Surface openings are negative density at the original surface with air above. Roofed samples do not certify player access. OpenGL software renderer, not a performance or collision test.')
        (args.output/'polar_massif_caves.json').write_text(json.dumps(result,indent=2)+'\n')
        print(json.dumps({k:result[k] for k in ('sites_measured','solid_samples','removed_samples','removed_fraction','eligible_flank_removed_fraction','roofed_removed_samples','surface_open_columns','pipeline')},indent=2),flush=True)
    return 0

if __name__=='__main__':raise SystemExit(main())
