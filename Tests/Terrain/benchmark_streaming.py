#!/usr/bin/env python3
"""Compare terrain initial-fill mesh work and CPU planning against a Git revision.

Uses the real LOD selector and transaction planner, through complete convergence.
Checks final origins/LODs/transition masks byte for byte for 4-km and saved 40-km
scene settings. CPU planning time and redundant mesh job counts are measured;
this does not measure terrain GPU execution, rendering FPS, or disk loading.
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
SOURCE = Path('ANTAGONIZ/Source/Miscellaneous/ProceduralTerrainStreaming.cpp')
LOD = Path('ANTAGONIZ/Source/Miscellaneous/ProceduralTerrainLod.cpp')
FIXTURE = Path('Tests/Terrain/benchmark_streaming.cpp')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--reference-ref', required=True, help='Available earlier local Git revision')
    parser.add_argument('--runs', type=int, default=5)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    if not 1 <= args.runs <= 100:
        parser.error('runs must be 1..100')
    before = subprocess.check_output(['git', 'show', f'{args.reference_ref}:{SOURCE}'], cwd=ROOT)
    current = (ROOT / SOURCE).read_bytes()
    measurements = {'before': [], 'after': []}
    with tempfile.TemporaryDirectory(prefix='terrain-streaming-benchmark-') as directory:
        directory = Path(directory)
        old_source = directory / 'before.cpp'
        old_source.write_bytes(before)
        for name, source in [('before', old_source), ('after', ROOT / SOURCE)]:
            command = shlex.split(os.environ.get('CXX', 'c++')) + [
                '-std=c++17', '-O3', '-ffast-math',
                '-I', str(ROOT / 'ANTAGONIZ/Header/Miscellaneous'),
                '-I', str(ROOT / 'ThirdParty/glm'),
                str(ROOT / FIXTURE), str(source), str(ROOT / LOD),
                '-o', str(directory / name)]
            subprocess.run(command, check=True)
        expected = None
        for run in range(args.runs):
            for name in (['before', 'after'] if run % 2 == 0 else ['after', 'before']):
                snapshot = directory / f'{name}.bin'
                result = json.loads(subprocess.check_output([directory / name, snapshot], text=True))
                actual = snapshot.read_bytes()
                if expected is None:
                    expected = actual
                if actual != expected:
                    raise RuntimeError(f'{name} run {run} changed final coverage or transition masks')
                measurements[name].append(result)
        cases = []
        for case in range(2):
            result = {}
            for name, runs in measurements.items():
                values = [run[case] for run in runs]
                stable = [{key: value for key, value in row.items()
                           if key not in ('planning_ms', 'peak_planning_ms')} for row in values]
                if any(row != stable[0] for row in stable):
                    raise RuntimeError('Non-deterministic planned workload')
                result[name] = stable[0] | {
                    'planning_ms_runs': [row['planning_ms'] for row in values],
                    'planning_ms_median': statistics.median(row['planning_ms'] for row in values),
                    'peak_planning_ms_max': max(row['peak_planning_ms'] for row in values)}
            result['mesh_job_reduction_percent'] = 100 * (
                1 - result['after']['mesh_jobs'] / result['before']['mesh_jobs'])
            cases.append(result)
        report = {
            'scope': 'Complete production planner initial fill; CPU time and exact job counts, no GPU timing',
            'camera': [80, 180, 120],
            'reference_ref': args.reference_ref,
            'compiler': os.environ.get('CXX', 'c++'),
            'flags': '-std=c++17 -O3 -ffast-math',
            'source_before_sha256': hashlib.sha256(before).hexdigest(),
            'source_after_sha256': hashlib.sha256(current).hexdigest(),
            'shared_lod_sha256': hashlib.sha256((ROOT / LOD).read_bytes()).hexdigest(),
            'fixture_sha256': hashlib.sha256((ROOT / FIXTURE).read_bytes()).hexdigest(),
            'exact_final_snapshot_bytes': len(expected),
            'exact_final_snapshot_sha256': hashlib.sha256(expected).hexdigest(),
            'queue_timing_assumptions': {
                'target_hardware': 'GTX 1660 / 6 GB, not measured by this CPU benchmark',
                'saved_max_in_flight': 3,
                'single_poll_instant_single_stage_frames_min': 889,
                'single_poll_single_stage_seconds_at_60_40_fps': [889 / 60, 889 / 40],
                'single_poll_count_emit_frames_approx': 1778,
                'single_poll_count_emit_seconds_at_60_40_fps': [1778 / 60, 1778 / 40],
                'limitation': 'Ideal 2665-job frame-cadence bounds before CPU planning, publication and GPU time. '
                              'The bounded second poll can advance count/emission and free empty slots sooner; '
                              'queue depth and real throughput still require target-GPU measurements.'},
            'cases': cases}
    formatted = json.dumps(report, indent=2) + '\n'
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(formatted)
    print(formatted, end='')


if __name__ == '__main__':
    main()
