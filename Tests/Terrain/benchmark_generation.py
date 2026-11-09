#!/usr/bin/env python3
"""Run the same generation benchmark against either checkout in a headless EGL context."""
import argparse
import ctypes
import json
import os
from pathlib import Path
import statistics
import subprocess
import tempfile
from gl_headless import GL

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--engine-root", type=Path, default=ROOT)
    parser.add_argument("--glew-object", type=Path, required=True)
    parser.add_argument("--cells", type=int, default=64, choices=(4, 8, 16, 32, 64))
    parser.add_argument("--jobs", type=int, default=3)
    parser.add_argument("--iterations", type=int, default=3)
    parser.add_argument("--biome", type=int, default=-1, choices=range(-1, 26))
    parser.add_argument("--disable-caches", action="store_true")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    root = args.engine_root.resolve()
    shader = root / "ANTAGONIZ/Source/Rendering/Shader"
    optimized = "columnCacheEntries" in (root / "ANTAGONIZ/Header/Miscellaneous/ProceduralTerrainGpu.hpp").read_text()
    with tempfile.TemporaryDirectory(prefix="terrain-benchmark-") as temp:
        library = Path(temp) / "benchmark.so"
        # Keep cold compilation separate from measured field generation. Isolate
        # files from user shader caches; warm lazy dispatch code inside the fixture.
        os.environ["ENDURANCE_SHADER_CACHE_DIR"] = str(Path(temp) / "shader-cache")
        command = [os.environ.get("CXX", "c++"), "-std=c++17", "-O3", "-ffast-math", "-fPIC", "-shared",
                   "-DGLEW_NO_GLU", "-DTERRAIN_BENCHMARK_SHARED"]
        if optimized:
            command.append("-DTERRAIN_BENCHMARK_CACHE_API")
        for include in ("ANTAGONIZ/Header/Struct", "ANTAGONIZ/Header/Miscellaneous",
                        "ANTAGONIZ/Header/Rendering/Shader", "ThirdParty"):
            command += ["-I", str(root / include)]
        command += [str(ROOT / "Tests/Terrain/benchmark_generation.cpp"),
                    str(root / "ANTAGONIZ/Source/Miscellaneous/ProceduralTerrainGpu.cpp")]
        command += [str(shader / (name + ".cpp")) for name in
                    ("ShaderCompilation", "ShaderBinaryCache", "ShaderProgramBatch", "ParallelShaderCompiler", "ComputeShader")]
        command += [str(args.glew_object.resolve()), "-pthread", "-l:libGL.so.1", "-l:libEGL.so.1", "-o", str(library)]
        subprocess.run(command, check=True)
        with GL():
            runner = ctypes.CDLL(str(library))
            run = runner.terrain_generation_benchmark
            run.argtypes = [ctypes.c_char_p, ctypes.c_uint, ctypes.c_uint, ctypes.c_uint, ctypes.c_int, ctypes.c_int]
            run.restype = ctypes.c_char_p
            result = json.loads(run(str(root / "Asset/Shader/ProceduralTerrain").encode(), args.cells,
                                    args.jobs, args.iterations, args.biome, not args.disable_caches))
    if "error" in result:
        raise RuntimeError(result["error"])
    result["engine_root"] = str(root)
    try:
        result["commit"] = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=root, text=True,
                                                   stderr=subprocess.DEVNULL).strip()
        result["working_tree_modified"] = bool(subprocess.check_output(["git", "status", "--porcelain"], cwd=root, text=True))
    except (OSError, subprocess.CalledProcessError):
        pass
    if args.output:
        args.output.write_text(json.dumps(result, indent=2) + "\n")
    else:
        print(json.dumps(result, indent=2))
    print(f"Renderer: {result['renderer']}")
    if "llvmpipe" in result["renderer"].lower() or "softpipe" in result["renderer"].lower():
        print("Software renderer: validate the harness only; do not extrapolate these times to a hardware GPU.")
    for name in dict.fromkeys(phase["name"] for phase in result["phases"]):
        times = [phase["wall_ms"] for phase in result["phases"] if phase["name"] == name]
        print(f"{name}: median {statistics.median(times):.3f} ms / 24 requests")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
