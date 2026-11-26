#!/usr/bin/env python3
"""Compile production Bloom and compare fixed modes against generic GLSL paths."""
import argparse
import ctypes
import os
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/"Tests/Atmosphere"))
from gl_support import AtmosphereGL

SERVICES={
    "GraphicsDataMisc.hpp": r'''#pragma once
struct GraphicsDataMisc { unsigned str_width=64,str_height=32; };''',
    "PointeurClass.hpp": r'''#pragma once
namespace Ge { class GraphiquePipelineManager; }
struct ptrClass { Ge::GraphiquePipelineManager* graphiquePipelineManager=nullptr; };''',
    "Engine.hpp": r'''#pragma once
#include "PointeurClass.hpp"
namespace Ge { class Engine { public: static inline ptrClass pointers;
 static const ptrClass& getPtrClass() { return pointers; }
}; }''',
    "Debug.hpp": r'''#pragma once
#include <iostream>
namespace Debug { inline void Error(const char* message) { std::cerr<<message<<"\n"; } }
namespace ImGui {
 inline bool TreeNodeEx(const char*) { return false; }
 inline bool Checkbox(const char*,bool*) { return false; }
 inline bool DragFloat(const char*,float*,float) { return false; }
 inline void TreePop() {}
}''',
    "ShapeBuffer.hpp": r'''#pragma once
namespace Ge { class ShapeBuffer { public: unsigned vao=0,ibo=0;
 unsigned getVAO() const { return vao; } unsigned getIBO() const { return ibo; }
 unsigned getIndiceSize() const { return 6; }
}; }''',
    "GraphiquePipeline.hpp": r'''#pragma once
#include "glcore.hpp"
namespace Ge { class GraphiquePipeline { public:
 explicit GraphiquePipeline(unsigned p):program(p) {} ~GraphiquePipeline() { if(program) glDeleteProgram(program); }
 unsigned getProgram() const { return program; }
 private: unsigned program;
}; }''',
    "GraphiquePipelineManager.hpp": r'''#pragma once
#include "GraphiquePipeline.hpp"
#include "ShaderCompilation.hpp"
#include <iostream>
#include <vector>
#include <algorithm>
namespace Ge { class GraphiquePipelineManager { public:
 std::vector<GraphiquePipeline*> owned;
 GraphiquePipeline* createPipeline(const std::string& frag,const std::string& vert,bool=false,bool=true,bool=false,int=1,
 const std::string& defines={},const ShaderCacheOptions& cache={}) {
 std::string error; auto id=buildShaderProgram({{GL_VERTEX_SHADER,vert,defines},{GL_FRAGMENT_SHADER,frag,defines}},error,"Bloom test",cache);
 if(!id) std::cerr<<error<<"\n";
 auto* p=new GraphiquePipeline(id); owned.push_back(p); return p; }
 void destroyPipeline(GraphiquePipeline* p) { owned.erase(std::remove(owned.begin(),owned.end(),p),owned.end()); delete p; }
}; }'''
}

def build(library):
    with tempfile.TemporaryDirectory(prefix="endurance-bloom-services-") as temporary:
        stubs=Path(temporary)
        for name,text in SERVICES.items(): (stubs/name).write_text(text)
        cmd=[os.environ.get("CXX","c++"),"-std=c++17","-O2","-Wall","-Wextra","-Wno-sign-compare",
             "-fPIC","-shared","-DGLEW_NO_GLU","-pthread","-fvisibility=hidden"]
        for path in (stubs,ROOT/"ANTAGONIZ/Header/Rendering/Shader",ROOT/"ANTAGONIZ/Header/Interface",
                     ROOT/"ANTAGONIZ/Header/Struct",ROOT/"ThirdParty",ROOT/"ThirdParty/glm"):
            cmd += ["-I",str(path)]
        for source in ("Tests/Rendering/check_bloom.cpp","ANTAGONIZ/Source/Rendering/Shader/Bloom.cpp",
                       "ANTAGONIZ/Source/Rendering/Shader/ShaderCompilation.cpp","ANTAGONIZ/Source/Rendering/Shader/ShaderBinaryCache.cpp"):
            cmd.append(str(ROOT/source))
        cmd += ["-l:libGLEW.so.2.2","-l:libGL.so.1","-l:libEGL.so.1","-ldl","-Wl,-z,defs","-o",str(library)]
        subprocess.run(cmd,check=True)

def main():
    parser=argparse.ArgumentParser(); parser.add_argument("--compile-only",action="store_true"); args=parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="endurance-bloom-") as temporary:
        directory=Path(temporary); library=directory/"bloom.so"; build(library)
        if args.compile_only:
            print("PASS production Bloom C++ compile/link"); return
        os.environ["ENDURANCE_SHADER_CACHE_DIR"]=str(directory/"cache")
        with AtmosphereGL() as gl:
            print(f"OpenGL {gl.version}; renderer: {gl.renderer}",flush=True)
            runner=ctypes.CDLL(str(library)); runner.bloom_runtime_tests.argtypes=[ctypes.c_char_p]
            runner.bloom_runtime_tests.restype=ctypes.c_int
            result=runner.bloom_runtime_tests(str(ROOT).encode())
            if result: raise SystemExit(result)

if __name__=="__main__": main()
