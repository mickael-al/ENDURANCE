"""Read the production CPU matrix/variant definitions without duplicating them."""
from functools import lru_cache
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
_temporary = None


@lru_cache(maxsize=1)
def biome_exporter():
    global _temporary
    _temporary = tempfile.TemporaryDirectory(prefix="terrain-biome-layout-")
    binary = Path(_temporary.name) / "layout"
    subprocess.run([os.environ.get("CXX", "c++"), "-std=c++17", "-O2", "-I",
                    str(ROOT / "ANTAGONIZ/Header/Miscellaneous"),
                    str(ROOT / "Tests/Terrain/check_biome_layout.cpp"), "-o", str(binary)], check=True)
    return binary


@lru_cache(maxsize=1)
def packed_biome_cells():
    data = subprocess.run([str(biome_exporter()), "--cells"], check=True, stdout=subprocess.PIPE).stdout
    assert len(data) == 64 * 16
    return data


@lru_cache(maxsize=128)
def biome_masks(low, high):
    text = subprocess.run([str(biome_exporter()), "--mask", str(low), str(high)],
                          check=True, stdout=subprocess.PIPE, text=True).stdout
    return tuple(map(int, text.split()))


@lru_cache(maxsize=128)
def biome_defines(surface, volume):
    return subprocess.run([str(biome_exporter()), "--defines", str(surface), str(volume)],
                          check=True, stdout=subprocess.PIPE, text=True).stdout
