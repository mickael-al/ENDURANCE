#!/usr/bin/env python3
"""GPU regression gates for the PR #4 cloud/sky review.

The expected results are physical/behavioral invariants rather than copies of
the production formulas: an empty medium transmits one, a homogeneous bounded
ray obeys Beer--Lambert, occluding a light must darken the *composed* sky, and
the current visible view must not depend on an old lighting cubemap.

These small odd-sized allocations exercise real GLSL and dispatch tails on
EGL. They establish correctness, not a hardware frame-time or image-quality
claim for the production Ultra budgets.
"""
from __future__ import annotations

import argparse
import math
from pathlib import Path
import struct

from check_luts import DIRECTORY, finite, rgb
from check_volumes import VolumeFixture
from fixtures import Parameters
from gl_support import (
    AtmosphereGL, RG16F, RGBA32F, TEXTURE_3D, TEXTURE_CUBE_MAP,
    shader_source,
)

TEST_DIRECTORY = Path(__file__).resolve().parent


def query(gl, program, records):
    """Run a vector probe and reject both nonfinite and unwritten results."""
    width = len(records[0])
    assert width in (4, 8) and all(len(row) == width for row in records)
    payload = b"".join(struct.pack(f"<{width}f", *row) for row in records)
    inputs = gl.buffer(payload, 0)
    outputs = gl.buffer(struct.pack("<4f", *([float("nan")] * 4)) * len(records), 1)
    gl.uniform(program, "geRevisionCount", "1ui", len(records))
    gl.dispatch_extent(program, len(records))
    values = struct.unpack(f"<{len(records) * 4}f", gl.read(outputs, len(records) * 16))
    finite(values, "revision probe")
    gl.check("revision vector query")
    # Keep Python references for clarity; GL owns both allocations until close.
    assert inputs and outputs
    return [values[i:i + 4] for i in range(0, len(values), 4)]


def constant_noise(gl):
    """Uniform shape, no erosion: a test medium with known optical depth."""
    return gl.texture((4, 4, 4), target=TEXTURE_3D, value=(1., .5, 1., .5))


def check_grazing_visibility(gl, fixture):
    f = fixture
    material = gl.compute(TEST_DIRECTORY / "revision_visibility.C.glsl")
    reference = gl.compute(TEST_DIRECTORY / "revision_visibility.C.glsl",
                           "#define GE_REVISION_REFERENCE 1")
    assert gl.glGetUniformLocation(material, b"geAtmCloudNoise") == -1, (
        "Cloud visibility sampled noise in the material pass; sampler 3 is occupied by roughness"
    )
    assert gl.glGetUniformLocation(reference, b"geAtmCloudNoise") >= 0
    noise = constant_noise(gl)
    fake_occluded_map = gl.texture((3, 3), internal=RG16F, value=(0., 0.))
    f.parameters = Parameters()
    f.parameters.set("Control", 0, 0, 1, 0)
    f.parameters.set("CloudLayer", 2000, 1500, 1, 1e-6)
    f.parameters.set("CloudShape", 4000, 4, 0, .6)
    f.parameters.set("CloudWeather", 48000, 0, 1.4, 0)
    f.parameters.set("CloudProfiles", 0, 1, 0, 0)
    f.parameters.set("Samples", 2, 16, 64, 32)
    slopes = (-1., -.002, -.00105, -.001, -.00095, 0.,
              .00095, .001, .00105, .002, 1.)
    cases = [(0., height, 0., 0., math.sqrt(max(0., 1 - y * y)), y, 0., 0.)
             for height in (1000., 2750., 4500.) for y in slopes]
    gl.sampler(3, noise, TEXTURE_3D)
    gl.sampler(5, fake_occluded_map)
    for reason in ("density", "coverage", "disabled"):
        f.parameters.set("Control", 0, 0, 1, 0)
        f.parameters.set("CloudLayer", 2000, 1500, 1, 1e-6)
        if reason == "density":
            f.parameters.values["CloudLayer"][3] = 0
        elif reason == "coverage":
            f.parameters.values["CloudLayer"][2] = 0
        else:
            f.parameters.values["Control"][2] = 0
        f.upload()
        for program in (material, reference):
            results = query(gl, program, cases)
            assert all(row[0] == row[1] == 1 for row in results), (
                "An empty medium shadowed a grazing sun", reason,
                [(case, result) for case, result in zip(cases, results) if result[0] != 1],
            )
    print("PASS grazing empty medium: density 0, coverage 0 and disabled clouds transmit one below/inside/above the layer at 11 light elevations; material pass needs no noise sampler", flush=True)

    # A constant 1e-6 /m medium stays in the flat portion of the cumulus
    # profile for every grazing ray tested here. Its 64 km ray has an
    # independent analytic answer, including a perfectly horizontal source.
    f.parameters.set("Control", 0, 0, 1, 0)
    f.parameters.set("CloudLayer", 2000, 1500, 1, 1e-6)
    expected = math.exp(-1e-6 * 64000.)
    values = []
    for y in (-.00105, -.001, -.00095, 0., .00095, .001, .00105):
        direction = (math.sqrt(1 - y * y), y, 0.)
        f.parameters.set("SunDirection", *direction, 1)
        f.parameters.set("MoonDirection", *direction, 1)
        f.upload()
        gl.sampler(3, noise, TEXTURE_3D)
        gl.image(0, f.cloud_shadow, internal=RG16F)
        gl.dispatch_extent(f.programs["cloud_shadow.C.glsl"], *f.shadow_size)
        gl.sampler(5, f.cloud_shadow)
        row = [(0., 2750., 0., 0., *direction, 0.)]
        approximate = query(gl, material, row)[0]
        integrated = query(gl, reference, row)[0]
        for actual in (*approximate[:2], *integrated[:2]):
            assert 0 <= actual <= 1 and abs(actual - expected) < .002, (
                "Bounded grazing homogeneous ray violates Beer--Lambert", y, actual, expected,
            )
        values.append(approximate[0])
    assert max(values) - min(values) < .002, (
        "Material visibility jumps around the old +/-0.001 horizon threshold", values,
    )

    # These rays cannot reach the cloud slab within 64 km. Integrating a
    # 64 km segment *after* an arbitrarily distant intersection is wrong.
    bounded_cases = [(0., 1000., 0., 0., math.sqrt(1 - .001**2), .001, 0., 0.),
                     (0., 4500., 0., 0., math.sqrt(1 - .001**2), -.001, 0., 0.)]
    for program in (material, reference):
        assert all(row[0] == row[1] == 1 for row in query(gl, program, bounded_cases)), (
            "A cloud beyond the bounded light ray incorrectly attenuated sunlight"
        )
    print(f"PASS grazing nonempty medium: map and 64-step ray agree with analytic T={expected:.6f}, continuous through +/-0.001; out-of-reach cloud slabs remain clear", flush=True)


def check_cloud_shadow_composition(gl, fixture):
    f = fixture
    old_noise = f.noise
    f.noise = constant_noise(gl)
    f.parameters = Parameters()
    f.parameters.set("Control", 0, 0, 1, 1)  # Clouds + terrain shadows only.
    f.parameters.set("Shadow", 0, -1, 1, 0)
    f.parameters.set("Camera", 0, 8000, 0, 0)
    f.parameters.set("Volume", 6000, .5, 0, 0)
    f.parameters.set("CloudLayer", 0, 20000, 1, .0001)
    f.parameters.set("CloudShape", 4000, 4, 0, .6)
    f.parameters.set("CloudWeather", 48000, 0, 1.4, 0)
    f.parameters.set("CloudProfiles", 0, 0, 1, 0)
    f.parameters.set("Medium", .444e-6, .8, 15000, 0)  # No multiple-scattered fill.
    f.parameters.set("Horizon", .04, .04, .04, 0)
    f.parameters.set("Middle", .04, .04, .04, .35)
    f.parameters.set("Zenith", .04, .04, .04, 1)
    f.parameters.set("SunRadiance", 50, 48, 46, 0)
    f.parameters.set("SunDisk", 0, 0, 0, 0)
    f.parameters.set("Samples", 2, 16, 16, 64)
    f.build()
    program = gl.compute(TEST_DIRECTORY / "revision_composite.C.glsl")
    size = f.size[:2]
    raw_output = gl.texture(size, internal=RGBA32F)
    composed_output = gl.texture(size, internal=RGBA32F)

    def capture(blocked):
        scattering, transmission = f.volumes(blocked=blocked)
        baseline = gl.texels(f.baseline_s, size)
        baseline_t = gl.texels(f.baseline_t, size)
        gl.sampler(2, f.sky)
        gl.sampler(3, f.noise, TEXTURE_3D)
        gl.sampler(9, f.baseline_s)
        gl.sampler(10, f.baseline_t)
        gl.sampler(11, f.multiscatter)
        gl.sampler(12, f.scattering, TEXTURE_3D)
        gl.sampler(13, f.transmission, TEXTURE_3D)
        gl.sampler(14, f.cloud_shadow)
        gl.sampler(15, f.transmittance)
        gl.image(0, raw_output, internal=RGBA32F)
        gl.image(1, composed_output, internal=RGBA32F)
        gl.uniform(program, "uAtmosphereActive", "1i", 1)
        gl.dispatch_extent(program, *size)
        raw = gl.texels(raw_output, size)
        composed = gl.texels(composed_output, size)
        for data, label in ((baseline, "reference cloud scattering"),
                            (raw, "unoccluded cloud sky"), (composed, "composed cloud sky")):
            finite(data, label)
        return scattering, transmission, baseline, baseline_t, raw, composed

    lit_s, lit_t, lit_baseline, lit_base_t, lit_raw, lit = capture(False)
    dark_s, dark_t, dark_baseline, dark_base_t, dark_raw, dark = capture(True)
    f.noise = old_noise
    assert dark_t == lit_t and dark_base_t == lit_base_t, "Terrain occlusion changed cloud optical depth"
    assert min(rgb(lit_t)) < .8 and max(rgb(lit_s)) > .1, "Cloud shadow fixture is not optically and radiometrically meaningful"
    assert max(rgb(dark_s)) == 0, "A fully occulted, cloud-only direct source emitted light"
    assert lit_raw == dark_raw, "Terrain-free sky reference unexpectedly depends on terrain shadows"
    assert max(abs(a - b) for a, b in zip(rgb(lit_baseline), rgb(dark_baseline))) < .002, (
        "Reference cloud scattering was terrain-shadowed, which cancels its shadow in sky composition"
    )
    differences = [a - b for a, b in zip(rgb(lit), rgb(dark))]
    assert min(differences) >= -.002 and max(differences) > .05, (
        "Terrain shadow failed to darken the actual composed cloud sky", min(differences), max(differences),
    )
    assert sum(differences) / len(differences) > .01, "Only an isolated numerical outlier changed the sky"
    assert max(abs(a - b) for a, b in zip(rgb(lit), rgb(lit_raw))) < .004, (
        "Unoccluded cloud composition double-counted or removed the captured medium"
    )
    print(f"PASS cloud terrain shadow composition: identical extinction and unoccluded reference, shadowed final sky darkens by up to {max(differences):.4f} HDR; unoccluded composition is identity", flush=True)


def check_weather(gl, fixture):
    f = fixture
    program = gl.compute(TEST_DIRECTORY / "revision_weather.C.glsl")
    f.parameters = Parameters()
    f.parameters.set("Control", 0, 0, 1, 0)
    f.parameters.set("CloudLayer", 2200, 1800, .42, .0012)
    f.parameters.set("CloudShape", 6000, 4, .35, .6)
    f.parameters.set("CloudWeather", 48000, .65, 1.4, .7)
    f.cloud_noise()
    gl.sampler(3, f.noise, TEXTURE_3D)
    positions = [((x * 1.732 + .17) * 48000 / 9, 2560.,
                  (z * 1.619 + .31) * 48000 / 9, 0.)
                 for z in range(32) for x in range(32)]

    def sample(rows=positions):
        f.upload()
        gl.sampler(3, f.noise, TEXTURE_3D)
        return query(gl, program, rows)

    weather = sample()
    assert weather == sample(), "Identical weather parameters are not deterministic"
    coverages = [row[0] for row in weather]
    densities = [row[2] for row in weather]
    assert all(0 <= row[0] <= 1 and 0 <= row[1] <= 1 and 0 <= row[3] <= 1 for row in weather)
    assert max(coverages) - min(coverages) > .2, "Regional weather has insufficient coverage variation"
    clear_count = sum(value == 0 for value in densities)
    cloud_count = sum(value > 1e-5 for value in densities)
    assert clear_count > len(densities) * .05 and cloud_count > len(densities) * .05, (
        "Regional default weather did not produce both clear openings and cloud regions", clear_count, cloud_count,
    )

    f.parameters.values["CloudWeather"][1] = 0
    uniform = sample()
    assert max(abs(row[0] - .42) for row in uniform) < 1e-6, "Weather strength zero does not restore global uniform coverage"
    f.parameters.values["CloudWeather"][1] = .65
    for endpoint in (0., 1.):
        f.parameters.values["CloudLayer"][2] = endpoint
        rows = sample()
        assert all(row[0] == endpoint for row in rows), "Regional weather changed an exact global coverage endpoint"
        if endpoint == 0:
            assert all(row[2] == 0 for row in rows), "Zero global coverage generated local cloud density"
    f.parameters.values["CloudLayer"][2] = .42

    # Test the actual advected fields using the displacement integrated by
    # the engine, independent of the clock still available to custom hooks.
    f.parameters.values["Camera"][3] = 137
    f.parameters.set("CloudOffset", 30 * 137, 5 * 137, 20000, 0)
    stationary = sample()
    assert max(abs(a[0] - b[0]) for a, b in zip(weather, stationary)) > .01, "Regional weather stopped at a stationary camera"
    advected = [(x + 30 * 137, y, z + 5 * 137, w) for x, y, z, w in positions]
    transported = sample(advected)
    assert max(abs(a[i] - b[i]) for a, b in zip(weather, transported) for i in (0, 1, 3)) < 3e-5, (
        "Weather coverage/type/profile do not share the cloud wind domain"
    )
    assert max(abs(a[2] - b[2]) for a, b in zip(weather, transported)) < 2e-7, (
        "3D cloud density does not share the weather wind domain"
    )
    f.parameters.values["Camera"][3] = 0
    f.parameters.set("CloudOffset", 0, 0, 20000, 0)
    f.parameters.values["Misc"][0] += 1
    f.cloud_noise()
    changed_seed = sample()
    assert max(abs(a[0] - b[0]) for a, b in zip(weather, changed_seed)) > .03, "Seed change did not affect regional weather"
    f.parameters.values["Misc"][0] -= 1
    f.cloud_noise()
    assert weather == sample(), "Restoring the seed did not restore weather and density"
    print(f"PASS regional weather: coverage spread {max(coverages) - min(coverages):.3f}, {clear_count}/1024 clear samples and {cloud_count}/1024 cloudy; uniform-strength fallback, exact endpoints, deterministic seeds and coherent advection", flush=True)

    # Height profiles have externally meaningful ordering: a low, thin
    # stratus deck, a deeper cumulus body and a taller tower in the same layer.
    heights = [(1234., 2200 + 1800 * i / 64, 5678., 0.) for i in range(65)]
    profiles = []
    for weights in ((1, 0, 0), (0, 1, 0), (0, 0, 1)):
        f.parameters.set("CloudProfiles", *weights, 0)
        profile = [row[3] for row in sample(heights)]
        assert all(0 <= value <= 1.00001 for value in profile) and profile[0] == profile[-1] == 0
        profiles.append(profile)
    integrals = [sum(values) / 64 for values in profiles]
    centroids = [sum(i / 64 * value for i, value in enumerate(values)) / sum(values)
                 for values in profiles]
    assert integrals[0] < integrals[1] - .2 and integrals[1] < integrals[2] - .1, (
        "Stratus, cumulus and towering weights do not control distinct vertical extents", integrals,
    )
    assert centroids[0] < centroids[1] < centroids[2], "Cloud-type centers of mass have the wrong height ordering"
    for first, second in ((0, 1), (0, 2), (1, 2)):
        assert max(abs(a - b) for a, b in zip(profiles[first], profiles[second])) > .5
    f.parameters.set("CloudProfiles", .25, .5, .25, 0)
    high_positions = [(x, 2200 + 1800 * .7, z, w) for x, _, z, w in positions]
    f.parameters.values["CloudWeather"][3] = 0
    uniform_profile = [row[3] for row in sample(high_positions)]
    assert max(uniform_profile) - min(uniform_profile) < 1e-6
    f.parameters.values["CloudWeather"][3] = 1
    regional_profile = [row[3] for row in sample(high_positions)]
    assert max(regional_profile) - min(regional_profile) > .05, "Regional cloud types did not alter vertical shape"
    print(f"PASS cloud profiles: stratus/cumulus/tower column extents {integrals[0]:.3f}/{integrals[1]:.3f}/{integrals[2]:.3f}, ordered heights and independently adjustable regional type variation", flush=True)


def check_current_visible_clouds(gl, fixture):
    f = fixture
    f.parameters = Parameters()
    f.parameters.set("Control", 1, 0, 1, 0)
    f.parameters.set("CloudLayer", 2200, 1800, .58, .0012)
    f.parameters.set("CloudShape", 6000, 4, .35, .6)
    f.parameters.set("Samples", 2, 8, 16, 48)
    f.parameters.set("SunDisk", 0, 0, 0, 0)
    f.build()
    f.cloud_noise()
    size = (25, 17)
    visible_program = gl.compute(DIRECTORY / "visible_clouds.C.glsl")
    reference_program = gl.compute(TEST_DIRECTORY / "revision_sky.C.glsl")
    draw_program = gl.compile([
        (0x8B31, shader_source(DIRECTORY / "sky.vs.glsl")),
        (0x8B30, shader_source(DIRECTORY / "sky.fs.glsl", "#define GE_PHYSICAL_SKY 1")),
    ], "current visible cloud sky")
    assert gl.glGetUniformLocation(visible_program, b"geDisplayedEnvironment") == -1, (
        "Current cloud computation depends on delayed lighting environment"
    )
    assert gl.glGetUniformLocation(draw_program, b"geCurrentCloudView") >= 0
    delta = gl.texture(size)
    direct = gl.texture(size, internal=RGBA32F)
    outputs = [gl.texture(size, internal=RGBA32F) for _ in range(4)]
    old_cube = gl.texture((3, 3), target=TEXTURE_CUBE_MAP, value=(9., 2., 5., 1.))
    other_cube = gl.texture((3, 3), target=TEXTURE_CUBE_MAP, value=(1., 8., 3., 1.))

    def draw(cube=old_cube, current=True):
        gl.sampler(0, cube, TEXTURE_CUBE_MAP)
        gl.sampler(1, delta)
        gl.sampler(2, f.sky)
        gl.sampler(3, f.noise, TEXTURE_3D)
        gl.sampler(11, f.multiscatter)
        gl.sampler(14, f.cloud_shadow)
        gl.sampler(15, f.transmittance)
        gl.uniform(draw_program, "geCurrentCloudView", "1i", int(current))
        gl.fullscreen(draw_program, *size, outputs)
        actual = gl.texels(outputs[2], size)
        finite(actual, "current visible sky framebuffer")
        assert all(value == -2 for value in gl.texels(outputs[0], size)[3::4]), "Current sky lost its deferred HDR sky marker"
        return actual

    def capture():
        f.upload()
        gl.sampler(0, f.transmittance)
        gl.sampler(1, f.multiscatter)
        gl.sampler(2, f.sky)
        gl.sampler(3, f.noise, TEXTURE_3D)
        gl.image(0, delta)
        gl.dispatch_extent(visible_program, *size)
        cloud_delta = gl.texels(delta, size)
        finite(cloud_delta, "current visible cloud delta including odd dispatch tails")
        gl.image(0, direct, internal=RGBA32F)
        gl.dispatch_extent(reference_program, *size)
        expected = gl.texels(direct, size)
        finite(expected, "independent current-view sky evaluation")
        actual = draw()
        errors = [abs(a - b) for a, b in zip(rgb(actual), rgb(expected))]
        assert all(abs(a - b) <= .004 + .002 * abs(b)
                   for a, b in zip(rgb(actual), rgb(expected))), (
            "Visible sky disagrees with the current camera ray (RGBA16F delta tolerance)", max(errors),
        )
        assert max(abs(value) for value in rgb(cloud_delta)) > .02, "Visible cloud fixture contains no clouds"
        assert draw(other_cube) == actual, "Visible cloud view changed when only the frozen IBL environment changed"
        return actual

    initial = capture()
    f.parameters.values["Camera"][3] = 137
    assert capture() == initial, "The clock moved visible clouds without an accumulated displacement"
    f.parameters.set("CloudOffset", 30 * 137, 5 * 137, 20000, 0)
    later = capture()
    offset_difference = max(abs(a - b) for a, b in zip(rgb(initial), rgb(later)))
    assert offset_difference > .02, "Visible clouds ignored accumulated displacement while camera and IBL stayed fixed"
    f.parameters.set("Camera", 7100, 100, -4300, 137)
    moved = capture()
    position_difference = max(abs(a - b) for a, b in zip(rgb(later), rgb(moved)))
    assert position_difference > .02, "Visible clouds failed to follow current camera XZ with frozen IBL"
    assert draw(current=False) != draw(other_cube, current=False), "Sky fallback no longer uses its environment cube"

    # An empty cloud view writes its entire delta to zero, so an old cloudy
    # allocation cannot survive switching coverage off between IBL captures.
    f.parameters.values["CloudLayer"][2] = 0
    f.upload()
    gl.sampler(0, f.transmittance)
    gl.sampler(1, f.multiscatter)
    gl.sampler(2, f.sky)
    gl.sampler(3, f.noise, TEXTURE_3D)
    gl.image(0, delta)
    gl.dispatch_extent(visible_program, *size)
    assert all(value == 0 for value in rgb(gl.texels(delta, size))), "Coverage-zero visible cloud pass retained stale cloud pixels"
    print(f"PASS current visible sky: real sky.fs matches current cloud ray, ignores changed frozen IBL and clock-only changes, advances with cloud displacement at fixed camera (+{offset_difference:.4f} HDR) and after camera XZ motion (+{position_difference:.4f}); empty view clears stale delta", flush=True)


def check_accumulated_advection(gl, fixture):
    """Velocity edits must not reinterpret all elapsed time in the shaders."""
    f = fixture
    program = gl.compute(TEST_DIRECTORY / "revision_weather.C.glsl")
    f.parameters = Parameters()
    f.parameters.set("Control", 0, 0, 1, 0)
    f.parameters.set("CloudLayer", 2200, 1800, .58, .0012)
    f.parameters.set("CloudShape", 6000, 4, .35, .6)
    f.parameters.set("CloudWeather", 48000, .65, 1.4, .7)
    f.parameters.set("Samples", 2, 8, 32, 48)
    f.parameters.set("SunDirection", .3, .9, .3, 1)
    f.parameters.set("MoonDirection", -.6, .7, .3, 1)
    # Begin at a nonzero phase: a leftover offset*time calculation must fail
    # even when no displacement is added during the time-only checks.
    phase = (-875., 2125.)
    f.parameters.set("CloudOffset", *phase, 20000, 0)
    f.cloud_noise()
    positions = [(x * 1733.19 - 14000, 2560., z * 1931.13 - 11000, 0.)
                 for z in range(13) for x in range(17)]

    def capture(rows=positions):
        f.upload()
        gl.sampler(3, f.noise, TEXTURE_3D)
        fields = query(gl, program, rows)
        gl.image(0, f.cloud_shadow, internal=RG16F)
        gl.dispatch_extent(f.programs["cloud_shadow.C.glsl"], *f.shadow_size)
        shadows = gl.texels(f.cloud_shadow, f.shadow_size, channels=2)
        finite(shadows, "advected sun/moon cloud-shadow map")
        assert all(0 <= value <= 1 for value in shadows)
        return fields, shadows

    original_fields, original_shadows = capture()
    f.parameters.values["Camera"][3] = 137
    assert capture() == (original_fields, original_shadows), (
        "The clock alone displaced regional weather, 3D density or cloud shadows at fixed nonzero offset"
    )

    delta = (4110., 685.)
    f.parameters.set("CloudOffset", phase[0] + delta[0], phase[1] + delta[1], 20000, 0)
    moved_fields, moved_shadows = capture()
    coverage_change = max(abs(a[0] - b[0]) for a, b in zip(original_fields, moved_fields))
    density_change = max(abs(a[2] - b[2]) for a, b in zip(original_fields, moved_fields))
    shadow_change = max(abs(a - b) for a, b in zip(original_shadows, moved_shadows))
    assert coverage_change > .01, "Accumulated displacement did not advect regional weather"
    assert density_change > 1e-5, "Accumulated displacement did not advect 3D cloud density"
    assert shadow_change > .01, "Accumulated displacement did not advect cloud shadows"

    # Translating observations and the shadow-map world origin by exactly
    # the added displacement must recover the same medium and optical depth.
    # This checks shared direction/scale and transport, not just 'it changed'.
    transported_positions = [(x + delta[0], y, z + delta[1], w)
                             for x, y, z, w in positions]
    f.parameters.set("ShadowOrigin", delta[0], delta[1], 0, 0)
    transported_fields, transported_shadows = capture(transported_positions)
    assert max(abs(a[i] - b[i]) for a, b in zip(original_fields, transported_fields)
               for i in (0, 1, 3)) < 3e-5, "Weather/profile transport disagrees with accumulated displacement"
    assert max(abs(a[2] - b[2]) for a, b in zip(original_fields, transported_fields)) < 2e-7, (
        "3D density transport disagrees with regional weather displacement"
    )
    shadow_transport_error = max(abs(a - b) for a, b in zip(original_shadows, transported_shadows))
    assert shadow_transport_error < .0015, (
        "Sun/moon shadow-map transport disagrees with weather and density displacement", shadow_transport_error,
    )
    f.parameters.values["Camera"][3] = 1_000_000
    assert capture(transported_positions) == (transported_fields, transported_shadows), (
        "Large elapsed time changed cloud phase despite an unchanged accumulated offset"
    )
    print(f"PASS accumulated cloud displacement: clock changes to 137 s and 1,000,000 s preserve fixed nonzero phase exactly; offset moves coverage ({coverage_change:.4f}), density ({density_change:.6f}/m) and shadows ({shadow_change:.4f}); translating observations recovers all fields and both shadow channels (error {shadow_transport_error:.6f})", flush=True)


CHECKS = {
    "visibility": check_grazing_visibility,
    "composition": check_cloud_shadow_composition,
    "weather": check_weather,
    "visible": check_current_visible_clouds,
    "advection": check_accumulated_advection,
}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--group", choices=CHECKS, help="Run a single regression group")
    arguments = parser.parse_args()
    with AtmosphereGL() as gl:
        print(f"OpenGL {gl.version}; renderer: {gl.renderer}", flush=True)
        fixture = VolumeFixture(gl)
        for name, check in CHECKS.items():
            if arguments.group is None or arguments.group == name:
                check(gl, fixture)
        gl.check("PR #4 regression suite")
    print("PASS requested cloud/sky regression gates", flush=True)


if __name__ == "__main__":
    main()
