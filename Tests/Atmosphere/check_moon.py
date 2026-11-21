#!/usr/bin/env python3
"""Actual lunar surface/atmosphere shaders: phases, occlusion, reflection input.

Headless readbacks are diagnostics only. Optional previews are real shader output.
"""
import argparse
import ctypes as C
import math
from pathlib import Path
import struct
import numpy as np
from PIL import Image, ImageDraw
from gl_support import AtmosphereGL, ROOT, FULLSCREEN_VERTEX, TEXTURE_3D, TEXTURE_CUBE_MAP, shader_source
from check_volumes import VolumeFixture


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--preview',type=Path)
    parser.add_argument('--surface',type=Path,default=ROOT/'ENDURANCE/Ressources/Atmosphere/moon.surface')
    parser.add_argument('--size',type=int,default=321)
    args=parser.parse_args()
    if not 64<=args.size<=1024: parser.error('size must be between 64 and 1024')
    blob=args.surface.read_bytes()
    assert blob[:8]==b'EMOON001'
    w,h,mean=struct.unpack_from('<IIf',blob,8)
    assert len(blob)==20+w*h*4 and (w,h)==(2048,1024)
    pixels=np.frombuffer(blob[20:],np.uint8).reshape(h,w,4)
    assert abs(pixels[...,3].mean()/255-mean)<1e-6
    assert pixels[...,3].std()>5 and .02<pixels[...,3].min()/255<.3
    assert pixels[...,3].max()/255<.65, 'Lunar material saturates to white'
    assert .20<mean<.22, 'Surface detail unexpectedly changes total moon lighting'
    normals=pixels[...,:3].astype(np.float32)/127.5-1
    assert np.max(np.abs(np.linalg.norm(normals,axis=-1)-1))<.008
    with AtmosphereGL() as gl:
        f=VolumeFixture(gl); f.parameters.set('Horizon',.5,.5,.5,0)
        f.parameters.set('MoonDisk',.82,.80,.75,math.radians(10))
        f.parameters.set('SunRadiance',12,12,12,0); f.parameters.set('Control',1,0,0,0)
        f.build(); f.cloud_noise()
        atlas=gl.texture((w,h),internal=0x8058,levels=12,value=0.)
        gl.glBindTexture(0x0DE1,atlas)
        gl.glTexImage2D(0x0DE1,0,0x8058,w,h,0,0x1908,0x1401,C.c_char_p(blob[20:]))
        gl.glTexParameteri(0x0DE1,0x2802,0x2901); gl.glGenerateMipmap(0x0DE1)
        size=(args.size,args.size); output=gl.texture(size)
        probe=ROOT/'Tests/Atmosphere/moon_probe.fs.glsl'
        programs=[gl.compile([(0x8B31,FULLSCREEN_VERTEX),(0x8B30,shader_source(probe,defines))],defines)
                  for defines in ('','#define PROBE_ATMOSPHERE 1',
                                  '#define PROBE_ATMOSPHERE 1\n#define PROBE_BACKGROUND 1')]
        moon=gl.buffer(bytes(32)); gl.glBindBufferBase(0x8A11,7,moon)
        distance=1000000.; radius=distance*math.sin(math.radians(10))
        forward=np.array([0.,0.,1.],np.float32)
        def place(centre,rotation=0.,normal_strength=.65):
            gl.write(moon,struct.pack('<8f',*centre,radius,math.cos(rotation),math.sin(rotation),normal_strength,1))
            gl.glBindBufferBase(0x8A11,7,moon)
        def render(mode=0):
            f.upload()
            for unit,tex in ((0,f.transmittance),(1,f.multiscatter),(2,f.sky),(4,atlas),(5,f.cloud_shadow)):
                gl.sampler(unit,tex)
            gl.sampler(3,f.noise,TEXTURE_3D)
            gl.uniform(programs[mode],'probeForward','3f',*forward)
            gl.uniform(programs[mode],'geMoonFootprint','1f',.62/size[0])
            gl.fullscreen(programs[mode],*size,[output])
            result=np.array(gl.texels(output,size)).reshape(size[1],size[0],4)
            assert np.isfinite(result).all() and (result>=0).all()
            return result
        previews=[]; place(forward*distance)
        for label,light in [('Full',-forward),('Quarter',(1,0,0)),('New',forward)]:
            f.parameters.set('SunDirection',*light,1)
            result=render(); previews.append((label,result))
        assert previews[0][1][...,:3].sum()>previews[1][1][...,:3].sum()*1.7
        assert previews[1][1][...,:3].max()>.1
        assert previews[2][1][...,:3].max()<1e-6,'Night side emits light'
        f.parameters.set('SunDirection',0,0,-1,0)
        assert render()[...,:3].max()==0,'Missing sun still illuminates moon'
        f.parameters.set('SunDirection',0,0,-1,1)
        full=render(); place(forward*distance,1.2); rotated=render()
        assert abs(full[...,3]-rotated[...,3]).max()==0
        assert abs(full[...,:3]-rotated[...,:3]).max()>.03
        place((100000,0,distance)); shifted=render()
        assert abs(full[...,3]-shifted[...,3]).max()>.9,'Finite moon has no parallax'
        place((0,0,-distance)); assert render().max()==0,'Rearward sphere appears in front'
        # Real atmospheric T, evaluated per lunar pixel, including horizon clipping.
        angle=math.radians(25); forward=np.array([0,math.sin(angle),math.cos(angle)])
        place(forward*distance)
        f.parameters.set('SunDirection',-.7,.35,-.622495,1)
        f.build(); plain=render(); atmospheric=render(1)
        assert atmospheric[...,:3].sum()<plain[...,:3].sum()*.99
        assert atmospheric[...,:3].max()>0
        previews.append(('Day atmosphere',render(2)))
        # Turning the scene light off below the horizon must not turn off the
        # sunlight which illuminates the remote lunar surface itself.
        f.parameters.set('SunDirection',*[-float(x) for x in forward],1)
        assert render()[...,:3].max()>.1,'Night selection extinguished the visible moon'
        f.parameters.set('SunDirection',-.7,.35,-.622495,1)

        # A uniform cloud slab must extinguish the body, not just its centre.
        opaque=gl.texture((2,2,2),target=TEXTURE_3D,value=(1,1,1,1))
        original=f.noise; f.noise=opaque
        f.parameters.set('Control',1,0,1,0)
        f.parameters.set('CloudLayer',2000,1500,1,.02)
        f.parameters.set('CloudWeather',48000,0,1,0)
        cloud=render(1)
        assert cloud[...,:3].sum()<atmospheric[...,:3].sum()*1e-3
        f.noise=original; f.parameters.set('CloudLayer',2200,1800,.42,.0012)
        f.parameters.set('CloudWeather',48000,.65,1.4,.7)
        previews.append(('Clouds in front',render(2)))
        # Full integration: diffuse remains disk-free, GGX includes the moon.
        f.parameters.set('Control',1,0,0,0)
        f.parameters.set('MoonRadiance',.02,.02,.02,1)
        f.parameters.set('SunDisk',0,0,0,.01); f.upload()
        program=gl.compute(ROOT/'Asset/Shader/Atmosphere/environment.C.glsl',
                           '#define GE_PHYSICAL_SKY 1\n#define GE_ATM_MOON_SURFACE 1')
        cubes=[gl.texture((65,65),target=TEXTURE_CUBE_MAP) for _ in range(3)]
        for unit,tex in ((0,f.transmittance),(1,f.multiscatter),(2,f.sky),(4,atlas)):
            gl.sampler(unit,tex)
        gl.sampler(3,f.noise,TEXTURE_3D)
        for i,tex in enumerate(cubes): gl.image(i,tex,layered=True)
        gl.uniform(program,'geUseExternalEnvironment','1i',0)
        gl.uniform(program,'geMoonFootprint','1f',2/65)
        gl.dispatch_extent(program,65,65,6)
        values=[np.concatenate([gl.texels(tex,(65,65),target=TEXTURE_CUBE_MAP,face=i) for i in range(6)]) for tex in cubes]
        assert np.isfinite(values).all()
        assert (values[2]-values[1]).max()>.03,'Moon absent from reflection input'
        assert abs(values[0]-values[2]).max()<.003,'Sky/reflection use different lunar evaluation'
        # Final production sky draw: disabled clouds have a zero placeholder,
        # but that placeholder alpha must not make the moon disappear.
        sky_program=gl.compile([(0x8B31,shader_source(ROOT/'Asset/Shader/Atmosphere/sky.vs.glsl')),
                                (0x8B30,shader_source(ROOT/'Asset/Shader/Atmosphere/sky.fs.glsl',
                                 '#define GE_PHYSICAL_SKY 1\n#define GE_ATM_MOON_SURFACE 1'))],'lunar sky draw')
        outputs=[gl.texture(size) for _ in range(4)]
        empty_clouds=gl.texture((1,1),value=(0,0,0,0))
        clear_clouds=gl.texture((1,1),value=(0,0,0,1))
        f.parameters.matrices['InvProjection']=[.31,0,0,0, 0,.31,0,0, 0,0,1,0, 0,0,0,1]
        co,si=math.cos(angle),math.sin(angle)
        f.parameters.matrices['InvView']=[1,0,0,0, 0,co,-si,0, 0,si,co,0, 0,0,0,1]
        def sky_draw(cloud_texture,show_moon=True):
            f.upload()
            place(forward*distance if show_moon else -forward*distance)
            for unit,tex in ((1,cloud_texture),(2,f.sky),(4,atlas),(11,f.multiscatter),
                             (14,f.cloud_shadow),(15,f.transmittance)):
                gl.sampler(unit,tex)
            gl.sampler(0,cubes[1],TEXTURE_CUBE_MAP); gl.sampler(3,f.noise,TEXTURE_3D)
            gl.uniform(sky_program,'geCurrentCloudView','1i',1)
            gl.uniform(sky_program,'geMoonFootprint','1f',.62/size[0])
            gl.fullscreen(sky_program,*size,outputs)
            return np.array(gl.texels(outputs[2],size)).reshape(*size,4)[...,:3]
        baseline=sky_draw(empty_clouds,False)
        no_clouds=sky_draw(empty_clouds)
        assert (no_clouds-baseline).max()>.03,'Disabled cloud placeholder erased the lunar surface'
        f.parameters.set('Control',1,0,1,0)
        assert abs(sky_draw(empty_clouds)-baseline).max()<.002,'Opaque cached cloud transmission leaked the moon'
        assert abs(sky_draw(clear_clouds)-no_clouds).max()<.002,'Cached clear sky changes moon radiance'
        print('PASS lunar atlas, phases, darkness, rotation, finite parallax, atmosphere, cloud occlusion, sky/GGX consistency, final cached-cloud sky draw')
        if args.preview:
            sheet=Image.new('RGB',(size[0]*len(previews),size[1]+24))
            draw=ImageDraw.Draw(sheet)
            for i,(label,array) in enumerate(previews):
                linear=np.maximum(array[...,:3],0); rgb=(1-np.exp(-linear))**(1/2.2)
                image=Image.fromarray(np.uint8(np.clip(rgb,0,1)*255)).transpose(Image.Transpose.FLIP_TOP_BOTTOM)
                sheet.paste(image,(i*size[0],24)); draw.text((i*size[0]+6,6),label,fill='white')
            args.preview.parent.mkdir(parents=True,exist_ok=True); sheet.save(args.preview)
            print(args.preview)


if __name__=='__main__': main()
