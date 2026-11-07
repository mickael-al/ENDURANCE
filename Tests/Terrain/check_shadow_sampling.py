#!/usr/bin/env python3
"""Execute the production shadow sampling helpers on a known depth array."""

import ctypes as C
import math
from pathlib import Path
import struct
import sys

from gl_headless import GL, GLUnavailable


ROOT = Path(__file__).resolve().parents[2]
SOURCES = ("forward.fs.glsl", "forward_transparency.fs.glsl", "water_transparency.fs.glsl")


def sampling_helpers(source):
    """Copy the actual functions, not a separate shadow implementation."""
    start = source.index("float calculateBias(")
    body = source.index("{", source.index("float filterPCF(", start))
    depth, end = 1, body + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


def check_shadow_sampling(gl):
    functions = {
        "glGenTextures": (None, C.c_int, C.POINTER(C.c_uint)),
        "glDeleteTextures": (None, C.c_int, C.POINTER(C.c_uint)),
        "glBindTexture": (None, C.c_uint, C.c_uint),
        "glActiveTexture": (None, C.c_uint),
        "glTexParameteri": (None, C.c_uint, C.c_uint, C.c_int),
        "glTexImage3D": (None, C.c_uint, C.c_int, C.c_int, C.c_int, C.c_int,
                         C.c_int, C.c_int, C.c_uint, C.c_uint, C.c_void_p),
    }
    for name, signature in functions.items():
        gl._fn(name, *signature)
    texture = C.c_uint()
    gl.glGenTextures(1, C.byref(texture))
    gl.glActiveTexture(0x84C0)  # GL_TEXTURE0
    gl.glBindTexture(0x8C1A, texture.value)  # GL_TEXTURE_2D_ARRAY
    for parameter in (0x2801, 0x2800):  # MIN/MAG: compare before filtering
        gl.glTexParameteri(0x8C1A, parameter, 0x2600)  # GL_NEAREST
    for parameter in (0x2802, 0x2803):
        gl.glTexParameteri(0x8C1A, parameter, 0x812F)  # GL_CLAMP_TO_EDGE
    # Layer 0 occludes, layer 1 is farther away; layer 2 alternates columns.
    depths = [0.25] * 16 + [0.9] * 16 + [0.25, 0.9, 0.25, 0.9] * 4
    gl.glTexImage3D(0x8C1A, 0, 0x8CAC, 4, 4, 3, 0, 0x1902, 0x1406,
                    (C.c_float * len(depths))(*depths))

    cases = [
        ((0.5, 0.5, 0.6, 1.0), 0, 0.0, "occluded receiver"),
        ((0.5, 0.5, 0.6, 1.0), 1, 1.0, "independent shadow layer"),
        ((-0.1, 0.5, 0.6, 1.0), 0, 1.0, "outside left"),
        ((1.1, 0.5, 0.6, 1.0), 0, 1.0, "outside right"),
        ((0.5, -0.1, 0.6, 1.0), 0, 1.0, "outside bottom"),
        ((0.5, 1.1, 0.6, 1.0), 0, 1.0, "outside top"),
        ((0.5, 0.5, -0.1, 1.0), 0, 1.0, "before near depth"),
        ((0.5, 0.5, 1.1, 1.0), 0, 1.0, "after far depth"),
        ((-0.5, -0.5, -0.6, -1.0), 0, 1.0, "negative homogeneous W"),
        ((0.5, 0.5, 0.6, 0.0), 0, 1.0, "zero homogeneous W"),
        ((1.0, 1.0, 1.2, 2.0), 0, 0.0, "perspective divide"),
        ((0.01, 0.5, 0.6, 1.0), 0, 1.0 / 3.0, "outside PCF taps are lit"),
        ((0.375, 0.5, 0.6, 1.0), 2, 1.0 / 3.0, "three texel PCF footprint"),
    ]
    cosines = (-1.0, 0.0, 0.01, 0.5, 1.0, 1.0001)
    inputs = [coordinate for coordinate, _, _, _ in cases]
    inputs += [(cosine, 0.0, 0.0, 0.0) for cosine in cosines]
    output = gl.buffer(len(inputs) * 4, 0)
    gl.buffer(struct.pack(f"<{len(inputs) * 4}f", *(v for row in inputs for v in row)), 1)
    layers = ",".join(f"{layer}u" for _, layer, _, _ in cases)
    main = f"""
layout(local_size_x = 1) in;
layout(std430, binding = 0) writeonly buffer Results {{ float values[]; }};
layout(std430, binding = 1) readonly buffer Inputs {{ vec4 inputs[]; }};
const uint layers[{len(cases)}] = uint[{len(cases)}]({layers});
void main() {{
    uint i = gl_GlobalInvocationID.x;
    values[i] = i < {len(cases)}u
        ? filterPCF(inputs[i], layers[i], 0.0)
        : calculateBias(vec4(0.0), vec3(0,0,1), vec3(0,0,inputs[i].x));
}}
"""
    try:
        for name in SOURCES:
            source = (ROOT / "Asset/Shader" / name).read_text()
            # Also compile/link the entire production fragment stage, catching
            # incompatible signatures or errors outside the extracted helpers.
            gl.compile([(0x8B30, source)], name)
            helpers = sampling_helpers(source)
            program = gl.compile([(0x91B9, "#version 450\nlayout(binding=0) uniform sampler2DArray shadowDepth;\n"
                                    + helpers + main)], name + " shadow sampling probe")
            gl.dispatch(program, len(inputs))
            values = struct.unpack(f"<{len(inputs)}f", gl.read(output, len(inputs) * 4))
            for actual, (_, _, expected, label) in zip(values, cases):
                assert math.isfinite(actual) and abs(actual - expected) < 1e-6, (name, label, actual, expected)
            biases = values[len(cases):]
            assert all(math.isfinite(v) and 0.0 <= v <= 0.010001 for v in biases), (name, "unbounded bias", biases)
            assert abs(biases[3] - 0.005 * math.sqrt(3)) < 1e-6, (name, "regular slope changed", biases)
            assert biases[4] == 0.0 and biases[5] == 0.0, (name, "normal incidence/overshoot", biases)
            assert all(biases[i] >= biases[i + 1] for i in range(len(biases) - 1)), (name, "bias monotonicity", biases)
            gl.check(name)
            print(f"PASS {name}: projection guards, array layer, PCF footprint, bounded grazing bias")
    finally:
        gl.glDeleteTextures(1, C.byref(texture))


if __name__ == "__main__":
    try:
        with GL() as gl:
            print(f"OpenGL {gl.version}; renderer: {gl.renderer}")
            check_shadow_sampling(gl)
    except GLUnavailable as error:
        print(f"SKIP: {error}", file=sys.stderr)
        sys.exit(77)
