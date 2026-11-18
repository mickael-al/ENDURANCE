#!/usr/bin/env python3
"""Run all CPU/GPU atmosphere validations; fail immediately on any failed gate."""
from pathlib import Path
import subprocess
import sys

DIRECTORY=Path(__file__).resolve().parent
TESTS=("check_cpu.py","check_shaders.py","check_common.py","check_luts.py",
       "check_ibl.py","check_volumes.py","check_sampling.py","check_sky.py",
       "check_game_climate.py","check_runtime.py")


def main():
    for test in TESTS:
        print(f"\nRunning {test}",flush=True)
        subprocess.run([sys.executable,str(DIRECTORY/test)],check=True)
    print(f"\nPASS all {len(TESTS)} atmosphere validation groups",flush=True)


if __name__=="__main__":
    main()
