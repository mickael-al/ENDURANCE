#!/usr/bin/env python3
"""Validate actual persistent shader binaries, including a fresh-process reload."""
import argparse
import ctypes
import hashlib
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'Tests/Terrain'))
from gl_headless import GL


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--glew-object', type=Path)
    parser.add_argument('--run-library', type=Path)
    parser.add_argument('--directory', type=Path)
    parser.add_argument('--mode', choices=('full', 'cold', 'warm'))
    args = parser.parse_args()
    if args.run_library:
        with GL() as gl:
            print(f'OpenGL {gl.version}; renderer: {gl.renderer}', flush=True)
            runner = ctypes.CDLL(str(args.run_library))
            runner.shader_binary_cache_tests.argtypes = [ctypes.c_char_p, ctypes.c_char_p]
            runner.shader_binary_cache_tests.restype = ctypes.c_int
            return runner.shader_binary_cache_tests(str(args.directory).encode(), args.mode.encode())
    if not args.glew_object:
        parser.error('--glew-object is required')
    with tempfile.TemporaryDirectory(prefix='engine-binary-cache-test-') as temporary:
        directory = Path(temporary)
        library = directory / 'binary_cache_tests.so'
        sources = ['ShaderCompilation.cpp', 'ShaderBinaryCache.cpp', 'ComputeShader.cpp',
                   'GraphiquePipeline.cpp', 'ShaderPair.cpp']
        command = [os.environ.get('CXX', 'c++'), '-std=c++17', '-O2', '-Wall', '-Wextra',
                   '-fPIC', '-shared', '-Wl,-Bsymbolic-functions', '-DGLEW_NO_GLU',
                   '-I', str(ROOT / 'ANTAGONIZ/Header/Struct'),
                   '-I', str(ROOT / 'ANTAGONIZ/Header/Rendering/Shader'),
                   '-I', str(ROOT / 'ThirdParty'),
                   str(ROOT / 'Tests/ShaderCompilation/check_binary_cache.cpp')]
        command.extend(str(ROOT / 'ANTAGONIZ/Source/Rendering/Shader' / name) for name in sources)
        command.extend([str(args.glew_object.resolve()), '-l:libGL.so.1', '-ldl', '-o', str(library)])
        subprocess.run(command, check=True)
        for mode in ('full', 'cold', 'warm'):
            fixture = directory / ('complete' if mode == 'full' else 'persistent')
            subprocess.run([sys.executable, __file__, '--run-library', str(library),
                            '--directory', str(fixture), '--mode', mode], check=True)
        files = list((directory / 'persistent/cache').glob('*.glbin'))
        assert len(files) == 1, 'Cross-process cache did not retain exactly one entry'
        content = files[0].read_bytes()
        assert content[:8] == b'ENDGLBIN' and struct.unpack_from('<I', content, 8)[0] == 1
        assert struct.unpack_from('<Q', content, 16)[0] == len(content) - 88
        assert content[24:56].hex() == files[0].stem, 'Cache key/header mismatch'
        assert hashlib.sha256(content[88:]).digest() == content[56:88], 'Payload hash differs from independent SHA-256'
        print('PASS binary file: framed length, filename key and independent SHA-256 checksum')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
