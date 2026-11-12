#!/usr/bin/env python3
"""Real OpenGL regression tests for the climate lab (EGL, no engine window).

Run: python3 Tests/ClimateNoiseLab/check_gpu.py
Requires desktop OpenGL 4.5 + EGL; Mesa llvmpipe is sufficient. Readbacks are
test instrumentation, not part of the runtime trainer.
"""

from __future__ import annotations

import ctypes as C
import math
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "Tests/Terrain"))
from gl_headless import GL, GLUnavailable, shader_source

PARAMETERS = 329
POPULATION = 4
SEED = 1337
GUARD = 123456.0


def floats(values):
    return struct.pack(f"<{len(values)}f", *values)


def constant_network(value):
    result = [0.0] * PARAMETERS
    result[328] = value * 2.0 - 1.0
    return result


def loss(predicted, target):
    # Test fixtures avoid half-way cases of GLSL round().
    levels = (math.floor(predicted * 4.0 + 0.5) - math.floor(target * 4.0 + 0.5)) / 4.0
    return (predicted - target) ** 2 + 0.08 * levels ** 2


def hash32(value):
    value &= 0xFFFFFFFF
    value ^= value >> 16
    value = (value * 0x7FEB352D) & 0xFFFFFFFF
    value ^= value >> 15
    value = (value * 0x846CA68B) & 0xFFFFFFFF
    return value ^ (value >> 16)


def validation_pixels(width, height):
    for sample in range(32 * 32):
        gx, gy = sample % 32, sample // 32
        x0, y0 = gx * width // 32, gy * height // 32
        x1, y1 = max(x0 + 1, (gx + 1) * width // 32), max(y0 + 1, (gy + 1) * height // 32)
        hx = hash32(sample ^ ((SEED * 0x85EBCA6B) & 0xFFFFFFFF) ^ 0xD1B54A35)
        hy = hash32(hx ^ 0x68BC21EB)
        yield min(x0 + hx % (x1 - x0), width - 1), min(y0 + hy % (y1 - y0), height - 1)


class Lab:
    def __init__(self, gl):
        self.gl = gl
        self.trainer = gl.compute(ROOT / "Asset/Shader/ClimateNoiseLab/neural_genetic_train.C.glsl")
        self.preview = gl.compute(ROOT / "Asset/Shader/ClimateNoiseLab/climate_noise_lab.C.glsl")
        gl._fn("glUniform2i", None, C.c_int, C.c_int, C.c_int)
        gl._fn("glUniform1iv", None, C.c_int, C.c_int, C.POINTER(C.c_int))
        gl._fn("glGenTextures", None, C.c_int, C.POINTER(C.c_uint))
        gl._fn("glDeleteTextures", None, C.c_int, C.POINTER(C.c_uint))
        gl._fn("glBindTexture", None, C.c_uint, C.c_uint)
        gl._fn("glBindTextureUnit", None, C.c_uint, C.c_uint)
        gl._fn("glTexImage2D", None, C.c_uint, C.c_int, C.c_int, C.c_int, C.c_int,
               C.c_int, C.c_uint, C.c_uint, C.c_void_p)
        gl._fn("glTexParameteri", None, C.c_uint, C.c_uint, C.c_int)
        gl._fn("glGetTexImage", None, C.c_uint, C.c_int, C.c_uint, C.c_uint, C.c_void_p)
        gl._fn("glBindImageTexture", None, C.c_uint, C.c_uint, C.c_int, C.c_ubyte,
               C.c_int, C.c_uint, C.c_uint)
        self.population = gl.buffer(2 * POPULATION * PARAMETERS * 4, 0)
        self.scores = gl.buffer(floats([GUARD] * (POPULATION + 4)), 1)
        self.best = gl.buffer(floats([0.0] * PARAMETERS), 2)
        self.state = gl.buffer(struct.pack("<fIII", 1e30, 0, 0, 0), 3)
        self.textures = []
        self.u("uPopulationSize", "1ui", POPULATION)
        self.u("uSourceIndex", "1ui", 0)
        self.u("uGeneration", "1ui", 0)
        self.u("uSeed", "1ui", SEED)
        self.u("uSampleGrid", "1ui", 8)
        self.u("uMutationScale", "1f", 0.18)
        self.u("uReferenceChannel", "1i", 0)
        self.u("uReferenceImage", "1i", 0)

    def u(self, name, suffix, *values):
        self.gl.uniform(self.trainer, name, suffix, *values)

    def pass_(self, kind, groups):
        self.u("uPass", "1ui", kind)
        self.gl.dispatch(self.trainer, groups)

    def texture(self, width, height, payload=None, rgba8=False):
        gl = self.gl
        handle = C.c_uint()
        gl.glGenTextures(1, C.byref(handle))
        self.textures.append(handle.value)
        gl.glBindTexture(0x0DE1, handle.value)
        gl.glTexImage2D(0x0DE1, 0, 0x8058 if rgba8 else 0x8814, width, height,
                        0, 0x1908, 0x1401 if rgba8 else 0x1406,
                        C.c_char_p(payload) if payload else None)
        gl.glTexParameteri(0x0DE1, 0x2801, 0x2600)
        gl.glTexParameteri(0x0DE1, 0x2800, 0x2600)
        return handle.value

    def reference(self, width, height, values):
        texture = self.texture(width, height, floats(values))
        self.gl.glBindTextureUnit(0, texture)
        self.u("uReferenceSize", "2i", width, height)
        self.u("uCropOffset", "2i", 0, 0)
        self.u("uCropSize", "1i", min(width, height))
        return texture

    def networks(self, values, half=0, historical_score=1e30):
        self.gl.write(self.population, floats(sum(values, [])), half * POPULATION * PARAMETERS * 4)
        self.gl.write(self.best, floats(values[0]))
        self.gl.write(self.state, struct.pack("<fIII", historical_score, 0, 0, half))
        self.u("uSourceIndex", "1ui", half)

    def read_scores(self):
        return struct.unpack(f"<{POPULATION + 4}f", self.gl.read(self.scores, (POPULATION + 4) * 4))

    def close(self):
        handles = (C.c_uint * len(self.textures))(*self.textures)
        self.gl.glDeleteTextures(len(handles), handles)


def check_initialization(lab):
    lab.pass_(0, (POPULATION * PARAMETERS + 63) // 64)
    first = lab.gl.read(lab.population, POPULATION * PARAMETERS * 4)
    lab.pass_(0, (POPULATION * PARAMETERS + 63) // 64)
    assert first == lab.gl.read(lab.population, len(first)), "Initial population must be seed-deterministic"
    values = struct.unpack(f"<{POPULATION * PARAMETERS}f", first)
    assert values[:PARAMETERS] == tuple([0.0] * PARAMETERS)
    assert all(math.isfinite(value) and abs(value) <= 0.38 for value in values)


def check_validation_and_elitism(lab):
    # Equal black/white regions: a zero network fits a black crop perfectly,
    # but 0.5 is a much better global prediction. A historical crop score of 0
    # must neither replace 0.5 with black nor lock out the better global model.
    rgba = [component for y in range(64) for x in range(64)
            for component in ([0.0 if x < 32 else 1.0] * 3 + [1.0])]
    lab.reference(64, 64, rgba)
    lab.u("uCropSize", "1i", 24)
    values = [constant_network(x) for x in (0.5, 0.0, 1.0, 0.8)]
    lab.networks(values, historical_score=0.0)
    lab.pass_(1, POPULATION)
    assert lab.read_scores()[1] == 0.0, "Black crop should select the black candidate"
    lab.pass_(3, 2)
    scores = lab.read_scores()
    assert abs(scores[POPULATION] - 0.54) < 2e-6
    assert abs(scores[POPULATION + 1] - 0.27) < 2e-6
    lab.pass_(2, 1)
    assert lab.gl.read(lab.best, PARAMETERS * 4) == floats(values[0]), "Crop-perfect candidate must not replace global champion"
    destination = POPULATION * PARAMETERS * 4
    assert lab.gl.read(lab.population, PARAMETERS * 4, destination + PARAMETERS * 4) == floats(values[1]), "Keep crop winner as second elite"
    state = struct.unpack("<fIII", lab.gl.read(lab.state, 16))
    assert abs(state[0] - 0.27) < 2e-6 and state[2:] == (1, 1)

    # The same fixed metric is used regardless of generation, crop or grid.
    reference_score = scores[POPULATION + 1]
    for generation, grid, crop in ((17, 13, (39, 40)), (901, 64, (1, 5))):
        lab.networks(values)
        lab.u("uGeneration", "1ui", generation)
        lab.u("uSampleGrid", "1ui", grid)
        lab.u("uCropOffset", "2i", *crop)
        lab.pass_(1, POPULATION)
        lab.pass_(3, 2)
        assert lab.read_scores()[POPULATION + 1] == reference_score

    # Reverse incumbent/candidate, using source half one: a stale historical
    # zero must not prevent a globally better candidate from replacing it.
    values = [constant_network(x) for x in (0.0, 0.5, 1.0, 0.8)]
    lab.networks(values, half=1, historical_score=0.0)
    lab.u("uCropOffset", "2i", 20, 0)
    lab.u("uSampleGrid", "1ui", 8)
    lab.pass_(1, POPULATION)
    lab.pass_(3, 2)
    lab.pass_(2, 1)
    assert lab.gl.read(lab.best, PARAMETERS * 4) == floats(values[1])
    assert lab.gl.read(lab.population, PARAMETERS * 4) == floats(values[1])
    assert lab.read_scores()[-2:] == (GUARD, GUARD), "Validation must not overrun its two score slots"


def check_full_image_oracle(lab):
    width, height = 83, 59
    rgba = [component for y in range(height) for x in range(width)
            for component in ((x + 0.2) / width, (y + 0.3) / height,
                              ((x * 3 + y * 5) % 29) / 29.0, 1.0)]
    # Match texture float32 conversion before comparing CPU and GPU losses.
    rgba = struct.unpack(f"<{len(rgba)}f", floats(rgba))
    lab.reference(width, height, rgba)
    values = [constant_network(x) for x in (0.55, 0.0, 1.0, 0.8)]
    lab.networks(values)
    for channel in range(4):
        lab.u("uReferenceChannel", "1i", channel)
        lab.pass_(1, POPULATION)
        lab.pass_(3, 2)
        expected = 0.0
        for x, y in validation_pixels(width, height):
            pixel = rgba[(y * width + x) * 4:(y * width + x + 1) * 4]
            target = pixel[channel] if channel < 3 else sum(a * b for a, b in zip(pixel, (0.2126, 0.7152, 0.0722)))
            expected += loss(0.55, target) / 1024
        actual = lab.read_scores()[POPULATION + 1]
        assert math.isfinite(actual) and abs(actual - expected) < 2e-6, (channel, actual, expected)


def check_preview_coordinates(lab):
    gl = lab.gl
    # Fourier-only test network; independent CPU expression checks weight
    # offsets, all four tanh layers and the training/preview coordinate map.
    weights = [0.0] * PARAMETERS
    weights[0], weights[2], weights[96] = 0.7, 0.4, 0.1
    weights[104], weights[176], weights[248], weights[320], weights[328] = 0.9, 0.8, 0.7, 0.6, 0.1
    size = 8
    expected = []
    for y in range(size):
        for x in range(size):
            value = math.tanh(0.1 + 0.7 * math.sin((x + 0.5) / size * math.tau)
                              + 0.4 * math.sin((y + 0.5) / size * math.tau))
            for multiplier in (0.9, 0.8, 0.7):
                value = math.tanh(value * multiplier)
            expected.append(0.5 + 0.5 * (0.1 + 0.6 * value))
    target = [0.2, 0.4, 0.7, 1.0] * (size * size)
    lab.reference(size, size, target)
    lab.networks([weights] * POPULATION)
    lab.u("uReferenceChannel", "1i", 0)
    lab.u("uSampleGrid", "1ui", size)
    lab.pass_(1, POPULATION)
    cpu_score = sum(loss(value, 0.2) for value in expected) / len(expected)
    assert abs(lab.read_scores()[0] - cpu_score) < 2e-6

    gl.bind(lab.best, 5)
    for name, suffix, args in (
        ("uResolution", "1ui", (size,)), ("uSeed", "1ui", (SEED,)),
        ("uWorldOrigin", "2f", (0.0, 0.0)), ("uWorldExtent", "1f", (32768.0,)),
        ("uNeuralWorldPeriod", "1f", (32768.0,)),
        ("uClimateFrequency", "1f", (0.0005,)), ("uNeuralReferencePreview", "1i", (1,)),
        ("uHumidityFrequencyMultiplier", "1f", (1.0,)), ("uTemperatureFrequencyMultiplier", "1f", (1.0,)),
        ("uHumidityNoiseType", "1i", (11,)), ("uTemperatureNoiseType", "1i", (11,)),
        ("uPostContrast", "2f", (1.0, 1.0)), ("uPostGamma", "2f", (1.0, 1.0)),
        ("uAttractionMode", "1i", (0,)), ("uMatrixBlend", "1i", (1,)),
    ):
        gl.uniform(lab.preview, name, suffix, *args)
    gl.glUniform1iv(gl.glGetUniformLocation(lab.preview, b"uBiomeMatrix"), 25, (C.c_int * 25)(*range(25)))
    images = [lab.texture(size, size, rgba8=True) for _ in range(5)]
    for binding, image in enumerate(images):
        gl.glBindImageTexture(binding, image, 0, 0, 0, 0x88B9, 0x8058)
    gl.dispatch(lab.preview, 1, 1)
    results = []
    for image in images:
        gl.glBindTexture(0x0DE1, image)
        data = (C.c_ubyte * (size * size * 4))()
        gl.glGetTexImage(0x0DE1, 0, 0x1908, 0x1401, data)
        results.append(bytes(data))
    for index, value in enumerate(expected):
        for texture in results[:2]:
            pixel = texture[index * 4:(index + 1) * 4]
            assert abs(pixel[0] / 255.0 - value) <= 1.0 / 255.0
            assert pixel[0] == pixel[1] == pixel[2] and pixel[3] == 255
    assert results[0] == results[1], "Reference diagnostic must reproduce the learned scalar on both axes"
    assert results[2] == results[3], "Disabled attraction must preserve climate"


def perlin2d(x, y, seed):
    # Independent scalar CPU oracle for the vectorized GLSL implementation.
    ix, iy = math.floor(x), math.floor(y)
    fx, fy = x - ix, y - iy
    fade = lambda v: v * v * v * (v * (v * 6 - 15) + 10)
    corners = []
    for ox, oy in ((0, 0), (1, 0), (0, 1), (1, 1)):
        direction = hash32(((ix + ox) * 0x9E3779B9) ^ ((iy + oy) * 0x85EBCA6B) ^ seed) & 7
        a = (fx - ox) if direction < 6 else (fy - oy)
        b = (fy - oy) if direction < 4 else 0
        corners.append((-a if direction & 1 else a) + (-b if direction & 2 else b))
    mix = lambda a, b, t: a + (b - a) * t
    return mix(mix(corners[0], corners[1], fade(fx)),
               mix(corners[2], corners[3], fade(fx)), fade(fy))


def check_neural_axis_seeds_and_frequency(lab):
    gl, size = lab.gl, 16
    images = [lab.texture(size, size, rgba8=True) for _ in range(5)]
    for binding, image in enumerate(images):
        gl.glBindImageTexture(binding, image, 0, 0, 0, 0x88B9, 0x8058)
    gl.bind(lab.best, 5)
    gl.uniform(lab.preview, "uResolution", "1ui", size)
    gl.uniform(lab.preview, "uNeuralReferencePreview", "1i", 0)

    def render(seed, frequency):
        gl.uniform(lab.preview, "uSeed", "1ui", seed)
        gl.uniform(lab.preview, "uClimateFrequency", "1f", frequency)
        gl.dispatch(lab.preview, 2, 2)
        result = []
        for image in images[:2]:
            gl.glBindTexture(0x0DE1, image)
            data = (C.c_ubyte * (size * size * 4))()
            gl.glGetTexImage(0x0DE1, 0, 0x1908, 0x1401, data)
            result.append(bytes(data))
        return result

    for perlin in (False, True):
        # Test a model that ignores all Perlin features, then one that ignores
        # all Fourier features. Both must receive distinct deterministic inputs.
        weights = [0.0] * PARAMETERS
        weights[8 if perlin else 0], weights[96] = 0.7, 0.1
        weights[104], weights[176], weights[248], weights[320], weights[328] = 0.9, 0.8, 0.7, 0.6, 0.1
        lab.networks([weights] * POPULATION)
        baseline = render(SEED, 0.0005)
        assert baseline[0] != baseline[1], "Neural humidity and temperature still share inputs"
        assert baseline == render(SEED, 0.0005), "Neural previews must be deterministic"
        assert baseline != render(SEED + 1, 0.0005), "Neural seed does not affect the field"
        assert baseline != render(SEED, 0.001), "Climate frequency does not affect the neural field"
        for frequency in (0.0, 0.00025, 0.0005, 0.001):
            result = render(SEED, frequency)
            for axis, salt in enumerate((0x85A308D3, 0x243F6A88)):
                axis_seed = SEED ^ salt
                phase_x = (hash32(axis_seed) >> 8) / 16777215
                phase_y = (hash32(axis_seed ^ 0x68BC21EB) >> 8) / 16777215
                for y in range(size):
                    for x in range(size):
                        px = ((x + 0.5) / size - 0.5) * frequency / 0.0005 + 0.5 + phase_x
                        py = ((y + 0.5) / size - 0.5) * frequency / 0.0005 + 0.5 + phase_y
                        signal = (perlin2d(px + 11.7, py - 5.3, axis_seed ^ 0x243F6A88)
                                  if perlin else math.sin(px * math.tau))
                        value = math.tanh(0.1 + 0.7 * signal)
                        for multiplier in (0.9, 0.8, 0.7):
                            value = math.tanh(value * multiplier)
                        expected = 0.5 + 0.5 * (0.1 + 0.6 * value)
                        actual = result[axis][(y * size + x) * 4] / 255
                        assert abs(actual - expected) <= 1 / 255, (perlin, axis, frequency, actual, expected)


def simplex2d(x, y, seed):
    skew, unskew = 0.36602540378443864676, 0.21132486540518711775
    cx, cy = math.floor(x + (x + y) * skew), math.floor(y + (x + y) * skew)
    offset = (cx + cy) * unskew
    dx, dy = x - cx + offset, y - cy + offset
    mx, my = (1, 0) if dx > dy else (0, 1)
    result = 0.0
    for ox, oy, px, py in ((0, 0, dx, dy),
                           (mx, my, dx - mx + unskew, dy - my + unskew),
                           (1, 1, dx - 1 + 2 * unskew, dy - 1 + 2 * unskew)):
        direction = hash32(((cx + ox) * 0x9E3779B9) ^ ((cy + oy) * 0x85EBCA6B) ^ seed) & 7
        a = px if direction < 6 else py
        b = py if direction < 4 else 0
        gradient = (-a if direction & 1 else a) + (-b if direction & 2 else b)
        result += max(0.5 - px * px - py * py, 0) ** 4 * gradient
    return max(-1.0, min(1.0, result * 70))


def simplex_fbm2d(x, y, seed):
    # The test fixture uses 4 octaves, lacunarity 2 and gain 0.5.
    return sum(simplex2d(x * 2 ** i, y * 2 ** i, (seed + i * 0x9E3779B9) & 0xffffffff)
               * 0.5 ** i for i in range(4)) / 1.875


def check_binary_five_noise(lab):
    gl, size = lab.gl, 32
    images = [lab.texture(size, size, rgba8=True) for _ in range(5)]
    for binding, image in enumerate(images):
        gl.glBindImageTexture(binding, image, 0, 0, 0, 0x88B9, 0x8058)
    for name, suffix, args in (
        ("uResolution", "1ui", (size,)), ("uWorldOrigin", "2f", (125.25, -79.5)),
        ("uWorldExtent", "1f", (8192.0,)),
        ("uHumidityFrequencyMultiplier", "1f", (1.0,)),
        ("uTemperatureFrequencyMultiplier", "1f", (1.0,)),
        ("uOctaves", "1i", (4,)), ("uLacunarity", "1f", (2.0,)), ("uGain", "1f", (0.5,)),
        ("uBinaryTransition", "2f", (0.0, 0.0)),
        ("uRemapMinimum", "1f", (-0.32,)), ("uRemapMaximum", "1f", (0.32,)),
        ("uPostContrast", "2f", (1.0, 1.0)), ("uPostGamma", "2f", (1.0, 1.0)),
        ("uAttractionMode", "1i", (0,)),
    ):
        gl.uniform(lab.preview, name, suffix, *args)

    def render(seed=SEED, frequency=0.0005):
        gl.uniform(lab.preview, "uSeed", "1ui", seed)
        gl.uniform(lab.preview, "uClimateFrequency", "1f", frequency)
        gl.dispatch(lab.preview, size // 8, size // 8)
        result = []
        for image in images:
            gl.glBindTexture(0x0DE1, image)
            data = (C.c_ubyte * (size * size * 4))()
            gl.glGetTexImage(0x0DE1, 0, 0x1908, 0x1401, data)
            result.append(bytes(data))
        return result

    for noise_type, base in ((12, perlin2d), (13, simplex_fbm2d)):
        gl.uniform(lab.preview, "uHumidityNoiseType", "1i", noise_type)
        gl.uniform(lab.preview, "uTemperatureNoiseType", "1i", noise_type)
        result = render()
        assert result == render(), "Binary fields are not deterministic"
        assert result[:2] != render(seed=SEED + 1)[:2], "Binary fields ignore the seed"
        assert result[:2] != render(frequency=0.001)[:2], "Binary fields ignore spatial frequency"
        assert result[0] != result[1], "Binary humidity and temperature share seeds"
        assert result[2] == result[3], "Disabled attraction changed binary levels"
        checked = 0
        all_seeds = set()
        samples = [[], []]
        for axis, salt in enumerate((0x85A308D3, 0x243F6A88)):
            seeds = [hash32((SEED ^ salt) ^ ((i + 1) * 0x9E3779B9)) for i in range(5)]
            all_seeds.update(seeds)
            assert set(result[axis][0::4]) == {0, 51, 102, 153, 204, 255}, "Expected all six binary-sum levels"
            assert result[axis][0::4] == result[axis][1::4] == result[axis][2::4]
            assert set(result[axis][3::4]) == {255}
            for y in range(size):
                for x in range(size):
                    px = (125.25 + ((x + 0.5) / size - 0.5) * 8192) * 0.0005
                    py = (-79.5 + ((y + 0.5) / size - 0.5) * 8192) * 0.0005
                    values = [base(px, py, seed) for seed in seeds]
                    samples[axis].append(values)
                    # float32/double can disagree exactly at a discontinuity.
                    # Equality itself is covered separately at frequency zero.
                    if min(map(abs, values)) < 0.0001:
                        continue
                    expected = 51 * sum(value > 0 for value in values)
                    assert result[axis][(y * size + x) * 4] == expected, (noise_type, x, y, expected)
                    checked += 1
        assert len(all_seeds) == 10, "Five distinct layer seeds required per axis"
        assert checked > 0.95 * size * size * 2
        gl.uniform(lab.preview, "uRemapMinimum", "1f", (2.0))
        gl.uniform(lab.preview, "uRemapMaximum", "1f", (3.0))
        assert result == render(), "Smoothstep destroyed the binary sum levels"
        # At p=0 all five base noises are exactly zero, so strict >0 yields 0.
        zero = bytes([0, 0, 0, 255]) * (size * size)
        assert render(frequency=0.0)[:2] == [zero, zero], "Zero noise was counted as positive"
        if noise_type == 13:
            gl.uniform(lab.preview, "uOctaves", "1i", 0)
            assert render()[:2] == [zero, zero], "Zero-octave fBm should add nothing"
            gl.uniform(lab.preview, "uOctaves", "1i", 4)

        # Compare the actual preview with the independent continuous CPU noises.
        # One axis stays binary while the other gets a configurable soft band.
        for humidity_width, temperature_width in ((0.1, 0.0), (0.0, 0.1), (0.1, 0.2)):
            gl.uniform(lab.preview, "uBinaryTransition", "2f", humidity_width, temperature_width)
            softened = render()
            assert softened == render(), "Soft fields are not deterministic"
            for axis, width in enumerate((temperature_width, humidity_width)):
                if width == 0:
                    assert softened[axis] == result[axis], "Transition setting changed the other axis"
                    continue
                assert len(set(softened[axis][0::4])) > 6, "Soft threshold still produces binary plateaus only"
                for pixel, values in enumerate(samples[axis]):
                    terms = [max(0.0, min(1.0, (n + width) / (2 * width))) for n in values]
                    expected = sum(t * t * (3 - 2 * t) for t in terms) / 5
                    assert abs(softened[axis][pixel * 4] / 255 - expected) <= 1 / 255
                    if min(map(abs, values)) > width + 0.0001:
                        assert softened[axis][pixel * 4] == result[axis][pixel * 4], "Smoothing modified a plateau outside its band"
            assert softened[2] == softened[3], "Disabled attraction changed soft climate"
        # Exactly zero is the midpoint of the soft threshold, unlike strict >0.
        flat = render(frequency=0.0)
        assert all(abs(value - 127.5) <= 0.5 for image in flat[:2] for value in image[0::4])
        gl.uniform(lab.preview, "uBinaryTransition", "2f", 0.0, 0.0)
        assert render() == result, "Setting transition back to zero did not restore the binary map"


def check_continuous_attraction(lab):
    # Exercise the production function on a dense ramp and around every former
    # round() discontinuity. These properties catch jumps without duplicating
    # its implementation in a CPU oracle.
    gl = lab.gl
    points = sorted(set([i / 4096 for i in range(4097)] +
                        [mid + delta for mid in (0.125, 0.375, 0.625, 0.875)
                         for delta in (-1e-6, 0, 1e-6)]))
    points = struct.unpack(f"<{len(points)}f", floats(points))
    source = "#version 450 core\n" + shader_source(ROOT / "Asset/Shader/ClimateNoiseLab/climate_experiments.glsl") + """
layout(local_size_x=64) in;
layout(std430,binding=6) readonly buffer Input { float points[]; };
layout(std430,binding=7) writeonly buffer Output { float values[]; };
uniform float uShift;
void main() {
    uint i=gl_GlobalInvocationID.x;
    if(i<uint(points.length())) values[i]=attractNearestMatrixLevel01(points[i],uShift);
}
"""
    program = gl.compile([(0x91B9, source)], "continuous climate attraction")
    gl.buffer(floats(points), 6)
    output = gl.buffer(len(points) * 4, 7)
    for shift in (0.0, 1e-10, 0.032, 0.5, 1.0):
        gl.uniform(program, "uShift", "1f", shift)
        gl.dispatch(program, (len(points) + 63) // 64)
        values = struct.unpack(f"<{len(points)}f", gl.read(output, len(points) * 4))
        assert all(math.isfinite(v) and 0 <= v <= 1 and abs(v - p) <= shift + 1e-7
                   for p, v in zip(points, values))
        for i in range(1, len(points)):
            delta = values[i] - values[i - 1]
            assert -2e-7 <= delta <= 8 * (points[i] - points[i - 1]) + 2e-7, "Attraction retains a jump or reverses climate ordering"
        for level in (0, 0.25, 0.5, 0.75, 1):
            assert values[points.index(level)] == level, "Attraction moved a matrix level"
        if shift == 0:
            assert values == points, "Zero attraction must be the identity"


def main():
    try:
        with GL() as gl:
            print(f"OpenGL {gl.version}; {gl.renderer}")
            lab = Lab(gl)
            try:
                for test in (check_initialization, check_validation_and_elitism,
                             check_full_image_oracle, check_preview_coordinates,
                             check_neural_axis_seeds_and_frequency, check_binary_five_noise,
                             check_continuous_attraction):
                    test(lab)
                    gl.check(test.__name__)
                    print(f"PASS {test.__name__}")
            finally:
                lab.close()
    except GLUnavailable as error:
        print(f"SKIP: {error}", file=sys.stderr)
        return 77
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
