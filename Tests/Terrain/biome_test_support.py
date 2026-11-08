"""Read production biome data and isolate one recipe for headless probes."""
from functools import lru_cache
import os
from pathlib import Path
import re
import subprocess
import tempfile

from gl_headless import shader_source

ROOT = Path(__file__).resolve().parents[2]
_temporary = None
BIOME_NAMES = """
PolarIce FrozenTundra Tundra Taiga BorealBog RockyTundra ColdSteppe
BorealForest ColdForest ColdMarsh DryPlain Meadow TemperateForest
TemperateRainforest TemperateMarsh RockDesert DrySavanna Savanna DryJungle
TropicalJungle VolcanicDesert RedDesert AridPlateau BurningJungle
TropicalMangrove Marine
""".split()


def biome_recipe_source(path):
    """Extract one production recipe, replacing only its dispatch harness.

    Production entrypoints end with their shared accumulation kernel. A point
    probe supplies its own main instead, and uses ordinary seed/origin uniforms
    rather than the job buffer. The recipe and its noise helpers stay unchanged;
    no all-biome switch, parameter blending, or production test define is added.
    """
    path = Path(path)
    kernel = {"relief.C.glsl": "field_surface_accumulate.glsl",
              "volume.C.glsl": "field_volume_accumulate.glsl"}[path.name]
    include = f'#include "../../{kernel}"'
    source = path.read_text()
    assert source.rstrip().endswith(include), f"Missing final accumulation include: {path}"
    source = source[:source.rindex(include)]
    batch_include = '#include "../../field_batch_common.glsl"'
    assert source.count(batch_include) == 1, f"Unexpected job harness: {path}"
    source = source.replace(batch_include, '#include "../../field_common.glsl"')
    return re.sub(r'^\s*#include\s+"([^"]+)"\s*$',
                  lambda match: shader_source(path.parent / match.group(1)),
                  source, flags=re.MULTILINE)


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
