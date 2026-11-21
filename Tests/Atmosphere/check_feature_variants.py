#!/usr/bin/env python3
"""Compare specialized shader features with the generic uniform reference."""
import ctypes as C
import math
import struct
from check_volumes import VolumeFixture
from check_luts import DIRECTORY
from check_ibl import cube_values
from gl_support import AtmosphereGL, TEXTURE_CUBE_MAP


def defines(flags):
    return "\n".join(f"#define {name} {(flags>>bit)&1}" for bit,name in enumerate(
        ("GE_ATM_PHYSICAL","GE_ATM_LOCAL_FOG","GE_ATM_CLOUDS","GE_ATM_TERRAIN_SHADOWS")))


def compare(a,b,label):
    assert len(a)==len(b)
    maximum=0
    for x,y in zip(a,b):
        assert math.isfinite(x) and math.isfinite(y),label
        error=abs(x-y); maximum=max(maximum,error)
        assert error<=max(1e-5,abs(x)*.001), (label,x,y,error)
    return maximum


def main():
    with AtmosphereGL() as gl:
        print(f"OpenGL {gl.version}; renderer: {gl.renderer}",flush=True)
        fixture=VolumeFixture(gl); fixture.build(); fixture.cloud_noise()
        baseline=fixture.programs.copy(); cache={}; maximum=0
        masks={"aerial.C.glsl":13,"cloud_volume.C.glsl":13,"volume.C.glsl":15,"cloud_shadow.C.glsl":4}
        fixture.parameters.set("Volume",20000,.5,.00002,500)
        fixture.parameters.set("Shadow",0,-1,1,0)
        for flags in range(16):
            fixture.parameters.set("Control",*((flags>>bit)&1 for bit in range(4)))
            fixture.programs=baseline.copy()
            expected=fixture.volumes(blocked=True)
            for name,mask in masks.items():
                key=(name,flags&mask)
                if key not in cache: cache[key]=gl.compute(DIRECTORY/name,defines(flags&mask))
                fixture.programs[name]=cache[key]
            actual=fixture.volumes(blocked=True)
            for a,b in zip(expected,actual): maximum=max(maximum,compare(a,b,f"flags {flags}"))
        # Configured clouds remain compiled when weather density reaches zero.
        # The dynamic layer guard must still prevent sampling absent media.
        fixture.parameters.set("CloudLayer",2000,1500,0,.002)
        fixture.parameters.set("Control",1,1,0,1)
        fixture.programs=baseline.copy(); expected=fixture.volumes(blocked=True)
        for name,mask in masks.items(): fixture.programs[name]=cache[(name,15&mask)]
        actual=fixture.volumes(blocked=True)
        for a,b in zip(expected,actual): maximum=max(maximum,compare(a,b,"empty cloud coverage"))
        print(f"PASS 16 atmosphere feature combinations + dynamic empty clouds: reference versus specialized volumes, max absolute difference {maximum:.9g}",flush=True)

        size=(9,9); levels=4
        source=gl.texture(size,target=TEXTURE_CUBE_MAP,levels=levels,value=(.2,.4,.8,1))
        gl.glBindTexture(TEXTURE_CUBE_MAP,source)
        for face in range(6):
            pixels=[component for y in range(9) for x in range(9) for component in
                    (float(face+1)*(.2+x/4),float(y+1)/3,float((x+y)%3),1)]
            gl.glTexImage2D(0x8515+face,0,0x881A,*size,0,0x1908,0x1406,C.c_char_p(struct.pack(f"<{len(pixels)}f",*pixels)))
        gl.glGenerateMipmap(TEXTURE_CUBE_MAP); gl.sampler(0,source,TEXTURE_CUBE_MAP)
        output=gl.texture(size,target=TEXTURE_CUBE_MAP,levels=levels)
        gl.sampler(0,source,TEXTURE_CUBE_MAP)
        generic=gl.compute(DIRECTORY/"filter.C.glsl"); maximum=0
        for mode in range(2):
            specialized=gl.compute(DIRECTORY/"filter.C.glsl",f"#define GE_FILTER_MODE {mode}")
            assert gl.glGetUniformLocation(specialized,b"geFilterMode")==-1
            for level in range(levels):
                result=[]
                for program in (generic,specialized):
                    gl.uniform(program,"geFilterMode","1i",mode); gl.uniform(program,"geFilterSamples","1i",64)
                    gl.uniform(program,"geFilterRoughness","1f",level/(levels-1))
                    gl.image(0,output,level=level,layered=True)
                    gl.dispatch_extent(program,max(1,9>>level),max(1,9>>level),6)
                    result.append(cube_values(gl,output,size,level))
                maximum=max(maximum,compare(*result,f"filter {mode}, mip {level}"))
        gl.check("feature variant equivalence")
        print(f"PASS diffuse/GGX fixed filter modes: six HDR faces, four mips, max absolute difference {maximum:.9g}",flush=True)

if __name__=="__main__": main()
