#!/usr/bin/env python3
"""Exercise complete cube faces, diffuse/GGX filtering, HDR blend and disk energy."""
from __future__ import annotations

import ctypes as C
import math
import struct

from fixtures import Parameters
from gl_support import AtmosphereGL, ROOT, TEXTURE_CUBE_MAP, RG16F, RG32F, RGBA32F, shader_source
from check_luts import finite, rgb

DIRECTORY=ROOT/"Asset/Shader/Atmosphere"


def cube_values(gl,texture,size,level=0):
    return [value for face in range(6) for value in gl.texels(texture,size,target=TEXTURE_CUBE_MAP,level=level,face=face)]


def check_ibl(gl,programs=None):
    names=("environment.C.glsl","filter.C.glsl","blend.C.glsl","brdf.C.glsl")
    programs=programs or {name:gl.compute(DIRECTORY/name) for name in names}
    size=(9,9)
    levels=4
    source=gl.texture(size,target=TEXTURE_CUBE_MAP,levels=levels,value=(2,4,8,1))
    diffuse=gl.texture(size,target=TEXTURE_CUBE_MAP)
    specular=gl.texture(size,target=TEXTURE_CUBE_MAP,levels=levels)
    filter_=programs["filter.C.glsl"]
    gl.sampler(0,source,TEXTURE_CUBE_MAP)
    gl.uniform(filter_,"geFilterSamples","1i",64)
    gl.uniform(filter_,"geFilterMode","1i",0)
    gl.image(0,diffuse,layered=True)
    gl.dispatch_extent(filter_,*size,6)
    values=cube_values(gl,diffuse,size)
    finite(values,"diffuse cube")
    assert all(abs(v-(2,4,8,1)[i%4])<.001 for i,v in enumerate(values)),"Cosine convolution E/pi did not preserve constant radiance"
    gl.uniform(filter_,"geFilterMode","1i",1)
    for level in range(levels):
        gl.uniform(filter_,"geFilterRoughness","1f",level/(levels-1))
        gl.image(0,specular,level=level,layered=True)
        gl.dispatch_extent(filter_,max(1,size[0]>>level),max(1,size[1]>>level),6)
        values=cube_values(gl,specular,size,level)
        finite(values,f"GGX cube mip {level}")
        assert all(abs(v-(2,4,8,1)[i%4])<.001 for i,v in enumerate(values)),("GGX did not preserve constant HDR radiance",level)
    print("PASS IBL: cosine irradiance E/pi and GGX preserve constant HDR radiance on all six faces and all four mips",flush=True)

    # A finite bright angular feature must spread over rougher reflection
    # lobes. Ordinary source mipmaps are generated first, then GGX convolution
    # is explicitly executed for every roughness level.
    directional=gl.texture(size,target=TEXTURE_CUBE_MAP,levels=levels,value=0)
    gl.glBindTexture(TEXTURE_CUBE_MAP,directional)
    payload=struct.pack("<4f",10,10,10,1)*(size[0]*size[1])
    gl.glTexImage2D(0x8515,0,0x881A,*size,0,0x1908,0x1406,C.c_char_p(payload))
    gl.glGenerateMipmap(TEXTURE_CUBE_MAP)
    gl.sampler(0,directional,TEXTURE_CUBE_MAP)
    peak=[]
    for level in range(levels):
        gl.uniform(filter_,"geFilterRoughness","1f",level/(levels-1))
        gl.image(0,specular,level=level,layered=True)
        gl.dispatch_extent(filter_,max(1,size[0]>>level),max(1,size[1]>>level),6)
        values=cube_values(gl,specular,size,level)
        finite(values,f"directional GGX mip {level}")
        assert all(0<=v<=10.01 for v in rgb(values))
        peak.append(max(rgb(values)))
    assert peak[0]>9.9 and peak[-1]<.8*peak[0],("GGX roughness did not spread the bright feature",peak)
    print("PASS roughness: sharp directional feature spreads through explicit GGX prefiltering",flush=True)

    target=gl.texture(size,target=TEXTURE_CUBE_MAP,levels=levels,value=(10,6,4,1))
    output=gl.texture(size,target=TEXTURE_CUBE_MAP,levels=levels)
    blend=programs["blend.C.glsl"]
    gl.sampler(0,source,TEXTURE_CUBE_MAP)
    gl.sampler(1,target,TEXTURE_CUBE_MAP)
    gl.uniform(blend,"geBlendFactor","1f",.375)
    for level in range(levels):
        gl.uniform(blend,"geBlendLevel","1f",float(level))
        gl.image(0,output,level=level,layered=True)
        gl.dispatch_extent(blend,max(1,size[0]>>level),max(1,size[1]>>level),6)
        values=cube_values(gl,output,size,level)
        assert all(abs(v-(5,4.75,6.5,1)[i%4])<.001 for i,v in enumerate(values)),("Incomplete/incoherent HDR cube blend",level)
    print("PASS environment transition: exact HDR blend on every face and roughness mip",flush=True)

    parameters=Parameters()
    for name in ("Horizon","Middle","Zenith"):
        w=parameters.values[name][3]
        parameters.set(name,1,2,3,w)
    parameters.set("SunDirection",0,1,0,1)
    parameters.set("SunDisk",50,45,40,.25)
    uniform=parameters.upload(gl)
    visible=gl.texture(size,target=TEXTURE_CUBE_MAP)
    lighting=gl.texture(size,target=TEXTURE_CUBE_MAP)
    environment=programs["environment.C.glsl"]
    gl.uniform(environment,"geUseExternalEnvironment","1i",0)
    gl.image(0,visible,layered=True)
    gl.image(1,lighting,layered=True)
    gl.dispatch_extent(environment,*size,6)
    displayed=cube_values(gl,visible,size)
    indirect=cube_values(gl,lighting,size)
    finite(displayed,"visible environment")
    finite(indirect,"IBL source environment")
    assert max(rgb(displayed))>40,"Visible solar disk absent"
    assert all(abs(v-(1,2,3,1)[i%4])<.002 for i,v in enumerate(indirect)),"Direct disk energy leaked into IBL source"
    parameters.set("SunDirection",0,0,0,0)
    parameters.upload(gl,uniform)
    gl.dispatch_extent(environment,*size,6)
    assert cube_values(gl,visible,size)==cube_values(gl,lighting,size),"Unassigned source left a visible disk"
    print("PASS source energy: visible solar disk preserved, excluded from diffuse/GGX input, absent when unassigned",flush=True)

    brdf_size=(17,17)
    brdf=gl.texture(brdf_size,internal=RG16F)
    gl.image(0,brdf,internal=RG16F)
    gl.dispatch_extent(programs["brdf.C.glsl"],*brdf_size)
    values=gl.texels(brdf,brdf_size,channels=2)
    finite(values,"BRDF integration LUT")
    assert all(0<=v<=1.01 for v in values),"BRDF split-sum LUT unbounded"
    assert max(values)>.9 and min(values)<.01,"BRDF LUT appears uninitialized/constant"
    gl.check("complete IBL validation")
    print("PASS BRDF: finite bounded split-sum integration, independent of atmosphere/biome data",flush=True)

    sky_program=gl.compile([(0x8B31,shader_source(DIRECTORY/"sky.vs.glsl")),
                            (0x8B30,shader_source(DIRECTORY/"sky.fs.glsl"))],"HDR sky G-buffer")
    attachments=[gl.texture((3,5),internal=RG32F if i==0 else RGBA32F) for i in range(4)]
    gl.sampler(0,source,TEXTURE_CUBE_MAP)
    gl.fullscreen(sky_program,3,5,attachments)
    color=gl.texels(attachments[2],(3,5))
    marker=gl.texels(attachments[0],(3,5),channels=2)
    assert all(abs(v-(2,4,8,1)[i%4])<1e-5 for i,v in enumerate(color)),"Sky fragment stage clipped/gamma-converted HDR radiance"
    assert all(marker[i]==-2 for i in range(1,len(marker),2)),"Sky classification marker lost"
    parameters.values["Art"][1]=0
    parameters.upload(gl,uniform)
    gl.fullscreen(sky_program,3,5,attachments)
    color=gl.texels(attachments[2],(3,5))
    assert all(abs(v-(2,4,8,1)[i%4])<1e-5 for i,v in enumerate(color)),"G-buffer applied display saturation before atmospheric composition"
    print("PASS sky G-buffer: unclamped linear HDR, explicit sky marker and deferred final display saturation",flush=True)


def main():
    with AtmosphereGL() as gl:
        print(f"OpenGL {gl.version}; renderer: {gl.renderer}",flush=True)
        check_ibl(gl)


if __name__=="__main__":
    main()
