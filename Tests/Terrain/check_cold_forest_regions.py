#!/usr/bin/env python3
"""Calibrate ColdForest's broad mask and verify room for complete 20-km pits."""
from pathlib import Path
import argparse
import hashlib
import json
import re

import numpy as np

from gl_headless import GL, shader_source

ROOT = Path(__file__).resolve().parents[2]
TERRAIN = ROOT / 'Asset/Shader/ProceduralTerrain'
BIOME = TERRAIN / 'biomes/ColdForest'
SEEDS = (1337, 17, 41, 12345, 99991, 73, 5641, 18263)

GRID_MAIN = r'''
layout(local_size_x=8,local_size_y=8) in;
layout(std430,binding=6) writeonly buffer Results { vec4 values[]; };
uniform uint probeSize;
uniform float probeSpacing;
void main() {
 uvec2 i=gl_GlobalInvocationID.xy;
 if(any(greaterThanEqual(i,uvec2(probeSize))))return;
 vec2 p=(vec2(i)-float(probeSize-1u)*.5)*probeSpacing;
 float signal=regionCalibrationSignal(p);
 float oldMask=clamp(noiseFadeQuintic(clamp((signal+.18)/.4,0.,1.)),0.,1.);
 values[i.x+i.y*probeSize]=vec4(signal,coldForestRegionMask(p),oldMask,0.);
}
'''

SITE_MAIN = r'''
layout(local_size_x=8,local_size_y=8) in;
layout(std430,binding=6) writeonly buffer Results { vec4 values[]; };
uniform uint probeSize;
void main() {
 uvec2 i=gl_GlobalInvocationID.xy;
 if(any(greaterThanEqual(i,uvec2(probeSize))))return;
 ColdForestPitSite site=coldForestPitInCell(ivec2(i)-ivec2(int(probeSize)/2));
 values[i.x+i.y*probeSize]=vec4(coldForestPitEligibility(site),
  coldForestRegionMask(site.center),
  coldForestRiverDiskClear(site.center,COLD_FOREST_PIT_RADIUS*1.1)?1.:0.,0.);
}
'''


def const(name):
    text=(BIOME/'ground_settings.glsl').read_text()
    return float(re.search(r'const float '+name+r'\s*=\s*([^;]+);',text).group(1))


def mask_source():
    # Instrument the production function's return, preserving its entire noise
    # expression. This exposes the underlying signal for a quantile estimate
    # without duplicating its FBM/warp implementation in a Python oracle.
    body=re.search(r'float coldForestRegionMask\(vec2 p\)\s*\{(.*?)\n\}',
        (BIOME/'ground.glsl').read_text(),re.S).group(1)
    body,count=re.subn(r'return clamp\(noiseFadeQuintic\(t\), 0\.0, 1\.0\);',
        'return region;',body)
    assert count==1,'Update the calibration return instrumentation'
    return '#version 450 core\n'+shader_source(TERRAIN/'field_common.glsl') \
        +shader_source(BIOME/'ground.glsl') \
        +'\nfloat regionCalibrationSignal(vec2 p) {'+body+'\n}\n'+GRID_MAIN


def site_source(region_scale):
    source='#version 450 core\n'+shader_source(TERRAIN/'field_common.glsl') \
        +shader_source(BIOME/'pits.glsl')+SITE_MAIN
    # Compare old and enlarged wavelengths with identical final placement,
    # pit dimensions, target coverage, river pitch and admission implementation.
    return re.sub(r'(const float COLD_FOREST_REGION_SCALE\s*=\s*)[^;]+',
        lambda match:match.group(1)+str(float(region_scale)),source)


def sample(gl,program,seed,size,spacing=None):
    gl.uniform(program,'uSeed','1ui',seed)
    gl.uniform(program,'probeSize','1ui',size)
    if spacing is not None:gl.uniform(program,'probeSpacing','1f',spacing)
    buffer=gl.buffer(size*size*16);gl.bind(buffer,6)
    gl.dispatch_extent(program,size,size)
    result=np.frombuffer(gl.read(buffer,size*size*16),dtype=np.float32).reshape(size,size,4).copy()
    gl.check('ColdForest region probe')
    assert np.isfinite(result).all()
    return result


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path)
    parser.add_argument('--calibrate',action='store_true',help='Report measurements without asserting the 30% target')
    args=parser.parse_args()
    scale=const('COLD_FOREST_REGION_SCALE')
    low=const('COLD_FOREST_REGION_LOW');high=const('COLD_FOREST_REGION_HIGH')
    assert abs(high-low-.4)<1e-6,'The region fade width changed'
    with GL() as gl:
        program=gl.compile([(0x91B9,mask_source())],'ColdForest broad region coverage')
        records=[];signals=[]
        for seed in SEEDS:
            result=sample(gl,program,seed,513,scale*.5)
            signal,mask,old=result[:,:,:3].transpose(2,0,1)
            assert (mask>=0.).all() and (mask<=1.).all()
            assert (mask<=old+1e-6).all(),'Reduced region grew beyond the old signal threshold'
            records.append(dict(seed=seed,samples=mask.size,
                dominant_fraction=float((mask>=.5).mean()),mean_mask=float(mask.mean()),
                full_mask_fraction=float((mask==1.).mean()),
                previous_dominant_fraction=float((old>=.5).mean())))
            signals.append(signal.ravel())
        threshold=float(np.quantile(np.concatenate(signals),.7))
        coverage=float(np.mean([r['dominant_fraction'] for r in records]))
        if not args.calibrate:
            assert .295<coverage<.305,('Expected about 30% dominant region coverage',coverage)
            assert all(.285<r['dominant_fraction']<.315 for r in records)
        fine=sample(gl,program,1337,385,250.)
        assert np.array_equal(fine,sample(gl,program,1337,385,250.)), 'Region mask is nondeterministic'
        mask=fine[:,:,1]
        max_jump=float(max(abs(np.diff(mask,axis=0)).max(),abs(np.diff(mask,axis=1)).max()))
        assert max_jump<.2,('Abrupt regional transition at 250m resolution',max_jump)

        admissions=[]
        for test_scale in dict.fromkeys((24000.,60000.,scale)):
            p=gl.compile([(0x91B9,site_source(test_scale))],'ColdForest complete-pit admission')
            per_seed=[]
            for seed in (1337,17,41,99991):
                data=sample(gl,p,seed,257)
                assert np.isin(data[:,:,0],(0.,1.)).all()
                per_seed.append(dict(seed=seed,candidates=257**2,
                    accepted=int(data[:,:,0].sum()),
                    admitted_fraction=float(data[:,:,0].mean()),
                    river_clear_fraction=float(data[:,:,2].mean())))
            admissions.append(dict(region_scale_m=test_scale,seeds=per_seed,
                mean_admitted_fraction=float(np.mean([r['admitted_fraction'] for r in per_seed]))))
        if not args.calibrate:
            final=next(a for a in admissions if a['region_scale_m']==scale)
            assert final['mean_admitted_fraction']>.02,'Large pit regions became effectively empty'

        paths=[BIOME/p for p in ('ground_settings.glsl','ground.glsl','pit_settings.glsl','pits.glsl')]
        report=dict(renderer=gl.renderer,
            source_sha256={str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in paths},
            region_scale_m=scale,river_spacing_m=const('COLD_FOREST_RIVER_SPACING'),
            region_low=low,region_high=high,central_threshold=(low+high)*.5,
            calibrated_30_percent_threshold=threshold,
            total_coverage_samples=sum(r['samples'] for r in records),records=records,
            aggregate_dominant_fraction=coverage,
            aggregate_mean_mask=float(np.mean([r['mean_mask'] for r in records])),
            maximum_mask_step_at_250m=max_jump,whole_pit_admission=admissions,
            note='30% means ensemble area with regional mask >= 0.5; the smoothly weighted mean is reported separately. The river channel width is independent of this broad regional coverage. The wavelength comparison uses the same final 20-km pits and 50-km candidate cells; only regional wavelength changes. Whole-disk river/region safety is covered by check_cold_forest.py.')
    if args.output:
        args.output.parent.mkdir(parents=True,exist_ok=True)
        args.output.write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report,indent=2))


if __name__=='__main__':main()
