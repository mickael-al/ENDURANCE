#!/usr/bin/env python3
"""Paired two-source / exclusive day-night lighting on the production shaders.

This deliberately synchronizes for isolated measurements, never in the game.
Mesa llvmpipe timer queries can report constant 1 ns, so use wall time across
one dispatch + glFinish, explicitly NOT a hardware GPU or full-frame estimate.
"""
from __future__ import annotations
import argparse
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
        # Test-only reference: keep the production integrator, but allow both
        # source branches as before the exclusive day/night selection. This
        # adds no comparison/debug branch to any shipped shader.
        def dual_source(name):
            source=shader_source(DIRECTORY/name)
            solar='return geAtmSunAboveHorizon() &&'
            lunar='return !geAtmSunAboveHorizon() && geAtmMoonDirection.w>0.5\n        && geAtmMoonDirection.y>=0.0 &&'
            assert solar in source and lunar in source
            return source.replace(solar,'return geAtmSunDirection.w>0.5 &&').replace(lunar,'return geAtmMoonDirection.w>0.5 &&')
        jobs={
            'visible_clouds.C.glsl':((96,54),(8,8,1),0x881A,0x0DE1),
            'cloud_volume.C.glsl':((64,36,32),(4,4,4),RGBA32F,TEXTURE_3D),
            'cloud_shadow.C.glsl':((192,192),(8,8,1),RG16F,0x0DE1),
        }
        programs={name:[gl.compile([(0x91B9,dual_source(name))],'dual-light reference '+name),
                        gl.compute(DIRECTORY/name)] for name in jobs}
        textures={name:gl.texture(size,target=target,internal=fmt)
                  for name,(size,group,fmt,target) in jobs.items()}
        report={'renderer':gl.renderer,'measurement':'Synchronous dispatch wall milliseconds; not a GTX 1660 or full-frame prediction.',
                'repeats':args.repeats,'columns':['two_lights_reference','one_selected_light'],
                'visible_extent':[96,54],'volume_extent':[64,36,32],'shadow_extent':[192,192],
                'cloud_steps':40,'light_steps':8,'cases':{}}
        for label,elevation in [('day',math.sqrt(.5)),('night',-math.sqrt(.5))]:
            p.set('SunDirection',.5,elevation,.5,1)
            p.set('MoonDirection',-.64085638,.42261826,-.64085638,1)
            f.upload(); bind(); gl.image(0,f.cloud_shadow,internal=RG16F)
            gl.dispatch_extent(f.programs['cloud_shadow.C.glsl'],*f.shadow_size)
            f.build(False,False,True)
            result={}
            for name,pair in programs.items():
                size,group,fmt,target=jobs[name]; tex=textures[name]
                bind();gl.image(0,tex,internal=fmt,layered=len(size)==3)
                for program in pair: render(program,size,group)
                samples=[[],[]]
                for iteration in range(args.repeats):
                    for i in ((0,1) if iteration%2==0 else (1,0)):
                        samples[i].append(render(pair[i],size,group))
                render(pair[1],size,group)
                current=np.array(gl.texels(tex,size,target=target))
                # Selecting one light must exactly match physically removing
                # the inactive source from the former two-source integrator.
                inactive='MoonDirection' if label=='day' else 'SunDirection'
                p.values[inactive][3]=0;f.upload();bind();render(pair[0],size,group)
                reference=np.array(gl.texels(tex,size,target=target))
                assert np.isfinite(current).all() and np.array_equal(current,reference),name+': selected light differs from single-source reference'
                p.values[inactive][3]=1;f.upload();bind()
                result[name]={'median_ms':[statistics.median(x) for x in samples],
                              'samples_ms':samples,'single_source_max_error':float(np.max(np.abs(current-reference)))}
            report['cases'][label]=result
            print('PASS',label,json.dumps({k:{'median_ms':v['median_ms'],'single_source_max_error':v['single_source_max_error']} for k,v in result.items()}),flush=True)
        gl.check('exclusive lunar lighting comparison')
        if args.output:
            args.output.parent.mkdir(parents=True,exist_ok=True)
            args.output.write_text(json.dumps(report,indent=2)+'\n')
            print(args.output)


if __name__=='__main__': main()
