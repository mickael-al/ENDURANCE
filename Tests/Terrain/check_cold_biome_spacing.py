#!/usr/bin/env python3
"""Measure cold-biome footprint, spacing and warped-cell continuity on actual GLSL.

Requires the immediately preceding shaders for a meaningful before/after.
The atlas is a sampled heightfield, not a screenshot or a GPU performance test.
"""
from __future__ import annotations
import argparse
import json
from pathlib import Path
import numpy as np
from scipy.spatial.distance import cdist
from biome_placement_probe import (anchor_probe, placement_for, OmitPlacementProbe,
                                  INVERSE_SPARSE)
from biome_test_support import BIOME_NAMES
from check_biomes import BiomeProbe
from check_biome_landforms import HEIGHT_PROBE, height_grid, SCENE_OPTIONS, source_fingerprint
from gl_headless import GL

MOUNTAINS=((0,111,5.,4.,(440.,590.)),(1,211,4.,2.5,(415.,565.)),
           (2,321,6.,5.,(790.,810.)),(3,421,4.,3.,(690.,710.)))


def nearest_distances(rows):
    points=np.asarray(rows)[:,:2]
    distances=cdist(points,points)
    np.fill_diagonal(distances,np.inf)
    closest=distances.min(axis=1)
    return {'minimum_m':float(closest.min()),'median_m':float(np.median(closest)),
            'maximum_m':float(closest.max())}


def region(probe,biome,center,extent,size=193,seed=1337):
    return height_grid(probe,biome,tuple(np.asarray(center)-extent),2*extent/(size-1),
                       size=size,seed=seed,**SCENE_OPTIONS)[:,:,0]


def isolated_shape(gl,height,biome,salt,site,directory,seed,extent=None):
    extent=extent or float(site[2])*1.6
    omitted=OmitPlacementProbe(gl,salt,anchor=site[10:12],directory=directory,source=HEIGHT_PROBE)
    actual=region(height,biome,site[:2],extent,seed=seed)
    absent=region(omitted,biome,site[:2],extent,seed=seed)
    delta=actual-absent
    peak=float(np.max(np.abs(delta)))
    step=2*extent/(len(delta)-1)
    area=float(np.sum(np.abs(delta)>peak*.1)*step*step)
    return delta,extent,{'peak_absolute_m':peak,'area_above_tenth_peak_m2':area,
                        'equivalent_diameter_above_tenth_peak_m':2*np.sqrt(area/np.pi)}


def check_warp_edges(gl,biome,placement,seed):
    # Sample both sides of true DEFORMED cell boundaries, not world-grid lines.
    # The compact population must be absent throughout the .025*S margin.
    source=INVERSE_SPARSE+r'''
layout(local_size_x=8,local_size_y=8) in;
struct ProbeSample { vec4 terrain; vec4 volume; vec4 samples; vec4 shape; };
layout(std430,binding=6) writeonly buffer ProbeOutput { ProbeSample outputValues[]; };
uniform uint uProbeSize;
uniform vec2 uProbeOrigin;
uniform float uProbeSpacing;
void main() {
    uvec2 i=gl_GlobalInvocationID.xy;
    if(any(greaterThanEqual(i,uvec2(uProbeSize))))return;
    vec2 d=uProbeOrigin+vec2(i)*uProbeSpacing;
    vec2 p=probeWorldFromDomain(d,SPACING,SALT);
    LandSite site=SITE_CALL;
    FieldColumn c=fieldProbeColumn(p);
    outputValues[i.x+i.y*uProbeSize]=ProbeSample(c.terrain,
        vec4(site.mask,length(landSparseDomain(p,SPACING,SALT)-d),0,0),vec4(0),c.blend);
}
'''
    source=source.replace('SITE_CALL',placement.call()).replace('SPACING',str(placement.spacing)).replace('SALT',str(placement.salt)+'u')
    probe=BiomeProbe(gl,source=source)
    spacing=placement.spacing
    maximum=0.
    for axis in (0,1):
        for cell in (-2,-1,0,1,2):
            # Sample a noncentral part of the boundary to exercise changing warp.
            point=np.array((cell*spacing,.317*spacing))
            if axis:point=point[::-1]
            direction=np.zeros(2);direction[axis]=1
            previous=None
            for step in (.8,.2,.05):
                pair=[]
                for sign in (-1,1):
                    data=height_grid(probe,biome,tuple(point+sign*direction*step),0,size=1,seed=seed,**SCENE_OPTIONS)[0,0]
                    assert data[4]==0.,('Feature reaches placement boundary',biome,placement.salt,data[4])
                    pair.append(data)
                jump=np.abs(pair[0][[0,3]].astype(float)-pair[1][[0,3]].astype(float))
                if previous is not None:
                    assert np.all(jump<=previous*.7+.04),('Nonconvergent warped boundary',biome,cell,axis,previous,jump)
                previous=jump
            maximum=max(maximum,float(previous[0]))
    return maximum


def render(output,records,images,rift):
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    fig,axes=plt.subplots(4,3,figsize=(12,14),constrained_layout=True)
    for row,record in enumerate(records):
        old,new,extent,shape_old,shape_new,local_extent=images[row]
        for col,data in enumerate((old,new)):
            ax=axes[row,col]
            ax.imshow(data,origin='lower',extent=np.array([-extent,extent,-extent,extent])/1000,
                      cmap='terrain',vmin=-100,vmax=850,interpolation='nearest')
            ax.set_title(f"{record['biome']} — {'before' if col==0 else 'after'}")
            ax.set_xlabel('World X (km)');ax.set_ylabel('World Z (km)')
        ax=axes[row,2]
        x=np.linspace(-local_extent,local_extent,len(shape_new))/1000
        # Profiles cross each corresponding anchor at equal physical scale.
        ax.plot(x,shape_old[len(shape_old)//2],label='Before',color='#b9692b')
        ax.plot(x,shape_new[len(shape_new)//2],label='After',color='#235b80')
        ax.set_xlabel('Distance from anchor (km)');ax.set_ylabel('Added height (m)')
        ax.set_title(f"Isolated massif; footprint x{record['nominal_width_ratio']:g}")
        ax.grid(alpha=.2);ax.legend()
    fig.suptitle('Actual GLSL samples — identical world extent and height scale within each row\nProfiles isolate the mountain from all surrounding ground; no game rendering',fontsize=12)
    fig.savefig(output/'cold_spacing_atlas.png',dpi=90)
    plt.close(fig)
    # A palette preserves the measured geometry while keeping repository
    # review images small enough to open quickly over the GitHub connector.
    from PIL import Image
    with Image.open(output/'cold_spacing_atlas.png') as image:
        image.convert('RGB').quantize(colors=128).save(output/'cold_spacing_atlas.png',optimize=True)
    fig,axes=plt.subplots(1,3,figsize=(12,3.7),constrained_layout=True)
    old,new,extent=rift
    for i,data in enumerate((old,new)):
        axes[i].imshow(data,origin='lower',extent=[-extent,extent,-extent,extent],cmap='Blues_r',vmin=-900,vmax=0)
        axes[i].set_title('Polar rift '+('before' if i==0 else 'after'))
        axes[i].set_xlabel('Metres from anchor');axes[i].set_ylabel('Metres from anchor')
    x=np.linspace(-extent,extent,len(new))
    axes[2].plot(x,old[len(old)//2],label='Before')
    axes[2].plot(x,new[len(new)//2],label='After')
    axes[2].set_xlabel('Metres from anchor');axes[2].set_ylabel('Excavated depth (m)');axes[2].legend();axes[2].grid(alpha=.2)
    fig.savefig(output/'polar_rift_contour.png',dpi=90)
    plt.close(fig)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--reference-shaders',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--seed',type=int,default=1337)
    args=parser.parse_args()
    records=[];images=[]
    args.output.mkdir(parents=True,exist_ok=True)
    with GL() as gl:
        height=BiomeProbe(gl,source=HEIGHT_PROBE)
        prior=BiomeProbe(gl,directory=args.reference_shaders,source=HEIGHT_PROBE)
        fingerprints={b:source_fingerprint(b) for b in range(4)}
        for biome,salt,spacing_ratio,width_ratio,expected in MOUNTAINS:
            placement=placement_for(biome,salt)
            oldplacement=placement_for(biome,salt,args.reference_shaders)
            newrows,newall=anchor_probe(gl,biome,placement,args.seed)
            oldrows,oldall=anchor_probe(gl,biome,oldplacement,args.seed,directory=args.reference_shaders)
            assert abs(placement.spacing/oldplacement.spacing-spacing_ratio)<1.e-5
            assert np.allclose(newall[:,2]/oldall[:,2],width_ratio,rtol=1.e-5)
            site=newrows[0]
            index=np.argmin(np.linalg.norm(newall[:,:2]-site[:2],axis=1))
            oldsite=oldall[index]
            extent=float(site[2])*1.6
            shape,_,metrics=isolated_shape(gl,height,biome,salt,site,height.directory,args.seed,extent)
            oldshape,_,oldmetrics=isolated_shape(gl,prior,biome,salt,oldsite,args.reference_shaders,args.seed,extent)
            assert expected[0]<metrics['peak_absolute_m']<expected[1],('Peak height changed',biome,metrics)
            actual_ratio=metrics['equivalent_diameter_above_tenth_peak_m']/oldmetrics['equivalent_diameter_above_tenth_peak_m']
            assert actual_ratio>width_ratio*.8,('Widened footprint ineffective',biome,actual_ratio,width_ratio)
            newdistance=nearest_distances(newrows);olddistance=nearest_distances(oldrows)
            assert newdistance['median_m']/olddistance['median_m']>spacing_ratio*.8,('Spacing ineffective',biome,newdistance,olddistance)
            boundary=check_warp_edges(gl,biome,placement,args.seed)
            world_extent=placement.spacing*2.
            newmap=region(height,biome,(0,0),world_extent,size=257,seed=args.seed)
            oldmap=region(prior,biome,(0,0),world_extent,size=257,seed=args.seed)
            record={'biome':BIOME_NAMES[biome],'source_sha256':fingerprints[biome],
                'nominal_spacing_m':placement.spacing,'nominal_spacing_ratio':spacing_ratio,
                'nominal_width_ratio':width_ratio,'active_candidates':len(newrows),'sampled_cells':100,
                'nearest_neighbour_before':olddistance,'nearest_neighbour_after':newdistance,
                'isolated_footprint_before':oldmetrics,'isolated_footprint_after':metrics,
                'measured_width_ratio':actual_ratio,'maximum_final_boundary_jump_m':boundary,
                'world_anchor':site[:2].tolist(),'placement_anchor':site[10:12].tolist()}
            records.append(record);images.append((oldmap,newmap,world_extent,oldshape,shape,extent))
            print(f'PASS {BIOME_NAMES[biome]}: nominal spacing x{spacing_ratio:g}, width x{width_ratio:g}; measured width x{actual_ratio:.2f}; peak {metrics["peak_absolute_m"]:.1f} m',flush=True)
        placement=placement_for(0,109);oldplacement=placement_for(0,109,args.reference_shaders)
        rows,allrows=anchor_probe(gl,0,placement,args.seed)
        oldrows,oldall=anchor_probe(gl,0,oldplacement,args.seed,directory=args.reference_shaders)
        site=rows[0];index=np.argmin(np.linalg.norm(allrows[:,:2]-site[:2],axis=1));oldsite=oldall[index]
        assert placement.spacing/oldplacement.spacing==4.
        extent=max(site[2],oldsite[2])*1.4
        shape,_,metrics=isolated_shape(gl,height,0,109,site,height.directory,args.seed,extent)
        oldshape,_,oldmetrics=isolated_shape(gl,prior,0,109,oldsite,args.reference_shaders,args.seed,extent)
        assert 690<metrics['peak_absolute_m']<910,('Rift depth',metrics)
        rift_record={'nominal_spacing_ratio':4.,'before':oldmetrics,'after':metrics,
                     'nearest_neighbour_before':nearest_distances(oldrows),
                     'nearest_neighbour_after':nearest_distances(rows),
                     'maximum_final_boundary_jump_m':check_warp_edges(gl,0,placement,args.seed)}
        assert all(source_fingerprint(b)==fingerprints[b] for b in range(4)),'Sources changed during measurement'
        result={'seed':args.seed,'renderer':gl.renderer,'mountains':records,'polar_rift':rift_record,
                'note':'Nominal spacings and widths are measured in the warped placement domain. Nearest-neighbour distances and contours are real world-space probes. A walk need not encounter every candidate. Atlas is not a gameplay rendering.'}
        (args.output/'cold_spacing_revision.json').write_text(json.dumps(result,indent=2)+'\n')
        render(args.output,records,images,(oldshape,shape,extent))
        print(f'PASS Polar rift: depth {metrics["peak_absolute_m"]:.1f} m, spacing x4',flush=True)
    return 0


if __name__=='__main__':raise SystemExit(main())
