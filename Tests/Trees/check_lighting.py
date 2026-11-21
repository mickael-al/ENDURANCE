#!/usr/bin/env python3
"""Exercise thin foliage through the production deferred shader in EGL.

Optional --captures consumes the real renderer's .gbuf dumps (check_runtime.py)
and shades them with this same deferred program, fixed lighting and exposure.
These isolated previews are not screenshots or GTX performance measurements.
"""
import argparse
import ctypes as C
import math
from pathlib import Path
import re
import struct
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "Tests/Atmosphere"))
from check_screen_composition import ScreenGL, DeferredFixture, maximum_error, unit
from gl_support import RG32F, RGBA16F, RGBA32F, TEXTURE_2D, TEXTURE_2D_ARRAY, TEXTURE_CUBE_MAP, shader_source


def program(gl, defines="", baseline=None):
    source = shader_source(ROOT / "Asset/Shader/forward.fs.glsl", defines)
    if baseline:
        source = re.sub(r'^\s*#include\s+"([^"]+)"\s*$',
                        lambda m: shader_source(ROOT / "Asset/Shader" / m[1]),
                        baseline.read_text(), flags=re.MULTILINE)
    return gl.compile([(0x8B31, shader_source(ROOT / "Asset/Shader/forward.vs.glsl")),
                       (0x8B30, source)], "production deferred foliage " + defines)


def illumination(gl, fixture, direction=(.2, .4, 1), *, light_type=0, enabled=True,
                 ambient=0., shadow=False, irradiance=(0., 0., 0., 1.)):
    light = bytearray(80)
    direction = unit(direction)
    # Point/spot fixtures look at the Z=-20 plane; keep the light on its back
    # side for the same transmission test as a backlit directional light.
    struct.pack_into("<3f", light, 0, 0, 0, -40)
    struct.pack_into("<3f", light, 16, 1, 1, 1)
    struct.pack_into("<3f", light, 32, *(-v for v in direction) if light_type==0 else (0, 0, 1))
    struct.pack_into("<2f4i", light, 44, 10 if light_type==0 else 400, 150,
                     light_type, 0 if shadow else -1, -1, -1)
    gl.buffer(bytes(light), 1)
    gl.buffer(struct.pack("<i4fi", int(enabled), 0., 1., ambient, 75., 0), 2)
    matrix = (.01, 0, 0, 0, 0, .01, 0, 0, 0, 0, .01, 0, 0, 0, 0, 1)
    gl.buffer(b"".join(struct.pack("<20f", *matrix, 0, 0, 0, split)
                      for split in (-100., -500., -1000., -10000.)), 3)
    fixture.samplers[4] = (gl.texture((4, 4, 4), target=TEXTURE_2D_ARRAY,
                                    internal=0x822E, value=0 if shadow else 1), TEXTURE_2D_ARRAY)
    fixture.samplers[6] = (gl.texture((2, 2), target=TEXTURE_CUBE_MAP,
                                    value=irradiance), TEXTURE_CUBE_MAP)
    fixture.samplers[7] = (gl.texture((16, 16), target=TEXTURE_CUBE_MAP, levels=5,
                                    value=(0, 0, 0, 1)), TEXTURE_CUBE_MAP)


def surface(gl, fixture, alpha, normal=(0., 0., 1.), *, metallic=0., roughness=.82):
    count = math.prod(fixture.size)
    for slot, values, internal in (
        (0, [(-20., 0.)] * count, RG32F),
        (1, [(*normal, 1.)] * count, RGBA32F),
        (2, [(.24, .46, .08, alpha)] * count, RGBA16F),
        (3, [(metallic, roughness, 1., 1.)] * count, RGBA32F)):
        fixture.samplers[slot] = (gl.field_texture(fixture.size, values, repeat=True,
                                                  internal=internal), TEXTURE_2D)


def average(image):
    return sum(sum(pixel) for pixel in image) / (3 * len(image))


def checks(gl, current, disabled, baseline):
    fixture = DeferredFixture(gl, (33, 25), 0, 0, "opaque", camera_position=(0, 0, 0))
    # Existing positive-alpha materials keep the same output across three light
    # types and both atmosphere composition states, including bright HDR values.
    largest = 0.
    for light_type in range(3):
        illumination(gl, fixture, light_type=light_type, ambient=.4, irradiance=(.4,.5,.7,1))
        for alpha in (0., .5, 1.):
            surface(gl, fixture, alpha)
            for atmosphere in (False, True):
                result = fixture.render(current, atmosphere)
                for reference in (disabled, baseline):
                    if reference:
                        error = maximum_error(result, fixture.render(reference, atmosphere))
                        largest = max(largest, error)
                        assert error < 2e-5, ("opaque regression", light_type, alpha, error)
    print(f"PASS opaque/terrain/wood path: max difference {largest:.3g}", flush=True)

    illumination(gl, fixture, direction=(.1, .1, -1))
    surface(gl, fixture, 1)
    opaque = fixture.render(current, False)
    surface(gl, fixture, -.65)
    leaves = fixture.render(current, False)
    assert average(leaves) > average(opaque) + .02, "Backlighting never reaches leaves"
    assert maximum_error(opaque, fixture.render(disabled, False)) < 1e-6, "Static disable did not restore opaque response"
    print(f"PASS backlighting: opaque mean {average(opaque):.5f}, foliage {average(leaves):.5f}", flush=True)
    illumination(gl, fixture, direction=(0, 0, -1))
    exact_backlight = fixture.render(current, False)
    assert min(exact_backlight[len(exact_backlight)//2]) > .001, "Exact backlight lost transmission"
    for kind in (1, 2):
        illumination(gl, fixture, light_type=kind)
        lit = fixture.render(current, False)
        assert min(lit[len(lit)//2]) > .01 and average(lit) > .005, ("missing local-light transmission", kind)

    illumination(gl, fixture, direction=(.1, .1, -1), shadow=True)
    assert max(max(row) for row in fixture.render(current, False)) < 1e-6, "Transmission escaped the shadow map"
    illumination(gl, fixture, enabled=False)
    assert max(max(row) for row in fixture.render(current, False)) < 1e-6, "Foliage emits light in darkness"
    print("PASS directional/point/spot transmission, fully blocked shadow, no unlit emission", flush=True)

    illumination(gl, fixture, enabled=False, ambient=1, irradiance=(.4,.5,.7,1))
    surface(gl, fixture, 1)
    opaque = fixture.render(current, False)
    surface(gl, fixture, -.65)
    assert maximum_error(opaque, fixture.render(current, False)) < 2e-6, "Constant IBL gained energy"
    # Positive/negative pole irradiance tests the actual second cubemap fetch.
    texture, _ = fixture.samplers[6]
    gl.glBindTexture(TEXTURE_CUBE_MAP, texture)
    for face in range(6):
        value = (.05, .05, .05, 1) if face == 4 else (.8, .8, .8, 1)
        data = struct.pack("<4f", *value) * 4
        gl.glTexImage2D(0x8515+face, 0, RGBA16F, 2, 2, 0, 0x1908, 0x1406, C.c_char_p(data))
    surface(gl, fixture, 1)
    front = average(fixture.render(current, False))
    surface(gl, fixture, -.65)
    assert average(fixture.render(current, False)) > front * 2, "Backside skylight ignored"
    print("PASS two-sided IBL: constant-environment energy preserved, opposite hemisphere sampled", flush=True)

    illumination(gl, fixture, direction=(.1, .1, -1), ambient=.2, irradiance=(.2,.3,.4,1))
    for normal in ((0,0,1), (0,0,-1), (1,0,0), unit((1,1,.001))):
        for strength in (.001, .65, 1):
            surface(gl, fixture, -strength, normal)
            image = fixture.render(current, False)
            assert all(math.isfinite(c) and c >= 0 for row in image for c in row)
    # The crown-height ramp must approach opaque continuously, including its
    # specular/IBL response on a back-facing card, with no material seam.
    for normal in ((0,0,1),(0,0,-1),(1,0,0)):
        surface(gl,fixture,1,normal); opaque=fixture.render(current,False)
        surface(gl,fixture,-1e-6,normal)
        assert maximum_error(opaque,fixture.render(current,False)) < 5e-6, "Crown mask has an opaque/foliage discontinuity"
    print("PASS grazing/back-facing normals, endpoints and continuous crown mask", flush=True)


def captures(gl, current, directory):
    from PIL import Image, ImageDraw
    import numpy as np
    names = []
    for path in sorted(directory.glob("*.gbuf")):
        data = path.read_bytes(); size, = struct.unpack_from("<I", data)
        camera = struct.unpack_from("<52f", data, 4)
        fixture = DeferredFixture(gl, (size,size), 0, 0, "opaque", camera_position=camera[:3])
        # Fixed sun + hemisphere IBL, same exposure for every image. No shadows
        # are captured here; shadow geometry/visibility has separate test gates.
        illumination(gl, fixture, direction=(-.3,.6,.7), ambient=.7, irradiance=(.3,.4,.55,1))
        gl.buffer(struct.pack("<i4fi", 1, 0., camera[3], .7, 60., 0), 2)
        fixture.camera_buffer = gl.buffer(struct.pack("<52f", *camera[:3],0,*camera[4:]), 0)
        offset = 4 + 52*4; mask = None
        for slot, channels in enumerate((2,3,4,3)):
            array = np.frombuffer(data, dtype="<f4", count=size*size*channels, offset=offset).reshape(-1,channels).copy()
            offset += array.nbytes
            if slot == 0:
                mask = array[:,0] < 0
                array[~mask] = (-1000,-2)
            elif slot == 2:
                array[~mask] = (.07,.10,.16,1)
            if channels == 3:
                array = np.column_stack([array,np.ones(len(array))])
            fixture.samplers[slot] = (gl.field_texture((size,size),array,repeat=True,
                internal=RG32F if slot==0 else RGBA16F if slot==2 else RGBA32F),TEXTURE_2D)
        image = np.array(fixture.render(current, False)).reshape(size,size,3)[::-1]
        # Fixed Reinhard exposure and display gamma, applied equally.
        image = np.power(image*2/(1+image*2),1/2.2)
        image = Image.fromarray(np.uint8(np.clip(image*255,0,255)))
        image.save(path.with_suffix(".png")); names.append((path.stem,image))
    if names:
        sheet = Image.new("RGB",(size*len(names),size+36),(20,24,30)); draw = ImageDraw.Draw(sheet)
        for i,(label,image) in enumerate(names):
            sheet.paste(image,(i*size,36)); draw.text((i*size+8,10),label,fill="white")
        sheet.save(directory/"deferred-comparison.png")
        print(f"Production deferred previews: {directory/'deferred-comparison.png'}",flush=True)


def check_lunar_specular(gl, current, baseline):
    """Compare guarded GGX against a supplied pre-optimization shader.

    Exercise the actual per-light suppression uniform, not only the ordinary
    material path: the extended moon is reflected by IBL while Fresnel must
    still control direct diffuse energy on metals and thin foliage.
    """
    fixture = DeferredFixture(gl, (17, 13), 0, 0, "opaque", camera_position=(0, 0, 0))
    illumination(gl, fixture, direction=(.2, .2, 1), ambient=.3, irradiance=(.3,.4,.5,1))
    fixture.parameters.set("Control", 0, 0, 0, 0)
    fixture.parameters.set("MoonRadiance", .8, .9, 1., 1.)

    def render(shader, suppress):
        fixture.render(shader, True)  # Bind the complete production fixture.
        gl.uniform(shader, "uAtmosphereMoonIndex", "1i", 0)
        gl.uniform(shader, "uAtmosphereIblActive", "1i", int(suppress))
        gl.uniform(shader, "uAtmosphereIblSaturation", "1f", 1.)
        return gl.draw_engine_triangle(shader, fixture.size)

    largest, suppression = 0., 0.
    for metallic in (0., 1.):
        for roughness in (.08, .4, .95):
            for alpha in (1., -.65):
                surface(gl, fixture, alpha, metallic=metallic, roughness=roughness)
                states = []
                for suppress in (False, True):
                    image = render(current, suppress)
                    error = maximum_error(image, render(baseline, suppress))
                    largest = max(largest, error)
                    assert error < 2e-5, ("lunar GGX/Fresnel regression", metallic, roughness, alpha, suppress, error)
                    states.append(image)
                suppression = max(suppression, maximum_error(*states))
    assert suppression > .001, "Fixture never exercises lunar specular suppression"
    print(f"PASS lunar GGX uniform guard: pre-optimization agreement {largest:.3g}, opaque/foliage, metallic/roughness endpoints", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline",type=Path,help="Previous forward.fs.glsl, expands repository-relative includes")
    parser.add_argument("--captures",type=Path)
    args = parser.parse_args()
    with ScreenGL() as gl:
        print(f"OpenGL {gl.version}; {gl.renderer}",flush=True)
        current = program(gl)
        disabled = program(gl,"#define GE_FOLIAGE_LIGHTING 0")
        baseline = program(gl,baseline=args.baseline) if args.baseline else None
        checks(gl,current,disabled,baseline)
        if baseline: check_lunar_specular(gl,current,baseline)
        if args.captures: captures(gl,current,args.captures)


if __name__ == "__main__": main()
