#!/usr/bin/env python3
"""Exercise the actual engine shader classes in a desktop EGL context."""
import argparse
import ctypes
import os
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'Tests/Terrain'))
from gl_headless import GL


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--glew-object', required=True, type=Path)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='engine-shader-test-') as temporary:
        directory = Path(temporary)
        library = directory / 'shader_tests.so'
        sources = ['ShaderCompilation.cpp', 'ShaderBinaryCache.cpp', 'ComputeShader.cpp',
                   'GraphiquePipeline.cpp', 'ShaderPair.cpp']
        command = [os.environ.get('CXX', 'c++'), '-std=c++17', '-O2', '-Wall', '-Wextra',
                   '-fPIC', '-shared', '-DGLEW_NO_GLU',
                   '-I', str(ROOT / 'ANTAGONIZ/Header/Struct'),
                   '-I', str(ROOT / 'ANTAGONIZ/Header/Rendering/Shader'),
                   '-I', str(ROOT / 'ThirdParty'),
                   str(ROOT / 'Tests/ShaderCompilation/check_shader_compilation.cpp')]
        command.extend(str(ROOT / 'ANTAGONIZ/Source/Rendering/Shader' / name) for name in sources)
        command.extend([str(args.glew_object.resolve()), '-l:libGL.so.1', '-o', str(library)])
        subprocess.run(command, check=True)
        with GL() as gl:
            print(f'OpenGL {gl.version}; renderer: {gl.renderer}', flush=True)
            runner = ctypes.CDLL(str(library))
            runner.shader_compilation_tests.argtypes = [ctypes.c_char_p]
            runner.shader_compilation_tests.restype = ctypes.c_int
            return runner.shader_compilation_tests(str(directory).encode())


if __name__ == '__main__':
    raise SystemExit(main())
