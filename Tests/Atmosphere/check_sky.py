#!/usr/bin/env python3
"""Validate physical sky/cubemap integration; optionally save diagnostic panoramas."""
from __future__ import annotations

import argparse
import math
from pathlib import Path

from check_ibl import cube_values
from check_luts import finite, rgb
from check_volumes import VolumeFixture
from gl_support import AtmosphereGL,ROOT,TEXTURE_3D,TEXTURE_CUBE_MAP


def preview(cases,size,path):
    """Diagnostic only: nearest cubemap lookup, Reinhard then sRGB output."""
    from PIL import Image,ImageDraw
    width,height=512,256
    image=Image.new("RGB",(width,(height+24)*len(cases)),(18,22,28))
    draw=ImageDraw.Draw(image)
    for case,(label,values) in enumerate(cases):
        draw.text((8,case*(height+24)+6),label,fill=(235,238,242))
        pixels=image.load()
        for y in range(height):
            latitude=(.5-(y+.5)/height)*math.pi
            for x in range(width):
                longitude=((x+.5)/width-.5)*2*math.pi
                dx=math.cos(latitude)*math.cos(longitude)
                dy=math.sin(latitude)
                dz=math.cos(latitude)*math.sin(longitude)
                ax,ay,az=abs(dx),abs(dy),abs(dz)
                if ax>=ay and ax>=az:
                    face=0 if dx>0 else 1
                    u=(-dz if dx>0 else dz)/ax;v=-dy/ax
                elif ay>=az:
                    face=2 if dy>0 else 3
                    u=dx/ay;v=(dz if dy>0 else -dz)/ay
                else:
                    face=4 if dz>0 else 5
                    u=(dx if dz>0 else -dx)/az;v=-dy/az
                ix=min(size-1,max(0,int((u*.5+.5)*size)))
                iy=min(size-1,max(0,int((v*.5+.5)*size)))
                index=((face*size+iy)*size+ix)*4
                channels=[]
                for radiance in values[index:index+3]:
                    value=max(0,radiance)/(1+max(0,radiance))
                    srgb=12.92*value if value<=.0031308 else 1.055*value**(1/2.4)-.055
                    channels.append(round(255*min(max(srgb,0),1)))
                pixels[x,case*(height+24)+24+y]=tuple(channels)
    path.parent.mkdir(parents=True,exist_ok=True)
    image.save(path)
    print(f"Diagnostic panorama: {path}",flush=True)


def check_sky(gl,preview_path=None):
    f=VolumeFixture(gl)
    # Public Ultra weather defaults, with tiny diagnostic textures/budgets.
    f.parameters.set("CloudLayer",2200,1800,.42,.0012)
    f.parameters.set("CloudShape",6000,4,.35,.6)
    f.parameters.set("SunDirection",0,math.sqrt(.5),math.sqrt(.5),1)
    f.parameters.set("SunDisk",100,95,90,.04)  # Resolvable diagnostic disk.
    f.parameters.set("Samples",2,8,16,48)
    f.build()
    f.cloud_noise()
    size=(65,65)
    environment=gl.compute(ROOT/"Asset/Shader/Atmosphere/environment.C.glsl","#define GE_PHYSICAL_SKY 1")
    visible=gl.texture(size,target=TEXTURE_CUBE_MAP)
    lighting=gl.texture(size,target=TEXTURE_CUBE_MAP)

    def capture(clouds):
        f.parameters.values["Control"][2]=int(clouds)
        f.upload()
        gl.sampler(0,f.transmittance)
        gl.sampler(1,f.multiscatter)
        gl.sampler(2,f.sky)
        gl.sampler(3,f.noise,TEXTURE_3D)
        gl.image(0,visible,layered=True)
        gl.image(1,lighting,layered=True)
        gl.uniform(environment,"geUseExternalEnvironment","1i",0)
        gl.dispatch_extent(environment,*size,6)
        outputs=cube_values(gl,visible,size),cube_values(gl,lighting,size)
        finite(outputs[0],"physical visible cube")
        finite(outputs[1],"physical IBL source cube")
        return outputs

    clear,clear_ibl=capture(False)
    assert max(rgb(clear))>1 and max(b-a for a,b in zip(rgb(clear_ibl),rgb(clear)))>1,"Physical disk/HDR source lost in cubemap"
    cloud,cloud_ibl=capture(True)
    assert max(abs(a-b) for a,b in zip(rgb(clear_ibl),rgb(cloud_ibl)))>.01,"Default cloud controls produced no visible cloud contribution"
    assert all(a<=b+.002 for a,b in zip(rgb(cloud_ibl),rgb(cloud))),"Direct solar disk subtraction produced negative energy"
    f.parameters.values["CloudLayer"][2]=0
    empty,empty_ibl=capture(True)
    assert empty==clear and empty_ibl==clear_ibl,"Coverage-zero sky is not clear-sky identity"
    f.parameters.values["CloudLayer"][2]=.42
    f.parameters.values["Camera"][3]=137
    moving,moving_ibl=capture(True)
    assert max(abs(a-b) for a,b in zip(rgb(cloud_ibl),rgb(moving_ibl)))>.01,"Cloud sky/cubemap stopped when camera XYZ stayed fixed"
    f.parameters.set("SunDirection",0,0,0,0)
    f.parameters.set("MoonDirection",0,0,0,0)
    f.build(False,False,True)
    night,night_ibl=capture(True)
    assert all(v==0 for v in rgb(night)) and all(v==0 for v in rgb(night_ibl)),"No-light physical cloudy sky emitted phantom radiance"
    print("PASS physical environment: HDR+disk, default clouds visible, coverage-zero identity, stationary wind, disk-free IBL and no-source darkness",flush=True)
    if preview_path:
        preview([("Clear physical sky - diagnostic exposure",clear),
                 ("Cloud coverage 0.42 - identical atmosphere",cloud),
                 ("Same clouds after 137 s - camera stationary",moving)],size[0],preview_path)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--preview",type=Path,help="Optional diagnostic PNG path; requires Pillow")
    args=parser.parse_args()
    with AtmosphereGL() as gl:
        print(f"OpenGL {gl.version}; renderer: {gl.renderer}",flush=True)
        check_sky(gl,args.preview)


if __name__=="__main__":
    main()
