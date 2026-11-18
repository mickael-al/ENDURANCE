#!/usr/bin/env python3
"""Numerical GPU regression for directional surface-shadow receiver planes.

The production helper reads a rasterized DEPTH_COMPONENT32 array through the
same NEAREST sampler as the engine. Independent CPU tap counts check real
occluders and PCF contact edges. Two negative controls reproduce the original
grid and the remaining grid when only integer tap offsets are corrected.
All GPU readbacks here are test instrumentation, never renderer code.
"""
from __future__ import annotations

import ctypes as C
from dataclasses import dataclass
import math
from pathlib import Path

from gl_support import (
    AtmosphereGL, FLOAT, FULLSCREEN_VERTEX, RGBA32F, TEXTURE_2D_ARRAY,
    shader_source,
)

HERE = Path(__file__).resolve().parent
DEPTH_COMPONENT32 = 0x81A7
DEPTH_COMPONENT = 0x1902
SIZE = (128, 96)


class ShadowGL(AtmosphereGL):
    def __init__(self):
        super().__init__()
        signatures = {
            "glFramebufferTextureLayer": (None, C.c_uint, C.c_uint, C.c_uint, C.c_int, C.c_int),
            "glDrawBuffer": (None, C.c_uint),
            "glReadBuffer": (None, C.c_uint),
            "glEnable": (None, C.c_uint),
            "glDepthFunc": (None, C.c_uint),
            "glDepthMask": (None, C.c_ubyte),
            "glClearDepth": (None, C.c_double),
            "glClear": (None, C.c_uint),
        }
        for name, signature in signatures.items():
            self._fn(name, *signature)
        self.depth_program = self.compile(
            [(0x8B31, FULLSCREEN_VERTEX), (0x8B30, shader_source(HERE / "surface_shadow_depth.fs.glsl"))],
            "rasterized surface-shadow fixture",
        )
        self.probe = self.compile(
            [(0x8B31, FULLSCREEN_VERTEX), (0x8B30, shader_source(HERE / "surface_shadow_probe.fs.glsl"))],
            "actual directional surface-shadow helper",
        )
        self.shadow_maps = {}
        self.output = self.texture(SIZE, internal=RGBA32F)
        self.diagnostic = self.texture(SIZE, internal=RGBA32F)

    def shadow_texture(self, size):
        if size in self.shadow_maps:
            return self.shadow_maps[size]
        texture = C.c_uint()
        self.glGenTextures(1, C.byref(texture))
        self.textures.append(texture.value)
        self.glBindTexture(TEXTURE_2D_ARRAY, texture.value)
        self.glTexImage3D(TEXTURE_2D_ARRAY, 0, DEPTH_COMPONENT32, *size, 2, 0,
                          DEPTH_COMPONENT, FLOAT, None)
        for name, value in ((0x2800, 0x2600), (0x2801, 0x2600), (0x2802, 0x812F),
                            (0x2803, 0x812F), (0x813C, 0), (0x813D, 0), (0x884C, 0)):
            self.glTexParameteri(TEXTURE_2D_ARRAY, name, value)
        self.check("engine-format shadow-map allocation")
        self.shadow_maps[size] = texture.value
        return texture.value

    def rasterize(self, case, obstacle=False):
        texture = self.shadow_texture(case.resolution)
        program = self.depth_program
        self.uniform(program, "plane", "3f", *case.plane)
        self.uniform(program, "obstaclePixels", "4f", *(case.obstacle if obstacle else (0, 0, 0, 0)))
        self.uniform(program, "obstacleGap", "1f", case.gap / case.depth)
        framebuffer, array = C.c_uint(), C.c_uint()
        self.glGenFramebuffers(1, C.byref(framebuffer))
        self.glGenVertexArrays(1, C.byref(array))
        self.framebuffers.append(framebuffer.value)
        self.arrays.append(array.value)
        self.glBindFramebuffer(0x8D40, framebuffer.value)
        self.glDrawBuffer(0)
        self.glReadBuffer(0)
        self.glViewport(0, 0, *case.resolution)
        self.glEnable(0x0B71)
        self.glDepthFunc(0x0201)
        self.glDepthMask(True)
        self.glClearDepth(1.0)
        self.glDisable(0x0BE2)
        self.glDisable(0x0B44)
        self.glBindVertexArray(array.value)
        self.glUseProgram(program)
        for layer in range(2):
            self.glFramebufferTextureLayer(0x8D40, 0x8D00, texture, 0, layer)
            assert self.glCheckFramebufferStatus(0x8D40) == 0x8CD5
            self.glClear(0x00000100)
            if layer == 1:
                # Layer zero is deliberately all-lit: incorrect array-layer
                # addressing cannot accidentally pass the occluder tests.
                self.glDrawArrays(0x0004, 0, 3)
        self.glBindFramebuffer(0x8D40, 0)
        self.glMemoryBarrier(self.ALL_BARRIER_BITS)
        self.check("shadow receiver and occluder rasterization")
        return texture

    def evaluate(self, case, texture, *, degenerate=0, domain=None, layer=1, w=1,
                 position_error=(0, 0, 0)):
        self.matrix(self.probe, "projectionView", case.projection)
        self.matrix(self.probe, "inverseProjectionView", case.inverse)
        self.uniform(self.probe, "plane", "3f", *case.plane)
        self.uniform(self.probe, "receiverDomain", "4f", *(domain or case.domain))
        self.uniform(self.probe, "layer", "1i", layer)
        self.uniform(self.probe, "degenerateMode", "1i", degenerate)
        self.uniform(self.probe, "homogeneousW", "1f", w)
        self.uniform(self.probe, "worldPositionError", "3f", *position_error)
        self.sampler(0, texture, TEXTURE_2D_ARRAY)
        self.fullscreen(self.probe, *SIZE, [self.output, self.diagnostic])
        output = self.texels(self.output, SIZE)
        diagnostic = self.texels(self.diagnostic, SIZE)
        assert all(math.isfinite(value) for value in output + diagnostic), case.name
        visibility = output[1::4]
        assert min(visibility) >= 0 and max(visibility) <= 1, case.name
        return output, diagnostic


def dot(a, b):
    return sum(x * y for x, y in zip(a, b))


def cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def unit(vector):
    norm = math.sqrt(dot(vector, vector))
    return tuple(value / norm for value in vector)


@dataclass
class Case:
    name: str
    resolution: tuple[int, int]
    width: float
    depth: float
    slope: float
    azimuth: float
    rotation: float = 0
    reverse_y: bool = True
    origin: tuple[float, float, float] = (0., 0., 0.)
    center: float = .5
    gap: float = .01

    def __post_init__(self):
        self.height = self.width * self.resolution[1] / self.resolution[0]
        z = unit((.32, -.84, .44))
        x0 = unit(cross((0., 1., 0.), z))
        y0 = cross(z, x0)
        angle = math.radians(self.rotation)
        x = tuple(math.cos(angle) * a + math.sin(angle) * b for a, b in zip(x0, y0))
        y = tuple(-math.sin(angle) * a + math.cos(angle) * b for a, b in zip(x0, y0))
        if self.reverse_y:
            y = tuple(-value for value in y)
        axes = (x, y, z)
        scales = (self.width / 2, self.height / 2, self.depth / 2)
        rows = [tuple(value / scale for value in axis) for axis, scale in zip(axes, scales)]
        rows = [row + (-dot(row, self.origin),) for row in rows] + [(0., 0., 0., 1.)]
        self.projection = [rows[row][column] for column in range(4) for row in range(4)]
        self.inverse = [axis[row] * scale if row < 3 else 0.
                        for axis, scale in zip(axes, scales) for row in range(4)] + [*self.origin, 1.]
        slope, azimuth = math.tan(math.radians(self.slope)), math.radians(self.azimuth)
        self.plane = (slope * math.cos(azimuth) * self.width / self.depth,
                      slope * math.sin(azimuth) * self.height / self.depth, self.center)
        nx, ny = self.resolution
        # Cover several texels at many receiver subpixel phases. A steep plane
        # may leave the depth frustum far away; this central patch stays valid.
        self.domain = (.5 - 3.25 / nx, .5 - 3.25 / ny, 6.5 / nx, 6.5 / ny)
        self.obstacle = (nx // 2 - 1, ny // 2 - 1, nx // 2 + 1, ny // 2 + 1)


def expected_visibility(case, diagnostics, obstacle):
    result = []
    nx, ny = case.resolution
    left, bottom, right, top = case.obstacle
    for i in range(0, len(diagnostics), 4):
        u, v, z = diagnostics[i:i + 3]
        if u < 0 or u > 1 or v < 0 or v > 1 or z < 0 or z > 1:
            result.append(1.)
            continue
        ix, iy = math.floor(u * nx), math.floor(v * ny)
        blocked = sum(1 for y in range(iy - 1, iy + 2) for x in range(ix - 1, ix + 2)
                      if obstacle and left <= x < right and bottom <= y < top)
        result.append(1 - blocked / 9)
    return result


def check():
    cases = [
        Case("flat", (64, 64), 128, 100, 0, 0),
        Case("inclined20", (64, 64), 128, 100, 20, 0),
        Case("inclined45", (64, 64), 128, 100, 45, 37),
        Case("inclined70", (64, 64), 128, 100, 70, 90),
        Case("inclined85", (64, 64), 128, 100, 85, 135),
        Case("azimuth_negative", (128, 64), 256, 500, 55, -47, rotation=63),
        Case("unflipped_y", (64, 128), 128, 500, 55, 147, rotation=-31, reverse_y=False),
        Case("coarse_cascade", (32, 32), 64, 16100, 65, 23, rotation=27),
        Case("long_caster_margin", (256, 256), 1024, 16100, 45, 37, rotation=81),
        Case("near_depth", (128, 128), 256, 16100, 70, 199, center=.2),
        Case("far_depth", (128, 128), 256, 16100, 70, 251, center=.8),
        Case("distant_world", (256, 128), 2048, 16100, 45, -32,
             rotation=-63, origin=(12000., 3600., -8000.), gap=.02),
        Case("engine_resolution", (2048, 2048), 8192, 16100, 78, 47, rotation=36, gap=.02),
        Case("extreme_depth_span", (128, 128), 512, 100000, 45, 37, gap=.05),
    ]
    maximum_error = 0.
    old_grid, integer_grid = 0., 0.
    worst_old_mean = 1.
    with ShadowGL() as gl:
        print(f"OpenGL {gl.version}; {gl.renderer}")
        for case in cases:
            for obstacle in (False, True):
                texture = gl.rasterize(case, obstacle)
                result, diagnostic = gl.evaluate(case, texture)
                expected = expected_visibility(case, diagnostic, obstacle)
                error = max(abs(a - b) for a, b in zip(result[1::4], expected))
                maximum_error = max(maximum_error, error)
                assert error < 2e-6, (case.name, obstacle, error,
                    min(result[1::4]), max(result[1::4]), max(diagnostic[3::4]))
                if not obstacle:
                    old, partial = result[0::4], result[2::4]
                    old_grid = max(old_grid, max(old) - min(old))
                    integer_grid = max(integer_grid, max(partial) - min(partial))
                    worst_old_mean = min(worst_old_mean, sum(old) / len(old))
                else:
                    assert min(expected) < .6 and max(expected) == 1, case.name
            print(f"PASS {case.name}: inclined plane fully lit; {case.gap * 1000:g} mm occluder/PCF edge retained")

        assert old_grid >= .2 and worst_old_mean < .85, (old_grid, worst_old_mean)
        assert integer_grid >= .9, integer_grid
        print(f"PASS negative controls: old grid range={old_grid:.6f}, lowest mean={worst_old_mean:.6f}; "
              f"integer-offset-only grid range={integer_grid:.6f}")

        # Exercise the caller-provided representation bound independently of
        # the storage/transform regression in check_position_precision.py.
        # Its normalized margin must follow the signed plane equation, remain
        # stable in metres when caster depth changes, and preserve contacts.
        precision_cases = [
            (Case("near_precision", (128, 128), 256, 100, 45, 37,
                  origin=(10., 20., 30.)), 100.),
            (Case("kilometre_precision", (128, 128), 256, 16100, 45, 37,
                  origin=(700., 600., 100.)), 1000.),
            (Case("40km_precision", (256, 256), 1024, 16100, 45, 37,
                  origin=(12000., 3600., -38000.), gap=.05), 40000.),
            # Combining a 40 km world position with a 100 km depth span adds
            # float-projection/depth quantization beyond the engine's 16 km
            # caster margin. Keep a representable 100 mm occluder here; the
            # preceding production-range case requires the 50 mm contact.
            (Case("40km_extended_depth", (256, 256), 1024, 100000, 45, 37,
                  origin=(12000., 3600., -38000.), gap=.10), 40000.),
        ]
        precision_biases = []
        for case, view_distance in precision_cases:
            # Identity camera rotation here: the production diagnostic covers
            # rotated camera matrices and actual quantized GBuffer positions.
            error_bound = tuple(4 * 2**-23 * max(abs(value), view_distance if i == 2 else 0.)
                                for i, value in enumerate(case.origin))
            texture = gl.rasterize(case, True)
            result, diagnostic = gl.evaluate(case, texture, position_error=error_bound)
            expected = expected_visibility(case, diagnostic, True)
            error = max(abs(a - b) for a, b in zip(result[1::4], expected))
            maximum_error = max(maximum_error, error)
            assert error < 2e-6, case.name
            rows = [case.projection[row::4][:3] for row in range(3)]
            q = tuple(.5 * (rows[2][i] - case.plane[0] * rows[0][i] -
                            case.plane[1] * rows[1][i]) for i in range(3))
            metric_bias = (.005 / case.depth + dot(tuple(abs(v) for v in q), error_bound)) * case.depth
            actual = [value * case.depth for value in result[3::4]]
            assert max(abs(value - metric_bias) for value in actual) < 1e-5, (case.name, metric_bias)
            precision_biases.append(sum(actual) / len(actual))
            print(f"PASS {case.name}: representation XYZ bound {tuple(round(v * 1000, 3) for v in error_bound)} mm; "
                  f"actual bias {precision_biases[-1] * 1000:.3f} mm; {case.gap * 1000:g} mm occluder retained")
        assert abs(precision_biases[-1] - precision_biases[-2]) < 1e-5

        flat = cases[0]
        texture = gl.rasterize(flat, True)
        for degenerate in (1, 2):
            result, diagnostic = gl.evaluate(flat, texture, degenerate=degenerate)
            expected = expected_visibility(flat, diagnostic, True)
            assert max(abs(a - b) for a, b in zip(result[1::4], expected)) < 2e-6
        print("PASS collapsed/collinear derivatives: finite fallback and real occluder preserved")

        for options in ({"layer": -1}, {"layer": 2}, {"w": 0}, {"w": -1},
                        {"domain": (-.2, .1, .1, .1)}, {"domain": (1.1, .1, .1, .1)}):
            result, _ = gl.evaluate(flat, texture, **options)
            assert all(value == 1 for value in result[1::4]), options
        result, _ = gl.evaluate(flat, texture, layer=0)
        assert all(value == 1 for value in result[1::4])
        print("PASS invalid layer/projection/outside-map guards and independent array layers")
        print(f"PASS exact receiver-plane 3x3 visibility: {len(cases) * 2 + len(precision_cases)} "
              f"plane/occluder/precision cases, 2 degenerate and 7 guard draws; maximum error={maximum_error:.3g}")


if __name__ == "__main__":
    check()
