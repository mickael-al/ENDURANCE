#!/usr/bin/env python3
"""Verify sea publication using the actual terrain scheduler and planner worker.

GPU completion is controlled: surface chunks have no terrain vertices but do
carry mixed marine metadata. This is an orchestration test, not GPU timing.
"""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'Tests/Terrain'))
from check_scheduler import GL_STUB, ENGINE_STUB, MISC, SOURCES


def main():
    with tempfile.TemporaryDirectory(prefix='terrain-sea-streaming-') as temp:
        temp=Path(temp)
        stub=temp/'stubs'
        stub.mkdir()
        (stub/'glcore.hpp').write_text(GL_STUB)
        (stub/'scheduler_engine.hpp').write_text(ENGINE_STUB)
        for name in ('Engine','PointeurClass','Camera','GraphiquePipeline','ShaderPair'):
            (stub/(name+'.hpp')).write_text('#include "scheduler_engine.hpp"\n')
        includes=(stub,ROOT/MISC,ROOT/'ANTAGONIZ/Header/Interface',
                  ROOT/'ThirdParty/glm',ROOT/'ThirdParty')
        sources=[ROOT/'Tests/Sea/check_streaming.cpp']+[
            ROOT/SOURCES/(name+'.cpp') for name in
            ('ProceduralGeneration','ProceduralTerrainPlanner',
             'ProceduralTerrainStreaming','ProceduralTerrainLod')]
        command=[os.environ.get('CXX','c++'),'-std=c++17','-O3','-ffast-math',
                 '-pthread','-DTERRAIN_SCHEDULER_LOOKAHEAD']
        for include in includes: command+=['-I',str(include)]
        binary=temp/'check_sea_streaming'
        subprocess.run(command+[str(p) for p in sources]+['-o',str(binary)],check=True)
        result=subprocess.run([str(binary)],text=True,capture_output=True,check=False)
        if result.returncode: raise RuntimeError(result.stdout+result.stderr)
        report=json.loads(result.stdout)
        report['sea_checks']=['mixed marine presence','EMPTY terrain chunks',
            'atomic LOD publication','no overlapping water','dry chunks excluded',
            'idle storage stability','failure/cancel recovery','stale completion ignored',
            'shutdown clears coverage','sea above terrain height bounds',
            'sea below terrain height bounds','analytic Plane/Sphere stay dry']
        print(json.dumps(report,indent=2))


if __name__=='__main__': main()
