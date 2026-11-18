#!/usr/bin/env python3
"""Validate the shared deferred/transparent composition at each fragment depth."""
import math
import struct

from check_luts import finite
from check_volumes import VolumeFixture
from gl_support import AtmosphereGL,ROOT,TEXTURE_3D,RGBA32F


def check_sampling(gl):
    f=VolumeFixture(gl)
    f.build()
    f.cloud_noise()
    p=f.parameters
    p.set("Control",0,1,0,0)
    p.set("SunDirection",0,0,0,0)
    p.set("MoonDirection",0,0,0,0)
    p.set("Volume",20000,.5,.0001,1000000)
    p.set("Fog",0,0,0,200)
    f.volumes()
    program=gl.compute(ROOT/"Tests/Atmosphere/sample_probe.C.glsl")
    output=gl.buffer(struct.pack("<f",float("nan"))*64,0)
    gl.sampler(9,f.baseline_s)
    gl.sampler(10,f.baseline_t)
    gl.sampler(11,f.multiscatter)
    gl.sampler(12,f.scattering,TEXTURE_3D)
    gl.sampler(13,f.transmission,TEXTURE_3D)
    gl.sampler(14,f.cloud_shadow)
    gl.sampler(15,f.transmittance)
    gl.uniform(program,"uAtmosphereActive","1i",1)
    gl.dispatch(program,1)
    values=struct.unpack("<64f",gl.read(output,256))
    finite(values,"shared atmosphere composition")
    for i in range(9):
        distance=20000*(i/8)**2
        for channel,surface in enumerate((4,2,1)):
            expected=surface*math.exp(-.0001*distance)
            assert abs(values[i*4+channel]-expected)<.004,("Per-fragment atmospheric depth",i,channel,values[i*4+channel],expected)
    assert values[9*4:9*4+4]==values[8*4:8*4+4],"Far-distance clamp sampled beyond allocated volume"
    assert all(abs(a-b)<.004 for a,b in zip(values[10*4:10*4+3],values[8*4:8*4+3])),"Sky fog composition double-counted air or ignored local fog"
    assert values[4]>values[8*4],"Front transparent fragment received opaque-background extinction"
    gl.uniform(program,"uAtmosphereActive","1i",0)
    gl.dispatch(program,1)
    disabled=struct.unpack("<64f",gl.read(output,256))
    for i in range(11):
        assert disabled[i*4:i*4+4]==(4,2,1,1),"Disabled atmosphere changed material/sky radiance"
    print("PASS composition: own fragment distance, near zero identity, far clamp, sky local-fog composition and disabled identity",flush=True)

    # Deliberately coloured S and spectral T separate display saturation from
    # material reflectance. The sky must be desaturated after its full local
    # composition, including the signed physical-baseline subtraction.
    source=(.8,.1,.3)
    transmission=(.9,.5,.25)
    baseline_source=(.2,.4,.1)
    baseline_transmission=(.9,.8,.7)
    constants=[gl.texture((2,2,2),target=TEXTURE_3D,internal=RGBA32F,value=source+(1,)),
               gl.texture((2,2,2),target=TEXTURE_3D,internal=RGBA32F,value=transmission+(1,)),
               gl.texture((2,2),internal=RGBA32F,value=baseline_source+(1,)),
               gl.texture((2,2),internal=RGBA32F,value=baseline_transmission+(1,))]
    gl.sampler(12,constants[0],TEXTURE_3D)
    gl.sampler(13,constants[1],TEXTURE_3D)
    gl.sampler(9,constants[2])
    gl.sampler(10,constants[3])
    p.values["Art"][1]=0
    f.upload()
    gl.uniform(program,"uAtmosphereActive","1i",1)
    gl.dispatch(program,1)
    gray=struct.unpack("<64f",gl.read(output,256))
    weights=(.2126,.7152,.0722)
    source_luminance=sum(a*b for a,b in zip(source,weights))
    expected_material=[surface*t+source_luminance for surface,t in zip((4,2,1),transmission)]
    assert all(abs(a-b)<1e-5 for a,b in zip(gray[4:7],expected_material)),"Sky saturation altered material reflectance/transmission instead of only S"
    raw_sky=[s+(surface-b)*min(t/bt,1) for s,surface,b,t,bt in
             zip(source,(4,2,1),baseline_source,transmission,baseline_transmission)]
    sky_luminance=sum(a*b for a,b in zip(raw_sky,weights))
    assert all(abs(v-sky_luminance)<1e-5 for v in gray[40:43]),"Sky saturation was applied before local fog/baseline composition"
    p.set("Control",0,0,0,0)
    f.upload()
    gl.dispatch(program,1)
    clear=struct.unpack("<64f",gl.read(output,256))
    clear_luminance=sum(a*b for a,b in zip((4,2,1),weights))
    assert all(abs(v-clear_luminance)<1e-5 for v in clear[40:43]),"Cloudless/fogless sky bypass skipped visible saturation"
    print("PASS visible saturation: final sky including local media is gray at zero saturation; material reflectance and spectral transmission remain unchanged",flush=True)


def main():
    with AtmosphereGL() as gl:
        print(f"OpenGL {gl.version}; renderer: {gl.renderer}",flush=True)
        check_sampling(gl)


if __name__=="__main__":
    main()
