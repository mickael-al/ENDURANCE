#!/usr/bin/env python3
"""Probe actual FrozenTundra regions, plain detail and rare connected 3D caves."""
from pathlib import Path
import argparse
import json
import re
import numpy as np
from scipy import ndimage
from biome_test_support import biome_recipe_source
from check_biomes import BiomeProbe, profile_probe_source
from check_biome_landforms import DensityProbe, SCENE_OPTIONS, check_targeted_pipeline, source_fingerprint
from check_gpu import FieldRunner
from gl_headless import GL

HEADER = r'''
layout(local_size_x=8,local_size_y=8) in;
struct ProbeSample { vec4 terrain; vec4 volume; vec4 samples; vec4 shape; };
layout(std430,binding=6) writeonly buffer ProbeOutput { ProbeSample outputValues[]; };
uniform uint uProbeSize;
uniform uint uProbeMode;
uniform vec2 uProbeOrigin;
uniform float uProbeSpacing;
uniform float uProbeStepY;
uniform float uProbeMinY;
'''
REGIONS = HEADER + r'''
void main() {
 uvec2 i=gl_GlobalInvocationID.xy;
 if(any(greaterThanEqual(i,uvec2(uProbeSize))))return;
 vec2 p=uProbeOrigin+vec2(i)*vec2(uProbeSpacing,uProbeStepY);
 vec4 raw=fieldRelief(p);
 outputValues[i.x+i.y*uProbeSize]=ProbeSample(
  vec4(raw.x*.35,frozenMountainPresence(p),frozenPlainHeight(p),raw.y*.35),
  vec4(p,raw.z,raw.w),vec4(0),vec4(0));
}
'''



CAVE_SITES = HEADER + r'''
void main() {
 uvec2 i=gl_GlobalInvocationID.xy;
 if(any(greaterThanEqual(i,uvec2(uProbeSize))))return;
 ivec2 cell=ivec2(i)-ivec2(int(uProbeSize/2u));
 vec2 p=(vec2(cell)+0.5)*FROZEN_CAVE_SPACING;
 LandSite s=frozenCaveSite(p);p=s.center;s=frozenCaveSite(p);
 float presence=frozenMountainPresence(p);
 vec4 raw=fieldRelief(p);
 float support=frozenCaveSupport(p,s,presence);
 vec2 axis=frozenCaveAxis(s);
 outputValues[i.x+i.y*uProbeSize]=ProbeSample(vec4(p,s.mask,presence),
  vec4(raw.x*.35,raw.y*.35,s.radius,s.variant),
  vec4(axis,support,frozenCaveHeightGate(raw.x*.35-FROZEN_CAVE_PLAIN_CEILING)),
  vec4(cell,0,0));
}
'''


def find_cave_candidates(gl,seed,size=41):
 rows=values(BiomeProbe(gl,source=CAVE_SITES),size=size,mode=1,origin=(0.,0.),spacing=1.,uSeed=seed).reshape(-1,16)
 active=rows[:,2]>.99;regional=active&(rows[:,10]>.99);eligible=regional&(rows[:,11]>.99)&(rows[:,5]>0)
 candidates=rows[eligible];candidates=candidates[np.argsort(np.sum(candidates[:,:2]**2,axis=1))]
 assert len(candidates)>=3,'Too few complete rare cave candidates to verify entrances'
 assert .04<active.mean()<.16,'Cave candidates are no longer rare'
 assert eligible.mean()<.05,'Mountain cave eligibility became too frequent'
 assert np.all(rows[rows[:,3]==0,5]==0),'Cave candidates enable bands in plains'
 return candidates,dict(cells_sampled=len(rows),active_candidates=int(active.sum()),full_region_candidates=int(regional.sum()),
  strong_mountain_candidates=int(eligible.sum()),candidate_fraction=float(active.mean()),eligible_fraction=float(eligible.mean()))


def sample_cave(actual,baseline,center,seed,size=129):
 extent=800.;spacing=2*extent/(size-1);origin=tuple(np.asarray(center)-extent)
 columns=actual.sample(1,origin,spacing,0.,1.,size=size,depth=1,seed=seed,**SCENE_OPTIONS)['columns']
 step=12.5;low=float(np.floor((columns[:,:,0].min()-450.)/step)*step);high=float(np.ceil((columns[:,:,0].max()+120.)/step)*step)
 depth=int(round((high-low)/step))+1
 opts=dict(biome=1,origin=origin,spacing=spacing,min_y=low,step_y=step,size=size,depth=depth,seed=seed,**SCENE_OPTIONS)
 return actual.sample(**opts),baseline.sample(**opts)


class WithoutCaves(DensityProbe):
 def program_for(self, biome):
  if biome not in self.programs:
   directory=self.directory/'biomes'/'FrozenTundra'
   relief=biome_recipe_source(directory/'relief.C.glsl')
   volume=re.sub(r'^#version[^\n]*\n','',biome_recipe_source(directory/'volume.C.glsl'),count=1)
   volume,count=re.subn(r'float fieldVolume\(vec3 p,\s*FieldColumn column\)\s*\{[\s\S]*?^\}',
    'float fieldVolume(vec3 p, FieldColumn column) { return 0.0; }',volume,flags=re.MULTILINE)
   assert count==1,'Update cave omission adapter after production entrypoint change'
   self.programs[biome]=self.gl.compile([(0x91B9,relief+volume+profile_probe_source()+self.main_source)],'FrozenTundra cave omission')
  return self.programs[biome]


def cave_metrics(after_sample, before_sample):
 after=after_sample['density'];before=before_sample['density'];columns=after_sample['columns']
 assert np.array_equal(columns,before_sample['columns']),'Cave omission changed 2D columns'
 assert np.max(after-before)<.02,'Caves unexpectedly add solid terrain'
 removed=(before>0)&(after<0);solid=after>0;air=after<0
 assert removed.sum()>100,'No substantial 3D cavity was excavated'
 above=np.maximum.accumulate(solid[:,::-1,:],axis=1)[:,::-1,:]
 below=np.maximum.accumulate(solid,axis=1)
 roofed=removed&above&below
 assert roofed.sum()>100,'Excavation is a surface pit, not a chamber with roof and floor'
 labels,count=ndimage.label(air,structure=ndimage.generate_binary_structure(3,1))
 # Baseline air is connected to the exterior above the heightfield. A cavity
 # reaches it only through a genuine sampled entrance, not a cropped box edge.
 exterior_ids=np.unique(labels[(before<-.5)&air]);exterior_ids=exterior_ids[exterior_ids>0]
 connected=roofed&np.isin(labels,exterior_ids)
 assert connected.sum()>100 and connected.sum()/roofed.sum()>.95,'Roofed chamber is sealed from the exterior'
 voxel=after_sample['spacing']**2*after_sample['step_y']
 runs=np.zeros_like(air,dtype=np.int16)
 for j in range(air.shape[1]):runs[:,j,:]=np.where(air[:,j,:],(runs[:,j-1,:]+1) if j else 1,0)
 clearances=runs[roofed]
 maximum_clearance=float(clearances.max(initial=0)*after_sample['step_y'])
 assert maximum_clearance>=30.,'Large cave lacks broad vertical clearance'
 return dict(removed_samples=int(removed.sum()),roof_and_floor_samples=int(roofed.sum()),
  roofed_samples_connected_to_surface=int(connected.sum()),connected_roofed_fraction=float(connected.sum()/roofed.sum()),removed_volume_m3=float(removed.sum()*voxel),
  sampled_maximum_roofed_clearance_m=maximum_clearance,removed_fraction_of_solid=float(removed.sum()/max((before>0).sum(),1)),
  horizontal_spacing_m=after_sample['spacing'],vertical_spacing_m=after_sample['step_y']),removed,roofed


def render_cave(path,after_sample,before_sample,removed,roofed):
 import matplotlib
 matplotlib.use('Agg')
 import matplotlib.pyplot as plt
 from matplotlib.colors import ListedColormap
 after=after_sample['density'];before=before_sample['density']
 z=int(np.argmax(roofed.sum(axis=(1,2))));n,depth,_=after.shape
 x=after_sample['origin'][0]+np.arange(n)*after_sample['spacing']
 y=after_sample['min_y']+np.arange(depth)*after_sample['step_y']
 zcoords=after_sample['origin'][1]+np.arange(n)*after_sample['spacing']
 fig,axes=plt.subplots(1,3,figsize=(15,5),constrained_layout=True)
 for ax,d,title in zip(axes[:2],(before[z],after[z]),('Same terrain, caves disabled','Production chamber, roof and floor')):
  ax.imshow(d>0,origin='lower',extent=[x[0],x[-1],y[0],y[-1]],cmap=ListedColormap(['#263e51','#cbd8d5']),aspect='equal')
  ax.contour(x,y,d,levels=[0],colors='#6c959a',linewidths=.6)
  ax.set_xlabel('World X (m)');ax.set_ylabel('World height (m)');ax.set_title(title)
 axes[2].imshow(removed.any(axis=1),origin='lower',extent=[x[0],x[-1],zcoords[0],zcoords[-1]],cmap=ListedColormap(['#263e51','#a7c9c5']),aspect='equal')
 axes[2].set_xlabel('World X (m)');axes[2].set_ylabel('World Z (m)');axes[2].set_title('Excavated footprint, all heights')
 fig.suptitle('Rare FrozenTundra cave — actual GLSL density; no vertical exaggeration',fontsize=12)
 fig.savefig(path,dpi=100);plt.close(fig)


def values(probe, **options):
 return np.asarray(probe.run(biome=1,**options)['values']).reshape(options['size'],options['size'],16)


def largest_region(mask, spacing):
 labels,count=ndimage.label(mask,structure=ndimage.generate_binary_structure(2,1))
 if not count:return dict(area_km2=0.,span_x_km=0.,span_z_km=0.)
 counts=np.bincount(labels.ravel());counts[0]=0;chosen=int(np.argmax(counts))
 z,x=np.nonzero(labels==chosen)
 return dict(area_km2=float(len(x)*(spacing/1000.)**2),span_x_km=float((x.max()-x.min())*spacing/1000.),span_z_km=float((z.max()-z.min())*spacing/1000.))


def region_metrics(data, spacing):
 h=data[:,:,0];mask=data[:,:,1];plain=data[:,:,2];band=data[:,:,3]
 assert np.isfinite(data).all()
 assert mask.min()>=0 and mask.max()<=1
 plains=mask<=.001;mountains=mask>=.999;transition=(mask>.001)&(mask<.999)
 assert plains.mean()>.03 and mountains.mean()>.03,'Missing broad plains or mountain regions'
 assert transition.mean()>.03,'No gradual regional transition'
 # A local mask change at a 250 m step must not make a kilometre-scale cliff.
 jump=max(np.abs(np.diff(mask,axis=0)).max(),np.abs(np.diff(mask,axis=1)).max())
 assert jump<.20,'Regional mask changes too abruptly at the map sampling scale'
 plain_region=largest_region(plains,spacing);mountain_region=largest_region(mountains,spacing)
 assert plain_region['area_km2']>9 and mountain_region['area_km2']>9,'Regions do not cover multiple kilometres'
 assert max(plain_region['span_x_km'],plain_region['span_z_km'])>5
 assert max(mountain_region['span_x_km'],mountain_region['span_z_km'])>5
 assert np.ptp(plain[plains])>1.,'Plain recipe lacks visible height detail'
 assert np.ptp(plain[plains])<160.,'Plain recipe grew into another mountain biome'
 assert np.all(band[mask==0]==0),'Cave band reaches a region with no mountains'
 return dict(plain_fraction=float(plains.mean()),mountain_fraction=float(mountains.mean()),transition_fraction=float(transition.mean()),
  maximum_mask_step=float(jump),plain_height_range_m=[float(plain[plains].min()),float(plain[plains].max())],
  terrain_height_range_m=[float(h.min()),float(h.max())],largest_plain=plain_region,largest_mountain=mountain_region,
  nonzero_band_fraction=float((band>0).mean()))


def boundary_continuity(probe,data,origin,spacing,seed):
 mask=data[:,:,1];candidates=[]
 for level in (.0001,.9999):
  # Crossings along both world axes, not only a hand-picked contour direction.
  for axis in (0,1):
   left=np.take(mask,np.arange(mask.shape[axis]-1),axis=axis)
   right=np.take(mask,np.arange(1,mask.shape[axis]),axis=axis)
   crossing=np.argwhere((left-level)*(right-level)<0)
   for index in np.linspace(0,max(len(crossing)-1,0),min(4,len(crossing)),dtype=int):
    z,x=crossing[index];a=np.asarray(origin)+np.asarray((x,z))*spacing
    direction=np.asarray((1.,0.) if axis==1 else (0.,1.));b=a+direction*spacing
    candidates.append((level,a,b,direction))
 assert len(candidates)>=8,'Too few region boundaries sampled'
 def point(p):return values(probe,size=1,origin=tuple(p),spacing=0.,step_y=0.,uSeed=seed)[0,0]
 maxima=[]
 for level,a,b,direction in candidates:
  side=point(a)[1]<level
  for _ in range(15):
   mid=(a+b)*.5
   if (point(mid)[1]<level)==side:a=mid
   else:b=mid
  center=(a+b)*.5
  ulp=abs(float(np.spacing(np.float32(max(1.,np.abs(center).max())))))
  small=max(.0625,16*ulp)
  prior=None
  for step in (small*16,small*4,small):
   left=point(center-direction*step);right=point(center+direction*step)
   jump=np.abs(left[[0,1]]-right[[0,1]])
   if prior is not None:assert np.all(jump<=prior*.65+np.asarray((.04,.00005))),'Non-converging height/mask discontinuity'
   prior=jump
  maxima.append(float(prior[0]))
 return dict(boundaries_tested=len(candidates),largest_finest_height_difference_m=max(maxima))


def render_regions(path,data,origin,spacing):
 import matplotlib
 matplotlib.use('Agg')
 import matplotlib.pyplot as plt
 from matplotlib.colors import LightSource,LinearSegmentedColormap
 h=data[:,:,0];mask=data[:,:,1];n=len(h)
 extent=[origin[0]/1000.,(origin[0]+spacing*(n-1))/1000.,origin[1]/1000.,(origin[1]+spacing*(n-1))/1000.]
 cmap=LinearSegmentedColormap.from_list('frozen_relief',['#3b514e','#7d8d87','#b8c2c4','#f5f5f0'])
 fig,axes=plt.subplots(1,3,figsize=(16,5),constrained_layout=True)
 image=axes[0].imshow(mask,origin='lower',extent=extent,cmap='cividis',vmin=0,vmax=1);fig.colorbar(image,ax=axes[0],label='Mountain presence')
 axes[0].set_title('Large mountain regions and plains')
 shaded=LightSource(azdeg=315,altdeg=45).shade(h,cmap=cmap,vmin=0,vmax=2400,vert_exag=1,dx=spacing,dy=spacing,blend_mode='soft')
 axes[1].imshow(shaded,origin='lower',extent=extent);axes[1].set_title('Actual GLSL relief, same physical scale')
 # A row crossing many mask levels; show its complete terrain profile.
 row=int(np.argmax(mask.std(axis=1)));x=np.linspace(extent[0],extent[1],n)
 axes[2].plot(x,h[row],color='#345c69',lw=1,label='Terrain');axes[2].plot(x,data[row,:,2],color='#a17d49',lw=1,label='Plain recipe')
 axes[2].set_xlabel('World X (km)');axes[2].set_ylabel('Relief (m)');axes[2].set_title(f'Continuous section at Z={(origin[1]+row*spacing)/1000.:.1f} km')
 secondary=axes[2].twinx();secondary.plot(x,mask[row],color='#8d5597',alpha=.5,lw=.8);secondary.set_ylim(-.02,1.02);secondary.set_ylabel('Mountain presence')
 axes[2].legend(loc='upper left',fontsize=8)
 for ax in axes[:2]:ax.set_xlabel('World X (km)');ax.set_ylabel('World Z (km)')
 fig.suptitle('FrozenTundra — sampled production shaders, diagnostic outside the game',fontsize=12)
 fig.savefig(path,dpi=100);plt.close(fig)


def main():
 parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--output',type=Path,required=True);args=parser.parse_args();args.output.mkdir(parents=True,exist_ok=True)
 fingerprint=source_fingerprint(1)
 with GL() as gl:
  probe=BiomeProbe(gl,source=REGIONS);records=[];actual=DensityProbe(gl);baseline=WithoutCaves(gl)
  for seed in (1337,17):
   origin=(-48000.,-48000.);spacing=250.
   data=values(probe,size=385,origin=origin,spacing=spacing,step_y=spacing,uSeed=seed)
   metrics=region_metrics(data,spacing);metrics['continuity']=boundary_continuity(probe,data,origin,spacing,seed)
   assert np.array_equal(data,values(probe,size=385,origin=origin,spacing=spacing,step_y=spacing,uSeed=seed)),'Regional field is not deterministic'
   candidates,population=find_cave_candidates(gl,seed)
   caves=[]
   for index,candidate in enumerate(candidates[:3]):
    after,before=sample_cave(actual,baseline,candidate[:2],seed)
    measurement,removed,roofed=cave_metrics(after,before)
    record=dict(center=candidate[:2].tolist(),**measurement)
    if index==0:
     # Target an actual excavated wall, not the largest positive-solid density
     # reduction far below the chamber (the generic helper's default anchor).
     wall=np.where(roofed,np.abs(after['density']),np.inf)
     z,j,x=np.unravel_index(np.argmin(wall),wall.shape)
     target=dict(after,density=after['density'][z:z+1,j:j+1,x:x+1],
      columns=after['columns'][z:z+1,x:x+1],
      origin=(after['origin'][0]+x*after['spacing'],after['origin'][1]+z*after['spacing']),
      min_y=after['min_y']+j*after['step_y'])
     assert target['density'][0,0,0]<0 and before['density'][z,j,x]>0
     record['pipeline']=check_targeted_pipeline(FieldRunner(gl),actual,target,1,seed)
     record['pipeline']['excavated_wall_reference_density']=float(target['density'][0,0,0])
     render_cave(args.output/f'frozen_cave_{seed}.png',after,before,removed,roofed)
    caves.append(record)
   records.append(dict(seed=seed,regions=metrics,cave_population=population,caves=caves));print(json.dumps(records[-1]),flush=True)
   if seed==1337:render_regions(args.output/'frozen_regions.png',data,origin,spacing)
  assert source_fingerprint(1)==fingerprint,'Source changed during regional validation'
  report=dict(renderer=gl.renderer,source_sha256=fingerprint,records=records,note='Regions, plains and six nearest fully eligible cave candidates use actual production GLSL. Cave tests compare identical columns with excavation disabled, require roof/floor and >95% roofed air connected to above-surface air, and compare production accumulation/LOD. The regional mask has no sampled finite jump; gradient normalization gives an approximate transition distance, not an exact signed-distance solution. This is not a Windows GPU performance or collision certification.')
  (args.output/'frozen_regions.json').write_text(json.dumps(report,indent=2)+'\n')
 return 0

if __name__=='__main__':raise SystemExit(main())
