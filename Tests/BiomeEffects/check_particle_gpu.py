#!/usr/bin/env python3
"""Render real biome sprite families and run the existing particle simulation.

Synthetic packed particle data provides an independent ABI/visual witness; no
production GLSL is rewritten. Blocking readbacks are test instrumentation only.
"""
from __future__ import annotations
import argparse
import ctypes as C
import math
from pathlib import Path
import struct
import sys

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'Tests/Atmosphere'))
from fixtures import Parameters,IDENTITY
from gl_support import AtmosphereGL,shader_source,RGBA32F,RG32F,TEXTURE_3D

SHADERS=ROOT/'ENDURANCE/Ressources/Shader/Environment'
SIZE=(97,97)
SETTINGS_WORDS=101

def floats(*values):return struct.pack('<%df'%len(values),*values)
def integer(value):return struct.pack('<I',value)
def quantize(value,scale):return max(0,min(65535,int(value/scale*65535)))

def payload(*,strength=1.,emission=0.,aspect=1.,position=(0.,0.,-6.),scale=1.,turbulence=0.,rotate=False,color=(.25,.5,.75,.7),velocity=(0.,0.,0.)):
    data=bytearray((SETTINGS_WORDS+8)*4)
    def f(index,*values):data[index*4:index*4+4*len(values)]=floats(*values)
    def u(index,value):data[index*4:index*4+4]=integer(value)
    f(0,10.,10.);f(2,*position);f(5,1.,0.,0.);f(8,0.)
    u(9,(1<<1)|(1<<8)|(1<<10)|((1<<6) if rotate else 0)) # box, instant births, randomized constant scale
    f(10,2.,2.,scale,scale);f(26,0.)
    u(36,2)
    f(37,*color);f(41,*color)
    f(83,.65)
    f(93,strength,emission,aspect,0.,turbulence,.12,0.,0.)
    base=SETTINGS_WORDS;f(base,*position)
    rgba=[quantize(v,64) for v in color]
    u(base+3,rgba[0]|(rgba[1]<<16));u(base+4,rgba[2]|(rgba[3]<<16));f(base+5,10.)
    v=[max(0,min(65535,int((x/256.+1)*32767.5+.5))) for x in velocity]
    u(base+6,quantize(scale,64)|(v[0]<<16));u(base+7,v[1]|(v[2]<<16))
    return bytes(data)

class Fixture:
    def __init__(self,gl):
        self.gl=gl
        self.parameters=Parameters().set('Camera',0,0,0,0)
        self.parameters.set('Art',1,1,1,.2)
        self.uniforms=self.parameters.upload(gl)
        f=1/math.tan(math.radians(75)*.5)
        projection=[f,0,0,0,0,f,0,0,0,0,-1.0002,-1,0,0,-.20002,0]
        self.camera=gl.buffer(floats(0,0,0,0,*IDENTITY,*IDENTITY,*projection),0)
        self.misc=gl.buffer(struct.pack('<i4fi',0,0.,1.,.7,75.,0),3)
        self.data=gl.buffer(payload(),6)
        self.instances=gl.buffer(struct.pack('<5I',1,0,SETTINGS_WORDS,SETTINGS_WORDS,1),7)
        self.depth=gl.texture(SIZE,internal=RG32F,value=(-100.,0.),linear=False)
        self.transmission=gl.texture((1,1,1),target=TEXTURE_3D,internal=RGBA32F,value=(1,1,1,1))
        self.scattering=gl.texture((1,1,1),target=TEXTURE_3D,internal=RGBA32F,value=(0,0,0,1))
        gl._fn('glUniform2ui',None,C.c_int,C.c_uint,C.c_uint)
    def render(self,program,*,data=None,depth=100.,sky=False,atmosphere=False,time=0.):
        gl=self.gl
        if data is not None:gl.write(self.data,data)
        gl.bind(self.data,6);gl.bind(self.instances,7);gl.bind(self.camera,0)
        gl.write(self.misc,struct.pack('<i4fi',0,time,1.,.7,75.,0));gl.bind(self.misc,3)
        self.parameters.upload(gl,self.uniforms)
        self.depth=gl.texture(SIZE,internal=RG32F,value=(-depth,-2. if sky else 0.),linear=False)
        output=gl.texture(SIZE,internal=RGBA32F,value=(0,0,0,0))
        gl.sampler(0,self.depth);gl.sampler(12,self.scattering,TEXTURE_3D);gl.sampler(13,self.transmission,TEXTURE_3D)
        gl.uniform(program,'u_count','2ui',1,0)
        gl.uniform(program,'uAtmosphereActive','1i',int(atmosphere))
        gl.uniform(program,'uEnvironmentInvResolution','2f',1/SIZE[0],1/SIZE[1])
        gl.fullscreen(program,*SIZE,[output])
        values=gl.texels(output,SIZE)
        assert all(math.isfinite(v) for v in values),'Non-finite particle output'
        return [values[i:i+4] for i in range(0,len(values),4)]

def check_rendering(gl,programs):
    fixture=Fixture(gl)
    images=[]
    for family,program in enumerate(programs):
        image=fixture.render(program,data=payload())
        covered=sum(p[3]>.001 for p in image)
        assert 20<covered<3000,(family,'Missing or oversized sprite',covered)
        assert max(p[3] for p in image)<=.701,'Alpha escaped prescribed opacity'
        assert all(p==(0.,0.,0.,0.) for p in fixture.render(program,data=payload(strength=0.))),family
        images.append(image)
    assert len({tuple(round(p[3],5) for p in image) for image in images})==16,'Sprite families lost their distinct coverage'
    print('PASS sixteen real procedural sprite kinds: distinct coverage, finite HDR, zero strength clips geometry',flush=True)
    program=programs[0]
    full=fixture.render(program,data=payload())
    soft=fixture.render(program,depth=6.2)
    blocked=fixture.render(program,depth=5.)
    sky=fixture.render(program,depth=0.,sky=True)
    assert all(p==(0.,0.,0.,0.) for p in blocked),'Opaque foreground did not occlude particles'
    assert sky==full,'Sky marker incorrectly attenuates particles'
    ratio=sum(p[3] for p in soft)/sum(p[3] for p in full)
    assert .30<ratio<.32,('Soft intersection disagrees with independent depth',ratio)
    assert all(p==(0.,0.,0.,0.) for p in fixture.render(program,data=payload(position=(0,0,-.3)))),'Near camera particle sheets were not faded'
    print('PASS opaque soft contact, sky/background rejection and near-camera overdraw fade',flush=True)
    emission=fixture.render(programs[6],data=payload(emission=4.))
    assert max(max(p[:3]) for p in emission)>5.,'Emissive particles never reach bloom HDR range'
    fixture.render(program,data=payload(turbulence=.7))
    first=fixture.render(program,time=0.)
    second=fixture.render(program,time=3.)
    assert first!=second,'Coherent vertex flutter is static'
    assert struct.unpack_from('<3f',gl.read(fixture.data,(SETTINGS_WORDS+8)*4),SETTINGS_WORDS*4)==(0.,0.,-6.),'Vertex flutter changed simulation positions'
    fixture.render(program,data=payload())
    fixture.scattering=gl.texture((1,1,1),target=TEXTURE_3D,internal=RGBA32F,value=(.1,.2,.3,1))
    fixture.transmission=gl.texture((1,1,1),target=TEXTURE_3D,internal=RGBA32F,value=(.5,.6,.7,1))
    fog=fixture.render(program,atmosphere=True)
    for original,actual in zip(full,fog):
        if original[3]>.01:
            expected=tuple(c*t+s for c,t,s in zip(original[:3],(.5,.6,.7),(.1,.2,.3)))
            assert max(abs(a-b) for a,b in zip(actual[:3],expected))<1e-6,(actual,expected)
            assert actual[3]==original[3],'Atmosphere altered transparency coverage'
    print('PASS emissive HDR, time-varying flutter, and medium S+T composition at the particle depth',flush=True)

def centroid(image):
    total=sum(p[3] for p in image)
    return (sum((i%SIZE[0])*p[3] for i,p in enumerate(image))/total,
            sum((i//SIZE[0])*p[3] for i,p in enumerate(image))/total)

def check_identity_motion(gl,programs):
    fixture=Fixture(gl)
    motions=[]
    for kind,program in enumerate(programs):
        initial=fixture.render(program,data=payload(turbulence=.6,rotate=True),time=0.)
        later=fixture.render(program,time=2.7)
        if kind in (2,10,12):
            assert initial==later,('Ballistic sprite acquired unrelated wobble',kind)
        else:
            assert initial!=later,('Kind has no characteristic motion',kind)
            a,b=centroid(initial),centroid(later)
            motions.append(tuple(round(y-x,3) for x,y in zip(a,b)))
    assert len(set(motions))>=10,'Local motions collapsed to a shared wind displacement'
    # Snow retains sixfold open branches while pollen is an asymmetric tuft.
    snow=fixture.render(programs[1],data=payload())
    pollen=fixture.render(programs[14],data=payload())
    ash=fixture.render(programs[13],data=payload())
    assert sum(abs(a[3]-b[3]) for a,b in zip(snow,pollen))>10,'Snow and pollen lost distinct silhouettes'
    assert sum(abs(a[3]-b[3]) for a,b in zip(snow,ash))>10,'Ash still uses snow coverage'
    # At distance a subpixel footprint remains finite/visible; it is attenuated
    # proportionally to projected area instead of becoming a large opaque flake.
    near=fixture.render(programs[1],data=payload(scale=.08,position=(0,0,-6)))
    far=fixture.render(programs[1],data=payload(scale=.08,position=(0,0,-60)))
    assert 0 < sum(p[3] for p in far) < sum(p[3] for p in near),'Distant particle filtering lost coverage'
    anchored=bytearray(payload(position=(-1024.,0.,-6.)))
    struct.pack_into('<3f',anchored,31*4,1024.,0.,0.)
    anchoredImage=fixture.render(programs[0],data=bytes(anchored))
    worldImage=fixture.render(programs[0],data=payload())
    assert anchoredImage==worldImage,'Local simulation anchor was not reconstructed for display'
    # Both ballistic heads must lead velocity, never sit behind their tails.
    for kind in (10,12):
        image=fixture.render(programs[kind],data=payload(velocity=(0,-20,0),emission=3.))
        peak=max(range(len(image)),key=lambda i:max(image[i][:3])*image[i][3])
        assert peak//SIZE[0] < SIZE[1]//2,('Bright head trails velocity',kind,peak//SIZE[0])
    print('PASS local anchor and leading droplet/meteor heads',flush=True)
    print('PASS per-kind motion: snow/pollen/ash identity, ballistic stability, distinct flutter and subpixel coverage',flush=True)

def write_snapshots(gl,programs,directory):
    # Debug renders of the real shaders, not external artwork or runtime assets.
    from PIL import Image,ImageDraw
    global SIZE
    SIZE=(257,257)
    names=['Mineral dust','Snow dendrite','Rain','Leaf','Petal','Winged seed','Firefly','Vapour',
           'Ember','Ice crystal','Droplet','Insect','Meteor','Ash shard','Pollen tuft','Spore']
    colors=[(.65,.48,.27),(.9,.95,1),(.58,.69,.76),(.35,.45,.16),(.85,.43,.48),(.64,.48,.24),
            (.4,.9,.2),(.60,.67,.7),(1,.18,.025),(.65,.85,1),(.6,.8,.88),(.12,.15,.08),
            (1,.33,.05),(.18,.16,.15),(.84,.75,.39),(.23,.67,.63)]
    fixture=Fixture(gl)
    directory.mkdir(parents=True,exist_ok=True)
    for time in (0.,2.7):
        sheet=Image.new('RGB',(4*SIZE[0],4*(SIZE[1]+24)),(24,30,38));draw=ImageDraw.Draw(sheet)
        for kind,program in enumerate(programs):
            rgba=fixture.render(program,data=payload(position=(0,0,-4),scale=1.35,
                color=(*colors[kind],.9),turbulence=.28,rotate=True,
                emission=2.0 if kind in (6,8,12,15) else 0.),time=time)
            rgb=[]
            for i,p in enumerate(rgba):
                background=(.12,.16,.21)
                rgb.append(tuple(int(255*max(0,min(1,((c*p[3]+b*(1-p[3]))/(1+c*p[3]+b*(1-p[3])))**(1/2.2)))) for c,b in zip(p[:3],background)))
            tile=Image.new('RGB',SIZE);tile.putdata(rgb);tile=tile.transpose(Image.Transpose.FLIP_TOP_BOTTOM)
            x=(kind%4)*SIZE[0];y=(kind//4)*(SIZE[1]+24)
            sheet.paste(tile,(x,y));draw.text((x+8,y+SIZE[1]+3),names[kind],fill=(225,230,240))
        path=directory/f'particle_kinds_t{time:g}.png';sheet.save(path);print(path,flush=True)

def check_simulation(gl):
    fixture=Fixture(gl)
    init=gl.compute(ROOT/'Asset/Shader/init_particle.C.glsl')
    advance=gl.compute(ROOT/'Asset/Shader/particle_base.C.glsl')
    gl.write(fixture.data,payload(position=(1,2,-6)))
    gl.bind(fixture.data,0)
    for name,value in (('u_offset',SETTINGS_WORDS),('u_offsetSettings',0),('u_count',1)):
        gl.uniform(init,name,'1ui',value)
    gl.dispatch(init,1)
    initial=gl.read(fixture.data,(SETTINGS_WORDS+8)*4)
    assert struct.unpack_from('<f',initial,(SETTINGS_WORDS+5)*4)[0]==-1
    assert struct.unpack_from('<I',initial,(SETTINGS_WORDS+6)*4)[0]&65535==0
    gl.bind(fixture.data,6);gl.bind(fixture.instances,7)
    gl.uniform(advance,'u_instance','3ui',1,0,1);gl.uniform(advance,'u_dt','1f',.1)
    gl.dispatch(advance,1)
    born=gl.read(fixture.data,(SETTINGS_WORDS+8)*4)
    assert struct.unpack_from('<3f',born,SETTINGS_WORDS*4)==(1.,2.,-6.)
    gl.write(fixture.data,floats(100,2,-6),2*4)
    gl.dispatch(advance,1)
    moved=gl.read(fixture.data,(SETTINGS_WORDS+8)*4)
    position=struct.unpack_from('<3f',moved,SETTINGS_WORDS*4)
    assert 1.19<position[0]<1.21 and abs(position[1]-2)<.002,('Moving emitter dragged living particles',position)
    gl.write(fixture.data,floats(-1),(SETTINGS_WORDS+5)*4)
    gl.dispatch(advance,1)
    reborn=gl.read(fixture.data,(SETTINGS_WORDS+8)*4)
    assert struct.unpack_from('<3f',reborn,SETTINGS_WORDS*4)==(100.,2.,-6.)
    print('PASS production init/base GPU simulation: delayed draw, world-anchored movement, new emitter used only at rebirth',flush=True)

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--snapshots',type=Path);args=parser.parse_args()
    with AtmosphereGL() as gl:
        print(gl.version,gl.renderer,flush=True)
        programs=[]
        for family in range(16):
            defines=f'#define GE_BIOME_PARTICLE_KIND {family}\n'
            programs.append(gl.compile([(0x8B31,shader_source(SHADERS/'particle.vs.glsl',defines)),
                                        (0x8B30,shader_source(SHADERS/'particle.fs.glsl',defines))],f'biome particle family {family}'))
        check_rendering(gl,programs)
        check_identity_motion(gl,programs)
        check_simulation(gl)
        if args.snapshots:write_snapshots(gl,programs,args.snapshots)
        gl.check('biome particle visual/simulation tests')
    print('PASS biome particle GPU tests',flush=True)

if __name__=='__main__':main()
