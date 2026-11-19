#!/usr/bin/env python3
"""Compile the real scene JSON and GPU light-layout compatibility regression."""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def main():
    with tempfile.TemporaryDirectory(prefix="endurance-light-persistence-") as temporary:
        executable = Path(temporary) / "check_light_persistence"
        command = [os.environ.get("CXX", "c++"), "-std=c++17", "-O2", "-Wall", "-Wextra",
                   "-Wno-unused-parameter"]
        for directory in ("ANTAGONIZ/Header/Miscellaneous", "ANTAGONIZ/Header/Struct",
                          "ANTAGONIZ/Header/Rendering/Shape", "ANTAGONIZ/Header/Rendering/Shader",
                          "ANTAGONIZ/Header/Interface", "ThirdParty/glm"):
            command.extend(("-I", str(ROOT / directory)))
        command.extend((str(ROOT / "Tests/Atmosphere/check_light_persistence.cpp"), "-o", str(executable)))
        subprocess.run(command, check=True)
        subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    main()
