"""Compile the actual RPC decoder with malformed/valid wire payloads.

Requires a C++17 compiler and GLM. No Steam client or network connection is used.
"""
from pathlib import Path
import argparse
import os
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--glm", type=Path, default=ROOT / "ThirdParty/glm")
parser.add_argument("--compiler", default=os.environ.get("CXX", "g++"))
parser.add_argument("--sanitize", action="store_true")
args = parser.parse_args()
command = shlex.split(args.compiler) + ["-std=c++17", "-O1", "-g", "-Wall", "-Wextra", "-Werror"]
if args.sanitize:
    command += ["-fsanitize=address,undefined", "-fno-sanitize-recover=all",
                "-fno-omit-frame-pointer", "-fno-pie", "-no-pie"]
command += ["-I" + str(ROOT / "ANTAGONIZ/Header/Network"), "-I" + str(args.glm.resolve())]
environment = os.environ.copy()
environment.setdefault("ASAN_OPTIONS", "detect_leaks=0")
with tempfile.TemporaryDirectory(prefix="endurance-rpc-decoder-") as temporary:
    executable = Path(temporary) / "rpc_decoder"
    subprocess.run(command + [str(ROOT / "Tests/Network/test_rpc_decoder.cpp"), "-o", str(executable)], check=True)
    subprocess.run([str(executable)], check=True, env=environment)
