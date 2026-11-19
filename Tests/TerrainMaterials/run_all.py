#!/usr/bin/env python3
"""Run terrain appearance data, renderer lifecycle and actual shader checks.

Optional timing experiments are deliberately separate: machine-dependent
performance is not a deterministic correctness assertion.
"""
from pathlib import Path
import subprocess
import sys


HERE=Path(__file__).resolve().parent


def main():
    for name in ('check_cpu.py','check_runtime.py','check_gpu.py'):
        print(f'Running {name}',flush=True)
        subprocess.run([sys.executable,str(HERE/name)],check=True)
    print('PASS all terrain material validation groups',flush=True)


if __name__=='__main__': main()
