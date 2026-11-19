#!/usr/bin/env python3
"""Render the real deferred caller with ModelManager's actual fullscreen mesh.

The legacy triangle interpolates negative Y UVs. GBuffer textures repeat, but
the atmosphere volumes clamp: feeding the same UV into both silently samples
one atmosphere row. This integration gate draws the production forward.vs and
forward.fs with the indexed mesh extracted from C++, then checks every output
pixel against independently filtered, nonuniform S/T fields.

No production shader or shared fixture is rewritten. A negative-control
program changes only the two atmosphere call sites in memory, demonstrating
that the old caller fails with the exact same geometry and inputs.
"""
from __future__ import annotations

import ctypes as C
import math
import re
import struct

from check_luts import finite
from fixtures import Parameters
from gl_support import (
    AtmosphereGL, FLOAT, R32F, RED, RG, RGBA, RG32F, RGBA32F, ROOT,
    TEXTURE_2D, TEXTURE_2D_ARRAY, TEXTURE_3D, TEXTURE_CUBE_MAP,
    shader_source,
)

SHADERS = ROOT / "Asset/Shader"
MODEL_MANAGER = ROOT / "ANTAGONIZ/Source/Rendering/Shape/ModelManager.cpp"


def read_engine_triangle():
    """Extract the actual allocation arguments and their C++ initializers."""
    raw = MODEL_MANAGER.read_bytes()
    try:
        source = raw.decode("utf-8")
    except UnicodeDecodeError:
        source = raw.decode("cp1252")
    call = re.search(
        r"m_fullScreenTriangle\s*=\s*allocateBuffer\(\s*(\w+)\s*,\s*(\w+)\s*,"
        r"\s*(\w+)\s*,\s*(\w+)\s*,\s*(\d+)\s*,\s*(\d+)\s*\)", source,
    )
    assert call, "Cannot locate ModelManager's fullscreen triangle allocation"
    arrays = []
    for name in call.groups()[:4]:
        initializer = re.search(r"\b" + re.escape(name) + r"\s*\[[^]]*\]\s*=\s*\{([^}]+)\}", source)
        assert initializer, f"Cannot extract actual fullscreen array {name}"
        arrays.append([float(value) for value in re.findall(
            r"[-+]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][-+]?\d+)?", initializer.group(1),
        )])
    count, index_count = map(int, call.groups()[4:])
    positions, uvs, normals, indices = arrays
    assert len(positions) == 3 * count and len(normals) == 3 * count
    assert len(uvs) == 2 * count and len(indices) == index_count
    assert count == index_count == 3 and all(value.is_integer() for value in indices)
    return positions, uvs, normals, [int(value) for value in indices]


class ScreenGL(AtmosphereGL):
    """Local mesh-draw support; shared GPU test infrastructure stays unchanged."""
    def __init__(self):
        super().__init__()
        self._fn("glGetAttribLocation", C.c_int, C.c_uint, C.c_char_p)
        self._fn("glEnableVertexAttribArray", None, C.c_uint)
        self._fn("glVertexAttribPointer", None, C.c_uint, C.c_int, C.c_uint,
                 C.c_ubyte, C.c_int, C.c_void_p)
        self._fn("glDrawElements", None, C.c_uint, C.c_int, C.c_uint, C.c_void_p)
        self.mesh = read_engine_triangle()
        self.mesh_arrays = {}

    def mesh_array(self, program):
        if program in self.mesh_arrays:
            return self.mesh_arrays[program]
        vao = C.c_uint()
        self.glGenVertexArrays(1, C.byref(vao))
        self.arrays.append(vao.value)
        self.glBindVertexArray(vao.value)
        for name, width, values in (("a_Position", 3, self.mesh[0]),
                                    ("a_TexCoords", 2, self.mesh[1]),
                                    ("a_Normal", 3, self.mesh[2])):
            location = self.glGetAttribLocation(program, name.encode())
            if location < 0:
                assert name != "a_Position", "Fullscreen position attribute is inactive"
                # Geometry-only probes use gl_FragCoord with compact depth and
                # legitimately optimize both normal and texture-coordinate inputs.
                continue
            buffer = self.buffer(struct.pack(f"<{len(values)}f", *values))
            self.glBindBuffer(0x8892, buffer)  # GL_ARRAY_BUFFER
            self.glEnableVertexAttribArray(location)
            self.glVertexAttribPointer(location, width, FLOAT, False, 0, None)
        indices = self.mesh[3]
        elements = self.buffer(struct.pack(f"<{len(indices)}I", *indices))
        self.glBindBuffer(0x8893, elements)  # GL_ELEMENT_ARRAY_BUFFER is VAO state.
        self.mesh_arrays[program] = vao.value
        return vao.value

    def draw_engine_triangle(self, program, size):
        previous_texture = C.c_int()
        self.glGetIntegerv(0x8069, C.byref(previous_texture))
        output = self.texture(size, internal=RGBA32F)  # NaN-poison uncovered pixels.
        self.glBindTexture(TEXTURE_2D, previous_texture.value)
        framebuffer = C.c_uint()
        self.glGenFramebuffers(1, C.byref(framebuffer))
        self.framebuffers.append(framebuffer.value)
        self.glBindFramebuffer(0x8D40, framebuffer.value)
        self.glFramebufferTexture2D(0x8D40, 0x8CE0, TEXTURE_2D, output, 0)
        self.glDrawBuffers(1, (C.c_uint * 1)(0x8CE0))
        assert self.glCheckFramebufferStatus(0x8D40) == 0x8CD5
        self.glViewport(0, 0, *size)
        for capability in (0x0B71, 0x0BE2, 0x0B44):  # Depth, blend, cull.
            self.glDisable(capability)
        self.glBindVertexArray(self.mesh_array(program))
        self.glUseProgram(program)
        self.glDrawElements(0x0004, len(self.mesh[3]), 0x1405, None)
        self.glBindFramebuffer(0x8D40, 0)
        values = self.texels(output, size)
        finite(values, "actual fullscreen triangle framebuffer")
        self.check("production deferred draw")
        return [values[i:i + 3] for i in range(0, len(values), 4)]

    def field_texture(self, size, rows, *, repeat=False, internal=RGBA32F):
        target = TEXTURE_3D if len(size) == 3 else TEXTURE_2D
        texture = self.texture(size, target=target, internal=internal)
        channels = 2 if internal == RG32F else 4
        format_ = RG if internal == RG32F else RGBA
        payload = b"".join(struct.pack(f"<{channels}f", *row) for row in rows)
        assert len(rows) == math.prod(size)
        self.glBindTexture(target, texture)
        if target == TEXTURE_3D:
            self.glTexImage3D(target, 0, internal, *size, 0, format_, FLOAT, C.c_char_p(payload))
        else:
            self.glTexImage2D(target, 0, internal, *size, 0, format_, FLOAT, C.c_char_p(payload))
        if repeat:
            # FrameBuffer.cpp uses nearest filtering for the GBuffer. Only
            # the atmosphere fields use linear reconstruction in this test.
            self.glTexParameteri(target, 0x2800, 0x2600)
            self.glTexParameteri(target, 0x2801, 0x2600)
            for axis in (0x2802, 0x2803):
                self.glTexParameteri(target, axis, 0x2901)  # GL_REPEAT, as for legacy GBuffer UVs.
        return texture


def multiply(matrix, vector):
    return tuple(sum(a * b for a, b in zip(row, vector)) for row in matrix)


def unit(vector):
    length = math.sqrt(sum(value * value for value in vector))
    return tuple(value / length for value in vector)


def camera(pitch_degrees, yaw_degrees, aspect, position=(120., 800., -450.)):
    pitch, yaw = map(math.radians, (pitch_degrees, yaw_degrees))
    cp, sp, cy, sy = math.cos(pitch), math.sin(pitch), math.cos(yaw), math.sin(yaw)
    rotation = ((cy, sy * sp, sy * cp), (0., cp, -sp), (-sy, cy * sp, cy * cp))
    inverse = [rotation[row][column] if row < 3 else 0.
               for column in range(3) for row in range(4)] + [*position, 1.]
    transpose = tuple(zip(*rotation))
    translation = multiply(transpose, tuple(-value for value in position))
    view = [transpose[row][column] if row < 3 else 0.
            for column in range(3) for row in range(4)] + [*translation, 1.]
    f = 1. / math.tan(math.radians(75) / 2)
    near, far = 1., 10000.
    projection = [f / aspect, 0, 0, 0, 0, f, 0, 0,
                  0, 0, -(far + near) / (far - near), -1,
                  0, 0, -2 * far * near / (far - near), 0]
    return position, rotation, view, inverse, projection


def view_ray(u, v, aspect):
    scale = math.tan(math.radians(75) / 2)
    return unit(((2 * u - 1) * aspect * scale, (2 * v - 1) * scale, -1.))


def filtered(field, size, u, v):
    """Independent CPU bilinear reconstruction, including clamped edge texels."""
    width, height = size
    px, py = u * width - .5, v * height - .5
    x, y = math.floor(px), math.floor(py)
    fx, fy = px - x, py - y
    result = [0., 0., 0.]
    for dx, wx in ((0, 1 - fx), (1, fx)):
        for dy, wy in ((0, 1 - fy), (1, fy)):
            ix, iy = min(max(x + dx, 0), width - 1), min(max(y + dy, 0), height - 1)
            value = field[iy * width + ix]
            for channel in range(3):
                result[channel] += value[channel] * wx * wy
    return result


def maximum_error(first, second):
    return max(abs(a - b) for left, right in zip(first, second) for a, b in zip(left, right))


class DeferredFixture:
    def __init__(self, gl, size, pitch, yaw, mode, camera_position=(120., 800., -450.)):
        self.gl, self.size, self.mode = gl, size, mode
        width, height = size
        aspect = width / height
        position, rotation, view, inverse, projection = camera(pitch, yaw, aspect, camera_position)
        parameters = Parameters()
        parameters.set("Camera", *position, 0)
        parameters.set("Control", 0, 1, 0, 0)
        parameters.set("Volume", 4000, .5, 0, 0)
        parameters.set("Art", 1, 1, 1, .2)
        parameters.matrices["View"] = view
        parameters.matrices["InvView"] = inverse
        self.parameters = parameters
        self.uniforms = parameters.upload(gl)
        self.camera_buffer = gl.buffer(struct.pack("<52f", *position, 0, *view, *inverse, *projection), 0)
        gl.buffer(bytes(80), 1)  # No direct lights; IBL still exercises real PBR opaque shading.
        gl.buffer(struct.pack("<i4fi", 0, 0., 1., .7, 75., 0), 2)
        gl.buffer(bytes(80), 3)
        gl.buffer(struct.pack("<4f", .1, .2, .3, 0) * 64, 4)
        gl.buffer(bytes(64), 5)

        positions, colours, normals, others = [], [], [], []
        normal = multiply(rotation, (0., 0., 1.))
        for y in range(height):
            for x in range(width):
                u, v = (x + .5) / width, (y + .5) / height
                ray = view_ray(u, v, aspect)
                positions.append((4000 * ray[2], -2. if mode == "sky" else 0.))
                colours.append((2. + 1.1 * u, .6 + .7 * v, 1. + .3 * u + .6 * v, 1.)
                               if mode == "sky" else (.3 + .4 * u, .2 + .6 * v, .1 + .2 * u + .1 * v, 1.))
                normals.append(normal + (1.,))
                others.append((.3, .5, 1., 1.))
        self.colours = [row[:3] for row in colours]
        self.samplers = {unit_: (gl.field_texture(size, rows, repeat=True, internal=RG32F if unit_ == 0 else RGBA32F), TEXTURE_2D)
                         for unit_, rows in enumerate((positions, normals, colours, others))}
        self.samplers.update({
            4: (gl.texture((2, 2, 1), target=TEXTURE_2D_ARRAY, internal=R32F, value=1), TEXTURE_2D_ARRAY),
            5: (gl.texture((2, 2), internal=RGBA32F, value=(1, 0, 0, 1)), TEXTURE_2D),
            6: (gl.texture((2, 2), target=TEXTURE_CUBE_MAP, value=(.8, .6, .4, 1)), TEXTURE_CUBE_MAP),
            7: (gl.texture((16, 16), target=TEXTURE_CUBE_MAP, levels=5, value=(.15, .12, .08, 1)), TEXTURE_CUBE_MAP),
            8: (gl.texture((2, 2), value=(.3, .1, 0, 1)), TEXTURE_2D),
            11: (gl.texture((1, 1), value=0), TEXTURE_2D),
            14: (gl.texture((1, 1), value=1), TEXTURE_2D),
            15: (gl.texture((1, 1), value=1), TEXTURE_2D),
        })
        self.volume_size = (7, 5, 5)
        self.scattering, self.transmission, self.baseline_s, self.baseline_t = [], [], [], []
        for y in range(5):
            for x in range(7):
                u, v = (x + .5) / 7, (y + .5) / 5
                direction = multiply(rotation, view_ray(u, v, aspect))
                self.scattering.append((.1 + .7 * u + .4 * v + .2 * max(direction[1], 0),
                                        .2 + .6 * v + .1 * u + .15 * max(direction[0], 0),
                                        .15 + .45 * u + .35 * v + .1 * direction[2]**2, 1))
                self.transmission.append((.9 - .2 * u - .15 * v, .85 - .1 * u - .3 * v,
                                          .8 - .25 * u - .1 * v, 1))
                self.baseline_s.append((.03 + .1 * u, .02 + .08 * v, .04 + .03 * u + .04 * v, 1))
                self.baseline_t.append((.98 - .02 * v, .98 - .02 * u, .97 - .02 * v, 1))
        scattering, transmission = [], []
        for z in range(5):
            progress = z / 4
            scattering.extend(tuple(value * progress for value in row[:3]) + (1.,) for row in self.scattering)
            transmission.extend(tuple(1 - (1 - value) * progress for value in row[:3]) + (1.,) for row in self.transmission)
        self.samplers.update({
            9: (gl.field_texture((7, 5), self.baseline_s), TEXTURE_2D),
            10: (gl.field_texture((7, 5), self.baseline_t), TEXTURE_2D),
            12: (gl.field_texture(self.volume_size, scattering), TEXTURE_3D),
            13: (gl.field_texture(self.volume_size, transmission), TEXTURE_3D),
        })
        self.disabled_source = gl.texture((1, 1, 1), target=TEXTURE_3D, value=(100, 200, 300, 1))
        self.disabled_transmission = gl.texture((1, 1, 1), target=TEXTURE_3D, value=(.1, .2, .3, 1))

    def render(self, program, active, perturb_disabled=False):
        gl = self.gl
        self.parameters.upload(gl, self.uniforms)
        gl.bind(self.camera_buffer, 0)
        for unit_, (texture, target) in self.samplers.items():
            gl.sampler(unit_, texture, target)
        if perturb_disabled:
            gl.sampler(12, self.disabled_source, TEXTURE_3D)
            gl.sampler(13, self.disabled_transmission, TEXTURE_3D)
        gl.uniform(program, "uAtmosphereActive", "1i", int(active))
        gl.uniform(program, "uAtmosphereIblActive", "1i", 0)
        gl.uniform(program, "uAtmosphereSunIndex", "1i", -1)
        gl.uniform(program, "uAtmosphereMoonIndex", "1i", -1)
        gl.uniform(program, "uAtmosphereInvResolution", "2f", 1 / self.size[0], 1 / self.size[1])
        return gl.draw_engine_triangle(program, self.size)

    def expected(self, surface):
        result = []
        width, height = self.size
        for y in range(height):
            for x in range(width):
                u, v = (x + .5) / width, (y + .5) / height
                s = filtered(self.scattering, (7, 5), u, v)
                t = filtered(self.transmission, (7, 5), u, v)
                raw = surface[y * width + x]
                if self.mode == "sky":
                    bs = filtered(self.baseline_s, (7, 5), u, v)
                    bt = filtered(self.baseline_t, (7, 5), u, v)
                    result.append(tuple(a + (value - b) * transmission / reference
                                        for a, value, b, transmission, reference in zip(s, raw, bs, t, bt)))
                else:
                    result.append(tuple(value * transmission + a for value, transmission, a in zip(raw, t, s)))
        return result


def check_screen_composition(gl):
    vertex = shader_source(SHADERS / "forward.vs.glsl")
    fragment = shader_source(SHADERS / "forward.fs.glsl")
    production = gl.compile([(0x8B31, vertex), (0x8B30, fragment)], "actual deferred screen composition")
    legacy, sky_count = re.subn(
        r"o_FragColor\s*=\s*geAtmCompositeSky\([^;]+\);",
        "o_FragColor = geAtmCompositeSky(col.rgb, v_UV);", fragment,
    )
    legacy, opaque_count = re.subn(
        r"o_FragColor\s*=\s*geAtmComposite\([^;]+\);",
        "o_FragColor = geAtmComposite(color, worldPos, v_UV);", legacy,
    )
    assert sky_count == opaque_count == 1, "Cannot isolate the two real atmosphere call sites for negative control"
    broken = gl.compile([(0x8B31, vertex), (0x8B30, legacy)], "negative control: former v_UV atmosphere callers")
    uv_program = gl.compile([(0x8B31, vertex), (0x8B30, """#version 450
in vec2 v_UV;
out vec4 observed;
void main() { observed=vec4(v_UV,0,1); }
""")], "actual engine triangle UV observation")
    observed = gl.draw_engine_triangle(uv_program, (17, 11))
    expected_uv = [((x + .5) / 17, (y + .5) / 11 - 1, 0)
                   for y in range(11) for x in range(17)]
    assert maximum_error(observed, expected_uv) < 1e-6, "The real triangle's legacy UV convention changed; revisit this integration test"
    print("PASS real mesh: positions, UVs and indices extracted from ModelManager.cpp; production forward.vs interpolates Y in [-1,0], with complete screen coverage", flush=True)

    worst, edge_worst, negative_errors, cases = 0., 0., {}, 0
    for size in ((37, 23), (65, 17), (19, 41)):
        for pitch, yaw in ((0, 0), (72, 35), (-68, -110)):
            for mode in ("sky", "opaque"):
                fixture = DeferredFixture(gl, size, pitch, yaw, mode)
                disabled = fixture.render(production, False)
                assert maximum_error(disabled, fixture.render(production, False, True)) < 1e-6, (
                    "Disabled atmosphere changed the real deferred output", size, pitch, mode,
                )
                if mode == "sky":
                    assert maximum_error(disabled, fixture.colours) < 2e-5, "Legacy repeating GBuffer coordinates did not preserve the sky image"
                else:
                    assert max(value for row in disabled for value in row) > .02, "Opaque PBR fixture is black and cannot establish composition"
                expected = fixture.expected(disabled)
                actual = fixture.render(production, True)
                error = maximum_error(actual, expected)
                assert error < 5e-5, (
                    "Real deferred atmosphere composition samples the wrong screen position",
                    mode, size, pitch, yaw, "maximum HDR error", error,
                )
                width, height = size
                edges = [y * width + x for y in range(height) for x in range(width)
                         if x in (0, width - 1) or y in (0, height - 1)]
                edge_error = maximum_error([actual[i] for i in edges], [expected[i] for i in edges])
                worst, edge_worst = max(worst, error), max(edge_worst, edge_error)
                if mode not in negative_errors:
                    old = fixture.render(broken, True)
                    negative_errors[mode] = maximum_error(old, expected)
                    assert negative_errors[mode] > .1, (
                        "Negative control did not detect the former atmosphere caller", mode, negative_errors[mode],
                    )
                cases += 1
    print(f"PASS actual deferred sky + PBR opaque: {cases} cases, 3 resolutions/aspects, camera pitch 0/+72/-68 degrees, nonuniform X/Y volume reconstruction and all four edges; max HDR error {worst:.8f}, edge error {edge_worst:.8f}", flush=True)
    print(f"PASS negative control: restoring only the old v_UV callers yields sky error {negative_errors['sky']:.4f} and opaque error {negative_errors['opaque']:.4f}; disabled atmosphere remains neutral", flush=True)
    check_directional_caller(gl, production, vertex, fragment)


def check_directional_caller(gl, production, vertex, fragment):
    """Exercise the real directional-light branch with a rotated world plane."""
    size, pitch, yaw = (53, 37), -68, 35
    # Keep the geometric blocker resolved by this deliberately small map.
    # The atmospheric UV cases above retain their 800 m camera altitude.
    camera_position = (120., 8., -450.)
    fixture = DeferredFixture(gl, size, pitch, yaw, "opaque", camera_position)
    width, height = size
    position, rotation, _, _, _ = camera(pitch, yaw, width / height, camera_position)
    view_positions, world_positions = [], []
    for y in range(height):
        for x in range(width):
            ray = view_ray((x + .5) / width, (y + .5) / height, width / height)
            world_ray = multiply(rotation, ray)
            assert world_ray[1] < 0, "The selected camera must see the ground plane in every pixel"
            distance = -position[1] / world_ray[1]
            view_positions.append((ray[2] * distance, 0.))
            world_positions.append(tuple(origin + value * distance for origin, value in zip(position, world_ray)))
    fixture.samplers[0] = (gl.field_texture(size, view_positions, repeat=True, internal=RG32F), TEXTURE_2D)
    for unit_, colour in ((1, (0., 1., 0., 1.)), (2, (.65, .5, .3, 1.)), (3, (0., .8, 1., 1.))):
        fixture.samplers[unit_] = (gl.field_texture(size, [colour] * (width * height), repeat=True), TEXTURE_2D)
    for unit_ in (6, 7):
        fixture.samplers[unit_] = (gl.texture((2, 2), target=TEXTURE_CUBE_MAP, value=0), TEXTURE_CUBE_MAP)
    # Direct lighting only, with an SSAO kernel whose subpixel samples are in
    # front of this plane. No IBL or atmosphere can mask an erroneous shadow.
    gl.buffer(struct.pack("<i4fi", 1, 0., 1., 0., 75., 0), 2)
    gl.buffer(struct.pack("<4f", 0., 0., 1., 0.) * 64, 4)
    light = unit((.35, .3, .887))
    light_data = bytearray(80)  # Published std430 StructUBL stride.
    struct.pack_into("<3f", light_data, 0, *(a + 10000 * b for a, b in zip(position, light)))
    struct.pack_into("<3f", light_data, 16, 30., 28., 25.)
    struct.pack_into("<3f", light_data, 32, *(-value for value in light))
    struct.pack_into("<2f4i", light_data, 44, 10., 30., 0, 0, -1, -1)
    struct.pack_into("<2f", light_data, 68, .5, 1.5)  # Directional depth/normal bias in texels.
    gl.buffer(bytes(light_data), 1)

    def dot(a, b):
        return sum(x * y for x, y in zip(a, b))

    def cross(a, b):
        return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])

    center_ray = multiply(rotation, (0., 0., -1.))
    center = tuple(origin - value * position[1] / center_ray[1]
                   for origin, value in zip(position, center_ray))
    axis_u = unit(cross((0., 1., 0.), light))
    axis_v = cross(light, axis_u)
    relative = [tuple(value - origin for value, origin in zip(world, center)) for world in world_positions]
    extent_u = 1.25 * max(abs(dot(axis_u, value)) for value in relative)
    extent_v = 1.25 * max(abs(dot(axis_v, value)) for value in relative)
    depth_extent = max(4 * extent_v * axis_v[1] / light[1], 1000.)
    # The same signed-Y orthographic convention as the real engine. A point
    # moving towards the light has a smaller stored depth.
    rows = [tuple(value / extent_u for value in axis_u),
            tuple(-value / extent_v for value in axis_v),
            tuple(-value / depth_extent for value in light)]
    projection_view = [rows[row][column] if row < 3 else 0.
                       for column in range(3) for row in range(4)]
    projection_view += [-dot(row, center) for row in rows] + [1.]
    shadow = struct.pack("<20f", *projection_view, 0., 0., 0., -1_000_000.)
    gl.buffer(shadow * 4, 3)  # Actual cascade array; this scene selects cascade 0.

    depth_size = (256, 256, 4)  # <=0.15 m texels; the 2 m blocker is resolved.
    plane, obstacle = [], []
    for y in range(depth_size[1]):
        for x in range(depth_size[0]):
            u, v = (x + .5) / depth_size[0], (y + .5) / depth_size[1]
            # Solve world Y=0 in the orthonormal light frame. These are exact
            # texel-center depths of geometry, not outputs of the shadow helper.
            along_v = -(2 * v - 1) * extent_v
            along_light = -axis_v[1] * along_v / light[1]
            depth = .5 - along_light / (2 * depth_extent)
            plane.append(depth)
            # A rectangular caster two metres in front of the receiver plane
            # along the light ray. The rest of the map still contains the plane.
            in_obstacle = abs(u - .5) < .07 and abs(v - .5) < .05
            obstacle.append(depth - (2 / (2 * depth_extent) if in_obstacle else 0.))

    def depth_texture(values):
        texture = gl.texture(depth_size, target=TEXTURE_2D_ARRAY, internal=R32F, linear=False, value=1)
        payload = struct.pack(f"<{len(values) * 4}f", *(values * 4))
        gl.glBindTexture(TEXTURE_2D_ARRAY, texture)
        gl.glTexImage3D(TEXTURE_2D_ARRAY, 0, R32F, *depth_size, 0, RED, FLOAT, C.c_char_p(payload))
        return texture

    def render(depth, program=production):
        fixture.samplers[4] = (depth, TEXTURE_2D_ARRAY)
        return fixture.render(program, False)

    empty_depth = gl.texture(depth_size, target=TEXTURE_2D_ARRAY, internal=R32F, linear=False, value=1)
    plane_depth, obstacle_depth = depth_texture(plane), depth_texture(obstacle)
    unoccluded, self_shadow, blocked = render(empty_depth), render(plane_depth), render(obstacle_depth)
    assert max(value for row in unoccluded for value in row) > .1, "Directional-light fixture did not emit usable direct light"
    plane_error = maximum_error(unoccluded, self_shadow)
    assert plane_error < 1e-5, ("Real deferred directional caller self-shadowed the planar receiver", plane_error)
    drops = [max(a - b for a, b in zip(clear, dark)) for clear, dark in zip(unoccluded, blocked)]
    assert all(b <= a + 1e-6 for clear, dark in zip(unoccluded, blocked) for a, b in zip(clear, dark)), "Caster increased direct illumination"
    dark_pixels = sum(value > .01 for value in drops)
    clear_pixels = sum(value < 1e-6 for value in drops)
    assert max(drops) > .05 and dark_pixels > len(drops) * .02 and clear_pixels > len(drops) * .1, (
        "The actual directional branch failed to produce a local caster shadow", max(drops), dark_pixels, clear_pixels,
    )

    # Inspect the actual caller's geometric derivatives before light branches.
    # A conservative bias can hide a wrong normal on a plane, so use a direct
    # normal oracle instead of requiring wrong-space derivatives to make acne.
    marker = re.search(r"vec3\s+shadowWorldDy\s*=[^;]+;", fragment).group()
    diagnostic = fragment.replace(marker, marker +
        "\n o_FragColor = abs(normalize(cross(shadowWorldDx, shadowWorldDy))); return;")
    normal_probe = gl.compile([(0x8B31, vertex), (0x8B30, diagnostic)],
                              "actual directional caller: geometric normal")
    normals = render(plane_depth, normal_probe)
    expected_normals = [(0., 1., 0.)] * len(normals)
    normal_error = maximum_error(normals, expected_normals)
    assert normal_error < 3e-5, ("Actual deferred derivatives must give the world ground normal", normal_error)
    wrong, dx_count = re.subn(r"vec3\s+shadowWorldDx\s*=[^;]+;",
                             "vec3 shadowWorldDx = dFdx(viewPos.xyz);", diagnostic)
    wrong, dy_count = re.subn(r"vec3\s+shadowWorldDy\s*=[^;]+;",
                             "vec3 shadowWorldDy = dFdy(viewPos.xyz);", wrong)
    assert dx_count == dy_count == 1
    incorrect = gl.compile([(0x8B31, vertex), (0x8B30, wrong)],
                           "negative control: directional derivatives in view space")
    wrong_error = maximum_error(expected_normals, render(plane_depth, incorrect))
    assert wrong_error > .5, ("Rotated-camera test did not detect a normal in the wrong space", wrong_error)
    print(f"PASS real directional-light caller: rotated -68/35 degree camera, planar GBuffer32F and four-layer depth map; receiver matches empty map (error {plane_error:.8f}), resolved 2 m caster darkens {dark_pixels} pixels while {clear_pixels} remain clear (max {max(drops):.4f} HDR); normal error {normal_error:.7f}, wrong-space normal error {wrong_error:.4f}", flush=True)



def main():
    with ScreenGL() as gl:
        print(f"OpenGL {gl.version}; renderer: {gl.renderer}", flush=True)
        check_screen_composition(gl)


if __name__ == "__main__":
    main()
