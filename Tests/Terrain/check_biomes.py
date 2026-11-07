#!/usr/bin/env python3
"""Exercise every production biome recipe and optionally render diagnostic atlases.

All scalar data come from the actual GLSL implementation. Readbacks here are test
instrumentation and are never part of the terrain runtime path.
"""
from __future__ import annotations

import argparse
import ctypes as C
import json
import math
from pathlib import Path
import statistics
import struct
import sys

from check_gpu import FieldRunner, COLUMN_WORDS, SHADERS, lattice_values
from gl_headless import GL, GLUnavailable, shader_source

BIOMES = (
    "Glace polaire", "Toundra gelée", "Toundra", "Taïga", "Forêt boréale marécageuse",
    "Toundra rocheuse", "Steppe froide", "Forêt boréale", "Forêt froide", "Marécage froid",
    "Plaine sèche", "Prairie", "Forêt tempérée", "Forêt tempérée humide", "Marais tempéré",
    "Désert de roche", "Savane sèche", "Savane", "Jungle sèche", "Jungle tropicale",
    "Désert volcanique", "Désert rouge", "Plateau aride", "Jungle brûlante", "Mangrove tropicale",
    "Fond marin",
)

PROBE = r"""
layout(local_size_x=8,local_size_y=8) in;
struct ProbeSample { vec4 terrain; vec4 volume; vec4 samples; vec4 shape; };
layout(std430,binding=6) writeonly buffer ProbeOutput { ProbeSample outputValues[]; };
uniform uint uProbeSize;
uniform uint uProbeMode;
uniform vec2 uProbeOrigin;
uniform float uProbeSpacing;
uniform float uProbeMinY;
uniform float uProbeStepY;
uniform vec3 uProbeClimate;
void main()
{
    uvec2 index=gl_GlobalInvocationID.xy;
    if(any(greaterThanEqual(index,uvec2(uProbeSize)))) return;
    vec2 p=uProbeOrigin+vec2(index)*uProbeSpacing;
    uint address=index.x+index.y*uProbeSize;
    FieldColumn column;
    if(uProbeMode==0u)
    {
        column=fieldColumnAt(p);
        float center=column.terrain.x+column.volume.x;
        float halfBand=max(column.terrain.w*0.5,0.001);
        float a=fieldDensityAt(vec3(p.x,center-halfBand*.65,p.y),column);
        float b=fieldDensityAt(vec3(p.x,center-halfBand*.2,p.y),column);
        float c=fieldDensityAt(vec3(p.x,center+halfBand*.2,p.y),column);
        float d=fieldDensityAt(vec3(p.x,center+halfBand*.65,p.y),column);
        outputValues[address]=ProbeSample(column.terrain,column.volume,vec4(a,b,c,d),column.shape);
    }
    else if(uProbeMode==1u)
    {
        p=vec2(p.x,uProbeOrigin.y);
        column=fieldColumnAt(p);
        float y=uProbeMinY+float(index.y)*uProbeStepY;
        float value=fieldDensityAt(vec3(p.x,y,p.y),column);
        outputValues[address]=ProbeSample(vec4(column.terrain.x,y,column.volume.x,column.terrain.w),
            column.volume,vec4(value),column.shape);
    }
    else if(uProbeMode==2u)
    {
        vec4 relief=fieldBlendRelief(p,uProbeClimate.xy);
        BiomeVolume profile=fieldBlendVolume(uProbeClimate,p);
        outputValues[address]=ProbeSample(relief,profile.volume,profile.shape,profile.style);
    }
    else if(uProbeMode==3u)
    {
        uint biome=uint(round(uProbeClimate.x*4.0))+5u*uint(round(uProbeClimate.y*4.0));
        vec4 expected=fieldBiomeRelief(biome,p);
        BiomeVolume direct=fieldBiomeVolumeAt(biome,p);
        BiomeVolume mixed=fieldBlendVolume(uProbeClimate,p);
        outputValues[address]=ProbeSample(fieldBlendRelief(p,uProbeClimate.xy)-expected,
            mixed.volume-direct.volume,mixed.shape-direct.shape,mixed.style-direct.style);
    }
    else
    {
        outputValues[address]=ProbeSample(vec4(fieldClimateAt(p),0.0,0.0),vec4(0.0),vec4(0.0),vec4(0.0));
    }
}
"""


class BiomeProbe:
    def __init__(self, gl):
        self.gl=gl
        self.program=gl.compile([(0x91B9,"#version 450 core\n"+shader_source(SHADERS/"world_field.glsl")+PROBE)],"biome production probe")
        self.buffer=gl.buffer(0)
        self.capacity=0

    def run(self, biome=-1, size=17, origin=(-347.25,-591.75), spacing=39.5, mode=0,
            min_y=-800., step_y=10., climate=(.5,.5,.5), **overrides):
        gl=self.gl
        defaults={
            "uCells":("1ui",64),"uOrigin":("3i",0,0,0),"uLod":("1ui",0),
            "uBaseSpacing":("1f",1.),"uSeed":("1ui",1337),"uFieldMode":("1ui",0),
            "uBaseHeight":("1f",0.),"uHeightAmplitude":("1f",320.),
            "uHeightFrequency":("1f",.00015),"uClimateFrequency":("1f",.0005),
            "uBandThickness":("1f",64.),"uBandVariation":("1f",16.),
            "uBandOffset":("1f",0.),"uBandOffsetVariation":("1f",0.),
            "uDetailScale":("1f",.35),"uSeaLevel":("1f",0.),"uSeaBlendWidth":("1f",64.),
            "uBiomeOverride":("1i",biome),"uSphereCenter":("3f",0.,0.,0.),"uSphereRadius":("1f",48.),
            "uProbeSize":("1ui",size),"uProbeMode":("1ui",mode),"uProbeOrigin":("2f",*origin),
            "uProbeSpacing":("1f",spacing),"uProbeMinY":("1f",min_y),"uProbeStepY":("1f",step_y),
            "uProbeClimate":("3f",*climate),
        }
        for name,value in overrides.items():
            kind=defaults[name][0]
            defaults[name]=(kind,*(value if isinstance(value,tuple) else (value,)))
        for name,args in defaults.items():gl.uniform(self.program,name,*args)
        byte_count=size*size*64
        if byte_count>self.capacity:
            gl.glBindBuffer(gl.SHADER_STORAGE_BUFFER,self.buffer)
            gl.glBufferData(gl.SHADER_STORAGE_BUFFER,byte_count,None,0x88EA)
            self.capacity=byte_count
        gl.bind(self.buffer,6)
        nan=struct.pack("<I",0x7fc00001)
        gl.write(self.buffer,nan*(byte_count//4))
        gl.dispatch_extent(self.program,size,size)
        data=gl.read(self.buffer,byte_count)
        gl.check("biome probe")
        values=struct.unpack(f"<{size*size*16}f",data)
        assert all(math.isfinite(v) for v in values),f"Biome {biome} contains a nonfinite or unwritten sample"
        return {"raw":data,"values":values,"size":size,"spacing":spacing,"origin":origin,"mode":mode}


def check_recipes(gl):
    probe=BiomeProbe(gl)
    heights=set()
    profiles=set()
    records=[]
    carved_biomes=[]
    for biome,name in enumerate(BIOMES):
        sample=probe.run(biome=biome)
        data=sample["values"]
        h=data[0::16]
        # A recipe is exercised over hundreds of positions, not just the origin
        # where lattice-gradient noise is often identically zero.
        height_span=max(h)-min(h)
        assert height_span>0.01,("Biome height recipe is flat",biome,name,height_span)
        assert all(value>=0 for value in data[3::16]),("Negative volume band",biome)
        for i in range(sample["size"]**2):
            height,band,offset=data[i*16],data[i*16+3],data[i*16+4]
            assert min(height,height+offset-band*.5)>=-696.0001,("Biome escaped conservative lower bound",biome,height,band,offset)
            assert max(height,height+offset+band*.5)<=696.0001,("Biome escaped conservative upper bound",biome,height,band,offset)
        heights.add(tuple(round(v,4) for v in h))
        profiles.add(tuple(round(v,5) for n in range(4,16) for v in data[n::16]))
        carved=0
        for i in range(sample["size"]**2):
            band,offset=data[i*16+3],data[i*16+4]
            for j,relative in enumerate((-.65,-.2,.2,.65)):
                original_solid=-offset-relative*.5*band
                if original_solid>1.0 and data[i*16+8+j]<-.01:carved+=1
        if carved:carved_biomes.append(biome)
        records.append({"id":biome,"name":name,"minimum_height":min(h),"maximum_height":max(h),
                        "height_span":height_span,"average_band":statistics.fmean(data[3::16]),"carved_interior_samples":carved})
    assert len(carved_biomes)==len(BIOMES),("Some volume recipes never carve the solid in the diagnostic region",carved_biomes)
    assert len(heights)==len(BIOMES),f"Only {len(heights)} distinct height recipes for {len(BIOMES)} biomes"
    assert len(profiles)==len(BIOMES),f"Only {len(profiles)} distinct volume/profile signatures"
    a=probe.run(biome=19)
    b=probe.run(biome=19)
    assert a["raw"]==b["raw"],"Forced biome is not deterministic"
    shifted=probe.run(biome=19,uBandOffset=-96.)
    assert a["values"][0::16]==shifted["values"][0::16],"Band offset changed the initial height surface"
    assert all(abs((y-x)+96.)<2e-5 for x,y in zip(a["values"][4::16],shifted["values"][4::16])),"Band offset was not applied independently"
    print(f"PASS all biomes: {len(BIOMES)} distinct finite 2D/3D recipes with interior carving; forced seed and independent volume offset")
    return probe,records


def check_matrix_continuity(probe):
    # The diagnostic shader calls the real blend functions directly. It cannot
    # pass by accidentally testing a different/clamped climate-noise value.
    for biome in range(25):
        climate=((biome%5)*.25,(biome//5)*.25,.5)
        delta=probe.run(mode=3,size=3,climate=climate)["values"]
        assert max(abs(v) for v in delta)<1e-5,("Matrix cell does not select its paired 2D/3D biome",biome,max(abs(v) for v in delta))
    largest=0.
    for axis in range(3):
        for boundary in (.25,.5,.75):
            center=[.37,.63,.59]
            center[axis]=boundary
            a=center.copy();b=center.copy();a[axis]-=1e-6;b[axis]+=1e-6
            left=probe.run(mode=2,size=3,climate=tuple(a))["values"]
            right=probe.run(mode=2,size=3,climate=tuple(b))["values"]
            difference=max(abs(x-y) for x,y in zip(left,right))
            largest=max(largest,difference)
            # Metre-valued recipes can have a large derivative with respect to
            # normalized climate; a discontinuity would exceed this small jump.
            assert difference<.05,("Biome matrix boundary jumps",axis,boundary,difference)
    # The complete first altitude slice must be the marine profile regardless
    # of humidity and temperature. Relief remains the independent climate map.
    ocean=[]
    for h,t in ((0.,0.),(1.,0.),(0.,1.),(1.,1.),(.37,.63)):
        data=probe.run(mode=2,size=1,climate=(h,t,0.))["values"]
        ocean.append(data[4:16])
    assert all(ocean[0]==row for row in ocean[1:]),"Lowest volume slice is not exclusively marine"
    print(f"PASS matrices: all 25 climate cells select paired recipes; continuous 2D/3D boundaries; complete marine altitude slice; largest epsilon jump {largest:.6g}")




def check_sea_transition(probe):
    for boundary in (-32.,0.,32.):
        a=probe.run(size=5,uHeightAmplitude=0.,uSeaLevel=boundary-.0001)["values"]
        b=probe.run(size=5,uHeightAmplitude=0.,uSeaLevel=boundary+.0001)["values"]
        jump=max(abs(x-y) for x,y in zip(a,b))
        assert jump<.02,("Marine transition is discontinuous",boundary,jump)
    automatic=probe.run(size=5,uHeightAmplitude=0.,uSeaLevel=128.)["values"]
    marine=probe.run(biome=25,size=5,uHeightAmplitude=0.,uSeaLevel=128.)["values"]
    assert max(abs(x-y) for x,y in zip(automatic,marine))<.0001,"Below the marine threshold, auto field differs from forced seafloor"
    print("PASS sea transition: continuous shoreline and fully marine field below its threshold")


def check_climate_coverage(probe):
    reached=set()
    counts={}
    for seed in (1337,1,17,98765):
        sample=probe.run(mode=4,size=129,origin=(-16000.,-16000.),spacing=250.,uSeed=seed)
        pairs=zip(sample["values"][0::16],sample["values"][1::16])
        local=set()
        for humidity,temperature in pairs:
            assert 0<=humidity<=1 and 0<=temperature<=1
            biome=int(math.floor(humidity*4+.5))+5*int(math.floor(temperature*4+.5))
            local.add(biome)
            reached.add(biome)
        counts[seed]=len(local)
    assert reached==set(range(25)),("Climate remap cannot expose all matrix cells in sampled worlds",sorted(reached))
    print(f"PASS climate reachability: all 25 dominant cells observed across ±16km; per-seed coverage {counts}")


def check_forced_lod(gl):
    fields=FieldRunner(gl)
    for biome in range(len(BIOMES)):
        options={"uBiomeOverride":biome,"seed":123456789}
        fine=fields.run(cells=8,origin=(-8,-8,-8),**options)
        coarse=fields.run(cells=8,origin=(-16,-16,-16),lod=1,**options)
        a,b=lattice_values(fine),lattice_values(coarse)
        shared=a.keys()&b.keys()
        assert len(shared)>=125
        assert all(a[p]==b[p] for p in shared),("Forced biome samples changed with LOD",biome)
        assert all(math.isfinite(v) for v in fine["columns"]+fine["density"]),("Unwritten forced biome",biome)
    print(f"PASS forced biome LOD: all {len(BIOMES)} recipes agree bitwise on shared samples and complete every halo")


def write_atlases(probe, directory:Path, records):
    # Optional visualization dependencies are deliberately not needed to run
    # the correctness tests. These are measured height/density plots, not art.
    import numpy as np
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    from matplotlib.colors import LightSource
    directory.mkdir(parents=True,exist_ok=True)
    count=193; spacing=8.; extent=(count-1)*spacing
    hill=LightSource(azdeg=315,altdeg=40)
    figure,axes=plt.subplots(6,5,figsize=(16,18),constrained_layout=True)
    sections,section_axes=plt.subplots(6,5,figsize=(16,18),constrained_layout=True)
    for biome,name in enumerate(BIOMES):
        sample=probe.run(biome=biome,size=count,origin=(-.5*extent,-.5*extent),spacing=spacing)
        values=np.asarray(sample["values"]).reshape(count,count,16)
        h=values[:,:,0]
        ax=axes.flat[biome]
        # Each tile shows the actual metric extent. Local color normalization
        # makes low-amplitude wetland profiles visible; labels retain metres.
        rgb=hill.shade(h,cmap=plt.get_cmap("terrain"),vert_exag=1,dx=spacing,dy=spacing,blend_mode="soft")
        ax.imshow(rgb,origin="lower",extent=(0,extent,0,extent))
        ax.set_title(f"{biome:02d} {name}\nH={h.min():.0f}…{h.max():.0f} m",fontsize=9)
        ax.set_xticks((0,extent));ax.set_yticks((0,extent));ax.tick_params(labelsize=7)
        # Use the Z row containing this tile's highest sampled point. Z=0 is
        # often deliberately a clear corridor between cellular landmarks and
        # would hide the tall structures. This diagnostic selection is labeled;
        # the metrics below continue to use the complete, unbiased height tile.
        margin=max(80.,float(values[:,:,3].max()))
        low=float(h.min()-margin);high=float(h.max()+margin)
        peak_row=int(np.unravel_index(np.argmax(h),h.shape)[0])
        section_z=-.5*extent+peak_row*spacing
        records[biome]["section_z"]=section_z
        section=probe.run(biome=biome,size=count,mode=1,origin=(-.5*extent,section_z),spacing=spacing,
                          min_y=low,step_y=(high-low)/(count-1))
        density=np.asarray(section["values"]).reshape(count,count,16)[:,:,8]
        sx=section_axes.flat[biome]
        sx.imshow(density>0,origin="lower",extent=(-.5*extent,.5*extent,low,high),cmap="gray_r",interpolation="nearest",aspect="auto")
        sx.set_title(f"{biome:02d} {name} — Z={section_z:.0f} m",fontsize=9)
        sx.tick_params(labelsize=7)
        slope_x,slope_z=np.gradient(h,spacing,spacing)
        records[biome]["fraction_slope_below_30_degrees"]=float(np.mean(np.hypot(slope_x,slope_z)<math.tan(math.radians(30))))
    for i in range(len(BIOMES),30):axes.flat[i].axis("off");section_axes.flat[i].axis("off")
    figure.suptitle("ENDURANCE — hauteurs GPU, seed 1337, 1536 × 1536 m par biome\nÉclairage de diagnostic ; palettes normalisées par case, sans végétation ni textures",fontsize=13)
    sections.suptitle("ENDURANCE — coupes GPU passant par le point culminant de chaque case, seed 1337\nNoir : solide ; blanc : vide. Axes en mètres, échelle verticale adaptée par case.",fontsize=13)
    figure.savefig(directory/"biome_height_atlas.png",dpi=150)
    figure.savefig(directory/"biome_height_atlas.jpg",dpi=150,pil_kwargs={"quality":85,"optimize":True})
    sections.savefig(directory/"biome_density_sections.png",dpi=150)
    plt.close(figure);plt.close(sections)
    (directory/"biome_metrics.json").write_text(json.dumps(records,ensure_ascii=False,indent=2)+"\n")
    print(f"WROTE diagnostic height/density atlases and sampled metrics: {directory}")


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--atlas",type=Path,help="Optional output directory for measured height and density plots (requires numpy/matplotlib)")
    args=parser.parse_args()
    try:
        with GL() as gl:
            print(f"OpenGL {gl.version}; renderer: {gl.renderer}")
            probe,records=check_recipes(gl)
            check_matrix_continuity(probe)
            check_sea_transition(probe)
            check_climate_coverage(probe)
            check_forced_lod(gl)
            if args.atlas:write_atlases(probe,args.atlas,records)
    except GLUnavailable as error:
        print(f"SKIP: {error}",file=sys.stderr)
        return 77
    return 0

if __name__=="__main__":raise SystemExit(main())
