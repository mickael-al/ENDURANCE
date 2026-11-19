#!/usr/bin/env python3
"""Probe ColdMarsh's unchanged height and large porous spherical formations.

Tests run the production GLSL. Independent oracles cover the original height
formula, mathematical sphere bounds, burial, and additive field invariants.
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
float previousColdMarshHeight(vec2 p) {
 vec2 q=landWarp(p,3200.0,180.0,901u);
 float broad=.5+.5*landFbm(q*.00062,902u);
 float banks=smoothstep(.42,.78,broad);
 float h=-28.0+landTerrace(banks*68.0,17.0,.5);
 h+=landFbm(q*.00020,903u)*17.0;
 h+=noiseSimplex2D(q*.006,uSeed^904u)*.65*(.2+.8*banks);
 LandSite s=landSite(p,2300.0,905u,.58);
 vec2 t=s.q/s.radius;
 float radius=length(t);
 float shore=exp(-35.0*(radius-.52)*(radius-.52));
 float openings=smoothstep(.10,.34,abs(t.y));
 h+=s.mask*shore*openings*(54.0+45.0*s.variant);
 return h;
}
void main() {
 uint i=gl_GlobalInvocationID.x+gl_GlobalInvocationID.y*gl_NumWorkGroups.x*8u;
 if(i>=uCount)return;
 vec2 p=queries[i].xz;
 vec4 r=fieldRelief(p);
 result[4u*i]=vec4(r.x,previousColdMarshHeight(p),r.y,r.w);
 for(uint j=1u;j<4u;++j)result[4u*i+j]=vec4(0.);
}
'''

SITES=r'''
void main() {
 uint i=gl_GlobalInvocationID.x+gl_GlobalInvocationID.y*gl_NumWorkGroups.x*8u;
 if(i>=uCount)return;
 ivec2 cell=ivec2(queries[i].xy);
 ColdMarshSphereSite s=coldMarshSphereInCell(cell);
 result[4u*i]=vec4(s.center,coldMarshSphereStoredAnchor(s),coldMarshSphereAnchorHeight(s));
 result[4u*i+1u]=vec4(float(s.key&65535u),float(s.key>>16u),cell);
 result[4u*i+2u]=vec4(COLD_MARSH_SPHERE_RADIUS,COLD_MARSH_SPHERE_SPACING,
  COLD_MARSH_SPHERE_BURIED_FRACTION,COLD_MARSH_VORONOI_SCALE);
 result[4u*i+3u]=vec4(0.);
}
'''

GEOMETRY=r'''
void main() {
 uint i=gl_GlobalInvocationID.x+gl_GlobalInvocationID.y*gl_NumWorkGroups.x*8u;
 if(i>=uCount)return;
 vec3 p=queries[i].xyz;
 ColdMarshSphereSite s=coldMarshSphereSite(p.xz);
 vec3 q=coldMarshSphereLocal(p,s,0.);
 float porous=coldMarshPorousField(q,s.key);
 result[4u*i]=vec4(coldMarshSphereGeometry(p,s,0.),coldMarshSphereEnvelope(q),porous,q.y);
 result[4u*i+1u]=vec4(q,0.);
 result[4u*i+2u]=vec4(s.center,float(s.key&65535u),float(s.key>>16u));
 result[4u*i+3u]=vec4(0.);
}
'''

DETAIL=r'''
void main() {
 uint i=gl_GlobalInvocationID.x+gl_GlobalInvocationID.y*gl_NumWorkGroups.x*8u;
 if(i>=uCount)return;
 vec3 p=queries[i].xyz;
 ColdMarshSphereSite s=coldMarshSphereSite(p.xz);
 vec3 q=coldMarshSphereLocal(p,s,0.);
 vec3 phase=coldMarshNoisePhase(s.key);
 vec3 domain=coldMarshVoronoiCoordinates(q,s.key,phase);
 float coarse=(noiseVoronoi3D01(domain,s.key^0x39d7af25u)-COLD_MARSH_PORE_THRESHOLD)*COLD_MARSH_VORONOI_SCALE;
 result[4u*i]=vec4(coldMarshPorousField(q,s.key),coarse,
  length(domain-(q/COLD_MARSH_VORONOI_SCALE+phase)),COLD_MARSH_DETAIL_AMPLITUDE);
 for(uint j=1u;j<4u;++j)result[4u*i+j]=vec4(0.);
}
'''

WORLD=r'''
void main() {
 uint i=gl_GlobalInvocationID.x+gl_GlobalInvocationID.y*gl_NumWorkGroups.x*8u;
 if(i>=uCount)return;
 vec3 p=queries[i].xyz;
 ColdMarshSphereSite s=coldMarshSphereSite(p.xz);
 float anchor=coldMarshSphereStoredAnchor(s);
 p.y+=anchor;
 FieldColumn c=fieldProbeColumn(p.xz);
 float base=c.terrain.x-p.y;
 float geometry=coldMarshSphereGeometry(p,s,anchor);
 result[4u*i]=vec4(fieldProbeDensity(p,c),base,fieldVolume(p,c),geometry);
 result[4u*i+1u]=vec4(c.terrain.x,anchor,p.y,c.terrain.w);
 result[4u*i+2u]=vec4(fieldVolumeEnvelope(c),c.blend.w,coldMarshSphereEnvelope(coldMarshSphereLocal(p,s,anchor)));
 result[4u*i+3u]=vec4(coldMarshSphereLocal(p,s,anchor),0.);
}
'''

MIXED=r'''
void main() {
 uint i=gl_GlobalInvocationID.x+gl_GlobalInvocationID.y*gl_NumWorkGroups.x*8u;
 if(i>=uCount)return;
 vec3 p=queries[i].xyz;
 ColdMarshSphereSite s=coldMarshSphereSite(p.xz);
 p.y+=coldMarshSphereStoredAnchor(s);
 FieldColumn c=fieldProbeColumn(p.xz);
 const uint cell=23u;
 uvec4 mapping=fieldBiomeCells[cell];
 c.terrain.yz=vec2((3.+queries[i].w)*.25,.25);
 c.blend.x=.5;c.detail=uvec4(mapping.x|(cell<<20u),mapping.yz,0u);
 float sw=fieldSurfaceWeight(c,9u),vw=fieldVolumeWeight(c,9u);
 c.terrain.w*=sw;
 c=fieldUnpackColumn(fieldPackColumn(c,c.terrain.w/uDetailScale));
 c.blend.w=0.;float a=fieldVolume(p,c);
 c.blend.w=.93;float b=fieldVolume(p,c);
 float base=c.terrain.x-p.y;
 vec2 envelope=fieldVolumeEnvelope(c);
 float wanted=base+vw*b;
 float actual=base;
 if(c.terrain.w>.0002&&p.y>=envelope.x&&p.y<=envelope.y)actual+=vw*b;
 result[4u*i]=vec4(actual,wanted,a,b);
 result[4u*i+1u]=vec4(sw,vw,c.terrain.w,base);
 result[4u*i+2u]=vec4(envelope,p.y,0.);
 result[4u*i+3u]=vec4(0.);
}
'''

class PointProbe:
 def __init__(self,gl,source):
  self.gl=gl;directory=SHADERS/'biomes'/'ColdMarsh'
  relief=biome_recipe_source(directory/'relief.C.glsl')
  volume=re.sub(r'^#version[^\n]*\n','',biome_recipe_source(directory/'volume.C.glsl'),count=1)
  self.program=gl.compile([(0x91B9,relief+volume+profile_probe_source()+HEADER+source)],'ColdMarsh sphere probe')
  self.inputs=gl.buffer(0);self.output=gl.buffer(0);self.mapping=gl.buffer(packed_biome_cells())
 def run(self,queries,seed=1337,**options):
  q=np.asarray(queries,dtype='<f4').reshape(-1,4);gl=self.gl;n=len(q)
  uniforms=default_uniforms(64,**({'uBiomeOverride':9}|SCENE_OPTIONS|options))
  uniforms.update(uSeed=('1ui',seed),uCount=('1ui',n))
  for name,args in uniforms.items():gl.uniform(self.program,name,*args)
  gl.resize(self.inputs,q.tobytes());gl.bind(self.inputs,9)
  gl.resize(self.output,bytes(n*64));gl.bind(self.output,6);gl.bind(self.mapping,7)
  gl.dispatch_extent(self.program,8,math.ceil(n/8))
  result=np.frombuffer(gl.read(self.output,n*64),dtype='<f4').copy().reshape(n,16)
  assert np.isfinite(result).all(),'Nonfinite ColdMarsh result'
  gl.check('ColdMarsh sphere probe');return result
 def chunks(self,queries,**options):
  return np.concatenate([self.run(queries[i:i+32768],**options) for i in range(0,len(queries),32768)])

def query_points(center,xyz):
 p=np.zeros((len(xyz),4));p[:,:3]=xyz;p[:,0]+=center[0];p[:,2]+=center[1];return p

def height_checks(gl):
 probe=PointProbe(gl,HEIGHT);u=qmc.Sobol(2,scramble=True,seed=7791).random_base2(16)
 queries=np.zeros((len(u),4));queries[:,[0,2]]=(u-.5)*256000.
 records=[]
 for seed in (1337,97,98765):
  r=probe.run(queries,seed=seed);error=float(np.max(abs(r[:,0]-r[:,1])))
  assert error<.004,('2D height changed',seed,error)
  records.append(dict(seed=seed,samples=len(r),maximum_height_error_raw=error))
 return records

def get_sites(gl,seed):
 cells=np.asarray([(x,z,0.,0.) for z in range(-8,9) for x in range(-8,9)])
 probe=PointProbe(gl,SITES);r=probe.run(cells,seed=seed)
 assert np.array_equal(r,probe.run(cells,seed=seed)),'Nondeterministic placement'
 assert np.allclose(r[:,8:12],[1000.,10000.,.25,900.]),'Initial sphere dimensions changed'
 assert np.max(abs(r[:,2]-r[:,3]))<.08,'Anchor storage moves the reference ground too far'
 delta=r[:,:2]-cells[:,:2]*10000.
 assert np.all((delta>=2500.)&(delta<=7500.)),'Centers escaped cell central half'
 nearest=cKDTree(r[:,:2]).query(r[:,:2],k=2)[0][:,1]
 assert nearest.min()>=5000.-.03,'Adjacent sphere envelopes can overlap'
 assert np.std(delta[:,0])>1000. and np.std(delta[:,1])>1000.,'Placement has no meaningful random jitter'
 return r,dict(seed=seed,candidates=len(cells),cell_size_m=10000.,minimum_center_spacing_m=float(nearest.min()),anchor_storage_error_max_m=float(abs(r[:,2]-r[:,3]).max()))

def envelope_checks(gl,sites):
 probe=PointProbe(gl,GEOMETRY)
 u=qmc.Sobol(2,scramble=True,seed=2681).random_base2(11)
 phi=2*np.pi*u[:,0];cosine=2*u[:,1]-1;horizontal=np.sqrt(1-cosine*cosine)
 directions=np.column_stack((horizontal*np.cos(phi),cosine,horizontal*np.sin(phi)))
 records=[]
 for site in sites[:4]:
  xyz=directions*1000.;xyz[:,1]+=500.
  r=probe.run(query_points(site[:2],xyz));error=float(abs(r[:,1]).max())
  assert error<.025,('Sphere is not spherical in world metres',error)
  outside=directions*1005.;outside[:,1]+=500.
  e=probe.run(query_points(site[:2],outside))
  assert np.all(e[:,1]>0.) and np.all(e[:,0]<0.),'Rock leaks out of the exact spherical envelope'
  cardinal=np.array([[0,-500,0],[0,1500,0],[1000,500,0],[-1000,500,0],[0,500,1000],[0,500,-1000]])
  c=probe.run(query_points(site[:2],cardinal));assert abs(c[:,1]).max()<.025
  records.append(error)
 return dict(diameter_m=2000.,bottom_relative_anchor_m=-500.,top_relative_anchor_m=1500.,buried_height_fraction=.25,
  boundary_samples=4*len(directions),maximum_envelope_error_m=max(records))

def sphere_samples(power=12,seed=5917):
 u=qmc.Sobol(3,scramble=True,seed=seed).random_base2(power)
 radius=995.*np.cbrt(u[:,0]);phi=2*np.pi*u[:,1];cy=2*u[:,2]-1;xy=np.sqrt(1-cy*cy)
 return np.column_stack((radius*xy*np.cos(phi),500.+radius*cy,radius*xy*np.sin(phi)))

def porosity_checks(gl,sites):
 probe=PointProbe(gl,GEOMETRY);xyz=sphere_samples()
 selected=sites[np.linspace(0,len(sites)-1,16).astype(int)]
 r=probe.chunks(np.concatenate([query_points(s[:2],xyz) for s in selected]))
 assert (r[:,0]>1.).any() and (r[:,0]<-1.).any(),'Voronoi sphere is entirely empty or entirely filled'
 fractions=(r[:,0]>0.).reshape(len(selected),-1).mean(axis=1)
 assert .15<fractions.mean()<.85,('Unexpectedly trivial porous shape',fractions.mean())
 detail=PointProbe(gl,DETAIL).chunks(np.concatenate([query_points(s[:2],xyz) for s in selected[:4]]))
 difference=detail[:,0]-detail[:,1]
 assert np.max(abs(difference))<=22.01,'Fine noise exceeds its 22-m amplitude'
 assert np.std(difference)>1.,'Fine detail has no visible density contribution'
 assert np.max(detail[:,2])>.1,'Perlin does not warp the large Voronoi cells'
 return dict(sites=len(selected),samples=len(r),measured_stone_fraction=float(fractions.mean()),
  detail_density_amplitude_max_m=float(abs(difference).max()),detail_density_std_m=float(difference.std()),
  warped_domain_displacement_max_cells=float(detail[:,2].max()),
  site_stone_fraction_min=float(fractions.min()),site_stone_fraction_max=float(fractions.max()),
  note='Empirical volume proportions before union with soil; no exact percentage was requested.')

def world_checks(gl,sites,scale):
 probe=PointProbe(gl,WORLD);xyz=sphere_samples(power=11)
 selected=sites[np.linspace(0,len(sites)-1,6).astype(int)]
 queries=np.concatenate([query_points(s[:2],xyz) for s in selected]);r=probe.chunks(queries,uDetailScale=scale)
 assert np.all(r[:,2]>=-.002),'New 3D excavates unchanged ground'
 assert np.all(r[:,0]>=r[:,1]-.002),'Terrain density decreased'
 wanted=r[:,1]+r[:,2]
 assert np.allclose(r[:,0],wanted,atol=.03,rtol=2e-5),'Column band skips requested sphere density'
 stone=(r[:,0]>1.)&(r[:,1]<-1.);pores=(r[:,0]<-1.)&(r[:,1]<-1.)&(r[:,11]<-10.)
 assert stone.any(),'No visible added sphere above the terrain'
 assert pores.any(),'No visible internal Voronoi cavity above the terrain'
 # Reference density uses unquantized rigid anchor; compare expected zero signs
 # away from its sub-metre cached anchor quantization uncertainty.
 expected=(r[:,3]>1.)&(r[:,1]<-1.)
 assert np.all(r[expected,0]>0.),('Sphere clipped or anchor misdecoded',scale,int(np.sum(r[expected,0]<=0.)))
 inside=(r[:,6]>=r[:,8])&(r[:,6]<=r[:,9]);assert np.all(inside[stone])
 outside=xyz.copy();outside[:,0]*=1.4;outside[:,2]*=1.4
 exterior=probe.chunks(np.concatenate([query_points(s[:2],outside) for s in selected]),uDetailScale=scale)
 outside_sphere=exterior[:,11]>1.
 assert np.all(exterior[outside_sphere,0]<=np.maximum(exterior[outside_sphere,1],0.)+.02),'Added rock escapes the sphere'
 below=np.concatenate([query_points(s[:2],np.array([[0,-700,0],[0,-600,0],[0,1700,0]])) for s in selected])
 e=probe.run(below,uDetailScale=scale);assert np.allclose(e[:,0],e[:,1],atol=.02),'Volume leaks above/below the sphere'
 disabled=probe.run(queries[:256],uDetailScale=scale,uFieldMode=1);assert np.all(disabled[:,2]==0.)
 idx=np.flatnonzero(stone)[len(np.flatnonzero(stone))//2];position=queries[idx,:3].copy();position[1]=r[idx,6]
 return dict(detail_scale=scale,samples=len(r),above_ground_stone_samples=int(stone.sum()),above_ground_pore_samples=int(pores.sum()),
  band_density_error_max=float(abs(r[:,0]-wanted).max()),world_band_max_m=float(r[:,7].max()),
  original_ground_preserved=True),position

def seam_checks(gl):
 queries=[]
 for axis in (0,1):
  for cell in (-5,-2,-1,0,1,3,7):
   for other in (-15731.,4173.,19751.):
    for side in (-.02,.02):
     p=[cell*10000.+side,other]
     if axis:p.reverse()
     queries.append((p[0],500.,p[1],0.))
 r=PointProbe(gl,WORLD).run(queries)
 assert np.all(r[:,2]==0.),'Sphere support reaches a cell edge'
 return dict(samples=len(queries),maximum_delta=float(r[:,2].max()))

def mixed_checks(gl,sites):
 xyz=sphere_samples(power=10);selected=sites[:6]
 queries=np.concatenate([query_points(s[:2],xyz) for s in selected])
 records=[];probe=PointProbe(gl,MIXED)
 for scale in (.2,.35,1.):
  for ratio in (.35,.5,.65):
   q=queries.copy();q[:,3]=ratio;r=probe.chunks(q,uBiomeOverride=-1,uDetailScale=scale)
   assert np.all((r[:,4]>0.)&(r[:,4]<1.)), 'Mixed branch not exercised'
   assert np.array_equal(r[:,2],r[:,3]),'Neighbour pilot changes this sphere'
   assert np.all(r[:,3]>=-.002),'Mixed sphere excavates original ground'
   assert np.allclose(r[:,0],r[:,1],atol=.03,rtol=2e-5),'Mixed band skips requested sphere density'
   assert (r[:,0]>0.).any(),'No surviving sphere density in blend'
   records.append(dict(detail_scale=scale,weight=float(r[0,4]),samples=len(q),pilot_independent=True,
    maximum_band_density_error=float(abs(r[:,0]-r[:,1]).max())))
 return records

def render(gl,path,sites,report):
 import matplotlib
 matplotlib.use('Agg')
 import matplotlib.pyplot as plt
 from render_biome_landforms import isosurface,draw_projected_mesh
 site=sites[np.argmin(np.sum(sites[:,:2]**2,axis=1))];center=site[:2];anchor=site[2]
 spacing=20.;size=121;extent=spacing*(size-1)
 origin=(float(center[0]-extent/2),float(center[1]-extent/2))
 sample=DensityProbe(gl).sample(9,origin,spacing,float(anchor-650.),25.,size=size,depth=93,seed=1337,**SCENE_OPTIONS)
 isolated=PointProbe(gl,GEOMETRY)
 axis=(np.arange(size)-(size-1)*.5)*spacing;y=np.arange(-650.,1650.1,25.)
 zz,yy,xx=np.meshgrid(axis,y,axis,indexing='ij')
 q=np.column_stack((xx.ravel()+center[0],yy.ravel(),zz.ravel()+center[1],np.zeros(xx.size)))
 g=isolated.chunks(q)[:,0]
 isolate_sample=dict(density=g.reshape(size,len(y),size),columns=np.zeros((size,size,4)),origin=origin,spacing=spacing,min_y=-650.,step_y=25.)
 fig=plt.figure(figsize=(14,10),constrained_layout=True);grid=fig.add_gridspec(2,2)
 ax=fig.add_subplot(grid[0,0]);draw_projected_mesh(ax,sample,isosurface(sample));ax.set_title('Terrain + sphère : densité GLSL réelle')
 ax=fig.add_subplot(grid[0,1]);draw_projected_mesh(ax,isolate_sample,isosurface(isolate_sample));ax.set_title('Sphère poreuse isolée, avant union au sol')
 xx,yy=np.meshgrid(axis,y);q=np.column_stack((xx.ravel()+center[0],yy.ravel(),np.full(xx.size,center[1]),np.zeros(xx.size)))
 section=PointProbe(gl,WORLD).chunks(q)[:,0].reshape(len(y),len(axis))
 ax=fig.add_subplot(grid[1,0]);ax.contourf(axis,y,section,levels=[float(section.min())-1.,0.,float(section.max())+1.],colors=['#f3f7f8','#526874'])
 theta=np.linspace(0.,2*np.pi,361);ax.plot(1000.*np.cos(theta),500.+1000.*np.sin(theta),'--',color='#b5843b',lw=1.)
 ax.axhline(0.,color='#b5843b',lw=.8);ax.set_aspect('equal');ax.set_xlabel('Distance au centre (m)');ax.set_ylabel('Y relatif au sol de l’ancrage (m)');ax.set_title('Coupe : blanc = vide ; contour sphérique de référence')
 ax=fig.add_subplot(grid[1,1]);ax.scatter(sites[:,0]/1000.,sites[:,1]/1000.,s=8);ax.scatter(center[0]/1000.,center[1]/1000.,s=50,facecolors='none',edgecolors='#b5843b');ax.set_aspect('equal');ax.set_xlabel('km');ax.set_ylabel('km');ax.set_title('Centres aléatoires dans les cellules de 10 km')
 fig.suptitle('Marécage froid — sphères de 2 km, Voronoï déformé et détail fin\nReconstruction du GLSL, pas une capture du jeu.',fontsize=14)
 fig.savefig(path,dpi=120);plt.close(fig)
 return dict(seed=1337,center_xz_m=center.tolist(),anchor_height_m=float(anchor),spacing_m=spacing,horizontal_samples=size,vertical_samples=93)

def main():
 parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--output',type=Path,default=Path('docs/biome-validation/cold_marsh.json'));parser.add_argument('--no-render',action='store_true');parser.add_argument('--render-only',action='store_true');args=parser.parse_args()
 fingerprint=source_fingerprint(9)
 with GL() as gl:
  if args.render_only:
   report=json.loads(args.output.read_text());assert report['source_sha256']==fingerprint
   sites,_=get_sites(gl,1337);report['figure']=render(gl,args.output.with_suffix('.png'),sites,report)
  else:
   report=dict(renderer=gl.renderer,source_sha256=fingerprint)
   print('Checking unchanged ColdMarsh height...',flush=True);report['height']=height_checks(gl)
   print('Checking placement and spherical envelope...',flush=True);sites,a=get_sites(gl,1337);_,b=get_sites(gl,97);report['placement']=[a,b]
   report['envelope']=envelope_checks(gl,sites);report['porosity']=porosity_checks(gl,sites);report['cell_seams']=seam_checks(gl)
   report['world']=[]
   for scale in (.2,.35,1.):
    print(f'Checking sphere integration at scale {scale}...',flush=True);metrics,position=world_checks(gl,sites,scale);report['world'].append(metrics)
    if scale==.35:
     density=DensityProbe(gl);sample=density.sample(9,(float(position[0]-8),float(position[2]-8)),2.,float(position[1]-8),2.,size=9,depth=9,seed=1337,**SCENE_OPTIONS)
     report['targeted_pipeline']=check_targeted_pipeline(FieldRunner(gl),density,sample,9,1337)
   report['mixed']=mixed_checks(gl,sites)
   args.output.parent.mkdir(parents=True,exist_ok=True)
   if not args.no_render:print('Rendering actual ColdMarsh density...',flush=True);report['figure']=render(gl,args.output.with_suffix('.png'),sites,report)
  assert source_fingerprint(9)==fingerprint,'Sources changed during validation'
 args.output.parent.mkdir(parents=True,exist_ok=True);args.output.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
if __name__=='__main__':main()
