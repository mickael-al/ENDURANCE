#!/usr/bin/env python3
"""Compare JSON reports from the identical before/after terrain benchmark."""
import argparse
import json
import math
from pathlib import Path
import statistics


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("before", type=Path)
    parser.add_argument("after", type=Path)
    args = parser.parse_args()
    before, after = (json.loads(path.read_text()) for path in (args.before, args.after))
    for name in ("renderer", "vendor", "opengl", "cells", "jobs", "biome", "iterations",
                 "base_height", "height_amplitude", "detail_scale"):
        if name not in before or before[name] != after.get(name):
            raise ValueError(f"Reports use different/missing {name}: {before.get(name)} / {after.get(name)}")
    print(f"GPU: {after['renderer']} | N={after['cells']} | jobs={after['jobs']} | biome={after['biome']}")
    if any(name in after["renderer"].lower() for name in ("llvmpipe", "softpipe", "software")):
        print("Software renderer: these results validate the benchmark, not hardware GPU performance.")
    print("| Phase | Before median (ms) | After median (ms) | Time change | Vertices before/after |")
    print("| --- | ---: | ---: | ---: | ---: |")
    phases = list(dict.fromkeys(item["name"] for item in before["phases"]))
    if set(phases) != {item["name"] for item in after["phases"]}:
        raise ValueError("Reports contain different phases")
    for name in phases:
        left = [item for item in before["phases"] if item["name"] == name]
        right = [item for item in after["phases"] if item["name"] == name]
        if [(item["iteration"], item["requests"]) for item in left] != [(item["iteration"], item["requests"]) for item in right]:
            raise ValueError(f"Different workload/iterations in {name}")
        values = [item["wall_ms"] for item in left + right]
        if any(not math.isfinite(value) or value <= 0 for value in values):
            raise ValueError(f"Invalid wall time in {name}")
        a, b = statistics.median(item["wall_ms"] for item in left), statistics.median(item["wall_ms"] for item in right)
        vertices_a = sum(item["vertices"] for item in left)
        vertices_b = sum(item["vertices"] for item in right)
        print(f"| {name} | {a:.3f} | {b:.3f} | {(b/a-1)*100:+.1f}% | {vertices_a}/{vertices_b} |")
        if [(item["ready"], item["empty"]) for item in left] != [(item["ready"], item["empty"]) for item in right]:
            print(f"  {name}: ready/empty classifications differ; inspect geometry before interpreting speed.")
    print("Negative time change means faster. Generation excludes shader initialization and exact-workload warmup.")
    print("Vertex totals may differ slightly after control quantization; compare geometry as well as elapsed time.")


if __name__ == "__main__":
    main()
