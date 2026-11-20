#!/usr/bin/env python3
"""Validate migration against the actual original v1 JSON, including edits."""
from pathlib import Path
import os
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]


def main():
    with tempfile.TemporaryDirectory(prefix="endurance-material-migration-") as directory:
        binary = Path(directory) / "migration"
        command = [os.environ.get("CXX", "c++"), "-std=c++17", "-O2", "-Wall", "-Wextra",
                   "-Werror", "-I", str(ROOT / "ANTAGONIZ/Header/Miscellaneous"),
                   "-I", str(ROOT / "ANTAGONIZ/Header/ProjectUser"),
                   str(HERE / "check_migration.cpp"),
                   str(ROOT / "ANTAGONIZ/Source/ProjectUser/EnduranceTerrainMaterials.cpp"),
                   "-o", str(binary)]
        subprocess.run(command, check=True)
        subprocess.run([str(binary), str(HERE / "fixtures/legacy-appearance-v1.json")], check=True)


if __name__ == "__main__":
    main()
