#!/usr/bin/env python3
"""Check the actual BorealForest GLSL layers and Voronoi excavation.

The ground oracle is the pre-revision recipe with only its localized dome block
removed. All generated layers and cavities are sampled from production GLSL.
"""
from pathlib import Path
import argparse
import json
import math
import re

import numpy as np
from scipy.stats import qmc

from biome_test_support import biome_recipe_source, packed_biome_cells
from check_biomes import profile_probe_source
from check_biome_landforms import DensityProbe, SCENE_OPTIONS, check_targeted_pipeline, source_fingerprint
from check_gpu import SHADERS, FieldRunner
from field_batch_runner import default_uniforms
from gl_headless import GL

HEADER = r'''
layout(local_size_x=8,local_size_y=8) in;
layout(std430,binding=9) readonly buffer ProbeInput { vec4 queries[]; };
layout(std430,binding=6) writeonly buffer ProbeOutput { vec4 result[]; };
uniform uint uCount;
'''

# Independent preservation oracle from commit de814026, before the rewrite.
OLD_GROUND = r'''
float oldGroundWithoutDome(vec2 p) {
 vec2 q=landWarp(p,2800.0,210.0,701u);
 float rounded=.5+.5*landFbm(q*vec2(.00065,.00039),702u);
 float regional=.5+.5*noiseSimplex2D(q*.00019,uSeed^703u);
 float h=rounded*rounded*(210.0+110.0*regional)-72.0;
 h+=landFbm(q*vec2(.0015,.00086),704u)*24.0;
 h+=noiseSimplex2D(q*.008,uSeed^706u)*1.4;
 return h;
}
'''

HEIGHT = OLD_GROUND+r'''
void main() {
 uint i=gl_GlobalInvocationID.x+gl_GlobalInvocationID.y*gl_NumWorkGroups.x*8u;
 if(i>=uCount)return;
 vec2 p=queries[i].xz;
 vec4 relief=fieldRelief(p);
 result[4u*i]=vec4(borealGroundHeight(p),oldGroundWithoutDome(p),
                   borealLayerCount(p),borealMountainHeight(p));
 result[4u*i+1u]=borealLayerNoise(p);
 result[4u*i+2u]=relief;
 FieldColumn c=fieldProbeColumn(p);
 result[4u*i+3u]=vec4(c.terrain.x,c.terrain.w,c.blend.w,round(c.blend.w*4.));
}
'''

WORLD = r'''
void main() {
 uint i=gl_GlobalInvocationID.x+gl_GlobalInvocationID.y*gl_NumWorkGroups.x*8u;
 if(i>=uCount)return;
 vec2 xz=queries[i].xz;
 FieldColumn c=fieldProbeColumn(xz);
 float mountain=borealMountainHeight(xz)*uDetailScale;
 float ground=c.terrain.x-mountain;
 // Y input is a fraction of the newly added mountain slab. Negative fractions
 // test the pre-existing substrate; values greater than1 test exterior air.
 vec3 p=vec3(xz.x,ground+queries[i].y*max(mountain,1.),xz.y);
 float base=c.terrain.x-p.y;
 result[4u*i]=vec4(fieldProbeDensity(p,c),base,borealCavityField(p,uSeed),mountain);
 result[4u*i+1u]=vec4(ground,c.terrain.x,p.y,fieldVolume(p,c));
 result[4u*i+2u]=vec4(fieldVolumeEnvelope(c),c.terrain.w,c.blend.w);
 result[4u*i+3u]=vec4(borealCavityNoise(p,uSeed),0.,0.,0.);
}
'''

MIXED = r'''
void main() {
 uint i=gl_GlobalInvocationID.x+gl_GlobalInvocationID.y*gl_NumWorkGroups.x*8u;
 if(i>=uCount)return;
 vec2 xz=queries[i].xz;
 FieldColumn c=fieldProbeColumn(xz);
 const uint cell=21u; // ColdSteppe6 -> BorealForest7 above the marine slice.
 uvec4 mapping=fieldBiomeCells[cell];
 c.terrain.yz=vec2((1.+queries[i].w)*.25,.25);
 c.blend.x=.5;c.detail=uvec4(mapping.x|(cell<<20u),mapping.yz,0u);
 float weight=fieldSurfaceWeight(c,7u);
 float mountain=borealMountainHeight(xz)*uDetailScale;
 c.terrain.x-=(1.-weight)*mountain;
 c.terrain.w*=weight;
 c=fieldUnpackColumn(fieldPackColumn(c,c.terrain.w/uDetailScale));
 float ground=c.terrain.x-weight*mountain;
 vec3 p=vec3(xz.x,ground+queries[i].y*max(weight*mountain,1.),xz.y);
 c.blend.w=0.;float a=fieldVolume(p,c);
 c.blend.w=.93;float b=fieldVolume(p,c);
 vec2 envelope=fieldVolumeEnvelope(c);
 float base=c.terrain.x-p.y;
 float wanted=base+fieldVolumeWeight(c,7u)*b;
 float actual=base;
 if(c.terrain.w>.0002&&p.y>=envelope.x&&p.y<=envelope.y)
  actual+=fieldVolumeWeight(c,7u)*b;
 result[4u*i]=vec4(a,b,wanted,actual);
 result[4u*i+1u]=vec4(weight,mountain,ground,p.y);
 result[4u*i+2u]=vec4(envelope,c.terrain.w,base);
 result[4u*i+3u]=vec4(0.);
}
'''


class PointProbe:
    def __init__(self, gl, source):
        self.gl=gl
        directory=SHADERS/'biomes'/'BorealForest'
        relief=biome_recipe_source(directory/'relief.C.glsl')
        volume=re.sub(r'^#version[^\n]*\n','',biome_recipe_source(directory/'volume.C.glsl'),count=1)
        self.program=gl.compile([(0x91B9,relief+volume+profile_probe_source()+HEADER+source)],'BorealForest measurement')
        self.inputs=gl.buffer(0);self.output=gl.buffer(0)
        self.mapping=gl.buffer(packed_biome_cells())

    def run(self,queries,seed=1337,**options):
        queries=np.asarray(queries,dtype='<f4').reshape(-1,4)
        count=len(queries);gl=self.gl
        uniforms=default_uniforms(64,**({'uBiomeOverride':7}|SCENE_OPTIONS|options))
        uniforms.update(uSeed=('1ui',seed),uCount=('1ui',count))
        for name,args in uniforms.items():gl.uniform(self.program,name,*args)
        gl.resize(self.inputs,queries.tobytes());gl.bind(self.inputs,9)
        gl.resize(self.output,bytes(count*64));gl.bind(self.output,6)
        gl.bind(self.mapping,7)
        gl.dispatch_extent(self.program,8,math.ceil(count/8))
        result=np.frombuffer(gl.read(self.output,count*64),dtype='<f4').copy().reshape(count,16)
        assert np.isfinite(result).all(),'Nonfinite BorealForest result'
        gl.check('BorealForest probe')
        return result


def height_checks(gl):
    probe=PointProbe(gl,HEIGHT)
    records=[];active=[]
    unit=qmc.Sobol(2,scramble=True,seed=517).random_base2(18)
    queries=np.zeros((len(unit),4));queries[:,[0,2]]=(unit-.5)*1024000.
    for seed in (1337,97,98765):
        r=probe.run(queries,seed=seed)
        assert np.array_equal(r[:,0].view('u4'),r[:,1].view('u4')),'Original ground noise changed'
        count=(r[:,4:8]>.7).sum(axis=1)
        assert np.array_equal(count,r[:,2]),'Layers are not the sum of four strict binary thresholds'
        assert np.allclose(r[:,3],count*(600./.35),rtol=2e-7,atol=.002),'Layer height/count mismatch'
        assert np.allclose(r[:,8],r[:,0]+r[:,3],rtol=2e-7,atol=.002),'Unexpected mound still changes h'
        assert np.all(r[count==0,9]==0.),'Volume band opens without mountains'
        assert np.array_equal(r[:,11],count/4.),'Pilot is not exclusively normalized mountain height'
        assert np.array_equal(r[:,15],count),'Packed pilot loses discrete layer count'
        records.append(dict(seed=seed,samples=len(r),ground_bit_identical=True,
            layer_count_histogram=np.bincount(count,minlength=5).tolist(),
            mountain_area_fraction=float((count>0).mean()),
            raw_noise_min=float(r[:,4:8].min()),raw_noise_max=float(r[:,4:8].max())))
        active.append((seed,queries[count>0].copy(),count[count>0].copy()))
    repeated=probe.run(queries[:8192],seed=1337)
    twice=probe.run(queries[:8192],seed=1337)
    assert np.array_equal(repeated,twice),'Seeded fields not deterministic'
    assert all(len(points)>64 for _,points,_ in active),'Insufficient active mountain samples'
    return records,active


def world_checks(gl,active,scale):
    probe=PointProbe(gl,WORLD)
    records=[];target=None
    rng=np.random.default_rng(623)
    for seed,points,counts in active:
        # Sampling columns in proportion to their added height makes each
        # sample representative of uniform mountain volume, not just area.
        indices=rng.choice(len(points),size=32768,p=counts/counts.sum())
        q=points[indices].copy();q[:,1]=rng.uniform(.001,.999,len(q))
        r=probe.run(q,seed=seed,uDetailScale=scale)
        inside=(r[:,6]>=r[:,8])&(r[:,6]<=r[:,9])
        assert inside.all(),'A mountain slab escapes the production volume envelope'
        assert np.all(r[:,0]<=r[:,1]+.002),'Excavation adds rock above the requested layer heights'
        carved=r[:,0]<0.
        assert .55<float(carved.mean())<.65,('Mountain cavity fraction differs from60%',seed,scale,float(carved.mean()))
        changed=np.flatnonzero(carved&(r[:,1]>2.))
        assert len(changed)>0,'No actual excavated air below the mountain surface'
        if target is None:
            j=changed[len(changed)//2]
            target=(seed,np.array((q[j,0],r[j,6],q[j,2])))
        # Only mountain material can be removed. Beneath the original ground
        # all samples must remain solid, while exterior air stays exterior.
        bounds=points[:min(len(points),4096)].copy()
        ground_min=[]
        for fraction in (-.25,-.001,1.001,1.25):
            bounds[:,1]=fraction
            b=probe.run(bounds,seed=seed,uDetailScale=scale)
            if fraction<0.:
                assert np.all(b[:,0]>0.),'Cavities damaged the old soil'
                ground_min.append(float(b[:,0].min()))
            else: assert np.all(b[:,0]<0.),'Excavation filled air above the mountain'
        disabled=probe.run(q[:256],seed=seed,uDetailScale=scale,uFieldMode=1)
        assert np.all(disabled[:,7]==0.),'Analytical mode still carves mountains'
        # Same XZ positions, several separated heights: the Voronoi must be 3D.
        columns=points[:128].copy();variations=[]
        for f in (.1,.3,.5,.7,.9):
            columns[:,1]=f
            variations.append(probe.run(columns,seed=seed,uDetailScale=scale)[:,12])
        vertical=np.ptp(np.asarray(variations),axis=0)
        assert (vertical>.005).mean()>.95,'Cavity field is extruded rather than volumetric'
        records.append(dict(seed=seed,detail_scale=scale,sampled_volume_points=len(q),
            void_fraction=float(carved.mean()),inside_band_samples=int(inside.sum()),
            unchanged_ground_min_density=min(ground_min),
            vertically_varying_columns_fraction=float((vertical>.005).mean())))
    return records,target


def mixed_checks(gl,active):
    probe=PointProbe(gl,MIXED);rng=np.random.default_rng(775)
    seed,points,_=active[0];selected=points[rng.choice(len(points),4096)].copy()
    records=[]
    for scale in (.2,.35,1.):
        for fraction in (.35,.5,.65):
            q=selected.copy();q[:,1]=rng.uniform(-.2,1.2,len(q));q[:,3]=fraction
            r=probe.run(q,seed=seed,uDetailScale=scale,uBiomeOverride=-1)
            assert np.array_equal(r[:,0].view('u4'),r[:,1].view('u4')),'Mixed pilot contaminates mountain height'
            ground=q[:,1]<0.
            assert np.all(r[ground,3]>0.),'Mixed cavity removes original ground'
            assert np.allclose(r[:,2],r[:,3],atol=.002,rtol=2e-5),'Mixed band clips requested density'
            records.append(dict(detail_scale=scale,biome_weight=float(r[0,4]),samples=len(q),
                pilot_independent=True,maximum_band_density_error=float(np.abs(r[:,2]-r[:,3]).max())))
    return records



def render_figure(gl,path,report):
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    from render_biome_landforms import isosurface,draw_projected_mesh
    # Select a real multi-layer location; no synthetic mountain or hand-made
    # cavity model is substituted for the production field.
    unit=qmc.Sobol(2,scramble=True,seed=515).random_base2(14)
    queries=np.zeros((len(unit),4));queries[:,[0,2]]=(unit-.5)*256000.
    height=PointProbe(gl,HEIGHT);r=height.run(queries)
    eligible=np.flatnonzero(r[:,2]>=3.)
    if not len(eligible):eligible=np.flatnonzero(r[:,2]>=2.)
    selected=eligible[np.argmin(np.sum(queries[eligible][:,[0,2]]**2,axis=1))]
    center=queries[selected,[0,2]]
    size=129;spacing=64.;extent=spacing*(size-1)
    axis=(np.arange(size)-(size-1)*.5)*spacing
    xx,zz=np.meshgrid(center[0]+axis,center[1]+axis)
    grid=np.column_stack((xx.ravel(),np.zeros(xx.size),zz.ravel(),np.zeros(xx.size)))
    h=height.run(grid).reshape(size,size,16)
    ground=h[:,:,12]-h[:,:,3]*.35
    low=float(ground.min()-30.);high=float(h[:,:,12].max()+30.)
    sample=DensityProbe(gl).sample(7,(float(xx[0,0]),float(zz[0,0])),spacing,
        low,(high-low)/96.,size=size,depth=97,seed=1337,**SCENE_OPTIONS)
    fig=plt.figure(figsize=(14,9),constrained_layout=True)
    gs=fig.add_gridspec(2,2,height_ratios=(1.15,1.))
    ax=fig.add_subplot(gs[0,0]);draw_projected_mesh(ax,sample,isosurface(sample))
    ax.set_title('Montagnes creusées : densité GLSL réelle',fontsize=11)
    ax=fig.add_subplot(gs[0,1]);im=ax.imshow(h[:,:,2],origin='lower',cmap='viridis',
        vmin=0,vmax=4,extent=(-extent/2000.,extent/2000.,-extent/2000.,extent/2000.))
    ax.set_xlabel('km');ax.set_ylabel('km');ax.set_title('Nombre de couches avant excavation')
    fig.colorbar(im,ax=ax,ticks=range(5),label='Couches de 600 m')
    row=size//2
    ax=fig.add_subplot(gs[1,0]);ax.imshow(sample['density'][row]>0.,origin='lower',
        extent=(-extent/2.,extent/2.,low,high),cmap='gray_r',interpolation='nearest',aspect='auto')
    ax.plot(axis,ground[row],color='#c0781c',linewidth=1.,label='Sol conservé')
    ax.plot(axis,h[row,:,12],color='#5f7582',linewidth=.8,label='Sommet avant excavation')
    ax.set_xlabel('Distance X au centre (m)');ax.set_ylabel('Altitude (m)')
    ax.set_title('Coupe du volume : noir = solide, blanc = air');ax.legend(fontsize=8)
    ax=fig.add_subplot(gs[1,1]);cases=report['world']
    colors={.2:'#819ca1',.35:'#557b63',1.:'#a48665'}
    ax.bar(np.arange(len(cases)),[100*x['void_fraction'] for x in cases],
        color=[colors[x['detail_scale']] for x in cases])
    ax.axhline(60.,color='#b34a3c',linestyle='--',label='Cible : 60 %')
    ax.set_xticks(np.arange(len(cases)),[str(x['seed'])+'\n'+str(x['detail_scale']) for x in cases],fontsize=8)
    ax.set_ylim(0,100);ax.set_ylabel('Vide dans les montagnes (%)');ax.set_xlabel('Seed / detailScale')
    ax.set_title('Mesures dans les véritables couches');ax.legend(fontsize=8)
    fig.suptitle('Forêt boréale — quatre couches simplex et cavités Voronoï\n'
        'Reconstruction de diagnostic, pas une capture du jeu.',fontsize=15)
    path.parent.mkdir(parents=True,exist_ok=True);fig.savefig(path,dpi=115);plt.close(fig)
    return dict(seed=1337,center_xz_m=center.tolist(),horizontal_extent_m=extent,
        horizontal_samples=size,vertical_samples=97,horizontal_spacing_m=spacing,
        note='All densities are actual GLSL; hard layer boundaries are resolved at the plotted sampling step.')

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,default=Path('docs/biome-validation/boreal_forest.json'))
    parser.add_argument('--render-only',action='store_true',help='Render the existing measured report without rerunning unchanged checks')
    args=parser.parse_args()
    if args.render_only:
        report=json.loads(args.output.read_text())
        assert report['source_sha256']==source_fingerprint(7),'Report shader fingerprint is stale'
        with GL() as gl:report['figure']=render_figure(gl,args.output.with_suffix('.png'),report)
        args.output.write_text(json.dumps(report,indent=2)+'\n')
        return
    fingerprint=source_fingerprint(7)
    with GL() as gl:
        report=dict(renderer=gl.renderer,source_sha256=fingerprint)
        print('Checking old ground preservation and four discrete noise layers...',flush=True)
        report['layers'],active=height_checks(gl)
        report['world']=[]
        for scale in (.2,.35,1.):
            print(f'Checking excavation and volume bounds at scale {scale}...',flush=True)
            records,target=world_checks(gl,active,scale);report['world']+=records
            if scale==.35:
                seed,position=target;density=DensityProbe(gl)
                sample=density.sample(7,(float(position[0]-8),float(position[2]-8)),2.,
                    float(position[1]-8),2.,size=9,depth=9,seed=seed,**SCENE_OPTIONS)
                report['targeted_pipeline']=check_targeted_pipeline(FieldRunner(gl),density,sample,7,seed)
        print('Checking mixed mountain controls...',flush=True)
        report['mixed']=mixed_checks(gl,active)
        assert source_fingerprint(7)==fingerprint,'Sources changed during test; rerun'
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report,indent=2))


if __name__=='__main__':main()
