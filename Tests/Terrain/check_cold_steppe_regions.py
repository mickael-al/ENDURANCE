#!/usr/bin/env python3
"""Measure the production ColdSteppe mask against the unchanged FrozenTundra."""
from pathlib import Path
import argparse
import hashlib
import json
import re
import numpy as np

from gl_headless import GL, shader_source

ROOT = Path(__file__).resolve().parents[2]
TERRAIN = ROOT / 'Asset/Shader/ProceduralTerrain'
SEEDS = (1337, 17, 41, 12345, 99991, 73, 5641, 18263)

MAIN = r'''
layout(local_size_x=8,local_size_y=8) in;
layout(std430,binding=6) writeonly buffer Results { vec4 values[]; };
uniform uint probeSize;
uniform vec2 probeOrigin;
uniform float probeSpacing;
void main() {
 uvec2 i=gl_GlobalInvocationID.xy;
 if(any(greaterThanEqual(i,uvec2(probeSize))))return;
 vec2 p=probeOrigin+vec2(i)*probeSpacing;
 values[i.x+i.y*probeSize]=vec4(frozenRegionSignal(p),
  frozenMountainPresence(p),steppeMountainPresence(p),
  steppePlainHeight(p)-frozenPlainHeight(p));
}
'''


def source():
 return '#version 450 core\n'+shader_source(TERRAIN/'field_common.glsl')+''.join(
  shader_source(TERRAIN/'biomes'/biome/'regions.glsl')
  for biome in ('FrozenTundra','ColdSteppe'))+MAIN


def sample(gl, program, seed, size, spacing, origin):
 gl.uniform(program,'uSeed','1ui',seed)
 gl.uniform(program,'probeSize','1ui',size)
 gl.uniform(program,'probeSpacing','1f',spacing)
 gl.uniform(program,'probeOrigin','2f',*origin)
 buffer=gl.buffer(size*size*16)
 gl.bind(buffer,6)
 gl.dispatch_extent(program,size,size)
 result=np.frombuffer(gl.read(buffer,size*size*16),dtype=np.float32).reshape(size,size,4).copy()
 gl.check('ColdSteppe regional mask comparison')
 assert np.isfinite(result).all()
 return result


def main():
 parser=argparse.ArgumentParser(description=__doc__)
 parser.add_argument('--output',type=Path)
 parser.add_argument('--calibrate',action='store_true',help='Report the threshold quantile without editing production')
 args=parser.parse_args()
 copied={}
 for filename in ('chain.glsl','caves.glsl'):
  original=(TERRAIN/'biomes/FrozenTundra'/filename).read_text()
  steppe=(TERRAIN/'biomes/ColdSteppe'/filename).read_text()
  assert original.replace('FROZEN','STEPPE').replace('frozen','steppe')==steppe,filename+' changed beyond biome naming'
  copied[filename]='identical after identifier renaming'
 original=(TERRAIN/'biomes/FrozenTundra/regions.glsl').read_text().replace('FROZEN','STEPPE').replace('frozen','steppe')
 steppe=(TERRAIN/'biomes/ColdSteppe/regions.glsl').read_text()
 # Whitespace/comments may explain calibration; executable code must only
 # differ in the region threshold, never in mountain height or plain detail.
 def normalized(text):
  text=re.sub(r'//[^\n]*','',text)
  text=re.sub(r'(STEPPE_REGION_THRESHOLD\s*=\s*)[^;]+',r'\1THRESHOLD',text)
  return re.sub(r'\s+','',text)
 assert normalized(original)==normalized(steppe),'Region implementation changed beyond its threshold'
 threshold=float(re.search(r'STEPPE_REGION_THRESHOLD\s*=\s*([\d.]+)',steppe).group(1))
 records=[];signals=[]
 with GL() as gl:
  program=gl.compile([(0x91B9,source())],'ColdSteppe / FrozenTundra regional comparison')
  for seed in SEEDS:
   values=sample(gl,program,seed,513,2000.,(-512000.,-512000.))
   signal,old,new,plain=values.transpose(2,0,1)
   assert np.array_equal(plain,np.zeros_like(plain)),'The copied plain height changed'
   assert (new<=old).all(),'Reduced mountain regions grew outside the former mask'
   assert new.min()>=0. and new.max()<=1.
   before=float((old>=.5).mean());after=float((new>=.5).mean())
   record=dict(seed=seed,baseline_dominant_coverage=before,steppe_dominant_coverage=after,
    coverage_ratio=after/before,baseline_mean_presence=float(old.mean()),steppe_mean_presence=float(new.mean()),
    full_height_coverage=float((new==1.).mean()),plain_coverage=float((new==0.).mean()))
   records.append(record);signals.append(signal.ravel())
  all_signals=np.concatenate(signals)
  baseline=float((all_signals>=.02).mean())
  calibrated=float(np.quantile(all_signals,1.-.8*baseline))
  aggregate=sum(r['steppe_dominant_coverage'] for r in records)/sum(r['baseline_dominant_coverage'] for r in records)
  if not args.calibrate:
   assert .79<=aggregate<=.81,('Expected approximately 20% less mountain area',aggregate)
   assert all(.77<r['coverage_ratio']<.83 for r in records),'Mask reduction is too seed-dependent'
  # Fine sampling checks continuity at real region transitions, independently
  # of the much wider sample used for the spatial coverage measurement.
  fine=sample(gl,program,1337,385,250.,(-48000.,-48000.))
  repeated=sample(gl,program,1337,385,250.,(-48000.,-48000.))
  assert np.array_equal(fine,repeated),'Mask is not deterministic'
  mask=fine[:,:,2]
  maximum_jump=float(max(np.abs(np.diff(mask,axis=0)).max(),np.abs(np.diff(mask,axis=1)).max()))
  assert maximum_jump<.20,('Abrupt region transition',maximum_jump)
  fingerprint={str(path.relative_to(ROOT)):hashlib.sha256(path.read_bytes()).hexdigest()
   for biome in ('FrozenTundra','ColdSteppe')
   for filename in ('regions.glsl','chain.glsl','caves.glsl')
   for path in (TERRAIN/'biomes'/biome/filename,)}
  report=dict(renderer=gl.renderer,source_sha256=fingerprint,threshold=threshold,calibrated_threshold=calibrated,
   samples_per_seed=513**2,world_span_km=1024,records=records,aggregate_coverage_ratio=aggregate,
   reduction_percent=100*(1.-aggregate),maximum_mask_step_at_250m=maximum_jump,
   copied_geometry=copied,note='Dominant mountain area means presence >= 0.5. A fixed threshold gives an approximate statistical reduction, not exactly 20% in every local map. Plain details, transition formulation, mountain geometry and caves are copied unchanged.')
  if args.output:
   args.output.parent.mkdir(parents=True,exist_ok=True)
   args.output.write_text(json.dumps(report,indent=2)+'\n')
  print(json.dumps(report,indent=2))
 return 0


if __name__=='__main__':raise SystemExit(main())
