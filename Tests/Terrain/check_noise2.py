#!/usr/bin/env python3
"""Compare production vector noise against its former scalar construction."""
import random
import struct
from pathlib import Path

from gl_headless import GL, shader_source

ROOT = Path(__file__).resolve().parents[2]
SHADER = ROOT / "Asset/Shader/Noise/noise_2d.glsl"

# The four-corner scalar construction is intentionally independent of the
# vector implementation. Its fixed historical hash/gradient helpers are independent of the library.
REFERENCE = r"""
uint referenceHash(uint x)
{
    x ^= x >> 16u; x *= 0x7feb352du;
    x ^= x >> 15u; x *= 0x846ca68bu;
    return x ^ (x >> 16u);
}
uint referenceHash2(ivec2 p, uint salt)
{
    return referenceHash(uint(p.x)*0x9e3779b9u ^ uint(p.y)*0x85ebca6bu ^ uSeed ^ salt);
}
float referenceGradient2(uint h, vec2 p)
{
    uint g=h&7u;
    if(g<4u) return ((g&1u)==0u?p.x:-p.x)+((g&2u)==0u?p.y:-p.y);
    return g==4u?p.x:g==5u?-p.x:g==6u?p.y:-p.y;
}
vec2 referenceFade(vec2 f) { return f*f*f*(f*(f*6.0-15.0)+10.0); }
float referenceNoise2(vec2 p, uint salt)
{
    ivec2 i = ivec2(floor(p));
    vec2 f = fract(p), u = referenceFade(f);
    float a = referenceGradient2(referenceHash2(i, salt), f);
    float b = referenceGradient2(referenceHash2(i + ivec2(1,0), salt), f - vec2(1,0));
    float c = referenceGradient2(referenceHash2(i + ivec2(0,1), salt), f - vec2(0,1));
    float d = referenceGradient2(referenceHash2(i + ivec2(1,1), salt), f - vec2(1,1));
    return clamp(mix(mix(a,b,u.x), mix(c,d,u.x), u.y), -1.0, 1.0);
}
layout(local_size_x=64) in;
layout(std430,binding=0) readonly buffer Inputs { vec2 points[]; };
layout(std430,binding=1) writeonly buffer Outputs { float results[]; };
void main()
{
    uint index = gl_GlobalInvocationID.x;
    results[index] = PROBE_NOISE(points[index], 0xb5297a4du);
}
"""


def main():
    rng = random.Random(1337)
    points = []
    grid = (-1024., -17., -1., -0., 0., 1., 17., 1024.)
    for x in grid:
        for y in grid:
            for epsilon in (-1e-5, 0., 1e-5):
                points.append((x + epsilon, y + epsilon))
    while len(points) < 8192:
        points.append((rng.uniform(-4096, 4096), rng.uniform(-4096, 4096)))
    payload = struct.pack(f"<{len(points)*2}f", *(v for p in points for v in p))
    source = shader_source(SHADER) + REFERENCE
    with GL() as gl:
        print(f"OpenGL {gl.version}; renderer: {gl.renderer}", flush=True)
        programs = [gl.compile([(0x91B9, "#version 450 core\n"
            f"#define PROBE_NOISE(p,salt) {expression}\nuniform uint uSeed;\n" + source)],
            label) for label,expression in (("historical scalar", "referenceNoise2(p,salt)"),
                ("library vector", "noisePerlin2D(p,uSeed ^ (salt))"))]
        gl.buffer(payload, 0)
        output = gl.buffer(len(points) * 4, 1)
        for seed in (0, 1, 1337, 98765, 0xffffffff):
            values = []
            for program in programs:
                gl.uniform(program, "uSeed", "1ui", seed)
                gl.dispatch(program, len(points) // 64)
                values.append(gl.read(output, len(points) * 4))
            if values[0] != values[1]:
                reference = struct.unpack(f"<{len(points)}I", values[0])
                actual = struct.unpack(f"<{len(points)}I", values[1])
                index = next(i for i, (a, b) in enumerate(zip(reference, actual)) if a != b)
                raise AssertionError(f"Noise mismatch: seed={seed}, point={points[index]}, "
                    f"reference={reference[index]:08x}, actual={actual[index]:08x}")
        gl.check("noise2 scalar/vector equivalence")
    print("PASS noise2: 40960 values bitwise identical, including signed zeros, "
          "integer grids, near-grid points and random coordinates across five seeds")


if __name__ == "__main__":
    main()
