#!/usr/bin/env python3
"""Exercise real terrain emission and object pools in desktop OpenGL/EGL."""
import argparse
import ctypes
import ctypes.util
import os
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/"Tests/Terrain"))
from gl_headless import GL

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fast-math",action="store_true")
    args=parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="endurance-placement-") as temporary:
        library=Path(temporary)/"placement.so"
        # Share binaries between invocations, while the production cache still
        # keys every shader/include/define and the current driver identity.
        os.environ.setdefault("ENDURANCE_SHADER_CACHE_DIR",str(Path(tempfile.gettempdir())/"endurance-placement-shaders"))
        command=[os.environ.get("CXX","c++"),"-std=c++17","-O2","-fPIC","-shared","-DGLEW_NO_GLU","-pthread"]
        if args.fast_math: command.append("-ffast-math")
        for path in ("ThirdParty","ThirdParty/glm","ANTAGONIZ/Header/Struct","ANTAGONIZ/Header/Miscellaneous","ANTAGONIZ/Header/ProjectUser","ANTAGONIZ/Header/Rendering/Shader"):
            command.extend(["-I",str(ROOT/path)])
        for path in ("Tests/Trees/check_placement.cpp","ANTAGONIZ/Source/Miscellaneous/ProceduralTerrainGpu.cpp",
            "ANTAGONIZ/Source/ProjectUser/EnduranceObjectPlacement.cpp","ANTAGONIZ/Source/Rendering/Shader/ShaderCompilation.cpp",
            "ANTAGONIZ/Source/Rendering/Shader/ShaderBinaryCache.cpp","ANTAGONIZ/Source/Rendering/Shader/ShaderProgramBatch.cpp",
            "ANTAGONIZ/Source/Rendering/Shader/ParallelShaderCompiler.cpp","ANTAGONIZ/Source/Rendering/Shader/ComputeShader.cpp"):
            command.append(str(ROOT/path))
        glew=ctypes.util.find_library("GLEW")
        if not glew: raise RuntimeError("Install GLEW to run the desktop GL placement tests")
        command.extend(["-l:"+glew,"-l:libGL.so.1","-l:libEGL.so.1","-o",str(library)])
        subprocess.run(command,check=True)
        with GL() as gl:
            print(f"OpenGL {gl.version}; {gl.renderer}",flush=True)
            runner=ctypes.CDLL(str(library)); runner.object_placement_tests.argtypes=[ctypes.c_char_p,ctypes.c_char_p]
            runner.object_placement_tests.restype=ctypes.c_int
            return runner.object_placement_tests(str(ROOT/"Asset/Shader/ProceduralTerrain").encode(),
                str(ROOT/"ENDURANCE/Ressources/Trees/object_placement.json").encode())

if __name__=="__main__": raise SystemExit(main())
