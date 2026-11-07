"""Run CPU-only networking regressions; no Steam account or remote peer is needed."""
from pathlib import Path
import argparse
import subprocess
import sys

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--sanitize', action='store_true')
parser.add_argument('--sdk-check', action='store_true', help='Also compile session and transport against the vendored Steam SDK')
args = parser.parse_args()
folder = Path(__file__).resolve().parent
for filename in ['check_rpc_decoder.py', 'check_helpers.py', 'check_transport.py',
                 'test_sessions.py', 'integrity/check_digest.py']:
    command = [sys.executable, str(folder / filename)]
    if args.sanitize:
        command.append('--sanitize')
    if args.sdk_check and filename in ['check_transport.py', 'test_sessions.py']:
        command.append('--sdk-check')
    subprocess.run(command, check=True)
print('PASS: all networking regressions')
