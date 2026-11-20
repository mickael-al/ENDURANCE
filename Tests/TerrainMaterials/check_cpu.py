#!/usr/bin/env python3
"""Compile the actual material data API and all game-authored biome recipes."""
from __future__ import annotations

import os
from pathlib import Path
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]


def compile_export(directory: Path) -> Path:
    binary = directory / "terrain_material_data"
    command = [os.environ.get("CXX", "c++"), "-std=c++17", "-O2", "-Wall", "-Wextra",
               "-Werror", "-I", str(ROOT / "ANTAGONIZ/Header/Miscellaneous"),
               "-I", str(ROOT / "ANTAGONIZ/Header/ProjectUser"),
               "-I", str(ROOT / "ThirdParty/glm"),
               str(HERE / "check_cpu.cpp"),
               str(ROOT / "ANTAGONIZ/Source/ProjectUser/EnduranceTerrainMaterials.cpp"),
               "-o", str(binary)]
    subprocess.run(command, check=True)
    return binary


def export_table() -> bytes:
    return export_appearance()[0]


def export_appearance():
    with tempfile.TemporaryDirectory(prefix="endurance-material-data-") as directory:
        binary = compile_export(Path(directory))
        payload = subprocess.check_output([str(binary), "--dump"])
        settings = subprocess.check_output([str(binary), "--settings"],text=True).split()
    assert len(payload) == 13312, len(payload)
    return payload,dict(terrainMacroPeriod=float(settings[0]),terrainPatchPeriod=float(settings[1]),
                        terrainMaterialDebug=int(settings[2]))


def main():
    with tempfile.TemporaryDirectory(prefix="endurance-material-cpu-") as directory:
        subprocess.run([str(compile_export(Path(directory)))], check=True)


if __name__ == "__main__":
    main()
