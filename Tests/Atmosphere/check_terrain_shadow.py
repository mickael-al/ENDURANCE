#!/usr/bin/env python3
"""Actual terrain rasterization exposes false shadows at nonplanar PCF taps.

The terrain and shadow passes use the SAME packed vertex buffer and the
unchanged production GLSL460 terrain shaders. The real deferred shader and
ModelManager fullscreen mesh then light the RG32F GBuffer. Under a vertical
sun a single-valued heightfield cannot shadow itself: the independently known
answer is the same direct radiance as an empty shadow map. A raised plate,
included in both geometry passes, supplies a positive occlusion control.

The frozen HEAD helper is a negative witness, not an oracle. --diagnose prints
the current/frozen failure rates without requiring the current fix to pass.
"""
from __future__ import annotations

import argparse
import ctypes as C
import math
import os
from pathlib import Path
import re
import struct

# Mesa's existing terrain test workflow exposes GLSL460 draw parameters this
# way; no production shader version or vertex decoding is rewritten.
os.environ.setdefault("MESA_GL_VERSION_OVERRIDE", "4.6")
os.environ.setdefault("MESA_GLSL_VERSION_OVERRIDE", "460")

from check_screen_composition import ScreenGL
from check_position_precision import cross, dot, normalized, multiply, column_major, inverse_rigid, ortho, transform_point
from fixtures import Parameters
from gl_support import ROOT, RG32F, RGBA32F, TEXTURE_2D, TEXTURE_2D_ARRAY, TEXTURE_3D, TEXTURE_CUBE_MAP, shader_source

HERE = Path(__file__).resolve().parent
TERRAIN = ROOT / "Asset/Shader/ProceduralTerrain"
SHADOW_SIZE = 2048
VIEW_SIZE = (192, 128)
EXTENT = 1024.
RADIUS = 1024.
SUN = (0., 1., 0.)
CENTER = (512., 128., 512.)


def look_at(eye, target, up):
    forward = normalized(tuple(b - a for a, b in zip(eye, target)))
    right = normalized(cross(forward, up))
    vertical = cross(right, forward)
    rows = (right, vertical, tuple(-v for v in forward))
    return tuple(tuple(row) + (-dot(row, eye),) for row in rows) + ((0., 0., 0., 1.),)


def normal_code(normal):
    denominator = sum(abs(v) for v in normal)
    x, y, z = (v / denominator for v in normal)
    if z < 0:
        x, y = (1 - abs(y)) * (1 if x >= 0 else -1), (1 - abs(x)) * (1 if y >= 0 else -1)
    a, b = round(x * 32767), round(y * 32767)
    return (a & 65535) | ((b & 65535) << 16)


def mesh(pattern, obstacle=False):
    def height(x, z):
        if pattern == "bumps":
            return 128 + 7 * math.sin(2 * math.pi * x / 64) * math.sin(2 * math.pi * z / 64)
        if pattern == "monotone":
            # Both partial derivatives stay positive: .09/.06 +/- .03682.
            # Unlike a plane this is triangulated and weakly curved, but it
            # has no alternating ridges, bowls or abrupt changes of slope.
            return 64 + .09 * x + .06 * z + 3 * math.sin(2 * math.pi * x / 512) * math.sin(2 * math.pi * z / 512)
        if pattern == "contact":
            fade = min(max((max(abs(x - 512), abs(z - 512)) - 64) / 64, 0), 1)
            return 128 + fade * 7 * math.sin(2 * math.pi * x / 64) * math.sin(2 * math.pi * z / 64)
        triangle = lambda value: 1 - 4 * abs((value % 1) - .5)
        return 128 + 8 * (triangle(x / 96) + .65 * triangle(z / 64))

    vertices = []
    def triangle(a, b, c):
        quantized = [tuple(round(v / EXTENT * 65534) for v in p) for p in (a, b, c)]
        decoded = [tuple(v * EXTENT / 65534 for v in p) for p in quantized]
        normal = normalized(cross(tuple(b - a for a, b in zip(decoded[0], decoded[1])),
                                  tuple(c - a for a, c in zip(decoded[0], decoded[2]))))
        # A heightfield with |gradient| < .75 cannot shadow itself under a
        # vertical sun or a sun at elevation60 (tan60=1.732), in any azimuth.
        assert normal[1] > .8, "The terrain slope bound for the independent no-shadow oracle was violated"
        encoded = normal_code(normal)
        for x, y, z in quantized:
            assert all(0 <= v <= 65534 for v in (x, y, z))
            vertices.extend((x | (y << 16), z | (128 << 16) | (192 << 24), encoded))

    for z in range(0, 1024, 16):
        for x in range(0, 1024, 16):
            a, b = (x, height(x, z), z), (x + 16, height(x + 16, z), z)
            c, d = (x, height(x, z + 16), z + 16), (x + 16, height(x + 16, z + 16), z + 16)
            if (x // 16 + z // 16) % 2:
                triangle(a, c, d); triangle(a, d, b)
            else:
                triangle(a, c, b); triangle(b, c, d)
    if obstacle:
        # A real plate above the center, outside this downward camera view but
        # inside the light frustum, casts a geometrically unambiguous shadow.
        roof = 128 + obstacle if isinstance(obstacle, float) else 400
        a, b, c, d = (496, roof, 496), (528, roof, 496), (496, roof, 528), (528, roof, 528)
        triangle(a, c, b); triangle(b, c, d)
    return struct.pack(f"<{len(vertices)}I", *vertices), len(vertices) // 3


class TerrainGL(ScreenGL):
    def __init__(self):
        super().__init__()
        for name, signature in {
            "glVertexAttribIPointer": (None, C.c_uint, C.c_int, C.c_uint, C.c_int, C.c_void_p),
            "glDrawArraysInstancedBaseInstance": (None, C.c_uint, C.c_int, C.c_int, C.c_int, C.c_uint),
            "glFramebufferTextureLayer": (None, C.c_uint, C.c_uint, C.c_uint, C.c_int, C.c_int),
            "glDrawBuffer": (None, C.c_uint), "glReadBuffer": (None, C.c_uint),
            "glEnable": (None, C.c_uint), "glDepthFunc": (None, C.c_uint),
            "glDepthMask": (None, C.c_ubyte), "glClearDepth": (None, C.c_double),
            "glClear": (None, C.c_uint),
            "glClearBufferfv": (None, C.c_uint, C.c_int, C.POINTER(C.c_float)),
        }.items():
            self._fn(name, *signature)

    def depth(self, size, target=TEXTURE_2D):
        texture = C.c_uint()
        self.glGenTextures(1, C.byref(texture)); self.textures.append(texture.value)
        self.glBindTexture(target, texture.value)
        if target == TEXTURE_2D_ARRAY:
            self.glTexImage3D(target, 0, 0x81A7, *size, 0, 0x1902, 0x1406, None)
        else:
            self.glTexImage2D(target, 0, 0x81A7, *size, 0, 0x1902, 0x1406, None)
        for key, value in ((0x2800, 0x2600), (0x2801, 0x2600), (0x2802, 0x812F),
                           (0x2803, 0x812F), (0x813D, 0), (0x884C, 0)):
            self.glTexParameteri(target, key, value)
        return texture.value

    def framebuffer(self):
        framebuffer = C.c_uint()
        self.glGenFramebuffers(1, C.byref(framebuffer)); self.framebuffers.append(framebuffer.value)
        self.glBindFramebuffer(0x8D40, framebuffer.value)
        return framebuffer.value


class Scene:
    def __init__(self, gl):
        self.gl = gl
        self.render = gl.compile([(0x8B31, shader_source(TERRAIN / "render.vs.glsl")),
                                  (0x8B30, shader_source(TERRAIN / "render.fs.glsl"))], "actual packed terrain GBuffer")
        full_writer = shader_source(TERRAIN / "render.fs.glsl").replace(
            "out vec2 gPosition;", "out vec4 gPosition;").replace(
            "gPosition = geGBufferPackPosition(viewPosition, geometryTag);",
            "gPosition = vec4(viewPosition, geometryTag);")
        assert "gPosition = vec4(viewPosition, geometryTag);" in full_writer
        self.reference_render = gl.compile([(0x8B31, shader_source(TERRAIN / "render.vs.glsl")),
                                            (0x8B30, full_writer)], "reference terrain XYZ GBuffer encoding")
        self.shadow = gl.compile([(0x8B31, shader_source(TERRAIN / "shadow.vs.glsl")),
                                  (0x8B30, shader_source(TERRAIN / "shadow.fs.glsl"))], "actual packed terrain shadow")
        vertex = shader_source(ROOT / "Asset/Shader/forward.vs.glsl")
        source = shader_source(ROOT / "Asset/Shader/forward.fs.glsl")
        helper = shader_source(ROOT / "Asset/Shader/Shadow/directional_surface.glsl")
        assert source.count(helper) == 1
        witness = (HERE / "terrain_shadow_legacy.glsl").read_text()
        # The engine now carries explicit bias controls in the previous SSBO
        # padding. Preserve the frozen seven-argument implementation verbatim.
        witness += """
float geDirectionalSurfaceShadow(sampler2DArray depthMap,vec4 shadowCoord,int layer,
    mat4 projectionView,vec3 worldDx,vec3 worldDy,vec3 worldPositionError,vec2 ignoredBias)
{
    return geDirectionalSurfaceShadow(depthMap,shadowCoord,layer,projectionView,
        worldDx,worldDy,worldPositionError);
}
"""
        # The corrected deferred path receives a per-primitive normal encoded
        # by the actual geometry pass. Keep its fallback declaration available
        # when linking the witness, but restore the OLD call with GBuffer quad
        # derivatives: feeding the good stored normal to the old helper would
        # hide the integration defect that this negative control must expose.
        normal_signature = "vec3 geDirectionalSurfaceNormal("
        if normal_signature in helper:
            start = helper.index(normal_signature)
            body = helper.index("{", start)
            depth = 1
            end = body + 1
            while depth:
                depth += (helper[end] == "{") - (helper[end] == "}")
                end += 1
            witness += "\n" + helper[start:end] + "\n"
        old = source.replace(helper, witness)
        # Rewrite both directional samples (including cascade overlap), but
        # leave other lighting consumers, such as perspective spots, intact.
        def legacy_call(match):
            return match.group(0).replace("geDirectionalSurfaceShadowFromNormal(",
                                         "geDirectionalSurfaceShadow(").replace(
                                             "shadowGeometricNormal", "shadowWorldDx, shadowWorldDy")
        old, replacements = re.subn(
            r"geDirectionalSurfaceShadowFromNormal\(shadowDepth,[^;]*?shadowGeometricNormal\s*,\s*shadowPositionError",
            legacy_call, old)
        assert replacements == 2, "Both actual directional cascade calls must exercise the frozen witness"
        single = old.replace("for(int y=-1;y<=1;++y)", "for(int y=0;y<=0;++y)")
        single = single.replace("for(int x=-1;x<=1;++x)", "for(int x=0;x<=0;++x)")
        single = single.replace("return visibility/9.0;", "return visibility;")
        assert single != old and "return visibility/9.0;" not in single
        self.deferred = {"current": gl.compile([(0x8B31, vertex), (0x8B30, source)], "actual deferred terrain lighting"),
                         "frozen": gl.compile([(0x8B31, vertex), (0x8B30, old)], "frozen pre-fix nonplanar PCF witness"),
                         "frozen1": gl.compile([(0x8B31, vertex), (0x8B30, single)], "frozen center tap with fractional receiver-plane correction")}
        full_reader = source.replace(
            "geGBufferReadPosition(gPosition, ivec2(gl_FragCoord.xy), ubc.proj)",
            "texture(gPosition, v_UV)").replace(
            "float sampleDepth = texture(gPosition, offset.xy).r;",
            "float sampleDepth = texture(gPosition, offset.xy).z;")
        assert full_reader != source
        self.deferred["reference"] = gl.compile([(0x8B31, vertex), (0x8B30, full_reader)],
                                               "reference deferred XYZ GBuffer encoding")
        normal_probe = source.replace("vec2 atmosphereUV = gl_FragCoord.xy * uAtmosphereInvResolution;",
                                      "o_FragColor = shadowGeometricNormal; return;\nvec2 atmosphereUV = vec2(0);")
        assert normal_probe != source
        self.deferred["geometric"] = gl.compile([(0x8B31, vertex), (0x8B30, normal_probe)],
                                               "actual GBuffer normal decode through deferred camera rotation")
        self.eye = (512., 428., 640.)
        self.view = look_at(self.eye, CENTER, (0., 1., 0.))
        self.inverse = inverse_rigid(self.view)
        aspect = VIEW_SIZE[0] / VIEW_SIZE[1]
        f = 1 / math.tan(math.radians(70) / 2)
        near, far = .1, 8192.
        self.projection = ((f / aspect, 0., 0., 0.), (0., f, 0., 0.),
                           (0., 0., -(far + near) / (far - near), -2 * far * near / (far - near)),
                           (0., 0., -1., 0.))
        self.camera = gl.buffer(struct.pack("<52f", *self.eye, 0., *column_major(self.view),
                                            *column_major(self.inverse), *column_major(self.projection)))
        self.descriptor = gl.buffer(struct.pack("<4f4I", 0., 0., 0., EXTENT, 0, 0, 0, 0), 8)
        self.shadow_buffer = gl.buffer(bytes(320))
        self.light_direction = SUN
        self.radius = RADIUS
        self.parameters = Parameters().set("Camera", *self.eye, 0).set("Control", 0, 0, 0, 0)
        self.parameters.upload(gl)
        self.light = bytearray(80)
        struct.pack_into("<3f", self.light, 0, 512., 10000., 512.)
        struct.pack_into("<3f", self.light, 16, 30., 28., 25.)
        struct.pack_into("<3f", self.light, 32, 0., -1., 0.)
        struct.pack_into("<2f4i", self.light, 44, 10., 30., 0, 0, -1, -1)
        struct.pack_into("<2f", self.light, 68, .5, 1.5)
        self.light_buffer = gl.buffer(bytes(self.light))
        self.divers = gl.buffer(struct.pack("<i4fi", 1, 0., 1., 0., 70., 0))
        self.ssao = gl.buffer(bytes(64 * 16))
        self.planes = gl.buffer(bytes(64))
        self.gbuffer = [gl.texture(VIEW_SIZE, internal=fmt, linear=False)
                        for fmt in (RG32F, RGBA32F, RGBA32F, RGBA32F)]
        for texture in self.gbuffer:
            gl.glBindTexture(TEXTURE_2D, texture)
            gl.glTexParameteri(TEXTURE_2D, 0x2802, 0x2901); gl.glTexParameteri(TEXTURE_2D, 0x2803, 0x2901)
        self.gdepth = gl.depth(VIEW_SIZE)
        self.gframe = gl.framebuffer()
        for i, texture in enumerate(self.gbuffer):
            gl.glFramebufferTexture2D(0x8D40, 0x8CE0 + i, TEXTURE_2D, texture, 0)
        gl.glFramebufferTexture2D(0x8D40, 0x8D00, TEXTURE_2D, self.gdepth, 0)
        gl.glDrawBuffers(4, (C.c_uint * 4)(*(0x8CE0 + i for i in range(4))))
        assert gl.glCheckFramebufferStatus(0x8D40) == 0x8CD5
        self.depth = gl.depth((SHADOW_SIZE, SHADOW_SIZE, 1), TEXTURE_2D_ARRAY)
        self.empty = gl.depth((SHADOW_SIZE, SHADOW_SIZE, 1), TEXTURE_2D_ARRAY)
        self.sframe = gl.framebuffer()
        gl.glDrawBuffer(0); gl.glReadBuffer(0)
        self.bind_depth(self.empty)
        gl.glClearDepth(1); gl.glDepthMask(True); gl.glClear(0x00000100)
        self.extra = {
            5: (gl.texture((1, 1), value=(1, 0, 0, 1)), TEXTURE_2D),
            6: (gl.texture((1, 1), target=TEXTURE_CUBE_MAP, value=0), TEXTURE_CUBE_MAP),
            7: (gl.texture((1, 1), target=TEXTURE_CUBE_MAP, value=0), TEXTURE_CUBE_MAP),
            8: (gl.texture((1, 1), value=0), TEXTURE_2D),
            9: (gl.texture((1, 1), value=0), TEXTURE_2D),
            10: (gl.texture((1, 1), value=1), TEXTURE_2D),
            11: (gl.texture((1, 1), value=0), TEXTURE_2D),
            12: (gl.texture((1, 1, 1), target=TEXTURE_3D, value=0), TEXTURE_3D),
            13: (gl.texture((1, 1, 1), target=TEXTURE_3D, value=1), TEXTURE_3D),
            14: (gl.texture((1, 1), value=1), TEXTURE_2D),
            15: (gl.texture((1, 1), value=1), TEXTURE_2D),
        }
        self.vao = C.c_uint(); gl.glGenVertexArrays(1, C.byref(self.vao)); gl.arrays.append(self.vao.value)

    def bind_depth(self, texture):
        gl = self.gl
        gl.glBindFramebuffer(0x8D40, self.sframe)
        gl.glFramebufferTextureLayer(0x8D40, 0x8D00, texture, 0, 0)
        assert gl.glCheckFramebufferStatus(0x8D40) == 0x8CD5

    def set_light(self, elevation):
        angle = math.radians(elevation)
        self.light_direction = (math.cos(angle), math.sin(angle), 0.)
        struct.pack_into("<3f", self.light, 0, *(p + v * 10000 for p, v in zip(CENTER, self.light_direction)))
        struct.pack_into("<3f", self.light, 32, *(-value for value in self.light_direction))
        self.gl.write(self.light_buffer, bytes(self.light))

    def set_camera(self, eye, target, radius):
        self.eye, self.radius = eye, radius
        self.view = look_at(eye, target, (0., 1., 0.))
        self.inverse = inverse_rigid(self.view)
        self.gl.write(self.camera, struct.pack("<52f", *eye, 0., *column_major(self.view),
                                               *column_major(self.inverse), *column_major(self.projection)))
        self.parameters.set("Camera", *eye, 0).upload(self.gl)

    def geometry(self, pattern, obstacle, microdetail=False):
        gl = self.gl
        payload, self.count = mesh(pattern, obstacle)
        self.vertices = gl.buffer(payload)
        gl.glBindVertexArray(self.vao.value); gl.glBindBuffer(0x8892, self.vertices)
        for i in range(3):
            gl.glEnableVertexAttribArray(i)
            gl.glVertexAttribIPointer(i, 1, 0x1405, 12, C.c_void_p(i * 4))
        gl.bind(self.descriptor, 8); gl.bind(self.camera, 0)
        gl.glBindFramebuffer(0x8D40, self.gframe)
        gl.glViewport(0, 0, *VIEW_SIZE)
        gl.glEnable(0x0B71); gl.glDepthFunc(0x0201); gl.glDepthMask(True)
        gl.glDisable(0x0BE2); gl.glDisable(0x0B44)
        for i in range(4):
            gl.glClearBufferfv(0x1800, i, (C.c_float * 4)(0, -2 if i == 0 else 0, 0, 0))
        gl.glClearDepth(1); gl.glClear(0x00000100)
        gl.uniform(self.render, "microDetailStrength", "1f", .3 if microdetail else 0)
        gl.uniform(self.render, "microDetailScale", "1f", .01 if microdetail else 1)
        gl.uniform(self.render, "worldSeed", "1ui", 1337)
        gl.uniform(self.render, "terrainBiomeOverride", "1i", 1)
        gl.glDrawArraysInstancedBaseInstance(0x0004, 0, self.count, 1, 0)
        gl.glBindFramebuffer(0x8D40, 0)
        gl.glMemoryBarrier(gl.ALL_BARRIER_BITS)
        position = gl.texels(self.gbuffer[0], VIEW_SIZE, channels=2)
        self.worlds = []
        # Independent inverse projection for this perspective fixture, using
        # the FP32 depth actually rasterized by the production terrain shader.
        for i in range(VIEW_SIZE[0] * VIEW_SIZE[1]):
            z, tag = position[2*i:2*i+2]
            if tag != 0 and tag > -4:
                self.worlds.append(None)
                continue
            x = (2*((i % VIEW_SIZE[0])+.5)/VIEW_SIZE[0]-1)*(-z)/self.projection[0][0]
            y = (2*((i // VIEW_SIZE[0])+.5)/VIEW_SIZE[1]-1)*(-z)/self.projection[1][1]
            self.worlds.append(transform_point(self.inverse, (x,y,z)))
        gl.check("same packed terrain through production GBuffer vertex/fragment shaders")

    def rasterize_shadow(self, margin):
        gl = self.gl
        eye = tuple(p + v * (self.radius + margin) for p, v in zip(CENTER, self.light_direction))
        up = (0., 0., 1.) if abs(self.light_direction[1]) > .99 else (0., 1., 0.)
        view = look_at(eye, CENTER, up)
        projection = ortho(-self.radius, self.radius, self.radius, -self.radius, 0, 2 * self.radius + margin)
        matrix = multiply(projection, view)
        gl.write(self.shadow_buffer, struct.pack("<20f", *column_major(matrix), *eye, -1_000_000.) * 4)
        self.bind_depth(self.depth)
        gl.glViewport(0, 0, SHADOW_SIZE, SHADOW_SIZE)
        gl.glEnable(0x0B71); gl.glDepthFunc(0x0201); gl.glDepthMask(True)
        gl.glDisable(0x0BE2); gl.glDisable(0x0B44)
        gl.glClearDepth(1); gl.glClear(0x00000100)
        gl.bind(self.shadow_buffer, 0); gl.bind(self.descriptor, 8)
        gl.glBindVertexArray(self.vao.value)
        gl.uniform(self.shadow, "offsetShadow", "1i", 0)
        gl.glDrawArraysInstancedBaseInstance(0x0004, 0, self.count, 1, 0)
        gl.glBindFramebuffer(0x8D40, 0); gl.glMemoryBarrier(gl.ALL_BARRIER_BITS)
        gl.check("actual packed terrain rasterization into DEPTH_COMPONENT32/2048 shadow map")

    def lighting(self, kind, empty=False):
        gl = self.gl
        for binding, buffer in enumerate((self.camera, self.light_buffer, self.divers,
                                          self.shadow_buffer, self.ssao, self.planes)):
            gl.bind(buffer, binding)
        for i, texture in enumerate(self.gbuffer): gl.sampler(i, texture)
        gl.sampler(4, self.empty if empty else self.depth, TEXTURE_2D_ARRAY)
        for unit_, (texture, target) in self.extra.items(): gl.sampler(unit_, texture, target)
        program = self.deferred[kind]
        gl.uniform(program, "uAtmosphereActive", "1i", 0)
        gl.uniform(program, "uAtmosphereIblActive", "1i", 0)
        gl.uniform(program, "uAtmosphereInvResolution", "2f", 1 / VIEW_SIZE[0], 1 / VIEW_SIZE[1])
        return gl.draw_engine_triangle(program, VIEW_SIZE)


def visibility(scene, lit, shadow, obstacle=False):
    values = []
    for world, clear, dark in zip(scene.worlds, lit, shadow):
        if world is None or not (64 < world[0] < 960 and 64 < world[2] < 960 and world[1] < 180): continue
        if obstacle and not (abs(world[0] - 512) < 12 and abs(world[2] - 512) < 12): continue
        energy = sum(clear)
        if energy <= .01: continue
        ratio = sum(dark) / energy
        assert math.isfinite(ratio) and -.001 <= ratio <= 1.001
        values.append(max(0, min(1, ratio)))
    assert len(values) >= (20 if obstacle else 10000), ("Insufficient visible receiver coverage", len(values))
    return {"count": len(values), "false_percent": 100 * sum(v < .999 for v in values) / len(values),
            "mean_occlusion": 1 - sum(values) / len(values), "minimum_visibility": min(values),
            "maximum_visibility": max(values)}


def contact_case(scene, gap, radius):
    """A visible ground shadow from a low overhead plate with a known edge."""
    scene.set_light(60)
    step = 2 * radius / SHADOW_SIZE
    ground_step = step / scene.light_direction[1]
    quantize = lambda value: round(value / EXTENT * 65534) * EXTENT / 65534
    floor, roof = quantize(128), quantize(128 + gap)
    # Exact ray/roof-plane intersection on the flat local terrain patch.
    left = quantize(496) - (roof - floor) * scene.light_direction[0] / scene.light_direction[1]
    eye_height = min(gap * .8, 3.2)
    target = (left + eye_height * .25, floor, 512.)
    eye = (target[0], floor + eye_height, 512 + eye_height * .5)
    # The camera is below the plate and looks down. Both passes contain the
    # plate, but it cannot cover the ground in the visible camera's direction.
    scene.set_camera(eye, target, radius)
    scene.geometry("contact", float(gap))
    scene.rasterize_shadow(4000)
    clear = scene.lighting("current", empty=True)
    shaded = scene.lighting("current")
    rows = [[] for _ in range(VIEW_SIZE[1])]
    outside, interior, fractional = [], [], 0
    for index, (world, a, b) in enumerate(zip(scene.worlds, clear, shaded)):
        if world is None or abs(world[1] - floor) > .02 or not 500 < world[2] < 524: continue
        energy = sum(a)
        if energy <= .01: continue
        value = min(1, max(0, sum(b) / energy))
        distance = world[0] - left
        rows[index // VIEW_SIZE[0]].append((distance, value))
        if distance < -1.5 * ground_step: outside.append(value)
        if distance > 2 * ground_step: interior.append(value)
        fractional += .01 < value < .99

    def crossing(row, target_value):
        row = sorted(row)
        for (a, av), (b, bv) in zip(row, row[1:]):
            if av >= target_value > bv:
                return a + (target_value - av) * (b - a) / (bv - av)
        return None

    boundaries, widths = [], []
    for row in rows:
        a, center, b = (crossing(row, v) for v in (.9, .5, .1))
        if center is not None: boundaries.append(center)
        if a is not None and b is not None: widths.append(b - a)
    assert len(outside) > 20 and len(interior) > 20 and len(boundaries) > 20 and widths, (
        "Contact camera does not expose both sides and the edge of the geometric shadow",
        gap, radius, len(outside), len(interior), len(boundaries), len(widths),
    )
    data = {"gap_m": gap, "shadow_texel_m": step, "outside_pixels": len(outside),
            "inside_pixels": len(interior), "outside_mean_visibility": sum(outside) / len(outside),
            "inside_mean_occlusion": 1 - sum(interior) / len(interior),
            "max_edge_error_ground_texels": max(abs(value) for value in boundaries) / ground_step,
            "max_transition_width_ground_texels": max(widths) / ground_step,
            "fractional_pixels": fractional}
    print("low real caster/edge", data, flush=True)
    assert data["outside_mean_visibility"] > .995, ("Low caster darkened clear receivers beyond the PCF footprint", data)
    assert data["inside_mean_occlusion"] > .98, ("Bias erased a caster at resolvable height", data)
    assert data["max_edge_error_ground_texels"] < 1.5, ("Shadow edge detached beyond its sampling footprint", data)
    assert data["max_transition_width_ground_texels"] < 2.5 and fractional > 20, (
        "Shadow edge lacks a bounded filtered transition", data,
    )
    return data


def microdetail_case(scene):
    scene.set_camera((512., 428., 640.), CENTER, RADIUS)
    scene.set_light(60)
    scene.geometry("bumps", False)
    scene.rasterize_shadow(4000)
    positions = scene.gl.texels(scene.gbuffer[0], VIEW_SIZE, channels=2)
    normals = scene.gl.texels(scene.gbuffer[1], VIEW_SIZE)
    lit, shaded = scene.lighting("current", empty=True), scene.lighting("current")
    scene.geometry("bumps", False, microdetail=True)
    detailed_positions = scene.gl.texels(scene.gbuffer[0], VIEW_SIZE, channels=2)
    detailed_normals = scene.gl.texels(scene.gbuffer[1], VIEW_SIZE)
    detailed_lit, detailed_shaded = scene.lighting("current", empty=True), scene.lighting("current")
    assert detailed_positions == positions, "Microdetail changed actual rasterized terrain positions"
    normal_change = max(abs(a - b) for i, (a, b) in enumerate(zip(normals, detailed_normals)) if i % 4 != 3)
    assert normal_change > .001, "Microdetail test was silently disabled by its distance fade"
    ratios = []
    for a, b, c, d in zip(lit, shaded, detailed_lit, detailed_shaded):
        if sum(a) > .01 and sum(c) > .01:
            ratios.append(abs(sum(b) / sum(a) - sum(d) / sum(c)))
    assert ratios and max(ratios) < 2e-5, "Decorative normal detail changed geometric shadow visibility"
    print("microdetail geometry invariant", {"max_normal_change": normal_change,
          "max_visibility_change": max(ratios), "positions_bit_identical": True}, flush=True)


def stored_normal_case(scene):
    """Compare the production decoded normal with actual packed triangles.

    Neither the CPU oracle nor the frozen witness computes a shadow bias.
    The oracle picks a triangle using its decoded XYZ geometry, independently
    of the normal codec and of the PCF implementation.
    """
    scene.set_camera((512., 428., 640.), CENTER, RADIUS)
    scene.set_light(60)
    scene.geometry("bumps", False)
    scene.rasterize_shadow(4000)
    gl = scene.gl
    positions = gl.texels(scene.gbuffer[0], VIEW_SIZE, channels=2)
    tags = [positions[i * 2 + 1] for i, world in enumerate(scene.worlds) if world is not None]
    assert len(tags) > 10000 and all(-1048579 <= tag <= -4 and tag == round(tag) for tag in tags), (
        "The actual terrain writer did not preserve integer geometric normal tags in RG32F"
    )
    decoded = scene.lighting("geometric")
    payload, count = mesh("bumps", False)
    vertices = []
    for i in range(count):
        xy, z_colour, _ = struct.unpack_from("<III", payload, i * 12)
        vertices.append(((xy & 65535) * EXTENT / 65534, (xy >> 16) * EXTENT / 65534,
                         (z_colour & 65535) * EXTENT / 65534))

    def geometric_normal(point):
        x, y, z = point
        # Quantized grid knots differ slightly from integer world positions;
        # inspect adjacent cells instead of assuming a mathematical grid hit.
        for iz in range(max(0, int(z // 16) - 1), min(64, int(z // 16) + 2)):
            for ix in range(max(0, int(x // 16) - 1), min(64, int(x // 16) + 2)):
                start = (iz * 64 + ix) * 6
                for index in (start, start + 3):
                    a, b, c = vertices[index:index + 3]
                    denominator = (b[2] - c[2]) * (a[0] - c[0]) + (c[0] - b[0]) * (a[2] - c[2])
                    wa = ((b[2] - c[2]) * (x - c[0]) + (c[0] - b[0]) * (z - c[2])) / denominator
                    wb = ((c[2] - a[2]) * (x - c[0]) + (a[0] - c[0]) * (z - c[2])) / denominator
                    wc = 1 - wa - wb
                    # Exactly on an edge either neighbouring primitive may
                    # win rasterization. Skip that ambiguity, not its vicinity.
                    if min(wa, wb, wc) > .003:
                        assert abs(wa * a[1] + wb * b[1] + wc * c[1] - y) < .002
                        return normalized(cross(tuple(v - u for u, v in zip(a, b)),
                                                tuple(v - u for u, v in zip(a, c))))
        return None

    errors = []
    # Explicitly include the three worst pre-fix crease pixels as well as a
    # regular spread across the whole rendered heightfield.
    selected = set(range(5, len(scene.worlds), 97)) | {110 * VIEW_SIZE[0] + 175,
                                                     99 * VIEW_SIZE[0] + 151,
                                                     59 * VIEW_SIZE[0] + 109}
    for i in selected:
        point = scene.worlds[i]
        if point is None:
            continue
        expected = geometric_normal(point)
        if expected is not None:
            agreement = min(1, max(-1, abs(dot(expected, normalized(decoded[i])))))
            errors.append(math.degrees(math.acos(agreement)))
    assert len(errors) > 150 and max(errors) < .5, (
        "Stored/deferred geometric normal disagrees with the actual rasterized triangle", len(errors), max(errors)
    )

    # Strip only the tag, retaining identical positions, material normals,
    # depth, light and geometry. The legacy deferred quad fallback must expose
    # the remaining crease artifact; the real tag path must remain clear.
    lit = scene.lighting("current", empty=True)
    tagged = visibility(scene, lit, scene.lighting("current"))
    original = scene.gbuffer[0]
    rows = [(positions[i], 0 if positions[i + 1] <= -4 else positions[i + 1])
            for i in range(0, len(positions), 2)]
    scene.gbuffer[0] = gl.field_texture(VIEW_SIZE, rows, repeat=True, internal=RG32F)
    fallback = visibility(scene, lit, scene.lighting("current"))
    scene.gbuffer[0] = original
    print("per-primitive geometric normal", {"encoded_pixels": len(tags), "independent_triangle_checks": len(errors),
          "max_angular_error_degrees": max(errors), "tagged_false_percent": tagged["false_percent"],
          "legacy_quad_false_percent": fallback["false_percent"]}, flush=True)
    assert tagged["false_percent"] == 0 and fallback["false_percent"] > 0 and fallback["minimum_visibility"] < .99, (
        "Normal tag negative control no longer distinguishes per-primitive geometry from deferred quads", tagged, fallback
    )



def packing_ab_case(scene):
    """Compare actual terrain+deferred HDR against the previous XYZ layout.

    Only the writer/readback encoding changes in the reference shaders; all
    current PBR, normals, shadow maps, PCF, light and vertex code stay identical.
    """
    gl=scene.gl
    full_position=gl.texture(VIEW_SIZE,internal=RGBA32F,linear=False)
    gl.glBindTexture(TEXTURE_2D,full_position)
    gl.glTexParameteri(TEXTURE_2D,0x2802,0x2901)
    gl.glTexParameteri(TEXTURE_2D,0x2803,0x2901)
    worst=0.;cases=0
    scene.set_camera((512.,428.,640.),CENTER,RADIUS)
    for pattern,obstacle,elevation in (("bumps",False,90),("bumps",True,60),("creases",False,60)):
        scene.set_light(elevation)
        scene.geometry(pattern,obstacle)
        scene.rasterize_shadow(4000)
        packed=scene.lighting("current")
        packed_position=scene.gbuffer[0]
        gl.glBindFramebuffer(0x8D40,scene.gframe)
        gl.glFramebufferTexture2D(0x8D40,0x8CE0,TEXTURE_2D,full_position,0)
        gl.glViewport(0,0,*VIEW_SIZE)
        gl.glEnable(0x0B71);gl.glDepthFunc(0x0201);gl.glDepthMask(True)
        gl.glClearBufferfv(0x1800,0,(C.c_float*4)(0,0,0,-2))
        gl.glClearDepth(1);gl.glClear(0x00000100)
        gl.bind(scene.descriptor,8);gl.bind(scene.camera,0)
        gl.glBindVertexArray(scene.vao.value)
        gl.uniform(scene.reference_render,"microDetailStrength","1f",0)
        gl.uniform(scene.reference_render,"microDetailScale","1f",1)
        gl.uniform(scene.reference_render,"worldSeed","1ui",1337)
        gl.uniform(scene.reference_render,"terrainBiomeOverride","1i",1)
        gl.glDrawArraysInstancedBaseInstance(0x0004,0,scene.count,1,0)
        gl.glBindFramebuffer(0x8D40,0);gl.glMemoryBarrier(gl.ALL_BARRIER_BITS)
        scene.gbuffer[0]=full_position
        reference=scene.lighting("reference")
        scene.gbuffer[0]=packed_position
        gl.glBindFramebuffer(0x8D40,scene.gframe)
        gl.glFramebufferTexture2D(0x8D40,0x8CE0,TEXTURE_2D,packed_position,0)
        gl.glBindFramebuffer(0x8D40,0)
        # L/(1+L) is used only as an independent finite display-range error
        # metric; it does not alter either actual HDR render.
        difference=max(abs(a/(1+a)-b/(1+b)) for left,right in zip(packed,reference)
                       for a,b in zip(left,right))
        assert difference<1/1024,("Packed GBuffer changed visible deferred radiance",pattern,obstacle,difference)
        worst=max(worst,difference);cases+=1
    print(f"PASS packed/reference real terrain+deferred A/B: {cases} cases, max normalized radiance difference={worst:.9f}",flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--diagnose", action="store_true", help="Report the production defect before its correction")
    args = parser.parse_args()
    results = []
    with TerrainGL() as gl:
        print(f"OpenGL {gl.version}; renderer: {gl.renderer}", flush=True)
        scene = Scene(gl)
        for elevation in (90, 60):
            scene.set_light(elevation)
            for pattern in ("bumps", "creases", "monotone"):
                scene.geometry(pattern, False)
                for margin in (0, 4000, 16000):
                    scene.rasterize_shadow(margin)
                    lit = scene.lighting("current", empty=True)
                    for kind in ("frozen", "frozen1", "current"):
                        data = visibility(scene, lit, scene.lighting(kind))
                        results.append((pattern, elevation, margin, kind, data))
                        print(pattern, f"sun={elevation}", f"margin={margin}", kind, data, flush=True)
        packing_ab_case(scene)
        stored_normal_case(scene)
        scene.set_light(90)
        scene.geometry("bumps", True)
        scene.rasterize_shadow(4000)
        lit = scene.lighting("current", empty=True)
        for kind in ("frozen", "current"):
            data = visibility(scene, lit, scene.lighting(kind), obstacle=True)
            print("real raised caster", kind, data, flush=True)
            assert data["mean_occlusion"] > .98, ("Real off-screen occluder lost", kind, data)
        microdetail_case(scene)
        for gap, radius in ((2., 1024.), (4., 1024.), (.5, 128.)):
            contact_case(scene, gap, radius)
        gl.check("nonplanar terrain shadow integration")
    assert max(data["false_percent"] for _, _, _, kind, data in results if kind == "frozen") > 5, (
        "Frozen planar-PCF witness no longer reproduces the nonplanar defect"
    )
    if not args.diagnose:
        for pattern, elevation, margin, kind, data in results:
            if kind == "current":
                assert data["false_percent"] == 0, ("Terrain falsely shadows itself under a high source", pattern, elevation, margin, data)
        print("PASS production nonplanar terrain: no false self-shadow, true elevated caster preserved; frozen helper demonstrably fails", flush=True)


if __name__ == "__main__":
    main()
