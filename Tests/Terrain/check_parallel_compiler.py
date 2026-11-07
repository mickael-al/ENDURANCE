#!/usr/bin/env python3
"""Exercise the engine parallel compiler in a real surfaceless OpenGL context."""
import argparse
import ctypes
import os
from pathlib import Path
import subprocess
import tempfile

from gl_headless import GL, GLUnavailable

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--glew-object", required=True, type=Path)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="parallel-compiler-") as temporary:
        library = Path(temporary) / "compiler.so"
        command = [os.environ.get("CXX", "c++"), "-std=c++17", "-O2", "-Wall", "-Wextra",
                   "-fPIC", "-shared", "-DGLEW_NO_GLU",
                   "-I", str(ROOT / "ANTAGONIZ/Header/Struct"),
                   "-I", str(ROOT / "ANTAGONIZ/Header/Rendering/Shader"),
                   "-I", str(ROOT / "ThirdParty"),
                   str(ROOT / "Tests/Terrain/check_parallel_compiler.cpp"),
                   str(ROOT / "ANTAGONIZ/Source/Rendering/Shader/ParallelShaderCompiler.cpp"),
                   str(args.glew_object.resolve()), "-l:libGL.so.1", "-o", str(library)]
        subprocess.run(command, check=True)
        try:
            with GL() as gl:
                print(f"OpenGL {gl.version}; renderer: {gl.renderer}", flush=True)
                runner = ctypes.CDLL(str(library))
                runner.parallel_compiler_tests.argtypes = []
                runner.parallel_compiler_tests.restype = ctypes.c_int
                return runner.parallel_compiler_tests()
        except GLUnavailable as error:
            print(f"SKIP {error}")
            return 77


if __name__ == "__main__":
    raise SystemExit(main())
