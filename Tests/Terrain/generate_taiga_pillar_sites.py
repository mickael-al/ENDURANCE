#!/usr/bin/env python3
"""Reproduce or verify Taiga's periodic blue-noise placement table.

Default: verify the packed table embedded in pillar_sites.glsl, including torus
seams, bin indices and angular-order diagnostics. --regenerate repeats the exact
seeded generation and requires byte-identical packed arrays. --emit-arrays prints
the regenerated GLSL arrays without editing the shader. Requires NumPy and SciPy.

The POSITION pattern repeats every 48 km. The shader rotates/translates it per
world seed and hashes tile coordinates into the independent pillar attributes.
"""
from pathlib import Path
import argparse
import json
import re

import numpy as np
from scipy.spatial import cKDTree

ROOT = Path(__file__).resolve().parents[2]
HEADER = ROOT / 'Asset/Shader/ProceduralTerrain/biomes/Taiga/pillar_sites.glsl'
TILE_SIZE = 48000.0
EXCLUSION = 1035.0
SEED = 500122
BIN_SIZE = 1600.0
BINS_PER_SIDE = 30
PACK_SCALE = 65536
POINT_ARRAY = 'TAIGA_PILLAR_POINTS'
BIN_ARRAY = 'TAIGA_PILLAR_BINS'


def generate():
    """Periodic Bridson sampling, then repair excessively isolated points."""
    rng = np.random.Generator(np.random.PCG64(SEED))
    cells = 64
    cell_size = TILE_SIZE / cells
    grid = [[[] for _ in range(cells)] for _ in range(cells)]
    points, active = [], []

    def insert(point):
        index = len(points)
        points.append(point)
        active.append(index)
        cell = np.floor(point / cell_size).astype(int)
        grid[cell[1]][cell[0]].append(index)

    def admissible(point):
        cell = np.floor(point / cell_size).astype(int)
        for dz in range(-2, 3):
            for dx in range(-2, 3):
                for index in grid[(cell[1] + dz) % cells][(cell[0] + dx) % cells]:
                    distance = np.abs(points[index] - point)
                    distance = np.minimum(distance, TILE_SIZE - distance)
                    if distance @ distance < EXCLUSION * EXCLUSION:
                        return False
        return True

    insert(rng.random(2) * TILE_SIZE)
    while active:
        active_index = int(rng.integers(len(active)))
        origin = points[active[active_index]]
        found = False
        for _ in range(30):
            angle = rng.random() * 2 * np.pi
            radius = EXCLUSION * np.sqrt(1 + 3 * rng.random())
            point = (origin + radius * np.array([np.cos(angle), np.sin(angle)])) % TILE_SIZE
            if admissible(point):
                insert(point)
                found = True
                break
        if not found:
            active[active_index] = active[-1]
            active.pop()

    points = np.asarray(points)
    for repairs in range(1000):
        distance, neighbor = cKDTree(points, boxsize=TILE_SIZE).query(points, k=2)
        isolated = np.flatnonzero(distance[:, 1] > 1450.0)
        if not len(isolated):
            break
        index = isolated[np.argmax(distance[isolated, 1])]
        other = neighbor[index, 1]
        delta = (points[other] - points[index] + TILE_SIZE / 2) % TILE_SIZE - TILE_SIZE / 2
        # Move toward the nearest neighbor until 1448 m remain. Every other
        # distance stays >=1448 m by the triangle inequality, so this repair
        # cannot violate the 1035-m exclusion. Quantization is checked below.
        points[index] = (points[index] + delta * (1 - 1448.0 / np.linalg.norm(delta))) % TILE_SIZE
    else:
        raise AssertionError('Isolated-point repair did not converge')

    quantized = np.round(points / TILE_SIZE * PACK_SCALE).astype(np.uint32) & 65535
    points = quantized.astype(float) * (TILE_SIZE / PACK_SCALE)
    cell = np.floor(points / BIN_SIZE).astype(int)
    bin_id = cell[:, 0] + BINS_PER_SIDE * cell[:, 1]
    order = np.argsort(bin_id, kind='stable')
    quantized, bin_id = quantized[order], bin_id[order]
    offsets = np.r_[0, np.cumsum(np.bincount(bin_id, minlength=BINS_PER_SIDE**2))]
    packed_points = quantized[:, 0] | (quantized[:, 1] << np.uint32(16))
    packed_bins = offsets[:-1].astype(np.uint32) | (np.diff(offsets).astype(np.uint32) << 16)
    return packed_points, packed_bins, repairs


def read_array(source, name):
    pattern = rf'const uint {name}\[(\d+)\]\s*=\s*uint\[\d+\]\((.*?)\);'
    match = re.search(pattern, source, flags=re.S)
    assert match, f'Missing GLSL array {name}'
    values = np.array([int(value, 16) for value in re.findall(r'0x([0-9a-fA-F]+)u', match[2])], dtype=np.uint32)
    assert len(values) == int(match[1]), f'Wrong entry count in {name}'
    return values


def verify(packed_points, packed_bins):
    assert len(packed_bins) == BINS_PER_SIDE**2
    points = np.column_stack((packed_points & 65535, packed_points >> 16)).astype(float)
    points *= TILE_SIZE / PACK_SCALE
    starts, counts = packed_bins & 65535, packed_bins >> 16
    assert int(counts.sum()) == len(points)
    assert np.array_equal(starts, np.r_[0, np.cumsum(counts[:-1])])
    cells = np.floor(points / BIN_SIZE).astype(int)
    bin_ids = cells[:, 0] + BINS_PER_SIDE * cells[:, 1]
    assert np.array_equal(bin_ids, np.repeat(np.arange(len(packed_bins)), counts.astype(int)))

    distance, neighbor = cKDTree(points, boxsize=TILE_SIZE).query(points, k=2)
    nearest = distance[:, 1]
    assert nearest.min() >= 1000.0, 'Packed foot spacing falls below 1000 m'
    assert nearest.max() <= 1500.0, 'Packed nearest-neighbor spacing exceeds 1500 m'
    delta = (points[neighbor[:, 1]] - points + TILE_SIZE / 2) % TILE_SIZE - TILE_SIZE / 2
    angle = np.arctan2(delta[:, 1], delta[:, 0])
    angular_order = {str(k): float(abs(np.mean(np.exp(1j * k * angle)))) for k in (2, 4, 6, 8)}
    # Perfect square/triangular rows yield a large fourth/sixth angular order.
    # This checks directional ordering; it does not erase the documented period.
    assert angular_order['4'] < .10 and angular_order['6'] < .10
    return dict(points=len(points),position_period_m=TILE_SIZE,quantization_m=TILE_SIZE / PACK_SCALE,
                nearest_min_m=float(nearest.min()),nearest_max_m=float(nearest.max()),
                nearest_mean_m=float(nearest.mean()),maximum_bin_count=int(counts.max()),
                nearest_bond_angular_order=angular_order)


def emit_array(name, values):
    print(f'const uint {name}[{len(values)}] = uint[{len(values)}](')
    rows = ['    ' + ', '.join(f'0x{int(v):08x}u' for v in values[i:i+8]) for i in range(0, len(values), 8)]
    print(',\n'.join(rows))
    print(');')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--regenerate', action='store_true', help='Repeat seeded generation and compare exact packed arrays')
    parser.add_argument('--emit-arrays', action='store_true', help='Regenerate and print GLSL arrays; never edit the shader')
    args = parser.parse_args()
    source = HEADER.read_text()
    embedded_points, embedded_bins = read_array(source, POINT_ARRAY), read_array(source, BIN_ARRAY)
    metrics = verify(embedded_points, embedded_bins)
    if args.regenerate or args.emit_arrays:
        generated_points, generated_bins, repairs = generate()
        verify(generated_points, generated_bins)
        assert np.array_equal(generated_points, embedded_points), 'Regenerated point table differs from shader'
        assert np.array_equal(generated_bins, embedded_bins), 'Regenerated bin index differs from shader'
        metrics.update(generator_seed=SEED,repair_iterations=repairs,regenerated_arrays_identical=True)
        if args.emit_arrays:
            emit_array(POINT_ARRAY, generated_points)
            emit_array(BIN_ARRAY, generated_bins)
            return
    print(json.dumps(metrics, indent=2))


if __name__ == '__main__':
    main()
