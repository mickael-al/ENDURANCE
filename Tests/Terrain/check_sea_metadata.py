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
    mapping=gl.buffer(packed_biome_cells(),7)
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
        assert mask==int(marine),(point,mask,marine)
    # Exact zero/positive semantics at the shoreline, including one float ULP.
    one_ulp_below=struct.unpack('<f',struct.pack('<I',0x3f7fffff))[0]
    for ratio,expected in ((1.0,False),(one_ulp_below,True),(0.0,True)):
        gl.write(columns,struct.pack('<5f3I',-64.,0.,0.,ratio,0.,0,0,0)*stride)
        gl.write(states,struct.pack('<4I',0,0xffffffff,0,0)+bytes(16))
        gl.dispatch(program,1,1,1)
        assert struct.unpack('<4I',gl.read(states,16))[3]==int(expected)
    # A user-defined matrix may put Marine in a land-climate corner. The fast
    # predicate must retain exact climate weighting for this uncommon case.
    table=bytearray(packed_biome_cells())
    struct.pack_into('<I',table,0,25<<15) # Only corner (1,1) is Marine.
    gl.write(mapping,bytes(table))
    for climate,expected in ((0.0,False),(0.125,True)):
        gl.write(columns,struct.pack('<5f3I',-64.,climate,climate,1.,0.,0,0,0)*stride)
        gl.write(states,struct.pack('<4I',0,0xffffffff,0,0)+bytes(16))
        gl.dispatch(program,1,1,1)
        assert struct.unpack('<4I',gl.read(states,16))[3]==int(expected)
    # A 3D band can force both density signs after the marine bit is set.
    # It must preserve that bit, and never leak it into the sign header.
    gl.write(mapping,packed_biome_cells())
    gl.uniform(program,'uDetailScale','1f',1.0)
    gl.write(columns,struct.pack('<5f3I',10.,0.,0.,0.5,0.,10000,0,1<<30)*stride)
    gl.write(states,struct.pack('<4I',0,0xffffffff,0,0)+bytes(16))
    gl.dispatch(program,1,1,1)
    header=struct.unpack('<4I',gl.read(states,16))
    assert header[0]==3 and header[3]==1,header
    gl.check('sea metadata footprint')
    print('PASS sea metadata: boundaries/halo, exact coast endpoints, custom Marine matrix corner',flush=True)


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
            'Struct','Miscellaneous','Rendering/Shader')]+['ThirdParty','ThirdParty/glm']
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
