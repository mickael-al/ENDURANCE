#!/usr/bin/env python3
"""Measure temporal alias residual against 4x4 supersampling, real deferred PBR.

A translating perspective camera renders the same static terrain at two
resolutions. Subtract the downsampled HDR reference before comparing frames:
ordinary parallax and legitimate changing reflections are not flicker errors.
The negative control only forces level zero in the CURRENT material shader.
An optional historical comparison uses its original texture encoding as well.
These controlled software-GL measurements do not predict game FPS or guarantee
zero aliasing without temporal AA.
"""
import argparse
import ctypes as C
import json
import hashlib
from pathlib import Path
import struct
import math
import numpy as np
from check_granular_render import (ROOT, TERRAIN, ScreenGL, DeferredFixture,
    camera, make_rig, draw, program, illumination, literal_table, edited_vec,
    shader_source, snapshot_source)


def set_camera(gl, fixture, rig, position):
    position,_,view,inverse,projection=camera(-24,0,fixture.size[0]/fixture.size[1],position)
    f=1/math.tan(math.radians(45)/2)
    projection[0]=f*fixture.size[1]/fixture.size[0]; projection[5]=f
    payload=struct.pack('<52f',*position,0,*view,*inverse,*projection)
    gl.glBindBuffer(0x90D2,fixture.camera_buffer)
    gl.glBufferData(0x90D2,len(payload),C.c_char_p(payload),0x88E8)
    rig.set(terrainGrainCameraPosition=position)


def display(rgb):
    return 1-np.exp(-np.maximum(rgb,0)*2)


def sequence(gl, shader, table, fragment, frames=8):
    size=(160,100); factor=4
    low=DeferredFixture(gl,size,-24,0,'opaque',camera_position=(0,3,0))
    high=DeferredFixture(gl,(size[0]*factor,size[1]*factor),-24,0,'opaque',camera_position=(0,3,0))
    rigs=[make_rig(gl,table,fixture,fragment) for fixture in (low,high)]
    illumination(gl,low,direction=(0,.27,-1))
    illumination(gl,high,direction=(0,.27,-1))
    errors=[]; references=[]; outputs=[]
    for frame in range(frames):
        position=(frame*.007,3.,frame*.011)
        images=[]
        for fixture,rig in zip((low,high),rigs):
            set_camera(gl,fixture,rig,position)
            images.append(np.array(draw(gl,rig,fixture,shader)).reshape(fixture.size[1],fixture.size[0],3))
        reference=images[1].reshape(size[1],factor,size[0],factor,3).mean(axis=(1,3))
        errors.append(display(images[0])-display(reference))
        references.append(display(reference)); outputs.append(display(images[0]))
    errors=np.array(errors); references=np.array(references)
    return dict(temporal_residual_rms=float(np.sqrt(np.mean(np.diff(errors,axis=0)**2))),
                spatial_error_rms=float(np.sqrt(np.mean(errors**2))),
                reference_mean=float(references.mean()),
                reference_contrast=float(references.std()),
                output_peak=float(np.max(outputs))),outputs


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--json',type=Path)
    parser.add_argument('--compare-ref')
    parser.add_argument('--preview',type=Path)
    args=parser.parse_args()
    table,_,_=literal_table(colors=[(.45,.22,.07)]*26,roughness=.8)
    table=edited_vec(table,0,224,(1.,0.,0.,0.))
    table=edited_vec(table,0,96,(.9,.15,0.,0.))
    source=shader_source(TERRAIN/'render.fs.glsl')
    unfiltered=source.replace('textureGrad(terrainGrainTexture,uv,dx,dy)',
                              'textureLod(terrainGrainTexture,uv,0.0)')
    assert unfiltered!=source, 'negative control did not disable footprint filtering'
    sources=[('filtered',source),('level_zero_control',unfiltered)]
    if args.compare_ref: sources.append(('previous',snapshot_source(TERRAIN/'render.fs.glsl',args.compare_ref)))
    result={'fragment_sha256':hashlib.sha256(source.encode()).hexdigest(),
            'fixture':'160x100 perspective plane, 8 translated camera frames, 4x4 HDR supersampling',
            'scope':'controlled real shaders, not game capture or hardware performance'}
    previews={}
    with ScreenGL() as gl:
        result['renderer']=gl.renderer
        shader=program(gl,'#define SSAO_ACTIVE 0')
        for name,fragment in sources:
            result[name],previews[name]=sequence(gl,shader,table,fragment)
            print(name,json.dumps(result[name]),flush=True)
        gl.check('grain camera-motion sequence')
    filtered=result['filtered']['temporal_residual_rms']
    control=result['level_zero_control']['temporal_residual_rms']
    result['temporal_ratio_to_level_zero']=filtered/control
    assert filtered<control*.8, ('filtering does not sufficiently reduce alias residual',result)
    assert result['filtered']['output_peak']>.6, 'test passed by removing visible reflections'
    if args.json:
        args.json.parent.mkdir(parents=True,exist_ok=True)
        args.json.write_text(json.dumps(result,indent=2)+'\n')
    if args.preview:
        from PIL import Image,ImageDraw
        frames=[]
        for frame in range(8):
            canvas=Image.new('RGB',(160*len(sources),125),(24,26,30))
            label=ImageDraw.Draw(canvas)
            for i,(name,_) in enumerate(sources):
                rgb=np.power(np.clip(previews[name][frame][::-1],0,1),1/2.2)
                canvas.paste(Image.fromarray(np.uint8(rgb*255)),(i*160,25))
                label.text((i*160+3,5),name,fill='white')
            frames.append(canvas)
        frames[0].save(args.preview,save_all=True,append_images=frames[1:],duration=110,loop=0)
    print(f'PASS motion: alias residual ratio {filtered/control:.3f} versus identical level-zero material',flush=True)


if __name__=='__main__': main()
