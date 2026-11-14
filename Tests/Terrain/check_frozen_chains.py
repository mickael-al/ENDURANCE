#!/usr/bin/env python3
"""Isolated production mountain geometry, shared gaps, masks and actual LOD.

The fixture bypasses the regional/plain composition only, so placement/shape
regressions remain comparable. Actual region/cave behavior has its own probe.
"""
from pathlib import Path
import argparse,json,re
import numpy as np
from biome_test_support import biome_recipe_source
from check_biomes import BiomeProbe,profile_probe_source
from check_biome_landforms import DensityProbe,SCENE_OPTIONS,check_targeted_pipeline,source_fingerprint
from check_gpu import FieldRunner
from gl_headless import GL

HEADER=r'''
layout(local_size_x=8,local_size_y=8) in;
struct ProbeSample {vec4 terrain;vec4 volume;vec4 samples;vec4 shape;};
layout(std430,binding=6) writeonly buffer ProbeOutput {ProbeSample outputValues[];};
uniform uint uProbeSize;
uniform uint uProbeMode;
uniform vec2 uProbeOrigin;
uniform float uProbeSpacing;
uniform float uProbeStepY;
uniform float uProbeMinY;
'''
HEIGHT_SOURCE=HEADER+r'''
void main(){
 uvec2 i=gl_GlobalInvocationID.xy;
 if(any(greaterThanEqual(i,uvec2(uProbeSize))))return;
 vec2 p=uProbeOrigin+vec2(i)*vec2(uProbeSpacing,uProbeStepY);
 vec4 raw=fieldRelief(p);
 outputValues[i.x+i.y*uProbeSize]=ProbeSample(vec4(raw.x*.35,p,raw.y),vec4(0),vec4(0),vec4(0));
}
'''
SOURCE=HEADER+r'''
void main(){
 uvec2 i=gl_GlobalInvocationID.xy;
 if(any(greaterThanEqual(i,uvec2(uProbeSize))))return;
 uint address=i.x+i.y*uProbeSize;
 if(uProbeMode==1u && uProbeMinY<0.5){
  // Six neighbor directions, multiple tangential samples on each shared face,
  // positive/negative sites. Test the geometry, not only the gap parameter.
  const ivec2 directions[6]=ivec2[6](ivec2(1,0),ivec2(0,1),ivec2(-1,1),
    ivec2(-1,0),ivec2(0,-1),ivec2(1,-1));
  ivec2 a=ivec2(int(i.x/8u)-4,int(i.y/6u)-5);
  ivec2 b=a+directions[i.y%6u];
  vec2 ca=frozenSiteCenter(a),cb=frozenSiteCenter(b);
  vec2 normal=normalize(cb-ca),tangent=vec2(-normal.y,normal.x);
  float t=(float(i.x%8u)-3.5)*160.0;
  vec2 q=(ca+cb)*0.5+tangent*t;
  float da=frozenPairPlane(q,a,b),db=frozenPairPlane(q,b,a);
  // Along the normal, tangent/wave remain fixed and the plane derivative is1.
  vec2 edgeA=q-normal*da,edgeB=q+normal*db;
  float gap=frozenPairGap(a,b),observed=dot(edgeB-edgeA,normal);
  vec2 fpA=frozenFootprint(edgeA,a),fpB=frozenFootprint(edgeB,b);
  vec2 middle=(edgeA+edgeB)*0.5;
  vec2 ha=frozenMountainParts(middle,a),hb=frozenMountainParts(middle,b);
  float passHeight=0.0;vec2 passPoint=(ca+cb)*0.5;
  for(int scan=-4;scan<=4;++scan){
   vec2 point=(ca+cb)*0.5+tangent*float(scan)*90.0;
   float h=frozenMountainPass(point,a,b);
   if(h>passHeight){passHeight=h;passPoint=point;}
  }
  float passReverse=frozenMountainPass(passPoint,b,a);
  float passEnds=max(frozenMountainPass(ca,a,b),frozenMountainPass(cb,a,b));
  float productionAbovePass=i.x%8u==0u ? fieldRelief(passPoint).x*.35-passHeight : 0.0;
  outputValues[address]=ProbeSample(vec4(gap,observed,da+db-gap,length(cb-ca)),
    vec4(frozenPairPlane(edgeA,a,b),frozenPairPlane(edgeB,b,a),fpA.x,fpB.x),
    vec4(min(ha.x,hb.x),max(ha.x,hb.x),min(max(ha.x,ha.y),max(hb.x,hb.y)),productionAbovePass),
    vec4(gap-frozenPairGap(b,a),passHeight,passHeight-passReverse,passEnds));
 }else if(uProbeMode==1u){
  ivec2 site=ivec2(int(i.y%8u)-4,int(i.y/8u)-4);
  vec2 center=frozenSiteCenter(site);
  float angle=float(i.x)*6.2831853072/float(uProbeSize);
  vec2 direction=vec2(cos(angle),sin(angle));
  float inside=frozenFootprint(center+direction*500.0,site).x;
  float outside=frozenFootprint(center+direction*3000.0,site).x;
  float lo=500.0,hi=3000.0;
  for(int iteration=0;iteration<15;++iteration){
   float middle=(lo+hi)*0.5;
   if(frozenFootprint(center+direction*middle,site).x>0.0)lo=middle;else hi=middle;
  }
  outputValues[address]=ProbeSample(vec4((lo+hi)*0.5,inside,outside,frozenFootprint(center,site).y),
    vec4(center,site),vec4(0),vec4(0));
 }else{
  vec2 p=uProbeOrigin+vec2(i)*vec2(uProbeSpacing,uProbeStepY);
  vec4 raw=fieldRelief(p);
  outputValues[address]=ProbeSample(vec4(raw.x*.35,p,raw.y),vec4(0),vec4(0),vec4(0));
 }
}
'''
ISOLATED_SOURCE=HEADER+r'''
void main(){
 uvec2 i=gl_GlobalInvocationID.xy;
 if(any(greaterThanEqual(i,uvec2(uProbeSize))))return;
 ivec2 site=ivec2(int(uProbeMinY),int(uProbeStepY));
 vec2 p=frozenSiteCenter(site)+uProbeOrigin+vec2(i)*uProbeSpacing;
 vec2 parts=frozenMountainParts(p,site);
 outputValues[i.x+i.y*uProbeSize]=ProbeSample(vec4(max(parts.x,parts.y),parts,frozenFootprint(p,site).x),vec4(0),vec4(0),vec4(0));
}
'''

def isolated_massif_recipe(path):
 source=biome_recipe_source(path)
 fixture='''vec4 fieldRelief(vec2 p)
{
    float mountainPresence = 1.0;
    return vec4(frozenChainHeight(p) * mountainPresence / 0.35, 0.0, 0.0, 0.0);
}'''
 source,count=re.subn(r'vec4 fieldRelief\(vec2 p\)\s*\{[\s\S]*?^\}',fixture,source,flags=re.MULTILINE)
 assert count==1,'Update isolated massif adapter after production entrypoint change'
 return source

class Variant(BiomeProbe):
 def __init__(self,gl,mode='1.0',source=SOURCE):super().__init__(gl,source=source);self.edit=mode
 def program_for(self,biome):
  if biome not in self.programs:
   d=self.directory/'biomes'/'FrozenTundra'
   relief=isolated_massif_recipe(d/'relief.C.glsl')
   if self.edit=='wide':
    for symbol,old,new in (('FROZEN_SITE_SEARCH_RADIUS',2,4),('FROZEN_FACE_SEARCH_RADIUS',3,5)):
     relief,count=re.subn(r'\b'+symbol+r'\s*=\s*'+str(old)+r'\b',symbol+' = '+str(new),relief)
     assert count==1,f'Missing bounded-neighbour constant: {symbol}'
   else:
    assert 'float mountainPresence = 1.0;' in relief
    replacement='smoothstep(-4000.0, 4000.0, p.x)' if self.edit=='spatial' else self.edit
    relief=relief.replace('float mountainPresence = 1.0;',f'float mountainPresence = {replacement};')
   volume=re.sub(r'^#version[^\n]*\n','',biome_recipe_source(d/'volume.C.glsl'),count=1)
   self.programs[biome]=self.gl.compile([(0x91B9,relief+volume+profile_probe_source()+self.main_source)],'Frozen isolated mountains '+self.edit)
  return self.programs[biome]

def values(probe,**opts):return np.asarray(probe.run(biome=1,**opts)['values']).reshape(opts['size'],opts['size'],16)

def plot_relief(ax,h,origin,steps,*,three_d=False):
 from matplotlib.colors import LightSource,LinearSegmentedColormap
 n=len(h);x=origin[0]+np.arange(n)*steps[0];z=origin[1]+np.arange(n)*steps[1]
 xx,zz=np.meshgrid(x/1000,z/1000)
 cmap=LinearSegmentedColormap.from_list('ice_relief',['#2b3942','#627581','#a6b1b5','#f4f6f7'])
 shaded=LightSource(azdeg=315,altdeg=45).shade(h,cmap=cmap,vmin=0,vmax=3200,vert_exag=1,dx=steps[0],dy=steps[1],blend_mode='soft')
 if three_d:
  ax.plot_surface(xx,zz,h/1000,facecolors=shaded,rcount=100,ccount=100,linewidth=0,antialiased=True,shade=False)
  ax.set_box_aspect((x[-1]-x[0],z[-1]-z[0],3200));ax.view_init(32,-68);ax.set_zlim(0,3.2)
  ax.set_zlabel('km',fontsize=8,labelpad=0);ax.tick_params(labelsize=7)
 else:
  ax.imshow(shaded,origin='lower',extent=[x[0]/1000,x[-1]/1000,z[0]/1000,z[-1]/1000],aspect='equal')
 ax.set_xlabel('World X (km)');ax.set_ylabel('World Z (km)')

def render(path,data,origin,steps,before=None):
 import matplotlib
 matplotlib.use('Agg')
 import matplotlib.pyplot as plt
 fig=plt.figure(figsize=(14,6 if before is None else 11),constrained_layout=True)
 if before is None:
  ax=fig.add_subplot(121);plot_relief(ax,data[:,:,0],origin,steps);ax.set_title('Actual GLSL relief, hillshade')
  ax=fig.add_subplot(122,projection='3d');plot_relief(ax,data[:,:,0],origin,steps,three_d=True);ax.set_title('No vertical exaggeration')
 else:
  for column,(title,item) in enumerate((('Before: aligned massifs',before),('After: irregular overlapping massifs',data))):
   ax=fig.add_subplot(2,2,column+1);plot_relief(ax,item[:,:,0],origin,steps);ax.set_title(title)
   ax=fig.add_subplot(2,2,column+3,projection='3d');plot_relief(ax,item[:,:,0],origin,steps,three_d=True);ax.set_title('No vertical exaggeration')
 from matplotlib.cm import ScalarMappable
 from matplotlib.colors import Normalize,LinearSegmentedColormap
 cmap=LinearSegmentedColormap.from_list('ice_relief',['#2b3942','#627581','#a6b1b5','#f4f6f7'])
 flat_axes=[ax for ax in fig.axes if ax.name!='3d']
 fig.colorbar(ScalarMappable(norm=Normalize(0,3.2),cmap=cmap),ax=flat_axes,label='Relief (km)',fraction=.035,pad=.03)
 fig.suptitle('FrozenTundra: isolated mountain geometry, regional mask omitted — no vertical exaggeration',fontsize=12)
 fig.savefig(path,dpi=100);plt.close(fig)

def largest_component_area(mask,step_m):
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

def gap_record(data):
 d=data.reshape(-1,16);gaps=d[:,0];observed=d[:,1]
 assert gaps.min()>=-500 and gaps.max()<=250
 assert np.all(d[:,3]>=1510.-.02),'Jitter exceeded minimum site separation'
 assert np.all(d[:,12]==0),'Pair gap depends on owner order'
 assert np.max(np.abs(observed-gaps))<.06,'Shared footprint gaps disagree with geometric boundary separation'
 assert np.max(np.abs(d[:,2]))<.06 and np.max(np.abs(d[:,4:6]))<.06,'Reciprocal curved boundaries disagree'
 active=(d[:,6]>=-.06)&(d[:,7]>=-.06)
 assert active.sum()>1000,'Insufficient active shared boundaries'
 assert (gaps[active]>0).sum()>100 and (gaps[active]<0).sum()>100
 overlaps=active&(gaps<-100)
 assert (d[overlaps,8]>0).mean()>.95,'Main mountains fail to overlap inside negative shared gaps'
 assert np.all(d[:,14]==0),'A pass changes when its endpoints are reversed'
 assert np.all(d[:,15]<.10),'Pass does not taper out at its endpoints (float tolerance)'
 assert np.all(d[gaps>=0,13]==0),'Positive gaps unexpectedly get high overlap passes'
 assert d[:,11].min()>-.02,'A production sample omits its contributing pass'
 strong=active&(gaps<-350)&(d[:,3]<4000)
 assert strong.sum()>100 and np.median(d[strong,13])>500,'Strong overlaps lack high connecting saddles'
 result=dict(pair_samples=len(d),active_pair_samples=int(active.sum()),gap_range_m=[float(gaps.min()),float(gaps.max())],
  maximum_gap_error_m=float(np.max(np.abs(observed-gaps))),overlap_fraction=float((gaps[active]<0).mean()),
  substantial_overlap_samples=int(overlaps.sum()),both_main_heights_positive_fraction=float((d[overlaps,8]>0).mean()),
  median_lower_main_height_in_overlap_m=float(np.median(d[overlaps,8])),median_lower_total_height_in_overlap_m=float(np.median(d[overlaps,10])),
  strong_overlap_pass_samples=int(strong.sum()),median_strong_overlap_pass_m=float(np.median(d[strong,13])),maximum_pass_reciprocity_error_m=float(np.abs(d[:,14]).max()),maximum_pass_endpoint_residual_m=float(d[:,15].max()))
 return result

def footprint_record(data):
 radii=data[:,:,0];n=radii.shape[1]
 assert np.all(data[:,:,1]>0),'Some main footprint does not contain the 500m disk'
 assert np.all(data[:,:,2]<0),'Some main footprint escapes the 3000m disk'
 widths=radii+np.roll(radii,n//2,axis=1)
 assert widths.min()>=1000 and widths.max()<=6000
 return dict(footprints_sampled=len(data),rays_per_footprint=n,diameter_through_site_range_m=[float(widths.min()),float(widths.max())],
  minimum_500m_circle_margin_m=float(data[:,:,1].min()),maximum_3000m_circle_margin_m=float(data[:,:,2].max()))

def coverage_record(height,step_m):
 assert np.all(np.isfinite(height)) and np.all(height>=0)
 low=height<=1.0
 result=dict(area_at_or_below_1m_fraction=float(low.mean()),area_below_100m_fraction=float((height<100).mean()),
  median_height_m=float(np.median(height)),maximum_height_m=float(height.max()),worst_row_low_fraction=float(low.mean(axis=1).max()),
  worst_column_low_fraction=float(low.mean(axis=0).max()),largest_low_component_km2=largest_component_area(low,step_m),sample_step_m=step_m,sample_span_km=48)
 assert result['area_at_or_below_1m_fraction']<=.01,'Broad bare plains remain with mountainPresence=1'
 assert result['worst_row_low_fraction']<=.05 and result['worst_column_low_fraction']<=.05,'Flat corridors remain'
 assert result['largest_low_component_km2']<=.25,'Large connected low plain remains'
 assert result['median_height_m']>=250,'Mountain relief does not fill the biome'
 return result

def shape_atlas(gl,path):
 import matplotlib
 matplotlib.use('Agg')
 import matplotlib.pyplot as plt
 probe=Variant(gl,source=ISOLATED_SOURCE);fig,axes=plt.subplots(3,3,figsize=(10,10),constrained_layout=True)
 records=[];size=97;spacing=6000/(size-1);x=np.linspace(-3000,3000,size);xx,zz=np.meshgrid(x,x)
 for index,ax in enumerate(axes.flat):
  site=(index%3-1,index//3-1)
  d=values(probe,size=size,origin=(-3000.,-3000.),spacing=spacing,min_y=float(site[0]),step_y=float(site[1]),uSeed=1337)
  h=d[:,:,1];weights=np.maximum(h-.25*h.max(),0)**2;weights/=weights.sum()
  mx=(weights*xx).sum();mz=(weights*zz).sum();dx=xx-mx;dz=zz-mz
  cov=np.array([[(weights*dx*dx).sum(),(weights*dx*dz).sum()],[(weights*dx*dz).sum(),(weights*dz*dz).sum()]])
  eigenvectors=np.linalg.eigh(cov);ratio=float(np.sqrt(eigenvectors[0][-1]/eigenvectors[0][0]));v=eigenvectors[1][:,-1]
  records.append(dict(site=site,main_peak_m=float(h.max()),height_mass_aspect_ratio=ratio,major_axis_degrees=float(np.degrees(np.arctan2(v[1],v[0]))%180)))
  plot_relief(ax,d[:,:,0],(-3000.,-3000.),(spacing,spacing));ax.set_title(f'Site {site}; aspect {ratio:.2f}');ax.set_xlabel('Local X (km)');ax.set_ylabel('Local Z (km)')
 fig.suptitle('Nine isolated production massifs — global forms and crests, seed 1337',fontsize=13)
 fig.savefig(path,dpi=90);plt.close(fig)
 # Report silhouette statistics for review; no arbitrary aesthetic pass/fail.
 return records

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True);p.add_argument('--before-shaders',type=Path);args=p.parse_args();args.output.mkdir(parents=True,exist_ok=True)
 fingerprint=source_fingerprint(1)
 with GL() as gl:
  probe=Variant(gl);wide=Variant(gl,'wide');zero=Variant(gl,'0.0');half=Variant(gl,'0.5');spatial=Variant(gl,'spatial')
  records=[]
  for seed in (1337,17):
   pairs=values(probe,size=64,mode=1,min_y=0.,uSeed=seed);gaps=gap_record(pairs)
   radii=values(probe,size=64,mode=1,min_y=1.,uSeed=seed);footprints=footprint_record(radii)
   # Ring expansion also covers isolated footprint queries, not only final max.
   assert np.array_equal(radii,values(wide,size=64,mode=1,min_y=1.,uSeed=seed)),'Face search missed a footprint boundary'
   opts=dict(size=65,origin=(-27000.,-27000.),spacing=54000/64,step_y=54000/64,uSeed=seed)
   a=values(probe,**opts);b=values(wide,**opts)
   assert np.array_equal(a,b),'Neighbourhood missed a contributing massif or saddle'
   assert np.array_equal(a,values(probe,**opts)),'Non-deterministic heights'
   off=values(zero,**opts);partial=values(half,**opts);masked=values(spatial,**opts)
   assert np.all(off[:,:,0]==0) and np.allclose(partial[:,:,0],a[:,:,0]*.5,atol=.001,rtol=0)
   t=np.clip((a[:,:,1]+4000.)/8000.,0.,1.);presence=t*t*(3.-2.*t)
   assert np.allclose(masked[:,:,0],a[:,:,0]*presence,atol=.002,rtol=0),'Spatial presence must mask every mountain contribution'
   assert np.all(masked[:,:,0][presence==0]==0) and np.any((presence>0)&(presence<1))
   assert np.all(a[:,:,3]==0),'Isolated shape fixture unexpectedly enables volume bands'
   assert a[:,:,0].max()>2500 and a[:,:,0].max()<3200.01
   coverage=coverage_record(values(probe,size=385,origin=(-24000.,-24000.),spacing=125.,step_y=125.,uSeed=seed)[:,:,0],125.)
   z,x=np.unravel_index(a[:,:,0].argmax(),a[:,:,0].shape);world=a[z,x,1:3];height=a[z,x,0]
   actual=BiomeProbe(gl).run(biome=1,size=1,origin=tuple(world),spacing=0.,uSeed=seed,**SCENE_OPTIONS)
   actual_height=float(actual['values'][0])
   density=DensityProbe(gl);sample=density.sample(1,tuple(world-4),1.,actual_height-4.,1.,size=9,depth=9,seed=seed,**SCENE_OPTIONS)
   pipeline=check_targeted_pipeline(FieldRunner(gl),density,sample,1,seed)
   records.append(dict(seed=seed,gaps=gaps,footprints=footprints,coverage=coverage,peak_sample_m=float(height),pipeline=pipeline))
   print(json.dumps(records[-1]),flush=True)
  origin=(-12000.,-12000.);steps=(24000/384,24000/384)
  atlas=values(probe,size=385,origin=origin,spacing=steps[0],step_y=steps[1],uSeed=1337)
  render(args.output/'frozen_chains.png',atlas,origin,steps)
  if args.before_shaders:
   old=BiomeProbe(gl,directory=args.before_shaders,source=HEIGHT_SOURCE)
   before=values(old,size=385,origin=origin,spacing=steps[0],step_y=steps[1],uSeed=1337)
   render(args.output/'frozen_comparison.png',atlas,origin,steps,before)
  shapes=shape_atlas(gl,args.output/'frozen_massifs.png')
  assert source_fingerprint(1)==fingerprint
  report=dict(renderer=gl.renderer,source_sha256=fingerprint,records=records,shape_samples=shapes,
   note='Regional/plain composition is omitted only in the isolated shape fixture; production accumulation and LOD use the actual regional surface. Shared curved main-footprint boundaries have signed separation -500..250m along their pair normal at fixed tangent. Negative values genuinely overlap the two main profiles; connecting foothills and high curved overlap passes extend beyond main footprints. Pass reciprocity, endpoints, positive-gap absence and inclusion in production are tested. Every main footprint contains a 500m disk and stays within radius3000m. Presence 0/.5/1 and a spatial mask tested; production site/face rings2/3 match expanded rings4/5. Coverage sampled over48x48km. Not a performance, in-game visual, or collision certification.')
  (args.output/'frozen_chains.json').write_text(json.dumps(report,indent=2)+'\n')
 return 0
if __name__=='__main__':raise SystemExit(main())
