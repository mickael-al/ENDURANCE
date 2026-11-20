#!/usr/bin/env python3
"""Compile the pure sea-coverage contract with normal and engine Release flags."""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def run():
    with tempfile.TemporaryDirectory(prefix='endurance-sea-coverage-') as directory:
        for name, extra in (('normal', []), ('fast-math', ['-ffast-math'])):
            binary = Path(directory) / name
            command = [os.environ.get('CXX', 'c++'), '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror',
                       *extra, '-I', str(ROOT / 'ANTAGONIZ/Header/Miscellaneous'),
                       '-I', str(ROOT / 'ThirdParty/glm'),
                       str(ROOT / 'Tests/Sea/check_coverage.cpp'), '-o', str(binary)]
            subprocess.run(command, check=True)
            subprocess.run([str(binary)], check=True)


if __name__ == '__main__':
    run()
