#!/usr/bin/env python3
"""Verify ColdForest's 30% mask, restored anchors and local river protection."""
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
 values[i.x+i.y*probeSize]=vec4(site.center,coldForestPitEligibility(site),
  coldForestRegionMask(site.center));
}
'''

LOCAL_MAIN = r'''
layout(local_size_x=8,local_size_y=8) in;
layout(std430,binding=6) writeonly buffer Results { vec4 values[]; };
uniform uint probeSize;
uniform float probeSpacing;
void main() {
 uvec2 i=gl_GlobalInvocationID.xy;
 if(any(greaterThanEqual(i,uvec2(probeSize))))return;
 vec2 p=(vec2(i)-float(probeSize-1u)*.5)*probeSpacing;
 values[i.x+i.y*probeSize]=vec4(coldForestRegionMask(p),
  coldForestRiverDistance(p),coldForestPitLocalMask(p),coldForestPitFields(p).x);
}
'''

# Original 10-km-grid coordinates captured before the enlarged-cell revision.
# Fixed fixtures catch a changed cell hash or phase; the restored placement must
# recover the same centers, even though local excavation now spans neighbours.
ORIGINAL_ANCHORS_1337 = {
    (0,2):(5518.52587890625,23952.875),
    (-2,-1):(-13287.423828125,-5639.1728515625),
    (1,-1):(14052.79296875,-4729.07763671875),
    (-1,-2):(-7076.83251953125,-13480.015625),
}


def const(name,filename='ground_settings.glsl'):
    text=(BIOME/filename).read_text()
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


def pit_source(main):
    return '#version 450 core\n'+shader_source(TERRAIN/'field_common.glsl') \
        +shader_source(BIOME/'pits.glsl')+main


def sample(gl,program,seed,size,spacing=None):
    gl.uniform(program,'uSeed','1ui',seed)
    gl.uniform(program,'uFieldMode','1ui',0)
    gl.uniform(program,'uDetailScale','1f',.35)
    gl.uniform(program,'uBandOffset','1f',0.)
    gl.uniform(program,'uBandOffsetVariation','1f',0.)
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
    pit_spacing=const('COLD_FOREST_PIT_SPACING','pit_settings.glsl')
    assert scale==24000. and const('COLD_FOREST_RIVER_SPACING')==9000.
    assert pit_spacing==10000.,'The original dense candidate grid was not restored'
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
        fine=sample(gl,program,1337,385,50.)
        assert np.array_equal(fine,sample(gl,program,1337,385,50.)), 'Region mask is nondeterministic'
        mask=fine[:,:,1]
        max_jump=float(max(abs(np.diff(mask,axis=0)).max(),abs(np.diff(mask,axis=1)).max()))
        assert max_jump<.2,('Abrupt regional transition at 50m resolution',max_jump)

        site_program=gl.compile([(0x91B9,pit_source(SITE_MAIN))],'ColdForest restored candidate anchors')
        admissions=[]
        for seed in (1337,17,41,99991):
            data=sample(gl,site_program,seed,257)
            assert np.isin(data[:,:,2],(0.,1.)).all()
            x,z=np.meshgrid(np.arange(-128,129),np.arange(-128,129))
            cell=np.stack((x,z),axis=-1)
            offset=data[:,:,:2]-cell*pit_spacing
            assert np.all((offset>=pit_spacing*.25-1.)&(offset<=pit_spacing*.75+1.))
            accepted=data[:,:,2]>.5
            fraction=float(accepted.mean())
            assert fraction>.25,'Restored candidate anchors remain too sparse'
            if seed==1337:
                for (cx,cz),expected in ORIGINAL_ANCHORS_1337.items():
                    assert np.allclose(data[cz+128,cx+128,:2],expected,rtol=0.,atol=.005), \
                        ('Original site hash/coordinates changed',(cx,cz))
            admissions.append(dict(seed=seed,candidates=257**2,
                accepted_anchors=int(accepted.sum()),admitted_fraction=fraction,
                anchors_outside_dominant_regions=int((accepted&(data[:,:,3]<.5)).sum())))

        # Requiring the entire 20-km disk to fit a region made pits disappear.
        # Test the replacement rule on actual production column depths:
        # existing anchors may overlap, with regional/river protection local to
        # each column. Scalar zero depths are independently exercised through
        # full density dispatch in check_cold_forest.py.
        local_program=gl.compile([(0x91B9,pit_source(LOCAL_MAIN))],'ColdForest local excavation support')
        local=sample(gl,local_program,1337,321,250.)
        region,distance,support,depth=local.transpose(2,0,1)
        assert np.all((support>=0.)&(support<=region+1e-6))
        inactive=region==0.
        banks=distance<=const('COLD_FOREST_RIVER_BANK_HALF_WIDTH')
        assert inactive.any() and banks.any()
        assert np.all(support[inactive|banks]==0.)
        assert np.all(depth[inactive|banks]==0.),'A pit column digs outside its region or into river banks'
        x,z=np.meshgrid((np.arange(321)-160)*250.,(np.arange(321)-160)*250.)
        distances=np.hypot(x,z)
        visible=(depth>100.)&(distances<=40000.)
        assert visible.any(),'No substantial pit patch within 40 km of the origin for seed 1337'
        nearest=np.argmin(np.where(visible,distances,np.inf))
        row,col=np.unravel_index(nearest,visible.shape)
        local_report=dict(seed=1337,samples=region.size,world_extent_km=80.,
            region_zero_columns=int(inactive.sum()),protected_bank_columns=int(banks.sum()),
            positive_pit_columns=int((depth>0.).sum()),
            substantial_pit_columns_within_40km=int(visible.sum()),
            nearest_substantial_patch_xz_m=[float(x[row,col]),float(z[row,col])],
            nearest_substantial_patch_distance_m=float(distances[row,col]),
            nearest_substantial_patch_depth_m=float(depth[row,col]))

        paths=[BIOME/p for p in ('ground_settings.glsl','ground.glsl','pit_settings.glsl','pits.glsl')]
        report=dict(renderer=gl.renderer,
            source_sha256={str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in paths},
            region_scale_m=scale,river_spacing_m=const('COLD_FOREST_RIVER_SPACING'),
            region_low=low,region_high=high,central_threshold=(low+high)*.5,
            calibrated_30_percent_threshold=threshold,
            total_coverage_samples=sum(r['samples'] for r in records),records=records,
            aggregate_dominant_fraction=coverage,
            aggregate_mean_mask=float(np.mean([r['mean_mask'] for r in records])),
            maximum_mask_step_at_50m=max_jump,candidate_spacing_m=pit_spacing,
            restored_anchor_admission=admissions,original_anchor_fixtures=len(ORIGINAL_ANCHORS_1337),
            local_excavation=local_report,
            historical_regression='The superseded whole-disk admission rejected all 264196 sampled 20-km pits at a 24-km regional wavelength. That former algorithm is not rerun here; this test instead verifies actual restored anchors and visible locally protected excavation.',
            note='30% means ensemble area with regional mask >= 0.5; the smoothly weighted mean is reported separately. Candidate cells are 10 km wide and the original hash is retained. Acceptance protects a small river-free anchor, not an entire 20-km disk. The regional mask and river-bank fade then restrict excavation per column. Full density, neighbouring-site unions and band bounds are covered by check_cold_forest.py.')
    if args.output:
        args.output.parent.mkdir(parents=True,exist_ok=True)
        args.output.write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report,indent=2))


if __name__=='__main__':main()
