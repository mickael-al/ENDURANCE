#!/usr/bin/env python3
"""Run the production ClimateNoiseLabPanel state machine in a desktop EGL context.

Shader/model/reference files are copied to a disposable directory; user training
data is never overwritten. Requires a C++17 compiler and Linux OpenGL 4.5/EGL.
Building bundled GLEW also requires a C compiler and X11 development headers;
--glew-object reuses an existing PIC object and bypasses that build requirement.
"""
import argparse
import ctypes
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import zlib

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "Tests/Terrain"))
from gl_headless import GL


def reference_png(path):
    """Small non-square image; channels have different, non-constant targets."""
    width, height = 8, 12
    pixels = b"".join(b"\0" + bytes(value for x in range(width)
                                    for value in (x * 32, y * 21, (x + y) * 13))
                      for y in range(height))

    def chunk(name, data):
        return (struct.pack(">I", len(data)) + name + data +
                struct.pack(">I", zlib.crc32(name + data) & 0xffffffff))

    path.write_bytes(b"\x89PNG\r\n\x1a\n" +
                     chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)) +
                     chunk(b"IDAT", zlib.compress(pixels)) + chunk(b"IEND", b""))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--glew-object", type=Path,
                        help="Reuse a PIC GLEW object; otherwise compile the bundled source")
    parser.add_argument("--fast-math", action="store_true",
                        help="Match the engine's Release floating-point optimization flags")
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="climate-panel-") as temporary:
        directory = Path(temporary)
        shaders = directory / "Asset/Shader"
        shaders.mkdir(parents=True)
        for name in ("ClimateNoiseLab", "Noise"):
            shutil.copytree(ROOT / "Asset/Shader" / name, shaders / name,
                            ignore=shutil.ignore_patterns("*.bin", "*.png"))
        reference_png(shaders / "ClimateNoiseLab/climate_reference.png")
        binary_dir = directory / "Bin"
        binary_dir.mkdir()
        library = directory / "check_panel.so"
        glew_object = args.glew_object.resolve() if args.glew_object else directory / "glew.o"
        if args.glew_object is None:
            subprocess.run([os.environ.get("CC", "cc"), "-O2", "-fPIC", "-DGLEW_NO_GLU",
                            "-I", str(ROOT / "ThirdParty/glew/include"), "-c",
                            str(ROOT / "ThirdParty/glew/src/glew.c"), "-o", str(glew_object)],
                           check=True)
        os.environ["ENDURANCE_SHADER_CACHE_DIR"] = str(directory / "shader-cache")
        command = [os.environ.get("CXX", "c++"), "-std=c++17", "-O2",
                   "-fPIC", "-shared", "-DGLEW_NO_GLU", "-pthread"]
        if args.fast_math:
            command.append("-ffast-math")
        includes = [ROOT / "ThirdParty", ROOT / "ThirdParty/imgui-cmake/Header"]
        includes.extend(ROOT / "ANTAGONIZ/Header" / name for name in
                        ("ProjectUser", "User", "Struct", "Interface", "Miscellaneous",
                         "Rendering/Shader"))
        for include in includes:
            command.extend(["-I", str(include)])
        command.append(str(ROOT / "Tests/ClimateNoiseLab/check_panel.cpp"))
        command.extend(str(ROOT / "ANTAGONIZ/Source/Rendering/Shader" / source)
                       for source in ("ShaderCompilation.cpp", "ShaderBinaryCache.cpp"))
        command.extend(str(ROOT / "ThirdParty/imgui-cmake/Source" / source)
                       for source in ("imgui.cpp", "imgui_draw.cpp", "imgui_widgets.cpp", "imgui_tables.cpp"))
        command.extend([str(glew_object), "-l:libGL.so.1", "-o", str(library)])
        subprocess.run(command, check=True)
        with GL() as gl:
            print(f"OpenGL {gl.version}; renderer: {gl.renderer}", flush=True)
            runner = ctypes.CDLL(str(library))
            runner.climate_panel_tests.restype = ctypes.c_int
            previous_directory = Path.cwd()
            try:
                os.chdir(binary_dir)
                return runner.climate_panel_tests()
            finally:
                os.chdir(previous_directory)


if __name__ == "__main__":
    raise SystemExit(main())
