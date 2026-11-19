#!/usr/bin/env python3
"""Measure actual opaque G-buffer position quantization on inclined planes.

The production opaque VS/FS write RG32F depth and metadata. Frozen-layout
RGBA16F/RGBA32F writer variants retain the former XYZ encoding for comparison. A
diagnostic fragment pass reconstructs world position and shadow depth exactly
as deferred lighting does. The independent analytic shadow plane removes PCF
sampling error. Its signed receiver-depth error can cause false occlusion or
detached shadows; it is not a request for a distance-scaled bias. The production
helper supplies the numerical error allowance and the texel-scaled geometric bias. Readbacks are test instrumentation.

The 40 km case deliberately keeps a compact 256 m shadow radius: it stresses
absolute-coordinate precision rather than reproducing a far perspective
cascade's footprint. Numeric position error is measured separately from the
production geometric bias. The 2 m and 4 m blockers span eight/sixteen shadow
texels along the light; both must remain fully occluded while the receiver
stays lit. Subtexel contact accuracy is not claimed by this test.
"""
from __future__ import annotations

import ctypes as C
import math
import re
import struct

from gl_support import AtmosphereGL, ROOT, RGBA16F, RGBA32F, RG32F, FULLSCREEN_VERTEX, TEXTURE_2D_ARRAY
from gl_headless import shader_source

WIDTH, HEIGHT = 96, 64
EXTENT_X, EXTENT_Y = 32.0, 24.0
SLOPE_X, SLOPE_Y = 0.371, 0.193
SHADOW_RADIUS, CASTER_MARGIN = 256.0, 16000.0
SHADOW_SPAN = 2 * SHADOW_RADIUS + CASTER_MARGIN
SHADOW_SIZE = 2048
OBSTACLE_GAPS = (0., 2., 4.)  # Metres measured along the light direction.


def dot(a, b):
    return sum(x * y for x, y in zip(a, b))


def cross(a, b):
    return (a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0])


def normalized(v):
    length = math.sqrt(dot(v, v))
    return tuple(x / length for x in v)


def apply(rows, value):
    return tuple(dot(row, value) for row in rows)


def multiply(a, b):
    return tuple(tuple(sum(a[y][i]*b[i][x] for i in range(4)) for x in range(4)) for y in range(4))


def column_major(rows):
    return tuple(rows[y][x] for x in range(4) for y in range(4))


def transform_point(matrix, point):
    value = apply(matrix, (*point, 1.0))
    return tuple(x/value[3] for x in value[:3])


def inverse_rigid(matrix):
    result = [[matrix[x][y] for x in range(3)] + [0.] for y in range(3)]
    translation = [matrix[i][3] for i in range(3)]
    for y in range(3):
        result[y][3] = -dot(result[y][:3], translation)
    return tuple(tuple(row) for row in result) + ((0., 0., 0., 1.),)


def ortho(left, right, bottom, top, near, far):
    return ((2/(right-left), 0., 0., -(right+left)/(right-left)),
            (0., 2/(top-bottom), 0., -(top+bottom)/(top-bottom)),
            (0., 0., -2/(far-near), -(far+near)/(far-near)),
            (0., 0., 0., 1.))


def camera_transform():
    yaw, roll = .37, .18
    cy, sy, cr, sr = math.cos(yaw), math.sin(yaw), math.cos(roll), math.sin(roll)
    return ((cy*cr, -cy*sr, sy, 1234.5),
            (sr, cr, 0., 678.25),
            (-sy*cr, sy*sr, cy, -321.75),
            (0., 0., 0., 1.))


def shadow_fixture(inverse_view, distance):
    center = transform_point(inverse_view, (0., 0., -distance))
    direction = normalized((-.3, -.6, -.74))
    right = normalized(cross(direction, (0., 1., 0.)))
    up = cross(right, direction)
    eye = tuple(p-d*(SHADOW_RADIUS+CASTER_MARGIN) for p, d in zip(center, direction))
    rows = (right, up, tuple(-x for x in direction))
    view = tuple(tuple(row)+(-dot(row, eye),) for row in rows) + ((0., 0., 0., 1.),)
    # Exact cascade convention used by Lights.cpp: reversed orthographic Y.
    projection = ortho(-SHADOW_RADIUS, SHADOW_RADIUS, SHADOW_RADIUS, -SHADOW_RADIUS, 0., SHADOW_SPAN)
    matrix = multiply(projection, view)
    anchor_clip = transform_point(matrix, center)
    anchor = tuple(.5*x+.5 for x in anchor_clip)
    normal_view = normalized((-SLOPE_X, -SLOPE_Y, 1.))
    normal_world = normalized(apply(inverse_view, (*normal_view, 0.))[:3])
    # Delta world = right*2R*dU - up*2R*dV + direction*span*dDepth.
    denominator = dot(normal_world, direction)*SHADOW_SPAN
    gradient = (-dot(normal_world, right)*2*SHADOW_RADIUS/denominator,
                dot(normal_world, up)*2*SHADOW_RADIUS/denominator)
    return matrix, anchor, gradient, normal_world


PROBE_FRAGMENT = """#version 450 core
//__PRODUCTION_SHADOW_HELPER__
layout(binding=0) uniform sampler2D positions;
layout(binding=1) uniform sampler2DArray shadowDepth;
uniform mat4 inverseView;
uniform mat4 shadowProjectionView;
uniform vec3 planeAnchor;
uniform vec2 planeGradient;
uniform float shadowSpan;
layout(location=0) out vec4 worldAndShadow;
layout(location=1) out vec4 derivativeXAndError;
layout(location=2) out vec4 derivativeYAndBias;
layout(location=3) out vec4 surfaceVisibility;
void main() {
    vec4 viewPos=texelFetch(positions,ivec2(gl_FragCoord.xy),0);
    // Match deferred lighting: differentiate before adding the world origin.
    vec3 shadowWorldDx=mat3(inverseView)*dFdx(viewPos.xyz);
    vec3 shadowWorldDy=mat3(inverseView)*dFdy(viewPos.xyz);
    vec3 worldPos=(inverseView*vec4(viewPos.xyz,1.0)).xyz;
    //__PRODUCTION_POSITION_ERROR__
    const mat4 biasMatrix=mat4(0.5,0,0,0, 0,0.5,0,0, 0,0,0.5,0, 0.5,0.5,0.5,1);
    vec4 shadowCoordinate=(biasMatrix*shadowProjectionView)*vec4(worldPos,1.0);
    vec3 shadow=shadowCoordinate.xyz/shadowCoordinate.w;
    float exactPlaneDepth=planeAnchor.z+dot(planeGradient,shadow.xy-planeAnchor.xy);
    float errorMetres=(shadow.z-exactPlaneDepth)*shadowSpan;
    float biasMetres=geDirectionalSurfaceNumericalBias(shadowProjectionView,shadow.z,
        shadowPositionError)*shadowSpan;
    worldAndShadow=vec4(worldPos,shadow.z);
    derivativeXAndError=vec4(shadowWorldDx,errorMetres);
    derivativeYAndBias=vec4(shadowWorldDy,biasMetres);
    surfaceVisibility=vec4(
        geDirectionalSurfaceShadow(shadowDepth,shadowCoordinate,0,shadowProjectionView,
            shadowWorldDx,shadowWorldDy,shadowPositionError),
        geDirectionalSurfaceShadow(shadowDepth,shadowCoordinate,1,shadowProjectionView,
            shadowWorldDx,shadowWorldDy,shadowPositionError),
        geDirectionalSurfaceShadow(shadowDepth,shadowCoordinate,2,shadowProjectionView,
            shadowWorldDx,shadowWorldDy,shadowPositionError),
        length(shadowPositionError));
}
"""


SHADOW_FRAGMENT = """#version 450 core
uniform vec3 planeAnchor;
uniform vec2 planeGradient;
uniform float inverseResolution;
uniform float gapDepth;
void main() {
    vec2 uv=gl_FragCoord.xy*inverseResolution;
    gl_FragDepth=planeAnchor.z+dot(planeGradient,uv-planeAnchor.xy)-gapDepth;
}
"""


def rasterize_shadow(gl, program, anchor, gradient):
    """Independent analytic plane in the engine's actual depth format/filter."""
    for name, signature in {
        'glFramebufferTextureLayer': (None,C.c_uint,C.c_uint,C.c_uint,C.c_int,C.c_int),
        'glDrawBuffer': (None,C.c_uint), 'glReadBuffer': (None,C.c_uint),
        'glEnable': (None,C.c_uint), 'glDepthFunc': (None,C.c_uint),
        'glDepthMask': (None,C.c_ubyte), 'glClearDepth': (None,C.c_double),
        'glClear': (None,C.c_uint),
    }.items():
        gl._fn(name,*signature)
    if not hasattr(gl,'position_shadow_texture'):
        texture=C.c_uint()
        gl.glGenTextures(1,C.byref(texture)); gl.textures.append(texture.value)
        gl.position_shadow_texture=texture.value
        gl.glBindTexture(TEXTURE_2D_ARRAY,texture.value)
        gl.glTexImage3D(TEXTURE_2D_ARRAY,0,0x81A7,SHADOW_SIZE,SHADOW_SIZE,len(OBSTACLE_GAPS),0,0x1902,0x1406,None)
        for key,value in ((0x2800,0x2600),(0x2801,0x2600),(0x2802,0x812F),
                          (0x2803,0x812F),(0x813D,0),(0x884C,0)):
            gl.glTexParameteri(TEXTURE_2D_ARRAY,key,value)
    texture=gl.position_shadow_texture
    framebuffer,array=C.c_uint(),C.c_uint()
    gl.glGenFramebuffers(1,C.byref(framebuffer)); gl.framebuffers.append(framebuffer.value)
    gl.glGenVertexArrays(1,C.byref(array)); gl.arrays.append(array.value)
    gl.glBindFramebuffer(0x8D40,framebuffer.value)
    gl.glDrawBuffer(0); gl.glReadBuffer(0)
    gl.glViewport(0,0,SHADOW_SIZE,SHADOW_SIZE)
    gl.glEnable(0x0B71); gl.glDepthFunc(0x0201); gl.glDepthMask(True)
    gl.glClearDepth(1.)
    gl.glDisable(0x0BE2); gl.glDisable(0x0B44)
    gl.glBindVertexArray(array.value)
    gl.uniform(program,'planeAnchor','3f',*anchor)
    gl.uniform(program,'planeGradient','2f',*gradient)
    gl.uniform(program,'inverseResolution','1f',1./SHADOW_SIZE)
    for layer,gap in enumerate(OBSTACLE_GAPS):
        gl.glFramebufferTextureLayer(0x8D40,0x8D00,texture,0,layer)
        assert gl.glCheckFramebufferStatus(0x8D40)==0x8CD5
        gl.glClear(0x00000100)
        gl.uniform(program,'gapDepth','1f',gap/SHADOW_SPAN)
        gl.glDrawArrays(0x0004,0,3)
    gl.glBindFramebuffer(0x8D40,0)
    gl.glMemoryBarrier(gl.ALL_BARRIER_BITS)
    gl.check('rasterized plane and 2/4 m obstacles in engine-format shadow map')
    return texture


def draw_opaque(gl, program, positions, inverse_view, view, projection, output):
    gl._fn('glGetAttribLocation', C.c_int, C.c_uint, C.c_char_p)
    gl._fn('glEnableVertexAttribArray', None, C.c_uint)
    gl._fn('glVertexAttribPointer', None, C.c_uint, C.c_int, C.c_uint, C.c_ubyte, C.c_int, C.c_void_p)
    # std430 layout of the actual production camera/object/material records.
    camera = struct.pack('<4f48f', *(inverse_view[i][3] for i in range(3)), 0.,
                         *column_major(view), *column_major(inverse_view), *column_major(projection))
    gl.buffer(camera, 0)
    gl.buffer(struct.pack('<16f4i', *column_major(inverse_view), 0, 0, 0, 0), 1)
    gl.buffer(struct.pack('<16f', 1,1,1,1, 0,0, 1,1, 0,1,1,1, 0,0,0,0), 2)
    normal = normalized((-SLOPE_X, -SLOPE_Y, 1.))
    tangent = normalized((1., 0., SLOPE_X))
    payload = b''.join(struct.pack('<14f', *p, *normal, *tangent, 1,1,1, 0,0) for p in positions)
    buffer = gl.buffer(payload, 7)
    framebuffer, array = C.c_uint(), C.c_uint()
    gl.glGenFramebuffers(1, C.byref(framebuffer)); gl.framebuffers.append(framebuffer.value)
    gl.glGenVertexArrays(1, C.byref(array)); gl.arrays.append(array.value)
    gl.glBindFramebuffer(0x8D40, framebuffer.value)
    for i, texture in enumerate(output):
        gl.glFramebufferTexture2D(0x8D40, 0x8CE0+i, 0x0DE1, texture, 0)
    gl.glDrawBuffers(4, (C.c_uint*4)(*(0x8CE0+i for i in range(4))))
    assert gl.glCheckFramebufferStatus(0x8D40) == 0x8CD5
    gl.glBindVertexArray(array.value); gl.glBindBuffer(0x8892, buffer)
    offset = 0
    for name, count in (('a_Position',3), ('a_Normal',3), ('a_Tangents',3), ('a_Color',3), ('a_TexCoords',2)):
        location = gl.glGetAttribLocation(program, name.encode())
        if location >= 0:
            gl.glEnableVertexAttribArray(location)
            gl.glVertexAttribPointer(location, count, 0x1406, False, 56, C.c_void_p(offset))
        offset += count*4
    gl.uniform(program, 'offsetUbo', '1i', 0)
    gl.glViewport(0, 0, WIDTH, HEIGHT)
    gl.glDisable(0x0B71); gl.glDisable(0x0BE2); gl.glDisable(0x0B44)
    gl.glDrawArrays(0x0004, 0, 3)
    gl.glBindFramebuffer(0x8D40, 0)
    gl.check('real opaque G-buffer write')


def measure(gl, opaque, probe, shadow_program, distance, internal, material_textures):
    inverse_view = camera_transform()
    view = inverse_rigid(inverse_view)
    projection = ortho(-EXTENT_X, EXTENT_X, -EXTENT_Y, EXTENT_Y, .1, 50000.)
    positions = [(x, y, -distance+SLOPE_X*x+SLOPE_Y*y)
                 for x,y in ((-EXTENT_X,-EXTENT_Y), (3*EXTENT_X,-EXTENT_Y), (-EXTENT_X,3*EXTENT_Y))]
    outputs = [gl.texture((WIDTH,HEIGHT), internal=internal, linear=False)]
    outputs += [gl.texture((WIDTH,HEIGHT), internal=RGBA16F, linear=False) for _ in range(3)]
    for unit, texture in enumerate(material_textures): gl.sampler(unit, texture)
    draw_opaque(gl, opaque, positions, inverse_view, view, projection, outputs)
    shadow, anchor, gradient, expected_normal = shadow_fixture(inverse_view, distance)
    shadow_texture=rasterize_shadow(gl,shadow_program,anchor,gradient)
    measured = [gl.texture((WIDTH,HEIGHT), internal=RGBA32F, linear=False) for _ in range(4)]
    gl.sampler(0, outputs[0])
    gl.sampler(1,shadow_texture,TEXTURE_2D_ARRAY)
    gl.matrix(probe, 'inverseView', column_major(inverse_view))
    if internal == RG32F:
        gl.matrix(probe, 'projection', column_major(projection))
    gl.matrix(probe, 'shadowProjectionView', column_major(shadow))
    gl.uniform(probe, 'planeAnchor', '3f', *anchor)
    gl.uniform(probe, 'planeGradient', '2f', *gradient)
    gl.uniform(probe, 'shadowSpan', '1f', SHADOW_SPAN)
    gl.fullscreen(probe, WIDTH, HEIGHT, measured)
    world, dx, dy, visibility = [gl.texels(t,(WIDTH,HEIGHT)) for t in measured]
    assert all(math.isfinite(v) for values in (world,dx,dy,visibility) for v in values)
    maximum_position = maximum_shadow = maximum_angle = 0.
    outside_bias = shadowed = count = 0
    minimum_bias = float('inf')
    maximum_bias = 0.
    minimum_signed_error = float('inf')
    maximum_signed_error = -float('inf')
    minimum_lit = 1.
    maximum_obstacle_lit = 0.
    total_obstacle_lit = 0.
    false_shadow_pcf = 0
    maximum_4m_lit = 0.
    for y in range(2,HEIGHT-2):
        for x in range(2,WIDTH-2):
            i = (x+y*WIDTH)*4
            vx=(2*(x+.5)/WIDTH-1)*EXTENT_X
            vy=(2*(y+.5)/HEIGHT-1)*EXTENT_Y
            expected=transform_point(inverse_view,(vx,vy,-distance+SLOPE_X*vx+SLOPE_Y*vy))
            error=math.sqrt(sum((a-b)**2 for a,b in zip(expected,world[i:i+3])))
            maximum_position=max(maximum_position,error)
            maximum_shadow=max(maximum_shadow,abs(dx[i+3]))
            reconstructed=cross(dx[i:i+3],dy[i:i+3])
            if dot(reconstructed,reconstructed)>1e-20:
                cosine=min(1.,max(-1.,abs(dot(normalized(reconstructed),expected_normal))))
                maximum_angle=max(maximum_angle,math.degrees(math.acos(cosine)))
            else:
                maximum_angle=90.
            bias=dy[i+3]
            minimum_bias=min(minimum_bias,bias)
            maximum_bias=max(maximum_bias,bias)
            minimum_signed_error=min(minimum_signed_error,dx[i+3])
            maximum_signed_error=max(maximum_signed_error,dx[i+3])
            outside_bias += abs(dx[i+3])>bias
            shadowed += dx[i+3]>bias
            minimum_lit=min(minimum_lit,visibility[i])
            maximum_obstacle_lit=max(maximum_obstacle_lit,visibility[i+1])
            total_obstacle_lit+=visibility[i+1]
            maximum_4m_lit=max(maximum_4m_lit,visibility[i+2])
            false_shadow_pcf+=visibility[i]<1.-1e-6
            count += 1
    return (maximum_position, maximum_shadow, maximum_angle, minimum_bias, maximum_bias,
            100*outside_bias/count, 100*shadowed/count, minimum_signed_error, maximum_signed_error,
            minimum_lit,maximum_obstacle_lit,1.-total_obstacle_lit/count,100*false_shadow_pcf/count,
            maximum_4m_lit)


def check_production_allocations():
    source=(ROOT/'ANTAGONIZ/Source/Rendering/Window/FrameBuffer.cpp').read_text()
    for method,end in (('initialize','resize'),('resize','getFrameBuffer')):
        body=source.split(f'FrameBuffer::{method}(',1)[1].split(f'FrameBuffer::{end}(',1)[0]
        bound=body.split('glBindTexture(GL_TEXTURE_2D, m_gPosition);',1)[1]
        allocation=re.search(r'glTexImage2D\s*\([^;]+;',bound).group()
        assert re.search(r'GL_TEXTURE_2D\s*,\s*0\s*,\s*GL_RG32F\s*,',allocation), (method,allocation)
    print('PASS production initialize and resize both allocate gPosition as RG32F',flush=True)


def main():
    check_production_allocations()
    with AtmosphereGL() as gl:
        print(f'OpenGL {gl.version}; renderer: {gl.renderer}',flush=True)
        vertex=shader_source(ROOT/'Asset/Shader/opaque.vs.glsl')
        fragment=shader_source(ROOT/'Asset/Shader/opaque.fs.glsl')
        opaque=gl.compile([(0x8B31,vertex),(0x8B30,fragment)], 'production packed opaque writer')
        reference_fragment=fragment.replace('out vec2 gPosition;', 'out vec4 gPosition;').replace(
            'gPosition = geGBufferPackPosition(ViewPos, emission > 0.0 ? emission : geometryTag);',
            'gPosition = vec4(ViewPos, emission > 0.0 ? emission : geometryTag);')
        assert reference_fragment != fragment and 'gPosition = vec4(ViewPos,' in reference_fragment
        reference_opaque=gl.compile([(0x8B31,vertex),(0x8B30,reference_fragment)], 'former full XYZ opaque encoding')
        probe_source=PROBE_FRAGMENT.replace('//__PRODUCTION_SHADOW_HELPER__',
            shader_source(ROOT/'Asset/Shader/Shadow/directional_surface.glsl'))
        # Use the caller's actual expression, not a duplicated test-only bound.
        deferred=(ROOT/'Asset/Shader/forward.fs.glsl').read_text()
        position_error=re.search(r'vec3\s+shadowPositionError\s*=[^;]+;',deferred).group()
        position_error=position_error.replace('ubc.invView','inverseView')
        probe_source=probe_source.replace('//__PRODUCTION_POSITION_ERROR__',position_error)
        probe=gl.compile([(0x8B31,FULLSCREEN_VERTEX),(0x8B30,probe_source)], 'position/shadow precision diagnostic')
        packed_probe_source=probe_source.replace('#version 450 core', '#version 450 core\n'+
            shader_source(ROOT/'Asset/Shader/Geometry/gbuffer_position.glsl')+'\nuniform mat4 projection;')
        packed_probe_source=packed_probe_source.replace(
            'vec4 viewPos=texelFetch(positions,ivec2(gl_FragCoord.xy),0);',
            'vec4 viewPos=geGBufferReadPosition(positions,ivec2(gl_FragCoord.xy),projection);')
        packed_probe=gl.compile([(0x8B31,FULLSCREEN_VERTEX),(0x8B30,packed_probe_source)], 'packed position/shadow precision diagnostic')
        shadow_program=gl.compile([(0x8B31,FULLSCREEN_VERTEX),(0x8B30,SHADOW_FRAGMENT)], 'independent receiver/2/4 m obstacle depth')
        material=[gl.texture((1,1),internal=RGBA32F,value=v,linear=False) for v in
                  ((1,1,1,1),(.5,.5,1,1),(0,0,0,0),(1,1,1,1),(1,0,0,0))]
        print('distance_m format max_world_error_m max_shadow_error_m max_derivative_angle_deg min_numerical_bias_m max_numerical_bias_m outside_bias_percent false_shadow_percent min_signed_error_m max_signed_error_m minimum_lit_visibility maximum_2m_visibility mean_2m_occlusion false_pcf_shadow_percent maximum_4m_visibility',flush=True)
        for distance in (100.,1000.,10000.,40000.):
            values={internal:measure(gl,opaque if internal == RG32F else reference_opaque,
                         packed_probe if internal == RG32F else probe,shadow_program,distance,internal,material)
                    for internal in (RGBA16F,RGBA32F,RG32F)}
            for internal,label in ((RGBA16F,'RGBA16F'),(RGBA32F,'RGBA32F'),(RG32F,'RG32F')):
                print(f'{distance:10.0f} {label:8s} '+' '.join(f'{v:.6f}' for v in values[internal]),flush=True)
            half,full=values[RGBA16F],values[RGBA32F]
            packed=values[RG32F]
            assert packed[0]<.025 and packed[1]<.025, ('packed plane precision',distance,packed)
            assert packed[6]==0. and packed[9]>=1.-1e-6, ('packed must not falsely self-shadow',distance,packed)
            assert packed[10]<=1e-6 and packed[13]<=1e-6, ('packed 2m and4m blockers remain shadowed',distance,packed)
            assert packed[2]<1.0, ('packed receiver derivatives',distance,packed)
            assert full[0]<.025 and full[1]<.025, ('32F plane precision',distance,full)
            assert full[6]==0. and full[9]>=1.-1e-6, ('32F must not falsely self-shadow',distance,full)
            assert full[10]<=1e-6, ('2 m blocker must remain fully shadowed with the production bias',distance,full)
            assert full[13]<=1e-6, ('4 m blocker must remain fully shadowed at every distance',distance,full)
            assert half[0]>full[0]*10 and half[1]>full[1]*8, ('fixture must isolate storage loss',distance,half,full)
            assert full[2]<1.0 and half[2]>full[2]*5, ('receiver derivatives',distance,half,full)
            if distance>=1000:
                assert half[1]>.1 and half[5]>10., ('half storage must exceed the actual residual bias',distance,half)
        for width,height in ((1920,1080),(2560,1440),(3840,2160)):
            old=width*height*16; new=width*height*8
            print(f'gPosition {width}x{height}: {old} -> {new} bytes; saved {old-new} bytes ({(old-new)/1048576:.5f} MiB)',flush=True)
        print('PASS actual opaque writer + PCF: no false RG32F/RGBA32F self-shadow through40km; 2 m and 4 m blockers fully shadowed with production texel bias at every distance',flush=True)
        gl.check('position precision regression')


if __name__=='__main__':
    main()
