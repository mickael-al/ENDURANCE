#!/usr/bin/env python3
"""Exercise the real oil paint shader and postprocess routing in headless OpenGL.

Window/editor/camera services and disabled fog are doubled; PostProcess, Bloom,
DepthOfField, OilPainting, ColorTint and shader compilation are production code.
"""
import ctypes
import os
from pathlib import Path
import subprocess
import tempfile

from check_bloom import ROOT, SERVICES, AtmosphereGL


def main():
    services = dict(SERVICES)
    services.update({
        "Camera.hpp": '''#pragma once
#include <glm/gtc/matrix_transform.hpp>
namespace Ge { class Camera { public:
 glm::mat4 getProjectionMatrix() const { return glm::perspective(glm::radians(70.f),2.f,.1f,1000.f); }
 bool getOrtho() const { return false; } float getFieldOfView() const { return 70; }
}; }''',
        "GraphicsDataMisc.hpp": '''#pragma once
#include "Camera.hpp"
struct GraphicsDataMisc { unsigned str_width=64,str_height=32; Ge::Camera* current_camera=nullptr; };''',
        "PointeurClass.hpp": '''#pragma once
namespace Ge { class GraphiquePipelineManager; }
struct TestSettings { float gamma=2.2f; float* getGammaAddr() { return &gamma; } };
struct ptrClass { Ge::GraphiquePipelineManager* graphiquePipelineManager=nullptr; TestSettings* settingManager=nullptr; };''',
        "Engine.hpp": '''#pragma once
#include "PointeurClass.hpp"
namespace Ge { class Engine { public: static inline ptrClass pointers;
 static const ptrClass& getPtrClass() { return pointers; }
 static const ptrClass* getPtrClassAddr() { return &pointers; }
}; }''',
        "Debug.hpp": '''#pragma once
#include <iostream>
#include "imgui-cmake/Header/imgui.h"
namespace Debug { inline void Error(const char* m) { std::cerr<<m<<"\\n"; } }''',
        "imgui-cmake/Header/imgui.h": '''#pragma once
namespace ImGui {
 inline bool TreeNodeEx(const char*) { return false; }
 inline bool Checkbox(const char*,bool*) { return false; }
 inline bool DragFloat(const char*,float*,float=1,float=0,float=0) { return false; }
 inline bool SliderFloat(const char*,float*,float,float) { return false; }
 inline bool SliderInt(const char*,int*,int,int) { return false; }
 inline bool ColorEdit3(const char*,float*) { return false; }
 inline void TextWrapped(const char*) {} inline void TextUnformatted(const char*) {}
 inline void TreePop() {}
}''',
    })
    for name in ("Fog", "VolumetricFog"):
        services[name + ".hpp"] = '''#pragma once
#include "ModulePP.hpp"
namespace Ge { class NAME final : public ModulePP { public:
 bool initialize(GraphicsDataMisc*) override { return true; } void release() override {}
 void resize(int,int) override {} void onGui(PPSetting*,PPSetting*) override {}
 void compute(unsigned,unsigned,unsigned,ShapeBuffer*,PPSetting*) override {}
}; }'''.replace("NAME", name)
    with tempfile.TemporaryDirectory(prefix="endurance-oil-") as temporary:
        directory = Path(temporary)
        for name, text in services.items():
            path = directory / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(text)
        library = directory / "oil.so"
        command = [os.environ.get("CXX", "c++"), "-std=c++17", "-O2", "-Wall", "-Wextra",
                   "-Wno-sign-compare", "-fPIC", "-shared", "-DGLEW_NO_GLU", "-pthread", "-fvisibility=hidden"]
        for path in (directory, ROOT/"ANTAGONIZ/Header/Rendering/Shader", ROOT/"ANTAGONIZ/Header/Interface",
                     ROOT/"ANTAGONIZ/Header/Struct", ROOT/"ThirdParty", ROOT/"ThirdParty/glm"):
            command += ["-I", str(path)]
        command.append(str(ROOT / "Tests/Rendering/check_oil_painting.cpp"))
        for source in ("PostProcess", "OilPainting", "Bloom", "DepthOfField", "ColorTint",
                       "ShaderCompilation", "ShaderBinaryCache"):
            command.append(str(ROOT / "ANTAGONIZ/Source/Rendering/Shader" / (source + ".cpp")))
        command += ["-l:libGLEW.so.2.2", "-l:libGL.so.1", "-l:libEGL.so.1", "-ldl",
                    "-Wl,-z,defs", "-Wl,--wrap=glDrawElements", "-o", str(library)]
        subprocess.run(command, check=True)
        os.environ["ENDURANCE_SHADER_CACHE_DIR"] = str(directory / "cache")
        with AtmosphereGL() as gl:
            print(f"OpenGL {gl.version}; renderer: {gl.renderer}", flush=True)
            runner = ctypes.CDLL(str(library))
            runner.oil_runtime_tests.argtypes = [ctypes.c_char_p]
            runner.oil_runtime_tests.restype = ctypes.c_int
            result = runner.oil_runtime_tests(str(ROOT).encode())
            if result:
                raise SystemExit(result)


if __name__ == "__main__":
    main()
