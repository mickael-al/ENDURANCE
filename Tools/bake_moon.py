#!/usr/bin/env python3
"""Bake ENDURANCE's fictional moon. Requires numpy, scipy and Pillow.

The runtime asset is linear RGBA8: RGB object-space normal, A albedo.
Header: EMOON001, little-endian uint32 width/height, float32 mean albedo.
No lighting or phase is baked. Coordinates are spherical, with repeat in U.
"""
from pathlib import Path
import argparse
import math
import struct
import numpy as np
from scipy.ndimage import map_coordinates
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


def fbm3(p, permutation, octaves=5, lacunarity=2.03, gain=.48):
    """Zero-centred, amplitude-normalized simplex fBm."""
    result = np.zeros(p.shape[:-1], np.float32)
    amplitude = total = 1.0
    for octave in range(octaves):
        if octave:
            p = p*lacunarity
            amplitude *= gain
            total += amplitude
        result += amplitude*simplex3(p, permutation)
    return result/total


def lunar_maria(n, seed):
    """Broad distorted dark deposits with quiet fine detail, baked to albedo.

    Fixed row batches bound temporary tetrahedral arrays even at 4096 pixels.
    A separate RNG preserves the existing crater/normal generation exactly.
    """
    rng = np.random.default_rng(seed ^ 0x4D4F4F4E)
    permutation = np.tile(rng.permutation(256), 2)
    result = np.empty(n.shape[:-1], np.float32)
    offsets = rng.uniform(-50, 50, (4,3)).astype(np.float32)
    for row in range(0, n.shape[0], 32):
        p = n[row:row+32]
        warp = np.stack([simplex3(p*.85+offset, permutation) for offset in offsets[:3]], axis=-1)
        field = fbm3((p+.18*warp)*2.4+offsets[3], permutation)
        # This soft contrast defines deposits, not a binary black/white speckle.
        result[row:row+32] = smooth(-.05, .22, field)
    return result


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
    grid = rng.random((32, 32, 32), dtype=np.float32)
    def noise(frequency):
        return map_coordinates(grid, np.moveaxis(n*frequency+15, -1, 0),
                               order=1, mode='grid-wrap', prefilter=False)*2-1
    broad, medium, fine = noise(3), noise(11), noise(38)
    terrain = (.0012*broad + .00035*medium + .00008*fine).astype(np.float32)
    maria = np.zeros((height, width), np.float32)
    # A small number of overlapping, irregular basins define the visual identity.
    for _ in range(13):
        center = rng.normal(size=3); center /= np.linalg.norm(center)
        radius = rng.uniform(.18, .50)
        distance = np.arccos(np.clip(n @ center, -1, 1))
        shape = 1-smooth(radius*.60, radius, distance+.055*broad+.018*medium)
        maria = np.maximum(maria, shape)
    terrain -= maria*.003
    # Latitude culling keeps the offline crater pass proportional to footprint.
    for _ in range(420):
        latitude = math.asin(rng.uniform(-1, 1)); longitude = rng.uniform(-np.pi, np.pi)
        radius = float(np.exp(rng.uniform(math.log(.008), math.log(.16))))
        rows = np.where(np.abs(lat-latitude) < radius*1.30)[0]
        if not len(rows): continue
        a, b = int(rows[0]), int(rows[-1])+1
        center = np.array([math.cos(latitude)*math.cos(longitude), math.sin(latitude),
                           math.cos(latitude)*math.sin(longitude)], np.float32)
        r = np.arccos(np.clip(n[a:b] @ center, -1, 1))/radius
        depth = radius*rng.uniform(.055, .095)
        floor = -depth*(1-smooth(.16, 1.0, r))
        rim = depth*.30*np.exp(-((r-1.0)/.085)**2)
        terrain[a:b] += (floor+rim)*(1-.65*maria[a:b])
    deposits = lunar_maria(n, seed)
    # Neutral material shades remain independent of solar lighting and phase.
    # Existing basins still influence deposition very gently; simplex fBm now
    # controls the broad irregular shapes instead of the circular basin masks.
    albedo = np.clip(.275-.175*deposits-.018*maria+.008*broad+.004*medium+.002*fine, .08, .36)
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
