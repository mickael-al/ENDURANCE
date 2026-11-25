#!/usr/bin/env python3
"""Measure sparse-glint sampling error with the real deferred lighting shader.

Each camera frame has a 4x4 spatial sampling reference. The reference scales
the terrain's derivatives by four: screen-sized glints must retain the target
resolution's size and grid levels, not become sixteen times denser. It uses
the production shader otherwise. HDR lighting is averaged before tone mapping.

Temporal residuals are reprojected through the static world plane and divided
by reference glint RMS, so simple dimming cannot masquerade as an improvement.
This finite reference and synthetic scene are diagnostics, not proof of zero
aliasing, physical accuracy, in-game performance, or all-camera stability.
"""
import argparse
import ctypes as C
import hashlib
import json
import math
from pathlib import Path
import struct
import subprocess

import numpy as np

from check_granular_render import (
    ROOT, TERRAIN, ScreenGL, DeferredFixture, camera, make_rig, draw,
    program, illumination, literal_table, edited_vec, shader_source,
    snapshot_source, PLANE,
)


def target_derivatives(source, factor):
    """Supersample one target-pixel signal without changing its grain LOD."""
    for axis in ('x', 'y'):
        expression = f'dFd{axis}(worldPosition)'
        assert expression in source, 'terrain derivative capture changed'
        source = source.replace(expression, f'({expression}*{float(factor):.1f})')
    return source


def matrices(size, position):
    position, _, view, inverse, projection = camera(-24, 0, size[0]/size[1], position)
    f = 1/math.tan(math.radians(45)/2)
    projection[0] = f*size[1]/size[0]
    projection[5] = f
    # The shared distant-atmosphere fixture uses a 1 m near plane, which would
    # clip the close-up grain fixture and leave poisoned G-buffer texels.
    near, far = .01, 10000.
    projection[10] = -(far+near)/(far-near)
    projection[14] = -2*far*near/(far-near)
    return position, view, inverse, projection


def set_camera(gl, fixture, rigs, position):
    position, view, inverse, projection = matrices(fixture.size, position)
    payload = struct.pack('<52f', *position, 0, *view, *inverse, *projection)
    gl.glBindBuffer(0x90D2, fixture.camera_buffer)
    gl.glBufferData(0x90D2, len(payload), C.c_char_p(payload), 0x88E8)
    for rig in rigs:
        rig.set(terrainGrainCameraPosition=position)


def world_plane(size, position):
    """Independent perspective ray/plane intersections at pixel centres."""
    _, _, inverse, projection = matrices(size, position)
    y, x = np.mgrid[:size[1], :size[0]]
    rays = np.stack(((2*(x+.5)/size[0]-1)/projection[0],
                     (2*(y+.5)/size[1]-1)/projection[5],
                     -np.ones_like(x)), axis=-1)
    rotation = np.asarray(inverse).reshape(4, 4, order='F')[:3, :3]
    rays = rays @ rotation.T
    distance = -position[1]/rays[..., 1]
    return np.asarray(position)+rays*distance[..., None]


def project(size, position, world):
    _, view, _, projection = matrices(size, position)
    view = np.asarray(view).reshape(4, 4, order='F')
    p = world @ view[:3, :3].T+view[:3, 3]
    return np.stack(((p[..., 0]*projection[0]/-p[..., 2]+1)*size[0]/2-.5,
                     (p[..., 1]*projection[5]/-p[..., 2]+1)*size[1]/2-.5), axis=-1)


def reproject(image, pixels):
    x, y = pixels[..., 0], pixels[..., 1]
    valid = (x >= 1)&(y >= 1)&(x < image.shape[1]-2)&(y < image.shape[0]-2)
    ix = np.clip(np.floor(x).astype(int), 0, image.shape[1]-2)
    iy = np.clip(np.floor(y).astype(int), 0, image.shape[0]-2)
    fx, fy = x-ix, y-iy
    result = ((1-fx)*(1-fy)*image[iy, ix]+fx*(1-fy)*image[iy, ix+1]
              +(1-fx)*fy*image[iy+1, ix]+fx*fy*image[iy+1, ix+1])
    return result, valid


def luminance(image):
    return image @ np.array((.2126, .7152, .0722))


def display(rgb):
    # Fixed exposure, identical across candidates and references, no bloom.
    return 1-np.exp(-np.maximum(rgb, 0)*2)


def components(mask):
    """Diagnostic bright-component sizes and aspect ratios (8 neighbours)."""
    seen = np.zeros_like(mask, dtype=bool)
    sizes, aspects = [], []
    for y, x in zip(*np.nonzero(mask)):
        if seen[y, x]:
            continue
        stack = [(y, x)]
        seen[y, x] = True
        points = []
        while stack:
            py, px = stack.pop()
            points.append((py, px))
            for dy in (-1, 0, 1):
                for dx in (-1, 0, 1):
                    ny, nx = py+dy, px+dx
                    if (0 <= ny < mask.shape[0] and 0 <= nx < mask.shape[1]
                            and mask[ny, nx] and not seen[ny, nx]):
                        seen[ny, nx] = True
                        stack.append((ny, nx))
        yy, xx = np.asarray(points).T
        width, height = xx.max()-xx.min()+1, yy.max()-yy.min()+1
        sizes.append(len(points))
        aspects.append(max(width, height)/min(width, height))
    return {'count': len(sizes),
            'area_median': float(np.median(sizes)) if sizes else 0.,
            'area_p95': float(np.percentile(sizes, 95)) if sizes else 0.,
            'aspect_p95': float(np.percentile(aspects, 95)) if aspects else 0.}


def sequence(gl, deferred, material, matte, source, *, size, factor, frames,
             height, approach=False):
    fixtures = [DeferredFixture(gl, (size[0]*s, size[1]*s), -24, 0,
                                'opaque', camera_position=(0, height, 0))
                for s in (1, factor)]
    sources = (source, target_derivatives(source, factor))
    rigs = [make_rig(gl, material, f, fragment) for f, fragment in zip(fixtures, sources)]
    matte_rigs = [make_rig(gl, matte, f, fragment) for f, fragment in zip(fixtures, sources)]
    # A larger synthetic plane avoids fixture clipping near the distant horizon.
    for rig in (*rigs, *matte_rigs):
        rig.set_vertex(PLANE.replace('256.0', '4096.0'))
    for fixture in fixtures:
        illumination(gl, fixture, direction=(0, .27, -1))
    errors, references, outputs, gains, positions = [], [], [], [], []
    base = []
    for fixture, rig, base_rig in zip(fixtures, rigs, matte_rigs):
        set_camera(gl, fixture, (rig, base_rig), (0., height, 0.))
        base.append(np.asarray(draw(gl, base_rig, fixture, deferred))
                    .reshape(fixture.size[1], fixture.size[0], 3))
    base_reference = base[1].reshape(size[1], factor, size[0], factor, 3).mean(axis=(1, 3))
    for frame in range(frames):
        position = (frame*.005*height, height*(1-.015*frame) if approach else height, 0.)
        positions.append(position)
        images = []
        for fixture, rig in zip(fixtures, rigs):
            set_camera(gl, fixture, (rig,), position)
            image = np.asarray(draw(gl, rig, fixture, deferred))
            image = image.reshape(fixture.size[1], fixture.size[0], 3)
            assert np.isfinite(image).all() and image.min() >= 0
            images.append(image)
        reference = images[1].reshape(size[1], factor, size[0], factor, 3).mean(axis=(1, 3))
        output, reference_display = display(images[0]), display(reference)
        errors.append(luminance(output-reference_display))
        references.append(luminance(reference_display-display(base_reference)))
        outputs.append(output)
        gains.append(luminance(images[0]-base[0]))
    temporal = []
    for frame in range(1, frames):
        world = world_plane(size, positions[frame])
        pixels = project(size, positions[frame-1], world)
        previous, valid = reproject(errors[frame-1], pixels)
        temporal.append((errors[frame]-previous)[valid])
    errors, references, gains = map(np.asarray, (errors, references, gains))
    denominator = float(np.sqrt(np.mean(references**2)))
    temporal_rms = float(np.sqrt(np.mean(np.concatenate(temporal)**2)))
    # Do not reward a candidate for making the effect vanish.
    assert denominator > .0001 and float(gains.max()) > .03, ('missing glints', denominator, gains.max())
    threshold = .1
    result = {'reprojected_temporal_residual_rms': temporal_rms,
              'normalized_temporal_residual': temporal_rms/denominator,
              'spatial_error_rms': float(np.sqrt(np.mean(errors**2))),
              'reference_glint_rms': denominator,
              'reference_glint_mean': float(references.mean()),
              'peak_hdr_glint_excess': float(gains.max()),
              'bright_pixel_fraction': float(np.mean(gains > threshold)),
              'near_shape_threshold_hdr': threshold,
              'bright_components': components(gains[0] > threshold)}
    return result, outputs


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compare-ref', help='read-only historical shader comparison')
    parser.add_argument('--json', type=Path)
    parser.add_argument('--preview', type=Path, help='optional labelled animated GIF')
    parser.add_argument('--frames', type=int, default=8)
    parser.add_argument('--factor', type=int, default=4)
    parser.add_argument('--size', type=int, nargs=2, default=(160, 100))
    args = parser.parse_args()
    assert args.frames >= 2 and args.factor >= 2 and min(args.size) >= 32
    matte, _, _ = literal_table(colors=[(.45, .22, .07)]*26, roughness=.8)
    matte = edited_vec(matte, 0, 224, (1., 0., 0., 0.))
    material = edited_vec(matte, 0, 96, (.9, .15, 0., 0.))
    source = shader_source(TERRAIN/'render.fs.glsl')
    sources = [('current', source)]
    if args.compare_ref:
        sources.append((args.compare_ref, snapshot_source(TERRAIN/'render.fs.glsl', args.compare_ref)))
    # Centimetre-scale close-up resolves the old source texels at this small
    # viewport, exposing curved highlight shapes rather than distant aliases.
    scenarios = [('near_lateral', .04, False), ('far_lateral', 12., False),
                 ('approach', 3., True)]
    result = {'shader_sha256': hashlib.sha256(source.encode()).hexdigest(),
              'comparison_revision': (subprocess.check_output(
                  ['git', 'rev-parse', args.compare_ref], cwd=ROOT, text=True).strip()
                  if args.compare_ref else None),
              'target_size': args.size, 'supersample_factor': args.factor,
              'frames': args.frames,
              'method': 'HDR supersampling with target derivatives; world-reprojected errors normalized by reference glint RMS',
              'scope': 'synthetic plane, production material/deferred shaders, finite reference; not game capture or GTX timing',
              'cases': {}}
    previews = {}
    with ScreenGL() as gl:
        result['renderer'] = gl.renderer
        deferred = program(gl, '#define SSAO_ACTIVE 0')
        for name, fragment in sources:
            result['cases'][name] = {}
            for scenario, height, approach in scenarios:
                metrics, images = sequence(gl, deferred, material, matte, fragment,
                    size=tuple(args.size), factor=args.factor, frames=args.frames,
                    height=height, approach=approach)
                result['cases'][name][scenario] = metrics
                previews[name, scenario] = images
                print(name, scenario, json.dumps(metrics), flush=True)
        gl.check('production grain motion and close-up fixtures')
    if args.compare_ref:
        for scenario, _, _ in scenarios:
            current = result['cases']['current'][scenario]
            previous = result['cases'][args.compare_ref][scenario]
            assert current['reference_glint_rms'] >= previous['reference_glint_rms']*.5, (
                'comparison lost most visible grain contrast', scenario, current, previous)
    if args.json:
        args.json.parent.mkdir(parents=True, exist_ok=True)
        args.json.write_text(json.dumps(result, indent=2)+'\n')
    if args.preview:
        from PIL import Image, ImageDraw
        frames = []
        width, height = args.size
        for frame in range(args.frames):
            canvas = Image.new('RGB', (width*len(sources), (height+25)*len(scenarios)), (24, 26, 30))
            label = ImageDraw.Draw(canvas)
            for column, (name, _) in enumerate(sources):
                for row, (scenario, _, _) in enumerate(scenarios):
                    rgb = np.power(np.clip(previews[name, scenario][frame][::-1], 0, 1), 1/2.2)
                    origin = (column*width, row*(height+25))
                    canvas.paste(Image.fromarray(np.uint8(rgb*255)), (origin[0], origin[1]+25))
                    label.text((origin[0]+3, origin[1]+5), name+' '+scenario, fill='white')
            frames.append(canvas)
        args.preview.parent.mkdir(parents=True, exist_ok=True)
        frames[0].save(args.preview, save_all=True, append_images=frames[1:], duration=140, loop=0)
    print('PASS finite nonzero grain motion renders; metrics are diagnostic, not a universal stability guarantee', flush=True)


if __name__ == '__main__':
    main()
