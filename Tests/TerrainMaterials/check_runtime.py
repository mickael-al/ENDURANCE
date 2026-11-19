#!/usr/bin/env python3
"""Exercise actual terrain runtime with controlled scheduler/GL service doubles.

Production shaders/UBO layout are checked on EGL by check_gpu.py; this test
targets live CPU edits, draw-state ownership and resource lifecycle instead.
"""
import os
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "Tests/Terrain"))
from check_scheduler import GL_STUB, ENGINE_STUB, MISC, SOURCES


def main():
    with tempfile.TemporaryDirectory(prefix="terrain-material-runtime-") as directory:
        directory = Path(directory)
        stub = directory / "stubs"
        stub.mkdir()
        (stub / "glcore.hpp").write_text(GL_STUB)
        (stub / "scheduler_engine.hpp").write_text(ENGINE_STUB)
        for name in ("Engine", "PointeurClass", "Camera", "GraphiquePipeline", "ShaderPair"):
            (stub / (name + ".hpp")).write_text('#include "scheduler_engine.hpp"\n')
        includes = (stub, ROOT / MISC, ROOT / "ANTAGONIZ/Header/Interface", ROOT / "ThirdParty/glm", ROOT / "ThirdParty")
        sources = [ROOT / "Tests/TerrainMaterials/check_runtime.cpp"] + [
            ROOT / SOURCES / (name + ".cpp") for name in
            ("ProceduralGeneration", "ProceduralTerrainPlanner", "ProceduralTerrainStreaming", "ProceduralTerrainLod")]
        for label, optimization in (("normal", ["-O2"]), ("fast", ["-O3", "-ffast-math"])):
            command = [os.environ.get("CXX", "c++"), "-std=c++17", "-pthread", *optimization]
            for include in includes:
                command += ["-I", str(include)]
            binary = directory / label
            subprocess.run(command + [str(path) for path in sources] + ["-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    main()
