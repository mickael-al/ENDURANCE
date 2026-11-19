#!/usr/bin/env python3
"""Compare the actual LOD selector with an earlier source using identical headers.

Only CPU selection is timed, not terrain generation or GPU work. The driver
alternates before/after order and checks all ordered origins/LODs/transition
masks byte for byte. Requires a C++17 compiler and bundled GLM, no OpenGL.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shlex
import statistics
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
SOURCE = Path('ANTAGONIZ/Source/Miscellaneous/ProceduralTerrainLod.cpp')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    reference = parser.add_mutually_exclusive_group(required=True)
    reference.add_argument('--reference-source', type=Path)
    reference.add_argument('--reference-ref', help='Available local git revision of the old selector')
    parser.add_argument('--runs', type=int, default=5)
    parser.add_argument('--iterations', type=int, default=8)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    if not 1 <= args.runs <= 100 or not 1 <= args.iterations <= 10000:
        parser.error('runs must be 1..100 and iterations 1..10000')
    before = (args.reference_source.read_bytes() if args.reference_source else
              subprocess.check_output(['git', 'show', f'{args.reference_ref}:{SOURCE}'], cwd=ROOT))
    current = (ROOT / SOURCE).read_bytes()
    times = {'before': [], 'after': []}
    with tempfile.TemporaryDirectory(prefix='terrain-lod-benchmark-') as directory:
        directory = Path(directory)
        old_source = directory / 'before.cpp'
        old_source.write_bytes(before)
        for name, source in [('before', old_source), ('after', ROOT / SOURCE)]:
            command = shlex.split(os.environ.get('CXX', 'c++')) + [
                '-std=c++17', '-O3', '-ffast-math',
                '-I', str(ROOT / 'ANTAGONIZ/Header/Miscellaneous'),
                '-I', str(ROOT / 'ThirdParty/glm'),
                str(ROOT / 'Tests/Terrain/benchmark_lod.cpp'), str(source),
                '-o', str(directory / name)]
            subprocess.run(command, check=True)
        expected = None
        for run in range(args.runs):
            order = ['before', 'after'] if run % 2 == 0 else ['after', 'before']
            for name in order:
                path = directory / f'{name}.bin'
                result = json.loads(subprocess.check_output(
                    [directory / name, str(args.iterations), path], text=True))
                actual = path.read_bytes()
                if expected is None:
                    expected = actual
                if actual != expected:
                    raise RuntimeError(f'{name} run {run} changed the ordered LOD output')
                times[name].append(result['milliseconds'])
        medians = {name: statistics.median(values) for name, values in times.items()}
        report = {
            'fixture': 'Six cameras, 4096/40096-m radii, current shared field bounds',
            'compiler': os.environ.get('CXX', 'c++'),
            'flags': '-std=c++17 -O3 -ffast-math',
            'reference_ref': args.reference_ref,
            'source_before_sha256': hashlib.sha256(before).hexdigest(),
            'source_after_sha256': hashlib.sha256(current).hexdigest(),
            'selections_per_run': result['selections'],
            'selected_leaves_per_run': result['leaves'],
            'runs_ms': times,
            'median_ms': medians,
            'selection_cpu_time_reduction_percent': 100 * (1 - medians['after'] / medians['before']),
            'exact_snapshot_bytes': len(expected),
            'exact_snapshot_sha256': hashlib.sha256(expected).hexdigest(),
            'limitation': 'CPU selector timing on this host; not full generation or game FPS.'
        }
    formatted = json.dumps(report, indent=2) + '\n'
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(formatted)
    print(formatted, end='')


if __name__ == '__main__':
    main()
