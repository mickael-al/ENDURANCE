#!/usr/bin/env python3
"""Actual GLSL dense mountain dimensions, coverage, masks and production LOD."""
from pathlib import Path
import argparse,json,re
import numpy as np
from biome_test_support import biome_recipe_source
from check_biomes import BiomeProbe,profile_probe_source
from check_biome_landforms import DensityProbe,SCENE_OPTIONS,check_targeted_pipeline,source_fingerprint
from check_gpu import FieldRunner
from gl_headless import GL

SOURCE=r'''
layout(local_size_x=8,local_size_y=8) in;
struct ProbeSample {vec4 terrain;vec4 volume;vec4 samples;vec4 shape;};
layout(std430,binding=6) writeonly buffer ProbeOutput {ProbeSample outputValues[];};
uniform uint uProbeSize;
uniform uint uProbeMode;
uniform vec2 uProbeOrigin;
uniform float uProbeSpacing;
uniform float uProbeStepY;
void main(){
 uvec2 i=gl_GlobalInvocationID.xy;
 if(any(greaterThanEqual(i,uvec2(uProbeSize))))return;
 if(uProbeMode==1u){
  int axis=int(i.y%2u);
  int block=int(i.x/4u)+(int(i.y/2u)-int(uProbeSize/4u))*int(uProbeSize/4u);
  int k=int(i.x%4u);
  // Keep offsets local to a block so large world positions do not obscure
  // sub-metre gap checks with float coordinate quantisation.
  float start=0.0;
  for(int j=0;j<k;++j){vec2 d=frozenDimensions(axis,block,j);start+=d.x+d.y;}
  vec2 d=frozenDimensions(axis,block,k);
  outputValues[i.x+i.y*uProbeSize]=ProbeSample(vec4(start,start+d.x,d),
    vec4(axis,block,k,frozenBlockLength()),vec4(frozenRowPhase(axis),0,0,0),vec4(0));
 }else{
  // World-space sampling includes the full rotation and shared domain shears.
  vec2 p=uProbeOrigin+vec2(i)*vec2(uProbeSpacing,uProbeStepY);
  vec4 raw=fieldRelief(p);
  outputValues[i.x+i.y*uProbeSize]=ProbeSample(vec4(raw.x*.35,p,raw.y),vec4(0),vec4(0),vec4(0));
 }
}
'''

class Variant(BiomeProbe):
 def __init__(self,gl,mode):super().__init__(gl,source=SOURCE);self.edit=mode
 def program_for(self,biome):
  if biome not in self.programs:
   d=self.directory/'biomes'/'FrozenTundra'
   relief=biome_recipe_source(d/'relief.C.glsl')
   if self.edit=='wide':
    for axis in ('z','x'):
     old=f'{axis}BlockOffset = -1; {axis}BlockOffset <= 1'
     assert relief.count(old)==1,f'Missing bounded-neighbour loop: {axis}'
     relief=relief.replace(old,f'{axis}BlockOffset = -3; {axis}BlockOffset <= 3')
   else:
    assert 'float mountainPresence = 1.0;' in relief
    relief=relief.replace('float mountainPresence = 1.0;',f'float mountainPresence = {"smoothstep(-4000.0, 4000.0, p.x)" if self.edit=="spatial" else self.edit};')
   volume=re.sub(r'^#version[^\n]*\n','',biome_recipe_source(d/'volume.C.glsl'),count=1)
   self.programs[biome]=self.gl.compile([(0x91B9,relief+volume+profile_probe_source()+SOURCE)],'Frozen dense mountains '+self.edit)
  return self.programs[biome]

def values(probe,**opts):return np.asarray(probe.run(biome=1,**opts)['values']).reshape(opts['size'],opts['size'],16)

def render(path,data,origin,steps):
 import matplotlib
 matplotlib.use('Agg')
 import matplotlib.pyplot as plt
 from matplotlib.colors import LightSource,LinearSegmentedColormap
 h=data[:,:,0];n=len(h);x=origin[0]+np.arange(n)*steps[0];z=origin[1]+np.arange(n)*steps[1]
 xx,zz=np.meshgrid(x/1000,z/1000)
 fig=plt.figure(figsize=(14,6),constrained_layout=True)
 cmap=LinearSegmentedColormap.from_list('ice_relief',['#2b3942','#627581','#a6b1b5','#f4f6f7'])
 ax=fig.add_subplot(121)
 shaded=LightSource(azdeg=315,altdeg=45).shade(h,cmap=cmap,vert_exag=1,dx=steps[0],dy=steps[1],blend_mode='soft')
 ax.imshow(shaded,origin='lower',extent=[x[0]/1000,x[-1]/1000,z[0]/1000,z[-1]/1000],aspect='equal')
 ax.contour(xx,zz,h,levels=[500,1500,2500],colors='white',linewidths=.35,alpha=.5)
 ax.set_xlabel('World X (km)');ax.set_ylabel('World Z (km)');ax.set_title('Actual GLSL relief, hillshade')
 ax=fig.add_subplot(122,projection='3d')
 ax.plot_surface(xx,zz,h/1000,facecolors=shaded,rcount=100,ccount=100,linewidth=0,antialiased=True,shade=False)
 ax.set_box_aspect((x[-1]-x[0],z[-1]-z[0],3200));ax.view_init(32,-68)
 ax.set_xlabel('World X (km)',fontsize=8);ax.set_ylabel('World Z (km)',fontsize=8);ax.set_zlabel('km',fontsize=8,labelpad=0);ax.tick_params(labelsize=7);ax.set_title('No vertical exaggeration')
 fig.suptitle('FrozenTundra: dense mountain terrain — production shader diagnostic, not in-game',fontsize=12)
 fig.savefig(path,dpi=110);plt.close(fig)

def largest_component_area(mask,step_m):
 # Four-neighbour connectivity avoids treating diagonal low samples as a broad
 # continuous plain. No scipy dependency is needed for this regression probe.
 pending=mask.copy();largest=0;rows,cols=pending.shape
 for start in np.flatnonzero(pending):
  row,col=divmod(int(start),cols)
  if not pending[row,col]:continue
  pending[row,col]=False;stack=[(row,col)];count=0
  while stack:
   row,col=stack.pop();count+=1
   for nr,nc in ((row-1,col),(row+1,col),(row,col-1),(row,col+1)):
    if 0<=nr<rows and 0<=nc<cols and pending[nr,nc]:
     pending[nr,nc]=False;stack.append((nr,nc))
  largest=max(largest,count)
 return largest*(step_m/1000.)**2

def dimensions_record(dims):
 axes=[]
 for axis in (0,1):
  d=dims[axis::2].reshape(-1,16)
  widths=d[:,2];gaps=d[:,3]
  assert np.all(d[:,4]==axis)
  assert widths.min()>=1000 and widths.max()<=6000
  assert gaps.min()>=-500 and gaps.max()<=250
  assert np.all((d[:,8]>=0)&(d[:,8]<=d[:,7])),'Invalid axis phase'
  observed=d[1:,0]-d[:-1,1]+(d[1:,5]-d[:-1,5])*d[:-1,7]
  error=float(np.max(np.abs(observed-gaps[:-1])))
  assert error<.06,'Incorrect signed gaps, including block joins'
  assert (gaps<0).sum()>100 and (gaps>0).sum()>100
  # Each complete block must address the next one without a cumulative walk.
  assert np.allclose((widths+gaps).reshape(-1,4).sum(axis=1),d[::4,7],atol=.005,rtol=0)
  axes.append(dict(axis='X' if axis==0 else 'Z',dimensions_tested=int(widths.size),
   width_range_m=[float(widths.min()),float(widths.max())],gap_range_m=[float(gaps.min()),float(gaps.max())],
   maximum_gap_error_m=error))
 return axes

def coverage_record(height,step_m):
 assert np.all(np.isfinite(height)) and np.all(height>=0)
 low=height<=1.0
 result=dict(area_at_or_below_1m_fraction=float(low.mean()),
  area_below_100m_fraction=float((height<100).mean()),median_height_m=float(np.median(height)),
  maximum_height_m=float(height.max()),worst_row_low_fraction=float(low.mean(axis=1).max()),
  worst_column_low_fraction=float(low.mean(axis=0).max()),
  largest_low_component_km2=largest_component_area(low,step_m),sample_step_m=step_m,sample_span_km=48)
 assert result['area_at_or_below_1m_fraction']<=.01,'Broad bare plains remain with mountainPresence=1'
 assert result['worst_row_low_fraction']<=.05 and result['worst_column_low_fraction']<=.05,'Flat corridors remain'
 assert result['largest_low_component_km2']<=.25,'Large connected low plain remains'
 assert result['median_height_m']>=250,'Mountain relief does not fill the biome'
 return result

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True);args=p.parse_args();args.output.mkdir(parents=True,exist_ok=True)
 fingerprint=source_fingerprint(1)
 with GL() as gl:
  probe=BiomeProbe(gl,source=SOURCE);wide=Variant(gl,'wide');zero=Variant(gl,'0.0');half=Variant(gl,'0.5');spatial=Variant(gl,'spatial')
  records=[]
  for seed in (1337,17):
   axes=dimensions_record(values(probe,size=64,mode=1,uSeed=seed))
   opts=dict(size=65,origin=(-27000.,-27000.),spacing=54000/64,step_y=54000/64,uSeed=seed)
   a=values(probe,**opts);b=values(wide,**opts)
   assert np.array_equal(a,b),'Neighbourhood missed a contributing massif or saddle'
   assert np.array_equal(a,values(probe,**opts)),'Non-deterministic heights'
   off=values(zero,**opts);partial=values(half,**opts);masked=values(spatial,**opts)
   assert np.all(off[:,:,0]==0) and np.allclose(partial[:,:,0],a[:,:,0]*.5,atol=.001,rtol=0)
   t=np.clip((a[:,:,1]+4000.)/8000.,0.,1.);presence=t*t*(3.-2.*t)
   assert np.allclose(masked[:,:,0],a[:,:,0]*presence,atol=.002,rtol=0),'Spatial presence must mask every mountain contribution'
   assert np.all(masked[:,:,0][presence==0]==0) and np.any((presence>0)&(presence<1))
   assert np.all(a[:,:,3]==0),'Removed volume features re-enabled'
   assert a[:,:,0].max()>2500 and a[:,:,0].max()<3200.01
   coverage=coverage_record(values(probe,size=385,origin=(-24000.,-24000.),spacing=125.,step_y=125.,uSeed=seed)[:,:,0],125.)
   z,x=np.unravel_index(a[:,:,0].argmax(),a[:,:,0].shape)
   world=a[z,x,1:3];height=a[z,x,0]
   density=DensityProbe(gl)
   sample=density.sample(1,tuple(world-4),1.,float(height-4),1.,size=9,depth=9,seed=seed,**SCENE_OPTIONS)
   pipeline=check_targeted_pipeline(FieldRunner(gl),density,sample,1,seed)
   records.append(dict(seed=seed,dimensions_tested=sum(axis['dimensions_tested'] for axis in axes),axes=axes,
    coverage=coverage,peak_sample_m=float(height),pipeline=pipeline))
   print(json.dumps(records[-1]),flush=True)
  origin=(-12000.,-12000.);steps=(24000/384,24000/384)
  atlas=values(probe,size=385,origin=origin,spacing=steps[0],step_y=steps[1],uSeed=1337)
  render(args.output/'frozen_chains.png',atlas,origin,steps)
  assert source_fingerprint(1)==fingerprint
  report=dict(renderer=gl.renderer,source_sha256=fingerprint,records=records,
   note='Widths/gaps are nominal axis footprints in the mountain placement domain; shared shears, rotated silhouettes, and connecting foothills change world-space contour separation. Presence 0/.5/1 and a spatial mask tested; bounded 3x3-block neighbourhood matches a 7x7-block reference. Coverage sampled over 48x48 km in world coordinates. Not a performance, in-game visual, or collision certification.')
  (args.output/'frozen_chains.json').write_text(json.dumps(report,indent=2)+'\n')
 return 0
if __name__=='__main__':raise SystemExit(main())
