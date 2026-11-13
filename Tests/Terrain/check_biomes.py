#!/usr/bin/env python3
"""Exercise every production biome recipe and optionally render diagnostic atlases.

Each recipe probe compiles only the selected biome's production 2D/3D functions.
Matrix, coastline and LOD checks execute the actual accumulation pipeline.
Readbacks here are test instrumentation and never enter the terrain runtime path.
"""
from __future__ import annotations

import argparse
import json
import math
import re
from pathlib import Path
import statistics
import struct
import sys

from biome_test_support import BIOME_NAMES, biome_recipe_source, packed_biome_cells
from check_gpu import FieldRunner, COLUMN_WORDS, SHADERS, lattice_values, finite_columns
from field_batch_runner import default_uniforms, MAX_JOBS
from gl_headless import GL, GLUnavailable, shader_source

BIOMES = (
    "Glace polaire", "Toundra gelée", "Toundra", "Taïga", "Forêt boréale marécageuse",
    "Toundra rocheuse", "Steppe froide", "Forêt boréale", "Forêt froide", "Marécage froid",
    "Plaine sèche", "Prairie", "Forêt tempérée", "Forêt tempérée humide", "Marais tempéré",
    "Désert de roche", "Savane sèche", "Savane", "Jungle sèche", "Jungle tropicale",
    "Désert volcanique", "Désert rouge", "Plateau aride", "Jungle brûlante", "Mangrove tropicale",
    "Fond marin",
)


def profile_probe_source():
    return shader_source(Path(__file__).with_name("biome_profile_probe.glsl"))

PROBE = r"""
layout(local_size_x=8,local_size_y=8) in;
struct ProbeSample { vec4 terrain; vec4 volume; vec4 samples; vec4 shape; };
layout(std430,binding=6) writeonly buffer ProbeOutput { ProbeSample outputValues[]; };
uniform uint uProbeSize;
uniform uint uProbeMode;
uniform uint uProbeSample;
uniform vec2 uProbeOrigin;
uniform float uProbeSpacing;
uniform float uProbeMinY;
uniform float uProbeStepY;
void main()
{
    uvec2 index=gl_GlobalInvocationID.xy;
    if(any(greaterThanEqual(index,uvec2(uProbeSize)))) return;
    vec2 p=uProbeOrigin+vec2(index)*uProbeSpacing;
    if(uProbeMode==1u) p.y=uProbeOrigin.y;
    uint address=index.x+index.y*uProbeSize;
    FieldColumn column=fieldProbeColumn(p);
    vec4 volume=fieldProbeVolume(column);
    float y=uProbeMinY+float(index.y)*uProbeStepY;
    if(uProbeMode==0u)
    {
        const float offsets[4]=float[4](-.65,-.2,.2,.65);
        y=column.terrain.x+volume.x+max(column.terrain.w*0.5,0.001)*offsets[uProbeSample];
    }
    float value=fieldProbeDensity(vec3(p.x,y,p.y),column);
    if(uProbeMode==0u)
    {
        if(uProbeSample==0u)
            outputValues[address]=ProbeSample(column.terrain,volume,vec4(0),column.blend);
        outputValues[address].samples[uProbeSample]=value;
    }
    else
        outputValues[address]=ProbeSample(vec4(column.terrain.x,y,volume.x,column.terrain.w),
            volume,vec4(value),column.blend);
}
"""


class BiomeProbe:
    """Point sampler for one production recipe pair, never a combined switch."""
    def __init__(self, gl, directory=SHADERS, source=None):
        self.gl=gl
        self.directory=Path(directory)
        self.main_source=source or PROBE
        self.density_passes=source is None
        self.programs={}
        self.mapping=gl.buffer(packed_biome_cells())
        self.buffer=gl.buffer(0)
        self.capacity=0

    def program_for(self, biome):
        assert 0<=biome<len(BIOME_NAMES), "Recipe probes require one explicit biome"
        if biome not in self.programs:
            directory=self.directory/"biomes"/BIOME_NAMES[biome]
            relief=biome_recipe_source(directory/"relief.C.glsl")
            volume=biome_recipe_source(directory/"volume.C.glsl")
            volume=re.sub(r"^#version[^\n]*\n", "", volume, count=1)
            self.programs[biome]=self.gl.compile([(0x91B9,relief+volume+
                profile_probe_source()+self.main_source)], BIOME_NAMES[biome]+" recipe point probe")
        return self.programs[biome]

    def run(self, biome=0, size=17, origin=(-347.25,-591.75), spacing=39.5, mode=0,
            min_y=-800., step_y=10., **overrides):
        assert mode in (0,1), "Mixed fields are tested through the production batch runner"
        gl=self.gl
        self.program=self.program_for(biome)
        gl.bind(self.mapping,7)
        defaults={
            "uCells":("1ui",64),"uOrigin":("3i",0,0,0),"uLod":("1ui",0),
            "uBaseSpacing":("1f",1.),"uSeed":("1ui",1337),"uFieldMode":("1ui",0),
            "uBaseHeight":("1f",0.),"uHeightAmplitude":("1f",320.),
            "uHeightFrequency":("1f",.00015),
            "uBandOffset":("1f",0.),"uBandOffsetVariation":("1f",0.),
            "uDetailScale":("1f",.35),"uSeaLevel":("1f",0.),"uSeaBlendWidth":("1f",64.),
            "uBiomeOverride":("1i",biome),"uSphereCenter":("3f",0.,0.,0.),"uSphereRadius":("1f",48.),
            "uProbeSize":("1ui",size),"uProbeMode":("1ui",mode),"uProbeOrigin":("2f",*origin),
            "uProbeSpacing":("1f",spacing),"uProbeMinY":("1f",min_y),"uProbeStepY":("1f",step_y),
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
        for sample in range(4 if mode==0 and self.density_passes else 1):
            gl.uniform(self.program,"uProbeSample","1ui",sample)
            gl.dispatch_extent(self.program,size,size)
        data=gl.read(self.buffer,byte_count)
        gl.check("biome recipe probe")
        values=struct.unpack(f"<{size*size*16}f",data)
        assert all(math.isfinite(v) for v in values),f"Biome {biome} contains a nonfinite or unwritten sample"
        return {"raw":data,"values":values,"size":size,"spacing":spacing,"origin":origin,"mode":mode}


def check_recipes(gl):
    probe=BiomeProbe(gl)
    heights=set()
    records=[]
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
            lower,upper=data[i*16+5:i*16+7]
            assert lower<=upper and lower<=height+offset<=upper,("Invalid volume envelope",biome,lower,upper,height,offset)
            assert min(height,lower)>=-700.802,("Biome escaped conservative lower bound",biome,height,band,offset,lower)
            assert max(height,upper)<=700.802,("Biome escaped conservative upper bound",biome,height,band,offset,upper)
        heights.add(tuple(round(v,4) for v in h))
        carved=0
        for i in range(sample["size"]**2):
            band,offset=data[i*16+3],data[i*16+4]
            for j,relative in enumerate((-.65,-.2,.2,.65)):
                original_solid=-offset-relative*.5*band
                if original_solid>1.0 and data[i*16+8+j]<-.01:carved+=1
        records.append({"id":biome,"name":name,"minimum_height":min(h),"maximum_height":max(h),
                        "height_span":height_span,"average_band":statistics.fmean(data[3::16]),"carved_interior_samples":carved})
    # Rare landforms deliberately need not occur in this fixed central tile.
    # check_biome_landforms targets actual GPU-generated sites and checks signed
    # additions/removals there, rather than requiring caves in all quiet plains.
    assert len(heights)==len(BIOMES),f"Only {len(heights)} distinct height recipes for {len(BIOMES)} biomes"
    a=probe.run(biome=19)
    b=probe.run(biome=19)
    assert a["raw"]==b["raw"],"Forced biome is not deterministic"
    shifted=probe.run(biome=19,uBandOffset=-96.)
    assert a["values"][0::16]==shifted["values"][0::16],"Band offset changed the initial height surface"
    assert all(abs((y-x)+96.)<2e-5 for x,y in zip(a["values"][4::16],shifted["values"][4::16])),"Band offset was not applied independently"
    print(f"PASS all biomes: {len(BIOMES)} distinct finite reliefs and bounded volume controls; forced seed and independent volume offset")
    return probe,records


def numeric_field(result, include_climate=True):
    # The local altitude fraction resets from 1 to 0 at a cell boundary. Its
    # resulting contributions must be continuous, not that internal coordinate.
    channels=(0,1,2,3,5,6,7) if include_climate else (0,3,5,6,7)
    return tuple(value for i,value in enumerate(result["columns"]) if i%COLUMN_WORDS in channels)+result["density"]


def check_matrix_continuity(probe):
    # Feed exact climate coordinates only at classification. All mapping,
    # weights, individual relief/volume passes and indirect jobs are production
    # code, so this checks the accumulated field rather than mixed parameters.
    gl=probe.gl
    fields=FieldRunner(gl)
    source=shader_source(SHADERS/"field_batch_classify.C.glsl")
    source,count=re.subn(r"vec2 climate=noiseClimate2D\(world,uSeed\);", "vec2 climate=uProbeClimate.xy;", source)
    assert count==1, "Production climate classification changed; update the point injection"
    needle="clamp(coast,0.0,4.0)"
    assert source.count(needle)==1
    source=source.replace(needle,"clamp(uProbeClimate.z,0.0,1.0)*4.0")
    source=source.replace("layout(local_size_x=", "uniform vec3 uProbeClimate;\nlayout(local_size_x=",1)
    original=fields.classify
    fields.classify=gl.compile([(0x91B9,source)],"production classifier with exact probe climate")
    fields.programs=[fields.classify if program==original else program for program in fields.programs]

    def sample(climate,biome=-1,height=-8):
        gl.uniform(fields.classify,"uProbeClimate","3f",*climate)
        return fields.run(cells=4,origin=(-20,height,-24),seed=1337,
                          uHeightAmplitude=0.,uBiomeOverride=biome)

    for biome in range(25):
        climate=((biome%5)*.25,(biome//5)*.25,.5)
        automatic=sample(climate)
        forced=sample(climate,biome)
        assert numeric_field(automatic)==numeric_field(forced),("Matrix node did not select its biome",biome)
    largest=0.
    for axis in range(3):
        for boundary in (.25,.5,.75):
            left=[.37,.63,.59];right=left.copy()
            left[axis]=boundary-1e-6;right[axis]=boundary+1e-6
            for height in (-48,0,48):
                a=numeric_field(sample(left,height=height))
                b=numeric_field(sample(right,height=height))
                difference=max(abs(x-y) for x,y in zip(a,b))
                largest=max(largest,difference)
                assert difference<.05,("Accumulated matrix boundary jumps",axis,boundary,height,difference)
    for humidity,temperature in ((0.,0.),(1.,0.),(0.,1.),(1.,1.),(.37,.63)):
        climate=(humidity,temperature,0.)
        automatic=sample(climate)
        marine=sample(climate,25)
        assert numeric_field(automatic)==numeric_field(marine),"Lowest altitude slice differs from the marine biome"
    print(f"PASS production accumulation matrices: 25 exact nodes, 2D/3D boundary continuity and full marine slice; largest epsilon jump {largest:.6g}")


def check_sea_transition(probe):
    fields=FieldRunner(probe.gl)

    def sample(**overrides):
        return numeric_field(fields.run(cells=4,origin=(-20,-8,-24),seed=1337,uHeightAmplitude=0.,**overrides))

    for boundary in (-32.,0.,32.):
        a=sample(uSeaLevel=boundary-.0001)
        b=sample(uSeaLevel=boundary+.0001)
        jump=max(abs(x-y) for x,y in zip(a,b))
        assert jump<.02,("Marine transition is discontinuous",boundary,jump)
    automatic=sample(uSeaLevel=128.)
    marine=sample(uSeaLevel=128.,uBiomeOverride=25)
    assert max(abs(x-y) for x,y in zip(automatic,marine))<.0001,"Submerged automatic field differs from forced seafloor"
    print("PASS production sea accumulation: continuous shoreline and fully marine field below its threshold")


def check_climate_coverage(probe):
    # Only the production classification pass is required to measure climate.
    # Sampling 129² columns avoids allocating or evaluating a 129³ density field.
    gl=probe.gl
    fields=FieldRunner(gl)
    size=129
    columns=gl.buffer(size*size*48,1)
    gl.bind(fields.mapping,7)
    gl.bind(fields.jobs,6)
    gl.bind(fields.queues,5)
    uniforms=default_uniforms(size-3,uBaseSpacing=250.,uDetailScale=0.)
    uniforms.update({"uJobSlots":("1ui",1),"uColumnStride":("1ui",size*size),
                     "uDensityStride":("1ui",size**3)})
    for name,args in uniforms.items():gl.uniform(fields.classify,name,*args)
    reached=set()
    counts={}
    for seed in (1337,1,17,98765):
        descriptors=struct.pack("<4i8I",-63,0,-63,0,seed,3,0,0,0,0,0,0)+bytes((MAX_JOBS-1)*48)
        gl.write(fields.jobs,descriptors)
        gl.dispatch(fields.classify,(size+7)//8,(size+7)//8,1)
        values=struct.unpack(f"<{size*size*COLUMN_WORDS}f",gl.read(columns,size*size*48))
        local=set()
        for humidity,temperature in zip(values[1::COLUMN_WORDS],values[2::COLUMN_WORDS]):
            assert 0<=humidity<=1 and 0<=temperature<=1
            biome=int(math.floor(humidity*4+.5))+5*int(math.floor(temperature*4+.5))
            local.add(biome)
            reached.add(biome)
        counts[seed]=len(local)
    assert reached==set(range(25)),("Climate remap cannot expose all matrix cells in sampled worlds",sorted(reached))
    gl.check("production climate coverage")
    print(f"PASS production climate reachability: all 25 dominant cells observed across ±16km; per-seed coverage {counts}")


def check_forced_lod(gl, probe):
    fields=FieldRunner(gl)
    for biome in range(len(BIOMES)):
        options={"uBiomeOverride":biome,"seed":123456789}
        fine=fields.run(cells=8,origin=(-8,-8,-8),**options)
        coarse=fields.run(cells=8,origin=(-16,-16,-16),lod=1,**options)
        a,b=lattice_values(fine),lattice_values(coarse)
        shared=a.keys()&b.keys()
        assert len(shared)>=125
        assert all(a[p]==b[p] for p in shared),("Forced biome samples changed with LOD",biome)
        assert finite_columns(fine["columns"]) and all(math.isfinite(v) for v in fine["density"]),("Unwritten forced biome",biome)
        # Keep the isolated point sampler honest against the actual production
        # columns and a complete X/Y density slice, including Marine's climate.
        columns=probe.run(biome=biome,size=fine["side"],origin=(-9.,-9.),spacing=1.,uSeed=123456789)["values"]
        assert all(math.isclose(columns[i*16+j],fine["columns"][i*COLUMN_WORDS+j],rel_tol=2e-6,abs_tol=2e-5)
                   for i in range(fine["side"]**2) for j in range(4)),("Recipe probe column differs from accumulation",biome)
        section=probe.run(biome=biome,size=fine["side"],mode=1,origin=(-9.,-9.),spacing=1.,
                          min_y=-9.,step_y=1.,uSeed=123456789)["values"][8::16]
        assert all(math.isclose(a,b,rel_tol=2e-6,abs_tol=2e-5)
                   for a,b in zip(section,fine["density"])),("Recipe probe density differs from accumulation",biome)
    print(f"PASS forced biome LOD: all {len(BIOMES)} recipes agree bitwise on shared samples and complete every halo")
    print(f"PASS isolated recipe probes: all {len(BIOMES)} match production column controls and X/Y density slices")


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
            check_forced_lod(gl,probe)
            from biome_mask_probe import check_biome_masks, check_volume_noise_budget
            check_biome_masks(gl)
            check_volume_noise_budget(gl)
            if args.atlas:write_atlases(probe,args.atlas,records)
    except GLUnavailable as error:
        print(f"SKIP: {error}",file=sys.stderr)
        return 77
    return 0

if __name__=="__main__":raise SystemExit(main())
