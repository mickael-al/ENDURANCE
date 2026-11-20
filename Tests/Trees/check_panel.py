#!/usr/bin/env python3
"""Run the real asynchronous Tree Lab panel in a disposable desktop EGL context.

Requires C/C++17 compilers, Linux GL/EGL, GLEW2.x or --glew-object. Tests never
write project resources; only the scene/HUD registration methods are stubbed.
"""
import argparse
import ctypes
import ctypes.util
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import zlib

os.environ.setdefault("MESA_GL_VERSION_OVERRIDE","4.6")
os.environ.setdefault("MESA_GLSL_VERSION_OVERRIDE","460")
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/"Tests/Terrain"))
from gl_headless import GL

def mask_png(path,alpha):
    width,height=7,11
    def pixel(x,y):
        v=255 if x+y>5 and x+y<12 else 0
        return bytes((255,255,255,v) if alpha else (v,v,v,255))
    pixels=b"".join(b"\0"+b"".join(pixel(x,y) for x in range(width)) for y in range(height))
    def chunk(name,data):
        return struct.pack(">I",len(data))+name+data+struct.pack(">I",zlib.crc32(name+data)&0xffffffff)
    path.write_bytes(b"\x89PNG\r\n\x1a\n"+chunk(b"IHDR",struct.pack(">IIBBBBB",width,height,8,6,0,0,0))+
                     chunk(b"IDAT",zlib.compress(pixels))+chunk(b"IEND",b""))

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--glew-object",type=Path)
    parser.add_argument("--fast-math",action="store_true")
    args=parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="endurance-tree-panel-") as temporary:
        directory=Path(temporary); (directory/"Bin").mkdir()
        shutil.copytree(ROOT/"ENDURANCE/Ressources/Shader/TreeLab",directory/"ENDURANCE/Ressources/Shader/TreeLab")
        mask_png(directory/"mask-alpha.png",True); mask_png(directory/"mask-opaque.png",False)
        os.environ["ENDURANCE_SHADER_CACHE_DIR"]=str(directory/"shader-cache")
        stb=directory/"stb.o"
        subprocess.run([os.environ.get("CC","cc"),"-O2","-fPIC","-I",str(ROOT/"ThirdParty/stb-cmake"),"-c",
                        str(ROOT/"ThirdParty/stb-cmake/stb_shared.c"),"-o",str(stb)],check=True)
        library=directory/"panel.so"
        command=[os.environ.get("CXX","c++"),"-std=c++17","-O2","-fPIC","-shared","-DGLEW_NO_GLU","-pthread"]
        if args.fast_math: command.append("-ffast-math")
        includes=["ThirdParty","ThirdParty/glm","ThirdParty/imgui-cmake/Header"]
        includes += ["ANTAGONIZ/Header/"+name for name in
                     ("ProjectUser","User","Struct","Interface","Miscellaneous","Rendering/Shader")]
        for include in includes: command.extend(["-I",str(ROOT/include)])
        sources=["Tests/Trees/check_panel.cpp"]
        sources += ["ANTAGONIZ/Source/ProjectUser/"+name+".cpp" for name in
                    ("ProceduralTree","ProceduralTreeBake","TreeLabPreview")]
        sources += ["ANTAGONIZ/Source/Rendering/Shader/"+name+".cpp" for name in
                    ("ShaderCompilation","ShaderBinaryCache")]
        sources += ["ThirdParty/imgui-cmake/Source/"+name+".cpp" for name in
                    ("imgui","imgui_draw","imgui_widgets","imgui_tables")]
        command += [str(ROOT/source) for source in sources]
        command.append(str(stb))
        if args.glew_object: command.append(str(args.glew_object.resolve()))
        else:
            glew=ctypes.util.find_library("GLEW")
            if not glew: raise RuntimeError("GLEW unavailable; supply --glew-object")
            command.append("-l:"+glew)
        command.extend(["-l:libGL.so.1","-o",str(library)])
        subprocess.run(command,check=True)
        with GL() as gl:
            print(f"OpenGL {gl.version}; {gl.renderer}",flush=True)
            runner=ctypes.CDLL(str(library)); runner.tree_panel_tests.restype=ctypes.c_int
            previous=Path.cwd()
            try:
                os.chdir(directory/"Bin"); return runner.tree_panel_tests()
            finally: os.chdir(previous)

if __name__=="__main__": raise SystemExit(main())
