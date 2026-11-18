#!/usr/bin/env python3
"""Build and execute the actual standalone atmosphere transition state."""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def main():
    with tempfile.TemporaryDirectory(prefix="endurance-atmosphere-cpu-") as directory:
        binary=Path(directory)/"transition"
        subprocess.run([os.environ.get("CXX","c++"),"-std=c++17","-O2","-Wall","-Wextra",
                        "-I",str(ROOT/"ThirdParty/glm"),
                        "-I",str(ROOT/"ANTAGONIZ/Header/Rendering/Shader"),
                        str(ROOT/"Tests/Atmosphere/check_transition.cpp"),"-o",str(binary)],check=True)
        subprocess.run([str(binary)],check=True)


if __name__=="__main__":
    main()
