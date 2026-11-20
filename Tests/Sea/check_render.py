#!/usr/bin/env python3
"""Render the real sea VS/FS with controlled opaque depth and lighting.

Checks tile/LOD invariance, finite shading, sky/depth behavior and the canonical
world-coordinate seams. Blocking readbacks are test instrumentation only; this
fixture makes no hardware-performance or in-game visual-quality claim.
"""
from __future__ import annotations

import ctypes as C
import math
import os
from pathlib import Path
import struct
import sys

os.environ.setdefault("MESA_GL_VERSION_OVERRIDE", "4.6")
os.environ.setdefault("MESA_GLSL_VERSION_OVERRIDE", "460")
ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "Tests/Atmosphere"))
from gl_support import (AtmosphereGL, RGBA32F, RG32F, R32F, TEXTURE_2D,
                        TEXTURE_2D_ARRAY, TEXTURE_3D, TEXTURE_CUBE_MAP, shader_source)
from check_position_precision import column_major, inverse_rigid, ortho
from check_terrain_shadow import look_at

SEA = ROOT / "ENDURANCE/Ressources/Shader/Water"
SIZE = (65, 49)  # Odd viewport exercises derivative/helper pixels at boundaries.


def max_difference(a, b):
    return max(abs(x-y) for x, y in zip(a, b))


class SeaRig:
    def __init__(self, gl):
        self.gl = gl
        self.program = gl.compile([(0x8B31, shader_source(SEA / "sea.vs.glsl")),
                                   (0x8B30, shader_source(SEA / "sea.fs.glsl"))],
                                  "production instanced sea")
        for name, signature in {
            "glDrawArraysInstanced": (None, C.c_uint, C.c_int, C.c_int, C.c_int),
            "glDrawArraysInstancedBaseInstance": (None, C.c_uint, C.c_int, C.c_int, C.c_int, C.c_uint),
            "glBeginTransformFeedback": (None, C.c_uint),
            "glEndTransformFeedback": (None,),
            "glClearBufferfv": (None, C.c_uint, C.c_int, C.POINTER(C.c_float)),
            "glEnable": (None, C.c_uint),
            "glDepthMask": (None, C.c_ubyte),
            "glDepthFunc": (None, C.c_uint),
            "glReadPixels": (None, C.c_int, C.c_int, C.c_int, C.c_int, C.c_uint, C.c_uint, C.c_void_p),
        }.items():
            gl._fn(name, *signature)
        self.camera = gl.buffer(208, 0)
        self.misc = gl.buffer(struct.pack("<i4fI", 1, 0., 2.2, 1., 80., 1), 3)
        light = bytearray(80)
        struct.pack_into("<3f", light, 0, 0., 200., 0.)
        struct.pack_into("<3f", light, 16, 1., .96, .88)
        struct.pack_into("<3f", light, 32, 0., -1., 0.)
        struct.pack_into("<2f5i", light, 44, 10., 40., 0, -1, -1, -1, 0)
        self.lights = gl.buffer(bytes(light), 4)
        self.shadows = gl.buffer(bytes(4 * 80), 5)
        self.tiles = gl.buffer(16, 8)
        atmosphere = gl.buffer(bytes(672))
        gl.glBindBufferBase(0x8A11, 6, atmosphere)
        self.opaque = gl.texture(SIZE, internal=RG32F, value=(-125., 0.), linear=False)
        self.output = gl.texture(SIZE, internal=RGBA32F, value=-77., linear=False)
        self.depth = C.c_uint()
        gl.glGenTextures(1, C.byref(self.depth))
        gl.textures.append(self.depth.value)
        gl.glBindTexture(TEXTURE_2D, self.depth.value)
        gl.glTexImage2D(TEXTURE_2D, 0, 0x8CAC, *SIZE, 0, 0x1902, 0x1406, None)
        gl.glTexParameteri(TEXTURE_2D, 0x2800, 0x2600)
        gl.glTexParameteri(TEXTURE_2D, 0x2801, 0x2600)
        shadow = gl.texture((1, 1, 4), target=TEXTURE_2D_ARRAY, internal=R32F, value=1.)
        irradiance = gl.texture((1, 1), target=TEXTURE_CUBE_MAP, internal=RGBA32F,
                                value=(.2, .25, .3, 1.))
        prefilter = gl.texture((16, 16), target=TEXTURE_CUBE_MAP, internal=RGBA32F,
                               levels=5, value=(.3, .35, .4, 1.))
        brdf = gl.texture((1, 1), internal=RG32F, value=(.7, .1))
        # Keep all active sampler types complete even with atmosphere disabled.
        for unit in (9, 10, 11, 14, 15):
            gl.sampler(unit, gl.texture((1, 1), value=(0., 0., 0., 1.)))
        for unit in (12, 13):
            gl.sampler(unit, gl.texture((1, 1, 1), target=TEXTURE_3D,
                                       value=(0., 0., 0., 1.)), TEXTURE_3D)
        # Texture allocation changes the currently active binding; establish
        # the real water inputs after all allocations are finished.
        gl.sampler(4, self.opaque)
        gl.sampler(5, shadow, TEXTURE_2D_ARRAY)
        gl.sampler(6, irradiance, TEXTURE_CUBE_MAP)
        gl.sampler(7, prefilter, TEXTURE_CUBE_MAP)
        gl.sampler(8, brdf)
        self.framebuffer, self.vao = C.c_uint(), C.c_uint()
        gl.glGenFramebuffers(1, C.byref(self.framebuffer))
        gl.glGenVertexArrays(1, C.byref(self.vao))
        gl.framebuffers.append(self.framebuffer.value)
        gl.arrays.append(self.vao.value)
        gl.glBindFramebuffer(0x8D40, self.framebuffer.value)
        gl.glFramebufferTexture2D(0x8D40, 0x8CE0, TEXTURE_2D, self.output, 0)
        gl.glFramebufferTexture2D(0x8D40, 0x8D00, TEXTURE_2D, self.depth.value, 0)
        gl.glDrawBuffers(1, (C.c_uint * 1)(0x8CE0))
        assert gl.glCheckFramebufferStatus(0x8D40) == 0x8CD5
        for name, value in (("uAtmosphereActive", 0), ("uAtmosphereIblActive", 0)):
            gl.uniform(self.program, name, "1i", value)
        gl.uniform(self.program, "uAtmosphereInvResolution", "2f", 1./SIZE[0], 1./SIZE[1])
        gl.uniform(self.program, "seaLevel", "1f", 0.)
        gl.uniform(self.program, "seaBaseExtent", "1f", 64.)
        self.set_camera()

    def set_camera(self, *, underwater=False, extent=128., center=(-64., -64.)):
        height = -100. if underwater else 100.
        eye = (center[0], height, center[1])
        view = look_at(eye, (center[0], 0., center[1]), (0., 0., -1.))
        projection = ortho(-extent*.5, extent*.5, -extent*.5, extent*.5, .1, 500.)
        self.gl.write(self.camera, struct.pack("<52f", *eye, 0., *column_major(view),
                                               *column_major(inverse_rigid(view)), *column_major(projection)))

    def set_opaque(self, view_z, tag=0.):
        gl = self.gl
        gl.sampler(4, self.opaque)
        payload = struct.pack("<2f", view_z, tag) * (SIZE[0] * SIZE[1])
        gl.glTexImage2D(TEXTURE_2D, 0, RG32F, *SIZE, 0, 0x8227, 0x1406, C.c_char_p(payload))

    def draw(self, tiles=((-2, -2, 1, 0),), *, stored_depth=1., depth_write=False, time=0.):
        gl = self.gl
        payload = b"".join(struct.pack("<iiII", *tile) for tile in tiles)
        gl.resize(self.tiles, payload or bytes(16))
        gl.bind(self.tiles, 8)
        gl.bind(self.camera, 0)
        gl.bind(self.misc, 3)
        gl.write(self.misc, struct.pack("<i4fI", 1, time, 2.2, 1., 80., 1))
        gl.bind(self.lights, 4)
        gl.bind(self.shadows, 5)
        gl.glBindFramebuffer(0x8D40, self.framebuffer.value)
        gl.glViewport(0, 0, *SIZE)
        gl.glDisable(0x0BE2)
        gl.glDisable(0x0B44)
        gl.glEnable(0x0B71)
        gl.glDepthFunc(0x0201)  # GL_LESS
        gl.glDepthMask(True)
        gl.glClearBufferfv(0x1800, 0, (C.c_float * 4)(-77., -77., -77., -77.))
        gl.glClearBufferfv(0x1801, 0, (C.c_float * 1)(stored_depth))
        gl.glDepthMask(depth_write)
        gl.glBindVertexArray(self.vao.value)
        gl.glUseProgram(self.program)
        gl.glDrawArraysInstanced(0x0004, 0, 6, len(tiles))
        result = gl.texels(self.output, SIZE)
        depth = (C.c_float * (SIZE[0] * SIZE[1]))()
        gl.glReadPixels(0, 0, *SIZE, 0x1902, 0x1406, depth)
        gl.glBindFramebuffer(0x8D40, 0)
        gl.glDepthMask(True)
        gl.check("production sea render")
        return result, tuple(depth)


def check_surface(gl):
    rig = SeaRig(gl)
    first, depths = rig.draw()
    assert all(math.isfinite(v) for v in first)
    assert all(v >= 0. for v in first), "Sea did not cover the whole expected tile"
    assert all(0. <= a <= 1. for a in first[3::4])
    assert all(v == 1. for v in depths), "Sea modified the opaque depth buffer"
    fine = ((-2, -2, 0, 0), (-1, -2, 0, 0), (-2, -1, 0, 0), (-1, -1, 0, 0))
    divided, _ = rig.draw(fine)
    delta = max_difference(first, divided)
    assert delta < 2e-4, ("Sea motifs depend on tile/LOD subdivision", delta)
    animated, _ = rig.draw(time=4.)
    assert max_difference(first, animated) > 1e-5, "Sea animation is inactive"
    blocked, _ = rig.draw(stored_depth=0.)
    assert all(v == -77. for v in blocked), "Sea appears over nearer opaque geometry"
    empty, _ = rig.draw(())
    assert all(v == -77. for v in empty), "Zero instances still render water"
    rig.set_opaque(-100.2)
    shallow, _ = rig.draw()
    assert max_difference(first, shallow) > .01, "Shallow-water depth/foam does not affect the surface"
    rig.set_opaque(-100.2, -2.)
    sky, _ = rig.draw()
    rig.set_opaque(0., 0.)
    no_surface, _ = rig.draw()
    assert max_difference(sky, no_surface) < 2e-5, "Sky/missing opaque background creates a foam band"
    assert all(math.isfinite(v) and v >= 0. for v in sky)
    rig.set_camera(underwater=True)
    below, _ = rig.draw()
    assert all(math.isfinite(v) and v >= 0. for v in below), "Water underside is invalid"
    print(f"PASS production sea shading: finite above/below, depth/sky/foam, animation, no depth writes; LOD subdivision delta {delta:.3g}")
    return rig


def check_canonical_seams(gl, rig):
    """Capture the actual vertex output, including rounding, at shared corners."""
    program = gl.compile([(0x8B31, shader_source(SEA / "sea.vs.glsl")),
                          (0x8B30, shader_source(SEA / "sea.fs.glsl"))],
                         "sea canonical seam feedback", feedback=("WorldPos",))
    descriptors, pairs = [], []
    for lod in (1, 5, 12, 20):
        stride = 1 << lod
        for sign in (-1, 1):
            x, z = sign * stride * 8, -sign * stride * 5
            # Coarse right edge meets two half-size fine left edges. Compare
            # its endpoints with the matching fine vertices, in both signs.
            coarse = len(descriptors)
            descriptors.extend(((x, z, lod, 0), (x+stride, z, lod-1, 0),
                                (x+stride, z+stride//2, lod-1, 0)))
            pairs.extend(((coarse*6+2, (coarse+1)*6),
                          (coarse*6+5, (coarse+2)*6+1)))
    gl.resize(rig.tiles, b"".join(struct.pack("<iiII", *v) for v in descriptors))
    gl.bind(rig.tiles, 8)
    gl.bind(rig.camera, 0)
    count = len(descriptors)*6
    output = gl.buffer(count*12)
    gl.glBindBufferBase(0x8C8E, 0, output)
    gl.glBindFramebuffer(0x8D40, rig.framebuffer.value)
    gl.glBindVertexArray(rig.vao.value)
    gl.glEnable(0x8C89)  # GL_RASTERIZER_DISCARD
    try:
        gl.uniform(program, "seaLevel", "1f", 3.25)
        for extent in (64., 22.4, 467.2):
            gl.uniform(program, "seaBaseExtent", "1f", extent)
            gl.glUseProgram(program)
            gl.glBeginTransformFeedback(0x0004)
            gl.glDrawArraysInstanced(0x0004, 0, 6, len(descriptors))
            gl.glEndTransformFeedback()
            raw = gl.read(output, count*12)
            for a, b in pairs:
                left, right = raw[a*12:(a+1)*12], raw[b*12:(b+1)*12]
                assert left == right, ("Sea LOD corner differs", extent, a, b,
                                       struct.unpack("<3f", left), struct.unpack("<3f", right))
                position = struct.unpack("<3f", left)
                assert all(math.isfinite(v) for v in position)
                assert position[1] == 3.25 and position[0] != 0., "Transform feedback did not capture the sea vertex"
        gl.check("sea canonical seam feedback")
        print(f"PASS {len(pairs)*3} bit-identical sea seam corners: positive/negative coordinates, LOD1–20, three base extents")
    finally:
        gl.glDisable(0x8C89)
        gl.glBindBufferBase(0x8C8E, 0, 0)
        gl.glBindFramebuffer(0x8D40, 0)


def check_noise_gradient(gl):
    """Compare the shipped analytic gradient with independent finite differences."""
    source = shader_source(SEA / "sea.fs.glsl")
    assert source.count("void main()") == 1
    source = source.replace("void main()", "void seaUnusedMain()") + """
layout(std430,binding=9) readonly buffer ProbePoints { vec2 points[]; };
void main() {
    vec2 p=points[int(gl_FragCoord.x)];
    const float e=0.001;
    vec3 sampleValue=seaNoiseValueGradient(p);
    vec2 finiteDifference=vec2(
        seaNoiseValueGradient(p+vec2(e,0)).x-seaNoiseValueGradient(p-vec2(e,0)).x,
        seaNoiseValueGradient(p+vec2(0,e)).x-seaNoiseValueGradient(p-vec2(0,e)).x)/(2.0*e);
    o_FragColor=vec4(sampleValue.yz,finiteDifference);
}
"""
    vertex = """#version 460 core
layout(location=0) out vec3 WorldPos;
void main() {
    vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);
    WorldPos=vec3(0);
    gl_Position=vec4(p*2.0-1.0,0,1);
}
"""
    points = [(i*.173-5.9, i*.317-8.2) for i in range(80)]
    points += [(x+dx, y+dy) for x, y in ((-5., -3.), (-1., 1.), (0., 0.), (3., 4.))
               for dx, dy in ((0., 0.), (.0001, 0.), (-.0001, 0.), (0., .0001), (0., -.0001))]
    program = gl.compile([(0x8B31, vertex), (0x8B30, source)], "sea noise gradient probe")
    gl.buffer(b"".join(struct.pack("<2f", *p) for p in points), 9)
    output = gl.texture((len(points), 1), internal=RGBA32F)
    gl.fullscreen(program, len(points), 1, (output,))
    values = gl.texels(output, (len(points), 1))
    assert all(math.isfinite(v) for v in values)
    errors = [abs(values[i+j]-values[i+j+2]) for i in range(0, len(values), 4) for j in (0, 1)]
    assert max(errors) < .003, ("Incorrect analytic wave gradient", max(errors))
    print(f"PASS analytic wave gradient: {len(points)} samples including lattice boundaries; finite-difference error {max(errors):.3g}")


def main():
    with AtmosphereGL() as gl:
        print(f"OpenGL {gl.version}; renderer {gl.renderer}", flush=True)
        rig = check_surface(gl)
        check_canonical_seams(gl, rig)
        check_noise_gradient(gl)


if __name__ == "__main__":
    main()
