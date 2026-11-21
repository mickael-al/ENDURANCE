#!/usr/bin/env python3
"""Exercise the production instanced tree renderer on real baked assets in EGL.

Readbacks/waits are test instrumentation only; this is not a hardware benchmark.
"""
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
os.environ.setdefault("MESA_GL_VERSION_OVERRIDE","4.6")
os.environ.setdefault("MESA_GLSL_VERSION_OVERRIDE","460")

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fast-math",action="store_true")
    parser.add_argument("--captures",type=Path)
    args=parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="endurance-tree-runtime-") as temporary:
        directory=Path(temporary); library=directory/"runtime.so"
        os.environ.setdefault("ENDURANCE_SHADER_CACHE_DIR",str(Path(tempfile.gettempdir())/"endurance-tree-runtime-shaders"))
        if args.captures:
            args.captures.mkdir(parents=True,exist_ok=True)
            os.environ["ENDURANCE_TREE_TEST_CAPTURE_DIR"]=str(args.captures.resolve())
        stb=directory/"stb.o"
        subprocess.run([os.environ.get("CC","cc"),"-O2","-fPIC","-c",str(ROOT/"ThirdParty/stb-cmake/stb_shared.c"),"-o",str(stb)],check=True)
        command=[os.environ.get("CXX","c++"),"-std=c++17","-O2","-fPIC","-shared","-DGLEW_NO_GLU","-pthread"]
        if args.fast_math: command.append("-ffast-math")
        for path in ("ThirdParty","ThirdParty/glm","ANTAGONIZ/Header/Struct","ANTAGONIZ/Header/Miscellaneous",
                     "ANTAGONIZ/Header/Interface","ANTAGONIZ/Header/ProjectUser","ANTAGONIZ/Header/Rendering/Shader"):
            command.extend(["-I",str(ROOT/path)])
        for path in ("Tests/Trees/check_runtime.cpp","ANTAGONIZ/Source/ProjectUser/TreeRenderGpu.cpp",
                     "ANTAGONIZ/Source/ProjectUser/ProceduralTree.cpp","ANTAGONIZ/Source/ProjectUser/ProceduralTreeBake.cpp",
                     "ANTAGONIZ/Source/Rendering/Shader/ShaderCompilation.cpp","ANTAGONIZ/Source/Rendering/Shader/ShaderBinaryCache.cpp"):
            command.append(str(ROOT/path))
        glew=ctypes.util.find_library("GLEW")
        if not glew: raise RuntimeError("Install GLEW to run the desktop GL tree tests")
        command.extend([str(stb),"-l:"+glew,"-l:libGL.so.1","-o",str(library)])
        subprocess.run(command,check=True)
        with GL() as gl:
            print(f"OpenGL {gl.version}; {gl.renderer}",flush=True)
            runner=ctypes.CDLL(str(library)); runner.tree_runtime_tests.argtypes=[ctypes.c_char_p,ctypes.c_char_p]
            runner.tree_runtime_tests.restype=ctypes.c_int
            result=runner.tree_runtime_tests(str(ROOT).encode(),str(directory).encode())
            if args.captures:
                from PIL import Image
                for source in args.captures.glob("*.ppm"):
                    Image.open(source).save(source.with_suffix(".png")); source.unlink()
            return result

if __name__=="__main__": raise SystemExit(main())
