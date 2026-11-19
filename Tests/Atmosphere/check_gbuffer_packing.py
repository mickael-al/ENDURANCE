#!/usr/bin/env python3
"""GPU roundtrip compact depth at real pixel centres and projection extremes.

CPU references use double arithmetic and the exact FP32 depth/matrix supplied
to GL. They do not call or translate the GLSL helper. Odd viewports and pixels
next to the centre expose cancellation hidden by even, small test buffers.
"""
from __future__ import annotations
import ctypes as C
import math
import re
import struct
from gl_support import AtmosphereGL, ROOT, RG32F, RGBA32F, FULLSCREEN_VERTEX, shader_source
from check_position_precision import column_major, ortho


def f32(value):
    return struct.unpack('<f', struct.pack('<f', value))[0]


def perspective(fov, aspect, offset=(0.,0.)):
    f=1/math.tan(math.radians(fov)/2)
    return ((f/aspect,0.,offset[0],0.),(0.,f,offset[1],0.),
            (0.,0.,-1.000004,-.2000004),(0.,0.,-1.,0.))



def check_clear(gl):
    source=(ROOT/'ANTAGONIZ/Source/Rendering/RenderingEngine.cpp').read_text()
    initializer=re.search(r'const GLfloat clearPosition\[4\]\s*=\s*\{([^}]+)\}',source)
    assert initializer and 'glClearBufferfv(GL_COLOR, 0, clearPosition)' in source
    terms=[v.strip() for v in initializer.group(1).split(',')]
    clear=dict(zip(('clear.x','clear.y','clear.z','clear.w'),(.2,.4,.6,1.)))
    values=[clear[term] if term in clear else float(term.rstrip('f')) for term in terms]
    assert values==[.6,1.,0.,0.],('Compact clear must preserve formerZ/tag',values)
    gl._fn('glClearBufferfv',None,C.c_uint,C.c_int,C.POINTER(C.c_float))
    texture=gl.texture((3,5),internal=RG32F,linear=False)
    framebuffer=C.c_uint();gl.glGenFramebuffers(1,C.byref(framebuffer));gl.framebuffers.append(framebuffer.value)
    gl.glBindFramebuffer(0x8D40,framebuffer.value)
    gl.glFramebufferTexture2D(0x8D40,0x8CE0,0x0DE1,texture,0)
    gl.glDrawBuffers(1,(C.c_uint*1)(0x8CE0))
    assert gl.glCheckFramebufferStatus(0x8D40)==0x8CD5
    gl.glClearBufferfv(0x1800,0,(C.c_float*4)(*values))
    gl.glBindFramebuffer(0x8D40,0)
    assert gl.texels(texture,(3,5),channels=2)==(f32(.6),1.)*15
    print('PASS production non-black framebuffer clear preserves depth/emission metadata in RG32F',flush=True)


def main():
    source='''#version 450 core
//__HELPER__
layout(binding=0) uniform sampler2D positions;
layout(std430,binding=0) readonly buffer Samples { ivec2 pixels[]; };
uniform mat4 projection;
layout(location=0) out vec4 result;
void main() { result=geGBufferReadPosition(positions,pixels[int(gl_FragCoord.x)],projection); }
'''.replace('//__HELPER__',shader_source(ROOT/'Asset/Shader/Geometry/gbuffer_position.glsl'))
    with AtmosphereGL() as gl:
        gl._fn('glClearTexImage',None,C.c_uint,C.c_int,C.c_uint,C.c_uint,C.c_void_p)
        program=gl.compile([(0x8B31,FULLSCREEN_VERTEX),(0x8B30,source)],'real packed GBuffer pixel reconstruction')
        max_error=0.; max_center_error=0.; cases=0
        for size in ((63,37),(1919,1079),(3841,2159)):
            w,h=size
            pixels=[(x,y) for y in (0,h//2-1,h//2,h//2+1,h-1)
                    for x in (0,w//2-1,w//2,w//2+1,w-1)]
            pixels += [(-1,-1),(w,h),(w+1,-1),(-w-1,h+1)]
            gl.buffer(b''.join(struct.pack('<2i',*p) for p in pixels),0)
            texture=gl.texture(size,internal=RG32F,linear=False)
            output=gl.texture((len(pixels),1),internal=RGBA32F,linear=False)
            projections=(('perspective',perspective(75,w/h)),
                         ('wide',perspective(120,w/h)),
                         ('offcentre',perspective(75,w/h,(.137,-.21))),
                         ('orthographic',ortho(-10000,11000,17000,-18000,.1,100000)))
            for distance in (.1,1.,100.,1000.,10000.,40000.):
                z=f32(-distance); tag=-1048579.
                gl.glClearTexImage(texture,0,0x8227,0x1406,(C.c_float*2)(z,tag))
                for label,raw in projections:
                    projection=tuple(tuple(f32(v) for v in row) for row in raw)
                    gl.sampler(0,texture)
                    gl.matrix(program,'projection',column_major(projection))
                    gl.fullscreen(program,len(pixels),1,[output])
                    actual=gl.texels(output,(len(pixels),1))
                    for i,(raw_x,raw_y) in enumerate(pixels):
                        # GL_REPEAT applies to both stored depth and the
                        # reconstructed pixel ray, including helper invocations.
                        x,y=raw_x%w,raw_y%h
                        clip_w=projection[3][2]*z+projection[3][3]
                        ndc=((2*x+1-w)/w,(2*y+1-h)/h)
                        expected=tuple((ndc[a]*clip_w-projection[a][2]*z-projection[a][3])/projection[a][a]
                                       for a in (0,1))+(z,tag)
                        values=actual[4*i:4*i+4]
                        assert values[2:]==(z,tag),('FP32 depth/tag changed',size,label,distance,values)
                        assert all(math.isfinite(v) for v in values)
                        error=max(abs(a-b) for a,b in zip(values[:3],expected[:3]))
                        max_error=max(max_error,error)
                        # Account for the arithmetic's actual intermediates;
                        # no half precision or nonlinear-depth error is allowed.
                        for axis in (0,1):
                            magnitude=(abs(ndc[axis]*clip_w)+abs(projection[axis][2]*z)+abs(projection[axis][3]))/abs(projection[axis][axis])
                            bound=4*2**-23*magnitude+1e-10
                            assert abs(values[axis]-expected[axis])<=bound,(label,size,distance,(x,y),axis,values,expected,bound)
                        if label in ('perspective','wide') and abs(x-w//2)<=1 and abs(y-h//2)<=1:
                            max_center_error=max(max_center_error,error)
                            # This is the existing receiver-error magnitude in
                            # view space. Exact integer NDC numerators prevent
                            # division/subtraction noise from exceeding it.
                            for axis in (0,1):
                                assert abs(values[axis]-expected[axis]) <= 4*2**-23*abs(expected[axis])+1e-10
                        cases+=1
            gl.glDeleteTextures(1,C.byref(C.c_uint(texture)));gl.textures.remove(texture)
        print(f'PASS {cases} packed GPU positions: 3 odd viewport sizes through3841x2159, perspective75/120deg, off-centre and reversed-Y orthographic, depth0.1..40000m; depth/tag exact, out-of-viewport repeat preserved; max XYZ error={max_error:.8f}m, centre-neighbour error={max_center_error:.8f}m',flush=True)
        check_clear(gl)
        gl.check('packed GBuffer reconstruction')

if __name__=='__main__': main()
