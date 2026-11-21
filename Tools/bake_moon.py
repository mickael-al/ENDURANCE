#!/usr/bin/env python3
"""Bake ENDURANCE's fictional moon. Requires numpy and Pillow.

The runtime asset is linear RGBA8: RGB object-space normal, A albedo.
Header: EMOON001, little-endian uint32 width/height, float32 mean albedo.
No lighting or phase is baked. Coordinates are spherical, with repeat in U.
"""
from pathlib import Path
import argparse
import math
import struct
import numpy as np
from PIL import Image


def smooth(a, b, x):
    t = np.clip((x-a)/(b-a), 0, 1)
    return t*t*(3-2*t)


def simplex3(p, permutation):
    """Seeded 3D simplex gradient noise, four tetrahedral corners per sample.

    This is an offline NumPy implementation, not a runtime shader dependency.
    Evaluate in Cartesian sphere coordinates so the atlas has no UV seam.
    """
    skew = p.sum(axis=-1, keepdims=True)/3.0
    cell = np.floor(p+skew).astype(np.int32)
    q = p-cell+cell.sum(axis=-1, keepdims=True)/6.0
    x, y, z = np.moveaxis(q, -1, 0)
    rank = np.stack(((x>=y).astype(np.int8)+(x>=z),
                     (y>x).astype(np.int8)+(y>=z),
                     (z>x).astype(np.int8)+(z>y)), axis=-1)
    gradients = np.array(((1,1,0),(-1,1,0),(1,-1,0),(-1,-1,0),
                          (1,0,1),(-1,0,1),(1,0,-1),(-1,0,-1),
                          (0,1,1),(0,-1,1),(0,1,-1),(0,-1,-1)), np.float32)
    result = np.zeros(p.shape[:-1], np.float32)
    for corner in range(4):
        offset = (rank>=3-corner).astype(np.int32)
        delta = q-offset+corner/6.0
        ids = (cell+offset)&255
        gradient = permutation[ids[...,0]+permutation[ids[...,1]+permutation[ids[...,2]]]]%12
        support = np.maximum(.6-(delta*delta).sum(axis=-1), 0.0)
        result += (support**4*(gradients[gradient]*delta).sum(axis=-1)).astype(np.float32)
    return result*32.0


def fbm3(p, permutation, octaves=8, lacunarity=2.03, gain=.52, footprint=0.):
    """Normalized fBm; fade octaves which cannot be resolved by the baked texel."""
    result = np.zeros(p.shape[:-1], np.float32)
    amplitude = frequency = 1.0
    total = 0.0
    for octave in range(octaves):
        if octave:
            p = p*lacunarity
            frequency *= lacunarity
            amplitude *= gain
        weight = amplitude*(1.0-smooth(.25, .5, footprint*frequency))
        if weight<=0.: break
        total += weight
        result += weight*simplex3(p, permutation)
    return result/max(total, 1e-8)


def lunar_fields(n, seed, texel_angle):
    """Two independent spherical fBms: continental deposits and fine mottling.

    Fixed row batches bound temporary tetrahedral arrays even at 4096 pixels.
    A separate RNG keeps the noise independent of crater count and placement.
    """
    rng = np.random.default_rng(seed ^ 0x4D4F4F4E)
    permutation = np.tile(rng.permutation(256), 2)
    deposits = np.empty(n.shape[:-1], np.float32)
    detail = np.empty_like(deposits)
    offsets = rng.uniform(-50, 50, (5,3)).astype(np.float32)
    macro_scale, detail_scale = 2.4, 18.0
    for row in range(0, n.shape[0], 32):
        p = n[row:row+32]
        warp = np.stack([simplex3(p*.85+offset, permutation) for offset in offsets[:3]], axis=-1)
        field = fbm3((p+.18*warp)*macro_scale+offsets[3], permutation,
                     footprint=texel_angle*macro_scale)
        fine = fbm3(p*detail_scale+offsets[4], permutation, gain=.60,
                    footprint=texel_angle*detail_scale)
        # Soft transitions with broad, dark interiors: detail must not erase
        # the distinction between basalt plains and cratered highlands.
        deposits[row:row+32] = smooth(-.09, .11, field+.05*fine)
        detail[row:row+32] = fine
    return deposits, detail


def bake(width, seed):
    rng = np.random.default_rng(seed)
    height = width//2
    lon = ((np.arange(width, dtype=np.float32)+.5)/width-.5)*2*np.pi
    lat = ((np.arange(height, dtype=np.float32)+.5)/height-.5)*np.pi
    cl, sl = np.cos(lat)[:, None], np.sin(lat)[:, None]
    n = np.empty((height, width, 3), np.float32)
    n[..., 0] = cl*np.cos(lon)
    n[..., 1] = sl
    n[..., 2] = cl*np.sin(lon)
    texel_angle = 2*np.pi/width
    maria, detail = lunar_fields(n, seed, texel_angle)
    mineral = np.clip(.5+2.4*detail, 0, 1)
    # Dark plains still contain detail; bright highlands are not a flat tint.
    highland = .28+.18*mineral
    basalt = .055+.035*mineral
    albedo = (highland*(1-maria)+basalt*maria).astype(np.float32)
    terrain = (-.0018*maria+.00018*detail).astype(np.float32)
    # Mostly small old impacts; a few larger young ones have bright ejecta.
    # All marks are material/relief, not baked sunlight or ambient occlusion.
    for impact in range(1800):
        latitude = math.asin(rng.uniform(-1, 1)); longitude = rng.uniform(-np.pi, np.pi)
        radius = float(np.exp(rng.uniform(math.log(.004), math.log(.085))))
        ray_count = int(rng.integers(11, 29))
        phase = rng.uniform(-np.pi, np.pi)
        young = impact<24
        if young: radius = rng.uniform(.015, .033)
        extent = radius*(13.0 if young else 1.65)
        rows = np.where(np.abs(lat-latitude) < extent)[0]
        if not len(rows): continue
        a, b = int(rows[0]), int(rows[-1])+1
        center = np.array([math.cos(latitude)*math.cos(longitude), math.sin(latitude),
                           math.cos(latitude)*math.sin(longitude)], np.float32)
        # Chord distance remains well-conditioned for the smallest craters.
        delta = n[a:b]-center
        r = np.sqrt((delta*delta).sum(axis=-1))/(2*math.sin(radius*.5))
        r *= 1.0+.10*detail[a:b]
        depth = radius*rng.uniform(.025, .055)
        floor = -depth*(1-smooth(.16, 1.0, r))
        rim = depth*.30*np.exp(-((r-1.0)/.13)**2)
        terrain[a:b] += (floor+rim)*(1-.65*maria[a:b])
        rim_color = np.exp(-((r-1.04)/.22)**2)
        floor_color = 1-smooth(.45, .95, r)
        albedo[a:b] += (.14 if young else .055)*rim_color - .025*floor_color
        if young:
            east = np.array([-math.sin(longitude),0,math.cos(longitude)], np.float32)
            north = np.cross(center,east)
            angle = np.arctan2(n[a:b] @ north, n[a:b] @ east)
            rays = np.maximum(np.sin(angle*ray_count+phase+detail[a:b]*3),0)**14
            rays *= smooth(1.0, 1.8, r)*(1-smooth(3.0, 13.0, r))*np.exp(-r/5)
            albedo[a:b] += .26*rays + .05*np.exp(-((r-1.3)/.60)**2)
    # Keep the whole sphere's reflected energy near the previous asset; change
    # local contrast rather than brightening the entire sky/IBL configuration.
    albedo = np.clip(albedo, .035, .60)
    albedo *= .2084/float(np.mean(albedo))
    albedo = np.clip(albedo, .035, .60)
    # Height derivatives in the local spherical tangent frame; renormalize
    # object-space normals after filtering at runtime, including polar mips.
    du = (np.roll(terrain, -1, 1)-np.roll(terrain, 1, 1))/(4*np.pi/width)
    dv = np.gradient(terrain, np.pi/height, axis=0)
    east = np.stack((-np.sin(lon), np.zeros(width), np.cos(lon)), axis=-1)
    north = np.empty_like(n)
    north[..., 0] = -sl*np.cos(lon); north[..., 1] = cl; north[..., 2] = -sl*np.sin(lon)
    # Fade unresolved longitude derivatives at the poles.
    normal = n - east[None]*((du/np.maximum(cl, .03))*smooth(.015, .06, cl))[..., None] - north*dv[..., None]
    normal /= np.linalg.norm(normal, axis=-1, keepdims=True)
    pixels = np.empty((height, width, 4), np.uint8)
    pixels[..., :3] = np.rint(np.clip(normal*.5+.5, 0, 1)*255)
    pixels[..., 3] = np.rint(albedo*255)
    return pixels, float(pixels[..., 3].mean()/255)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--width', type=int, default=2048)
    parser.add_argument('--seed', type=int, default=68291)
    parser.add_argument('--output', type=Path, default=Path(__file__).resolve().parents[1]/
                        'ENDURANCE/Ressources/Atmosphere/moon.surface')
    parser.add_argument('--preview', type=Path)
    args = parser.parse_args()
    if args.width < 64 or args.width > 4096 or args.width & (args.width-1):
        parser.error('width must be a power of two between 64 and 4096')
    pixels, mean = bake(args.width, args.seed)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(b'EMOON001'+struct.pack('<IIf', args.width, args.width//2, mean)+pixels.tobytes())
    if args.preview:
        args.preview.parent.mkdir(parents=True, exist_ok=True)
        Image.fromarray(pixels[..., 3], 'L').transpose(Image.Transpose.FLIP_TOP_BOTTOM).save(args.preview)
    print(f'{args.output}: {args.width}x{args.width//2}, mean albedo {mean:.6f}')


if __name__ == '__main__':
    main()
