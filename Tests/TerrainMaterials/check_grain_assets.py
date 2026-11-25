#!/usr/bin/env python3
"""Bake and validate compact facets using the production C++ game loader."""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def export_grain():
    with tempfile.TemporaryDirectory(prefix='endurance-grain-assets-') as directory:
        binary = Path(directory)/'grain_assets'
        subprocess.run([os.environ.get('CXX','c++'), '-std=c++17', '-O2',
                        '-I', str(ROOT/'ANTAGONIZ/Header/ProjectUser'),
                        '-I', str(ROOT/'ANTAGONIZ/Header/Miscellaneous'),
                        '-I', str(ROOT/'ThirdParty'),
                        str(ROOT/'Tests/TerrainMaterials/check_grain_assets.cpp'),
                        str(ROOT/'ANTAGONIZ/Source/ProjectUser/EnduranceTerrainGrain.cpp'),
                        '-o', str(binary)], check=True)
        data = subprocess.check_output([str(binary),'--dump'], cwd=ROOT/'ANTAGONIZ')
    return data


if __name__ == '__main__':
    assert len(export_grain()) == 1024*1024*4
    print('PASS production PNG decode/cache, compact facet bake and slope moments')
