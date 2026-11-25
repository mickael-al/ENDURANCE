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
import hashlib
import json
import os
from pathlib import Path
import re
import statistics
import subprocess
import sys
import tempfile
import time

from check_gpu import MaterialRig, AtmosphereGL, export_appearance, shader_source, TERRAIN, HERE, ROOT


def legacy_source():
    source=(HERE/'legacy_render.fs.glsl').read_text()
    return re.sub(r'^\s*#include\s+"([^"]+)"\s*$',
                  lambda match:shader_source(TERRAIN/match.group(1)),source,flags=re.MULTILINE)


def revision_baseline(reference):
    """Read historical shader/data into scratch; never switch the user's branch."""
    sha=subprocess.check_output(['git','rev-parse','--verify','--end-of-options',
                                 reference+'^{commit}'],cwd=ROOT,text=True).strip()
    paths=['ANTAGONIZ/Header/Miscellaneous/ProceduralTerrainMaterials.hpp',
           'ANTAGONIZ/Header/Miscellaneous/ProceduralTerrainBiomes.hpp',
           'ANTAGONIZ/Header/Miscellaneous/json_struct.h',
           'ANTAGONIZ/Header/ProjectUser/EnduranceTerrainMaterials.hpp',
           'ANTAGONIZ/Header/ProjectUser/EnduranceTerrainMaterialSerialization.hpp',
           'ANTAGONIZ/Source/ProjectUser/EnduranceTerrainMaterials.cpp',
           'Tests/TerrainMaterials/check_cpu.py','Tests/TerrainMaterials/check_cpu.cpp']
    optional='ANTAGONIZ/Header/Miscellaneous/ProceduralTerrainFineSurface.hpp'
    if subprocess.check_output(['git','ls-tree','--name-only',sha,'--',optional],cwd=ROOT).strip():
        paths.append(optional)
    with tempfile.TemporaryDirectory(prefix='endurance-material-baseline-') as directory:
        destination=Path(directory).resolve()
        for name in paths:
            target=(destination/name).resolve(); target.relative_to(destination)
            target.parent.mkdir(parents=True,exist_ok=True)
            target.write_bytes(subprocess.check_output(['git','show',sha+':'+name],cwd=ROOT))
        # A partial checkout need not download every unrelated generation,
        # atmosphere or post-process shader to compare this material pass.
        pending=['Asset/Shader/ProceduralTerrain/render.fs.glsl']; seen=set()
        while pending:
            name=pending.pop()
            if name in seen: continue
            seen.add(name)
            target=(destination/name).resolve(); target.relative_to(destination)
            text=subprocess.check_output(['git','show',sha+':'+name],cwd=ROOT,text=True)
            target.parent.mkdir(parents=True,exist_ok=True); target.write_text(text)
            for include in re.findall(r'^\s*#include\s+"([^"]+)"',text,re.MULTILINE):
                pending.append(str((target.parent/include).resolve().relative_to(destination)))
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


def spread(values):
    ordered=sorted(values)
    def percentile(fraction):
        position=(len(ordered)-1)*fraction
        low=int(position)
        high=min(low+1,len(ordered)-1)
        return ordered[low]+(ordered[high]-ordered[low])*(position-low)
    return {'p25':percentile(.25),'p75':percentile(.75)}


def run(size=(256,256),samples=17,baseline_ref=None,batch_draws=1,warmup_draws=8,
        timing_source='auto',current_ref=None,current_fragment_path=None,case_filter=None):
    if current_ref and current_fragment_path:
        raise ValueError('Choose either a historical candidate or a fragment override')
    historical_current=revision_baseline(current_ref) if current_ref else None
    table,settings=(historical_current[1:3] if historical_current else export_appearance())
    baseline=revision_baseline(baseline_ref) if baseline_ref else None
    current_fragment=(historical_current[3] if historical_current
                      else shader_source(TERRAIN/'render.fs.glsl'))
    if current_fragment_path:
        current_fragment=Path(current_fragment_path).read_text()
    with AtmosphereGL() as gl:
        print(f'OpenGL {gl.version}; renderer: {gl.renderer}',flush=True)
        software=any(name in gl.renderer.lower() for name in ('llvmpipe','softpipe'))
        # Mesa software drivers can under-count consecutive same-program timer
        # queries while completed-render wall time remains unchanged. Record
        # both clocks, and use completion time for software rendering by default.
        measured_source=('wall' if software else 'gpu') if timing_source=='auto' else timing_source
        print(f'Timing source: {measured_source}; {batch_draws} draws/query; '
              f'LP_NUM_THREADS={os.environ.get("LP_NUM_THREADS","driver default")}',flush=True)
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
        current=MaterialRig(gl,table,size,fragment=current_fragment,formats=formats)
        current.set(**settings)
        query=C.c_uint(); gl.glGenQueries(1,C.byref(query))
        results=[]
        cases=(
            ('glints','near, glossy sand grains',dict(terrainBiomeOverride=21,testOrigin=(-50.,180.,20.),
                testAxisX=(1.,0.,0.),testAxisY=(0.,0.,1.),testNormal=(0.,1.,0.))),
            ('glints_minified','minified sand grains, 4:1 footprint',dict(terrainBiomeOverride=21,testOrigin=(-50.,180.,20.),
                testAxisX=(128.,0.,0.),testAxisY=(0.,0.,512.),testNormal=(0.,1.,0.))),
            ('plain','near, fine stains on volcanic rock',dict(terrainBiomeOverride=20,testOrigin=(-50.,180.,20.),
                testAxisX=(8.,0.,0.),testAxisY=(0.,0.,8.),testNormal=(0.,1.,0.))),
            ('near1','near, one biome',dict(terrainBiomeOverride=16,testOrigin=(-50.,180.,20.),
                testAxisX=(8.,0.,0.),testAxisY=(0.,0.,8.),testNormal=(0.,1.,0.))),
            ('near4','near, four biomes + slope blend',dict(terrainBiomeOverride=-1,testClimate=(.375,.625),
                testOrigin=(-50.,180.,20.),testAxisX=(8.,0.,0.),testAxisY=(0.,0.,8.),testNormal=(.8,.6,0.))),
            ('far4','far, four biomes',dict(terrainBiomeOverride=-1,testClimate=(.375,.625),
                testOrigin=(-50.,180.,20.),testAxisX=(4096.,0.,0.),testAxisY=(0.,0.,4096.),testNormal=(0.,1.,0.))),
            ('coast','coast, four biomes + marine + slope',dict(terrainBiomeOverride=-1,testClimate=(.375,.625),
                testOrigin=(-50.,0.,20.),testAxisX=(256.,0.,0.),testAxisY=(0.,0.,256.),testNormal=(.8,.6,0.))),
        )
        try:
            for case_id,case,settings in cases:
                if case_filter and case_id not in case_filter: continue
                for rig in (old,current):
                    rig.set(**settings)
                    for _ in range(warmup_draws): rig.draw()
                gl.glFinish()
                values={'legacy':[],'materials':[]}
                gpu_values={'legacy':[],'materials':[]}
                wall_values={'legacy':[],'materials':[]}
                # Alternate order to reduce drift/thermal order bias.
                for sample in range(samples):
                    order=(('legacy',old),('materials',current))
                    if sample%2: order=tuple(reversed(order))
                    for name,rig in order:
                        gl.glFinish()
                        wall_start=time.perf_counter()
                        gl.glBeginQuery(0x88BF,query.value)
                        # Keep the complete draw/state path identical for both
                        # shaders. Batching amortizes query/readback scheduling
                        # noise; divide by count to report per-draw time.
                        for _ in range(batch_draws): rig.draw()
                        gl.glEndQuery(0x88BF)
                        gl.glFinish()
                        wall_ms=(time.perf_counter()-wall_start)*1000.0/batch_draws
                        elapsed=C.c_ulonglong()
                        gl.glGetQueryObjectui64v(query.value,0x8866,C.byref(elapsed))
                        gpu_ms=elapsed.value/(1e6*batch_draws)
                        gpu_values[name].append(gpu_ms)
                        wall_values[name].append(wall_ms)
                        values[name].append(wall_ms if measured_source=='wall' else gpu_ms)
                ratios=[new/old for old,new in zip(values['legacy'],values['materials'])]
                result={'case_id':case_id,'case':case,'legacy_ms':statistics.median(values['legacy']),
                        'materials_ms':statistics.median(values['materials']),
                        'legacy_samples_ms':values['legacy'],'materials_samples_ms':values['materials'],
                        'legacy_iqr_ms':spread(values['legacy']),
                        'materials_iqr_ms':spread(values['materials']),
                        'paired_ratio':statistics.median(ratios),
                        'paired_ratio_iqr':spread(ratios),
                        'gpu_query_samples_ms':gpu_values,
                        'completed_draw_samples_ms':wall_values,
                        'software_timer_undercount_observed':software and any(
                            gpu<wall*.25 for name in values
                            for gpu,wall in zip(gpu_values[name],wall_values[name]))}
                results.append(result)
                print(f"{case}: legacy {result['legacy_ms']:.3f} ms; materials {result['materials_ms']:.3f} ms; "
                      f"paired ratio {result['paired_ratio']:.3f} "
                      f"(IQR {result['paired_ratio_iqr']['p25']:.3f}..{result['paired_ratio_iqr']['p75']:.3f})",flush=True)
            gl.check('material pass timings')
        finally: gl.glDeleteQueries(1,C.byref(query))
        return {'renderer':gl.renderer,'size':size,'samples':samples,
                'batch_draws':batch_draws,'warmup_draws':warmup_draws,
                'timing_source':measured_source,
                'lp_num_threads':os.environ.get('LP_NUM_THREADS','driver default'),
                'cpu_affinity_count':len(os.sched_getaffinity(0)) if hasattr(os,'sched_getaffinity') else None,
                'current_fragment_sha256':hashlib.sha256(current_fragment.encode()).hexdigest(),
                'current_table_sha256':hashlib.sha256(table).hexdigest(),
                'current_sha':historical_current[0] if historical_current else 'working tree',
                'current_fragment_override':str(current_fragment_path) if current_fragment_path else None,
                'baseline_sha':baseline[0] if baseline else '9c358e6 (flat-color shader)',
                'results':results}


def main():
    def positive_int(value):
        result=int(value)
        if result<1: raise argparse.ArgumentTypeError('must be at least one')
        return result
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--size',type=positive_int,default=256)
    parser.add_argument('--samples',type=positive_int,default=17)
    parser.add_argument('--batch-draws',type=positive_int,default=1,
                        help='Complete draws per timer query, normalized to one draw')
    parser.add_argument('--warmup-draws',type=positive_int,default=8)
    parser.add_argument('--lp-threads',type=positive_int,
                        help='Explicit llvmpipe thread count for reproducibility; does not represent hardware GPU performance')
    parser.add_argument('--timing-source',choices=('auto','gpu','wall'),default='auto',
                        help='auto: completed-draw wall time for software renderers, GPU queries otherwise')
    parser.add_argument('--json',type=Path)
    parser.add_argument('--baseline-ref',help='Compare with the complete material pass at this Git ref')
    candidate=parser.add_mutually_exclusive_group()
    candidate.add_argument('--current-ref',help='Measure a historical candidate without changing the working tree')
    candidate.add_argument('--current-fragment',type=Path,
                           help='Measure an expanded fragment prototype with the current authored table')
    parser.add_argument('--case',action='append',choices=('glints','glints_minified','plain','near1','near4','far4','coast'),
                        help='Select screening cases; omit to measure all cases')
    args=parser.parse_args()
    if args.lp_threads is not None: os.environ['LP_NUM_THREADS']=str(args.lp_threads)
    results=run((args.size,args.size),args.samples,args.baseline_ref,args.batch_draws,
                args.warmup_draws,args.timing_source,args.current_ref,args.current_fragment,args.case)
    if args.json:
        args.json.parent.mkdir(parents=True,exist_ok=True)
        args.json.write_text(json.dumps(results,indent=2)+'\n')


if __name__=='__main__': main()
