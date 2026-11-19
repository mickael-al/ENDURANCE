#!/usr/bin/env python3
"""GPU regression for the engine's directional surface shadow filter.

The production helper reads a rasterized DEPTH_COMPONENT32 array through the
engine's NEAREST sampler. An independent closed-form tent/rectangle oracle
checks resolved occluders, bilinear transitions and map borders. Plane tests
cover rotations, grazing angles, precision bounds and caster depth changes.
The terrain regression separately preserves the old receiver-plane filter as
its negative control. All readbacks here are test instrumentation.
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
        self.geometry = self.texture(SIZE, internal=RGBA32F)

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
                 position_error=(0, 0, 0), bias_texels=(.5, 1.5)):
        self.matrix(self.probe, "projectionView", case.projection)
        self.matrix(self.probe, "inverseProjectionView", case.inverse)
        self.uniform(self.probe, "plane", "3f", *case.plane)
        self.uniform(self.probe, "receiverDomain", "4f", *(domain or case.domain))
        self.uniform(self.probe, "layer", "1i", layer)
        self.uniform(self.probe, "degenerateMode", "1i", degenerate)
        self.uniform(self.probe, "homogeneousW", "1f", w)
        self.uniform(self.probe, "worldPositionError", "3f", *position_error)
        self.uniform(self.probe, "biasTexels", "2f", *bias_texels)
        self.sampler(0, texture, TEXTURE_2D_ARRAY)
        self.fullscreen(self.probe, *SIZE, [self.output, self.diagnostic, self.geometry])
        output = self.texels(self.output, SIZE)
        diagnostic = self.texels(self.diagnostic, SIZE)
        geometry = self.texels(self.geometry, SIZE)
        assert all(math.isfinite(value) for value in output + diagnostic + geometry), case.name
        visibility = output[0::4]
        assert min(visibility) >= -1e-6 and max(visibility) <= 1 + 1e-6, case.name
        return output, diagnostic, geometry


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
    gap: float | None = None

    def __post_init__(self):
        self.height = self.width * self.resolution[1] / self.resolution[0]
        self.world_texel = self.width / self.resolution[0]
        # Resolve blockers above the geometric offset/filter footprint at all
        # tested slopes. Small contacts have their own fine-resolution cases.
        if self.gap is None:
            self.gap = 48 * self.world_texel
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
    """Closed-form tent reconstruction of an opaque rectangle's coverage.

    At each edge the coverage rises from zero to one over exactly one texel.
    The product of its horizontal and vertical ramps is the bilinear footprint
    of a resolved rectangular blocker. No depth/tap loop from the helper is
    copied here; the independent rasterizer supplies the binary rectangle.
    """
    if not obstacle:
        return [1.] * (len(diagnostics) // 4)
    nx, ny = case.resolution
    left, bottom, right, top = case.obstacle
    def ramp(value):
        return max(0., min(1., value))
    result = []
    for i in range(0, len(diagnostics), 4):
        u, v = diagnostics[i:i + 2]
        horizontal = ramp(u * nx - left + .5) * ramp(right + .5 - u * nx)
        vertical = ramp(v * ny - bottom + .5) * ramp(top + .5 - v * ny)
        result.append(1. - horizontal * vertical)
    return result


def check_oracle(case, result, diagnostic, obstacle):
    expected = expected_visibility(case, diagnostic, obstacle)
    actual = result[0::4]
    error = max(abs(a - b) for a, b in zip(actual, expected))
    assert error < 2e-6, (case.name, obstacle, error, min(actual), max(actual))
    return error, expected


def check():
    cases = [
        Case("flat", (64, 64), 128, 1000, 0, 0),
        Case("inclined20", (64, 64), 128, 1000, 20, 0),
        Case("inclined45", (64, 64), 128, 1000, 45, 37),
        Case("inclined70", (64, 64), 128, 1000, 70, 90),
        Case("inclined85", (64, 64), 128, 1000, 85, 135),
        Case("azimuth_negative", (128, 64), 256, 1000, 55, -47, rotation=63),
        Case("unflipped_y", (64, 128), 128, 1000, 55, 147, rotation=-31, reverse_y=False),
        Case("coarse_cascade", (32, 32), 64, 16100, 65, 23, rotation=27),
        Case("long_caster_margin", (256, 256), 1024, 16100, 45, 37, rotation=81),
        Case("near_depth", (128, 128), 256, 16100, 70, 199, center=.2),
        Case("far_depth", (128, 128), 256, 16100, 70, 251, center=.8),
        Case("distant_world", (256, 128), 2048, 16100, 45, -32,
             rotation=-63, origin=(12000., 3600., -8000.)),
        Case("engine_resolution", (2048, 2048), 8192, 16100, 78, 47, rotation=36),
        Case("extreme_depth_span", (128, 128), 512, 100000, 45, 37),
        # Contacts are checked where they are actually resolvable: 1 mm texels,
        # rather than asking a 2 m texel/grazing offset to resolve a 10 mm gap.
        Case("resolved_10mm_flat", (128, 128), .128, 100, 0, 0, gap=.01),
        Case("resolved_50mm_grazing", (2048, 2048), 2.048, 1000, 85, 135, gap=.05),
    ]
    maximum_error = 0.
    with ShadowGL() as gl:
        print(f"OpenGL {gl.version}; {gl.renderer}", flush=True)
        for case in cases:
            for obstacle in (False, True):
                texture = gl.rasterize(case, obstacle)
                result, diagnostic, _ = gl.evaluate(case, texture)
                error, expected = check_oracle(case, result, diagnostic, obstacle)
                maximum_error = max(maximum_error, error)
                if obstacle:
                    assert min(expected) < 1e-6 and max(expected) == 1., case.name
                    assert any(.05 < value < .95 for value in expected), case.name
            print(f"PASS {case.name}: plane fully lit; {case.gap:g} m blocker with "
                  f"{case.world_texel:g} m texels; exact bilinear edge", flush=True)

        # A translation/long caster depth span must not change a geometric
        # offset measured in world texels. The XYZ representation allowance
        # remains a separate numerical margin; it must not move sampled UVs.
        precision_cases = [
            Case("precision_16km_depth", (256, 256), 1024, 16100, 45, 37,
                 origin=(12000., 3600., -38000.)),
            Case("precision_100km_depth", (256, 256), 1024, 100000, 45, 37,
                 origin=(12000., 3600., -38000.)),
        ]
        position_error = (4 * 2**-23 * 12000, 4 * 2**-23 * 3600, 4 * 2**-23 * 40000)
        metric_biases, offsets = [], []
        for case in precision_cases:
            texture = gl.rasterize(case, True)
            result, diagnostic, geometry = gl.evaluate(case, texture, position_error=position_error)
            error, _ = check_oracle(case, result, diagnostic, True)
            maximum_error = max(maximum_error, error)
            actual_bias = [value * case.depth for value in result[1::4]]
            assert min(actual_bias) > .005, (case.name, min(actual_bias))
            metric_biases.append(sum(actual_bias) / len(actual_bias))
            offsets.append(geometry)
            print(f"PASS {case.name}: explicit XYZ uncertainty, "
                  f"numerical margin {metric_biases[-1] * 1000:.3f} mm, resolved blocker retained", flush=True)
        assert abs(metric_biases[0] - metric_biases[1]) < 1e-5, metric_biases
        assert max(abs(a - b) for a, b in zip(*offsets)) < 2e-4, "Caster Z span changed the world-space offset"

        # Tiny but resolved contact: collapse the derivative footprint and
        # verify a finite light-direction fallback with the actual filter.
        flat = cases[-2]
        texture = gl.rasterize(flat, True)
        for degenerate in (1, 2):
            result, diagnostic, _ = gl.evaluate(flat, texture, degenerate=degenerate)
            error, _ = check_oracle(flat, result, diagnostic, True)
            maximum_error = max(maximum_error, error)
        print("PASS collapsed/collinear derivatives: finite fallback and resolved 10 mm contact", flush=True)

        # Zero is an explicit per-light setting, not an alias for defaults.
        result, diagnostic, geometry = gl.evaluate(flat, texture, bias_texels=(0, 0))
        assert all(value == 0. for value in geometry), "Explicit zero bias still displaced the receiver"
        error, _ = check_oracle(flat, result, diagnostic, True)
        maximum_error = max(maximum_error, error)
        print("PASS explicit zero geometric bias retains the independent numerical margin", flush=True)

        for options in ({"layer": -1}, {"layer": 2}, {"w": 0}, {"w": -1},
                        {"domain": (-.2, .1, .1, .1)}, {"domain": (1.1, .1, .1, .1)}):
            result, _, _ = gl.evaluate(flat, texture, **options)
            assert all(value == 1 for value in result[0::4]), options
        result, _, _ = gl.evaluate(flat, texture, layer=0)
        # Four floating-point weights can sum one ULP below one.
        assert max(abs(value - 1.) for value in result[0::4]) < 2e-6
        for center in (-.1, 1.1):
            outside = Case("outside_depth", (128, 128), .128, 100, 0, 0, center=center, gap=.01)
            result, _, _ = gl.evaluate(outside, texture)
            assert all(value == 1 for value in result[0::4]), center
        print("PASS invalid layer/W/outside-XY/outside-depth guards and independent array layers", flush=True)

        # Cover the entire valid map with a resolved blocker. Border texels
        # outside the map are lit, so the analytical tent loses coverage there;
        # clamping the sampler or accidentally repeating an edge would fail.
        edge = Case("map_edges", (64, 64), 128, 1000, 0, 0)
        nx, ny = edge.resolution
        edge.obstacle = (0, 0, nx, ny)
        texture = gl.rasterize(edge, True)
        domains = ((0., .25, .25 / nx, .5), (1. - .25 / nx, .25, .25 / nx, .5),
                   (.25, 0., .5, .25 / ny), (.25, 1. - .25 / ny, .5, .25 / ny),
                   (0., 0., .25 / nx, .25 / ny))
        for domain in domains:
            result, diagnostic, _ = gl.evaluate(edge, texture, domain=domain)
            error, expected = check_oracle(edge, result, diagnostic, True)
            maximum_error = max(maximum_error, error)
            assert min(expected) > 0 and max(expected) < 1, domain
        print("PASS four map borders and corner: outside taps contribute light with correct tent weights", flush=True)
        print(f"PASS bilinear directional shadows: {len(cases)} planes/resolved blockers, "
              f"XYZ/depth-span invariance, degenerate/zero-bias/guard/border checks; maximum oracle error={maximum_error:.3g}", flush=True)


if __name__ == "__main__":
    check()
