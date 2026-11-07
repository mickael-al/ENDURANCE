"""Build/run the actual transport with deterministic Steam/libhv substitutes.

The checked-in Steam headers can additionally compile the transport with --sdk-check.
No Steam account or running client is used by this test.
"""
from pathlib import Path
import argparse
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--glm', type=Path, default=ROOT / 'ThirdParty/glm')
parser.add_argument('--compiler', default=os.environ.get('CXX', 'g++'))
parser.add_argument('--sanitize', action='store_true')
parser.add_argument('--sdk-check', action='store_true')
args = parser.parse_args()
includes = [
    '-I' + str(ROOT / 'Tests/Network/transport/stubs'),
    '-I' + str(ROOT / 'ANTAGONIZ/Header/Network'),
    '-I' + str(args.glm.resolve()),
]
source = str(ROOT / 'ANTAGONIZ/Source/Network/NetworkManager.cpp')
with tempfile.TemporaryDirectory(prefix='endurance-steam-transport-') as temporary:
    build = Path(temporary)
    command = [args.compiler, '-std=c++17', '-O1', '-g', '-pthread']
    if args.sanitize:
        command += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
    executable = build / 'transport_regression'
    subprocess.run(command + includes + [source,
        str(ROOT / 'Tests/Network/transport/transport_regression.cpp'), '-o', str(executable)], check=True)
    environment = os.environ.copy()
    environment.setdefault('ASAN_OPTIONS', 'detect_leaks=0')
    subprocess.run([str(executable)], check=True, env=environment)
    if args.sdk_check:
        subprocess.run([args.compiler, '-std=c++17', '-O0', '-pthread',
            '-I' + str(ROOT / 'ThirdParty/steamworks_sdk/public')]
            + includes + ['-c', source, '-o', str(build / 'actual-steam-sdk.o')], check=True)
        print('PASS production transport compiles against checked-in Steam SDK headers')
