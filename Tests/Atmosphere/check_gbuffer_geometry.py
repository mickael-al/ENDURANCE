#!/usr/bin/env python3
"""Validate the real geometric-normal tag through an RG32F framebuffer.

Roundtrip references come from independent 3D directions, not another oct
encoder. Reserved/malformed tags and degenerate derivatives must stay legacy.
The final checks render the real opaque material shader, including emission
and alpha discard. All readbacks are test instrumentation.
"""
import ctypes as C
import math
import random
import struct

from gl_support import AtmosphereGL, FULLSCREEN_VERTEX, RGBA32F, RG32F, ROOT, shader_source

HELPER = ROOT / "Asset/Shader/Geometry/gbuffer_geometry.glsl"
ENCODE = """#version 450 core
//__HELPER__
struct Input { vec4 dx; vec4 dy; };
layout(std430,binding=0) readonly buffer Inputs { Input samples[]; };
uniform uint width;
layout(location=0) out vec2 position;
void main() {
    uint i=uint(gl_FragCoord.x)+uint(gl_FragCoord.y)*width;
    position=vec2(0,geGBufferGeometryTag(samples[i].dx.xyz,samples[i].dy.xyz));
}
"""
DECODE = """#version 450 core
//__HELPER__
layout(binding=0) uniform sampler2D positions;
layout(location=0) out vec4 result;
void main() {
    vec3 normal;
    bool valid=geGBufferGeometricNormal(texelFetch(positions,ivec2(gl_FragCoord.xy),0).g,normal);
    result=vec4(normal,valid?1.0:0.0);
}
"""


def unit(value):
    length=math.sqrt(sum(v*v for v in value))
    return tuple(v/length for v in value)


def cross(a,b):
    return (a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0])


def basis(normal):
    axis=(0.,0.,1.) if abs(normal[2])<.9 else (0.,1.,0.)
    dx=unit(cross(axis,normal))
    return dx,cross(normal,dx)


def read_normals(gl,program,texture,size):
    output=gl.texture(size,internal=RGBA32F,linear=False)
    gl.sampler(0,texture)
    gl.fullscreen(program,*size,[output])
    return gl.texels(output,size)


def check_roundtrip(gl,encoder,decoder):
    rng=random.Random(0xE0D20)
    directions=[]
    for _ in range(32768):
        z=rng.uniform(-1.,1.)
        angle=rng.uniform(-math.pi,math.pi)
        r=math.sqrt(1.-z*z)
        directions.append((r*math.cos(angle),r*math.sin(angle),z))
    # Axis, fold and pole cases include zero components where sign(0) would
    # incorrectly collapse the negative hemisphere in a conventional fold.
    for x in (-1.,-1e-7,0.,1e-7,1.):
        for y in (-1.,-1e-7,0.,1e-7,1.):
            for z in (-1.,-1e-7,0.,1e-7,1.):
                if x or y or z:
                    directions.append(unit((x,y,z)))
    samples=[]
    for direction in directions:
        dx,dy=basis(direction)
        samples.append((dx,dy,direction))
    for direction in directions[-124:]:
        dx,dy=basis(direction)
        for sx,sy in ((1e-30,1e-30),(1e30,1e30),(1e-30,1e30),(1e30,1e-30)):
            samples.append((tuple(v*sx for v in dx),tuple(v*sy for v in dy),direction))
    valid_count=len(samples)
    for dx,dy in (((0,0,0),(1,0,0)),((1,0,0),(0,0,0)),((1,2,3),(2,4,6)),
                  ((1,0,0),(1,1e-9,0)),((float('nan'),0,0),(0,1,0)),
                  ((float('inf'),0,0),(0,1,0)),((1,0,0),(0,-float('inf'),0))):
        samples.append((dx,dy,None))
    count=len(samples)
    size=(256,(count+255)//256)
    samples.extend([((0,0,0),(0,0,0),None)]*(size[0]*size[1]-count))
    payload=b''.join(struct.pack('<8f',*dx,0.,*dy,0.) for dx,dy,_ in samples)
    gl.buffer(payload,0)
    texture=gl.texture(size,internal=RG32F,linear=False)
    gl.uniform(encoder,'width','1ui',size[0])
    gl.fullscreen(encoder,*size,[texture])
    tags=gl.texels(texture,size,channels=2)[1::2]
    output=read_normals(gl,decoder,texture,size)
    maximum=0.
    for i,(_,_,expected) in enumerate(samples):
        normal=output[4*i:4*i+3]
        accepted=output[4*i+3]
        if expected is None:
            assert tags[i]==0. and accepted==0. and normal==(0.,0.,0.), ('degenerate',i,tags[i],normal)
            continue
        assert accepted==1., ('valid normal rejected',i,tags[i])
        assert -1048579.<=tags[i]<=-4. and tags[i]==math.floor(tags[i]), ('tag range/exactness',i,tags[i])
        error=math.sqrt(sum((a-b)**2 for a,b in zip(normal,expected)))
        maximum=max(maximum,error)
        assert error<.005, ('oct20 roundtrip error',i,error,expected,normal)
        assert abs(sum(v*v for v in normal)-1.)<2e-6, ('unit normal',i,normal)
    print(f'PASS {valid_count} normals through RG32F: maximum vector error={maximum:.7f}; axes/folds, scales1e-30..1e30, degenerate/invalid derivatives',flush=True)


def check_tags(gl,decoder):
    invalid=(0.,-0.,-1.,-2.,-3.,1e-30,.1,1.,65536.,-3.999,-4.5,-1048578.5,-1048580.,
             -1e30,float('inf'),-float('inf'),float('nan'))
    valid=(-4.,-5.,-1027.,-1028.,-524804.,-1048579.)
    tags=invalid+valid
    size=(len(tags),1)
    texture=gl.texture(size,internal=RG32F,linear=False)
    data=struct.pack('<'+str(2*len(tags))+'f',*(v for tag in tags for v in (0.,tag)))
    gl.glBindTexture(0x0DE1,texture)
    gl.glTexImage2D(0x0DE1,0,RG32F,*size,0,0x8227,0x1406,C.c_char_p(data))
    output=read_normals(gl,decoder,texture,size)
    for i in range(len(tags)):
        normal,accepted=output[4*i:4*i+3],output[4*i+3]
        assert all(math.isfinite(v) for v in normal)
        assert accepted==(0. if i<len(invalid) else 1.), ('tag validation',tags[i],accepted)
        if i<len(invalid): assert normal==(0.,0.,0.)
    print('PASS legacy0, sky-2, positive emission, fractional/out-of-range/NaN/inf tags; valid interval endpoints',flush=True)


def check_opaque(gl,decoder):
    # Reuse the existing GL draw utility; shader implementations are unchanged
    # inputs to this test, not copied or replaced by a synthetic writer.
    from check_position_precision import draw_opaque, ortho, WIDTH, HEIGHT, EXTENT_X, EXTENT_Y, SLOPE_X, SLOPE_Y
    shaders=ROOT/'Asset/Shader'
    opaque=gl.compile([(0x8B31,shader_source(shaders/'opaque.vs.glsl')),
                       (0x8B30,shader_source(shaders/'opaque.fs.glsl'))], 'actual opaque geometry-tag writer')
    for name in ('opaque_skinned','opaque_cloth'):
        gl.compile([(0x8B31,shader_source(shaders/f'{name}.vs.glsl')),
                    (0x8B30,shader_source(shaders/'opaque.fs.glsl'))], f'actual {name} geometry-tag linkage')
    size=(WIDTH,HEIGHT)
    identity=((1.,0.,0.,0.),(0.,1.,0.,0.),(0.,0.,1.,0.),(0.,0.,0.,1.))
    positions=[(x,y,-100+SLOPE_X*x+SLOPE_Y*y) for x,y in
               ((-EXTENT_X,-EXTENT_Y),(3*EXTENT_X,-EXTENT_Y),(-EXTENT_X,3*EXTENT_Y))]
    outputs=[gl.texture(size,internal=fmt,linear=False) for fmt in (RG32F,RGBA32F,RGBA32F,RGBA32F)]
    material=[gl.texture((1,1),internal=RGBA32F,value=v,linear=False) for v in
              ((1,1,1,1),(1,.5,.5,1),(0,0,0,0),(1,1,1,1),(1,1,0,0))]
    for unit_index,texture in enumerate(material): gl.sampler(unit_index,texture)
    framebuffer_index=len(gl.framebuffers)
    draw_opaque(gl,opaque,positions,identity,identity,
                ortho(-EXTENT_X,EXTENT_X,-EXTENT_Y,EXTENT_Y,.1,1000),outputs)
    framebuffer=gl.framebuffers[framebuffer_index]
    array=C.c_int(); gl.glGetIntegerv(0x85B5,C.byref(array))
    gl._fn('glGetIntegeri_v',None,C.c_uint,C.c_uint,C.POINTER(C.c_int))
    material_buffer=C.c_int(); gl.glGetIntegeri_v(0x90D3,2,C.byref(material_buffer))
    tags=gl.texels(outputs[0],size,channels=2)[1::2]
    normals=read_normals(gl,decoder,outputs[0],size)
    expected=unit((-SLOPE_X,-SLOPE_Y,1.))
    for i,tag in enumerate(tags):
        assert tag<=-4. and normals[4*i+3]==1.
        assert math.sqrt(sum((a-b)**2 for a,b in zip(normals[4*i:4*i+3],expected)))<.005
    shading=gl.texels(outputs[1],size)
    assert abs(sum(a*b for a,b in zip(shading[:3],expected)))<.01, 'Normal-map independence fixture is ineffective'

    def redraw():
        for unit_index,texture in enumerate(material): gl.sampler(unit_index,texture)
        gl.glBindFramebuffer(0x8D40,framebuffer)
        gl.glViewport(0,0,*size)
        gl.glBindVertexArray(array.value)
        gl.glUseProgram(opaque)
        gl.glDrawArrays(0x0004,0,3)
        gl.glBindFramebuffer(0x8D40,0)

    for emission in (2.75,-3.):
        gl.write(material_buffer.value,struct.pack('<f',emission),48)
        redraw()
        written=gl.texels(outputs[0],size,channels=2)[1::2]
        assert all(v==emission for v in written) if emission>0 else written==tags
    gl._fn('glClearColor',None,C.c_float,C.c_float,C.c_float,C.c_float)
    gl._fn('glClear',None,C.c_uint)
    gl.glBindFramebuffer(0x8D40,framebuffer)
    gl.glClearColor(0,-2,0,0)
    gl.glClear(0x00004000)
    gl.glBindFramebuffer(0x8D40,0)
    material[0]=gl.texture((1,1),internal=RGBA32F,value=(1,1,1,0),linear=False)
    redraw()
    assert all(v==-2. for v in gl.texels(outputs[0],size,channels=2)[1::2]), 'Alpha discard overwrote the background tag'
    print('PASS real opaque/skinned/cloth linkage; primitive normal independent of normal map; positive emission preserved, nonpositive emission tagged, alpha discard intact',flush=True)


def main():
    helper=shader_source(HELPER)
    with AtmosphereGL() as gl:
        print(f'OpenGL {gl.version}; renderer: {gl.renderer}',flush=True)
        encoder=gl.compile([(0x8B31,FULLSCREEN_VERTEX),(0x8B30,ENCODE.replace('//__HELPER__',helper))], 'production geometry tag encoding')
        decoder=gl.compile([(0x8B31,FULLSCREEN_VERTEX),(0x8B30,DECODE.replace('//__HELPER__',helper))], 'production geometry tag decoding')
        check_roundtrip(gl,encoder,decoder)
        check_tags(gl,decoder)
        check_opaque(gl,decoder)
        gl.check('geometric GBuffer normal contract')


if __name__=='__main__':
    main()
