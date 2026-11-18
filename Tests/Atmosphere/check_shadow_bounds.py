#!/usr/bin/env python3
"""Test production Lights/LightManager and CPU caster fitting, without a GPU.

Only GL storage, camera/transform services and GUI are replaced. Light creation,
shadow matrices, publication cache and bias setters are production code.
"""
import os
import importlib.util
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]

GL = r'''
#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <unordered_map>
#include <vector>
using GLuint=unsigned; using GLint64=std::int64_t;
constexpr unsigned GL_SHADER_STORAGE_BUFFER=1, GL_BUFFER_SIZE=2, GL_DYNAMIC_DRAW=3,
 GL_TEXTURE_2D_ARRAY=4, GL_DEPTH_COMPONENT32=5, GL_DEPTH_COMPONENT=6, GL_FLOAT=7,
 GL_TEXTURE_MIN_FILTER=8, GL_TEXTURE_MAG_FILTER=9, GL_NEAREST=10,
 GL_TEXTURE_WRAP_S=11, GL_TEXTURE_WRAP_T=12, GL_CLAMP_TO_EDGE=13,
 GL_FRAMEBUFFER=14, GL_DEPTH_ATTACHMENT=15, GL_NONE=0, GL_FRAMEBUFFER_COMPLETE=16;
namespace FixtureGL {
inline unsigned next=1, buffer=0, textureAllocations=0;
inline std::unordered_map<unsigned,std::vector<unsigned char>> buffers;
inline std::unordered_map<unsigned,unsigned> writes;
}
inline void glGenBuffers(int n,unsigned* p){while(n--)*p++=FixtureGL::next++;}
inline void glBindBuffer(unsigned,unsigned p){FixtureGL::buffer=p;}
inline void glBufferData(unsigned,std::size_t size,const void* p,unsigned){
 auto& b=FixtureGL::buffers[FixtureGL::buffer];b.resize(size);if(p)std::memcpy(b.data(),p,size);
 ++FixtureGL::writes[FixtureGL::buffer];}
inline void glBufferSubData(unsigned,std::size_t offset,std::size_t size,const void* p){
 auto& b=FixtureGL::buffers[FixtureGL::buffer];
 if(offset+size>b.size())throw std::runtime_error("Production buffer upload exceeds storage");
 std::memcpy(b.data()+offset,p,size);++FixtureGL::writes[FixtureGL::buffer];}
inline void glGetBufferParameteri64v(unsigned,unsigned,std::int64_t* size){*size=FixtureGL::buffers[FixtureGL::buffer].size();}
inline void glDeleteBuffers(int n,const unsigned* p){while(n--)FixtureGL::buffers.erase(*p++);}
inline void glGenTextures(int n,unsigned* p){FixtureGL::textureAllocations+=n;glGenBuffers(n,p);}
inline void glDeleteTextures(int,const unsigned*){}
inline void glBindTexture(unsigned,unsigned){}
inline void glTexImage3D(unsigned,int,unsigned,int,int,int,int,unsigned,unsigned,const void*){}
inline void glTexParameteri(unsigned,unsigned,int){}
inline void glGenFramebuffers(int n,unsigned* p){glGenBuffers(n,p);}
inline void glDeleteFramebuffers(int,const unsigned*){}
inline void glBindFramebuffer(unsigned,unsigned){}
inline void glFramebufferTextureLayer(unsigned,unsigned,unsigned,int,int){}
inline void glDrawBuffer(unsigned){} inline void glReadBuffer(unsigned){}
inline unsigned glCheckFramebufferStatus(unsigned){return GL_FRAMEBUFFER_COMPLETE;}
'''

GOBJECT = r'''
#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <string>
struct Transform {glm::vec3 position{0};};
namespace Ge {
class GObject {
public:
 virtual ~GObject()=default; virtual void mapMemory()=0; virtual void onGUI(){}
 void setName(std::string){}
 void setPosition(glm::vec3 p){m_transform.position=p;mapMemory();}
 void setEulerAngles(glm::vec3 angles){direction=glm::quat(glm::radians(angles))*glm::vec3(0,0,-1);mapMemory();}
 glm::vec3 getDirection() const{return direction;}
protected: Transform m_transform; glm::vec3 direction{0,0,-1};
};
}
'''

GRAPHICS = r'''
#pragma once
namespace Ge {class Camera;}
using namespace Ge;
struct GraphicsDataMisc {
 struct {unsigned str_light=0,str_shadow=0,str_planeHidden=0;} str_ssbo;
 struct {unsigned lightCount=0;} str_dataMisc;
 Ge::Camera* current_camera=nullptr;
};
'''

CAMERA = r'''
#pragma once
#include "LightManager.hpp"
#include <glm/gtc/matrix_transform.hpp>
namespace Ge {
class Camera {
public:
 glm::vec3 position{0}; float nearPlane=.1f,farPlane=8192.0f;
 float getNear() const{return nearPlane;} float getFar() const{return farPlane;}
 glm::mat4 getProjectionMatrix() const{return glm::perspective(glm::radians(70.0f),1.7f,nearPlane,farPlane);}
 glm::mat4 getViewMatrix() const{return glm::translate(glm::mat4(1),-position);}
 void notify(LightManager& manager){manager.updateShadowCascadeMatrix();}
};
}
'''

ENGINE = r'''
#pragma once
namespace Ge {class LightManager;}
struct ptrClass {Ge::LightManager* lightManager=nullptr;};
namespace Ge {class Engine {public:static ptrClass* getPtrClassAddr(){static ptrClass pc;return &pc;}};}
'''

IMGUI = r'''
#pragma once
struct ImVec4 {ImVec4(float,float,float,float){}};
namespace ImGui {
template<class... T>void TextColored(T...){}
template<class... T>bool Checkbox(T...){return false;}
template<class... T>bool ColorEdit3(T...){return false;}
template<class... T>bool DragFloat(T...){return false;}
template<class... T>bool DragFloat2(T...){return false;}
template<class... T>bool DragFloat3(T...){return false;}
template<class... T>bool TreeNode(T...){return false;}
template<class... T>bool Button(T...){return false;}
inline void PushID(int){} inline void PopID(){} inline void Separator(){} inline void TreePop(){}
}
'''


def main():
    with tempfile.TemporaryDirectory(prefix="endurance-shadow-bounds-") as directory:
        tmp = Path(directory)
        files = {
            "glcore.hpp": GL, "GObject.hpp": GOBJECT,
            "GraphicsDataMisc.hpp": GRAPHICS, "Camera.hpp": CAMERA,
            "Engine.hpp": ENGINE, "PointeurClass.hpp": '#include "Engine.hpp"\n',
            "Transform.hpp": '#include "GObject.hpp"\n', "Component.hpp": '#pragma once\n',
            "Debug.hpp": '#pragma once\nnamespace Debug {template<class T>void INITSUCCESS(T){} template<class T>void RELEASESUCCESS(T){} template<class T>void Error(T){}}\n',
            "imgui-cmake/Header/imgui.h": IMGUI,
        }
        for name, content in files.items():
            path = tmp / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(content)
        includes = [tmp, ROOT / "ANTAGONIZ/Header/Rendering/Light",
                    ROOT / "ANTAGONIZ/Header/Interface", ROOT / "ANTAGONIZ/Header/Struct",
                    ROOT / "ThirdParty/glm"]
        sources = [ROOT / "Tests/Atmosphere/check_shadow_bounds.cpp"]
        sources += [ROOT / "ANTAGONIZ/Source/Rendering/Light" / (name + ".cpp")
                    for name in ("Lights", "LightManager", "DirectionalLight", "PointLight", "SpotLight")]
        for flags, name in ((["-O2"], "normal"), (["-O3", "-ffast-math"], "fast-math")):
            binary = tmp / name
            command = [os.environ.get("CXX", "c++"), "-std=c++17", "-Wall", "-Wextra", "-Wno-sign-compare"] + flags
            for include in includes:
                command += ["-I", str(include)]
            subprocess.run(command + [str(path) for path in sources] + ["-o", str(binary)], check=True)
            print(f"Production shadow bounds: {name}", flush=True)
            subprocess.run([str(binary)], check=True)

        # Reuse the established terrain scheduler's doubles; verify its actual
        # publication hooks as well as the light-side cache tested above.
        spec = importlib.util.spec_from_file_location("terrain_scheduler_fixture", ROOT / "Tests/Terrain/check_scheduler.py")
        scheduler = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(scheduler)
        terrain_stub = tmp / "terrain-stubs"
        terrain_stub.mkdir()
        (terrain_stub / "glcore.hpp").write_text(scheduler.GL_STUB)
        (terrain_stub / "scheduler_engine.hpp").write_text(scheduler.ENGINE_STUB)
        for name in ("Engine", "PointeurClass", "Camera", "GraphiquePipeline", "ShaderPair"):
            (terrain_stub / (name + ".hpp")).write_text('#include "scheduler_engine.hpp"\n')
        command = [os.environ.get("CXX", "c++"), "-std=c++17", "-O3", "-ffast-math", "-pthread"]
        for include in (terrain_stub, ROOT / "ANTAGONIZ/Header/Miscellaneous",
                        ROOT / "ANTAGONIZ/Header/Interface", ROOT / "ThirdParty/glm", ROOT / "ThirdParty"):
            command += ["-I", str(include)]
        sources = [ROOT / "Tests/Atmosphere/check_shadow_terrain_publication.cpp"]
        sources += [ROOT / "ANTAGONIZ/Source/Miscellaneous" / (name + ".cpp")
                    for name in ("ProceduralGeneration", "ProceduralTerrainPlanner", "ProceduralTerrainStreaming", "ProceduralTerrainLod")]
        binary = tmp / "terrain-publication"
        subprocess.run(command + [str(path) for path in sources] + ["-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    main()
