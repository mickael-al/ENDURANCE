#!/usr/bin/env python3
"""Time complete terrain material passes against flat tint or a material revision.

Both paths write the production G-buffer formats, using identical varyings and
viewport. The default baseline is frozen from 9c358e6, before palette materials.
Use --baseline-ref to compare against an authored material revision. Shader
compilation, table upload, allocation and readback are outside timed intervals.
The rich material pass does more work; this is a budget measurement, not a
claim of performance parity with flat colors or an estimate of whole-game FPS.
Software GL timings do not predict the user's hardware GPU performance.
"""
from __future__ import annotations

import argparse
import ctypes as C
import json
import io
from pathlib import Path
import re
import statistics
import subprocess
import sys
import tarfile
import tempfile

from check_gpu import MaterialRig, AtmosphereGL, export_appearance, shader_source, TERRAIN, HERE, ROOT


def legacy_source():
    source=(HERE/'legacy_render.fs.glsl').read_text()
    return re.sub(r'^\s*#include\s+"([^"]+)"\s*$',
                  lambda match:shader_source(TERRAIN/match.group(1)),source,flags=re.MULTILINE)


def revision_baseline(reference):
    """Read historical shader/data into scratch; never switch the user's branch."""
    sha=subprocess.check_output(['git','rev-parse','--verify','--end-of-options',
                                 reference+'^{commit}'],cwd=ROOT,text=True).strip()
    paths=['Asset/Shader',
           'ANTAGONIZ/Header/Miscellaneous/ProceduralTerrainMaterials.hpp',
           'ANTAGONIZ/Header/Miscellaneous/ProceduralTerrainBiomes.hpp',
           'ANTAGONIZ/Header/Miscellaneous/json_struct.h',
           'ANTAGONIZ/Header/ProjectUser/EnduranceTerrainMaterials.hpp',
           'ANTAGONIZ/Header/ProjectUser/EnduranceTerrainMaterialSerialization.hpp',
           'ANTAGONIZ/Source/ProjectUser/EnduranceTerrainMaterials.cpp',
           'Tests/TerrainMaterials/check_cpu.py','Tests/TerrainMaterials/check_cpu.cpp']
    archive=subprocess.check_output(['git','archive',sha,'--',*paths],cwd=ROOT)
    with tempfile.TemporaryDirectory(prefix='endurance-material-baseline-') as directory:
        destination=Path(directory).resolve()
        with tarfile.open(fileobj=io.BytesIO(archive)) as source:
            for member in source.getmembers():
                if not member.isfile(): continue
                target=(destination/member.name).resolve()
                target.relative_to(destination) # reject paths outside temporary checkout
                target.parent.mkdir(parents=True,exist_ok=True)
                target.write_bytes(source.extractfile(member).read())
        script='''import json,sys
from pathlib import Path
sys.path.insert(0,str(Path(sys.argv[1])/'Tests/TerrainMaterials'))
from check_cpu import export_appearance
table,settings=export_appearance()
Path(sys.argv[1],'table.bin').write_bytes(table)
Path(sys.argv[1],'settings.json').write_text(json.dumps(settings))
'''
        subprocess.run([sys.executable,'-c',script,str(destination)],check=True)
        table=(destination/'table.bin').read_bytes()
        settings=json.loads((destination/'settings.json').read_text())
        fragment=shader_source(destination/'Asset/Shader/ProceduralTerrain/render.fs.glsl')
    return sha,table,settings,fragment


def run(size=(256,256),samples=17,baseline_ref=None):
    table,settings=export_appearance()
    baseline=revision_baseline(baseline_ref) if baseline_ref else None
    with AtmosphereGL() as gl:
        print(f'OpenGL {gl.version}; renderer: {gl.renderer}',flush=True)
        for name,signature in {
            'glGenQueries':(None,C.c_int,C.POINTER(C.c_uint)),
            'glDeleteQueries':(None,C.c_int,C.POINTER(C.c_uint)),
            'glBeginQuery':(None,C.c_uint,C.c_uint),'glEndQuery':(None,C.c_uint),
            'glGetQueryObjectui64v':(None,C.c_uint,C.c_uint,C.POINTER(C.c_ulonglong)),
        }.items(): gl._fn(name,*signature)
        formats=(0x8230,0x881B,0x881A,0x8051) # RG32F, RGB16F, RGBA16F, RGB8
        old=MaterialRig(gl,baseline[1] if baseline else table,size,
                        fragment=baseline[3] if baseline else legacy_source(),formats=formats)
        if baseline: old.set(**baseline[2])
        current=MaterialRig(gl,table,size,formats=formats)
        current.set(**settings)
        query=C.c_uint(); gl.glGenQueries(1,C.byref(query))
        results=[]
        cases=(
            ('near, one biome',dict(terrainBiomeOverride=16,testOrigin=(-50.,180.,20.),
                testAxisX=(8.,0.,0.),testAxisY=(0.,0.,8.),testNormal=(0.,1.,0.))),
            ('near, four biomes + slope blend',dict(terrainBiomeOverride=-1,testClimate=(.375,.625),
                testOrigin=(-50.,180.,20.),testAxisX=(8.,0.,0.),testAxisY=(0.,0.,8.),testNormal=(.8,.6,0.))),
            ('far, four biomes',dict(terrainBiomeOverride=-1,testClimate=(.375,.625),
                testOrigin=(-50.,180.,20.),testAxisX=(4096.,0.,0.),testAxisY=(0.,0.,4096.),testNormal=(0.,1.,0.))),
            ('coast, four biomes + marine + slope',dict(terrainBiomeOverride=-1,testClimate=(.375,.625),
                testOrigin=(-50.,0.,20.),testAxisX=(256.,0.,0.),testAxisY=(0.,0.,256.),testNormal=(.8,.6,0.))),
        )
        try:
            for case,settings in cases:
                for rig in (old,current):
                    rig.set(**settings)
                    for _ in range(8): rig.draw()
                gl.glFinish()
                values={'legacy':[],'materials':[]}
                # Alternate order to reduce drift/thermal order bias.
                for sample in range(samples):
                    order=(('legacy',old),('materials',current))
                    if sample%2: order=tuple(reversed(order))
                    for name,rig in order:
                        gl.glBeginQuery(0x88BF,query.value)
                        rig.draw()
                        gl.glEndQuery(0x88BF)
                        elapsed=C.c_ulonglong()
                        gl.glGetQueryObjectui64v(query.value,0x8866,C.byref(elapsed))
                        values[name].append(elapsed.value/1e6)
                result={'case':case,'legacy_ms':statistics.median(values['legacy']),
                        'materials_ms':statistics.median(values['materials']),
                        'legacy_samples_ms':values['legacy'],'materials_samples_ms':values['materials']}
                results.append(result)
                print(f"{case}: legacy {result['legacy_ms']:.3f} ms; materials {result['materials_ms']:.3f} ms",flush=True)
            gl.check('material pass timings')
        finally: gl.glDeleteQueries(1,C.byref(query))
        return {'renderer':gl.renderer,'size':size,'samples':samples,
                'baseline_sha':baseline[0] if baseline else '9c358e6 (flat-color shader)',
                'results':results}


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--size',type=int,default=256)
    parser.add_argument('--samples',type=int,default=17)
    parser.add_argument('--json',type=Path)
    parser.add_argument('--baseline-ref',help='Compare with the complete material pass at this Git ref')
    args=parser.parse_args()
    results=run((args.size,args.size),args.samples,args.baseline_ref)
    if args.json:
        args.json.parent.mkdir(parents=True,exist_ok=True)
        args.json.write_text(json.dumps(results,indent=2)+'\n')


if __name__=='__main__': main()
