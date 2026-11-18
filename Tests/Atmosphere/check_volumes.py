#!/usr/bin/env python3
"""Execute atmospheric, fog and cloud passes, including actual shadow sampling."""
from __future__ import annotations

import math
import struct

from check_luts import DIRECTORY, PhysicalFixture, finite, rgb
from fixtures import Parameters
from gl_support import AtmosphereGL, ROOT, RG16F, RGBA32F, R32F, TEXTURE_3D, TEXTURE_2D_ARRAY


class VolumeFixture(PhysicalFixture):
    def __init__(self,gl):
        super().__init__(gl)
        self.size=(9,7,9)
        self.noise_size=(17,17,17)
        self.shadow_size=(9,7)
        for name in ("aerial.C.glsl","cloud_volume.C.glsl","volume.C.glsl","cloud_noise.C.glsl","cloud_shadow.C.glsl"):
            self.programs[name]=gl.compute(DIRECTORY/name)
        self.noise=gl.texture(self.noise_size,target=TEXTURE_3D)
        # The production cloud atlas is a periodic volume. Test samplers use
        # the same repeat addressing, including its boundary interpolation.
        gl.glBindTexture(TEXTURE_3D,self.noise)
        for axis in (0x2802,0x2803,0x8072):
            gl.glTexParameteri(TEXTURE_3D,axis,0x2901)
        self.cloud_shadow=gl.texture(self.shadow_size,internal=RG16F)
        self.coefficients=[gl.texture(self.size,target=TEXTURE_3D,internal=R32F if i==3 else RGBA32F) for i in range(4)]
        self.scattering=gl.texture(self.size,target=TEXTURE_3D)
        self.transmission=gl.texture(self.size,target=TEXTURE_3D)
        self.baseline_s=gl.texture(self.size[:2])
        self.baseline_t=gl.texture(self.size[:2])
        self.lit_shadow=gl.texture((4,4,1),target=TEXTURE_2D_ARRAY,internal=R32F,value=1)
        self.blocked_shadow=gl.texture((4,4,1),target=TEXTURE_2D_ARRAY,internal=R32F,value=0)
        matrix=[1e-5,0,0,0,0,1e-5,0,0,0,0,1e-5,0,0,0,0,1]
        self.shadow_matrices=gl.buffer(struct.pack("<20f",*(matrix+[0,0,0,-1000000.])),5)

    def cloud_noise(self):
        self.upload()
        self.gl.image(0,self.noise,layered=True)
        self.gl.dispatch_extent(self.programs["cloud_noise.C.glsl"],*self.noise_size)

    def volumes(self,*,blocked=False):
        gl=self.gl
        self.upload()
        gl.sampler(0,self.transmittance)
        gl.sampler(1,self.multiscatter)
        gl.sampler(3,self.noise,TEXTURE_3D)
        gl.sampler(4,self.blocked_shadow if blocked else self.lit_shadow,TEXTURE_2D_ARRAY)
        gl.bind(self.shadow_matrices,5)
        gl.image(0,self.cloud_shadow,internal=RG16F)
        gl.dispatch_extent(self.programs["cloud_shadow.C.glsl"],*self.shadow_size)
        gl.sampler(5,self.cloud_shadow)
        for name,start in (("aerial.C.glsl",0),("cloud_volume.C.glsl",2)):
            gl.image(0,self.coefficients[start],internal=RGBA32F,layered=True)
            gl.image(1,self.coefficients[start+1],internal=R32F if start==2 else RGBA32F,layered=True)
            gl.dispatch_extent(self.programs[name],*self.size)
        for unit,texture in enumerate(self.coefficients,9):
            gl.sampler(unit,texture,TEXTURE_3D)
        gl.image(0,self.scattering,layered=True)
        gl.image(1,self.transmission,layered=True)
        gl.image(2,self.baseline_s)
        gl.image(3,self.baseline_t)
        gl.dispatch_extent(self.programs["volume.C.glsl"],*self.size[:2])
        scattering=gl.texels(self.scattering,self.size,target=TEXTURE_3D)
        transmission=gl.texels(self.transmission,self.size,target=TEXTURE_3D)
        for values,label in ((scattering,"integrated scattering"),(transmission,"integrated transmission")):
            finite(values,label)
        gl.check("complete volumetric build")
        return scattering,transmission


def depth_invariants(f,scattering,transmission):
    width,height,depth=f.size
    for y in range(height):
        for x in range(width):
            previous_s=(0,0,0)
            previous_t=(1,1,1)
            for z in range(depth):
                begin=((z*height+y)*width+x)*4
                s,t=scattering[begin:begin+3],transmission[begin:begin+3]
                for channel in range(3):
                    assert s[channel]>=previous_s[channel]-1e-5,("Scattering decreased along ray",x,y,z,channel)
                    assert 0<=t[channel]<=previous_t[channel]+1e-5,("Transmittance increased along ray",x,y,z,channel)
                previous_s,previous_t=s,t


def check_volumes(gl):
    f=VolumeFixture(gl)
    f.build()
    f.cloud_noise()
    noise=gl.texels(f.noise,f.noise_size,target=TEXTURE_3D)
    finite(noise,"procedural cloud noise")
    assert all(0<=v<=1 for v in noise)
    f.cloud_noise()
    assert gl.texels(f.noise,f.noise_size,target=TEXTURE_3D)==noise,"Cloud noise changed at identical seed"
    f.parameters.values["Misc"][0]+=1
    f.cloud_noise()
    assert gl.texels(f.noise,f.noise_size,target=TEXTURE_3D)!=noise,"Cloud seed has no effect"
    s,t=f.volumes()
    depth_invariants(f,s,t)
    for i,texture in enumerate(f.coefficients):
        finite(gl.texels(texture,f.size,target=TEXTURE_3D,channels=1 if i==3 else 4),"source/extinction coefficient volume")
    assert all(v==0 for v in gl.texels(f.coefficients[3],f.size,target=TEXTURE_3D,channels=1)),"Disabled clouds left extinction"
    assert all(v==1 for v in gl.texels(f.cloud_shadow,f.shadow_size,channels=2)),"Disabled clouds cast shadows"
    print("PASS volumes: finite complete coefficient/output volumes, monotonic S/T, deterministic seeded noise, disabled cloud identity",flush=True)

    # Real terrain-shadow buffer and sampler: one uniform occluding depth map
    # removes the direct source without altering optical extinction. Clouds
    # remain disabled so the test proves cloud-independent god rays.
    f.parameters.set("Control",1,0,0,1)
    f.parameters.set("Shadow",0,-1,1,0)
    lit_s,lit_t=f.volumes(blocked=False)
    shadow_s,shadow_t=f.volumes(blocked=True)
    assert shadow_t==lit_t,"Terrain shadows changed medium extinction"
    assert all(a<=b+1e-4 for a,b in zip(rgb(shadow_s),rgb(lit_s))),"Occlusion brightened in-scattering"
    assert max(b-a for a,b in zip(rgb(shadow_s),rgb(lit_s)))>.01,"Terrain occluder did not affect atmospheric source"
    print("PASS god rays: actual terrain shadow sampling attenuates in-scattering with no clouds and leaves T unchanged",flush=True)

    # Pure absorption yields an exact independent reference even with no
    # atmospheric light assigned. A zero albedo cannot emit a fog glow.
    f.parameters=Parameters()
    f.parameters.set("Control",0,1,0,0)
    f.parameters.set("SunDirection",0,0,0,0)
    f.parameters.set("MoonDirection",0,0,0,0)
    f.parameters.set("Volume",20000,.5,.0001,1000000)
    f.parameters.set("Fog",0,0,0,200)
    s,t=f.volumes()
    assert all(v==0 for v in rgb(s)),"Unlit absorptive fog emitted phantom radiance"
    width,height,depth=f.size
    for z in range(depth):
        distance=20000*(z/(depth-1))**2
        expected=math.exp(-.0001*distance)
        values=t[z*width*height*4:(z+1)*width*height*4]
        assert all(abs(v-expected)<.0015 for v in rgb(values)),("Pure absorption differs from analytic T",z,expected,min(rgb(values)),max(rgb(values)))
    f.parameters.set("Control",0,0,0,0)
    s,identity=f.volumes()
    assert all(v==0 for v in rgb(s)) and all(v==1 for v in identity),"Absent media do not compose to identity"
    print("PASS fog absence/absorption: no-light S=0, homogeneous T matches exp(-density*distance), disabled media are identity",flush=True)

    f.parameters=Parameters()
    f.parameters.set("Control",1,0,1,0)
    f.parameters.set("CloudLayer",2000,1500,0,.004)
    f.build()
    s,t=f.volumes()
    assert all(v==0 for v in gl.texels(f.coefficients[3],f.size,target=TEXTURE_3D,channels=1)),"Coverage zero generated cloud extinction"
    assert all(v==1 for v in gl.texels(f.cloud_shadow,f.shadow_size,channels=2)),"Coverage zero shadowed sunlight"
    f.parameters.set("CloudLayer",2000,1500,1,.002)
    cloud_s,cloud_t=f.volumes()
    cloud_e=gl.texels(f.coefficients[3],f.size,target=TEXTURE_3D,channels=1)
    cloud_shadow=gl.texels(f.cloud_shadow,f.shadow_size,channels=2)
    assert max(cloud_e)>1e-4,"Coverage one did not produce clouds"
    assert all(0<=v<=1 for v in cloud_shadow) and min(cloud_shadow[::2])<.9,"Clouds did not attenuate sunlight"
    assert all(v==1 for v in cloud_shadow[1::2]),"Unassigned secondary source has non-identity cloud transmission"
    assert all(a<=b+.001 for a,b in zip(rgb(cloud_t),rgb(t))),"Cloud addition reduced extinction"
    # The engine integrates velocity; shaders consume its current displacement.
    f.parameters.values["Camera"][3]=97
    f.parameters.set("CloudOffset",30*97,5*97,20000,0)
    f.volumes()
    later_e=gl.texels(f.coefficients[3],f.size,target=TEXTURE_3D,channels=1)
    assert max(abs(a-b) for a,b in zip(cloud_e,later_e))>1e-5,"Cloud animation stopped with stationary camera"
    f.parameters.set("SunDirection",0,0,0,0)
    f.parameters.set("MoonDirection",0,0,0,0)
    night_s,night_t=f.volumes()
    assert all(v==0 for v in rgb(night_s)),"No-light cloud volume emitted phantom light"
    finite(night_t,"no-light cloud transmission")
    depth_invariants(f,night_s,night_t)
    print("PASS clouds: coverage-zero identity, finite coverage-one extinction, solar attenuation, stationary wind and no-light darkness",flush=True)
    return f


def main():
    with AtmosphereGL() as gl:
        print(f"OpenGL {gl.version}; renderer: {gl.renderer}",flush=True)
        check_volumes(gl)


if __name__=="__main__":
    main()
