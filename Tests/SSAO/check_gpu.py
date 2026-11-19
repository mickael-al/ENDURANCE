#!/usr/bin/env python3
"""Actual C++/OpenGL SSAO validation: resources, geometry and lighting.

All GPU readbacks are test instrumentation. Runtime tests use SSAOBuffer.cpp,
its real buffers/textures and the production inline GLSL evaluator. Analytic scene
geometry and the old broken shader provide independent positive/negative gates.
"""
from __future__ import annotations
import ctypes as C
import math
import os
from pathlib import Path
import statistics
import struct
import subprocess
import sys
import tempfile

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'Tests/Atmosphere'))
from gl_support import R16F,R32F,RG32F,RGBA32F,TEXTURE_2D,FULLSCREEN_VERTEX,shader_source
from check_screen_composition import ScreenGL,DeferredFixture,camera,multiply

HERE=Path(__file__).resolve().parent
SHADERS=ROOT/'Asset/Shader/SSAO'
RGB16F=0x881B

class Runtime:
    def __init__(self, directory):
        library=directory/'ssao_runtime.so'
        command=[os.environ.get('CXX','c++'),'-std=c++17','-O2','-fPIC','-shared',
                 '-DGLEW_NO_GLU','-pthread','-fvisibility=hidden','-ffunction-sections','-fdata-sections']
        for path in ('Tests/SSAO/runtime_fixture','Tests/Atmosphere/runtime_fixture',
                     'ANTAGONIZ/Header/Struct','ANTAGONIZ/Header/Interface',
                     'ANTAGONIZ/Header/Rendering/Window','ANTAGONIZ/Header/Rendering/Shader',
                     'ThirdParty','ThirdParty/glm'):
            command+=['-I',str(ROOT/path)]
        for path in ('Tests/SSAO/runtime_bridge.cpp','ANTAGONIZ/Source/Rendering/Window/SSAOBuffer.cpp'):
            command.append(str(ROOT/path))
        command+=['-l:libGLEW.so.2.2','-l:libGL.so.1','-l:libEGL.so.1','-ldl',
                  '-Wl,--gc-sections','-Wl,-z,defs','-o',str(library)]
        subprocess.run(command,check=True)
        os.environ['ENDURANCE_SHADER_CACHE_DIR']=str(directory/'cache')
        self.api=C.CDLL(str(library))
        self.api.ssao_create.argtypes=[C.c_char_p]; self.api.ssao_create.restype=C.c_void_p
        self.api.ssao_initialize.argtypes=[C.c_void_p]; self.api.ssao_initialize.restype=C.c_int
        for name in ('kernel','noise'):
            function=getattr(self.api,'ssao_'+name); function.argtypes=[C.c_void_p]; function.restype=C.c_uint
        for name in ('release','destroy'):
            function=getattr(self.api,'ssao_'+name); function.argtypes=[C.c_void_p];function.restype=None
        self.handle=None
    def create(self):
        self.handle=self.api.ssao_create(str(ROOT).encode());assert self.handle,'Production SSAO initialization failed'
    def texture(self,name): return getattr(self.api,'ssao_'+name)(self.handle)
    def close(self):
        self.api.ssao_destroy(self.handle);self.handle=None

class SSAOFixture:
    def __init__(self,gl,runtime,size=(97,65),scene='corner',distance=3.,pitch=0.,yaw=0.,projection_kind='perspective'):
        self.gl,self.runtime,self.size=gl,runtime,size
        self.positions=[];self.view_normals=[];self.world_normals=[]
        w,h=size
        origin,rotation,view,inverse,projection=camera(pitch,yaw,w/h,(5312.,800.,-4192.))
        if projection_kind=='offcentre': projection[8]=.137;projection[9]=-.21
        elif projection_kind=='wide':
            projection[0]=1/math.tan(math.radians(120)/2)/(w/h)
            projection[5]=1/math.tan(math.radians(120)/2)
        elif projection_kind.startswith('ortho'):
            span=10000. if projection_kind=='ortho_far' else 2.
            projection=[1/span,0,0,0,0,-1/span,0,0,0,0,-2/100000,0,.13,-.2,-1,1]
        self.view,self.inverse,self.projection=view,inverse,projection
        self.camera=gl.buffer(struct.pack('<52f',*origin,0,*view,*inverse,*projection),0)
        for y in range(h):
            for x in range(w):
                ndc=(2*(x+.5)/w-1,2*(y+.5)/h-1)
                if projection_kind.startswith('ortho'):
                    ray=(0.,0.,-1.)
                    ray_origin=((ndc[0]-projection[12])/projection[0],(ndc[1]-projection[13])/projection[5],0.)
                else:
                    ray=((ndc[0]+projection[8])/projection[0],(ndc[1]+projection[9])/projection[5],-1.)
                    ray_origin=(0.,0.,0.)
                t=distance;normal=(0.,0.,1.);tag=0.
                if scene=='oblique':
                    normal=(.45,.25,1.);length=math.sqrt(sum(v*v for v in normal));normal=tuple(v/length for v in normal)
                    denominator=sum(a*b for a,b in zip(normal,ray))
                    t=(-normal[2]*distance-sum(a*b for a,b in zip(normal,ray_origin)))/denominator
                    if t<=0: t=0.;tag=-2.;normal=(0.,0.,0.)
                if scene=='corner' and ray[1]<0 and -.7/ray[1]<t:
                    t=-.7/ray[1];normal=(0.,1.,0.)
                if scene=='step' and x<w//2: t=distance-2.
                if scene=='sky' or (scene=='mixed' and x>w*3//4): t=0.;tag=-2.;normal=(0.,0.,0.)
                self.positions.append((ray_origin[0]+ray[0]*t,ray_origin[1]+ray[1]*t,-t,tag))
                self.view_normals.append(normal)
                self.world_normals.append(multiply(rotation,normal)+(1.,))
        self.position=gl.field_texture(size,[(p[2],p[3]) for p in self.positions],internal=RG32F,repeat=True)
        self.normal=gl.field_texture(size,self.world_normals,internal=RGB16F,repeat=True)
    def render(self):
        return evaluate_inline(self.gl,self)


def finite_ao(values,label):
    assert all(math.isfinite(v) and 0<=v<=1 for v in values),(label,min(values),max(values))


def inspect_resources(gl,runtime):
    gl._fn('glGetBufferParameteriv',None,C.c_uint,C.c_uint,C.POINTER(C.c_int))
    gl._fn('glGetTextureLevelParameteriv',None,C.c_uint,C.c_int,C.c_uint,C.POINTER(C.c_int))
    gl._fn('glIsTexture',C.c_ubyte,C.c_uint);gl._fn('glIsBuffer',C.c_ubyte,C.c_uint)
    kernel=runtime.texture('kernel'); noise=runtime.texture('noise')
    gl.glBindBuffer(0x90D2,kernel);length=C.c_int();gl.glGetBufferParameteriv(0x90D2,0x8764,C.byref(length))
    assert length.value==64*16,('64 std430 samples need1024 bytes',length.value)
    data=struct.unpack('<256f',gl.read(kernel,1024))
    samples=[data[i:i+4] for i in range(0,256,4)]
    assert all(math.isfinite(v) for v in data)
    assert all(s[2]>=0 and s[3]==0 and 0<sum(v*v for v in s[:3])<=1.001 for s in samples)
    # Read actual shader array indexing, not just CPU byte counts.
    program=gl.compile([(0x91B9,'''#version 450 core
layout(local_size_x=64) in;
layout(std430,binding=4) readonly buffer Input { vec4 samples[]; };
layout(std430,binding=0) writeonly buffer Output { vec4 copied[]; };
void main(){uint i=gl_LocalInvocationIndex;copied[i]=samples[i];}
''')],'actual SSAO std430 roundtrip')
    output=gl.buffer(1024,0);gl.bind(kernel,4);gl.dispatch(program,1)
    assert gl.read(output,1024)==struct.pack('<256f',*data),'SSAO kernel does not survive actual GLSL indexing'
    fmt=C.c_int();gl.glGetTextureLevelParameteriv(noise,0,0x1003,C.byref(fmt));assert fmt.value==RG32F
    values=gl.texels(noise,(4,4),channels=2)
    assert all(abs(values[i]**2+values[i+1]**2-1)<1e-5 for i in range(0,32,2))
    print('PASS real C++ kernel/noise: 64 vec4 FP32 samples, 1024-byte actual SSBO, exact GPU indexing, normalized RG32F noise',flush=True)
    return data


def check_geometry(gl,runtime):
    for pitch,yaw in ((0,0),(0,90),(0,180),(72,35),(-68,-40)):
        f=SSAOFixture(gl,runtime,scene='plane',pitch=pitch,yaw=yaw)
        values=f.render();finite_ao(values,'plane')
        assert min(values)>.999,('Unobstructed plane self-occluded',pitch,yaw,min(values))
    print('PASS unobstructed planes at five camera rotations: no mixed-space self-occlusion',flush=True)
    corner=SSAOFixture(gl,runtime)
    values=corner.render();finite_ao(values,'corner')
    contact=[i for i,p in enumerate(corner.positions) if abs(p[1]+.7)<.08 and abs(p[2]+3)<.12 and abs(p[0])<1]
    far=[i for i,p in enumerate(corner.positions) if p[1]>.3 and abs(p[0])<1]
    assert contact and far
    assert statistics.mean(values[i] for i in contact)<.92,('Corner contact disappeared',min(values))
    assert min(values[i] for i in far)>.98,('AO leaked beyond radius',min(values[i] for i in far))
    for scene in ('step','sky','mixed'):
        f=SSAOFixture(gl,runtime,scene=scene,distance=5.)
        values=f.render();finite_ao(values,scene)
        if scene in ('step','sky'): assert min(values)>.999,('Distant layer/sky occludes',scene,min(values))
        for i,p in enumerate(f.positions):
            if p[3]==-2: assert values[i]==1.,'Sky receives SSAO'
    print('PASS real corner contact, finite AO [0,1], distant depth discontinuity and neutral sky',flush=True)


def evaluate_inline(gl,fixture,disable_skip=False):
    if not hasattr(gl,'ssao_probes'): gl.ssao_probes={}
    if disable_skip not in gl.ssao_probes:
        source="""#version 450 core
layout(std430,binding=0) readonly buffer Camera { vec3 camPos;mat4 view;mat4 invView;mat4 proj; } ubc;
layout(std430,binding=4) readonly buffer Samples { vec4 samples[]; } ubssao;
layout(binding=0) uniform sampler2D gPosition;
layout(binding=1) uniform sampler2D gNormal;
layout(binding=5) uniform sampler2D texNoise;
out float result;
"""+('#define GE_SSAO_DISABLE_SUBPIXEL_SKIP 1\n' if disable_skip else '')
        source+=shader_source(ROOT/'Asset/Shader/Geometry/gbuffer_position.glsl')+'\n'
        source+=shader_source(SHADERS/'evaluate.glsl')+"""
void main() {
 ivec2 pixel=ivec2(gl_FragCoord.xy);
 vec4 position=geGBufferReadPosition(gPosition,pixel,ubc.proj);
 if(position.w==-2.0 || position.w>0.0) { result=1.0;return; }
 vec3 normal=mat3(ubc.view)*texelFetch(gNormal,pixel,0).xyz;
 result=geSsaoEvaluate(position.xyz,normal,pixel);
}
"""
        gl.ssao_probes[disable_skip]=gl.compile([(0x8B31,FULLSCREEN_VERTEX),(0x8B30,source)],'production inline SSAO'+(' full64 reference' if disable_skip else ''))
    program=gl.ssao_probes[disable_skip]
    output=gl.texture(fixture.size,internal=R32F)
    gl.bind(fixture.camera,0);gl.bind(fixture.runtime.texture('kernel'),4)
    gl.sampler(0,fixture.position);gl.sampler(1,fixture.normal);gl.sampler(5,fixture.runtime.texture('noise'))
    gl.fullscreen(program,*fixture.size,[output])
    return gl.texels(output,fixture.size,channels=1)


def raw_reference(gl,fixture): return evaluate_inline(gl,fixture,disable_skip=True)


def check_fast_paths(gl,runtime):
    for distance in (.1,1.,3.,20.,100.,1000.,40000.):
        for scene in ('plane','corner','mixed'):
            f=SSAOFixture(gl,runtime,scene=scene,distance=distance)
            current=f.render()
            reference=raw_reference(gl,f)
            assert current==reference,('Subpixel skip changed AO',distance,scene,max(abs(a-b) for a,b in zip(current,reference)))
    for projection in ('offcentre','wide','ortho','ortho_far'):
        for distance in (.1,3.,100.,40000.):
            f=SSAOFixture(gl,runtime,scene='oblique',distance=distance,projection_kind=projection)
            current=f.render();reference=raw_reference(gl,f)
            assert current==reference,('Oblique/projection skip changed AO',projection,distance)
    f=SSAOFixture(gl,runtime,scene='plane')
    baseline=f.render();w,h=f.size
    rows=[(p[2]+.25 if x<4 else p[2],p[3]) for i,p in enumerate(f.positions) for x in (i%w,)]
    f.position=gl.field_texture(f.size,rows,internal=RG32F,repeat=True)
    modified=f.render()
    right=[y*w+x for y in range(h) for x in range(w-5,w)]
    assert all(modified[i]==baseline[i] for i in right),'Opposite-edge geometry wrapped into SSAO'
    for kind in ('depth_nan','normal_nan','zero_normal','emissive'):
        f=SSAOFixture(gl,runtime,scene='plane',distance=.1)
        if kind=='depth_nan': f.position=gl.field_texture(f.size,[(float('nan'),0.)]*(w*h),internal=RG32F,repeat=True)
        elif kind=='emissive': f.position=gl.field_texture(f.size,[(-.1,2.)]*(w*h),internal=RG32F,repeat=True)
        else:
            value=(float('nan'),0,0,1) if kind=='normal_nan' else (0,0,0,1)
            f.normal=gl.field_texture(f.size,[value]*(w*h),internal=RGB16F,repeat=True)
        values=f.render()
        assert set(values)=={1.},('Invalid/emissive data did not remain neutral',kind)
    print('PASS conservative subpixel optimization equals all 64 samples over 37 scenes, including oblique/off-centre/orthographic; no screen wrapping; neutral invalid/emissive input',flush=True)


def check_lighting(gl):
    vertex=shader_source(ROOT/'Asset/Shader/forward.vs.glsl')
    fragment=shader_source(ROOT/'Asset/Shader/forward.fs.glsl')
    def overridden(value):
        # Only replace the evaluator at its call site; every actual PBR and
        # composition equation remains production code.
        return fragment.replace('void main()',f'#define geSsaoEvaluate(p,n,pixel) {value}.0\nvoid main()',1)
    programs={value:gl.compile([(0x8B31,vertex),(0x8B30,overridden(value))],f'actual deferred with AO={value}') for value in (0,1)}
    from gl_support import TEXTURE_CUBE_MAP
    size=(23,15)
    fixture=DeferredFixture(gl,size,0,0,'opaque',camera_position=(0.,0.,0.))
    white=gl.texture((1,1),internal=R16F,value=1.)
    black=gl.texture((1,1),internal=R16F,value=0.)
    neutral_irradiance=fixture.samplers[6]; neutral_prefilter=fixture.samplers[7]
    dark_cube=gl.texture((16,16),target=TEXTURE_CUBE_MAP,levels=5,value=(0,0,0,1))
    light=bytearray(80)
    struct.pack_into('<3f',light,16,4.,3.,2.)
    struct.pack_into('<3f',light,32,0.,0.,-1.)
    struct.pack_into('<2f4i',light,44,1000.,90.,0,-1,-1,-1)
    gl.buffer(bytes(light),1)
    def render(ao):
        fixture.samplers[5]=(ao,TEXTURE_2D)
        return fixture.render(programs[1 if ao==white else 0],False)
    gl.buffer(struct.pack('<i4fi',1,0.,1.,0.,75.,0),2)
    fixture.samplers[6]=(dark_cube,TEXTURE_CUBE_MAP)
    fixture.samplers[7]=(dark_cube,TEXTURE_CUBE_MAP)
    direct=render(white)
    assert max(max(p) for p in direct)>.1,'Direct-light fixture is dark'
    assert render(black)==direct,'SSAO darkens direct light'
    fixture.samplers[3]=(gl.field_texture(size,[(.3,.5,0.,1.)]*(size[0]*size[1]),repeat=True),TEXTURE_2D)
    assert render(white)==direct,'Material AO darkens direct light'
    fixture.samplers[3]=(gl.field_texture(size,[(.3,.5,1.,1.)]*(size[0]*size[1]),repeat=True),TEXTURE_2D)
    gl.buffer(struct.pack('<i4fi',0,0.,1.,1.,75.,0),2)
    fixture.samplers[6]=neutral_irradiance;fixture.samplers[7]=neutral_prefilter
    ambient=render(white)
    assert min(min(p) for p in ambient)>0,'Ambient fixture is dark'
    assert max(max(p) for p in render(black))==0,'AO zero failed to occlude ambient light'
    for mode in ('sky','emissive'):
        fixture=DeferredFixture(gl,size,0,0,'sky' if mode=='sky' else 'opaque',camera_position=(0.,0.,0.))
        if mode=='emissive':
            fixture.samplers[0]=(gl.field_texture(size,[(-3.,2.)]*(size[0]*size[1]),internal=RG32F,repeat=True),TEXTURE_2D)
        assert render(white)==render(black),(mode,'was darkened by SSAO')
    print('PASS real deferred lighting: AO only attenuates ambient; direct light/material AO, sky and emission independent; original lighting equations exercised',flush=True)


def check_forward_geometry(gl,runtime):
    vertex=shader_source(ROOT/'Asset/Shader/forward.vs.glsl')
    fragment=shader_source(ROOT/'Asset/Shader/forward.fs.glsl')
    production=gl.compile([(0x8B31,vertex),(0x8B30,fragment)],'actual deferred SSAO geometry')
    neutral=gl.compile([(0x8B31,vertex),(0x8B30,fragment.replace(
        'void main()','#define geSsaoEvaluate(p,n,pixel) 1.0\nvoid main()',1))],
        'actual deferred without AO attenuation')
    for scene,pitch,yaw in [('plane',*angles) for angles in ((0,0),(0,90),(0,180),(72,35),(-68,-40))]+[('corner',0,0)]:
        field=SSAOFixture(gl,runtime,scene=scene,pitch=pitch,yaw=yaw)
        fixture=DeferredFixture(gl,field.size,pitch,yaw,'opaque',camera_position=(5312.,800.,-4192.))
        fixture.camera_buffer=field.camera
        fixture.samplers[0]=(field.position,TEXTURE_2D)
        fixture.samplers[1]=(field.normal,TEXTURE_2D)
        fixture.samplers[5]=(runtime.texture('noise'),TEXTURE_2D)
        gl.bind(runtime.texture('kernel'),4)
        baseline=fixture.render(neutral,False)
        actual=fixture.render(production,False)
        ao=field.render()
        error=max(abs(value-base*occlusion) for pixel,reference,occlusion in zip(actual,baseline,ao)
                  for value,base in zip(pixel,reference))
        assert error<1e-5,('Forward caller disagrees with material-normal AO',scene,pitch,yaw,error)
        if scene=='corner':
            contact=[i for i,p in enumerate(field.positions) if abs(p[1]+.7)<.08 and abs(p[2]+3)<.12 and abs(p[0])<1]
            ratio=statistics.mean(sum(actual[i])/sum(baseline[i]) for i in contact)
            assert ratio<.92,('Actual forward pass lost corner contact',ratio)
        else:
            assert actual==baseline,('Actual forward plane self-occludes',pitch,yaw)
            # Encodes the opposite primitive normal. Smooth/material normals
            # must remain authoritative for AO, regardless of shadow-bias tags.
            fixture.samplers[0]=(gl.field_texture(field.size,
                [(p[2],-1048579.) for p in field.positions],internal=RG32F,repeat=True),TEXTURE_2D)
            gl.bind(runtime.texture('kernel'),4)
            assert fixture.render(production,False)==actual,'Primitive normal replaced smooth/material SSAO normal'
    print('PASS actual forward caller: five camera rotations, real corner contact and smooth normals preserved across primitive-normal tags',flush=True)


def check_legacy_witness(gl):
    legacy=(HERE/'legacy_ssao.glsl').read_text()
    source="""#version 450 core
in vec2 TexCoords;
#define v_UV TexCoords
layout(binding=0) uniform sampler2D gPosition;
layout(binding=5) uniform sampler2D texNoise;
layout(std430,binding=4) readonly buffer Kernel { vec3 samples[]; } ubssao;
layout(std430,binding=0) readonly buffer Camera { mat4 proj; } ubc;
uniform vec3 surfaceNormal;
out vec4 result;
"""+legacy+"""
void main(){result=vec4(SSAO(vec3(0,0,-3),surfaceNormal),0,0,1);}
"""
    program=gl.compile([(0x8B31,FULLSCREEN_VERTEX),(0x8B30,source)],'frozen original SSAO defect')
    positions=gl.texture((17,11),internal=RG32F,value=(-3.,0.),linear=False)
    noise=gl.texture((4,4),internal=RGBA32F,value=(.3,.8,0.,0.),linear=False)
    output=gl.texture((1,1),internal=RGBA32F)
    f=1/math.tan(math.radians(75)/2)
    gl.buffer(struct.pack('<16f',f,0,0,0,0,f,0,0,0,0,-1.0002,-1,0,0,-.20002,0),0)
    kernel=[]
    for i in range(64):
        z=(i+.5)/64;phi=i*2.399963229728653;r=math.sqrt(1-z*z)
        kernel.extend((r*math.cos(phi),r*math.sin(phi),z,0))
    gl.buffer(struct.pack('<256f',*kernel),4)
    gl.sampler(0,positions);gl.sampler(5,noise)
    readings=[]
    for normal in ((0,0,1),(1,0,0),(0,0,-1)):
        gl.uniform(program,'surfaceNormal','3f',*normal)
        gl.fullscreen(program,1,1,[output]);readings.append(gl.texels(output,(1,1))[0])
    assert readings[0]==1 and readings[1]<.2 and readings[2]<0,('Frozen mixed-space/double-count witness stoppedfailing',readings)
    shader="""#version 450 core
layout(local_size_x=1) in;
layout(std430,binding=0) readonly buffer Input { vec3 samples[]; };
layout(std430,binding=1) writeonly buffer Output { vec4 values[]; };
void main(){values[0]=vec4(samples[0],float(samples.length()));values[1]=vec4(samples[1],0);}
"""
    probe=gl.compile([(0x91B9,shader)],'former packed vec3 stride negative control')
    gl.buffer(struct.pack('<192f',*range(1,193)),0)
    copied=gl.buffer(32,1);gl.dispatch(probe,1)
    values=struct.unpack('<8f',gl.read(copied,32))
    assert values==(1.,2.,3.,48.,5.,6.,7.,0.),values
    print(f'PASS frozen negative controls: plane AO {readings}, packed 64 vec3 exposes 48 GPU entries and wrong stride',flush=True)


def check_lifecycle(gl,runtime,original_kernel):
    original_noise=gl.texels(runtime.texture('noise'),(4,4),channels=2)
    for cycle in range(3):
        noise=runtime.texture('noise');kernel=runtime.texture('kernel')
        runtime.api.ssao_release(runtime.handle)
        assert not gl.glIsBuffer(kernel) and not gl.glIsTexture(noise),'Release leaked SSAO objects'
        assert runtime.texture('kernel')==runtime.texture('noise')==0
        runtime.api.ssao_release(runtime.handle);gl.check('double SSAO release')
        assert runtime.api.ssao_initialize(runtime.handle)==1
        assert struct.unpack('<256f',gl.read(runtime.texture('kernel'),1024))==original_kernel,'Restart appends or changes kernel'
        assert gl.texels(runtime.texture('noise'),(4,4),channels=2)==original_noise,'Restart changes noise'
    assert runtime.api.ssao_initialize(runtime.handle)==1
    assert struct.unpack('<256f',gl.read(runtime.texture('kernel'),1024))==original_kernel,'Repeated initialize corrupts kernel'
    f=SSAOFixture(gl,runtime,size=(9,7),scene='plane');values=f.render()
    assert set(values)=={1.},'Rendering after repeated initialize failed'
    print('PASS real C++ lifecycle: resource deletion, repeated release/initialize and deterministic kernel/noise',flush=True)


def main():
    with tempfile.TemporaryDirectory(prefix='endurance-ssao-test-') as temporary:
        runtime=Runtime(Path(temporary))
        with ScreenGL() as gl:
            print(f'OpenGL {gl.version}; renderer: {gl.renderer}',flush=True)
            runtime.create()
            try:
                original_kernel=inspect_resources(gl,runtime)
                check_geometry(gl,runtime)
                check_fast_paths(gl,runtime)
                check_lighting(gl)
                check_forward_geometry(gl,runtime)
                check_legacy_witness(gl)
                check_lifecycle(gl,runtime,original_kernel)
                gl.check('SSAO validation')
            finally: runtime.close()
    print('PASS SSAO production runtime and geometry',flush=True)

if __name__=='__main__':main()
