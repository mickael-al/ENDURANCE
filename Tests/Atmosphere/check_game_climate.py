#!/usr/bin/env python3
"""Exercise actual game climate/palette/fog shaders using the terrain's EGL harness.

Blocking GPU reads are test instrumentation only. The game component uses
zero-timeout fence polling and never reads an unfinished palette slot.
"""
from __future__ import annotations
import ctypes as C
import json
import math
from pathlib import Path
import random
import struct
import sys

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'Tests/Terrain'))
from gl_headless import GL,shader_source
from biome_test_support import packed_biome_cells
from field_batch_runner import default_uniforms

SHADERS=ROOT/'ENDURANCE/Ressources/Shader/Atmosphere'
TERRAIN=ROOT/'Asset/Shader/ProceduralTerrain'


def packed_styles(styles):
    result=bytearray()
    for s in styles:
        result+=struct.pack('<24f',*s['horizon'],s['middleElevation'],
            *s['middle'],s['gradientPower'],*s['zenith'],s['strength'],
            *(v*s['fogDensity'] for v in s['fogAlbedo']),s['fogDensity'],
            s['fogBaseHeight'],s['fogHeightScale'],s['fogNoiseAmount'],s['fogNoiseScale'],
            s['exposure'],0,0,0)
    return bytes(result)


def uniforms(gl,program,seed=1337,forced=-1):
    for name,value in default_uniforms(8,uBiomeOverride=forced).items():
        gl.uniform(program,name,*value)
    gl.uniform(program,'uSeed','1ui',seed)


def classify(gl,program,origin,seed,forced):
    side=11
    columns=gl.buffer(48*side*side,1)
    jobs=gl.buffer(struct.pack('<4i8I',*origin,0,seed,3,0,0,0,0,0,0),6)
    uniforms(gl,program,seed,forced)
    gl.uniform(program,'uJobSlots','1ui',1)
    gl.uniform(program,'uColumnStride','1ui',side*side)
    gl.dispatch(program,2,2)
    return gl.read(columns,48*side*side)


def expected_weights(column):
    # Decode the production classifier result. This oracle verifies palette
    # accumulation independently of the game shader, not climate noise itself.
    humidity,temperature=struct.unpack_from('<2f',column,4)
    altitude=struct.unpack_from('<f',column,16)[0]
    ids=struct.unpack_from('<I',column,32)[0]
    grid=[max(0,min(1,v))*4 for v in (humidity,temperature)]
    ratios=[]
    for g in grid:
        t=g-min(math.floor(g),3)
        ratios.append(max(0,min(1,t*t*t*(t*(t*6-15)+10))))
    x,y=ratios
    quad=[(1-x)*(1-y),x*(1-y),(1-x)*y,x*y]
    marine=1-altitude if ((ids>>20)&63)<16 else 0
    result=[0.0]*26
    for i,w in enumerate(quad): result[(ids>>(5*i))&31]+=w*(1-marine)
    result[25]+=marine
    return result


def texture_api(gl):
    gl._fn('glGenTextures',None,C.c_int,C.POINTER(C.c_uint))
    gl._fn('glDeleteTextures',None,C.c_int,C.POINTER(C.c_uint))
    gl._fn('glBindTexture',None,C.c_uint,C.c_uint)
    gl._fn('glActiveTexture',None,C.c_uint)
    gl._fn('glTexStorage2D',None,C.c_uint,C.c_int,C.c_uint,C.c_int,C.c_int)
    gl._fn('glTexParameteri',None,C.c_uint,C.c_uint,C.c_int)
    gl._fn('glBindImageTexture',None,C.c_uint,C.c_uint,C.c_int,C.c_ubyte,C.c_int,C.c_uint,C.c_uint)
    gl._fn('glGetTexImage',None,C.c_uint,C.c_int,C.c_uint,C.c_uint,C.c_void_p)


def atlas_texture(gl,format,size):
    result=C.c_uint()
    gl.glGenTextures(1,C.byref(result)); gl.glBindTexture(0x0DE1,result.value)
    gl.glTexStorage2D(0x0DE1,1,format,size,size)
    for setting,value in [(0x2801,0x2601),(0x2800,0x2601),(0x2802,0x812F),(0x2803,0x812F)]:
        gl.glTexParameteri(0x0DE1,setting,value)
    return result.value


def read_texture(gl,texture,size):
    result=(C.c_float*(size*size*4))()
    gl.glBindTexture(0x0DE1,texture)
    gl.glGetTexImage(0x0DE1,0,0x1908,0x1406,result)
    return list(result)


def run():
    styles=json.loads((ROOT/'ENDURANCE/Ressources/Atmosphere/biome_palettes.json').read_text())['palettes']
    assert len(styles)==26
    with GL() as gl:
        print('OpenGL:',gl.version,'/',gl.renderer)
        mapping=gl.buffer(packed_biome_cells(),7)
        style_buffer=gl.buffer(packed_styles(styles),12)
        result=gl.buffer(64,11)
        palette=gl.compute(SHADERS/'palette.C.glsl')
        classifier=gl.compute(TERRAIN/'field_batch_classify.C.glsl')
        for biome,s in enumerate(styles):
            uniforms(gl,palette,forced=biome)
            gl.uniform(palette,'uPaletteCameraXZ','2f',-754.5,12864.25)
            gl.dispatch(palette,1)
            actual=struct.unpack('<16f',gl.read(result,64))
            expected=[*s['horizon'],s['middleElevation'],*s['middle'],s['gradientPower'],
                      *s['zenith'],s['strength'],s['exposure']]
            assert max(abs(a-b) for a,b in zip(actual,expected))<1e-6,(biome,actual,expected)
        initial_horizon,initial_exposure=styles[0]['horizon'],styles[0]['exposure']
        styles[0]['horizon']=[3.2,1.7,0.4]; styles[0]['exposure']=2.5
        gl.write(style_buffer,packed_styles(styles))
        uniforms(gl,palette,forced=0); gl.dispatch(palette,1)
        hdr=struct.unpack('<16f',gl.read(result,64))
        assert abs(hdr[0]-3.2)<1e-6 and abs(hdr[1]-1.7)<1e-6 and hdr[12]==2.5
        styles[0]['horizon']=initial_horizon; styles[0]['exposure']=initial_exposure
        gl.write(style_buffer,packed_styles(styles))
        print('PASS all 26 forced palettes, unclipped HDR colors/exposure and gradient controls')

        rng=random.Random(5213)
        visited=set()
        for seed in [0,1337,0xffffffff]:
            for _ in range(10):
                x,z=rng.randrange(-80000,80000),rng.randrange(-80000,80000)
                raw=classify(gl,classifier,(x,0,z),seed,-1)
                # Pixel (1,1) samples exactly the XZ origin (halo starts at -1).
                weights=expected_weights(raw[(1+11)*48:(1+11)*48+48])
                assert abs(sum(weights)-1)<2e-5
                visited.update(i for i,w in enumerate(weights) if w>0.001)
                uniforms(gl,palette,seed=seed)
                gl.uniform(palette,'uPaletteCameraXZ','2f',x,z)
                gl.bind(result,11); gl.bind(style_buffer,12)
                gl.dispatch(palette,1)
                actual=struct.unpack('<16f',gl.read(result,64))
                expected=[sum(weights[b]*styles[b][key][axis] for b in range(26))
                          for key in ['horizon','middle','zenith'] for axis in range(3)]
                rgb=[actual[i] for i in [0,1,2,4,5,6,8,9,10]]
                assert max(abs(a-b) for a,b in zip(rgb,expected))<3e-5,(seed,x,z,rgb,expected)
                assert abs(actual[15]-weights[25])<3e-5
        print('PASS 30 seeded world points match terrain classifier weights; sampled biome IDs:',sorted(visited))

        # Changing altitude does not enter palette selection: the API/shader
        # accepts only camera XZ. The world medium independently uses Y.
        texture_api(gl)
        fog=gl.compute(SHADERS/'fog_map.C.glsl')
        size=32
        medium=atlas_texture(gl,0x881A,size) # RGBA16F
        shape=atlas_texture(gl,0x8814,size)  # RGBA32F
        hook='#version 450 core\n'+shader_source(SHADERS/'game_fog.glsl')+'''
layout(local_size_x=1) in;
layout(std430,binding=0) buffer Probe { vec4 result; };
uniform vec3 probePosition;
void main(){result=geGameFog(probePosition);}
'''
        probe=gl.compile([(0x91B9,hook)],'world fog hook')
        probe_result=gl.buffer(16,0)
        placement=(-40960.,-40960.,81920.,float(size))
        gl.uniform(fog,'uGameFogOriginSize','4f',*placement)
        gl.uniform(probe,'uGameFogOriginSize','4f',*placement)
        gl.uniform(probe,'uGameFogAtlasValid','1i',1)
        gl.uniform(probe,'uGameFogSeed','1ui',1337)

        def generate(forced,placement=placement):
            uniforms(gl,fog,forced=forced)
            gl.uniform(fog,'uGameFogOriginSize','4f',*placement)
            gl.bind(mapping,7); gl.bind(style_buffer,12)
            gl.glBindImageTexture(0,medium,0,False,0,0x88B9,0x881A)
            gl.glBindImageTexture(1,shape,0,False,0,0x88B9,0x8814)
            gl.dispatch(fog,4,4)
            return read_texture(gl,medium,size)

        def sample(position,wind_offset=(0.,0.,0.)):
            gl.glActiveTexture(0x84C0+6); gl.glBindTexture(0x0DE1,medium)
            gl.glActiveTexture(0x84C0+7); gl.glBindTexture(0x0DE1,shape)
            gl.uniform(probe,'probePosition','3f',*position)
            # This is the actual game hook contract; there is no synthetic
            # engine fog mode or test-only branch in the production shader.
            gl.uniform(probe,'uGameFogWindOffset','3f',*wind_offset)
            gl.dispatch(probe,1)
            return struct.unpack('<4f',gl.read(probe_result,16))

        dry=generate(15)
        assert max(abs(v) for v in dry)==0
        assert sample((0,20,0))==(0,0,0,0)
        assert sample((0,20,0),(240,50,-80))==(0,0,0,0)
        styles[14]['fogNoiseAmount']=0
        gl.write(style_buffer,packed_styles(styles))
        wet=generate(14)
        near=sample((0,60,0)); far=sample((20000,60,-5000)); high=sample((0,1060,0))
        assert near[3]>0 and max(abs(a-b) for a,b in zip(near,far))<1e-10
        assert 0<high[3]<near[3]*0.001
        assert all(0<=v<=near[3]+1e-10 for v in near[:3])
        # Even vertical wind moves only structure: the smooth world's height
        # profile and climate do not translate with the animated domain.
        assert sample((0,60,0),(240,50,-80))==near
        assert sample((0,1060,0),(240,50,-80))==high
        styles[14]['fogNoiseAmount']=.85
        gl.write(style_buffer,packed_styles(styles)); generate(14)
        assert sample((0,60,0))!=sample((450,60,450))
        assert sample((1000000,60,0))==(0,0,0,0)
        stationary=(125.,60.,-175.)
        start=sample(stationary)
        displacement=(240.,0.,-80.)
        later=sample(stationary,displacement)
        assert abs(later[3]-start[3])>1e-7,'Structured fog failed to evolve at a stationary world position'
        advected=sample(tuple(p+d for p,d in zip(stationary,displacement)),displacement)
        assert max(abs(a-b) for a,b in zip(start,advected))<1e-9,'Noise features did not follow wind velocity'
        assert sample(stationary,displacement)==later,'Fixed displacement must keep the medium stationary'
        print('PASS real game fog wind: stationary animation, advected structure, smooth/absent and height profiles unchanged')
        original=generate(-1)
        assert max(original[3::4])>min(original[3::4])+0.00005
        original_shape=read_texture(gl,shape,size)
        # The wind domain may travel outside the atlas. It must not move the
        # climate lookup or import wet media into an originally dry biome.
        dry_indices=[i for i,a in enumerate(original[3::4]) if a==0]
        assert dry_indices,'Mixed-climate fixture requires at least one dry node'
        dry_index=dry_indices[len(dry_indices)//2]
        dry_position=(placement[0]+((dry_index%size)+.5)*placement[2]/size,
                      60.,placement[1]+((dry_index//size)+.5)*placement[2]/size)
        assert sample(dry_position,(100000.,250.,-100000.))==(0,0,0,0)
        assert read_texture(gl,medium,size)==original
        assert read_texture(gl,shape,size)==original_shape
        # One atlas shift moves 2 texels at this resolution. The same world
        # nodes must preserve their media when recentered, avoiding swimming.
        shifted=generate(-1,(-35840.,-40960.,81920.,float(size)))
        for y in range(size):
            for x in range(size-2):
                a=original[(x+2+y*size)*4:(x+3+y*size)*4]
                b=shifted[(x+y*size)*4:(x+1+y*size)*4]
                assert max(abs(u-v) for u,v in zip(a,b))<1e-7
        print('PASS absent/smooth/structured world fog, altitude attenuation, neighbouring media, stable atlas recentering')
        gl.glDeleteTextures(2,(C.c_uint*2)(medium,shape))
        gl.check('game atmosphere tests')

if __name__=='__main__':
    run()
