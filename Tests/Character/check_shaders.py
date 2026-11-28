#!/usr/bin/env python3
"""Link character pipelines and exercise their actual fragment shaders on EGL.

This is a correctness test on the available driver, not a GPU timing benchmark.
"""
from pathlib import Path
import math
import struct
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "Tests/Atmosphere"))
from gl_support import AtmosphereGL, RGBA32F, TEXTURE_CUBE_MAP, TEXTURE_2D_ARRAY, shader_source

SHADERS = ROOT / "Asset/Shader"
VERTEX = """#version 450
layout(location=0) out vec2 uv;
layout(location=1) out vec3 color;
layout(location=2) out vec3 position;
layout(location=3) out mat3 basis;
layout(location=6) flat out int material;
void main() {
    vec2 p=vec2((gl_VertexID<<1)&2, gl_VertexID&2);
    uv=p; color=vec3(1); position=vec3(p*2-1,0); basis=mat3(1); material=0;
    gl_Position=vec4(p*2-1,0,1);
}
"""
IDENTITY = (1., 0., 0., 0., 0., 1., 0., 0., 0., 0., 1., 0., 0., 0., 0., 1.)


def main():
    with AtmosphereGL() as gl:
        print(f"OpenGL {gl.version}; renderer: {gl.renderer}", flush=True)
        for fragment, vertex, defines in (
            ("Character/jade", "forward_transparency_skinned", ""),
            ("Character/jade", "forward_transparency_skinned",
             "#define GE_CHARACTER_SCATTER 0.0\n#define GE_CHARACTER_FACETS 0.0"),
            ("Character/fabric", "opaque_skinned", ""),
            ("Character/fabric", "opaque_cloth", ""),
            ("shadow", "opaque_skinned_shadow", ""),
            ("shadow", "opaque_cloth_shadow", ""),
        ):
            gl.compile([(0x8B31, shader_source(SHADERS / (vertex + ".vs.glsl"))),
                        (0x8B30, shader_source(SHADERS / (fragment + ".fs.glsl"), defines))],
                       f"{fragment}/{vertex}")
            print(f"PASS production link {fragment}/{vertex}", flush=True)

        jade = gl.compile([(0x8B31, VERTEX), (0x8B30, shader_source(SHADERS / "Character/jade.fs.glsl"))], "jade pixels")
        fabric = gl.compile([(0x8B31, VERTEX), (0x8B30, shader_source(SHADERS / "Character/fabric.fs.glsl"))], "fabric pixels")
        fabric_back = gl.compile([(0x8B31, VERTEX.replace("gl_Position=vec4(p*2-1,0,1)",
            "gl_Position=vec4((p*2-1).yx,0,1)")),
            (0x8B30, shader_source(SHADERS / "Character/fabric.fs.glsl"))], "fabric backface pixels")
        gl.buffer(struct.pack("<52f", 0., 0., 2., 0., *IDENTITY, *IDENTITY, *IDENTITY), 0)
        material_buffer = gl.buffer(64, 2)
        misc_buffer = gl.buffer(32, 3)
        light_buffer = gl.buffer(80, 4)
        gl.buffer(80 * 6, 5)
        white = gl.texture((1, 1), value=(1., 1., 1., 1.))
        skin = gl.texture((1, 1), value=(0.55, 0.72, 0.42, 1.))
        invisible = gl.texture((1, 1), value=(1., 1., 1., 0.1))
        normal = gl.texture((1, 1), value=(0.5, 0.5, 1., 1.))
        black_cube = gl.texture((1, 1), target=TEXTURE_CUBE_MAP, value=(0., 0., 0., 1.))
        depth = gl.texture((1, 1, 6), target=TEXTURE_2D_ARRAY, value=(1., 1., 1., 1.))
        brdf = gl.texture((1, 1), value=(0., 0., 0., 1.))
        for unit, texture in ((0, skin), (1, normal), (2, white), (3, white), (4, white), (8, brdf)):
            gl.sampler(unit, texture)
        gl.sampler(5, depth, TEXTURE_2D_ARRAY)
        gl.sampler(6, black_cube, TEXTURE_CUBE_MAP)
        gl.sampler(7, black_cube, TEXTURE_CUBE_MAP)

        def material(roughness):
            # StructUBM: vec4 + 2 vec2 + 5 floats; array stride rounded to 64.
            gl.write(material_buffer, struct.pack("<16f", 1., 1., 1., 1., 0., 0., 1., 1.,
                     0., roughness, 1., 1., 0., 0., 0., 0.))

        def light(count=1, direction=(0., 0., -1.)):
            gl.write(misc_buffer, struct.pack("<iffffI8x", count, 0., 2.2, 0., 60., 0))
            data = bytearray(80)
            struct.pack_into("<3f", data, 16, 1., 1., 1.)
            struct.pack_into("<3f", data, 32, *direction)
            struct.pack_into("<ffi i i i ff", data, 44, 10., 45., 0, -1, -1, -1, 0., 0.)
            gl.write(light_buffer, bytes(data))

        def render(program=jade, outputs=1):
            gl.glActiveTexture(0x84C0 + 15)  # allocation must not replace input samplers
            targets = [gl.texture((1, 1), internal=RGBA32F, value=(-9., -9., -9., -9.)) for _ in range(outputs)]
            gl.glDisable(0x0B44)  # cull face: test both sides deliberately
            gl.fullscreen(program, 1, 1, targets)
            return [gl.texels(target, (1, 1)) for target in targets]

        material(0.30)
        light(0)
        dark = render()[0]
        assert max(abs(x) for x in dark[:3]) < 1e-7 and dark[3] == 1., dark
        light()
        lit = render()[0]
        assert all(math.isfinite(x) for x in lit) and lit[1] > lit[0] > lit[2] > 0., lit
        material(0.16)
        polished = render()[0]
        material(0.75)
        rough = render()[0]
        assert polished[1] > rough[1], (polished, rough)
        # Light beyond the local tangent still reaches the broad volume lobe.
        light(direction=(-math.sqrt(0.96), 0., 0.2))
        side = render()[0]
        assert all(math.isfinite(x) for x in side) and side[1] > 0., side
        gl.sampler(0, invisible)
        assert render()[0] == (-9., -9., -9., -9.), "jade alpha mask must discard"
        gl.sampler(0, skin)
        material(0.1)  # fabric must not become polished plastic from a bad texture
        front = render(fabric, 4)
        back = render(fabric_back, 4)
        assert front[3][0] == 0. and abs(front[3][1] - 0.55) < 1e-6, front
        assert front[1][2] > 0.99 and back[1][2] < -0.99, (front, back)
        gl.sampler(0, invisible)
        assert render(fabric, 4)[2] == (-9., -9., -9., -9.), "fabric alpha mask must discard"
        gl.check("character fragment behavior")
        print("PASS jade: finite lighting, green tint, no unlit emission, roughness response, volume lobe, alpha mask", flush=True)
        print("PASS fabric: dielectric roughness, outward front/back normals, alpha mask", flush=True)


if __name__ == "__main__":
    main()
