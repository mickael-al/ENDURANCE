#!/usr/bin/env python3
"""Compile and link the production atmosphere shaders on a real OpenGL driver."""
from __future__ import annotations

import argparse
import time

from gl_support import AtmosphereGL, FULLSCREEN_VERTEX, ROOT, shader_source

DIRECTORY = ROOT / "Asset/Shader/Atmosphere"


def compile_shaders(gl):
    programs = {}
    files = sorted(DIRECTORY.glob("*.C.glsl"))
    assert files, f"No production atmosphere compute shaders in {DIRECTORY}"
    for path in files:
        begin = time.perf_counter()
        programs[path.name] = gl.compute(path)
        print(f"PASS compile {path.relative_to(ROOT)} ({time.perf_counter() - begin:.3f}s)", flush=True)
    environment=DIRECTORY/"environment.C.glsl"
    programs["environment.physical"]=gl.compute(environment,"#define GE_PHYSICAL_SKY 1")
    print("PASS compile physical sky/environment integration variant",flush=True)
    for physical in (False,True):
        defines="#define GE_ATM_MOON_SURFACE 1\n"+("#define GE_PHYSICAL_SKY 1\n" if physical else "")
        programs[f"environment.moon.{physical}"]=gl.compute(environment,defines)
        fragment=shader_source(DIRECTORY/"sky.fs.glsl",defines)
        programs[f"sky.moon.{physical}"]=gl.compile([(0x8B31,shader_source(DIRECTORY/"sky.vs.glsl")),
                                                    (0x8B30,fragment)],"textured moon sky")
    print("PASS textured lunar sphere: physical and gradient variants",flush=True)
    for path in sorted(DIRECTORY.glob("*.fs.glsl")):
        vertex = path.with_name(path.name.replace(".fs.glsl", ".vs.glsl"))
        source = shader_source(vertex) if vertex.exists() else FULLSCREEN_VERTEX
        programs[path.name] = gl.compile([(0x8B31, source), (0x8B30, shader_source(path))], str(path))
        print(f"PASS link {path.relative_to(ROOT)}", flush=True)
    for fragment,vertex in (("forward","forward"),
                            ("forward_transparency","forward_transparency"),
                            ("forward_transparency","forward_transparency_skinned"),
                            ("water_transparency","water_transparency"),
                            ("opaque","opaque"),("opaque","opaque_skinned"),
                            ("opaque","opaque_cloth"),
                            ("lightmap","lightmap"),("iso","iso"),
                            ("background","background")):
        shaders=ROOT/"Asset/Shader"
        name=f"{fragment}/{vertex}"
        programs[name]=gl.compile([(0x8B31,shader_source(shaders/f"{vertex}.vs.glsl")),
                                   (0x8B30,shader_source(shaders/f"{fragment}.fs.glsl"))],name)
        print(f"PASS link engine material {name}",flush=True)
    gl.check("all atmosphere shader compilation")
    return programs


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.parse_args()
    with AtmosphereGL() as gl:
        print(f"OpenGL {gl.version}; renderer: {gl.renderer}", flush=True)
        programs = compile_shaders(gl)
        print(f"PASS {len(programs)} production atmosphere/material programs", flush=True)


if __name__ == "__main__":
    main()
