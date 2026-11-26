#!/usr/bin/env python3
"""Link and run the real C++ Atmosphere on an EGL context, without the editor."""
import ctypes
import os
from pathlib import Path
import subprocess
import tempfile

from gl_support import AtmosphereGL,ROOT


def main():
    with tempfile.TemporaryDirectory(prefix="endurance-atmosphere-runtime-") as temporary:
        directory=Path(temporary)
        library=directory/"runtime.so"
        command=[os.environ.get("CXX","c++"),"-std=c++17","-O2","-Wall","-Wextra",
                 "-fPIC","-shared","-DGLEW_NO_GLU","-pthread","-fvisibility=hidden",
                 "-ffunction-sections","-fdata-sections"]
        for path in ("Tests/Atmosphere/runtime_fixture","ANTAGONIZ/Header/Struct",
                     "ANTAGONIZ/Header/Rendering/Shader","ThirdParty","ThirdParty/glm"):
            command.extend(("-I",str(ROOT/path)))
        sources=("Tests/Atmosphere/check_runtime.cpp",
                 "ANTAGONIZ/Source/Rendering/Shader/Atmosphere.cpp",
                 "ANTAGONIZ/Source/Rendering/Shader/MoonMeshCache.cpp",
                 "ANTAGONIZ/Source/Rendering/Shader/AtmosphereSettings.cpp",
                 "ANTAGONIZ/Source/Rendering/Shader/ShaderCompilation.cpp",
                 "ANTAGONIZ/Source/Rendering/Shader/ShaderBinaryCache.cpp")
        command.extend(str(ROOT/path) for path in sources)
        command.extend(("-l:libGLEW.so.2.2","-l:libGL.so.1","-l:libEGL.so.1","-ldl",
                        "-Wl,--gc-sections","-Wl,-z,defs","-o",str(library)))
        subprocess.run(command,check=True)
        os.environ["ENDURANCE_SHADER_CACHE_DIR"]=str(directory/"cache")
        with AtmosphereGL() as gl:
            print(f"OpenGL {gl.version}; renderer: {gl.renderer}",flush=True)
            runner=ctypes.CDLL(str(library))
            runner.atmosphere_runtime_tests.argtypes=[ctypes.c_char_p]
            runner.atmosphere_runtime_tests.restype=ctypes.c_int
            result=runner.atmosphere_runtime_tests(str(ROOT).encode())
            if result: raise SystemExit(result)


if __name__=="__main__":
    main()
