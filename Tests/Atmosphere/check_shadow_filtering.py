#!/usr/bin/env python3
"""Execute production volumetric filtering and all three surface cascade callers.

Nearest depth textures deliberately alternate blocked/lit layers. An independent
tent oracle checks sub-texel filtering; deliberately disagreeing cascades make
hard switches observable. Readbacks are test instrumentation only.
"""
from __future__ import annotations

import ctypes as C
import re
import struct

from fixtures import Parameters, IDENTITY
from gl_support import AtmosphereGL, ROOT, R32F, TEXTURE_2D_ARRAY, shader_source

SHADERS = ROOT / "Asset/Shader"


def surface_program(gl, name):
    # Execute the actual caller, not a second implementation of cascade choice.
    source = (SHADERS / name).read_text()
    start = source.index("uint cascadeIndex = 0;")
    end = source.index("\n\t\t\t\t}", start)
    caller = source[start:end]
    declarations = []
    for type_name, buffer_name, instance in (("StructUBL", "UniformBufferLight", "ubl"),
                                              ("StructShadow", "UniformBufferShadow", "ubs")):
        match = re.search(r"struct " + type_name + r"\s*\{.*?\};", source, re.S)
        assert match
        declarations.append(match.group(0))
        match = re.search(r"layout\(std430, binding = \d+\) buffer " + buffer_name +
                          r"\s*\{.*?\} " + instance + r";", source, re.S)
        assert match
        declarations.append(match.group(0))
    code = "#version 450\n" + shader_source(SHADERS / "Shadow/directional_surface.glsl")
    code += "\n".join(declarations) + """
#define SHADOW_MAP_CASCADE_COUNT 4
layout(binding=4) uniform sampler2DArray shadowDepth;
layout(local_size_x=1) in;
layout(std430,binding=0) readonly buffer Cases { vec4 positions[]; };
layout(std430,binding=2) writeonly buffer Results { float visibility[]; };
const mat4 biasMat=mat4(.5,0,0,0, 0,.5,0,0, 0,0,.5,0, .5,.5,.5,1);
void main() {
 uint id=gl_GlobalInvocationID.x; int i=0;
 vec3 worldPos=positions[id].xyz, WorldPos=worldPos, viewPos=worldPos;
 vec3 shadowGeometricNormal=vec3(0,0,1), shadowPositionError=vec3(0);
 vec3 shadowWorldDx=vec3(1,0,0), shadowWorldDy=vec3(0,1,0);
 float shadow=1.0;
""" + caller + "\nvisibility[id]=shadow;\n}\n"
    return gl.compile([(0x91B9, code)], "actual cascade caller " + name)


class Fixture:
    def __init__(self, gl):
        self.gl = gl
        self.parameters = Parameters().set("Control", 0, 0, 0, 1).set("Shadow", 0, -1, 4, 0)
        self.ubo = self.parameters.upload(gl)
        self.inputs = gl.buffer(8192, 0)
        self.output = gl.buffer(2048, 2)
        matrix = IDENTITY.copy()
        matrix[10] = .00001  # 25 cm volume bias is small relative to test blockers.
        self.matrices = gl.buffer(b"".join(struct.pack("<20f", *(matrix + [0, 0, 0, split]))
                                         for split in (-100, -200, -400, -800)), 3)
        gl.glBindBufferBase(0x90D2, 5, self.matrices)
        light = bytearray(80)
        struct.pack_into("<i", light, 56, 0)  # shadowID; zero bias isolates filtering.
        self.light = gl.buffer(bytes(light), 1)
        self.texture = gl.texture((8, 8, 4), target=TEXTURE_2D_ARRAY, internal=R32F, value=1, linear=False)
        gl.sampler(4, self.texture, TEXTURE_2D_ARRAY)
        code = "#version 450\n" + shader_source(SHADERS / "Atmosphere/shadows.glsl") + """
layout(local_size_x=1) in;
layout(std430,binding=0) readonly buffer Cases { vec4 positions[]; };
layout(std430,binding=2) writeonly buffer Results { float visibility[]; };
uniform int firstLayer;
void main() { uint i=gl_GlobalInvocationID.x;
 visibility[i]=geAtmTerrainVisibility(positions[i].xyz,firstLayer); }
"""
        self.volume = gl.compile([(0x91B9, code)], "production god-ray visibility")
        self.surfaces = [surface_program(gl, name) for name in
                         ("forward.fs.glsl", "forward_transparency.fs.glsl", "water_transparency.fs.glsl")]

    def depths(self, fn):
        data = (C.c_float * 256)(*[fn(x, y, layer) for layer in range(4)
                                  for y in range(8) for x in range(8)])
        self.gl.glBindTexture(TEXTURE_2D_ARRAY, self.texture)
        self.gl.glTexImage3D(TEXTURE_2D_ARRAY, 0, R32F, 8, 8, 4, 0, 0x1903, 0x1406,
                            C.cast(data, C.c_void_p))

    def run(self, program, points, first=0):
        self.gl.write(self.inputs, b"".join(struct.pack("<4f", *p, 0) for p in points))
        if program == self.volume:
            self.gl.uniform(program, "firstLayer", "1i", first)
        self.gl.dispatch(program, len(points), 1, 1)
        return struct.unpack(f"<{len(points)}f", self.gl.read(self.output, len(points) * 4))


def main():
    with AtmosphereGL() as gl:
        print(f"OpenGL {gl.version}; renderer: {gl.renderer}", flush=True)
        f = Fixture(gl)
        f.depths(lambda x, y, layer: .25 if x < 4 else .75)
        xs = [-.2 + i * .01 for i in range(41)]
        expected = [min(1, max(0, .5 + 4*x)) for x in xs]
        for program in [f.volume] + f.surfaces:
            actual = f.run(program, [(x, 0, -50) for x in xs])
            assert max(abs(a-b) for a, b in zip(actual, expected)) < 2e-6, actual
            assert max(abs(a-b) for a, b in zip(actual, actual[1:])) < .04001
        print("PASS actual volume + opaque/transparent/water: independent tent oracle; no 50% sub-texel jumps", flush=True)

        # Coarse maps deliberately disagree. Each boundary must be continuous,
        # while points outside the final 10% keep the selected map unchanged.
        f.depths(lambda x, y, layer: .25 if layer % 2 == 0 else .75)
        depths = [80, 90, 92.5, 95, 97.5, 99.999, 100, 100.001,
                  180, 190, 195, 200, 200.001, 360, 380, 390, 400, 400.001, 750]
        expected = [0, 0, .15625, .5, .84375, 1, 1, 1,
                    1, 1, .5, 0, 0, 0, 0, .5, 1, 1, 1]
        for program in [f.volume] + f.surfaces:
            actual = f.run(program, [(0, 0, -z) for z in depths])
            assert max(abs(a-b) for a, b in zip(actual, expected)) < 2e-5, actual
        print("PASS actual volume + all surface callers: all three cascade overlaps, split continuity, unchanged interiors", flush=True)

        # Last map's finite XY coverage blends to light; edge taps must never
        # repeat the fully dark boundary texel through CLAMP_TO_EDGE.
        f.depths(lambda x, y, layer: .25)
        xs = [.7 + i * .001 for i in range(321)]
        for depth in (-50, -150, -300, -750):
            actual = f.run(f.volume, [(x, 0, depth) for x in xs])
            assert actual[0] == 0 and actual[-1] == 1
            assert all(b >= a-2e-6 for a, b in zip(actual, actual[1:]))
            assert max(abs(a-b) for a, b in zip(actual, actual[1:])) < .016
        assert all(v == 1 for v in f.run(f.volume, [(1.1, 0, -50), (-1.1, 0, -50)]))
        assert f.run(f.volume, [(0, 0, -50)], first=-1) == (1,)
        assert f.run(f.volume, [(0, 0, -50)], first=4) == (1,)
        f.parameters.set("Control", 0, 0, 0, 0).upload(gl, f.ubo)
        assert f.run(f.volume, [(0, 0, -50)]) == (1,)
        print("PASS volume coverage fade, out-of-bounds/layer safety and disabled identity", flush=True)


if __name__ == "__main__":
    main()
