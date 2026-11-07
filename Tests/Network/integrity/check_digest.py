#!/usr/bin/env python3
"""Check the production SHA-256 and canonical manifest encoder without the engine."""
import argparse
import hashlib
import os
from pathlib import Path
import shlex
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sanitize", action="store_true")
    parser.add_argument("--release", action="store_true",
                        help="Match Release optimization/fast-math assumptions")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[3]
    sizes = [0, 1, 2, 3, 7, 15, 31, 55, 56, 57, 63, 64, 65, 95, 111,
             119, 120, 127, 128, 129, 255, 256, 1024, 4096, 65537]
    vectors = []
    for size in sizes:
        payload = bytes((index * 73 + 19) & 255 for index in range(size))
        vectors.append(f'    {{{size}, "{hashlib.sha256(payload).hexdigest()}"}},')
    with tempfile.TemporaryDirectory(prefix="endurance-integrity-digest-") as directory:
        build = Path(directory)
        (build / "digest_vectors.inc").write_text(
            "constexpr Vector Vectors[] = {\n" + "\n".join(vectors) + "\n};\n")
        command = shlex.split(os.environ.get("CXX", "g++")) + [
            "-std=c++17", "-g", "-Wall", "-Wextra", "-Werror",
            "-fno-exceptions", "-fno-rtti",
            "-I" + str(root / "ANTAGONIZ/Header/Network"), "-I" + str(build),
            str(root / "ANTAGONIZ/Source/Network/ResourceIntegrityDigest.cpp"),
            str(root / "Tests/Network/integrity/digest_test.cpp"), "-o", str(build / "test"),
        ]
        command += ["-O3", "-ffast-math", "-DNDEBUG"] if args.release else ["-O2"]
        if args.sanitize:
            command += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
        subprocess.run(command, check=True)
        subprocess.run([str(build / "test")], check=True, env={**os.environ, "ASAN_OPTIONS": os.environ.get("ASAN_OPTIONS", "detect_leaks=0")})


if __name__ == "__main__":
    main()
