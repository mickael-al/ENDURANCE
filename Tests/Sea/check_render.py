#!/usr/bin/env python3
"""Render the real sea VS/FS with controlled opaque depth and lighting.

Checks flat geometry, softened animated normals, finite shading, sky/depth
behavior and canonical world-coordinate seams. Blocking readbacks are test
instrumentation only; this fixture makes no hardware-performance claim.
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
VERTICES = 6


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
        gl.glDrawArraysInstanced(0x0004, 0, VERTICES, len(tiles))
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
    assert all(0.70 < a < 0.95 for a in first[3::4]), \
        "The artistic depth transition saturates before the requested deeper range"
    # At ~25m the new 36m transition remains partial; the old 12m range was opaque.
    assert all(v == 1. for v in depths), "Sea modified the opaque depth buffer"
    fine = ((-2, -2, 0, 0), (-1, -2, 0, 0), (-2, -1, 0, 0), (-1, -1, 0, 0))
    divided, _ = rig.draw(fine)
    delta = max_difference(first, divided)
    assert all(math.isfinite(v) and v >= 0. for v in divided), "Fine tiles have holes or invalid shading"
    assert delta < 2e-4, ("Sea shading depends on tile/LOD subdivision", delta)
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


def feedback_program(gl):
    return gl.compile([(0x8B31, shader_source(SEA / "sea.vs.glsl")),
                       (0x8B30, shader_source(SEA / "sea.fs.glsl"))],
                      "flat sea seam feedback", feedback=("WorldPos",))


def capture_vertices(gl, rig, program, descriptors, *, extent=64., level=3.25, time=0.):
    """Execute the production six-vertex quad and retain float bit patterns."""
    gl.resize(rig.tiles, b"".join(struct.pack("<iiII", *v) for v in descriptors))
    gl.bind(rig.tiles, 8)
    gl.bind(rig.camera, 0)
    gl.bind(rig.misc, 3)
    gl.write(rig.misc, struct.pack("<i4fI", 1, time, 2.2, 1., 80., 1))
    count = len(descriptors) * VERTICES
    output = gl.buffer(count * 12)
    gl.glBindBufferBase(0x8C8E, 0, output)
    gl.glBindFramebuffer(0x8D40, rig.framebuffer.value)
    gl.glBindVertexArray(rig.vao.value)
    gl.glEnable(0x8C89)  # GL_RASTERIZER_DISCARD
    try:
        gl.uniform(program, "seaLevel", "1f", level)
        gl.uniform(program, "seaBaseExtent", "1f", extent)
        gl.glUseProgram(program)
        gl.glBeginTransformFeedback(0x0004)
        gl.glDrawArraysInstanced(0x0004, 0, VERTICES, len(descriptors))
        gl.glEndTransformFeedback()
        raw = gl.read(output, count * 12)
        gl.check("flat sea vertex feedback")
        return raw
    finally:
        gl.glDisable(0x8C89)
        gl.glBindBufferBase(0x8C8E, 0, 0)
        gl.glBindFramebuffer(0x8D40, 0)


def check_flat_geometry(gl, rig, program):
    descriptors = ((-2, -2, 1, 0), (128, 128, 3, 0))
    for level in (-0.375, 3.25):
        rig.set_camera()
        first = capture_vertices(gl, rig, program, descriptors, level=level)
        repeated = capture_vertices(gl, rig, program, descriptors, level=level, time=8.)
        rig.set_camera(underwater=True)
        moved = capture_vertices(gl, rig, program, descriptors, level=level, time=1000.)
        assert first == repeated == moved, "Camera/time still displaces the sea geometry"
        points = tuple(struct.iter_unpack("<3f", first))
        assert all(math.isfinite(v) for point in points for v in point)
        assert all(point[1] == level for point in points), "Sea is not exactly at seaLevel"
    print("PASS flat two-triangle geometry: exact seaLevel, invariant under time and camera changes")


def check_canonical_seams(gl, rig, program):
    """Shared endpoints must match, and finer endpoints remain on the flat edge."""
    descriptors, pairs, flat_edges = [], [], []
    # Corner IDs in the real six-vertex topology: (0,0),(0,1),(1,0),(1,1).
    corners = (0, 1, 2, 5)
    def index(tile, corner):
        return tile * VERTICES + corners[corner]
    for lod in (1, 5, 12, 20):
        stride = 1 << lod
        for sign in (-1, 1):
            x, z = sign * stride * 8, -sign * stride * 5
            coarse = len(descriptors)
            descriptors.append((x, z, lod, 0))
            for side in range(4):
                first = len(descriptors)
                if side == 0:
                    descriptors.extend(((x+stride, z, lod-1, 0),
                                        (x+stride, z+stride//2, lod-1, 0)))
                    cc, fc, axis = (2, 3), (0, 1), 0
                elif side == 1:
                    descriptors.extend(((x-stride//2, z, lod-1, 0),
                                        (x-stride//2, z+stride//2, lod-1, 0)))
                    cc, fc, axis = (0, 1), (2, 3), 0
                elif side == 2:
                    descriptors.extend(((x, z+stride, lod-1, 0),
                                        (x+stride//2, z+stride, lod-1, 0)))
                    cc, fc, axis = (1, 3), (0, 2), 2
                else:
                    descriptors.extend(((x, z-stride//2, lod-1, 0),
                                        (x+stride//2, z-stride//2, lod-1, 0)))
                    cc, fc, axis = (0, 2), (1, 3), 2
                pairs.extend(((index(coarse, cc[0]), index(first, fc[0])),
                              (index(coarse, cc[1]), index(first+1, fc[1])),
                              (index(first, fc[1]), index(first+1, fc[0]))))
                flat_edges.append((index(coarse, cc[0]), index(coarse, cc[1]),
                                   index(first, fc[1]), axis))
    for extent in (64., 22.4, 467.2):
        for time in (0., 8.):
            raw = capture_vertices(gl, rig, program, descriptors, extent=extent, time=time)
            positions = tuple(struct.iter_unpack("<3f", raw))
            for a, b in pairs:
                assert raw[a*12:(a+1)*12] == raw[b*12:(b+1)*12], \
                    ("Sea shared endpoint differs", extent, time, a, b, positions[a], positions[b])
            for a, b, midpoint, axis in flat_edges:
                lo, hi, mid = positions[a], positions[b], positions[midpoint]
                along = 2-axis
                assert lo[axis] == mid[axis] == hi[axis] and lo[1] == mid[1] == hi[1] == 3.25
                assert min(lo[along], hi[along]) <= mid[along] <= max(lo[along], hi[along]), \
                    "Fine corner lies outside the coarse edge"
    print(f"PASS {len(pairs)*6} bit-identical flat seam endpoints: all four edges, LOD1–20, three scales")


def check_mixed_coverage(gl, rig):
    cases = (
        (((-2, -2, 1, 0), (0, -2, 0, 0), (0, -1, 0, 0)), (0., -64.)),
        (((0, -2, 1, 0), (-1, -2, 0, 0), (-1, -1, 0, 0)), (0., -64.)),
        (((-2, -2, 1, 0), (-2, 0, 0, 0), (-1, 0, 0, 0)), (-64., 0.)),
        (((-2, 0, 1, 0), (-2, -1, 0, 0), (-1, -1, 0, 0)), (-64., 0.)),
    )
    rig.set_opaque(-150.)
    for descriptors, center in cases:
        rig.set_camera(center=center)
        pixels, _ = rig.draw(descriptors, time=7.25)
        assert all(math.isfinite(v) and v >= 0. for v in pixels), "Mixed-LOD quads leave a visible hole"
    print("PASS mixed-LOD flat raster coverage: no holes across four directions")


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
layout(location=2) out float SeaViewZ;
void main() {
    vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);
    WorldPos=vec3(0);
    SeaViewZ=-1.0;
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

    # Compare the shading slope against finite differences of its virtual
    # world-space height field, independently applying the chain rule.
    ripple = shader_source(SEA / "sea.fs.glsl").replace("void main()", "void seaUnusedMain()") + """
layout(std430,binding=9) readonly buffer ProbePoints { vec2 points[]; };
float virtualHeight(vec2 p) {
    return 0.4*seaNoiseValueGradient((p+vec2(0.56,0.28)*8.0)*0.125).x;
}
void main() {
    vec2 p=points[int(gl_FragCoord.x)];
    const float e=0.01;
    vec2 derivative=vec2(virtualHeight(p+vec2(e,0))-virtualHeight(p-vec2(e,0)),
                         virtualHeight(p+vec2(0,e))-virtualHeight(p-vec2(0,e)))/(2.0*e);
    o_FragColor=vec4(seaRippleSlope(p,8.0,0.0),derivative);
}
"""
    points = [(i*1.173-59., i*2.317-82.) for i in range(100)]
    program = gl.compile([(0x8B31, vertex), (0x8B30, ripple)], "sea ripple world slope probe")
    gl.buffer(b"".join(struct.pack("<2f", *p) for p in points), 9)
    output = gl.texture((len(points), 1), internal=RGBA32F)
    gl.fullscreen(program, len(points), 1, (output,))
    values = gl.texels(output, (len(points), 1))
    errors = [abs(values[i+j]-values[i+j+2]) for i in range(0, len(values), 4) for j in (0, 1)]
    assert all(math.isfinite(v) for v in values)
    assert max(errors) < 1e-4, ("Ripple slope does not match virtual height in metres", max(errors))
    print(f"PASS softened ripple world slopes: independent finite-difference error {max(errors):.3g}")

    probe = shader_source(SEA / "sea.fs.glsl").replace("void main()", "void seaUnusedMain()") + """
layout(std430,binding=9) readonly buffer ProbePoints { vec2 points[]; };
void main() {
    vec2 p=points[int(gl_FragCoord.x)];
    vec2 softened=seaRippleSlope(p,8.0,0.0);
    vec2 previous=seaNoiseValueGradient((p+vec2(0.56,0.28)*8.0)*0.125).yz*0.22;
    o_FragColor=vec4(length(softened),length(previous),seaRippleSlope(p,8.0,64.0));
}
"""
    program = gl.compile([(0x8B31, vertex), (0x8B30, probe)], "sea ripple softness/filter probe")
    gl.fullscreen(program, len(points), 1, (output,))
    flat = gl.texels(output, (len(points), 1))
    values = [flat[i:i+4] for i in range(0, len(flat), 4)]
    new_rms=math.sqrt(sum(v[0]*v[0] for v in values)/len(values))
    old_rms=math.sqrt(sum(v[1]*v[1] for v in values)/len(values))
    assert 0.1 < new_rms/old_rms < 0.3, ("Ripple slopes were not softened", new_rms,old_rms)
    assert all(v[2]==0.0 and v[3]==0.0 for v in values), "Subpixel ripples remain active"
    print(f"PASS reduced normal strength and distant filter: RMS ratio {new_rms/old_rms:.3g} versus previous ripple slope")


def main():
    with AtmosphereGL() as gl:
        print(f"OpenGL {gl.version}; renderer {gl.renderer}", flush=True)
        rig = check_surface(gl)
        program = feedback_program(gl)
        check_flat_geometry(gl, rig, program)
        check_canonical_seams(gl, rig, program)
        check_mixed_coverage(gl, rig)
        check_noise_gradient(gl)


if __name__ == "__main__":
    main()
