#!/usr/bin/env python3
"""Production camera/misc/pipeline methods on GL, with only game services stubbed."""
import argparse
import ctypes
import os
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "Tests/Atmosphere"))
from gl_support import AtmosphereGL

SERVICES = {
    "GraphicsDataMisc.hpp": r'''#pragma once
namespace Ge { class Camera; }
using namespace Ge;
struct GraphicsDataMisc {
    struct { unsigned str_camera=0, str_misc=0; } str_ssbo;
    struct { int lightCount=3; } str_dataMisc;
    unsigned str_width=1280,str_height=720;
    Ge::Camera* current_camera=nullptr;
};''',
    "GObject.hpp": r'''#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <string>
namespace Ge { class GObject { public:
 virtual ~GObject()=default;
 virtual void mapMemory() {} virtual void onGUI() {}
 glm::mat4 getModelMatrix() const { return glm::translate(glm::mat4(1),m_transform.position); }
 void setPosition(glm::vec3 value) { m_transform.position=value; mapMemory(); }
 void setName(const std::string&) {}
 protected: struct { glm::vec3 position{0}; } m_transform;
}; }''',
    "FlyCamera.hpp": r'''#pragma once
#include "Camera.hpp"
namespace Ge { class FlyCamera : public Camera { public:
 FlyCamera(GraphicsDataMisc* d):Camera(d,1) {} void updateCamera() {}
}; }''',
    "Time.hpp": r'''#pragma once
namespace Ge { class Time { public: float value=0; float getTime() const { return value; } }; }''',
    "SettingManager.hpp": r'''#pragma once
namespace Ge { class SettingManager { public:
 float getGamma() const { return 2.2f; } float getAmbient() const { return 0.6f; }
}; }''',
    "LightManager.hpp": r'''#pragma once
namespace Ge { class LightManager { public: unsigned updates=0; void updateShadowCascadeMatrix() { ++updates; } }; }''',
    "PointeurClass.hpp": r'''#pragma once
namespace Ge { class Time; class SettingManager; class LightManager; }
struct ptrClass { Ge::Time* time=nullptr; Ge::SettingManager* settingManager=nullptr; Ge::LightManager* lightManager=nullptr; };''',
    "Engine.hpp": r'''#pragma once
#include "PointeurClass.hpp"
namespace Ge { class Engine { public:
 static inline ptrClass pointers;
 static ptrClass getPtrClass() { return pointers; }
 static const ptrClass* getPtrClassAddr() { return &pointers; }
}; }''',
    "Debug.hpp": r'''#pragma once
namespace Debug { template<class... A> void INITSUCCESS(A...) {} template<class... A> void RELEASESUCCESS(A...) {} }
''',
    "imgui-cmake/Header/imgui.h": r'''#pragma once
struct ImVec4 { ImVec4(float,float,float,float) {} };
namespace ImGui {
 inline void TextColored(ImVec4,const char*) {}
 inline bool DragFloat(const char*,float*,float,float=0,float=0) { return false; }
 inline bool DragInt(const char*,int*,float) { return false; }
 inline bool Checkbox(const char*,bool*) { return false; }
}''',
}


def build(library, fast_math):
    with tempfile.TemporaryDirectory(prefix="endurance-frame-services-") as directory:
        stub = Path(directory)
        for name, text in SERVICES.items():
            path = stub / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(text)
        cmd = [os.environ.get("CXX", "c++"), "-std=c++17", "-O3" if fast_math else "-O2",
               "-Wall", "-Wextra", "-Wno-sign-compare", "-fPIC", "-shared", "-DGLEW_NO_GLU",
               "-fvisibility=hidden", "-ffunction-sections", "-fdata-sections", "-include", "climits"]
        if fast_math:
            cmd.append("-ffast-math")
        for path in (stub, ROOT / "ANTAGONIZ/Header/Rendering/Camera",
                     ROOT / "ANTAGONIZ/Header/Rendering/Light",
                     ROOT / "ANTAGONIZ/Header/Rendering/Shader", ROOT / "ANTAGONIZ/Header/Struct",
                     ROOT / "ANTAGONIZ/Header/Interface", ROOT / "ThirdParty", ROOT / "ThirdParty/glm"):
            cmd += ["-I", str(path)]
        sources = ["Tests/Rendering/check_frame_uploads.cpp", "ANTAGONIZ/Source/Rendering/Camera/Camera.cpp",
                   "ANTAGONIZ/Source/Rendering/Camera/CameraManager.cpp",
                   "ANTAGONIZ/Source/Rendering/Shader/ShaderDataMisc.cpp",
                   "ANTAGONIZ/Source/Rendering/Shader/GraphiquePipeline.cpp"]
        cmd += [str(ROOT / path) for path in sources]
        cmd += ["-l:libGLEW.so.2.2", "-l:libGL.so.1", "-l:libEGL.so.1", "-ldl",
                "-Wl,--gc-sections", "-Wl,-z,defs", "-o", str(library)]
        subprocess.run(cmd, check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--library", type=Path)
    parser.add_argument("--compile-only", action="store_true")
    parser.add_argument("--fast-math", action="store_true")
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="endurance-frame-tests-") as directory:
        library = args.library.resolve() if args.library else Path(directory) / "frame.so"
        if args.compile_only or not library.exists():
            library.parent.mkdir(parents=True, exist_ok=True)
            build(library, args.fast_math)
        if args.compile_only:
            return 0
        with AtmosphereGL() as gl:
            print(f"OpenGL {gl.version}; renderer: {gl.renderer}", flush=True)
            tests = ctypes.CDLL(str(library))
            tests.frame_upload_tests.restype = ctypes.c_int
            return tests.frame_upload_tests()


if __name__ == "__main__":
    raise SystemExit(main())
