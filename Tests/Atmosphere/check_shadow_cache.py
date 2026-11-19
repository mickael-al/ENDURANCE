#!/usr/bin/env python3
"""Exercise production LightManager/cache with the existing GL allocation doubles."""
import os
from pathlib import Path
import subprocess
import tempfile
import check_shadow_bounds as fixture

ROOT = Path(__file__).resolve().parents[2]

def main():
    with tempfile.TemporaryDirectory(prefix="endurance-shadow-cache-") as directory:
        tmp = Path(directory)
        files = {
            "glcore.hpp": fixture.GL, "GObject.hpp": fixture.GOBJECT,
            "GraphicsDataMisc.hpp": fixture.GRAPHICS, "Camera.hpp": fixture.CAMERA,
            "Engine.hpp": fixture.ENGINE, "PointeurClass.hpp": '#include "Engine.hpp"\n',
            "Transform.hpp": '#include "GObject.hpp"\n', "Component.hpp": '#pragma once\n',
            "Debug.hpp": '#pragma once\nnamespace Debug {template<class T>void INITSUCCESS(T){} template<class T>void RELEASESUCCESS(T){} template<class T>void Error(T){}}\n',
            "imgui-cmake/Header/imgui.h": fixture.IMGUI,
        }
        for name, content in files.items():
            path = tmp / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(content)
        includes = [tmp, ROOT / "ANTAGONIZ/Header/Rendering/Light",
                    ROOT / "ANTAGONIZ/Header/Interface", ROOT / "ANTAGONIZ/Header/Struct",
                    ROOT / "ThirdParty/glm"]
        sources = [ROOT / "Tests/Atmosphere/check_shadow_cache.cpp"]
        sources += [ROOT / "ANTAGONIZ/Source/Rendering/Light" / (name + ".cpp")
                    for name in ("Lights", "LightManager", "DirectionalLight", "PointLight", "SpotLight")]
        for flags, name in ((["-O2"], "normal"), (["-O3", "-ffast-math"], "fast-math")):
            command = [os.environ.get("CXX", "c++"), "-std=c++17", "-Wall", "-Wextra", "-Wno-sign-compare"] + flags
            for include in includes: command += ["-I", str(include)]
            binary = tmp / name
            subprocess.run(command + [str(path) for path in sources] + ["-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)

if __name__ == "__main__": main()
