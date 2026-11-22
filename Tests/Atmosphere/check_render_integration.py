#!/usr/bin/env python3
"""Compile the real render integration against engine headers (no service doubles).

The numerical/EGL tests replace window/editor services. This complementary gate
checks that the production renderer, lights, atmosphere and terrain agree on the
interfaces used by the shadow cache, including the real material/model types.
"""
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[2]


def main():
    headers = ROOT / "ANTAGONIZ/Header"
    includes = [headers] + sorted(path for path in headers.rglob("*") if path.is_dir())
    includes += [ROOT / "ThirdParty" / path for path in
                 ("", "glm", "glew/include", "glfw/include", "Bullet3/src", "openal-soft/include",
                  "stb-cmake", "OpenFBX/src")]
    command = [os.environ.get("CXX", "c++"), "-std=c++17", "-fsyntax-only",
               "-DGLEW_NO_GLU", "-DGLFW_INCLUDE_NONE", "-DANTAGONIZ_UNIX", "-DENDURANCE_ENABLE_NETWORK=0"]
    for include in includes:
        command += ["-I", str(include)]
    sources = ("Rendering/RenderingEngine.cpp", "Rendering/FrameGpuProfiler.cpp",
               "Rendering/Camera/Camera.cpp", "Rendering/Camera/CameraManager.cpp",
               "Rendering/Shader/PostProcess.cpp", "Rendering/Shader/OilPainting.cpp",
               "Rendering/Shader/EnvironmentScreenEffects.cpp",
               "Rendering/Shader/Bloom.cpp", "Rendering/Shader/DepthOfField.cpp", "Rendering/Shader/ColorTint.cpp",
               "Rendering/Shader/ShaderDataMisc.cpp", "Rendering/Shader/GraphiquePipeline.cpp",
               "Rendering/Shader/GraphiquePipelineManager.cpp", "Setting/SettingManager.cpp", "Main.cpp",
               "Rendering/Light/Lights.cpp",
               "Rendering/Light/LightManager.cpp", "Rendering/Light/SpotLight.cpp",
               "Rendering/Shader/Atmosphere.cpp", "Rendering/Shader/MoonMeshCache.cpp", "Rendering/Window/FrameBuffer.cpp", "Rendering/Window/SSAOBuffer.cpp",
               "Rendering/Shape/Model.cpp", "Rendering/Shape/ShapeBufferBase.cpp",
               "Rendering/Materials/Textures.cpp", "Miscellaneous/Editor.cpp",
               "Miscellaneous/ProceduralGeneration.cpp",
               "ProjectUser/ProceduralTerrain.cpp",
               "ProjectUser/EnduranceAtmosphere.cpp", "ProjectUser/EnduranceMoonMesh.cpp",
               "ProjectUser/EnduranceBiomeEffects.cpp", "ProjectUser/EnduranceBiomeEffectsCatalog.cpp",
               "ProjectUser/EnduranceBiomeEffectsState.cpp", "ProjectUser/EnduranceBiomeParticles.cpp",
               "ProjectUser/TreeRenderGpu.cpp", "ProjectUser/EnduranceTreeRenderer.cpp",
               "ProjectUser/EnduranceTerrainMaterials.cpp",
               "ProjectUser/EnduranceTerrainMaterialPanel.cpp")
    subprocess.run(command + [str(ROOT / "ANTAGONIZ/Source" / path) for path in sources], check=True)
    print("PASS real renderer/light/atmosphere/terrain interfaces with production headers")


if __name__ == "__main__":
    main()
