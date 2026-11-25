#!/usr/bin/env python3
"""Render granular terrain through the unchanged production deferred shader.

The test plane is perspective-correct and uses production G-buffer formats.
Optional captures are controlled shader fixtures, not gameplay screenshots or
hardware performance claims. No hand-written substitute lighting is used.
"""
import argparse
import ctypes as C
import math
from pathlib import Path
import re
import struct
import subprocess
import sys

from check_gpu import (ROOT, TERRAIN, MaterialRig, literal_table, edited_vec,
                       RG32F, TEXTURE_2D, shader_source)
sys.path.insert(0, str(ROOT/'Tests/Trees'))
from check_lighting import illumination, program
from check_screen_composition import ScreenGL, DeferredFixture, camera

PLANE = '''#version 460 core
layout(std430,binding=0) buffer Camera { vec3 position; mat4 view; mat4 inverseView; mat4 projection; } camera;
layout(location=0) out vec3 viewPosition;
layout(location=1) out vec3 worldPosition;
layout(location=2) out vec3 worldNormal;
layout(location=3) out vec2 climate;
layout(location=4) flat out uint chunkLod;
void main() {
    vec2 corner=vec2((gl_VertexID<<1)&2,gl_VertexID&2);
    worldPosition=vec3((corner.x*2.0-1.0)*256.0,0.0,-(corner.y*2.0-1.0)*256.0);
    viewPosition=(camera.view*vec4(worldPosition,1.0)).xyz;
    worldNormal=vec3(0,1,0); climate=vec2(.3,.6); chunkLod=0u;
    gl_Position=camera.projection*vec4(viewPosition,1.0);
}
'''


def snapshot_source(path, ref):
    relative=path.relative_to(ROOT).as_posix()
    raw=subprocess.check_output(['git','show',f'{ref}:{relative}'],cwd=ROOT,text=True)
    return re.sub(r'^\s*#include\s+"([^"]+)"\s*$',
                  lambda m: snapshot_source((path.parent/m[1]).resolve(),ref),raw,flags=re.M)


def make_rig(gl, table, fixture, fragment=None):
    # RG32F depth/tag, RGB16F normal, RGBA16F albedo, RGB8 material: same as engine.
    rig=MaterialRig(gl,table,fixture.size,fragment=fragment,formats=(RG32F,0x881B,0x881A,0x8051))
    rig.set_vertex(PLANE)
    rig.set(terrainSparkleGrainSize=.01,terrainAlbedoGamma=1.,microDetailStrength=0.,terrainGrainCameraPosition=(0.,3.,0.),
            terrainGrainLightDirection=(0.,.27/math.sqrt(1+.27**2),-1/math.sqrt(1+.27**2)))
    return rig


def draw(gl, rig, fixture, deferred):
    gl.bind(fixture.camera_buffer,0)
    rig.draw()
    for slot,texture in enumerate(rig.outputs):
        gl.sampler(slot,texture)
        # Engine fullscreen UV convention repeats negative Y coordinates.
        gl.glTexParameteri(TEXTURE_2D,0x2802,0x2901)
        gl.glTexParameteri(TEXTURE_2D,0x2803,0x2901)
        fixture.samplers[slot]=(texture,TEXTURE_2D)
    return fixture.render(deferred,False)


def mean(image):
    return sum(sum(pixel) for pixel in image)/(3*len(image))


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--preview',type=Path)
    parser.add_argument('--compare-ref',help='optional previous commit, read-only')
    args=parser.parse_args()
    size=(640,400) if args.preview else (192,128)
    with ScreenGL() as gl:
        print(f'OpenGL {gl.version}; {gl.renderer}',flush=True)
        shader=program(gl,'#define SSAO_ACTIVE 0')
        fixture=DeferredFixture(gl,size,-24,0,'opaque',camera_position=(0,3,0))
        position,_,view,inverse,projection=camera(-24,0,size[0]/size[1],(0,3,0))
        f=1/math.tan(math.radians(45)/2)
        projection[0]=f*size[1]/size[0]; projection[5]=f
        fixture.camera_buffer=gl.buffer(struct.pack('<52f',*position,0,*view,*inverse,*projection),0)
        table,_,_=literal_table(colors=[(.45,.22,.07)]*26,roughness=.8)
        table=edited_vec(table,0,224,(1.,0.,0.,0.))
        grains=edited_vec(table,0,96,(.9,.15,0.,0.))
        rig=make_rig(gl,grains,fixture)
        illumination(gl,fixture,direction=(0,.27,-1))
        image=draw(gl,rig,fixture,shader)
        baseline=make_rig(gl,table,fixture)
        matte=draw(gl,baseline,fixture,shader)
        gain=[max(a)-max(b) for a,b in zip(image,matte)]
        coverage=sum(x>.3 for x in gain)/len(gain)
        assert max(gain)>.5 and .0001<coverage<.1, ('missing sparse directional highlights',max(gain),coverage)
        assert all(math.isfinite(c) and c>=0 for pixel in image for c in pixel)
        print(f'PASS unchanged deferred: HDR grain excess {max(gain):.3f}, bright coverage {coverage:.3%}',flush=True)
        illumination(gl,fixture,direction=(0,.27,-1),enabled=False)
        dark=draw(gl,rig,fixture,shader)
        assert max(max(p) for p in dark)<1e-6, 'grains emit light without a source'
        illumination(gl,fixture,direction=(0,.27,-1),shadow=True)
        # Cover the WHOLE perspective plane. The foliage fixture's +/-100 m
        # projection leaves distant top-row pixels outside its shadow map.
        matrix=(.001,0,0,0, 0,.001,0,0, 0,0,.001,0, 0,0,0,1)
        gl.buffer(b''.join(struct.pack('<20f',*matrix,0,0,0,split)
                           for split in (-100.,-500.,-1000.,-10000.)),3)
        shadowed=draw(gl,rig,fixture,shader)
        assert max(max(p) for p in shadowed)<1e-6, 'grain escaped a fully covering shadow map'
        illumination(gl,fixture,direction=(0,.27,1))
        rig.set(terrainGrainLightDirection=(0.,.27/math.sqrt(1+.27**2),1/math.sqrt(1+.27**2)))
        away=draw(gl,rig,fixture,shader)
        # Turning the sun away must change WHICH grains are bright.
        original={i for i,p in enumerate(image) if max(p)>.5}
        turned={i for i,p in enumerate(away) if max(p)>.5}
        assert len(original)>0 and len(original & turned)<len(original)*.15
        print('PASS light-dependent grains, real shadow map and no emission',flush=True)
        illumination(gl,fixture,direction=(0,.27,-1),ambient=.35,irradiance=(.12,.17,.24,1))
        snow=edited_vec(grains,0,0,(.72,.77,.8,.88))
        snow=edited_vec(snow,0,16,(.72,.77,.8,.88))
        snow=edited_vec(snow,0,32,(.72,.77,.8,.88))
        rig.set(terrainGrainLightDirection=(0.,.27/math.sqrt(1+.27**2),-1/math.sqrt(1+.27**2)))
        rig.upload(snow)
        snow_image=draw(gl,rig,fixture,shader)
        assert all(math.isfinite(c) and c>=0 for pixel in snow_image for c in pixel)
        print('PASS same granular path on snow with existing IBL',flush=True)
        if args.preview:
            import numpy as np
            from PIL import Image,ImageDraw
            rig.upload(grains)
            panels=[('Nouveau : grains compacts + mipmaps / forward inchange',draw(gl,rig,fixture,shader))]
            if args.compare_ref:
                old=make_rig(gl,grains,fixture,snapshot_source(TERRAIN/'render.fs.glsl',args.compare_ref))
                panels.insert(0,(f'Avant : {args.compare_ref}',draw(gl,old,fixture,shader)))
            panels.append(('Neige : meme chemin de rendu',snow_image))
            canvas=Image.new('RGB',(size[0]*len(panels),size[1]+40),(24,26,30))
            labels=ImageDraw.Draw(canvas)
            for i,(label,values) in enumerate(panels):
                rgb=np.array(values).reshape(size[1],size[0],3)[::-1]
                # Identical fixed display transform on all panels, no bloom.
                rgb=np.power(np.clip(1-np.exp(-rgb*2),0,1),1/2.2)
                canvas.paste(Image.fromarray(np.uint8(rgb*255)),(i*size[0],40))
                labels.text((i*size[0]+8,12),label,fill='white')
            args.preview.parent.mkdir(parents=True,exist_ok=True)
            canvas.save(args.preview)
            print(f'Controlled production-shader comparison: {args.preview}',flush=True)
        gl.check('granular deferred integration')


if __name__=='__main__':
    main()
