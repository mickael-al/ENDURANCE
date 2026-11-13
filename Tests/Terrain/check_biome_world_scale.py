#!/usr/bin/env python3
"""Measure current unforced climate areas and seeded site occurrence on actual GLSL.

The production classifier is executed unchanged on a regular world grid. Its
local weights are read using production fieldSurfaceWeight/fieldVolumeWeight.
For site anchors, only classifier coordinates are injected from an SSBO; all
classification and weighting code remains unchanged. This is a planimetric
sampling diagnostic, not a full 3D terrain, collision, or traversal test.
"""
from __future__ import annotations
import argparse
import json
import hashlib
import math
from pathlib import Path
import re
import struct
import sys

import numpy as np
from scipy import ndimage

from biome_test_support import ROOT, BIOME_NAMES, packed_biome_cells
from field_batch_runner import SHADERS, default_uniforms
from gl_headless import GL, GLUnavailable, shader_source


def scene_settings(path):
    scene=json.loads(path.read_text(),strict=False)
    components=[s for e in scene['emptyData'] for s in e['scripts']
                if s['pathName']=='ProceduralTerrainComponent']
    assert len(components)==1
    settings=json.loads(components[0]['data'])
    # viewDistance is passed unchanged to the LOD selector, which uses it as a
    # radius. The free camera is the scene's saved diagnostic center.
    return settings,[scene['freeCamPos']['x'],scene['freeCamPos']['z']]


class WorldProbe:
    def __init__(self,gl,settings):
        self.gl=gl;self.settings=settings
        source=shader_source(SHADERS/'field_batch_classify.C.glsl')
        self.classify=gl.compile([(0x91B9,source)],'unchanged production world classifier')
        injected=source.replace('shared uint localMasks[64];',
            'layout(std430,binding=8) readonly buffer PointPositions { vec4 positions[]; };\nshared uint localMasks[64];')
        original='vec2 world=fieldWorldPosition(ivec3(p.x,1,p.y)).xz;'
        assert injected.count(original)==1,'Update coordinate adapter after classifier change'
        injected=injected.replace(original,'vec2 world=positions[p.x+p.y*side].xy;')
        self.point_classify=gl.compile([(0x91B9,injected)],'production classifier at actual seeded anchors')
        self.decode=gl.compile([(0x91B9,'#version 450 core\n'+
            shader_source(SHADERS/'field_common.glsl')+shader_source(SHADERS/'field_weights.glsl')+r'''
layout(local_size_x=64) in;
layout(std430,binding=1) readonly buffer Columns { FieldColumn columns[]; };
layout(std430,binding=2) writeonly buffer Output { float values[]; };
uniform uint uPointCount;
void main() {
 uint i=gl_GlobalInvocationID.x;if(i>=uPointCount)return;
 FieldColumn c=columns[i];float discrepancy=0.;
 for(uint b=0u;b<26u;++b) {
  float w=fieldSurfaceWeight(c,b);values[i*28u+b]=w;
  discrepancy=max(discrepancy,abs(w-fieldVolumeWeight(c,b)));
 }
 values[i*28u+26u]=discrepancy;
 values[i*28u+27u]=uintBitsToFloat(c.detail.w);
}
''')],'production biome weights readback')
        self.sites=gl.compile([(0x91B9,'#version 450 core\n'+
            shader_source(SHADERS/'field_common.glsl')+shader_source(SHADERS/'biome_landforms_common.glsl')+r'''
layout(local_size_x=8,local_size_y=8) in;
layout(std430,binding=2) writeonly buffer Output { vec4 values[]; };
uniform uint uPointSide;
uniform float uSiteSpacing;uniform uint uSiteSalt;uniform float uSiteOccupancy;
uniform vec2 uCellOrigin;
void main() {
 uvec2 i=gl_GlobalInvocationID.xy;if(any(greaterThanEqual(i,uvec2(uPointSide))))return;
 vec2 p=(uCellOrigin+vec2(i)+.5)*uSiteSpacing;
 LandSite s=landSite(p,uSiteSpacing,uSiteSalt,uSiteOccupancy);
 LandSite centered=landSite(s.center,uSiteSpacing,uSiteSalt,uSiteOccupancy);
 values[i.x+i.y*uPointSide]=vec4(s.center,centered.mask>.5?s.radius:-1.,s.variant);
}
''')],'actual seeded landform anchors')
        self.mapping=gl.buffer(packed_biome_cells())
        self.jobs=gl.buffer(16*48);self.columns=gl.buffer(0);self.output=gl.buffer(0);self.points=gl.buffer(0)

    def classify_grid(self,side,spacing,origin,points=None):
        gl=self.gl;n=side*side;s=self.settings
        program=self.classify if points is None else self.point_classify
        gl.bind(self.mapping,7);gl.bind(self.jobs,6)
        gl.write(self.jobs,struct.pack('<4i8I',origin[0]+1,0,origin[1]+1,0,int(s['seed']),3,0,0,0,0,0,0)+bytes(15*48))
        gl.resize(self.columns,n*48);gl.bind(self.columns,1)
        params=default_uniforms(side-3,uBaseSpacing=spacing,uDetailScale=0.,
            uBaseHeight=s['baseHeight'],uHeightAmplitude=s['heightAmplitude'],
            uHeightFrequency=s['heightFrequency'],uSeaLevel=s['seaLevel'],uSeaBlendWidth=s['seaBlendWidth'])
        params.update({'uJobSlots':('1ui',1),'uColumnStride':('1ui',n),'uDensityStride':('1ui',1)})
        for name,args in params.items():gl.uniform(program,name,*args)
        if points is not None:
            padded=np.zeros((n,4),dtype='<f4');padded[:len(points),:2]=points
            gl.resize(self.points,padded.tobytes());gl.bind(self.points,8)
        gl.dispatch_extent(program,side,side)
        gl.resize(self.output,n*28*4);gl.bind(self.output,2)
        gl.uniform(self.decode,'uPointCount','1ui',n)
        gl.uniform(self.decode,'uBiomeOverride','1i',-1)
        gl.dispatch(self.decode,(n+63)//64)
        values=np.frombuffer(gl.read(self.output,n*28*4),dtype='<f4').copy().reshape(n,28)
        gl.check('world area/anchor weights')
        assert np.isfinite(values).all()
        assert np.allclose(values[:,:26].sum(axis=1),1.,atol=1e-6)
        # The authoritative matrices currently assign identical surface/volume
        # weights. Fail visibly if that changes; site selection then needs both.
        assert values[:,26].max()<1e-6,'Surface/volume weights diverged; report them separately'
        return values

    def anchors(self,biome,center,radius):
        gl=self.gl
        recipe=(SHADERS/'biomes'/BIOME_NAMES[biome]/'relief.C.glsl').read_text()
        args=re.findall(r'landSite\(p,\s*([\d.]+),\s*(\d+)u,\s*([\d.]+)\)',recipe)
        assert len(set(args))==1,(biome,'Update diagnostic for multiple placement families')
        spacing,salt,occupancy=map(float,args[0]);salt=int(salt)
        low=np.floor((np.asarray(center)-radius)/spacing).astype(int)
        high=np.floor((np.asarray(center)+radius)/spacing).astype(int)
        side=int((high-low).max()+1)
        gl.bind(self.mapping,7);gl.resize(self.output,side*side*16);gl.bind(self.output,2)
        params={'uSeed':('1ui',int(self.settings['seed'])),'uPointSide':('1ui',side),
            'uSiteSpacing':('1f',spacing),'uSiteSalt':('1ui',salt),'uSiteOccupancy':('1f',occupancy),
            'uCellOrigin':('2f',*map(float,low))}
        for name,args in params.items():gl.uniform(self.sites,name,*args)
        gl.dispatch_extent(self.sites,side,side)
        data=np.frombuffer(gl.read(self.output,side*side*16),dtype='<f4').copy().reshape(-1,4)
        data=data[(data[:,2]>0)&(np.linalg.norm(data[:,:2]-center,axis=1)<=radius)]
        return data,spacing,occupancy


def connected_metrics(mask,spacing):
    labels,count=ndimage.label(mask,structure=ndimage.generate_binary_structure(2,1))
    sizes=np.bincount(labels.ravel())[1:]
    if not len(sizes):return {'count':0,'largest_area_km2':0.,'largest_bounds_km':[0.,0.]}
    largest=int(np.argmax(sizes))+1
    zz,xx=np.nonzero(labels==largest)
    return {'count':int(count),'largest_area_km2':float(sizes.max()*spacing**2/1e6),
        'largest_bounds_km':[float((xx.max()-xx.min()+1)*spacing/1000),float((zz.max()-zz.min()+1)*spacing/1000)],
        'median_area_km2':float(np.median(sizes)*spacing**2/1e6)}


def render_map(path,dominant,inside,low,spacing,center,radius,strong_points,seed):
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    from matplotlib.colors import ListedColormap,BoundaryNorm
    from matplotlib.patches import Patch,Circle
    colors=list(plt.get_cmap('tab20').colors)+list(plt.get_cmap('Set2').colors[:5])+[(.13,.28,.43)]
    cmap=ListedColormap(colors);cmap.set_bad('#f2f2f2')
    figure,ax=plt.subplots(figsize=(12,11),layout='constrained')
    side=len(dominant)
    extent=((low[0]-.5)*spacing/1000,(low[0]+side-.5)*spacing/1000,
            (low[1]-.5)*spacing/1000,(low[1]+side-.5)*spacing/1000)
    ax.imshow(np.ma.masked_where(~inside,dominant),origin='lower',extent=extent,
        cmap=cmap,norm=BoundaryNorm(np.arange(27)-.5,26),interpolation='nearest')
    for points in strong_points:
        if len(points):ax.scatter(points[:,0]/1000,points[:,1]/1000,s=9,c='#111111',linewidths=.25,edgecolors='white')
    ax.add_patch(Circle(np.asarray(center)/1000,radius/1000,fill=False,edgecolor='#222222',lw=.8))
    ax.plot(center[0]/1000,center[1]/1000,'+',color='white',markersize=9,markeredgewidth=1.5)
    ax.set(xlabel='X monde (km)',ylabel='Z monde (km)',aspect='equal')
    ax.set_title(f"ENDURANCE — biomes dominants du climat GLSL réel\nSeed {seed} · rayon {radius/1000:.3f} km · grille {spacing:.0f} m · points : sites de poids ≥ 0,80",fontsize=13)
    handles=[Patch(facecolor=colors[b],label=f'{b:02d} {name}') for b,name in enumerate(BIOME_NAMES)]
    ax.legend(handles=handles,loc='upper center',bbox_to_anchor=(.5,-.08),ncol=4,fontsize=8,frameon=False)
    figure.savefig(path,dpi=170);plt.close(figure)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--scene',type=Path,default=ROOT/'ENDURANCE/Scene/Terrain.scene')
    parser.add_argument('--spacing',type=float,default=100.)
    parser.add_argument('--output',type=Path,default=ROOT/'docs/biome-validation/world_scale.json')
    args=parser.parse_args();settings,center=scene_settings(args.scene)
    assert args.spacing>0
    radius=float(settings['viewDistance']);spacing=args.spacing
    low=np.floor((np.asarray(center)-radius)/spacing).astype(int)
    high=np.ceil((np.asarray(center)+radius)/spacing).astype(int)
    side=int((high-low).max()+1)
    z,x=np.mgrid[:side,:side];wx=(x+low[0])*spacing;wz=(z+low[1])*spacing
    inside=(wx-center[0])**2+(wz-center[1])**2<=radius**2
    try:
        with GL() as gl:
            print(f'OpenGL {gl.version}; {gl.renderer}',flush=True)
            probe=WorldProbe(gl,settings)
            values=probe.classify_grid(side,spacing,low).reshape(side,side,28)
            weights=values[:,:,:26];dominant=np.argmax(weights,axis=2)
            # Verify injected coordinates against the unchanged production
            # classifier, including climate, macro height and all weights.
            checks=[(side//4,side//3),(side//2,side//2),(3*side//4,2*side//3)]
            coordinates=np.asarray([[(ix+low[0])*spacing,(iz+low[1])*spacing] for iz,ix in checks])
            point_values=probe.classify_grid(4,1.,(0,0),points=coordinates)[:len(checks)]
            assert np.array_equal(point_values,np.asarray([values[iz,ix] for iz,ix in checks])), 'Anchor coordinate adapter changed classifier output'
            print('PASS coordinate injection: three lattice-aligned points identical to production grid',flush=True)
            records=[];strong_points=[]
            for biome,name in enumerate(BIOME_NAMES):
                selected=inside&(dominant==biome)
                record={'id':biome,'name':name,'dominant_area_km2':float(selected.sum()*spacing**2/1e6),
                    'weighted_area_km2':float(weights[:,:,biome][inside].sum(dtype=np.float64)*spacing**2/1e6),
                    'strong_weight_0_8_area_km2':float((inside&(weights[:,:,biome]>=.8)).sum()*spacing**2/1e6),
                    'dominant_components':connected_metrics(selected,spacing)}
                if biome<25:
                    anchors,site_spacing,occupancy=probe.anchors(biome,center,radius)
                    count=len(anchors);pside=max(4,math.ceil(math.sqrt(max(1,count))))
                    aw=probe.classify_grid(pside,1.,(0,0),points=anchors[:,:2])[:count,:26]
                    own=aw[:,biome];dom=np.argmax(aw,axis=1)
                    strict=(dom==biome)&(own>=.8)
                    strong_points.append(anchors[strict,:2])
                    record['sites']={'placement_cell_m':site_spacing,'occupancy':occupancy,'active_anchors_in_disk':count,
                        'anchors_in_own_dominant_biome':int((dom==biome).sum()),
                        'anchors_with_own_weight_at_least_0_8':int(strict.sum()),
                        'anchors_with_own_weight_at_least_0_2':int((own>=.2).sum()),
                        'strong_anchor_variants':[float(v) for v in anchors[strict,3]],
                        'median_radius_m':float(np.median(anchors[:,2])) if count else 0.,
                        'strong_site_examples':[{'x':float(row[0]),'z':float(row[1]),'radius_m':float(row[2]),
                            'variant':float(row[3]),'biome_weight':float(weight)} for row,weight in zip(anchors[strict][:3],own[strict][:3])]}
                records.append(record)
                print(f"{biome:02d} {name}: dominant {record['dominant_area_km2']:.2f} km²; strong {record['strong_weight_0_8_area_km2']:.2f} km²"+
                    (f"; own sites {record['sites']['anchors_in_own_dominant_biome']}/{record['sites']['anchors_with_own_weight_at_least_0_8']} strong" if biome<25 else ''),flush=True)
            result={'renderer':gl.renderer,'scene':str(args.scene.relative_to(ROOT)),'seed':settings['seed'],
                'source_sha256':{str(path.relative_to(ROOT)):hashlib.sha256(path.read_bytes()).hexdigest() for path in [SHADERS/'field_batch_classify.C.glsl',SHADERS.parent/'Noise/climate_2d.glsl',SHADERS/'biome_landforms_common.glsl',ROOT/'ANTAGONIZ/Header/Miscellaneous/ProceduralTerrainBiomes.hpp']},
                'coordinate_adapter_exact_samples':len(checks),'scene_settings':settings,'center_xz_m':center,'radius_m':radius,'grid_spacing_m':spacing,'grid_side':side,
                'sampled_disk_area_km2':float(inside.sum()*spacing**2/1e6),'analytic_disk_area_km2':math.pi*radius**2/1e6,
                'notes':['Plan-view area estimates on a regular grid; not triangulated ground surface area.',
                    'Four-neighbor connectivity; sub-grid corridors, islands and contacts may be missed.',
                    'Site anchors are production landSite outputs at the scene seed, weighted by the actual unforced classifier.',
                    'An active anchor is not proof of a passable cave or intact visible sculpture after blending.',
                    'World generation is unbounded; disk is the saved camera selection radius, not the total planet.',
                    'Scene climateFrequency is legacy; climate_2d.glsl defines production climate frequencies.'],
                'records':records}
            args.output.parent.mkdir(parents=True,exist_ok=True)
            args.output.write_text(json.dumps(result,indent=2,ensure_ascii=False)+'\n')
            render_map(args.output.with_suffix('.png'),dominant,inside,low,spacing,center,radius,strong_points,settings['seed'])
            print('WROTE',args.output,flush=True)
    except GLUnavailable as error:print(f'SKIP: {error}',file=sys.stderr);return 77
    return 0

if __name__=='__main__':raise SystemExit(main())
