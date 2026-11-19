#!/usr/bin/env python3
"""Print the Taiga Perlin palette's 70th-percentile thresholds.

Run from any directory with Python, NumPy, SciPy, and desktop OpenGL/EGL:
    python Tests/Terrain/calibrate_taiga_pillar_porosity.py

The default uses 1,048,576 deterministic, uniformly distributed volume samples
per normalized cylinder. This is offline calibration, never terrain runtime
work. It prints replacement values; it does not edit the production shader.
"""

from __future__ import annotations

import argparse
from pathlib import Path
import re

import numpy as np
from scipy.stats import qmc

from gl_headless import GL, shader_source


ROOT = Path(__file__).resolve().parents[2]
HELPER = ROOT / "Asset/Shader/ProceduralTerrain/biomes/Taiga/pillar_porosity.glsl"


def cylinder_samples(power: int) -> np.ndarray:
    unit = qmc.Sobol(3, scramble=True, seed=100).random_base2(power)
    radius = np.sqrt(unit[:, 0])  # Uniform area, not uniform radius.
    angle = 2.0 * np.pi * unit[:, 1]
    points = np.zeros((len(unit), 4), dtype=np.float32)
    points[:, 0] = radius * np.cos(angle)
    points[:, 1] = unit[:, 2]
    points[:, 2] = radius * np.sin(angle)
    return points


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--power", type=int, default=20,
                        help="use 2**power samples per cylinder (default: 20)")
    args = parser.parse_args()
    if not 10 <= args.power <= 23:
        parser.error("--power must be between 10 and 23")

    palette = re.search(r"TAIGA_PORE_SEEDS\[\d+\]\s*=.*?\((.*?)\);",
                        HELPER.read_text(), re.DOTALL)
    if palette is None:
        raise RuntimeError("Cannot find the Taiga Perlin seed palette")
    seeds = re.findall(r"0x[0-9a-fA-F]+u", palette.group(1))
    source = "#version 450 core\n" + shader_source(HELPER) + """
layout(local_size_x = 256) in;
layout(std430, binding = 0) readonly buffer Samples { vec4 points[]; };
layout(std430, binding = 1) writeonly buffer Values { float values[]; };
uniform uint uCount;
uniform uint uVariant;
void main()
{
    uint i = gl_GlobalInvocationID.x;
    if (i >= uCount) return;
    // Recover the raw noise from the actual production coordinate transform.
    // Rotations of the disk and periodic axial shifts preserve its distribution.
    values[i] = TAIGA_PORE_THRESHOLDS[uVariant]
        - taigaPillarPoreField(points[i].xyz, uVariant);
}
"""
    points = cylinder_samples(args.power)
    thresholds = []
    with GL() as gl:
        program = gl.compile([(0x91B9, source)], "Taiga porosity calibration")
        gl.buffer(points.tobytes(), 0)
        output = gl.buffer(len(points) * 4, 1)
        gl.uniform(program, "uCount", "1ui", len(points))
        print(f"// {gl.renderer}; {len(points):,} samples per cylinder")
        for variant, seed in enumerate(seeds):
            gl.uniform(program, "uVariant", "1ui", variant)
            gl.dispatch(program, (len(points) + 255) // 256)
            values = np.frombuffer(gl.read(output, len(points) * 4), dtype=np.float32)
            threshold = float(np.quantile(values, 0.70))
            thresholds.append(threshold)
            print(f"// {seed}: {threshold:.9f}; sampled void "
                  f"{np.mean(values > threshold):.6%}")
    print(f"const float TAIGA_PORE_THRESHOLDS[{len(seeds)}] = float[{len(seeds)}](")
    print("    " + ", ".join(f"{value:.9f}" for value in thresholds) + ");")


if __name__ == "__main__":
    main()
