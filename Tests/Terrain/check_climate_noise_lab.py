#!/usr/bin/env python3
"""Compile the Climate Noise Lab compute shader and validate its five views."""
from __future__ import annotations

import ctypes as C
from pathlib import Path
import re

from gl_headless import GL, GLUnavailable

ROOT = Path(__file__).resolve().parents[2]
SHADER = ROOT / "Asset/Shader/ClimateNoiseLab/climate_noise_lab.C.glsl"
BIOMES = ROOT / "ANTAGONIZ/Header/Miscellaneous/ProceduralTerrainBiomes.hpp"


def production_matrix() -> list[int]:
    source = BIOMES.read_text(encoding="utf-8")
    match = re.search(r"BiomeMatrix2D\s*\{\{(.*?)\}\};", source, re.S)
    assert match, "Cannot find the production 5x5 biome matrix"
    values = [int(value) for value in re.findall(r"\b\d+\b", match.group(1))]
    assert len(values) == 25 and all(0 <= value < 26 for value in values), values
    return values


def register_texture_api(gl: GL) -> None:
    gl._fn("glGenTextures", None, C.c_int, C.POINTER(C.c_uint))
    gl._fn("glDeleteTextures", None, C.c_int, C.POINTER(C.c_uint))
    gl._fn("glBindTexture", None, C.c_uint, C.c_uint)
    gl._fn("glTexStorage2D", None, C.c_uint, C.c_int, C.c_uint, C.c_int, C.c_int)
    gl._fn("glTexParameteri", None, C.c_uint, C.c_uint, C.c_int)
    gl._fn("glBindImageTexture", None, C.c_uint, C.c_uint, C.c_int,
           C.c_ubyte, C.c_int, C.c_uint, C.c_uint)
    gl._fn("glGetTexImage", None, C.c_uint, C.c_int, C.c_uint, C.c_uint, C.c_void_p)
    gl._fn("glUniform1iv", None, C.c_int, C.c_int, C.POINTER(C.c_int))


def read_texture(gl: GL, texture: int, resolution: int) -> bytes:
    gl.glBindTexture(0x0DE1, texture)  # GL_TEXTURE_2D
    output = (C.c_ubyte * (resolution * resolution * 4))()
    gl.glGetTexImage(0x0DE1, 0, 0x1908, 0x1401, output)  # RGBA / UNSIGNED_BYTE
    return bytes(output)


def dispatch(gl: GL, program: int, textures, resolution: int, attraction: int,
             humidity_noise: int = 1, temperature_noise: int = 1,
             remap: tuple[float, float] = (-0.32, 0.32)) -> list[bytes]:
    uniforms = {
        "uResolution": ("1ui", resolution),
        "uSeed": ("1ui", 1337),
        "uWorldOrigin": ("2f", 0.0, 0.0),
        "uWorldExtent": ("1f", 32768.0),
        "uClimateFrequency": ("1f", 0.0005),
        "uHumidityFrequencyMultiplier": ("1f", 1.0),
        "uTemperatureFrequencyMultiplier": ("1f", 1.0),
        "uHumidityNoiseType": ("1i", humidity_noise),
        "uTemperatureNoiseType": ("1i", temperature_noise),
        "uOctaves": ("1i", 4),
        "uLacunarity": ("1f", 2.0),
        "uGain": ("1f", 0.5),
        "uRemapMinimum": ("1f", remap[0]),
        "uRemapMaximum": ("1f", remap[1]),
        "uAttractionMode": ("1i", attraction),
        "uAttractionTarget": ("2f", 0.5, 0.5),
        "uAttractionMaxShift": ("1f", 0.08),
        "uMatrixBlend": ("1i", 1),
    }
    for name, arguments in uniforms.items():
        gl.uniform(program, name, *arguments)
    matrix = (C.c_int * 25)(*production_matrix())
    location = gl.glGetUniformLocation(program, b"uBiomeMatrix")
    assert location >= 0
    gl.glUseProgram(program)
    gl.glUniform1iv(location, 25, matrix)
    for binding, texture in enumerate(textures):
        gl.glBindImageTexture(binding, texture, 0, 0, 0, 0x88B9, 0x8058)  # WRITE_ONLY / RGBA8
    gl.dispatch(program, (resolution + 7) // 8, (resolution + 7) // 8, 1,
                barrier=0x20 | 0x8)  # IMAGE_ACCESS | TEXTURE_FETCH
    return [read_texture(gl, texture, resolution) for texture in textures]


def equal_horizontal_ratio(texture: bytes, resolution: int) -> float:
    values = texture[0::4]
    equal = 0
    total = resolution * (resolution - 1)
    for y in range(resolution):
        row = y * resolution
        equal += sum(values[row + x] == values[row + x + 1]
                     for x in range(resolution - 1))
    return equal / total


def check_channels(outputs: list[bytes]) -> None:
    temperature, humidity, combined, attracted, matrix = outputs
    assert all(temperature[i] == temperature[i+1] == temperature[i+2]
               for i in range(0, len(temperature), 4))
    assert all(humidity[i] == humidity[i+1] == humidity[i+2]
               for i in range(0, len(humidity), 4))
    assert len(set(temperature[0::4])) > 16 and len(set(humidity[0::4])) > 16
    assert all(combined[i] == temperature[i] and combined[i+1] == humidity[i]
               and combined[i+2] == 0 and combined[i+3] == 255
               for i in range(0, len(combined), 4))
    assert attracted == combined, "Disabled attraction changed the climate"
    assert len({matrix[i:i+4] for i in range(0, len(matrix), 4)}) > 16


def main() -> int:
    try:
        with GL() as gl:
            print(f"OpenGL {gl.version}; renderer: {gl.renderer}", flush=True)
            register_texture_api(gl)
            program = gl.compute(SHADER)
            resolution = 96
            textures = (C.c_uint * 5)()
            gl.glGenTextures(5, textures)
            try:
                for texture in textures:
                    gl.glBindTexture(0x0DE1, texture)
                    gl.glTexStorage2D(0x0DE1, 1, 0x8058, resolution, resolution)
                    gl.glTexParameteri(0x0DE1, 0x2801, 0x2601)  # MIN / LINEAR
                    gl.glTexParameteri(0x0DE1, 0x2800, 0x2601)  # MAG / LINEAR

                simplex = dispatch(gl, program, textures, resolution, attraction=0)
                repeated = dispatch(gl, program, textures, resolution, attraction=0)
                assert simplex == repeated, "Climate lab output is not deterministic"
                check_channels(simplex)

                patchwork = dispatch(gl, program, textures, resolution, attraction=0,
                                     humidity_noise=10, temperature_noise=10,
                                     remap=(-1.0, 1.0))
                patchwork_repeated = dispatch(gl, program, textures, resolution, attraction=0,
                                              humidity_noise=10, temperature_noise=10,
                                              remap=(-1.0, 1.0))
                assert patchwork == patchwork_repeated, "Patchwork output is not deterministic"
                check_channels(patchwork)
                assert patchwork[2] != simplex[2], "Patchwork duplicates the Simplex climate"
                assert equal_horizontal_ratio(patchwork[0], resolution) > 0.08
                assert equal_horizontal_ratio(patchwork[1], resolution) > 0.08

                with_attraction = dispatch(gl, program, textures, resolution, attraction=1,
                                           humidity_noise=10, temperature_noise=10,
                                           remap=(-1.0, 1.0))
                assert with_attraction[2] == patchwork[2], "Attraction changed the raw climate view"
                assert with_attraction[3] != patchwork[2], "attractNearFifths01 had no visible effect"
                assert with_attraction[4] != patchwork[4], "Matrix view ignored the attracted climate"
                gl.check("Climate Noise Lab")
            finally:
                gl.glDeleteTextures(5, textures)
            print("PASS Climate Noise Lab: Simplex and patchwork regions, deterministic channels, attraction and matrix output", flush=True)
    except GLUnavailable as error:
        print(f"SKIP: {error}")
        return 77
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
