#!/usr/bin/env python3
"""Compare production FP32 packing with independent, frozen unpacked GPU passes.

Only storage interfaces differ: both versions include the current physical
medium implementation. No Git history or recorded GPU image is needed. The
assertions deliberately require bit-identical coefficients and final half-float
results, not merely a generous visual tolerance.
"""
from __future__ import annotations

import math
from pathlib import Path
import struct

from check_luts import finite
from check_volumes import VolumeFixture
from fixtures import Parameters
from gl_support import AtmosphereGL, RGBA32F, R32F, TEXTURE_3D, TEXTURE_2D_ARRAY

REFERENCE = Path(__file__).resolve().parent / "texture_packing_reference"


def identical(actual, expected, label):
    finite(actual, label)
    finite(expected, label + " reference")
    # Comparing bytes also catches changes hidden by Python's -0.0 == +0.0.
    assert struct.pack(f"<{len(actual)}f", *actual) == struct.pack(f"<{len(expected)}f", *expected), (
        label, "packing changed stored float values",
        max(abs(a - b) for a, b in zip(actual, expected)))


def results(f):
    gl = f.gl
    return (gl.texels(f.scattering, f.size, target=TEXTURE_3D),
            gl.texels(f.transmission, f.size, target=TEXTURE_3D),
            gl.texels(f.baseline_s, f.size[:2]),
            gl.texels(f.baseline_t, f.size[:2]))


def unpack_current(f):
    gl = f.gl
    physical = gl.texels(f.coefficients[0], f.size, target=TEXTURE_3D)
    extinction_gb = gl.texels(f.coefficients[1], f.size, target=TEXTURE_3D, channels=2)
    cloud = gl.texels(f.coefficients[2], f.size, target=TEXTURE_3D)
    source, extinction, cloud_source, cloud_extinction = [], [], [], []
    for i in range(len(physical) // 4):
        source.extend(physical[i * 4:i * 4 + 3])
        extinction.extend((physical[i * 4 + 3], *extinction_gb[i * 2:i * 2 + 2]))
        cloud_source.extend(cloud[i * 4:i * 4 + 3])
        cloud_extinction.append(cloud[i * 4 + 3])
    return source, extinction, cloud_source, cloud_extinction


def unpack_reference(f, coefficients):
    values = [f.gl.texels(texture, f.size, target=TEXTURE_3D, channels=1 if i == 3 else 4)
              for i, texture in enumerate(coefficients)]
    return tuple([v for j, v in enumerate(data) if j % 4 != 3] if i < 3 else list(data)
                 for i, data in enumerate(values))


def integrate_reference(f, programs, coefficients, *, blocked=False):
    gl = f.gl
    f.upload()
    gl.sampler(0, f.transmittance)
    gl.sampler(1, f.multiscatter)
    gl.sampler(3, f.noise, TEXTURE_3D)
    gl.sampler(4, f.blocked_shadow if blocked else f.lit_shadow, TEXTURE_2D_ARRAY)
    gl.sampler(5, f.cloud_shadow)
    gl.bind(f.shadow_matrices, 5)
    for name, start in (("aerial.C.glsl", 0), ("cloud_volume.C.glsl", 2)):
        gl.image(0, coefficients[start], internal=RGBA32F, layered=True)
        gl.image(1, coefficients[start + 1], internal=R32F if start == 2 else RGBA32F, layered=True)
        gl.dispatch_extent(programs[name], *f.size)
    integrate(f, programs["volume.C.glsl"], coefficients)


def integrate(f, program, coefficients):
    gl = f.gl
    f.upload()
    for unit, texture in enumerate(coefficients, 9):
        gl.sampler(unit, texture, TEXTURE_3D)
    gl.image(0, f.scattering, layered=True)
    gl.image(1, f.transmission, layered=True)
    gl.image(2, f.baseline_s)
    gl.image(3, f.baseline_t)
    gl.dispatch_extent(program, *f.size[:2])
    gl.check("texture packing integration")


def check_texture_packing(gl):
    f = VolumeFixture(gl)
    programs = {name: gl.compute(REFERENCE / name)
                for name in ("aerial.C.glsl", "cloud_volume.C.glsl", "volume.C.glsl")}
    coefficients = [gl.texture(f.size, target=TEXTURE_3D, internal=R32F if i == 3 else RGBA32F)
                    for i in range(4)]

    scenes = []
    scenes.append(("day", Parameters(), False))
    moon = Parameters().set("SunDirection", 0, 0, 0, 0)
    moon.set("MoonDirection", 0, math.sqrt(.5), math.sqrt(.5), 1)
    moon.set("MoonRadiance", .002, .0015, .001, 0)
    scenes.append(("faint moon", moon, False))
    altitude = Parameters().set("Camera", 100000, 24000, -80000, 0)
    altitude.set("SunRadiance", .02, .019, .018, 0)
    scenes.append(("thin high-altitude medium", altitude, False))
    cloudy = Parameters().set("Control", 1, 1, 1, 1)
    cloudy.set("CloudLayer", 2000, 1500, 1, .002)
    cloudy.set("Volume", 20000, .5, .00002, 5000)
    cloudy.set("Shadow", 0, -1, 1, 0)
    scenes.extend((("clouds, fog and lit terrain", cloudy, False),
                   ("clouds, fog and occluded terrain", cloudy, True)))
    dark = Parameters().set("Control", 1, 0, 1, 0)
    dark.set("CloudLayer", 2000, 1500, 1, .002)
    dark.set("SunDirection", 0, 0, 0, 0).set("MoonDirection", 0, 0, 0, 0)
    scenes.append(("unlit clouds", dark, False))
    scenes.append(("no media", Parameters().set("Control", 0, 0, 0, 0), False))

    saw_tiny_source = saw_cloud = False
    for label, parameters, blocked in scenes:
        f.parameters = parameters
        f.build()
        f.cloud_noise()
        f.volumes(blocked=blocked)
        current_coefficients = unpack_current(f)
        current_outputs = results(f)
        integrate_reference(f, programs, coefficients, blocked=blocked)
        reference_coefficients = unpack_reference(f, coefficients)
        for i, (actual, expected) in enumerate(zip(current_coefficients, reference_coefficients)):
            identical(actual, expected, f"{label}: coefficient {i}")
        for i, (actual, expected) in enumerate(zip(current_outputs, results(f))):
            identical(actual, expected, f"{label}: integrated/baseline result {i}")
        saw_tiny_source |= any(0 < v < 2.98e-8 for v in current_coefficients[0])
        saw_cloud |= any(v > 1e-4 for v in current_coefficients[3])
        print(f"PASS lossless texture packing: {label}, all coefficient channels and four integration outputs bit-identical", flush=True)

    assert saw_tiny_source, "Fixture did not exercise coefficients erased by half-float storage"
    assert saw_cloud, "Fixture did not exercise populated cloud extinction"

    # Runtime uses one-voxel neutral textures for disabled media. They must
    # remain valid even though the output volume and camera coordinates are
    # much larger; explicitly use NaNs to expose any accidental fetch.
    from gl_support import RG32F
    neutral_current = [gl.texture((1, 1, 1), target=TEXTURE_3D, internal=internal)
                       for internal in (RGBA32F, RG32F, RGBA32F)]
    neutral_reference = [gl.texture((1, 1, 1), target=TEXTURE_3D, internal=R32F if i == 3 else RGBA32F)
                         for i in range(4)]
    f.parameters = Parameters().set("Control", 0, 0, 0, 0)
    integrate(f, f.programs["volume.C.glsl"], neutral_current)
    packed_identity = results(f)
    integrate(f, programs["volume.C.glsl"], neutral_reference)
    for i, (actual, expected) in enumerate(zip(packed_identity, results(f))):
        identical(actual, expected, f"disabled neutral allocation: output {i}")
    assert all(v == 0 for i, v in enumerate(packed_identity[0]) if i % 4 != 3)
    assert all(v == 1 for v in packed_identity[1])
    print("PASS lossless texture packing: disabled one-voxel media are never sampled and retain exact identity", flush=True)


def main():
    with AtmosphereGL() as gl:
        print(f"OpenGL {gl.version}; renderer: {gl.renderer}", flush=True)
        check_texture_packing(gl)


if __name__ == "__main__":
    main()
