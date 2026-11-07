"""Run production invitation parsing and lobby-language helpers without Steam."""
from pathlib import Path
import argparse
import os
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--sanitize', action='store_true')
args = parser.parse_args()
command = shlex.split(os.environ.get('CXX', 'g++')) + [
    '-std=c++17', '-Wall', '-Wextra', '-Werror', '-O1', '-g',
    '-I' + str(ROOT / 'ANTAGONIZ/Header/Network'),
]
if args.sanitize:
    command += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
env = dict(os.environ)
env.setdefault('ASAN_OPTIONS', 'detect_leaks=0')
with tempfile.TemporaryDirectory(prefix='endurance-network-helpers-') as directory:
    for filename in ['test_launch_arguments.cpp', 'test_lobby_language.cpp']:
        output = Path(directory) / Path(filename).stem
        subprocess.run(command + [str(ROOT / 'Tests/Network' / filename), '-o', str(output)], check=True)
        subprocess.run([str(output)], check=True, env=env)
