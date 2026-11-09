#!/usr/bin/env python3
"""Exercise the production collision pipeline using real OpenGL and Bullet."""
import argparse
import ctypes
import os
from pathlib import Path
import subprocess
import tempfile
from gl_headless import GL

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--glew-object', required=True, type=Path)
    args = parser.parse_args()
    bullet = ROOT / 'ThirdParty/Bullet3/src'
    sources = [
        'LinearMath/btAlignedAllocator.cpp', 'LinearMath/btSerializer.cpp',
        'LinearMath/btVector3.cpp',
        'BulletCollision/BroadphaseCollision/btQuantizedBvh.cpp',
        'BulletCollision/CollisionShapes/btCollisionShape.cpp',
        'BulletCollision/CollisionShapes/btConcaveShape.cpp',
        'BulletCollision/CollisionShapes/btTriangleCallback.cpp',
        'BulletCollision/CollisionShapes/btStridingMeshInterface.cpp',
        'BulletCollision/CollisionShapes/btTriangleIndexVertexArray.cpp',
        'BulletCollision/CollisionShapes/btTriangleMeshShape.cpp',
        'BulletCollision/CollisionShapes/btOptimizedBvh.cpp',
        'BulletCollision/CollisionShapes/btBvhTriangleMeshShape.cpp',
    ]
    with tempfile.TemporaryDirectory(prefix='terrain-collision-') as temporary:
        library = Path(temporary) / 'collision_tests.so'
        command = [os.environ.get('CXX', 'c++'), '-std=c++17', '-O2', '-Wall', '-Wextra',
                   '-fPIC', '-shared', '-DGLEW_NO_GLU', '-pthread',
                   '-I', str(ROOT / 'Tests/Terrain/collision_fixture'),
                   '-I', str(ROOT / 'ANTAGONIZ/Header/Struct'),
                   '-I', str(ROOT / 'ANTAGONIZ/Header/Miscellaneous'),
                   '-I', str(ROOT / 'ThirdParty'), '-I', str(ROOT / 'ThirdParty/glm'),
                   '-I', str(bullet),
                   str(ROOT / 'Tests/Terrain/check_collision.cpp'),
                   str(ROOT / 'ANTAGONIZ/Source/Miscellaneous/ProceduralTerrainCollision.cpp')]
        command.extend(str(bullet / source) for source in sources)
        command.extend([str(args.glew_object.resolve()), '-l:libGL.so.1', '-l:libEGL.so.1',
                        '-o', str(library)])
        subprocess.run(command, check=True)
        with GL() as gl:
            print(f'OpenGL {gl.version}; renderer: {gl.renderer}', flush=True)
            runner = ctypes.CDLL(str(library))
            runner.terrain_collision_tests.restype = ctypes.c_int
            return runner.terrain_collision_tests()


if __name__ == '__main__':
    raise SystemExit(main())
