#!/usr/bin/env python3
"""Build/run the C++ backend checks using a caller-supplied GLEW object or library."""
import argparse
import ctypes
import os
from pathlib import Path
import subprocess
import tempfile

from gl_headless import GL

ROOT=Path(__file__).resolve().parents[2]

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--glew-object",required=True,type=Path,help="PIC object, static archive or shared GLEW library")
    args=parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="terrain-backend-") as temporary:
        os.environ["ENDURANCE_SHADER_CACHE_DIR"] = str(Path(temporary)/"program-cache")
        library=Path(temporary)/"check_backend.so"
        command=[os.environ.get("CXX","c++"),"-std=c++17","-O3","-ffast-math","-fPIC","-shared","-DGLEW_NO_GLU",
                 "-I",str(ROOT/"ANTAGONIZ/Header/Struct"),"-I",str(ROOT/"ANTAGONIZ/Header/Miscellaneous"),
                 "-I",str(ROOT/"ANTAGONIZ/Header/Rendering/Shader"),
                 "-I",str(ROOT/"ThirdParty"),str(ROOT/"Tests/Terrain/check_backend.cpp"),
                 str(ROOT/"ANTAGONIZ/Source/Miscellaneous/ProceduralTerrainGpu.cpp"),
                 str(ROOT / "ANTAGONIZ/Source/Rendering/Shader/ShaderCompilation.cpp"),
                 str(ROOT / "ANTAGONIZ/Source/Rendering/Shader/ShaderBinaryCache.cpp"),
                 str(ROOT / "ANTAGONIZ/Source/Rendering/Shader/ShaderProgramBatch.cpp"),
                 str(ROOT / "ANTAGONIZ/Source/Rendering/Shader/ParallelShaderCompiler.cpp"),
                 str(ROOT / "ANTAGONIZ/Source/Rendering/Shader/ComputeShader.cpp"),
                 str(args.glew_object.resolve()),"-pthread","-l:libGL.so.1","-l:libEGL.so.1","-o",str(library)]
        subprocess.run(command,check=True)
        with GL() as gl:
            print(f"OpenGL {gl.version}; renderer: {gl.renderer}",flush=True)
            runner=ctypes.CDLL(str(library))
            runner.terrain_backend_tests.argtypes=[ctypes.c_char_p]
            runner.terrain_backend_tests.restype=ctypes.c_int
            return runner.terrain_backend_tests(str(ROOT/"Asset/Shader/ProceduralTerrain").encode())

if __name__=="__main__":
    raise SystemExit(main())
