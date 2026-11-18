#!/usr/bin/env python3
"""Execute production LUT shaders and validate their physical dependencies."""
from __future__ import annotations

import math

from fixtures import Parameters
from gl_support import AtmosphereGL, ROOT

DIRECTORY=ROOT/"Asset/Shader/Atmosphere"


def rgb(values):
    return [v for i,v in enumerate(values) if i%4!=3]


def finite(values,label):
    assert all(math.isfinite(v) for v in values),f"{label}: NaN, infinity or unwritten texel"


class PhysicalFixture:
    def __init__(self,gl,programs=None):
        self.gl=gl
        self.parameters=Parameters()
        self.uniforms=self.parameters.upload(gl)
        names=("transmittance.C.glsl","multiscatter.C.glsl","skyview.C.glsl")
        self.programs=programs or {name:gl.compute(DIRECTORY/name) for name in names}
        self.transmittance_size=(65,33)  # Deliberately exercises dispatch tails.
        self.multiscatter_size=(17,9)
        self.sky_size=(65,33)
        self.transmittance=gl.texture(self.transmittance_size)
        self.multiscatter=gl.texture(self.multiscatter_size)
        self.sky=gl.texture(self.sky_size)

    def upload(self):
        self.parameters.upload(self.gl,self.uniforms)

    def build(self,transmittance=True,multiscatter=True,sky=True):
        gl=self.gl
        self.upload()
        if transmittance:
            gl.image(0,self.transmittance)
            gl.dispatch_extent(self.programs["transmittance.C.glsl"],*self.transmittance_size)
        gl.sampler(0,self.transmittance)
        if multiscatter:
            gl.image(0,self.multiscatter)
            gl.dispatch_extent(self.programs["multiscatter.C.glsl"],*self.multiscatter_size)
        gl.sampler(1,self.multiscatter)
        if sky:
            gl.image(0,self.sky)
            gl.dispatch_extent(self.programs["skyview.C.glsl"],*self.sky_size)
        gl.check("physical LUT build")


def check_luts(gl,programs=None):
    f=PhysicalFixture(gl,programs)
    f.build()
    transmission=gl.texels(f.transmittance,f.transmittance_size)
    multiple=gl.texels(f.multiscatter,f.multiscatter_size)
    daylight=gl.texels(f.sky,f.sky_size)
    for values,label in ((transmission,"transmittance LUT"),(multiple,"multiple-scattering LUT"),(daylight,"sky-view LUT")):
        finite(values,label)
    assert all(0<=v<=1 for v in rgb(transmission)),"Transmittance left [0,1]"
    assert all(v>=0 for v in rgb(multiple)),"Negative multiple-scattering radiance"
    assert all(v>=0 for v in rgb(daylight)),"Negative sky radiance"
    assert max(rgb(daylight))>1.,"HDR sky was clamped or source energy lost"
    assert min(rgb(transmission))<.3 and max(rgb(transmission))>.99,"Transmittance fixture lacks expected atmosphere variation"

    # At the near-vertical direction less atmosphere remains above higher
    # points. All three wavelengths must therefore approach transmittance one.
    width,height=f.transmittance_size
    for channel in range(3):
        vertical=[transmission[(y*width+width-1)*4+channel] for y in range(height)]
        assert all(a<=b+2e-3 for a,b in zip(vertical,vertical[1:])),("Altitude transmittance is not monotone",channel,vertical)
    print("PASS LUTs: all dispatch tails written, finite HDR sky and nonnegative multi-scattering, spectral T in [0,1] and increasing with altitude",flush=True)

    # The physical material LUTs are valid for source intensities and optional
    # source assignments. Remove both lights without rebuilding T or MS.
    f.parameters.set("SunDirection",0,0,0,0)
    f.parameters.set("MoonDirection",0,0,0,0)
    f.build(False,False,True)
    darkness=gl.texels(f.sky,f.sky_size)
    finite(darkness,"unlit sky")
    assert all(v==0 for v in rgb(darkness)),"No-light scene acquired phantom sun/ambient light"

    # A secondary source alone works through the same atmosphere, while its
    # apparent disk is irrelevant to the diffuse sky LUT.
    f.parameters.set("MoonDirection",0,math.sqrt(.5),math.sqrt(.5),1)
    f.parameters.set("MoonRadiance",2,1.9,1.8,0)
    f.build(False,False,True)
    moonlight=gl.texels(f.sky,f.sky_size)
    assert max(rgb(moonlight))>0
    for moon,sun in zip(rgb(moonlight),rgb(daylight)):
        assert abs(moon-.1*sun)<=max(.002,.002*sun),("Secondary source differs from identical-direction scaled sun",moon,sun)
    print("PASS optional lights: no-light sky is black and finite; moon-only irradiance gives expected linear response",flush=True)

    f.parameters=Parameters()
    f.parameters.set("SunRadiance",40,38,36,0)
    f.build(False,False,True)
    double_sun=gl.texels(f.sky,f.sky_size)
    for doubled,base in zip(rgb(double_sun),rgb(daylight)):
        assert abs(doubled-2*base)<=max(.003,.003*base),("Sky irradiance lost linearity",doubled,base)

    # SkyView lives in world directions; rotating the view must not rotate
    # atmospheric light or require a different sky LUT.
    f.parameters.matrices["InvView"]=[-1,0,0,0,0,1,0,0,0,0,-1,0,0,0,0,1]
    f.build(False,False,True)
    assert gl.texels(f.sky,f.sky_size)==double_sun,"Camera orientation leaked into world-space SkyView"
    f.parameters.set("Camera",0,18000,0,0)
    f.build(False,False,True)
    high=gl.texels(f.sky,f.sky_size)
    finite(high,"high-altitude sky")
    assert max(abs(a-b) for a,b in zip(rgb(high),rgb(double_sun)))>.1,"Altitude did not affect SkyView"
    f.parameters.set("Camera",0,200000,0,0)
    f.build(False,False,True)
    space=gl.texels(f.sky,f.sky_size)
    finite(space,"camera outside atmosphere")
    assert all(v==0 for v in rgb(space[-f.sky_size[0]*4:])),"Vacuum above an outside-atmosphere camera emitted radiance"
    assert max(rgb(space))>0,"Atmosphere/planet disappeared when viewed from outside"
    print("PASS LUT dependencies: source irradiance is linear, camera rotation leaves SkyView fixed, altitude changes it",flush=True)

    f.parameters=Parameters()
    for name in ("Rayleigh","Mie","Absorption"):
        values=f.parameters.values[name].copy()
        f.parameters.set(name,0,0,0,values[3])
    f.parameters.set("Medium",0,.8,15000,1)
    f.build(True,False,False)
    vacuum=gl.texels(f.transmittance,f.transmittance_size)
    assert all(v==1 for v in vacuum),"Vacuum transmittance is not one"

    f.parameters=Parameters()
    for name in ("Rayleigh","Mie","Absorption"):
        v=f.parameters.values[name]
        f.parameters.set(name,2*v[0],2*v[1],2*v[2],v[3])
    f.parameters.values["Medium"][0]*=2
    f.build(True,False,False)
    dense=gl.texels(f.transmittance,f.transmittance_size)
    assert all(a<=b+1e-4 for a,b in zip(rgb(dense),rgb(transmission))),"Extra extinction increased transmission"
    for dense_value,base_value in zip(rgb(dense),rgb(transmission)):
        assert abs(dense_value-base_value*base_value)<.002,"Optical depth doubling violates Beer-Lambert law"
    print("PASS material dependencies: vacuum T=1; doubling all extinction squares T",flush=True)
    return f


def main():
    with AtmosphereGL() as gl:
        print(f"OpenGL {gl.version}; renderer: {gl.renderer}",flush=True)
        check_luts(gl)


if __name__=="__main__":
    main()
