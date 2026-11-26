#!/usr/bin/env python3
"""Compile and exercise the production game catalogue/state without OpenGL."""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def main():
    with tempfile.TemporaryDirectory(prefix="endurance-biome-effects-state-") as temporary:
        executable = Path(temporary) / "check_state"
        command = [os.environ.get("CXX", "c++"), "-std=c++17", "-O2", "-Wall", "-Wextra", "-pedantic"]
        for path in ("ANTAGONIZ/Header/ProjectUser", "ANTAGONIZ/Header/Miscellaneous", "ThirdParty/glm"):
            command += ["-I", str(ROOT / path)]
        sources = [str(ROOT / path) for path in (
            "Tests/BiomeEffects/check_state.cpp",
            "Tests/BiomeEffects/wind_runtime.cpp",
            "ANTAGONIZ/Source/ProjectUser/EnduranceBiomeEffectsCatalog.cpp",
            "ANTAGONIZ/Source/ProjectUser/EnduranceBiomeEffectsState.cpp")]
        # Compile the test oracle without fast-math, while separately compiling
        # production with the engine's release flag. Otherwise std::isfinite in
        # the test itself could stop detecting invalid results.
        for fast in (False, True):
            objects=[]
            common=command
            for i, source in enumerate(sources):
                output=Path(temporary)/f"{i}.o"
                flags=["-ffast-math"] if fast and i>0 else []
                subprocess.run(common+flags+["-c",source,"-o",str(output)],check=True)
                objects.append(str(output))
            subprocess.run([command[0],*objects,"-o",str(executable)], check=True)
            print("CPU mode:","release fast-math" if fast else "strict",flush=True)
            subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    main()
