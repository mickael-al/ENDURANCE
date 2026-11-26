#!/usr/bin/env python3
"""Exercise the production wind renderer in a real offscreen OpenGL context."""
from pathlib import Path
import argparse
import ctypes as C
import math
import os
import struct
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT/'Tests/Atmosphere'))
from gl_support import AtmosphereGL


def check_geometry(gl):
    from check_particle_gpu import Fixture, SHADERS, RGBA32F, floats, shader_source
    from check_rain_gpu import set_camera
    program = gl.compile([(0x8B31, shader_source(SHADERS/'wind_streaks.vs.glsl'))],
                         'wind centerline continuity', feedback=('VS_OUT.worldPosition', 'gl_Position'))
    fixture = Fixture(gl)
    vao = C.c_uint()
    gl.glGenVertexArrays(1, C.byref(vao)); gl.arrays.append(vao.value)
    gl.glBindVertexArray(vao.value)
    texture = gl.texture((1, 1), internal=RGBA32F, value=0.)
    fbo = C.c_uint()
    gl.glGenFramebuffers(1, C.byref(fbo)); gl.framebuffers.append(fbo.value)
    gl.glBindFramebuffer(0x8D40, fbo.value)
    gl.glFramebufferTexture2D(0x8D40, 0x8CE0, 0x0DE1, texture, 0)
    gl.glDrawBuffers(1, (C.c_uint*1)(0x8CE0))
    assert gl.glCheckFramebufferStatus(0x8D40) == 0x8CD5
    for name, result, parameters in (('glEnable', None, (C.c_uint,)),
                                     ('glBeginTransformFeedback', None, (C.c_uint,)),
                                     ('glEndTransformFeedback', None, ())):
        gl._fn(name, result, *parameters)
    instances = gl.buffer(64, 6)
    feedback = gl.buffer(50*7*4)
    gl.glBindBufferBase(0x8C8E, 0, feedback)
    gl.uniform(program, 'uWindAnchor', '3f', 0., 0., 0.)
    gl.uniform(program, 'uEnvironmentInvResolution', '2f', 1/256, 1/160)

    def capture(phase, eye=(0., 0., 0.), roll=0.):
        set_camera(fixture, eye, target=(0., 0., -20.), roll=roll)
        gl.bind(fixture.camera, 0)
        gl.write(instances, floats(0., 0., -20., 18., 1., 0., 0., .05,
                                   .8, .35, .9, phase, .34, .95, .98, 1.))
        gl.bind(instances, 6)
        gl.glUseProgram(program)
        gl.glEnable(0x8C89)
        gl.glBeginTransformFeedback(0x0000)
        gl.glDrawArrays(0x0000, 0, 50)
        gl.glEndTransformFeedback(); gl.glDisable(0x8C89)
        gl.glMemoryBarrier(gl.ALL_BARRIER_BITS)
        values = struct.unpack('<350f', gl.read(feedback, 50*7*4))
        assert all(math.isfinite(v) for v in values), 'Wind geometry contains NaN/Inf'
        return [values[i:i+7] for i in range(0, len(values), 7)]

    before, after = capture(2*math.pi-1e-5), capture(1e-5)
    assert max(abs(a-b) for p, q in zip(before, after) for a, b in zip(p[:3], q[:3])) < 1e-4, \
        'Wind ribbon jumps when its animation phase wraps'
    reference = capture(.8)
    for eye, roll in (((0., 0., 0.), 1.2), ((4., 3., 2.), 0.), ((-20., 0., -20.), 0.)):
        actual = capture(.8, eye, roll)
        # Opposite sides may face the viewer; their centerline must stay fixed.
        for i in range(0, 50, 2):
            assert max(abs((actual[i][j]+actual[i+1][j])-(reference[i][j]+reference[i+1][j]))
                       for j in range(3)) < 1e-5, 'Camera moved the world-space wind centerline'
    gl.glBindBufferBase(0x8C8E, 0, 0); gl.glBindFramebuffer(0x8D40, 0)
    gl.check('wind transform feedback')
    print('PASS wind geometry: continuous phase wrap, fixed centerline, finite end-on/rolled views', flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--fast-math', action='store_true')
    parser.add_argument('--preview', type=Path)
    args = parser.parse_args()
    os.environ.setdefault('MESA_GL_VERSION_OVERRIDE', '4.6')
    os.environ.setdefault('MESA_GLSL_VERSION_OVERRIDE', '460')
    with tempfile.TemporaryDirectory(prefix='endurance-wind-runtime-') as temporary:
        directory = Path(temporary)
        library = directory/'wind.so'
        command = [os.environ.get('CXX', 'c++'), '-std=c++17', '-O2', '-fPIC',
                   '-DGLEW_NO_GLU', '-DGLFW_INCLUDE_NONE', '-pthread',
                   '-fvisibility=hidden', '-ffunction-sections', '-fdata-sections']
        for path in ('Tests/BiomeEffects/particle_runtime_fixture', 'Tests/SSAO/runtime_fixture',
                     'ANTAGONIZ/Header/Struct', 'ANTAGONIZ/Header/Interface',
                     'ANTAGONIZ/Header/Miscellaneous', 'ANTAGONIZ/Header/ProjectUser',
                     'ANTAGONIZ/Header/Rendering/Shader', 'ANTAGONIZ/Header/Rendering/Window',
                     'ThirdParty', 'ThirdParty/glm', 'ThirdParty/glfw/include'):
            command += ['-I', str(ROOT/path)]
        sources = ('Tests/BiomeEffects/check_wind_streaks.cpp',
                   'ANTAGONIZ/Source/ProjectUser/EnduranceWindStreaks.cpp',
                   'ANTAGONIZ/Source/Rendering/Shader/GraphiquePipelineManager.cpp',
                   'ANTAGONIZ/Source/Rendering/Shader/GraphiquePipeline.cpp',
                   'ANTAGONIZ/Source/Rendering/Shader/ShaderPair.cpp',
                   'ANTAGONIZ/Source/Rendering/Shader/ShaderCompilation.cpp',
                   'ANTAGONIZ/Source/Rendering/Shader/ShaderBinaryCache.cpp')
        objects = []
        for index, path in enumerate(sources):
            output = directory/f'{index}.o'
            # Keep the oracle strict while exercising production release math.
            flags = ['-ffast-math'] if args.fast_math and index else []
            subprocess.run(command+flags+['-c', str(ROOT/path), '-o', str(output)], check=True)
            objects.append(str(output))
        subprocess.run([command[0], '-shared', *objects, '-l:libGLEW.so.2.2', '-l:libGL.so.1',
                        '-l:libEGL.so.1', '-ldl', '-Wl,--gc-sections', '-Wl,-z,defs',
                        '-o', str(library)], check=True)
        os.environ['ENDURANCE_SHADER_CACHE_DIR'] = str(directory/'cache')
        if args.preview:
            os.environ['ENDURANCE_WIND_PREVIEW'] = str(directory/'wind.ppm')
        api = C.CDLL(str(library))
        api.wind_validate.argtypes = [C.c_char_p]
        api.wind_validate.restype = C.c_int
        with AtmosphereGL() as gl:
            print(f'OpenGL {gl.version}; renderer: {gl.renderer}', flush=True)
            assert api.wind_validate(str(ROOT).encode()) == 1, 'Native wind renderer checks failed'
            gl.check('native wind renderer')
            check_geometry(gl)
        if args.preview:
            from PIL import Image
            args.preview.parent.mkdir(parents=True, exist_ok=True)
            Image.open(directory/'wind.ppm').save(args.preview)
            print(f'Controlled wind rendering diagnostic: {args.preview}', flush=True)
            os.environ.pop('ENDURANCE_WIND_PREVIEW', None)
    print('PASS dedicated wind lifecycle, anchoring, bounded draw, state isolation and occlusion', flush=True)


if __name__ == '__main__':
    main()
