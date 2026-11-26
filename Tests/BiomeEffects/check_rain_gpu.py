#!/usr/bin/env python3
"""Measure real rain shader geometry under camera rotation and render its medium.

Transform feedback reads production vertex outputs without replacing shader
math. These are controlled fixtures, not gameplay screenshots or GPU timings.
"""
from __future__ import annotations
import ctypes as C
import math
import struct

from check_particle_gpu import (AtmosphereGL, Fixture, SHADERS, SIZE,
                                TEXTURE_3D, RGBA32F, floats, payload, shader_source)


def dot(a,b): return sum(x*y for x,y in zip(a,b))
def cross(a,b): return (a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0])
def normalized(v):
    length=math.sqrt(dot(v,v))
    return tuple(x/length for x in v)


def set_camera(fixture, eye, target=(0.,0.,-6.), roll=0.):
    forward=normalized(tuple(a-b for a,b in zip(target,eye)))
    guide=(0.,0.,1.) if abs(forward[1])>.999 else (0.,1.,0.)
    right=normalized(cross(forward,guide)); up=cross(right,forward)
    c,s=math.cos(roll),math.sin(roll)
    right,up=(tuple(x*c+y*s for x,y in zip(right,up)),
              tuple(y*c-x*s for x,y in zip(right,up)))
    backward=tuple(-x for x in forward)
    view=(right[0],up[0],backward[0],0.,right[1],up[1],backward[1],0.,
          right[2],up[2],backward[2],0.,-dot(right,eye),-dot(up,eye),-dot(backward,eye),1.)
    inverse=(*right,0.,*up,0.,*backward,0.,*eye,1.)
    f=1/math.tan(math.radians(75)*.5)
    projection=(f,0.,0.,0.,0.,f,0.,0.,0.,0.,-1.0002,-1.,0.,0.,-.20002,0.)
    fixture.gl.write(fixture.camera,floats(*eye,0.,*view,*inverse,*projection))


def capture(gl,fixture,program,buffer,data,*,time=0.,count=1):
    gl.write(fixture.data,data)
    gl.bind(fixture.data,6); gl.bind(fixture.instances,7); gl.bind(fixture.camera,0)
    gl.write(fixture.misc,struct.pack('<i4fi',0,time,1.,.7,75.,0)); gl.bind(fixture.misc,3)
    gl.uniform(program,'u_count','2ui',count,0)
    gl.uniform(program,'uEnvironmentInvResolution','2f',1/SIZE[0],1/SIZE[1])
    gl.glBindBufferBase(0x8C8E,0,buffer) # GL_TRANSFORM_FEEDBACK_BUFFER
    gl.glEnable(0x8C89) # GL_RASTERIZER_DISCARD
    gl.glBeginTransformFeedback(0x0004)
    gl.glDrawArrays(0x0004,0,3)
    gl.glEndTransformFeedback()
    gl.glDisable(0x8C89)
    gl.glMemoryBarrier(gl.ALL_BARRIER_BITS)
    result=struct.unpack('<39f',gl.read(buffer,39*4))
    assert all(math.isfinite(v) for v in result),'Rain geometry contains NaN/Inf'
    gl.check('rain transform feedback')
    return [result[i:i+13] for i in range(0,39,13)]


def geometry_checks(gl,program):
    fixture=Fixture(gl)
    for name,result,args in (
        ('glEnable',None,(C.c_uint,)),
        ('glBeginTransformFeedback',None,(C.c_uint,)),
        ('glEndTransformFeedback',None,()),
    ): gl._fn(name,result,*args)
    vao=C.c_uint();gl.glGenVertexArrays(1,C.byref(vao));gl.arrays.append(vao.value)
    gl.glBindVertexArray(vao.value)
    # Surfaceless EGL still requires a complete draw framebuffer for feedback.
    output=gl.texture((1,1),internal=RGBA32F,value=0.)
    framebuffer=C.c_uint();gl.glGenFramebuffers(1,C.byref(framebuffer));gl.framebuffers.append(framebuffer.value)
    gl.glBindFramebuffer(0x8D40,framebuffer.value)
    gl.glFramebufferTexture2D(0x8D40,0x8CE0,0x0DE1,output,0)
    gl.glDrawBuffers(1,(C.c_uint*1)(0x8CE0))
    assert gl.glCheckFramebufferStatus(0x8D40)==0x8CD5
    buffer=gl.buffer(39*4)
    data=payload(scale=.25,aspect=8.,rotate=True,velocity=(0.,-20.,0.))
    target=(0.,0.,-6.)
    first=None
    for pitch in (-90.,-70.,0.,70.,90.):
        p=math.radians(pitch)
        for yaw in (0.,65.,180.):
            y=math.radians(yaw)
            eye=(6*math.cos(p)*math.sin(y),6*math.sin(p),-6+6*math.cos(p)*math.cos(y))
            unrolled=None
            for roll in (0.,.7,math.pi/2):
                set_camera(fixture,eye,target,roll)
                vertices=capture(gl,fixture,program,buffer,data)
                a,b,c=[v[:3] for v in vertices]
                width=tuple(y-x for x,y in zip(a,b)); height=tuple(y-x for x,y in zip(a,c))
                assert abs(width[1])<1e-6,'Rain card width left the horizontal plane'
                assert height[1]>0 and abs(height[0])+abs(height[2])<1e-6,('Rain tips away from world vertical',pitch,yaw,roll,height)
                center=tuple(x+(dy+dz)*.25 for x,dy,dz in zip(a,width,height))
                assert max(abs(x-y) for x,y in zip(center,target))<2e-6,'Camera movement dragged the rain center'
                assert [v[7:9] for v in vertices]==[(0.,0.),(2.,0.),(0.,2.)],'Rain no longer uses one clipped triangle'
                # Directly above/below, any horizontal card-width axis is
                # valid; the long axis must still remain world vertical.
                if abs(pitch)<89:
                    if unrolled is None: unrolled=[v[:3] for v in vertices]
                    else: assert max(abs(x-y) for a,b in zip(unrolled,vertices) for x,y in zip(a,b[:3]))<1e-6,'Camera roll rotates the physical rain quad'
                if pitch==0. and yaw==0. and roll==0.: first=vertices
    set_camera(fixture,(0.,0.,0.))
    moved_velocity=capture(gl,fixture,program,buffer,
        payload(scale=.25,aspect=8.,rotate=True,velocity=(70.,-3.,-20.)),time=123.)
    assert moved_velocity==first,'Rain orientation depends on screen velocity or sprite rotation'
    anchored=bytearray(data)
    struct.pack_into('<3f',anchored,101*4,-1024.,0.,-6.)
    struct.pack_into('<3f',anchored,31*4,1024.,0.,0.)
    assert capture(gl,fixture,program,buffer,bytes(anchored))==first,'Rain lost immutable world anchor reconstruction'
    zero=capture(gl,fixture,program,buffer,payload(strength=0.),count=1)
    assert all(v[3:7]==(2.,2.,2.,1.) and v[12]==0 for v in zero),'Zero-strength rain leaves live geometry'
    gl.glBindFramebuffer(0x8D40,0)
    gl.glBindBufferBase(0x8C8E,0,0)
    print('PASS rain geometry: fixed world-Y under yaw/pitch/roll, finite polar views, immutable centers, clipped triangle and disabled geometry',flush=True)


def render_checks(gl,program):
    fixture=Fixture(gl)
    data=payload(scale=.20,aspect=8.,velocity=(0.,-20.,0.))
    full=fixture.render(program,data=data)
    assert sum(p[3]>.001 for p in full)>5,'Rain stripe is invisible'
    assert max(p[3] for p in full)<=.701,'Rain alpha escaped its prescribed opacity'
    blocked=fixture.render(program,depth=5.)
    assert all(p==(0.,0.,0.,0.) for p in blocked),'Opaque foreground does not occlude rain'
    soft=fixture.render(program,depth=6.2)
    ratio=sum(p[3] for p in soft)/sum(p[3] for p in full)
    assert .30<ratio<.32,('Rain lost soft ground contact',ratio)
    assert fixture.render(program,depth=0.,sky=True)==full,'Rain is hidden by the sky marker'
    before=fixture.render(program,time=0.)
    assert fixture.render(program,time=17.)==before,'Rain acquired render-only wobble'
    fixture.scattering=gl.texture((1,1,1),target=TEXTURE_3D,internal=RGBA32F,value=(.1,.2,.3,1))
    fixture.transmission=gl.texture((1,1,1),target=TEXTURE_3D,internal=RGBA32F,value=(.5,.6,.7,1))
    fog=fixture.render(program,atmosphere=True)
    for original,actual in zip(full,fog):
        if original[3]>.01:
            expected=tuple(c*t+s for c,t,s in zip(original[:3],(.5,.6,.7),(.1,.2,.3)))
            assert max(abs(a-b) for a,b in zip(actual[:3],expected))<1e-6,'Rain lost atmospheric composition'
            assert actual[3]==original[3],'Atmosphere altered rain coverage'
    print('PASS rain render: soft rain shape, alpha, opaque occlusion, soft contact, sky marker and atmospheric S+T',flush=True)


def main():
    with AtmosphereGL() as gl:
        print(gl.version,gl.renderer,flush=True)
        program=gl.compile([(0x8B31,shader_source(SHADERS/'rain.vs.glsl')),
                            (0x8B30,shader_source(SHADERS/'particle.fs.glsl','#define GE_BIOME_PARTICLE_KIND 2\n'))],
                           'world-vertical rain',feedback=('VS_OUT.worldPosition','gl_Position','VS_OUT.uv','VS_OUT.color'))
        geometry_checks(gl,program)
        render_checks(gl,program)
        gl.check('rain shader checks')


if __name__=='__main__': main()
