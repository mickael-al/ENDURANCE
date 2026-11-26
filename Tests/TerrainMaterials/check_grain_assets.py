#!/usr/bin/env python3
"""Load historical grain images only for before/after rendering comparisons.

Production sparkles are procedural and allocate no grain texture. Keep these
fixtures in git history instead of shipping obsolete PNGs or their C++ loader.
"""
from functools import lru_cache
from io import BytesIO
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[2]


@lru_cache(maxsize=4)
def export_grain(ref='ee47948'):
    from PIL import Image
    folder = 'ENDURANCE/Ressources/Texture/Terrain'
    def load(name, mode):
        data = subprocess.check_output(['git', 'show', f'{ref}:{folder}/{name}'], cwd=ROOT)
        with Image.open(BytesIO(data)) as image:
            return image.convert(mode)
    normal = load('NormalMap.png', 'RGB')
    noise = load('grain.png', 'L')
    assert normal.size == noise.size == (1024, 1024)
    return Image.merge('RGBA', (*normal.split(), noise)).tobytes()


if __name__ == '__main__':
    assert len(export_grain()) == 1024*1024*4
    print('PASS historical grain texture fixture for before/after comparisons')
