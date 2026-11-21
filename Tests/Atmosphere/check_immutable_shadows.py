#!/usr/bin/env python3
"""CPU regression for production immutable-caster snapshots (no graphics context)."""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[2]

VAO_GL=r'''#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
constexpr unsigned GL_ELEMENT_ARRAY_BUFFER=1,GL_ARRAY_BUFFER=2,GL_STATIC_DRAW=3,GL_FLOAT=4,GL_TRUE=1;
namespace FixtureVAO { inline bool alive=false; inline unsigned nextBuffer=1,positionLocation=0; }
inline void glGenBuffers(int n,unsigned* ids) { for(int i=0;i<n;++i) ids[i]=FixtureVAO::nextBuffer++; }
inline void glBindBuffer(unsigned,unsigned) {}
inline void glBufferData(unsigned,std::ptrdiff_t,const void*,unsigned) {}
inline void glDeleteBuffers(int,const unsigned*) {}
inline unsigned glIsVertexArray(unsigned id) { return FixtureVAO::alive && id==7; }
inline void glDeleteVertexArrays(int,const unsigned*) { FixtureVAO::alive=false; }
inline void glGenVertexArrays(int,unsigned* id) { *id=7; FixtureVAO::alive=true; }
inline void glBindVertexArray(unsigned) {}
inline int glGetAttribLocation(unsigned program,const char* name)
{ return std::strcmp(name,"a_Position")==0 ? (program==1 ? 0 : 3) : -1; }
inline void glEnableVertexAttribArray(unsigned) {}
inline void glVertexAttribPointer(unsigned index,int,unsigned,bool,int,const void*) { FixtureVAO::positionLocation=index; }
'''

def main():
    with tempfile.TemporaryDirectory(prefix="endurance-immutable-shadows-") as directory:
        for flags,name in ((["-O2"],"normal"),(["-O3","-ffast-math"],"fast-math")):
            binary=Path(directory)/name
            subprocess.run([os.environ.get("CXX","c++"),"-std=c++17","-Wall","-Wextra","-Werror",
                *flags,"-I",str(ROOT/"ANTAGONIZ/Header/Interface"),"-I",str(ROOT/"ThirdParty/glm"),
                str(ROOT/"Tests/Atmosphere/check_immutable_shadows.cpp"),"-o",str(binary)],check=True)
            subprocess.run([str(binary)],check=True)
        # Use production scene structs/serializer; only GObject ownership is
        # omitted, since no editor/window is needed for serialized scene data.
        (Path(directory)/"GObject.hpp").write_text("#pragma once\n#include <glm/gtc/quaternion.hpp>\nnamespace Ge { class GObject; }\n")
        binary=Path(directory)/"scene"
        includes=[Path(directory),ROOT/"ANTAGONIZ/Header/Miscellaneous",ROOT/"ANTAGONIZ/Header/Rendering/Shader",
                  ROOT/"ANTAGONIZ/Header/Interface",ROOT/"ThirdParty",ROOT/"ThirdParty/glm"]
        command=[os.environ.get("CXX","c++"),"-std=c++17","-O2","-Wall","-Wextra"]
        for path in includes: command.extend(["-I",str(path)])
        subprocess.run([*command,str(ROOT/"Tests/Atmosphere/check_immutable_scene.cpp"),"-o",str(binary)],check=True)
        subprocess.run([str(binary),str(ROOT/"ENDURANCE/Scene/Terrain.scene")],check=True)
        # Execute production SetupVAO with recycled GL names, without a context.
        (Path(directory)/"glcore.hpp").write_text(VAO_GL)
        (Path(directory)/"Debug.hpp").write_text("#pragma once\n")
        includes=[Path(directory),ROOT/"ANTAGONIZ/Header/Rendering/Shape",ROOT/"ANTAGONIZ/Header/Struct",
                  ROOT/"ANTAGONIZ/Header/Interface",ROOT/"ThirdParty/glm"]
        for flags,name in ((["-O2"],"vao"),(["-O3","-ffast-math"],"vao-fast-math")):
            binary=Path(directory)/name
            command=[os.environ.get("CXX","c++"),"-std=c++17",*flags,"-Wall","-Wextra",
                     "-Wno-sign-compare","-Wno-unused-parameter"]
            for path in includes: command.extend(["-I",str(path)])
            subprocess.run([*command,str(ROOT/"Tests/Atmosphere/check_shadow_vao.cpp"),
                            str(ROOT/"ANTAGONIZ/Source/Rendering/Shape/ShapeBufferBase.cpp"),"-o",str(binary)],check=True)
            subprocess.run([str(binary)],check=True)

if __name__=="__main__": main()
