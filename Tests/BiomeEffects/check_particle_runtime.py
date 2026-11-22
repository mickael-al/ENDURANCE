#!/usr/bin/env python3
"""Run the actual adapter, ParticleSystem, allocator and shader compiler in GL.

Only scene/services are fixtures. No particle lifecycle or GPU algorithm is
reimplemented. Resource/call interception and synchronous reads are test-only.
"""
from pathlib import Path
import ctypes as C
import os
import subprocess
import sys
import tempfile

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'Tests/Atmosphere'))
from gl_support import AtmosphereGL

def main():
    os.environ.setdefault('MESA_GL_VERSION_OVERRIDE','4.6')
    os.environ.setdefault('MESA_GLSL_VERSION_OVERRIDE','460')
    with tempfile.TemporaryDirectory(prefix='endurance-particle-runtime-') as temporary:
        directory=Path(temporary)
        library=directory/'particles.so'
        command=[os.environ.get('CXX','c++'),'-std=c++17','-O2','-fPIC','-shared',
                 '-DGLEW_NO_GLU','-DGLFW_INCLUDE_NONE','-pthread',
                 '-fvisibility=hidden','-ffunction-sections','-fdata-sections']
        if '--fast-math' in sys.argv: command.append('-ffast-math')
        for path in ('Tests/BiomeEffects/particle_runtime_fixture','Tests/SSAO/runtime_fixture',
                     'ANTAGONIZ/Header/Struct','ANTAGONIZ/Header/Interface',
                     'ANTAGONIZ/Header/Miscellaneous','ANTAGONIZ/Header/ProjectUser',
                     'ANTAGONIZ/Header/Rendering/Shader','ANTAGONIZ/Header/Rendering/Window',
                     'ThirdParty','ThirdParty/glm','ThirdParty/glfw/include'):
            command+=['-I',str(ROOT/path)]
        for path in ('Tests/BiomeEffects/particle_runtime_bridge.cpp',
                     'ANTAGONIZ/Source/ProjectUser/EnduranceBiomeParticles.cpp',
                     'ANTAGONIZ/Source/ProjectUser/EnduranceBiomeEffectsCatalog.cpp',
                     'ANTAGONIZ/Source/Miscellaneous/ParticleSystem.cpp',
                     'ANTAGONIZ/Source/Rendering/Shader/GraphiquePipelineManager.cpp',
                     'ANTAGONIZ/Source/Rendering/Shader/GraphiquePipeline.cpp',
                     'ANTAGONIZ/Source/Rendering/Shader/ComputeShader.cpp',
                     'ANTAGONIZ/Source/Rendering/Shader/ShaderPair.cpp',
                     'ANTAGONIZ/Source/Rendering/Shader/ShaderCompilation.cpp',
                     'ANTAGONIZ/Source/Rendering/Shader/ShaderBinaryCache.cpp'):
            command.append(str(ROOT/path))
        command+=['-l:libGLEW.so.2.2','-l:libGL.so.1','-l:libEGL.so.1','-ldl',
                  '-Wl,--gc-sections','-Wl,-z,defs','-o',str(library)]
        subprocess.run(command,check=True)
        os.environ['ENDURANCE_SHADER_CACHE_DIR']=str(directory/'cache')
        api=C.CDLL(str(library))
        api.particles_validate.argtypes=[C.c_char_p]
        api.particles_validate.restype=C.c_int
        with AtmosphereGL() as gl:
            print(f'OpenGL {gl.version}; renderer: {gl.renderer}',flush=True)
            assert api.particles_validate(str(ROOT).encode())==1,'Native particle lifecycle gate failed'
            gl.check('native particle lifecycle')
    print('PASS native biome particle lifecycle and state isolation',flush=True)

if __name__=='__main__': main()
