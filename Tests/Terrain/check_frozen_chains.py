#!/usr/bin/env python3
"""Actual GLSL chain dimensions, masks, neighbour coverage and production LOD."""
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
  int row=int(i.y)-int(uProbeSize/2u);
  int block=int(i.x/4u)-int(uProbeSize/8u);
  int k=int(i.x%4u);
  float start=float(block)*frozenBlockLength()+frozenRowPhase(row);
  for(int j=0;j<k;++j){vec2 d=frozenDimensions(row,block,j);start+=d.x+d.y;}
  vec2 d=frozenDimensions(row,block,k);
  outputValues[i.x+i.y*uProbeSize]=ProbeSample(vec4(start,start+d.x,d),
    vec4(row,block,k,frozenBlockLength()),vec4(0),vec4(0));
 }else{
  vec2 domain=uProbeOrigin+vec2(i)*vec2(uProbeSpacing,uProbeStepY);
  vec2 axis=landSparseDirection(227u);
  vec2 p=axis*domain.x+vec2(-axis.y,axis.x)*domain.y;
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
    relief=relief.replace('rowOffset = -1; rowOffset <= 1','rowOffset = -3; rowOffset <= 3').replace('blockOffset = -1; blockOffset <= 1','blockOffset = -3; blockOffset <= 3')
   else:
    assert 'float mountainPresence = 1.0;' in relief
    relief=relief.replace('float mountainPresence = 1.0;',f'float mountainPresence = {self.edit};')
   volume=re.sub(r'^#version[^\n]*\n','',biome_recipe_source(d/'volume.C.glsl'),count=1)
   self.programs[biome]=self.gl.compile([(0x91B9,relief+volume+profile_probe_source()+SOURCE)],'Frozen chain '+self.edit)
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
 ax.set_xlabel('Along chain (km)');ax.set_ylabel('Across chains (km)');ax.set_title('Actual GLSL relief, hillshade')
 ax=fig.add_subplot(122,projection='3d')
 ax.plot_surface(xx,zz,h/1000,facecolors=shaded,rcount=100,ccount=100,linewidth=0,antialiased=True,shade=False)
 ax.set_box_aspect((x[-1]-x[0],z[-1]-z[0],3200));ax.view_init(32,-68)
 ax.set_xlabel('Along (km)',fontsize=8);ax.set_ylabel('Across (km)',fontsize=8);ax.set_zlabel('km',fontsize=8,labelpad=0);ax.tick_params(labelsize=7);ax.set_title('No vertical exaggeration')
 fig.suptitle('FrozenTundra: connected ridges and passes — production shader diagnostic, not in-game',fontsize=12)
 fig.savefig(path,dpi=110);plt.close(fig)

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True);args=p.parse_args();args.output.mkdir(parents=True,exist_ok=True)
 fingerprint=source_fingerprint(1)
 with GL() as gl:
  probe=BiomeProbe(gl,source=SOURCE);wide=Variant(gl,'wide');zero=Variant(gl,'0.0');half=Variant(gl,'0.5')
  records=[]
  for seed in (1337,17):
   dims=values(probe,size=64,mode=1,uSeed=seed)
   widths=dims[:,:,2];gaps=dims[:,:,3]
   assert widths.min()>=1000 and widths.max()<=6000
   assert gaps.min()>=-500 and gaps.max()<=250
   observed=dims[:,1:,0]-dims[:,:-1,1]
   assert np.max(np.abs(observed-gaps[:,:-1]))<.06,'Incorrect signed gaps, including block joins'
   assert (gaps<0).sum()>100 and (gaps>0).sum()>100
   opts=dict(size=65,origin=(-27000.,-18000.),spacing=54000/64,step_y=36000/64,uSeed=seed)
   a=values(probe,**opts);b=values(wide,**opts)
   assert np.array_equal(a,b),'Neighbourhood missed a contributing massif or saddle'
   assert np.array_equal(a,values(probe,**opts)),'Non-deterministic heights'
   off=values(zero,**opts);partial=values(half,**opts)
   assert np.all(off[:,:,0]==0) and np.allclose(partial[:,:,0],a[:,:,0]*.5,atol=.001)
   assert np.all(a[:,:,3]==0),'Removed volume features re-enabled'
   assert a[:,:,0].max()>2500 and a[:,:,0].max()<3200.01
   z,x=np.unravel_index(a[:,:,0].argmax(),a[:,:,0].shape)
   world=a[z,x,1:3];height=a[z,x,0]
   density=DensityProbe(gl)
   sample=density.sample(1,tuple(world-4),1.,float(height-4),1.,size=9,depth=9,seed=seed,**SCENE_OPTIONS)
   pipeline=check_targeted_pipeline(FieldRunner(gl),density,sample,1,seed)
   records.append(dict(seed=seed,dimensions_tested=int(widths.size),width_range_m=[float(widths.min()),float(widths.max())],gap_range_m=[float(gaps.min()),float(gaps.max())],maximum_gap_error_m=float(np.max(np.abs(observed-gaps[:,:-1]))),peak_sample_m=float(height),pipeline=pipeline))
   print(json.dumps(records[-1]),flush=True)
  origin=(-12000.,-4500.);steps=(24000/256,9000/256)
  atlas=values(probe,size=257,origin=origin,spacing=steps[0],step_y=steps[1],uSeed=1337)
  render(args.output/'frozen_chains.png',atlas,origin,steps)
  assert source_fingerprint(1)==fingerprint
  report=dict(renderer=gl.renderer,source_sha256=fingerprint,records=records,note='Widths and gaps measured along rotated chain axis; curved contours and connecting passes change visible ground separation. Presence 0/.5/1 tested; bounded neighbourhood matches 7x7-block reference; not a performance or collision certification.')
  (args.output/'frozen_chains.json').write_text(json.dumps(report,indent=2)+'\n')
 return 0
if __name__=='__main__':raise SystemExit(main())
