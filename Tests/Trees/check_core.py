#!/usr/bin/env python3
"""Compile and exercise production tree generation, including Release fast-math.

The fixture uses the actual CPU generator and GLM, with no generated-geometry
stubs. It checks asset invariants instead of depending on exact golden vertices.
"""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[2]

def main():
    with tempfile.TemporaryDirectory(prefix="endurance-trees-") as temporary:
        for label,flags in (("normal",["-O2"]),("fast",["-O3","-ffast-math"])):
            binary=Path(temporary)/label
            subprocess.run([os.environ.get("CXX","c++"),"-std=c++17",*flags,
                "-I",str(ROOT/"ThirdParty/glm"),"-I",str(ROOT/"ANTAGONIZ/Header/ProjectUser"),
                str(ROOT/"Tests/Trees/check_core.cpp"),
                str(ROOT/"ANTAGONIZ/Source/ProjectUser/ProceduralTree.cpp"),"-o",str(binary)],check=True)
            print(f"Production tree generator ({label})",flush=True)
            subprocess.run([str(binary)],check=True,timeout=120)

if __name__=="__main__": main()
