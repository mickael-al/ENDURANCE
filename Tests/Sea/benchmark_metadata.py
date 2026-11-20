#!/usr/bin/env python3
"""Compare only PrepareVolume against a Git revision using real compute shaders.

Wall time ends after glFinish; waits are benchmark instrumentation only. The
synthetic inputs disable 3D bands to isolate the metadata/reduction stage. This
does not measure full chunk generation, rendering contention or hardware FPS.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import posixpath
import re
import statistics
import struct
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
SHADER = "Asset/Shader/ProceduralTerrain/field_batch_prepare.C.glsl"
sys.path.insert(0, str(ROOT / "Tests/Terrain"))
from gl_headless import GL, shader_source
from field_batch_runner import default_uniforms
from biome_test_support import packed_biome_cells


def baseline_source(ref):
    commit = subprocess.check_output(
        ["git", "rev-parse", ref + "^{commit}"], cwd=ROOT, text=True).strip()
    cache = {}

    def expand(path, stack=()):
        if path in stack:
            raise RuntimeError("Recursive baseline include: " + path)
        if path not in cache:
            data = subprocess.check_output(["git", "show", commit + ":" + path], cwd=ROOT)
            try:
                cache[path] = data.decode("utf-8").replace("\r\n", "\n")
            except UnicodeDecodeError:
                cache[path] = data.decode("cp1252").replace("\r\n", "\n")
        return re.sub(r'^\s*#include\s+"([^"]+)"\s*$',
            lambda m: expand(posixpath.normpath(posixpath.join(
                posixpath.dirname(path), m.group(1))), stack + (path,)),
            cache[path], flags=re.MULTILINE)

    return commit, expand(SHADER)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline-ref", default="4864129")
    parser.add_argument("--cells", type=int, choices=(4, 8, 16, 32, 64), default=64)
    parser.add_argument("--jobs", type=int, choices=range(1, 17), default=16)
    parser.add_argument("--rounds", type=int, default=11)
    parser.add_argument("--repetitions", type=int, default=32)
    parser.add_argument("--warmup", type=int, default=10)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    if args.rounds < 4 or args.repetitions < 1 or args.warmup < 1:
        parser.error("Use at least four rounds, one repetition and one warmup dispatch.")
    os.environ.setdefault("LP_NUM_THREADS", "1")
    commit, before = baseline_source(args.baseline_ref)
    after = shader_source(ROOT / SHADER)
    report = {
        "scope": "Isolated PrepareVolume dispatch; synthetic 3D-inactive columns, no rendering",
        "limitation": "Software timings do not predict hardware GPU or whole-generation performance.",
        "clock": "completed wall time (perf_counter plus glFinish), excluding compilation/warmup",
        "baseline_commit": commit,
        "before_source_sha256": hashlib.sha256(before.encode()).hexdigest(),
        "after_source_sha256": hashlib.sha256(after.encode()).hexdigest(),
        "cells": args.cells, "jobs": args.jobs, "rounds": args.rounds,
        "repetitions_per_round": args.repetitions, "warmup_dispatches": args.warmup,
        "lp_num_threads": os.environ["LP_NUM_THREADS"], "results": [],
    }
    with GL() as gl:
        report.update(renderer=gl.renderer, opengl=gl.version)
        programs = {"old": gl.compile([(0x91B9, before)], "baseline PrepareVolume"),
                    "new": gl.compile([(0x91B9, after)], "candidate PrepareVolume")}
        side = args.cells + 3
        groups = (side + 7) // 8
        stride = side * side
        state_stride = 1 + groups * groups
        columns = gl.buffer(stride * args.jobs * 32, 1)
        states = gl.buffer(state_stride * args.jobs * 16, 3)
        gl.buffer(b"".join(struct.pack("<4i8I", 0, j * args.cells, 0, 0,
            1337, 5, 0, 0, j, 0, j, 0) for j in range(args.jobs)), 6)
        gl.buffer(packed_biome_cells(), 7)
        uniforms = default_uniforms(args.cells, uDetailScale=0, uHeightAmplitude=0)
        uniforms.update(uJobSlots=("1ui", args.jobs), uColumnStride=("1ui", stride),
                        uStateStride=("1ui", state_stride))
        for program in programs.values():
            for name, (kind, *values) in uniforms.items():
                gl.uniform(program, name, kind, *values)
        for label, altitude in (("land", 1.0), ("mixed", 0.5)):
            record = struct.pack("<5f3I", -64.0, 0.123, 0.456, altitude, 0.0, 0, 0, 0)
            gl.write(columns, record * (stride * args.jobs))
            gl.write(states, bytes(state_stride * args.jobs * 16))
            for program in programs.values():
                for _ in range(args.warmup):
                    gl.dispatch(program, groups, groups, args.jobs)
                gl.glFinish()
            rounds = []
            for index in range(args.rounds):
                pair = {}
                for name in (("old", "new") if index % 2 == 0 else ("new", "old")):
                    gl.glFinish()
                    start = time.perf_counter()
                    for _ in range(args.repetitions):
                        gl.dispatch(programs[name], groups, groups, args.jobs)
                    gl.glFinish()
                    pair[name] = (time.perf_counter() - start) * 1000 / args.repetitions
                pair["ratio"] = pair["new"] / pair["old"]
                rounds.append(pair)
            ratios = [r["ratio"] for r in rounds]
            report["results"].append({"case": label,
                "dispatch_ms_before": statistics.median(r["old"] for r in rounds),
                "dispatch_ms_after": statistics.median(r["new"] for r in rounds),
                "dispatch_ms_before_iqr": statistics.quantiles([r["old"] for r in rounds], n=4)[::2],
                "dispatch_ms_after_iqr": statistics.quantiles([r["new"] for r in rounds], n=4)[::2],
                "paired_ratio_median": statistics.median(ratios),
                "ratio_iqr": statistics.quantiles(ratios, n=4)[::2], "rounds": rounds})
        gl.check("PrepareVolume benchmark")
    payload = json.dumps(report, indent=2) + "\n"
    if args.output:
        args.output.write_text(payload)
    else:
        print(payload, end="")


if __name__ == "__main__":
    main()
