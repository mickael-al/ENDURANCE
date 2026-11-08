"""Check sparse 2D dispatch and the common 3D table against CPU corner sums.

Only relief/evaluator bodies are instrumented in memory. Production column
preparation, packed ID decoding, table and interpolation remain untouched.
"""
import math
import re
import struct

from check_gpu import SHADERS
from gl_headless import shader_source


RELIEF_NAMES = """
PolarIce FrozenTundra Tundra Taiga BorealWetland RockyTundra ColdSteppe
BorealForest ColdForest ColdMarsh DryPlain Prairie TemperateForest
HumidTemperateForest TemperateMarsh RockDesert DrySavanna Savanna DryJungle
TropicalJungle VolcanicDesert RedDesert AridPlateau BurningJungle
TropicalMangrove Marine
""".split()



def replace_body(source, name, body):
    match = re.search(r"\b" + re.escape(name) + r"\s*\([^)]*\)\s*\{", source)
    assert match, "Missing biome recipe: " + name
    start = match.end() - 1
    depth, end = 1, start + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[:start] + "{\n" + body + "\n}" + source[end:]


PROBE = r"""
layout(local_size_x=1) in;
layout(std430,binding=9) readonly buffer ProbeInputs {
    uvec4 probeSurfaceIDs;
    vec4 probeSurfaceRatios;
    vec4 probeBlend; // marine, local humidity ratio, temperature ratio, altitude
};
layout(std430,binding=10) buffer ProbeResults {
    vec4 probeSurface;
    vec4 probeVolume;
    vec4 probeShape;
    vec4 probeStyle;
    float probeDensity;
    uint probePreparedMask;
    uint probeSurfaceCalls[26];
    uint probeVolumeCalls;
};
void main()
{
    for(uint i=0u;i<26u;++i) surfaceCalls[i]=0u;
    volumeCalls=0u;
    FieldColumn raw;
    raw.terrain=vec4(10.0,0.5,0.5,0.0);
    raw.blend=vec4(probeBlend.yzw,0.0);
    raw.detail=uvec4(0u);
    FieldColumn column=fieldColumnFromClimate(vec2(0.0),raw);
    probePreparedMask=column.detail.w;

    for(uint i=0u;i<26u;++i) surfaceCalls[i]=0u;
    probeSurface=fieldSurfaceRecipes(vec2(0.0),probeSurfaceIDs,
        probeSurfaceRatios,probeBlend.x);
    column.terrain=vec4(10.0,0.5,0.5,256.0);
    column.blend=vec4(probeBlend.w,0.0,0.0,1.0);
    fieldVolumeParameters(column,probeBlend.yz,probeVolume,probeShape,probeStyle);
    probeDensity=fieldDensityAt(vec3(0.0,8.0,0.0),column);
    for(uint i=0u;i<26u;++i) probeSurfaceCalls[i]=surfaceCalls[i];
    probeVolumeCalls=volumeCalls;
}
"""


def volume_table(source):
    """Read constants as data; interpolation itself has an independent oracle."""
    start=source.index("FIELD_BIOME_VOLUME")
    declaration=source[start:source.index(";",start)]
    values=[tuple(float(v.strip()) for v in row.split(","))
            for row in re.findall(r"vec4\s*\(([^()]*)\)",declaration)]
    assert len(values)==26*3 and all(len(row)==4 for row in values)
    return [sum(values[biome*3:biome*3+3],()) for biome in range(26)]


def check_biome_masks(gl):
    source = shader_source(SHADERS / "world_field.glsl")
    for biome, name in enumerate(RELIEF_NAMES):
        source = replace_body(source, "fieldRelief" + name,
            f"++surfaceCalls[{biome}]; return vec4({biome}.0-13.0,"
            f"{biome}.0+2.0,{biome}.0*0.5,0.5);")
    table=volume_table(source)
    source=replace_body(source,"fieldDensityAtPrepared",
        "++volumeCalls; return terrain.x-p.y;")
    source = "#version 450 core\nuint surfaceCalls[26];\nuint volumeCalls;\n" + source + PROBE
    program = gl.compile([(0x91B9, source)], "sparse biome mask probe")
    inputs = gl.buffer(48, 9)
    output = gl.buffer(180, 10)
    mapping = gl.buffer(64 * 16, 7)
    gl.uniform(program, "uFieldMode", "1ui", 0)
    gl.uniform(program, "uDetailScale", "1f", 1.0)

    # Four surface corners plus sea; eight genuinely different volume corners.
    # The latter cannot be covered by the current repeated production layers.
    cases = [
        ((0, 4, 18, 24), (.125, .375, .125, .375), tuple(range(8)), (.25, .25, .75, .5), -1),
        ((0, 0, 25, 25), (.25,) * 4, (0, 0, 25, 25) * 2, (.25, .5, .5, .5), -1),
        ((1, 2, 3, 4), (1., 0., 0., 0.), tuple(range(1, 9)), (0., 0., 0., 0.), -1),
        ((0, 7, 13, 24), (.25,) * 4, (25,) * 8, (1., .17, .43, .73), -1),
        ((25,) * 4, (.25,) * 4, (25,) * 8, (0., .25, .75, .5), -1),
        ((7,) * 4, (.25,) * 4, (7,) * 8, (0., .25, .75, .5), -1),
        ((0, 1, 2, 3), (0.,) * 4, tuple(range(8)), (0., 1., 1., 1.), -1),
    ]
    cases += [((i,) * 4, (1., 0., 0., 0.), tuple(range(8)), (0., .25, .75, .5), i)
              for i in range(26)]
    for case, (surface_ids, ratios, volume_ids, blend, override) in enumerate(cases):
        mask = sum(1 << biome for biome in set(volume_ids))
        encoded_surface = sum(biome << (5 * corner) for corner, biome in enumerate(surface_ids))
        low = sum(biome << (5 * corner) for corner, biome in enumerate(volume_ids[:6]))
        high = sum(biome << (5 * corner) for corner, biome in enumerate(volume_ids[6:]))
        gl.write(mapping, struct.pack("<4I", encoded_surface, low, high, mask) * 64)
        gl.write(inputs, struct.pack("<4I8f", *surface_ids, *ratios, *blend))
        gl.uniform(program, "uBiomeOverride", "1i", override)
        gl.dispatch(program, 1)
        result = struct.unpack("<17f28I", gl.read(output, 180))
        assert result[17] == (mask if override < 0 else 1 << override), ("Prepared volume mask", case)

        # Independent CPU oracle: accumulate each corner, not the shader's mask.
        marine, fx, fy, altitude = blend
        surface_weights, volume_weights = [0.] * 26, [0.] * 26
        for biome, weight in zip(surface_ids, ratios):
            surface_weights[biome] += weight * (1. - marine)
        surface_weights[25] += marine
        for corner, biome in enumerate(volume_ids):
            volume_weights[biome] += ((fx if corner & 1 else 1. - fx)
                                     * (fy if corner & 2 else 1. - fy)
                                     * (altitude if corner & 4 else 1. - altitude))
        if override >= 0:
            volume_weights = [float(i == override) for i in range(26)]
        expected_surface = [sum(w * (i - 13., i + 2., i * .5, .5)[component]
                                for i, w in enumerate(surface_weights))
                            for component in range(4)]
        expected_parameters=[sum(weight*table[biome][component]
                                 for biome,weight in enumerate(volume_weights))
                             for component in range(12)]
        expected=expected_surface+expected_parameters+[2.]
        assert all(math.isclose(a,b,rel_tol=2e-6,abs_tol=3e-5)
                   for a,b in zip(result[:17],expected)), ("Local table blend",case,result[:17],expected)
        assert result[18:44] == tuple(int(w>0.) for w in surface_weights), ("Surface recipe call count",case)
        assert result[44] == 1, ("Common volume evaluator call count",case,result[44])
    gl.check("biome mask probe")
    print("PASS sparse surface masks/common volume table: 33 cases, five surface/eight volume contributors, duplicates, zero weights, sea and all forced IDs; 12 parameters match CPU corner sums and density is evaluated once")


def check_volume_noise_budget(gl):
    """Count real gradient-noise evaluations, including the cheap rejection paths."""
    source=shader_source(SHADERS/"field_volume.glsl")
    match=re.search(r"float\s+fieldNoise3\s*\([^)]*\)\s*\{",source)
    assert match
    source=source[:match.end()]+"\n++noiseCalls;\n"+source[match.end():]
    probe=r"""
layout(local_size_x=1) in;
layout(std430,binding=10) buffer NoiseProbeOutput { float densityValue; uint calls; };
uniform vec3 probePoint;
uniform vec2 probeTerrain;
uniform vec4 probeVolume;
void main()
{
    noiseCalls=0u;
    densityValue=fieldDensityAtPrepared(probePoint,probeTerrain,probeVolume,
        vec4(.35,.50,.15,.10),vec4(.85,.70,.15,.30));
    calls=noiseCalls;
}
"""
    program=gl.compile([(0x91B9,"#version 450 core\nuint noiseCalls;\n"+source+probe)],"common volume noise budget")
    output=gl.buffer(8,10)
    gl.uniform(program,"uSeed","1ui",1337)
    gl.uniform(program,"uBaseHeight","1f",10.)
    gl.uniform(program,"uSphereCenter","3f",0.,0.,0.)
    gl.uniform(program,"uSphereRadius","1f",20.)
    cases=[
        # mode, half-band, pointY, displacement, carving, expected noise calls
        (0,128.,-12.,3.,72.,3),
        (0,128.,-12.,3.,0.,3),
        (0,128.,-12.,0.,72.,3),
        (0,128.,-12.,0.,0.,0),
        (0,128.,1000.,3.,72.,0),
        (0,128.,128.,3.,72.,0),
        (0,0.,0.,3.,72.,0),
        (1,128.,-12.,3.,72.,0),
        (2,128.,-12.,3.,72.,0),
    ]
    for mode,half_band,y,displacement,carving,expected in cases:
        gl.uniform(program,"uFieldMode","1ui",mode)
        gl.uniform(program,"probePoint","3f",8.,y,20.)
        gl.uniform(program,"probeTerrain","2f",10.,half_band)
        gl.uniform(program,"probeVolume","4f",0.,displacement,carving,20.)
        gl.dispatch(program,1)
        density,count=struct.unpack("<fI",gl.read(output,8))
        assert count==expected,("Volume noise budget",mode,half_band,y,displacement,carving,count)
        assert math.isfinite(density)
        if expected==0:
            target=20.-math.sqrt(8.**2+y*y+20.**2) if mode==2 else 10.-y
            assert math.isclose(density,target,rel_tol=1e-6,abs_tol=1e-5),("Rejected volume changed density",density,target)
    gl.check("volume noise budget")
    print("PASS common volume noise budget: three gradient calls inside an active band; zero outside, for disabled volumes and analytical modes")
