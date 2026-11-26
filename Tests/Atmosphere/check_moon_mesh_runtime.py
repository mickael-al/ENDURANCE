#!/usr/bin/env python3
"""Exercise the real lunar cache and atmosphere on EGL, including the baked GLB.

Readbacks and blocking timings are confined to this test. No Python geometry or
image generation is involved; the production C++ loader/renderer do all work.
"""
import argparse
import ctypes
import os
from pathlib import Path
import subprocess
import tempfile

from gl_support import AtmosphereGL, ROOT


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--capture-dir", type=Path)
    parser.add_argument("--fast-math", action="store_true")
    args = parser.parse_args()
    if args.capture_dir:
        args.capture_dir.mkdir(parents=True, exist_ok=True)
        os.environ["ENDURANCE_MOON_CAPTURE_DIR"] = str(args.capture_dir.resolve())
    with tempfile.TemporaryDirectory(prefix="endurance-moon-runtime-") as temporary:
        directory = Path(temporary)
        library = directory / "moon-runtime.so"
        command = [os.environ.get("CXX", "c++"), "-std=c++17", "-O2", "-Wall", "-Wextra",
                   "-fPIC", "-shared", "-DGLEW_NO_GLU", "-pthread", "-fvisibility=hidden",
                   "-ffunction-sections", "-fdata-sections"]
        if args.fast_math:
            command.append("-ffast-math")
        for path in ("Tests/Atmosphere/runtime_fixture", "ANTAGONIZ/Header/Struct",
                     "ANTAGONIZ/Header/Rendering/Shader", "ANTAGONIZ/Header/ProjectUser",
                     "ThirdParty", "ThirdParty/glm"):
            command.extend(("-I", str(ROOT / path)))
        for path in ("Tests/Atmosphere/check_moon_mesh_runtime.cpp",
                     "ANTAGONIZ/Source/ProjectUser/EnduranceMoonMesh.cpp",
                     "ANTAGONIZ/Source/Rendering/Shader/MoonMeshCache.cpp",
                     "ANTAGONIZ/Source/Rendering/Shader/Atmosphere.cpp",
                     "ANTAGONIZ/Source/Rendering/Shader/AtmosphereSettings.cpp",
                     "ANTAGONIZ/Source/Rendering/Shader/ShaderCompilation.cpp",
                     "ANTAGONIZ/Source/Rendering/Shader/ShaderBinaryCache.cpp"):
            command.append(str(ROOT / path))
        command.extend(("-l:libGLEW.so.2.2", "-l:libGL.so.1", "-l:libEGL.so.1", "-ldl",
                        "-Wl,--gc-sections", "-Wl,-z,defs", "-o", str(library)))
        subprocess.run(command, check=True)
        os.environ["ENDURANCE_SHADER_CACHE_DIR"] = str(directory / "cache")
        with AtmosphereGL() as gl:
            print(f"OpenGL {gl.version}; renderer: {gl.renderer}", flush=True)
            runner = ctypes.CDLL(str(library))
            runner.moon_mesh_runtime_tests.argtypes = [ctypes.c_char_p]
            runner.moon_mesh_runtime_tests.restype = ctypes.c_int
            result = runner.moon_mesh_runtime_tests(str(ROOT).encode())
            if result:
                raise SystemExit(result)


if __name__ == "__main__":
    main()
