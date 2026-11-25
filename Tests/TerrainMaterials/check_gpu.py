#!/usr/bin/env python3
"""Exercise the production terrain material fragment shader on actual OpenGL.

Controlled plane varyings isolate palette/mask invariants without duplicating
the material implementation. The actual packed terrain VS/FS pair is linked
as well. --preview renders that pair on a synthetic packed terrain mesh; its
contact sheet is a material comparison under fixed test lighting, not an
ENDURANCE gameplay screenshot. Blocking readbacks are test instrumentation.
"""
from __future__ import annotations

import argparse
import ctypes as C
import math
import os
from pathlib import Path
import random
import struct
import subprocess
import tempfile
import sys

os.environ.setdefault("MESA_GL_VERSION_OVERRIDE", "4.6")
os.environ.setdefault("MESA_GLSL_VERSION_OVERRIDE", "460")
HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
from check_cpu import export_table, export_appearance, export_stain
from check_grain_assets import export_grain
sys.path.insert(0, str(ROOT / "Tests/Atmosphere"))
from gl_support import AtmosphereGL, RGBA32F, RG32F, TEXTURE_2D, shader_source

TERRAIN = ROOT / "Asset/Shader/ProceduralTerrain"
NAMES = ("PolarIce", "FrozenTundra", "Tundra", "Taiga", "BorealBog",
         "RockyTundra", "ColdSteppe", "BorealForest", "ColdForest", "ColdMarsh",
         "DryPlain", "Meadow", "TemperateForest", "TemperateRainforest", "TemperateMarsh",
         "RockDesert", "DrySavanna", "Savanna", "DryJungle", "TropicalJungle",
         "VolcanicDesert", "RedDesert", "AridPlateau", "BurningJungle", "TropicalMangrove", "Marine")
UNIFORM_BUFFER = 0x8A11

PLANE_VERTEX = """#version 460 core
layout(location=0) out vec3 viewPosition;
layout(location=1) out vec3 worldPosition;
layout(location=2) out vec3 worldNormal;
layout(location=3) out vec2 climate;
layout(location=4) flat out uint chunkLod;
uniform vec3 testOrigin;
uniform vec3 testAxisX;
uniform vec3 testAxisY;
uniform vec3 testNormal;
uniform vec3 testViewOffset;
uniform vec2 testClimate;
uniform uint testLod;
void main() {
    vec2 uv=vec2((gl_VertexID<<1)&2,gl_VertexID&2);
    worldPosition=testOrigin+uv.x*testAxisX+uv.y*testAxisY;
    worldNormal=testNormal;
    viewPosition=vec3(uv*20.0-10.0,-50.0)+testViewOffset;
    climate=testClimate;
    chunkLod=testLod;
    gl_Position=vec4(uv*2.0-1.0,0.0,1.0);
}
"""


def literal_table(colors=None, wall_colors=None, *, metallic=0., roughness=.6, ao=1.):
    """An independent constant-material oracle, with all pattern effects zero."""
    if colors is None:
        colors = [(.08+.8*((i*7+3)%29)/29, .07+.8*((i*13+2)%31)/31,
                   .06+.8*((i*17+5)%37)/37) for i in range(26)]
    if wall_colors is None:
        wall_colors = colors
    floats = []
    for i in range(26):
        for rgb in (colors[i], wall_colors[i]):
            for _ in range(2):
                floats.extend((*rgb, roughness)*3)
                floats.extend((metallic, ao, 0., 0., .5, 1., 1., 0., 0., 0., 0., 0., 0., .16, 0., 0.))
            floats.extend((.5, .1, 1., 0., 24., 1.5, 0., 0., 0., .15, .35, .35))
        floats.extend((.28, .65, 0., 1., 1., .1, 1., .32, 0., 0., 0., 0.))
    assert len(floats) == 26*148
    return struct.pack(f'<{len(floats)}f', *floats), colors, wall_colors


def historical_sparkle_cache():
    """Only historical shader comparisons need the removed RGBA grain cache.

    Compile its exact old C++ bake, never approximate it in Python or depend on
    the current renderer retaining an obsolete production texture/export API.
    """
    revision = '1ff2aecf8cc243df85c44676987f8bb77a3e8a3c'
    name = 'ANTAGONIZ/Header/Miscellaneous/ProceduralTerrainFineSurface.hpp'
    header = subprocess.check_output(['git','show',revision+':'+name],cwd=ROOT)
    with tempfile.TemporaryDirectory(prefix='terrain-old-grain-cache-') as directory:
        directory = Path(directory)
        (directory/'cache.hpp').write_bytes(header)
        (directory/'cache.cpp').write_text('#include "cache.hpp"\n#include <iostream>\nint main() {\n    const auto& p = ProceduralTerrain::terrainSparklePixels();\n    std::cout.write(reinterpret_cast<const char*>(p.data()),p.size());\n    return std::cout.good() ? 0 : 1;\n}\n')
        binary = directory/'cache'
        subprocess.run([os.environ.get('CXX','c++'),'-std=c++17','-O2',
                        str(directory/'cache.cpp'),'-o',str(binary)],check=True)
        return subprocess.check_output([str(binary)])


class MaterialRig:
    def __init__(self, gl, table, size=(32, 24), fragment=None, formats=None):
        self.gl, self.size = gl, size
        self.vertex_source = PLANE_VERTEX
        self.fragment_source = fragment or shader_source(TERRAIN/'render.fs.glsl')
        self.specialized = 'ENDURANCE_TERRAIN_RENDER_OPTIONS_GLSL' in self.fragment_source
        self.variants, self.program = {}, None
        if not hasattr(gl, 'terrain_stain'):
            # Production C++ bake, not an independent Python reimplementation.
            payload = export_stain()
            assert len(payload) == 512*512
            gl.terrain_stain = gl.texture((512,512),internal=0x8229,levels=10,value=0.)
            gl.sampler(0,gl.terrain_stain)
            gl.glTexImage2D(TEXTURE_2D,0,0x8229,512,512,0,0x1903,0x1401,C.c_char_p(payload))
            gl.glTexParameteri(TEXTURE_2D,0x2802,0x2901)
            gl.glTexParameteri(TEXTURE_2D,0x2803,0x2901)
            gl._fn('glGenerateMipmap',None,C.c_uint)
            gl.glGenerateMipmap(TEXTURE_2D)
            gl.check('production terrain stain cache')
        self.grain_texture = 'terrainGrainTexture' in self.fragment_source
        if self.grain_texture and not hasattr(gl, 'terrain_grain'):
            payload = export_grain() # Exact production decode + channel packing.
            gl.terrain_grain = gl.texture((1024,1024),internal=0x8058,levels=1,value=0.)
            gl.sampler(1,gl.terrain_grain)
            gl.glTexImage2D(TEXTURE_2D,0,0x8058,1024,1024,0,0x1908,0x1401,C.c_char_p(payload))
            gl.glTexParameteri(TEXTURE_2D,0x2801,0x2601) # GL_LINEAR, no mipmaps
            gl.glTexParameteri(TEXTURE_2D,0x2800,0x2601)
            gl.glTexParameteri(TEXTURE_2D,0x2802,0x2901)
            gl.glTexParameteri(TEXTURE_2D,0x2803,0x2901)
            gl.check('production packed terrain grain texture')
        self.historical_sparkle = 'terrainSparkleCache' in self.fragment_source
        if self.historical_sparkle and not hasattr(gl, 'terrain_sparkle'):
            payload = historical_sparkle_cache()
            assert len(payload) == 128*128*4
            gl.terrain_sparkle = gl.texture((128,128),internal=0x8058,levels=1,value=0.)
            gl.sampler(1,gl.terrain_sparkle)
            gl.glTexImage2D(TEXTURE_2D,0,0x8058,128,128,0,0x1908,0x1401,C.c_char_p(payload))
            gl.glTexParameteri(TEXTURE_2D,0x2802,0x2901)
            gl.glTexParameteri(TEXTURE_2D,0x2803,0x2901)
            gl.check('historical terrain sparkle cache')
        self.table_size = len(table)
        self.matrix_values = tuple(range(25))
        self.table = gl.buffer(table)
        gl.glBindBufferBase(UNIFORM_BUFFER, 6, self.table)
        if formats is None: formats=(RG32F,RGBA32F,RGBA32F,RGBA32F)
        self.outputs = [gl.texture(size,internal=format_,linear=False) for format_ in formats]
        framebuffer, vao = C.c_uint(), C.c_uint()
        gl.glGenFramebuffers(1, C.byref(framebuffer)); gl.framebuffers.append(framebuffer.value)
        gl.glGenVertexArrays(1, C.byref(vao)); gl.arrays.append(vao.value)
        self.framebuffer, self.vao = framebuffer.value, vao.value
        gl.glBindFramebuffer(0x8D40, self.framebuffer)
        for i, texture in enumerate(self.outputs):
            gl.glFramebufferTexture2D(0x8D40, 0x8CE0+i, TEXTURE_2D, texture, 0)
        gl.glDrawBuffers(4, (C.c_uint*4)(*(0x8CE0+i for i in range(4))))
        assert gl.glCheckFramebufferStatus(0x8D40) == 0x8CD5
        gl.glBindFramebuffer(0x8D40, 0)
        gl._fn('glUniform1uiv', None, C.c_int, C.c_int, C.POINTER(C.c_uint))
        gl._fn('glGetUniformBlockIndex', C.c_uint, C.c_uint, C.c_char_p)
        gl._fn('glGetActiveUniformBlockiv', None, C.c_uint, C.c_uint, C.c_uint, C.POINTER(C.c_int))
        self.uniforms = {
            'microDetailStrength': ('1f', .15), 'microDetailScale': ('1f', 2.),
            'worldSeed': ('1ui', 1337), 'debugLod': ('1i', 0),
            'terrainBiomeOverride': ('1i', 0), 'terrainSeaLevel': ('1f', 0.),
            'terrainSeaBlendWidth': ('1f', 64.), 'terrainMacroPeriod': ('1f', 384.),
            'terrainPatchPeriod': ('1f', 32.), 'terrainMaterialDebug': ('1i', 0),
            'terrainAlbedoGamma': ('1f', 2.2),
            'terrainVegetationStainSize': ('1f', 2.5),
            'terrainOtherStainSize': ('1f', .25),
            'terrainSparkleGrainSize': ('1f', .01),
            'testOrigin': ('3f', -57.3, 180., 71.8),
            'testAxisX': ('3f', 40., 0., 0.), 'testAxisY': ('3f', 0., 0., 30.),
            'testNormal': ('3f', 0., 1., 0.), 'testViewOffset': ('3f', 0., 0., 0.),
            'testClimate': ('2f', .3, .6), 'testLod': ('1ui', 0),
        }
        self._select_variant()

    def _select_variant(self):
        gl = self.gl
        lod = int(self.uniforms['debugLod'][1])
        debug = 0 if lod else int(self.uniforms['terrainMaterialDebug'][1])
        biome = int(self.uniforms['terrainBiomeOverride'][1])
        detail = int(self.uniforms['microDetailStrength'][1] > 0.)
        key = (lod, debug, biome, detail) if self.specialized else ()
        if key not in self.variants:
            defines = (f'#define DEBUG_TERRAIN_MATERIAL {debug}\n'
                       f'#define TERRAIN_FIXED_BIOME {biome}\n'
                       f'#define TERRAIN_MICRODETAIL_ENABLED {detail}\n'
                       + ('#define DEBUG_TERRAIN_LOD\n' if lod else '')) if self.specialized else ''
            def source(text):
                version, body = text.split('\n', 1)
                return version + '\n' + defines + body
            program = gl.compile([(0x8B31, source(self.vertex_source)),
                                  (0x8B30, source(self.fragment_source))],
                                 f'actual terrain material variant {key}')
            index = gl.glGetUniformBlockIndex(program, b'TerrainMaterialTable')
            if index != 0xffffffff:
                extent = C.c_int()
                gl.glGetActiveUniformBlockiv(program, index, 0x8A40, C.byref(extent))
                assert extent.value == self.table_size, ('std140 table extent', extent.value, self.table_size)
            self.variants[key] = program
        program = self.variants[key]
        if self.program == program: return False
        self.program = program
        for name, (kind, *values) in self.uniforms.items():
            gl.uniform(program, name, kind, *values)
        self.matrix(self.matrix_values)
        return True

    def set_vertex(self, vertex_source):
        self.vertex_source = vertex_source
        self.variants = {}
        self._select_variant()

    def matrix(self, values):
        self.matrix_values = tuple(values)
        gl = self.gl
        gl.glUseProgram(self.program)
        location = gl.glGetUniformLocation(self.program, b'terrainBiomeMatrix[0]')
        if location >= 0:
            gl.glUniform1uiv(location, 25, (C.c_uint*25)(*self.matrix_values))

    def upload(self, table):
        self.gl.resize(self.table, table)
        self.gl.glBindBufferBase(UNIFORM_BUFFER, 6, self.table)

    def set(self, **settings):
        for name, values in settings.items():
            kind = self.uniforms[name][0]
            if not isinstance(values, tuple): values = (values,)
            self.uniforms[name] = (kind, *values)
        if self._select_variant(): return
        for name in settings:
            kind, *values = self.uniforms[name]
            self.gl.uniform(self.program, name, kind, *values)

    def draw(self):
        gl = self.gl
        gl.glBindBufferBase(UNIFORM_BUFFER, 6, self.table)
        gl.glBindFramebuffer(0x8D40, self.framebuffer)
        gl.glViewport(0, 0, *self.size)
        gl.glDisable(0x0B71); gl.glDisable(0x0BE2)
        gl.glBindVertexArray(self.vao)
        gl.glUseProgram(self.program)
        gl.sampler(0,gl.terrain_stain)
        if self.historical_sparkle: gl.sampler(1,gl.terrain_sparkle)
        if self.grain_texture: gl.sampler(1,gl.terrain_grain)
        gl.glDrawArrays(0x0004, 0, 3)
        gl.glBindFramebuffer(0x8D40, 0)

    def read(self):
        self.draw()
        values = [self.gl.texels(t, self.size, channels=2 if i == 0 else 4)
                  for i, t in enumerate(self.outputs)]
        return values


def pixels(values, channels=4):
    return zip(*(values[i::channels] for i in range(channels)))


def linear_colors(values, gamma=2.2):
    return [tuple(max(v, 0.)**gamma for v in p[:3]) for p in pixels(values)]


def max_difference(a, b):
    return max(abs(x-y) for x, y in zip(a, b))


def check_authored(rig, table):
    rig.upload(table)
    distinct, color_ranges, rough_ranges = [], [], []
    for biome in range(26):
        rig.set(terrainBiomeOverride=biome)
        output = rig.read()
        for channel in output:
            assert all(math.isfinite(x) for x in channel), ('nonfinite authored material', biome)
        for n in pixels(output[1]):
            assert abs(sum(v*v for v in n[:3])-1.) < 3e-5, ('normal length', biome, n)
        for color in linear_colors(output[2]):
            assert all(-1e-6 <= c <= 1.00001 for c in color), ('reflectance', biome, color)
        for m, r, ao, _ in pixels(output[3]):
            assert 0. <= m <= 1. and .03999 <= r <= 1. and 0. <= ao <= 1., (biome, m, r, ao)
        distinct.append(tuple(round(sum(output[2][c::4])/len(output[2][c::4]), 5) for c in range(3)))
        color_ranges.append(max(output[2])-min(output[2]))
        rough_ranges.append(max(output[3][1::4])-min(output[3][1::4]))
    assert len(set(distinct)) == 26, ('biome palettes not visually distinct', distinct)
    assert max(rough_ranges) > .005, ('roughness lacks procedural variation', rough_ranges)
    # Wall profiles are authored independently, including the material on cave
    # ceilings. Exercise their real gradients rather than only a constant oracle.
    for normal in ((0.,0.,1.),(0.,-1.,0.)):
        rig.set(testNormal=normal,testAxisY=(0.,30.,0.) if normal[2] else (0.,0.,30.))
        for biome in range(26):
            rig.set(terrainBiomeOverride=biome)
            out=rig.read()
            assert all(math.isfinite(v) for channel in out for v in channel), ('wall/ceiling',biome,normal)
            for n in pixels(out[1]):
                assert abs(sum(v*v for v in n[:3])-1.) < 3e-5
                assert sum(a*b for a,b in zip(normal,n)) > 0., ('detail flipped base normal',biome,normal)
    rig.set(testNormal=(0.,1.,0.),testAxisY=(0.,0.,30.))
    print('PASS 26 authored biomes: finite ground/wall/ceiling normals and PBR, distinct colors and roughness variation', flush=True)


def check_oracles(rig):
    table, colors, _ = literal_table(metallic=.75, roughness=.43, ao=.61)
    rig.upload(table)
    rig.set(terrainBiomeOverride=7)
    for gamma in (1., 2.2, 2.6):
        rig.set(terrainAlbedoGamma=gamma)
        output = rig.read()
        assert max(max_difference(c, colors[7]) for c in linear_colors(output[2], gamma)) < 2e-5
        for m, r, ao, _ in pixels(output[3]):
            assert max_difference((m, r, ao), (.75, .43, .61)) < 2e-5, (m, r, ao)
    rig.set(terrainAlbedoGamma=2.2, terrainBiomeOverride=-1)
    for mapping in (tuple(range(25)), tuple(reversed(range(25)))):
        rig.matrix(mapping)
        for climate in ((.1,.15),(.375,.625),(.49,.98),(.73,.27),(0.,0.),(1.,1.)):
            x, y = (v*4 for v in climate)
            i, j = int(x), int(y)
            fx, fy = x-i, y-j
            smooth = lambda f: f*f*f*(f*(f*6.-15.)+10.)
            fx, fy = smooth(fx), smooth(fy)
            corners = (i+j*5, min(i+1,4)+j*5, i+min(j+1,4)*5, min(i+1,4)+min(j+1,4)*5)
            weights = ((1-fx)*(1-fy),fx*(1-fy),(1-fx)*fy,fx*fy)
            expected = [sum(colors[mapping[k]][c]*w for k,w in zip(corners,weights)) for c in range(3)]
            rig.set(testClimate=climate)
            output = rig.read()
            assert max(max_difference(c, expected) for c in linear_colors(output[2])) < 3e-5, (mapping, climate, expected)
    rig.matrix(tuple(range(25)))
    rig.set(testOrigin=(-57.3,-100.,71.8))
    output = rig.read()
    assert max(max_difference(c,colors[25]) for c in linear_colors(output[2])) < 2e-5
    rig.set(terrainBiomeOverride=0)
    output = rig.read()
    assert max(max_difference(c,colors[0]) for c in linear_colors(output[2])) < 2e-5
    rig.set(testOrigin=(-57.3,180.,71.8))
    red, blue = [(.7,.1,.05)]*26, [(.05,.2,.6)]*26
    table, _, _ = literal_table(red,blue)
    rig.upload(table)
    for normal, expected in (((0.,1.,0.),red[0]),((1.,0.,0.),blue[0]),((0.,-1.,0.),blue[0])):
        rig.set(testNormal=normal)
        output = rig.read()
        assert max(max_difference(c,expected) for c in linear_colors(output[2])) < 2e-5
    rig.set(testNormal=(0.,1.,0.))
    print('PASS independent constant-material oracle: gamma roundtrip, metallic/AO/roughness, four-corner climate weights, remapped matrix, marine override and cave ceilings', flush=True)


def check_continuity(rig, table):
    rig.upload(table)
    rig.set(terrainBiomeOverride=-1)
    for axis in (0,1):
        for boundary in (.25,.5,.75):
            samples=[]
            for offset in (-1e-6,1e-6):
                climate=[.37,.63]
                climate[axis]=boundary+offset
                rig.set(testClimate=tuple(climate))
                samples.append(rig.read())
            for output in (1,2,3):
                assert max_difference(samples[0][output],samples[1][output]) < .001, ('climate seam',axis,boundary,output)
    rig.set(terrainBiomeOverride=2,worldSeed=1337)
    first=rig.read()
    rig.set(testViewOffset=(80.,-24.,-300.),testLod=8)
    second=rig.read()
    for output in (1,2,3):
        assert first[output] == second[output], ('world-space material depends on view/LOD',output)
    rig.set(worldSeed=9127)
    changed=rig.read()
    assert max_difference(first[2],changed[2]) > .0001, 'world seed has no material effect'
    rig.set(worldSeed=1337)
    repeat=rig.read()
    for output in (1,2,3): assert first[output] == repeat[output], 'nondeterministic material'
    # Crossing a world-coordinate zero or cell boundary must not produce NaNs,
    # and a fixed location must agree across separate draws/chunk descriptions.
    for origin in ((-100000.,180.,-100000.),(-.001,180.,-.001),(0.,180.,0.),(100000.,180.,100000.)):
        rig.set(testOrigin=origin)
        output=rig.read()
        assert all(math.isfinite(v) for channel in output for v in channel)
    rig.set(testOrigin=(-57.3,180.,71.8),testViewOffset=(0.,0.,0.),testLod=0)
    for mode in range(7):
        rig.set(terrainMaterialDebug=mode)
        output=rig.read()
        assert all(math.isfinite(v) for channel in output for v in channel), ('debug view',mode)
        if mode:
            assert all(tag == 1. for tag in output[0][1::2]), ('debug must use unlit emission tag',mode)
        if mode in (4,5,6):
            for i,color in enumerate(linear_colors(output[2])):
                if mode==4: expected=(output[3][i*4+1],)*3
                elif mode==5: expected=(output[3][i*4],output[3][i*4+2],0.)
                else: expected=tuple(v*.5+.5 for v in output[1][i*4:i*4+3])
                assert max_difference(color,expected)<3e-5, ('debug values',mode,color,expected)
    rig.set(terrainMaterialDebug=0)
    print('PASS climate-cell continuity, world-space/LOD stability, deterministic seeds, negative/large coordinates and debug views',flush=True)


def check_filtering(rig, table):
    # This check concerns the generic smooth relief. Granular layers explicitly
    # keep their normal texture at range, verified separately below.
    smooth=table
    for offset in (0,112,272,384):
        smooth=edited_vec(smooth,15,offset+96,(0.,.16,0.,0.))
    rig.upload(smooth)
    rig.set(terrainBiomeOverride=15,microDetailStrength=1.,microDetailScale=2.,
            testOrigin=(-2.3,180.,1.8),testAxisX=(1.,0.,0.),testAxisY=(0.,0.,1.))
    close=rig.read()
    deviations=[math.sqrt(n[0]*n[0]+n[2]*n[2]) for n in pixels(close[1])]
    assert max(deviations) > .001, 'near-field procedural normals absent'
    rig.set(testAxisX=(1.e6,0.,0.),testAxisY=(0.,0.,1.e6))
    far=rig.read()
    deviations=[math.sqrt(n[0]*n[0]+n[2]*n[2]) for n in pixels(far[1])]
    assert max(deviations) < .001, ('subpixel normal detail did not fade',max(deviations))
    rig.set(microDetailStrength=.15,testOrigin=(-57.3,180.,71.8),
            testAxisX=(40.,0.,0.),testAxisY=(0.,0.,30.))
    rig.upload(table)
    print('PASS near-field microrelief and derivative-based subpixel normal suppression',flush=True)


def edited_vec(table, biome, offset, values):
    """Edit an independently specified std140 byte offset in one biome record."""
    data=bytearray(table)
    struct.pack_into('<4f',data,biome*592+offset,*values)
    return bytes(data)


def check_fine_surface(gl):
    # Independently constant materials isolate each new effect. Real GLSL and
    # the real C++ cache are used, including production mip generation.
    table,_,_=literal_table(colors=[(.3,.4,.2)]*26,roughness=.8)
    table=edited_vec(table,0,224,(1.,0.,0.,0.)) # base only
    rig=MaterialRig(gl,table,size=(128,128))
    rig.set(testOrigin=(-.37,180.,.23),testAxisX=(1.,0.,0.),testAxisY=(0.,0.,1.),
            microDetailStrength=0.,terrainSparkleGrainSize=.04)
    baseline=rig.read()
    grains=edited_vec(table,0,96,(1.,.12,0.,0.))
    rig.upload(grains); sparkle=rig.read()
    assert sparkle[0] == baseline[0] and sparkle[2] == baseline[2], 'glints changed geometry/emission/albedo'
    assert sparkle[3][0::4] == baseline[3][0::4] and sparkle[3][2::4] == baseline[3][2::4]
    assert .119 < min(sparkle[3][1::4]) < .16
    assert .16 < max(sparkle[3][1::4]) < .241, 'noise-driven microroughness missing'
    facets=list(pixels(sparkle[1]))
    for n in facets:
        assert abs(sum(x*x for x in n[:3])-1.) < 3e-5
        assert n[1]>.6, 'decoded normal left the terrain hemisphere'
    assert max(n[0] for n in facets)-min(n[0] for n in facets)>.5
    assert abs(sum(n[0] for n in facets)/len(facets))<.08
    assert abs(sum(n[2] for n in facets)/len(facets))<.08
    rig.set(testLod=5,testViewOffset=(5.,2.,-10.))
    stable=rig.read()
    assert stable[1:] == sparkle[1:], 'world grains changed with chunk LOD or camera origin'

    # Check real source pixels: matching normal/noise UVs, unsigned blue decode,
    # and hardware bilinear filtering (including the texture repeat boundary).
    from PIL import Image
    folder=ROOT/'ENDURANCE/Ressources/Texture/Terrain'
    normals=Image.open(folder/'NormalMap.png').convert('RGB')
    noise=Image.open(folder/'grain.png').convert('L')
    def source_pixel(x,y):
        return (*[v/255. for v in normals.getpixel((x%1024,y%1024))],
                noise.getpixel((x%1024,y%1024))/255.)
    def probe(u,v):
        u*=.04; v*=.04
        rig.set(worldSeed=0,testOrigin=(.8*u+.6*v,0.,-.6*u+.8*v),
                testAxisX=(0.,0.,0.),testAxisY=(0.,0.,0.))
        result=rig.read()
        return (*result[1][:3],result[3][1])
    for x,y,fx,fy in ((31,61,0.,0.),(31,61,.25,.75),(1023,1023,.5,.5)):
        samples=[source_pixel(x+dx,y+dy) for dy in (0,1) for dx in (0,1)]
        weights=((1-fx)*(1-fy),fx*(1-fy),(1-fx)*fy,fx*fy)
        red,green,blue,gray=[sum(w*c[i] for w,c in zip(weights,samples)) for i in range(4)]
        nx,ny,nz=red*2.-1.,green*2.-1.,blue
        length=math.sqrt(nx*nx+ny*ny+nz*nz)
        nx*=.55/length; ny*=.55/length; nz=.45+.55*nz/length
        world=(.8*nx+.6*ny,nz,-.6*nx+.8*ny)
        length=math.sqrt(sum(v*v for v in world))
        expected=(*(v/length for v in world),.12+(1-gray)*.12)
        actual=probe(x+.5+fx,y+.5+fy)
        # RGBA8 hardware bilerp can round intermediate channels to 8 bits.
        assert max_difference(actual,expected)<.008, ('PNG channels/UVs/filter/decode mismatch',x,y,fx,fy,actual,expected)
    left=probe(1024.-1e-5,63.37); right=probe(1e-5,63.37)
    assert max_difference(left,right)<.002, 'grain texture repeat seam is discontinuous'
    rig.set(worldSeed=1337,testOrigin=(-.37,180.,.23),testAxisX=(1.,0.,0.),testAxisY=(0.,0.,1.))
    first=rig.read()
    period=1024*.04
    rig.set(testOrigin=(-.37+.8*period,180.,.23-.6*period)); repeated_grain=rig.read()
    assert max_difference(first[1],repeated_grain[1])<.004, ('metres-per-texel scale is incorrect', max_difference(first[1],repeated_grain[1]))
    rig.set(testOrigin=(-.37,180.,.23),worldSeed=7331); reseeded=rig.read()
    assert max_difference(first[1],reseeded[1])>.1, 'grain ignores the world seed'
    rig.set(worldSeed=1337)

    # Saved v4 records can still contain stain controls. Granular materials
    # must suppress those controls before blending, without changing albedo.
    stale=edited_vec(grains,0,96,(1.,.12,.10,.06))
    rig.upload(stale); clean=rig.read()
    assert max_difference(clean[2],baseline[2])<2e-6, 'saved stains remain on sand/snow'
    assert max_difference(clean[3],sparkle[3])<2e-6, 'saved stain roughness affects granular material'
    # Old smooth microrelief is a separate source of ring-shaped highlights.
    # It must remain on ordinary material, but not on the granular material.
    noisy=edited_vec(table,0,48,(0.,1.,1.,.2))
    rig.set(microDetailStrength=.3)
    rig.upload(noisy); ordinary=rig.read()
    assert max_difference(ordinary[1],baseline[1])>.001, 'microrelief regression setup is flat'
    rig.upload(edited_vec(noisy,0,96,(1.,.12,.10,.06))); granular=rig.read()
    assert max_difference(granular[1],sparkle[1])<2e-6, 'coherent FBM bumps survive on sand/snow'
    assert max_difference(granular[3],sparkle[3])<2e-6, 'coherent FBM roughness survives on sand/snow'
    rig.set(microDetailStrength=0.)

    stains=edited_vec(table,0,96,(0.,.16,.10,.06))
    rig.upload(stains); stain=rig.read()
    assert stain[1] == baseline[1], 'stains introduced generic normal noise'
    ratios=[c[0]/.3 for c in linear_colors(stain[2])]
    assert min(ratios) < .902 and max(ratios) > .998, ('discrete stain mask',min(ratios),max(ratios))
    for ratio,rough in zip(ratios,stain[3][1::4]):
        assert .89999 <= ratio <= 1.00001, ('10 percent albedo bounds',ratio)
        assert abs(rough-(.8-.6*(1.-ratio))) < 3e-6, 'stain roughness not coupled to mask'
    rig.set(testOrigin=(12.43,180.,-9.37)) # inverse projection of one 16 m repeat
    repeated=rig.read()
    assert max_difference(stain[2],repeated[2]) < .0002, 'procedural cache does not tile continuously'

    # The vegetation pattern must be EXACTLY ten times larger, independently
    # of the biome's relief. Scaling the whole domain keeps the projected UVs.
    rig.set(testOrigin=(-.037,0.,.023),testAxisX=(.1,0.,0.),testAxisY=(0.,0.,.1))
    rig.upload(stains); small=rig.read()
    green=edited_vec(stains,0,576,(1.,0.,0.,0.))
    rig.set(testOrigin=(-.37,0.,.23),testAxisX=(1.,0.,0.),testAxisY=(0.,0.,1.))
    rig.upload(green); large=rig.read()
    assert max_difference(small[2],large[2]) < 1e-5, 'vegetation stains are not 10x larger'
    # Blend the two evaluated fixed-scale masks, not an interpolated frequency.
    rig.upload(stains); fine=rig.read()
    rig.upload(edited_vec(stains,0,576,(.5,0.,0.,0.))); blended=rig.read()
    for a,b,c in zip(linear_colors(large[2]),linear_colors(fine[2]),linear_colors(blended[2])):
        assert max(abs((x+y)*.5-z) for x,y,z in zip(a,b,c)) < 3e-6, 'stain blending moves the domains'
    rig.upload(grains); first=rig.read()
    rig.upload(edited_vec(grains,0,560,(1.,.91,3.,.48))); second=rig.read()
    assert max_difference(first[1],second[1]) < 1e-6 and max_difference(first[3],second[3]) < 1e-6, 'sparkle still follows the relief FBM'
    rig.set(terrainSparkleGrainSize=.08); resized=rig.read()
    assert max_difference(first[3],resized[3]) > .01, 'sparkle size control has no effect'
    rig.set(terrainSparkleGrainSize=.04)

    # Accents/ceilings never inherit a neighbouring base's sparkle or stains.
    for effect in (grains,stains):
        rig.upload(edited_vec(effect,0,224,(0.,0.,0.,0.)))
        accent=rig.read()
        assert max_difference(accent[2],baseline[2]) < 2e-6
        assert max_difference(accent[3],baseline[3]) < 2e-6
        rig.upload(effect); rig.set(testNormal=(0.,-1.,0.))
        ceiling=rig.read()
        assert max_difference(ceiling[2],baseline[2]) < 2e-6
        assert max_difference(ceiling[3],baseline[3]) < 2e-6
        rig.set(testNormal=(0.,1.,0.))

    # A pixel spans more than the complete 16 m cache: its last mip is uniform.
    rig.set(testAxisX=(4096.,0.,0.),testAxisY=(0.,0.,4096.))
    rig.upload(grains); far=rig.read()
    assert max_difference(far[1],baseline[1]) > .1, 'distant facet orientations disappeared'
    assert max_difference(far[3],baseline[3]) > .1, 'white grains must remain active at large pixel footprints'
    rig.upload(stains); far=rig.read()
    far_ratios=[c[0]/.3 for c in linear_colors(far[2])]
    assert .94 < min(far_ratios) <= max(far_ratios) < .96, 'mips must preserve average stain coverage'
    assert max(far_ratios)-min(far_ratios) < .001, 'distant stains shimmer instead of filtering'
    disabled=shader_source(TERRAIN/'render.fs.glsl').replace('#version 460 core',
        '#version 460 core\n#define TERRAIN_FINE_SURFACE_ENABLED 0',1)
    off=MaterialRig(gl,stains,fragment=disabled)
    assert max_difference(off.read()[2],baseline[2][:32*24*4]) < 2e-6
    print('PASS fine surfaces: original PNG normals/noise, bilinear repeat/UV/encoding, bounded normals, no granular FBM leakage, 10% stains, independent scales, LOD/seed stability, no grain cutoff and compile-time disable',flush=True)


def check_material_identity(rig, table):
    # Each semantic family changes surface normals at the same physical scale,
    # with an identical constant albedo. These are normal fields, not recolors.
    constant,_,_=literal_table()
    for surface in (0,272):
        for material in (0,112):
            constant=edited_vec(constant,0,surface+material+48,(0.,1.,1.,.05))
    rig.set(terrainBiomeOverride=0,testOrigin=(-1.7,180.,2.3),testAxisX=(2.,0.,0.),
            testAxisY=(0.,0.,2.),testNormal=(0.,1.,0.),microDetailStrength=.15)
    signatures=[];reference=None
    for family in range(8):
        candidate=edited_vec(constant,0,256,(float(family),1.2,.65,.3))
        candidate=edited_vec(candidate,0,560,(1.,.14,2.,.4))
        rig.upload(candidate);out=rig.read()
        assert all(math.isfinite(v) for attachment in out for v in attachment), ('normal family',family)
        signatures.append(tuple(round(v,5) for v in out[1]))
        if reference is None:reference=out
        else:
            assert max_difference(reference[2],out[2])<2e-6, ('normal family changes albedo',family)
            assert max_difference(reference[3],out[3])<2e-6, ('normal family changes PBR response',family)
    assert len(set(signatures))==8, 'semantic normal profiles collapse to the same field'

    # Changing the palette pattern must never create a different bump shape.
    # Author color grouping independently from the mineral/soil microrelief.
    rig.set(terrainBiomeOverride=15)
    rig.upload(table);before=rig.read()
    colored=table
    for material in (0,112):
        colored=edited_vec(colored,15,material+80,(0.,0.,0.,1.))
        colored=edited_vec(colored,15,material,(.1,.7,.3,.81))
        colored=edited_vec(colored,15,material+4*4,(.7,.1,.4,.81))
    rig.upload(colored);after=rig.read()
    assert max_difference(before[2],after[2])>.001, 'palette test did not change albedo'
    assert max_difference(before[1],after[1])<2e-6, 'albedo noise still drives generic normal bumps'

    # Stable per-biome domains use artist-controlled scales and fractional
    # octaves. Fine detail fades by footprint, not by draw LOD or frame index.
    rig.set(terrainBiomeOverride=0,testAxisX=(1.,0.,0.),testAxisY=(0.,0.,1.))
    octave_outputs=[]
    constant=edited_vec(constant,0,256,(0.,.3,.2,1.))
    for count in (1.,1.5,2.9):
        rig.upload(edited_vec(constant,0,560,(1.,.16,count,.5)))
        octave_outputs.append(rig.read()[1])
    for first,second in zip(octave_outputs,octave_outputs[1:]):
        assert max_difference(first,second)>1e-5, 'octave controls have no normal detail effect'
    scaled=[]
    for period in (.08,.32):
        rig.upload(edited_vec(constant,0,560,(1.,period,2.,.4)))
        scaled.append(rig.read()[1])
    assert max_difference(*scaled)>1e-4, 'grain period control has no effect'

    rig.set(terrainBiomeOverride=15,testAxisX=(240.,0.,0.),testAxisY=(0.,0.,240.))
    scales=[]
    floats=struct.unpack('<3848f',table)
    original=floats[15*148+140:15*148+144]
    for multiplier in (.2,3.):
        rig.upload(edited_vec(table,15,560,(multiplier,*original[1:])))
        scales.append(rig.read()[2])
    assert max_difference(*scales)>1e-5, 'biome patch scale has no albedo effect'
    rig.upload(table)
    rig.set(terrainBiomeOverride=0,testOrigin=(-57.3,180.,71.8),
            testAxisX=(40.,0.,0.),testAxisY=(0.,0.,30.))
    print('PASS independent semantic normal families, palette/normal separation, per-biome scales and fractional octaves',flush=True)


def check_noise_gradients(gl):
    """Independent finite differences check the production analytic derivative."""
    source='''#version 460 core
layout(local_size_x=64) in;
uniform uint worldSeed=1337u;
uniform float microDetailScale=2.0;
uniform float microDetailStrength=0.15;
uniform float terrainMacroPeriod=384.0;
uniform float terrainPatchPeriod=32.0;
'''+shader_source(TERRAIN/'patterns.glsl')+'''
layout(std430,binding=0) readonly buffer Inputs { vec4 points[]; };
layout(std430,binding=1) writeonly buffer Outputs { vec4 result[]; };
uniform uint pointCount;
void main() {
    uint i=gl_GlobalInvocationID.x;
    if(i>=pointCount) return;
    vec3 p=points[i].xyz;
    float period=points[i].w;
    vec3 offset=vec3(71.0,19.0,-53.0);
    TerrainNoise n=terrainWorldNoise(p,vec3(0),vec3(0),period,offset);
    vec3 finiteDifference;
    float epsilon=0.002*period;
    for(int axis=0;axis<3;++axis) {
        vec3 d=vec3(0); d[axis]=epsilon;
        float a=terrainWorldNoise(p+d,vec3(0),vec3(0),period,offset).value;
        float b=terrainWorldNoise(p-d,vec3(0),vec3(0),period,offset).value;
        finiteDifference[axis]=(a-b)/(2.0*epsilon);
    }
    result[i*2]=vec4(n.value,n.gradient);
    result[i*2+1]=vec4(finiteDifference,0);
}
'''
    program=gl.compile([(0x91B9,source)],'production noise analytic derivative oracle')
    rng=random.Random(738)
    points=[(rng.uniform(-50.,50.),rng.uniform(-50.,50.),rng.uniform(-50.,50.),period)
            for period in (.5,1.,8.,384.) for _ in range(128)]
    gl.buffer(struct.pack(f'<{len(points)*4}f',*(v for p in points for v in p)),0)
    output=gl.buffer(len(points)*32,1)
    gl.uniform(program,'pointCount','1ui',len(points))
    gl.dispatch(program,(len(points)+63)//64)
    values=struct.unpack(f'<{len(points)*8}f',gl.read(output,len(points)*32))
    maximum=0.
    for i,point in enumerate(points):
        value,*analytic=values[i*8:i*8+4]
        numeric=values[i*8+4:i*8+7]
        error=max_difference(analytic,numeric)*point[3]
        maximum=max(maximum,error)
        assert 0. <= value <= 1. and error<.007, ('analytic world derivative',point,error)
    print(f'PASS 512 production noise gradients vs finite differences, maximum wavelength-scaled error {maximum:.6f}',flush=True)


def check_relief_gradients(gl):
    """Check composed microrelief chain rules against independent finite differences.

    Position derivatives are zero here: filtering is intentionally held at full
    resolution, so the oracle differentiates height, not a moving screen filter.
    """
    source='''#version 460 core
layout(local_size_x=64) in;
uniform uint worldSeed=1337u;
uniform float microDetailScale=2.0;
uniform float microDetailStrength=0.15;
uniform float terrainMacroPeriod=384.0;
uniform float terrainPatchPeriod=32.0;
'''+shader_source(TERRAIN/'patterns.glsl')+'''
layout(std430,binding=0) readonly buffer Inputs { vec4 points[]; };
layout(std430,binding=1) writeonly buffer Outputs { vec4 result[]; };
uniform uint pointCount;
TerrainNoise relief(vec3 p, float family) {
    TerrainPatternContext c=terrainMakePatternContext(p,vec3(0),vec3(0),0.0);
    c=terrainBiomePatterns(11u,vec4(1.0,0.11,2.5,0.3),c);
    return terrainSurfaceRelief(vec4(family,1.3,0.65,0.25),
        vec4(23.0,1.0,0.73,4.0),c);
}
void main() {
    uint i=gl_GlobalInvocationID.x;
    if(i>=pointCount) return;
    vec3 p=points[i].xyz;
    float family=points[i].w;
    TerrainNoise n=relief(p,family);
    vec3 finiteDifference;
    float epsilon=0.00015;
    for(int axis=0;axis<3;++axis) {
        vec3 d=vec3(0); d[axis]=epsilon;
        finiteDifference[axis]=(relief(p+d,family).value-relief(p-d,family).value)/(2.0*epsilon);
    }
    result[i*2]=vec4(n.value,n.gradient);
    result[i*2+1]=vec4(finiteDifference,0);
}
'''
    program=gl.compile([(0x91B9,source)],'semantic relief analytic derivative oracle')
    rng=random.Random(9247)
    points=[(rng.uniform(-6.,6.),rng.uniform(-6.,6.),rng.uniform(-6.,6.),float(family))
            for family in range(8) for _ in range(64)]
    gl.buffer(struct.pack(f'<{len(points)*4}f',*(v for p in points for v in p)),0)
    output=gl.buffer(len(points)*32,1)
    gl.uniform(program,'pointCount','1ui',len(points))
    gl.dispatch(program,(len(points)+63)//64)
    values=struct.unpack(f'<{len(points)*8}f',gl.read(output,len(points)*32))
    maximum=0.
    for i,point in enumerate(points):
        analytic=values[i*8+1:i*8+4];numeric=values[i*8+4:i*8+7]
        error=max_difference(analytic,numeric);maximum=max(maximum,error)
        assert all(math.isfinite(v) for v in (*analytic,*numeric))
        assert error<.005, ('semantic relief derivative',point,error,analytic,numeric)
    print(f'PASS 512 semantic relief gradients vs finite differences, maximum slope error {maximum:.6f}',flush=True)


def render_preview(gl, table, path, settings=None, *, extent=1024., biomes=None, size=(192,144)):
    """Render real packed 12-byte vertices, then display fixed GGX test lighting."""
    from PIL import Image, ImageDraw, ImageFont
    from check_terrain_shadow import normal_code, look_at
    from check_position_precision import column_major, inverse_rigid, ortho, normalized

    biomes=list(range(26)) if biomes is None else biomes
    scale=extent/1024.
    rig=MaterialRig(gl,table,size)
    rig.set_vertex(shader_source(TERRAIN/'render.vs.glsl'))
    if settings: rig.set(**settings)
    rig.matrix(tuple(range(25)))
    for name,signature in {
        'glVertexAttribIPointer':(None,C.c_uint,C.c_int,C.c_uint,C.c_int,C.c_void_p),
        'glEnableVertexAttribArray':(None,C.c_uint),
        'glDrawArraysInstancedBaseInstance':(None,C.c_uint,C.c_int,C.c_int,C.c_int,C.c_uint),
        'glClearBufferfv':(None,C.c_uint,C.c_int,C.POINTER(C.c_float)),
        'glEnable':(None,C.c_uint), 'glClear':(None,C.c_uint),
    }.items(): gl._fn(name,*signature)
    depth=C.c_uint(); gl.glGenTextures(1,C.byref(depth)); gl.textures.append(depth.value)
    gl.glBindTexture(TEXTURE_2D,depth.value)
    gl.glTexImage2D(TEXTURE_2D,0,0x8CAC,*size,0,0x1902,0x1406,None)
    gl.glTexParameteri(TEXTURE_2D,0x2800,0x2600)
    gl.glTexParameteri(TEXTURE_2D,0x2801,0x2600)
    gl.glBindFramebuffer(0x8D40,rig.framebuffer)
    gl.glFramebufferTexture2D(0x8D40,0x8D00,TEXTURE_2D,depth.value,0)
    assert gl.glCheckFramebufferStatus(0x8D40)==0x8CD5
    gl.glBindFramebuffer(0x8D40,0)
    eye=tuple(v*scale for v in (1250.,740.,1410.))
    target=tuple(v*scale for v in (512.,150.,512.))
    view=look_at(eye,target,(0.,1.,0.))
    projection=ortho(-660.*scale,660.*scale,-495.*scale,495.*scale,.05,4000.*scale)
    camera=gl.buffer(struct.pack('<52f',*eye,0.,*column_major(view),
                                 *column_major(inverse_rigid(view)),*column_major(projection)),0)
    chunks=gl.buffer(struct.pack('<4f4I',0.,0.,0.,extent,0,0,0,0),8)

    def height(x,z):
        a=((x-610.)/105.)**2+((z-610.)/200.)**2
        b=((x-340.)/225.)**2+((z-420.)/190.)**2
        return 60.+320.*math.exp(-a)+120.*math.exp(-b)+12.*math.sin(x/90.+math.sin(z/140.))

    def vertex(x,z):
        h=height(x,z)
        dx=(height(x+.1,z)-height(x-.1,z))/.2
        dz=(height(x,z+.1)-height(x,z-.1))/.2
        qx,qy,qz=(round(v*65534./1024.) for v in (x,h,z))
        return (qx|(qy<<16),qz|(128<<16)|(128<<24),normal_code(normalized((-dx,1.,-dz))))

    vertices=[]
    step=16
    for z in range(0,1024,step):
        for x in range(0,1024,step):
            a,b,c,d=vertex(x,z),vertex(x+step,z),vertex(x,z+step),vertex(x+step,z+step)
            vertices.extend((*a,*c,*b,*b,*c,*d))
    vbo=gl.buffer(struct.pack(f'<{len(vertices)}I',*vertices))
    gl.glBindVertexArray(rig.vao)
    gl.glBindBuffer(0x8892,vbo)
    for i in range(3):
        gl.glEnableVertexAttribArray(i)
        gl.glVertexAttribIPointer(i,1,0x1405,12,C.c_void_p(i*4))
    gl.glBindVertexArray(0)

    width,height_px=size
    cell_height=height_px+38
    columns=min(5,len(biomes)); rows=(len(biomes)+columns-1)//columns
    sheet=Image.new('RGB',(width*columns,cell_height*rows+38),(23,25,29))
    draw=ImageDraw.Draw(sheet)
    try: font=ImageFont.truetype('DejaVuSans.ttf',12)
    except OSError: font=ImageFont.load_default()
    draw.text((9,10),f'ENDURANCE | Materials | Synthetic terrain extent {extent:g} m, fixed GGX test lighting',font=font,fill=(230,230,230))
    L=normalized((-.45,.8,.4))
    V=normalized(tuple(a-b for a,b in zip(eye,target)))
    H=normalized(tuple(a+b for a,b in zip(L,V)))
    VH=max(sum(a*b for a,b in zip(V,H)),0.)
    floats=struct.unpack('<3848f',table)
    for tile_index,biome in enumerate(biomes):
        name=NAMES[biome]
        rig.set(terrainBiomeOverride=biome)
        gl.glBindBufferBase(UNIFORM_BUFFER,6,rig.table)
        gl.bind(camera,0); gl.bind(chunks,8)
        gl.glBindFramebuffer(0x8D40,rig.framebuffer)
        for attachment in range(4): gl.glClearBufferfv(0x1800,attachment,(C.c_float*4)(0,0,0,0))
        gl.glEnable(0x0B71); gl.glClear(0x00000100)
        gl.glViewport(0,0,*size)
        gl.glBindVertexArray(rig.vao)
        gl.glUseProgram(rig.program)
        gl.sampler(0,gl.terrain_stain)
        if rig.historical_sparkle: gl.sampler(1,gl.terrain_sparkle)
        gl.glDrawArraysInstancedBaseInstance(0x0004,0,len(vertices)//3,1,0)
        gl.glBindFramebuffer(0x8D40,0)
        data=[gl.texels(t,size,channels=2 if i==0 else 4) for i,t in enumerate(rig.outputs)]
        rgb=[]
        for pixel in range(width*height_px):
            if data[0][pixel*2]==0.:
                rgb.append((82,96,110)); continue
            n=data[1][pixel*4:pixel*4+3]
            albedo=tuple(max(v,0.)**2.2 for v in data[2][pixel*4:pixel*4+3])
            metal,rough,ao=data[3][pixel*4:pixel*4+3]
            NL=max(sum(a*b for a,b in zip(n,L)),0.)
            NV=max(sum(a*b for a,b in zip(n,V)),1e-4)
            NH=max(sum(a*b for a,b in zip(n,H)),0.)
            alpha2=max(rough,.04)**4
            D=alpha2/(math.pi*(NH*NH*(alpha2-1.)+1.)**2)
            k=(rough+1.)**2/8.
            G=(NV/(NV*(1.-k)+k))*(NL/(NL*(1.-k)+k))
            color=[]
            for channel in range(3):
                F0=.04*(1.-metal)+albedo[channel]*metal
                F=F0+(1.-F0)*(1.-VH)**5
                spec=D*G*F/max(4.*NV*NL,1e-4)
                diffuse=(1.-F)*(1.-metal)*albedo[channel]/math.pi
                direct=(diffuse+spec)*NL*(3.6,3.4,3.1)[channel]
                ambient=albedo[channel]*(.18,.22,.28)[channel]*ao
                value=direct+ambient
                value=(value*(2.51*value+.03))/(value*(2.43*value+.59)+.14)
                color.append(round(255.*max(0.,min(value,1.))**(1./2.2)))
            rgb.append(tuple(color))
        tile=Image.new('RGB',size); tile.putdata(rgb)
        tile=tile.transpose(Image.Transpose.FLIP_TOP_BOTTOM)
        x=(tile_index%columns)*width; y=(tile_index//columns)*cell_height+38
        sheet.paste(tile,(x,y))
        draw.text((x+5,y+height_px+2),name,font=font,fill=(230,230,230))
        swatches=[]
        for material in (0,28,68,96):
            for stop in (0,4,8):
                start=biome*148+material+stop
                swatches.append(tuple(round(255.*max(0.,min(v,1.))**(1./2.2)) for v in floats[start:start+3]))
        for i,color in enumerate(swatches):
            draw.rectangle((x+i*width//12,y+height_px+21,x+(i+1)*width//12-1,y+height_px+34),fill=color)
    path.parent.mkdir(parents=True,exist_ok=True)
    sheet.save(path)
    print(f'Wrote synthetic material contact sheet: {path}',flush=True)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--preview',type=Path,help='optional synthetic terrain contact sheet PNG')
    parser.add_argument('--preview-extent',type=float,default=1024.,help='synthetic tile world extent in metres')
    parser.add_argument('--preview-biomes',help='optional comma-separated biome IDs for preview')
    parser.add_argument('--preview-width',type=int,default=192)
    args=parser.parse_args()
    table,settings=export_appearance()
    with AtmosphereGL() as gl:
        print(f'OpenGL {gl.version}; renderer: {gl.renderer}',flush=True)
        gl.compile([(0x8B31,shader_source(TERRAIN/'render.vs.glsl')),
                    (0x8B30,shader_source(TERRAIN/'render.fs.glsl'))],'unchanged packed terrain VS/FS pair')
        rig=MaterialRig(gl,table)
        rig.set(**settings)
        check_authored(rig,table)
        check_oracles(rig)
        check_continuity(rig,table)
        check_filtering(rig,table)
        check_material_identity(rig,table)
        check_fine_surface(gl)
        check_noise_gradients(gl)
        check_relief_gradients(gl)
        gl.check('terrain material validation')
        if args.preview:
            selected=[int(value) for value in args.preview_biomes.split(',')] if args.preview_biomes else None
            render_preview(gl,table,args.preview,settings,extent=args.preview_extent,biomes=selected,
                           size=(args.preview_width,args.preview_width*3//4))
    print('PASS production terrain materials GPU validation',flush=True)


if __name__=='__main__':
    main()
