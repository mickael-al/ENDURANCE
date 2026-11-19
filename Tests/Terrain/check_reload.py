#!/usr/bin/env python3
"""Verify production GPU shader reload using only temporary editable GLSL copies."""
import argparse
import ctypes
import os
from pathlib import Path
import subprocess
import tempfile

from gl_headless import GL, copy_terrain_shaders

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--glew-object", required=True, type=Path,
                        help="PIC object, static archive or shared GLEW library")
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="terrain-shader-reload-") as temporary:
        directory = Path(temporary)
        os.environ["ENDURANCE_SHADER_CACHE_DIR"] = str(directory / "program-cache")
        shaders = directory / "shaders"
        copy_terrain_shaders(ROOT / "Asset/Shader/ProceduralTerrain", shaders)
        library = directory / "check_reload.so"
        command = [os.environ.get("CXX", "c++"), "-std=c++17", "-O3", "-ffast-math",
                   "-fPIC", "-shared", "-DGLEW_NO_GLU",
                   "-I", str(ROOT / "ANTAGONIZ/Header/Struct"),
                   "-I", str(ROOT / "ANTAGONIZ/Header/Miscellaneous"),
                   "-I", str(ROOT / "ANTAGONIZ/Header/Rendering/Shader"),
                   "-I", str(ROOT / "ThirdParty"), str(ROOT / "Tests/Terrain/check_reload.cpp"),
                   str(ROOT / "ANTAGONIZ/Source/Miscellaneous/ProceduralTerrainGpu.cpp"),
                   str(ROOT / "ANTAGONIZ/Source/Rendering/Shader/ShaderCompilation.cpp"),
                   str(ROOT / "ANTAGONIZ/Source/Rendering/Shader/ShaderBinaryCache.cpp"),
                   str(ROOT / "ANTAGONIZ/Source/Rendering/Shader/ShaderProgramBatch.cpp"),
                   str(ROOT / "ANTAGONIZ/Source/Rendering/Shader/ParallelShaderCompiler.cpp"),
                   str(ROOT / "ANTAGONIZ/Source/Rendering/Shader/ComputeShader.cpp"),
                   str(args.glew_object.resolve()), "-pthread", "-l:libGL.so.1", "-l:libEGL.so.1", "-o", str(library)]
        subprocess.run(command, check=True)
        with GL() as gl:
            print(f"OpenGL {gl.version}; renderer: {gl.renderer}", flush=True)
            runner = ctypes.CDLL(str(library))
            runner.terrain_reload_tests.argtypes = [ctypes.c_char_p]
            runner.terrain_reload_tests.restype = ctypes.c_int
            return runner.terrain_reload_tests(str(shaders).encode())


if __name__ == "__main__":
    raise SystemExit(main())
