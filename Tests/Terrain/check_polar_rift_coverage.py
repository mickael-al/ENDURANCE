#!/usr/bin/env python3
"""Check that PolarIce wall coverage contains the entire bounded 3D warp.

The GLSL probe samples the trunk and optional branches with the compact site
mask held at one. This is conservative: the actual frozen site mask can only
reduce the density change. It tests all corners/axes of the warp rectangle and
four irregular interior offsets, because the noisy contour is not monotonic.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path

import numpy as np

from biome_placement_probe import anchor_probe, placements
from check_biome_landforms import source_fingerprint
from check_biomes import BiomeProbe
from gl_headless import GL


SOURCE = r'''
layout(local_size_x=8,local_size_y=8) in;
struct ProbeSample { vec4 terrain; vec4 volume; vec4 samples; vec4 shape; };
layout(std430,binding=6) writeonly buffer ProbeOutput { ProbeSample outputValues[]; };
uniform uint uProbeSize;
uniform vec2 uProbeOrigin;
uniform float uProbeSpacing;
uniform float uProbeMinY;
void main() {
 uvec2 i=gl_GlobalInvocationID.xy;
 if(any(greaterThanEqual(i,uvec2(uProbeSize))))return;
 vec2 q=(vec2(i)/float(uProbeSize-1u)*2.4-1.2)*uProbeSpacing;
 LandSite s=LandSite(q,uProbeOrigin,uProbeSpacing,1.0,uProbeMinY);
 float original=polarRiftMask(s);
 float coverage=polarRiftWallCoverage(s);
 vec2 bound=polarRiftWarpLimit();
 float difference=0.;
 for(int x=-1;x<=1;++x)for(int z=-1;z<=1;++z) {
  LandSite shifted=s;shifted.q+=vec2(x,z)*bound;
  difference=max(difference,abs(polarRiftMask(shifted)-original));
 }
 for(int n=0;n<4;++n) {
  vec2 d=vec2(landPlacementValue(q*.13+float(n)*17.1,351u),
              landPlacementValue(q*.17+float(n)*29.2,352u))*2.-1.;
  LandSite shifted=s;shifted.q+=d*bound;
  difference=max(difference,abs(polarRiftMask(shifted)-original));
 }
 outputValues[i.x+i.y*uProbeSize]=ProbeSample(
  vec4(original,coverage,difference,float(polarRiftBranchCount(s))),
  vec4(q,uProbeSpacing,uProbeMinY),vec4(0),vec4(0));
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    fingerprint = source_fingerprint(0)
    size = 193
    records = []
    with GL() as gl:
        probe = BiomeProbe(gl, source=SOURCE)
        placement = next(p for p in placements(0) if p.variable == 'rift')
        for seed in (1337, 17):
            rows, _ = anchor_probe(gl, 0, placement, seed, size=6)
            assert len(rows) >= 12, ('Insufficient active rifts', seed, len(rows))
            # Independent nearby sites, including both coordinate signs and
            # different widths. The center is in the warped placement domain.
            chosen = sorted(rows[:12], key=lambda row: row[8])
            for row in chosen:
                output = probe.run(
                    biome=0, size=size, origin=tuple(row[10:12]),
                    spacing=float(row[2]), min_y=float(row[8]), uSeed=seed)
                data = np.asarray(output['values']).reshape(-1, 16)
                assert np.isfinite(data).all(), ('Non-finite coverage', seed, row[:2])
                changed = data[:, 2] > 1e-5
                assert changed.any(), ('Probe did not cross a rift wall', seed, row[:2])
                uncovered = changed & (data[:, 1] < .999)
                record = {
                    'seed': seed, 'center': row[10:12].tolist(),
                    'variant': float(row[8]), 'branches': int(data[0, 3]),
                    'changed_points': int(changed.sum()),
                    'uncovered_points': int(uncovered.sum()),
                    'minimum_coverage': float(data[changed, 1].min()),
                }
                records.append(record)
                assert not uncovered.any(), (
                    'Warped contour escapes full wall coverage', record,
                    data[uncovered][np.argsort(data[uncovered, 1])[:5]].tolist())
        assert source_fingerprint(0) == fingerprint, 'Sources changed during measurement'
        result = {
            'renderer': gl.renderer, 'source_sha256': fingerprint,
            'sites_measured': len(records), 'grid_size': size,
            'points_measured': size * size * len(records),
            'offsets_per_point': 13,
            'uncovered_points': sum(r['uncovered_points'] for r in records),
            'sites': records,
            'note': 'Actual GLSL, with compact site mask fixed at one. Tests the '
                    'bounded warp rectangle and irregular interior offsets. '
                    'This finite sampling complements the analytical coverage '
                    'bound; it is not an in-game performance benchmark.',
        }
        (args.output / 'polar_rift_coverage.json').write_text(
            json.dumps(result, indent=2) + '\n')
        print(json.dumps({k: result[k] for k in (
            'sites_measured', 'points_measured', 'offsets_per_point',
            'uncovered_points')}, indent=2), flush=True)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
