#!/usr/bin/env python3
"""Compile and run the real native Moon Lab component in a disposable GL context.

Python only builds and launches the C++ integration tests. Surface generation,
preview work, serialization and export use production C++; the engine's HUD and
service locator are narrow fixtures. All export paths resolve inside a temporary
working directory, never the project's production moon.surface.
"""
import argparse
import ctypes
import ctypes.util
import os
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "Tests/Terrain"))
from gl_headless import GL


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--glew-object", type=Path,
                        help="Reuse a PIC GLEW object instead of the system library")
    parser.add_argument("--fast-math", action="store_true",
                        help="Match the engine's Release floating-point flags")
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="endurance-moon-lab-") as temporary:
        directory = Path(temporary)
        binary = directory / "Bin"
        binary.mkdir()
        library = directory / "moon-lab.so"
        command = [os.environ.get("CXX", "c++"), "-std=c++17", "-O2", "-fPIC",
                   "-shared", "-DGLEW_NO_GLU", "-pthread", "-Wl,-z,defs"]
        if args.fast_math:
            command.append("-ffast-math")
        for include in ("ThirdParty", "ThirdParty/glm", "ThirdParty/imgui-cmake/Header",
                        "ANTAGONIZ/Header", "ANTAGONIZ/Header/ProjectUser",
                        "ANTAGONIZ/Header/User", "ANTAGONIZ/Header/Struct",
                        "ANTAGONIZ/Header/Interface", "ANTAGONIZ/Header/Miscellaneous",
                        "ANTAGONIZ/Header/Rendering/ImGUI", "ANTAGONIZ/Header/Debug"):
            command.extend(("-I", str(ROOT / include)))
        sources = ["Tests/Atmosphere/check_moon_lab.cpp",
                   "ANTAGONIZ/Source/ProjectUser/ProceduralMoon.cpp",
                   "ANTAGONIZ/Source/ProjectUser/MoonLabPreview.cpp"]
        sources.extend("ThirdParty/imgui-cmake/Source/" + name + ".cpp"
                       for name in ("imgui", "imgui_draw", "imgui_widgets", "imgui_tables"))
        command.extend(str(ROOT / source) for source in sources)
        if args.glew_object:
            command.append(str(args.glew_object.resolve()))
        else:
            glew = ctypes.util.find_library("GLEW")
            if not glew:
                raise RuntimeError("GLEW unavailable; supply --glew-object")
            command.append("-l:" + glew)
        command.extend(("-l:libGL.so.1", "-o", str(library)))
        subprocess.run(command, check=True)
        with GL() as gl:
            print(f"OpenGL {gl.version}; {gl.renderer}", flush=True)
            runner = ctypes.CDLL(str(library))
            runner.moon_lab_tests.restype = ctypes.c_int
            previous = Path.cwd()
            try:
                os.chdir(binary)
                return runner.moon_lab_tests()
            finally:
                os.chdir(previous)


if __name__ == "__main__":
    raise SystemExit(main())
