#!/usr/bin/env python3
"""Measure the production VolcanicDesert recipe and render its actual height.

Independent shape checks use polar profiles, cell-boundary samples and terrain
density signs. The preview is an offline GLSL reconstruction, not a game image.
"""
from pathlib import Path
import argparse
import json
import math
import re
import numpy as np
from scipy.spatial import cKDTree
from scipy.stats import qmc
from biome_test_support import biome_recipe_source, packed_biome_cells
from check_biomes import profile_probe_source
from check_biome_landforms import DensityProbe, SCENE_OPTIONS, check_targeted_pipeline, source_fingerprint
from check_gpu import SHADERS, FieldRunner
from field_batch_runner import default_uniforms
from gl_headless import GL

HEADER=r'''
layout(local_size_x=8,local_size_y=8) in;
layout(std430,binding=9) readonly buffer ProbeInput { vec4 queries[]; };
layout(std430,binding=6) writeonly buffer ProbeOutput { vec4 result[]; };
uniform uint uCount;
'''

HEIGHT=r'''
void main() {
 uint i=gl_GlobalInvocationID.x+gl_GlobalInvocationID.y*gl_NumWorkGroups.x*8u;
 if(i>=uCount)return;
 vec2 p=queries[i].xz;
 vec4 r=fieldRelief(p);
 FieldColumn c=fieldProbeColumn(p);
 result[4u*i]=r;
 result[4u*i+1u]=vec4(c.terrain.x,c.terrain.w,volcanicGroundHeight(p),0.);
 result[4u*i+2u]=vec4(fieldVolume(vec3(p.x,c.terrain.x+queries[i].y,p.y),c),
  fieldProbeDensity(vec3(p.x,c.terrain.x+queries[i].y,p.y),c),0.,0.);
 result[4u*i+3u]=vec4(0.);
}
'''

GROUND=r'''
void main() {
 uint i=gl_GlobalInvocationID.x+gl_GlobalInvocationID.y*gl_NumWorkGroups.x*8u;
 if(i>=uCount)return;
 vec2 p=queries[i].xz;
 VolcanicGroundCrack c=volcanicGroundCrack(p);
 result[4u*i]=vec4(volcanicGroundHeight(p),c.edge,c.depth,0.);
 result[4u*i+1u]=vec4(c.warped,0.,0.);
 result[4u*i+2u]=vec4(VOLCANIC_GROUND_PLATE_SCALE,VOLCANIC_GROUND_CRACK_WIDTH,
  VOLCANIC_GROUND_CRACK_DEPTH,VOLCANIC_GROUND_WARP_STRENGTH);
 result[4u*i+3u]=vec4(0.);
}
'''

SITES=r'''
void main() {
 uint i=gl_GlobalInvocationID.x+gl_GlobalInvocationID.y*gl_NumWorkGroups.x*8u;
 if(i>=uCount)return;
 VolcanicSite s=volcanicSiteInCell(ivec2(queries[i].xy));
 result[4u*i]=vec4(s.center,s.radius,s.height);
 result[4u*i+1u]=vec4(float(s.key&65535u),float(s.key>>16u),0.,0.);
 result[4u*i+2u]=vec4(0.);
 result[4u*i+3u]=vec4(0.);
}
'''

PROFILE=r'''
void main() {
 uint i=gl_GlobalInvocationID.x+gl_GlobalInvocationID.y*gl_NumWorkGroups.x*8u;
 if(i>=uCount)return;
 VolcanicSite s=volcanicSiteInCell(ivec2(queries[i].xy));
 float angle=queries[i].z;
 vec2 p=s.center+s.radius*queries[i].w*vec2(cos(angle),sin(angle));
 result[4u*i]=vec4(volcanicConeHeight(p,s),s.height,p);
 for(uint j=1u;j<4u;++j)result[4u*i+j]=vec4(0.);
}
'''


class PointProbe:
 def __init__(self,gl,source):
  self.gl=gl;directory=SHADERS/'biomes'/'VolcanicDesert'
  relief=biome_recipe_source(directory/'relief.C.glsl')
  volume=re.sub(r'^#version[^\n]*\n','',biome_recipe_source(directory/'volume.C.glsl'),count=1)
  self.program=gl.compile([(0x91B9,relief+volume+profile_probe_source()+HEADER+source)],'VolcanicDesert probe')
  self.inputs=gl.buffer(0);self.output=gl.buffer(0);self.mapping=gl.buffer(packed_biome_cells())
 def run(self,queries,seed=1337,**options):
  q=np.asarray(queries,dtype='<f4').reshape(-1,4);gl=self.gl;n=len(q)
  uniforms=default_uniforms(64,**({'uBiomeOverride':20}|SCENE_OPTIONS|options))
  uniforms.update(uSeed=('1ui',seed),uCount=('1ui',n))
  for name,args in uniforms.items():gl.uniform(self.program,name,*args)
  gl.resize(self.inputs,q.tobytes());gl.bind(self.inputs,9)
  gl.resize(self.output,bytes(n*64));gl.bind(self.output,6);gl.bind(self.mapping,7)
  gl.dispatch_extent(self.program,8,math.ceil(n/8))
  result=np.frombuffer(gl.read(self.output,n*64),dtype='<f4').copy().reshape(n,16)
  assert np.isfinite(result).all(),'Nonfinite VolcanicDesert result'
  gl.check('VolcanicDesert probe');return result
 def chunks(self,queries,**options):
  return np.concatenate([self.run(queries[i:i+32768],**options) for i in range(0,len(queries),32768)])


def xz_queries(xz,y=0.):
 q=np.zeros((len(xz),4));q[:,[0,2]]=xz;q[:,1]=y;return q


def get_sites(gl,seed):
 cells=np.asarray([(x,z,0.,0.) for z in range(-8,9) for x in range(-8,9)])
 probe=PointProbe(gl,SITES);r=probe.run(cells,seed=seed)
 assert np.array_equal(r,probe.run(cells,seed=seed)),'Nondeterministic placement'
 delta=r[:,:2]-cells[:,:2]*10000.
 assert np.all((delta>=2500.-.03)&(delta<=7500.+.03)),'Centers escaped cell central half'
 nearest=cKDTree(r[:,:2]).query(r[:,:2],k=2)[0][:,1]
 assert nearest.min()>=5000.-.03,'Center spacing contradicts central-half jitter'
 assert np.std(delta[:,0])>1000. and np.std(delta[:,1])>1000.,'Placement lacks random jitter'
 assert np.ptp(r[:,2])>500. and np.ptp(r[:,3])>500.,'Volcano dimensions do not vary'
 return cells,r,dict(seed=seed,candidates=len(cells),cell_size_m=10000.,minimum_center_spacing_m=float(nearest.min()),
  radius_min_m=float(r[:,2].min()),radius_max_m=float(r[:,2].max()),height_min_m=float(r[:,3].min()),height_max_m=float(r[:,3].max()))


def morphology_checks(gl,cells):
 angles=np.linspace(0.,2*np.pi,64,endpoint=False);radial=np.linspace(0.,1.6,161)
 selected=cells[np.linspace(0,len(cells)-1,16).astype(int)]
 queries=np.asarray([(c[0],c[1],a,r) for c in selected for a in angles for r in radial])
 data=PointProbe(gl,PROFILE).chunks(queries).reshape(len(selected),len(angles),len(radial),16)
 height=data[:,:,:,0];nominal=data[:,:,0,1];crest=height.max(axis=2);center=height[:,:,0]
 rim_index=height.argmax(axis=2);rim_radius=radial[rim_index]
 assert np.all(crest-center>nominal*.15),'Some crater directions have no substantial depression'
 assert np.all((rim_radius>.08)&(rim_radius<.65)),'Crest is not an annular volcanic rim'
 assert np.all(height[:,:,-1]<.001),'Cone does not taper to zero outside its footprint'
 assert np.all(height>=-.01),'Volcano adds a negative broad pedestal'
 irregular=np.std(crest,axis=1)/np.mean(crest,axis=1)
 assert np.median(irregular)>.01,'Rims are perfectly circular and level'
 return dict(sites=len(selected),azimuths=len(angles),radial_samples=len(radial),
  minimum_crater_depth_m=float((crest-center).min()),maximum_crater_depth_m=float((crest-center).max()),
  rim_radius_fraction_min=float(rim_radius.min()),rim_radius_fraction_max=float(rim_radius.max()),
  relative_rim_height_std_median=float(np.median(irregular)))


def ground_checks(gl):
 u=qmc.Sobol(2,scramble=True,seed=3829).random_base2(16)
 xz=(u-.5)*64000.;q=xz_queries(xz);r=PointProbe(gl,GROUND).chunks(q)
 width=float(r[0,9]);depth=float(r[0,10])
 deep=r[:,1]<.075*width;plate=r[:,1]>1.125*width
 assert deep.sum()>200 and plate.sum()>200,'Cracks or plates are absent'
 contrast=float(np.median(r[plate,0])-np.median(r[deep,0]))
 assert contrast>depth*.69,('Fissures too shallow to read as broken basalt',contrast)
 rotated=np.column_stack((.8*xz[:,0]-.6*xz[:,1],.6*xz[:,0]+.8*xz[:,1]))
 warp=np.linalg.norm(r[:,4:6]-rotated,axis=1)
 assert np.median(warp)>20.,'Cell coordinates have no broad distortion'
 return dict(samples=len(r),height_min_m=float(r[:,0].min()),height_max_m=float(r[:,0].max()),
  deep_crack_samples=int(deep.sum()),plate_samples=int(plate.sum()),
  median_plate_to_crack_difference_m=contrast,median_warp_m=float(np.median(warp)),
  nominal_plate_scale_m=float(r[0,8]),approximate_crack_width_m=float(r[0,9]),crack_depth_setting_m=float(r[0,10]))


def seam_checks(gl):
 queries=[]
 for axis in (0,1):
  for cell in (-5,-2,-1,0,1,3,7):
   for other in np.linspace(-20000.,20000.,41):
    for side in (-.015625,.015625):
     p=[cell*10000.+side,other]
     if axis:p.reverse()
     queries.append((p[0],0.,p[1],0.))
 r=PointProbe(gl,HEIGHT).chunks(queries)[:,0]*.35
 jump=abs(r[0::2]-r[1::2]);assert jump.max()<2.,('Discontinuity at placement cell boundary',jump.max())
 return dict(pairs=len(jump),separation_m=.03125,maximum_height_change_m=float(jump.max()))


def integration_checks(gl,sites):
 u=qmc.Sobol(2,scramble=True,seed=4791).random_base2(14)
 q=xz_queries((u-.5)*128000.);q[:,1]=np.resize(np.array([-25.,-1.,1.,25.]),len(q))
 probe=PointProbe(gl,HEIGHT);records=[]
 for scale in (.2,.35,1.):
  r=probe.chunks(q,uDetailScale=scale)
  assert np.all(abs(r[:,0])<10240.),'New recipe exceeds raw height clamp'
  assert np.all(r[:,1]==0.) and np.all(r[:,8]==0.),'Old 3D sculpture or band remains'
  assert np.allclose(r[:,9],-q[:,1],atol=.002),'Height-only field has incorrect solid/air signs'
  records.append(dict(detail_scale=scale,samples=len(r),world_height_min_m=float(r[:,4].min()),world_height_max_m=float(r[:,4].max()),height_only_density=True))
 site=sites[np.argmin(np.sum(sites[:,:2]**2,axis=1))]
 point=site[:2]+np.array([site[2]*.28,0.]);height=probe.run(xz_queries(point[None,:]))[0,4]
 density=DensityProbe(gl);sample=density.sample(20,(float(point[0]-8.),float(point[1]-8.)),2.,float(height-8.),2.,size=9,depth=9,seed=1337,**SCENE_OPTIONS)
 return records,check_targeted_pipeline(FieldRunner(gl),density,sample,20,1337)


def height_grid(gl,center,extent,size,ground=False):
 axis=np.linspace(-extent*.5,extent*.5,size);xx,zz=np.meshgrid(axis,axis)
 q=xz_queries(np.column_stack((xx.ravel()+center[0],zz.ravel()+center[1])))
 r=PointProbe(gl,GROUND if ground else HEIGHT).chunks(q)
 return axis,r[:,0 if ground else 4].reshape(size,size),r


def render(gl,path,sites):
 import matplotlib
 matplotlib.use('Agg')
 import matplotlib.pyplot as plt
 from matplotlib.colors import LightSource
 from render_biome_landforms import draw_projected_mesh
 center=sites[np.argmin(np.sum(sites[:,:2]**2,axis=1)),:2]
 axis,h,_=height_grid(gl,center,24000.,321)
 close_axis,close,_=height_grid(gl,center,7000.,241)
 plain=np.array([0.,0.])
 ground_extent=2.*PointProbe(gl,GROUND).run(xz_queries(plain[None,:]))[0,8]
 ground_axis,ground,_=height_grid(gl,plain,ground_extent,401,ground=True)
 ground_step=ground_extent/400.
 fig=plt.figure(figsize=(14,10),constrained_layout=True);grid=fig.add_gridspec(2,2)
 ax=fig.add_subplot(grid[0,0]);shade=LightSource(315,40).hillshade(h,dx=75.,dy=75.,vert_exag=1.)
 ax.imshow(shade,extent=[-12,12,-12,12],origin='lower',cmap='gray',vmin=0,vmax=1)
 nearby=(abs(sites[:,0]-center[0])<12000)&(abs(sites[:,1]-center[1])<12000)
 ax.scatter((sites[nearby,0]-center[0])/1000.,(sites[nearby,1]-center[1])/1000.,s=15,facecolors='none',edgecolors='#b46e1c')
 ax.set_xlabel('X relatif (km)');ax.set_ylabel('Z relatif (km)');ax.set_title('Placement réel : paysage de 24 × 24 km')
 ax=fig.add_subplot(grid[0,1]);xx,zz=np.meshgrid(close_axis+center[0],close_axis+center[1]);v=np.stack((xx,close,zz),axis=-1)
 tris=np.concatenate((np.stack((v[:-1,:-1],v[:-1,1:],v[1:,1:]),axis=2).reshape(-1,3,3),np.stack((v[:-1,:-1],v[1:,1:],v[1:,:-1]),axis=2).reshape(-1,3,3)))
 columns=np.zeros((*close.shape,4));columns[:,:,0]=close
 sample=dict(origin=(center-3500.).tolist(),spacing=7000./240.,min_y=0.,step_y=1.,density=np.zeros((241,1,241)),columns=columns)
 draw_projected_mesh(ax,sample,tris);ax.set_title('Volcan + cratère : XYZ à la même échelle')
 ax=fig.add_subplot(grid[1,0]);shade=LightSource(315,40).hillshade(ground,dx=ground_step,dy=ground_step,vert_exag=1.)
 ax.imshow(shade,extent=[-ground_extent/2,ground_extent/2,-ground_extent/2,ground_extent/2],origin='lower',cmap='gray',vmin=0,vmax=1)
 ax.set_xlabel('X (m)');ax.set_ylabel('Z (m)');ax.set_title('Sol isolé : grandes plaques et fissures franches')
 ax=fig.add_subplot(grid[1,1]);ax.plot(close_axis/1000.,close[len(close)//2],label='Hauteur GLSL à Z = centre')
 ax.set_xlabel('Distance au centre (km)');ax.set_ylabel('Altitude monde (m)');ax.set_title('Coupe du cratère et raccord des flancs');ax.grid(alpha=.25)
 fig.suptitle('Désert volcanique — volcans hachés et plaques fracturées\nReconstruction du GLSL réel ; pas une capture du jeu.',fontsize=14)
 fig.savefig(path,dpi=120);plt.close(fig)
 return dict(seed=1337,center_xz_m=center.tolist(),landscape_extent_m=24000.,landscape_step_m=75.,
  volcano_extent_m=7000.,volcano_step_m=7000./240.,ground_extent_m=float(ground_extent),ground_step_m=float(ground_step))


def main():
 parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--output',type=Path,default=Path('docs/biome-validation/volcanic_desert.json'));parser.add_argument('--no-render',action='store_true');args=parser.parse_args()
 fingerprint=source_fingerprint(20)
 with GL() as gl:
  report=dict(renderer=gl.renderer,source_sha256=fingerprint)
  print('Checking hashed volcano placement...',flush=True);cells,sites,a=get_sites(gl,1337);_,_,b=get_sites(gl,97);report['placement']=[a,b]
  print('Checking open crater morphology and fractured ground...',flush=True);report['morphology']=morphology_checks(gl,cells);report['ground']=ground_checks(gl)
  report['cell_seams']=seam_checks(gl)
  print('Checking height field, production and shared LOD samples...',flush=True);report['integration'],report['targeted_pipeline']=integration_checks(gl,sites)
  args.output.parent.mkdir(parents=True,exist_ok=True)
  if not args.no_render:print('Rendering actual volcano and ground heights...',flush=True);report['figure']=render(gl,args.output.with_suffix('.png'),sites)
  assert source_fingerprint(20)==fingerprint,'Sources changed during validation'
 args.output.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
if __name__=='__main__':main()
