#!/usr/bin/env python3
"""Check actual GPU surface metadata and asynchronous C++ completion/cache paths."""
import argparse
import ctypes
import os
from pathlib import Path
import struct
import subprocess
import tempfile

from biome_test_support import packed_biome_cells
from field_batch_runner import default_uniforms
from gl_headless import GL

ROOT=Path(__file__).resolve().parents[2]


def check_footprint(gl):
    """Only a boundary/interior contribution, not the one-sample halo, counts."""
    directory=ROOT/'Asset/Shader/ProceduralTerrain'
    program=gl.compute(directory/'field_batch_prepare.C.glsl')
    cells=4;side=cells+3;stride=side*side;state_stride=2
    columns=gl.buffer(stride*32,1)
    states=gl.buffer(state_stride*16,3)
    gl.buffer(struct.pack('<4i8I',0,0,0,0,0,5,0,0,0,0,0,0),6)
    gl.buffer(packed_biome_cells(),7)
    uniforms=default_uniforms(cells,uDetailScale=0,uHeightAmplitude=0)
    uniforms.update(uJobSlots=('1ui',1),uColumnStride=('1ui',stride),
                    uStateStride=('1ui',state_stride))
    for name,(kind,*values) in uniforms.items():gl.uniform(program,name,kind,*values)
    for point,marine in (((0,2),False),((side-1,2),False),((1,2),True),((cells+1,cells+1),True)):
        payload=bytearray()
        for z in range(side):
            for x in range(side):
                # Cell0, climate corner0: land biome0; altitude blend1 -> land.
                # One sample with altitude blend0.5 also contributes Marine25.
                ratio=.5 if (x,z)==point else 1.
                payload.extend(struct.pack('<5f3I',-64.,0.,0.,ratio,0.,0,0,0))
        gl.write(columns,bytes(payload))
        gl.write(states,struct.pack('<4I',0,0xffffffff,0,0)+bytes(16))
        gl.dispatch(program,1,1,1)
        mask=struct.unpack('<4I',gl.read(states,16))[3]
        assert mask==(1|((1<<25) if marine else 0)),(point,mask,marine)
    gl.check('sea metadata footprint')
    print('PASS sea metadata: actual footprint boundaries included, halo excluded',flush=True)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--glew-object',required=True,type=Path)
    args=parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='endurance-sea-metadata-') as temporary:
        os.environ['ENDURANCE_SHADER_CACHE_DIR']=str(Path(temporary)/'program-cache')
        library=Path(temporary)/'check_sea_metadata.so'
        sources=['Tests/Terrain/check_sea_metadata.cpp',
                 'ANTAGONIZ/Source/Miscellaneous/ProceduralTerrainGpu.cpp']
        sources += ['ANTAGONIZ/Source/Rendering/Shader/'+name+'.cpp' for name in (
            'ShaderCompilation','ShaderBinaryCache','ShaderProgramBatch',
            'ParallelShaderCompiler','ComputeShader')]
        includes=['ANTAGONIZ/Header/'+part for part in (
            'Struct','Miscellaneous','Rendering/Shader')]+['ThirdParty']
        command=[os.environ.get('CXX','c++'),'-std=c++17','-O2','-ffast-math',
                 '-Wall','-Wextra','-fPIC','-shared','-DGLEW_NO_GLU']
        for include in includes:command+=['-I',str(ROOT/include)]
        command += [str(ROOT/source) for source in sources]
        command += [str(args.glew_object.resolve()),'-pthread','-l:libGL.so.1',
                    '-l:libEGL.so.1','-Wl,--wrap=glFinish','-o',str(library)]
        subprocess.run(command,check=True)
        with GL() as gl:
            print(f'OpenGL {gl.version}; renderer: {gl.renderer}',flush=True)
            check_footprint(gl)
            runner=ctypes.CDLL(str(library))
            runner.sea_metadata_tests.argtypes=[ctypes.c_char_p]
            runner.sea_metadata_tests.restype=ctypes.c_int
            return runner.sea_metadata_tests(str(ROOT/'Asset/Shader/ProceduralTerrain').encode())


if __name__=='__main__':raise SystemExit(main())
