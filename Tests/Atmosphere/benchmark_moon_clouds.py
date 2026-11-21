#!/usr/bin/env python3
"""Paired exact/cached lunar cloud lighting on the production shaders.

This deliberately synchronizes for isolated measurements, never in the game.
Mesa llvmpipe timer queries can report constant 1 ns, so use wall time across
one dispatch + glFinish, explicitly NOT a hardware GPU or full-frame estimate.
"""
from __future__ import annotations
import argparse
import ctypes as C
import json
import math
from pathlib import Path
import statistics
import time

import numpy as np
from gl_support import AtmosphereGL, ROOT, TEXTURE_3D, TEXTURE_2D_ARRAY, RGBA32F, RG16F, shader_source
from check_volumes import VolumeFixture

DIRECTORY=ROOT/'Asset/Shader/Atmosphere'


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path)
    parser.add_argument('--repeats',type=int,default=9)
    args=parser.parse_args()
    if args.repeats<3: parser.error('At least three paired samples are required')
    with AtmosphereGL() as gl:
        f=VolumeFixture(gl); p=f.parameters
        p.set('Control',1,0,1,0)
        p.set('CloudLayer',2200,1800,.58,.0012)
        p.set('CloudShape',6000,4,.35,.6)
        p.set('Samples',2,8,16,40); p.set('Quality',64,96,32,24)
        p.set('SunDirection',.5,math.sqrt(.5),.5,1); p.set('SunRadiance',12,12,12,0)
        p.set('MoonDirection',-.64085638,.42261826,-.64085638,1)
        p.set('MoonRadiance',.025,.024,.023,1)
        p.set('SunDisk',0,0,0,0); p.set('MoonDisk',0,0,0,0)
        p.set('CloudOffset',0,0,20000,0); p.set('Camera',0,100,0,0)
        f.noise_size=(64,64,64)
        f.noise=gl.texture(f.noise_size,target=TEXTURE_3D,levels=7)
        gl.glBindTexture(TEXTURE_3D,f.noise)
        for axis in (0x2802,0x2803,0x8072): gl.glTexParameteri(TEXTURE_3D,axis,0x2901)
        f.shadow_size=(192,192); f.cloud_shadow=gl.texture(f.shadow_size,internal=RG16F)
        f.build(); f.cloud_noise(); gl.sampler(3,f.noise,TEXTURE_3D); gl.glGenerateMipmap(TEXTURE_3D)
        gl.glFinish()
        def bind():
            for unit,tex in ((0,f.transmittance),(1,f.multiscatter),(2,f.sky),(5,f.cloud_shadow)):
                gl.sampler(unit,tex)
            gl.sampler(3,f.noise,TEXTURE_3D)
            gl.sampler(4,f.lit_shadow,TEXTURE_2D_ARRAY); gl.bind(f.shadow_matrices,5)
        def render(program,size,group):
            gl.glUseProgram(program); gl.glFinish()
            start=time.perf_counter()
            extent=size if len(size)==3 else (*size,1)
            gl.glDispatchCompute(*(math.ceil(n/g) for n,g in zip(extent,group)))
            gl.glMemoryBarrier(gl.ALL_BARRIER_BITS); gl.glFinish()
            return (time.perf_counter()-start)*1000
        programs={}
        for name in ('visible_clouds.C.glsl','cloud_volume.C.glsl'):
            programs[name]=[gl.compute(DIRECTORY/name,define) for define in
                ('#define GE_ATM_EXACT_MOON_CLOUD_LIGHT 1','')]
        visible=gl.texture((96,54))
        volume=gl.texture((64,36,32),target=TEXTURE_3D,internal=RGBA32F)
        report={'renderer':gl.renderer,'measurement':'Synchronous dispatch wall milliseconds on this renderer; not hardware GPU/frame-time prediction.',
                'repeats':args.repeats,'columns':['exact','optimized'],
                'visible_extent':[96,54],'volume_extent':[64,36,32],
                'cloud_steps':40,'light_steps':8,'atlas_extent':20000,'cases':{}}
        cases=[('sun_only',1,0,0),('day_moon',1,1,0),('moon_only',0,1,0),
               ('outside_atlas',1,1,1000000),('strong_moon',1,1,0),('low_sun',1,1,0)]
        for label,sun,moon,camera_x in cases:
            p.set('SunDirection',.5,math.sqrt(.5),.5,sun)
            if label=='low_sun': p.set('SunDirection',1,0,0,1)
            p.values['MoonDirection'][3]=moon
            p.set('MoonRadiance',.025,.024,.023,1)
            if label=='strong_moon': p.set('MoonRadiance',2.5,2.4,2.3,1)
            p.set('Camera',camera_x,100,0,0)
            f.upload(); bind(); gl.image(0,f.cloud_shadow,internal=RG16F)
            gl.dispatch_extent(f.programs['cloud_shadow.C.glsl'],*f.shadow_size)
            f.build(False,False,True)
            result={}
            for name,pair in programs.items():
                size=(96,54) if name.startswith('visible') else (64,36,32)
                tex=visible if len(size)==2 else volume
                group=(8,8,1) if len(size)==2 else (4,4,4)
                bind(); gl.image(0,tex,internal=RGBA32F if len(size)==3 else 0x881A,layered=len(size)==3)
                for program in pair: render(program,size,group)
                samples=[[],[]]
                for iteration in range(args.repeats):
                    for i in ((0,1) if iteration%2==0 else (1,0)):
                        samples[i].append(render(pair[i],size,group))
                images=[]
                for program in pair:
                    render(program,size,group)
                    images.append(np.array(gl.texels(tex,size,target=TEXTURE_3D if len(size)==3 else 0x0DE1)).reshape(-1,4))
                assert np.isfinite(images).all()
                assert np.array_equal(images[0][:,3],images[1][:,3]),'Lunar approximation changed extinction/transmittance'
                error=np.abs(images[0][:,:3]-images[1][:,:3])
                energy=np.mean(np.abs(images[0][:,:3]))
                relative=float(error.mean()/max(energy,1e-12))
                if label!='day_moon':
                    assert np.array_equal(images[0],images[1]),f'{label}: exact fallback changed the output'
                else:
                    assert relative<.002,f'Daylight approximation error too large: {relative}'
                result[name]={'median_ms':[statistics.median(x) for x in samples],'samples_ms':samples,
                    'mean_abs_rgb_error':float(error.mean()),'max_abs_rgb_error':float(error.max()),
                    'mean_relative_error':relative}
            report['cases'][label]=result
            print('PASS',label,json.dumps({k:{'median_ms':v['median_ms'],'relative_error':v['mean_relative_error']} for k,v in result.items()}),flush=True)
        # Separately inspect the actual helper across the finite shadow-map edge.
        probe_source='''#version 450 core
#include "../../Asset/Shader/Atmosphere/cloud_common.glsl"
layout(local_size_x=64) in;
layout(std430,binding=0) readonly buffer Positions {vec4 positions[];};
layout(std430,binding=1) writeonly buffer Results {vec4 results[];};
void main(){uint i=gl_GlobalInvocationID.x;if(i>=positions.length())return;
vec3 p=positions[i].xyz,d=normalize(geAtmMoonDirection.xyz);
results[i]=vec4(geAtmMoonCloudLight(p,d,geAtmSamples.y),geAtmCloudLightRay(p,d,geAtmSamples.y),
geAtmCloudVisibility(p,d,true),geAtmCloudAtlasWeight(geAtmCloudShadowUV(p,d)));}
'''
        # Resolve the include without adding another production/test shader file.
        common=shader_source(DIRECTORY/'cloud_common.glsl')
        program=gl.compile([(0x91B9,probe_source.replace('#include "../../Asset/Shader/Atmosphere/cloud_common.glsl"',common))],'lunar atlas boundary probe')
        constant=gl.texture((2,2,2),target=TEXTURE_3D,value=(1,1,1,1))
        shadow=gl.texture((2,2),internal=RG16F,value=(.5,.4))
        positions=np.array([(x,3000,0,0) for x in np.linspace(-30000,30000,601)],np.float32)
        inp=gl.buffer(positions.tobytes(),0); out=gl.buffer(positions.nbytes,1)
        p.set('SunDirection',.5,math.sqrt(.5),.5,1);p.set('MoonRadiance',.025,.024,.023,1)
        p.set('CloudLayer',2200,1800,1,.0012);p.set('CloudWeather',48000,0,1,0);f.upload()
        gl.sampler(3,constant,TEXTURE_3D);gl.sampler(5,shadow)
        gl.dispatch_extent(program,len(positions))
        rows=np.frombuffer(gl.read(out,positions.nbytes),np.float32).reshape(-1,4)
        assert np.isfinite(rows).all() and ((rows>=0)&(rows<=1)).all()
        assert (rows[:,3]==0).any() and (rows[:,3]==1).any() and ((rows[:,3]>0)&(rows[:,3]<1)).any()
        assert np.array_equal(rows[rows[:,3]==0,0],rows[rows[:,3]==0,1]),'Outside-map rays are not exact'
        assert np.array_equal(rows[rows[:,3]==1,0],rows[rows[:,3]==1,2]),'Interior does not use cached transmission'
        maximum_jump=float(np.max(np.abs(np.diff(rows[:,0]))))
        assert maximum_jump<.04,('Discontinuous atlas boundary',maximum_jump)
        report['boundary']={'samples':len(positions),'spacing_m':100,'maximum_transmittance_jump':maximum_jump,'outside_exact':True}
        print('PASS finite-atlas boundary: exact outside, cached inside, smooth transition',flush=True)
        gl.check('lunar cloud optimization comparison')
        if args.output:
            args.output.parent.mkdir(parents=True,exist_ok=True)
            args.output.write_text(json.dumps(report,indent=2)+'\n')
            print(args.output)


if __name__=='__main__': main()
