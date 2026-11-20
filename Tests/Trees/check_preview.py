#!/usr/bin/env python3
"""Exercise production tree preview resources/shaders in desktop EGL.

Requires Linux libGL, libEGL, a C++17 compiler, and either installed GLEW 2.x or
--glew-object pointing to a PIC object. No desktop/X11 development headers are
needed when using the installed library. Software GL verifies correctness only.
"""
import argparse
import ctypes
import ctypes.util
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

os.environ.setdefault("MESA_GL_VERSION_OVERRIDE","4.6")
os.environ.setdefault("MESA_GLSL_VERSION_OVERRIDE","460")
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/"Tests/Terrain"))
from gl_headless import GL

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--glew-object",type=Path)
    parser.add_argument("--fast-math",action="store_true")
    parser.add_argument("--captures",type=Path,help="Write four preset reference captures to this directory")
    args=parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="endurance-tree-preview-") as temporary:
        directory=Path(temporary); (directory/"Bin").mkdir()
        shaders=directory/"ENDURANCE/Ressources/Shader/TreeLab"
        shutil.copytree(ROOT/"ENDURANCE/Ressources/Shader/TreeLab",shaders)
        library=directory/"preview.so"
        os.environ["ENDURANCE_SHADER_CACHE_DIR"]=str(directory/"shader-cache")
        if args.captures:
            args.captures.mkdir(parents=True,exist_ok=True)
            os.environ["ENDURANCE_TREE_TEST_CAPTURE_DIR"]=str(args.captures.resolve())
        command=[os.environ.get("CXX","c++"),"-std=c++17","-O2","-fPIC","-shared","-DGLEW_NO_GLU","-pthread"]
        if args.fast_math: command.append("-ffast-math")
        for include in ("ThirdParty","ThirdParty/glm","ANTAGONIZ/Header/ProjectUser","ANTAGONIZ/Header/Struct","ANTAGONIZ/Header/Rendering/Shader"):
            command.extend(["-I",str(ROOT/include)])
        for source in ("Tests/Trees/check_preview.cpp","ANTAGONIZ/Source/ProjectUser/ProceduralTree.cpp","ANTAGONIZ/Source/ProjectUser/TreeLabPreview.cpp","ANTAGONIZ/Source/Rendering/Shader/ShaderCompilation.cpp","ANTAGONIZ/Source/Rendering/Shader/ShaderBinaryCache.cpp"):
            command.append(str(ROOT/source))
        if args.glew_object: command.append(str(args.glew_object.resolve()))
        else:
            glew=ctypes.util.find_library("GLEW")
            if not glew: raise RuntimeError("GLEW unavailable; supply --glew-object")
            command.append("-l:"+glew)
        command.extend(["-l:libGL.so.1","-o",str(library)])
        subprocess.run(command,check=True)
        with GL() as gl:
            print(f"OpenGL {gl.version}; {gl.renderer}",flush=True)
            runner=ctypes.CDLL(str(library)); runner.tree_preview_tests.restype=ctypes.c_int
            previous=Path.cwd()
            try:
                os.chdir(directory/"Bin")
                result=runner.tree_preview_tests()
                if args.captures:
                    from PIL import Image
                    for source in args.captures.glob("*.ppm"):
                        Image.open(source).save(source.with_suffix(".png"))
                        source.unlink()
                return result
            finally: os.chdir(previous)

if __name__=="__main__": raise SystemExit(main())
