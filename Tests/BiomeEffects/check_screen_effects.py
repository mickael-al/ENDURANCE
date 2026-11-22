#!/usr/bin/env python3
"""Run the production environment screen pass, its C++ owner and real GLSL.

The fixture replaces only editor/window services. No shader calculation or
pipeline choice is duplicated. Readbacks/queries are test instrumentation only.
"""
from __future__ import annotations
import argparse
import ctypes as C
import math
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'Tests/Atmosphere'))
from gl_support import AtmosphereGL, RGBA32F, R32F, TEXTURE_2D
sys.path.insert(0,str(ROOT/'Tests/Rendering'))
from check_bloom import SERVICES as BLOOM_SERVICES

SERVICES=dict(BLOOM_SERVICES)
SERVICES['Camera.hpp']='''#pragma once
#include <glm/glm.hpp>
namespace Ge { class Camera { public: glm::mat4 projection{0}; glm::mat4 getProjectionMatrix() const {return projection;} }; }
'''
SERVICES['GraphicsDataMisc.hpp']='''#pragma once
#include "Camera.hpp"
struct GraphicsDataMisc { unsigned str_width=128,str_height=96; Ge::Camera* current_camera=nullptr; };
'''
SERVICES['Engine.hpp']='''#pragma once
#include "PointeurClass.hpp"
namespace Ge { class Engine { public: static inline ptrClass pointers;
static const ptrClass* getPtrClassAddr(){return &pointers;}
}; }
'''
SERVICES['GraphiquePipeline.hpp']=SERVICES['GraphiquePipeline.hpp'].replace(
    'unsigned getProgram() const { return program; }',
    'unsigned getProgram() const { return program; } bool isValid() const {return program!=0;}')
SERVICES['GraphiquePipelineManager.hpp']=SERVICES['GraphiquePipelineManager.hpp'].replace('error,"Bloom test",cache','error,{},cache')

def build(library,fast_math=False):
    with tempfile.TemporaryDirectory(prefix='endurance-env-screen-services-') as temporary:
        directory=Path(temporary)
        for name,source in SERVICES.items():(directory/name).write_text(source)
        command=[os.environ.get('CXX','c++'),'-std=c++17','-O2','-fPIC','-shared','-DGLEW_NO_GLU','-pthread','-fvisibility=hidden']
        if fast_math:command.append('-ffast-math')
        for path in (directory,ROOT/'ANTAGONIZ/Header/Rendering/Shader',ROOT/'ANTAGONIZ/Header/Interface',
                     ROOT/'ANTAGONIZ/Header/Struct',ROOT/'ThirdParty',ROOT/'ThirdParty/glm'):
            command+=['-I',str(path)]
        for source in ('Tests/BiomeEffects/screen_runtime.cpp',
                       'ANTAGONIZ/Source/Rendering/Shader/EnvironmentScreenEffects.cpp',
                       'ANTAGONIZ/Source/Rendering/Shader/ShaderCompilation.cpp',
                       'ANTAGONIZ/Source/Rendering/Shader/ShaderBinaryCache.cpp'):
            command.append(str(ROOT/source))
        command+=['-l:libGLEW.so.2.2','-l:libGL.so.1','-l:libEGL.so.1','-ldl','-Wl,-z,defs','-o',str(library)]
        subprocess.run(command,check=True)

class Runtime:
    def __init__(self,path):
        self.api=C.CDLL(str(path))
        for name,ret,args in (
            ('screen_create',C.c_void_p,[C.c_char_p]),
            ('screen_destroy',C.c_uint,[C.c_void_p]),
            ('screen_program',C.c_uint,[C.c_void_p,C.c_uint]),
            ('screen_program_count',C.c_uint,[C.c_void_p]),
            ('screen_requested',C.c_int,[C.c_void_p]),
            ('screen_state',None,[C.c_void_p,C.POINTER(C.c_float)]),
            ('screen_draw',None,[C.c_void_p,C.c_uint,C.c_uint,C.c_uint,C.c_uint,C.c_uint])):
            function=getattr(self.api,name);function.restype=ret;function.argtypes=args
        self.handle=self.api.screen_create(str(ROOT).encode())
        assert self.handle,'EnvironmentScreenEffects initialization failed'
    def state(self,values=(0,0,0,0,0,0),time=2.3,tint=(.72,.64,.5),scale=1.,speed=1.):
        data=(*values,*tint,scale,speed,time)
        self.api.screen_state(self.handle,(C.c_float*12)(*data))
    def close(self):assert self.api.screen_destroy(self.handle)==0,'Screen program release leaked programs'


def check(gl,runtime):
    assert runtime.api.screen_program_count(runtime.handle)==16
    for mask in range(16):
        program=runtime.api.screen_program(runtime.handle,mask)
        assert program
        assert (gl.glGetUniformLocation(program,b'uDepth')>=0)==bool(mask&1),'Inactive heat still reads depth'
    gl._fn('glGenQueries',None,C.c_int,C.POINTER(C.c_uint))
    gl._fn('glBeginQuery',None,C.c_uint,C.c_uint)
    gl._fn('glEndQuery',None,C.c_uint)
    gl._fn('glGetQueryObjectuiv',None,C.c_uint,C.c_uint,C.POINTER(C.c_uint))
    gl._fn('glDeleteQueries',None,C.c_int,C.POINTER(C.c_uint))
    frames=0
    for width,height in ((128,96),(37,21),(2,3)):
        size=(width,height)
        source=gl.texture(size,internal=RGBA32F,linear=True,value=(.2,.3,.4,1.))
        output=gl.texture(size,internal=RGBA32F,value=(.04,.05,.06,1.))
        depth=gl.texture(size,internal=R32F,linear=False,value=.999)
        fbo=C.c_uint();gl.glGenFramebuffers(1,C.byref(fbo));gl.framebuffers.append(fbo.value)
        gl.glBindFramebuffer(0x8D40,fbo.value)
        gl.glFramebufferTexture2D(0x8D40,0x8CE0,TEXTURE_2D,output,0)
        gl.glDrawBuffers(1,(C.c_uint*1)(0x8CE0))
        assert gl.glCheckFramebufferStatus(0x8D40)==0x8CD5
        query=C.c_uint();gl.glGenQueries(1,C.byref(query))
        def render(strengths,time=2.3,**parameters):
            runtime.state(strengths,time,**parameters)
            gl.glBeginQuery(0x8C87,query.value) # GL_PRIMITIVES_GENERATED
            runtime.api.screen_draw(runtime.handle,width,height,fbo.value,source,depth)
            gl.glEndQuery(0x8C87)
            count=C.c_uint();gl.glGetQueryObjectuiv(query.value,0x8866,C.byref(count))
            assert count.value==(2 if any(v>0 and math.isfinite(v) for v in strengths) else 0)
            values=gl.texels(output,size)
            assert all(math.isfinite(v) and 0<=v<=1 for v in values)
            assert runtime.api.screen_program_count(runtime.handle)==16,'Settings compiled more shaders'
            return values
        # Inactive path neither draws nor touches a previously populated target.
        baseline=gl.texels(output,size)
        assert render((0,)*6)==baseline
        assert render((float('nan'),-1,float('inf'),0,0,0))==baseline
        for invalid in (float('nan'),float('inf'),float('-inf')):
            for channel in range(6):
                strengths=tuple(invalid if i==channel else 0. for i in range(6))
                assert render(strengths)==baseline,'Invalid strength activated the pass'
        # Match explicit fallbacks, including in the production fast-math build.
        # This exercises all twelve state fields through the actual GPU owner.
        strengths=(.6,.6,.6,.6,.4,.4)
        for parameter,fallback in (('time',0.),('scale',1.),('speed',1.),('tint',(.5,)*3)):
            expected=render(strengths,**{parameter:fallback})
            for invalid in (float('nan'),float('inf'),float('-inf')):
                value=(invalid,)*3 if parameter=='tint' else invalid
                assert render(strengths,**{parameter:value})==expected,(parameter,'Invalid value did not use fallback')
        # All permutations are rendered with nonzero relevant weights, on one VAO.
        for mask in range(16):
            strengths=tuple(.7 if mask&(1<<bit) else 0 for bit in range(4))+(0.25,0.)
            render(strengths);frames+=1
        for channel in range(6):
            strengths=tuple(1. if i==channel else 0. for i in range(6))
            values=render(strengths,time=.08/.19)
            # Uniform source remains uniform under distortion; other channels
            # must produce an observable actual overlay/refraction highlight.
            if channel==0:
                assert max(abs(v-(.2,.3,.4,1.)[i%4]) for i,v in enumerate(values))<1e-6
            elif width>2:
                assert max(abs(v-(.2,.3,.4,1.)[i%4]) for i,v in enumerate(values))>.001,(channel,'has no visible effect')
            frames+=1
        # Heat must preserve sky and near surfaces, while moving distant detail.
        image=[]
        for y in range(height):
            for x in range(width):image.extend((x/max(1,width-1),y/max(1,height-1),(x+y)%2,1.))
        gl.glBindTexture(TEXTURE_2D,source)
        gl.glTexImage2D(TEXTURE_2D,0,RGBA32F,width,height,0,0x1908,0x1406,C.c_char_p(struct.pack(f'<{len(image)}f',*image)))
        for d,unchanged in ((1.,True),(.5,True),(.999,False)):
            gl.glBindTexture(TEXTURE_2D,depth)
            gl.glTexImage2D(TEXTURE_2D,0,R32F,width,height,0,0x1903,0x1406,C.c_char_p(struct.pack('<f',d)*(width*height)))
            values=render((1,0,0,0,0,0))
            error=max(abs(a-b) for a,b in zip(values,image))
            if unchanged:assert error<2e-5,('Heat distorts sky/near surface',d,error)
            elif width>2:assert error>.001,('Heat does not move distant detail',error)
        # A subpixel warp must not leak the bilinear color footprint across a
        # near red object / distant blue background discontinuity.
        split=[];depths=[]
        for y in range(height):
            for x in range(width):
                split.extend((1.,0.,0.,1.) if x<width//2 else (0.,0.,1.,1.))
                depths.append(.5 if x<width//2 else .999)
        gl.glBindTexture(TEXTURE_2D,source)
        gl.glTexImage2D(TEXTURE_2D,0,RGBA32F,width,height,0,0x1908,0x1406,C.c_char_p(struct.pack(f'<{len(split)}f',*split)))
        gl.glBindTexture(TEXTURE_2D,depth)
        gl.glTexImage2D(TEXTURE_2D,0,R32F,width,height,0,0x1903,0x1406,C.c_char_p(struct.pack(f'<{len(depths)}f',*depths)))
        for time in (.4,2.3,8.):
            values=render((1,0,0,0,0,0),time=time)
            assert max(abs(a-b) for a,b in zip(values,split))<2e-5,'Heat leaks across foreground silhouette'
        gl.glDeleteQueries(1,C.byref(query))
    print(f'PASS environment screen: {frames} actual owner/shader renders, 16 fixed variants, zero-work disabled/invalid states, all six channels visible, sky/near-depth and bilinear-silhouette protection, odd/tiny resize and stable program count',flush=True)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--fast-math',action='store_true',help='Compile the real C++ owner with the Release fast-math flag')
    arguments=parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='endurance-env-screen-') as temporary:
        folder=Path(temporary);library=folder/'screen.so';build(library,arguments.fast_math)
        os.environ['ENDURANCE_SHADER_CACHE_DIR']=str(folder/'cache')
        with AtmosphereGL() as gl:
            print(f'OpenGL {gl.version}; {gl.renderer}',flush=True)
            runtime=Runtime(library)
            try:check(gl,runtime);gl.check('environment screen tests')
            finally:runtime.close()

if __name__=='__main__':main()
