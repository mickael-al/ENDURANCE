#!/usr/bin/env python3
"""Validate regional Tundra networks carved by the actual Marine 3D helper.

The GPU executes the production recipes. Supply --reference-shaders pointing to
the prior Asset/Shader/ProceduralTerrain directory (e.g. a git archive snapshot).
No noise, placement, or cavity geometry is reproduced on the CPU.
"""
from pathlib import Path
import argparse
import hashlib
import json
import re

import numpy as np
from scipy import ndimage

from biome_test_support import biome_recipe_source
from check_biomes import BiomeProbe, profile_probe_source
from check_biome_landforms import (
    DensityProbe, SCENE_OPTIONS, check_targeted_pipeline,
    source_fingerprint,
)
from check_gpu import FieldRunner, ROOT
from gl_headless import GL


HEADER = r'''
layout(local_size_x=8,local_size_y=8) in;
struct ProbeSample { vec4 terrain; vec4 volume; vec4 samples; vec4 shape; };
layout(std430,binding=6) writeonly buffer ProbeOutput { ProbeSample outputValues[]; };
uniform uint uProbeSize;
uniform vec2 uProbeOrigin;
uniform float uProbeSpacing;
'''

BROAD = HEADER + r'''
void main() {
 uvec2 i=gl_GlobalInvocationID.xy;
 if(any(greaterThanEqual(i,uvec2(uProbeSize)))) return;
 vec2 p=uProbeOrigin+vec2(i)*uProbeSpacing;
 FieldColumn c=fieldProbeColumn(p);
 vec4 a,b;
 for(uint j=0u;j<4u;++j) {
  a[j]=fieldProbeDensity(vec3(p.x,c.terrain.x-float(j)*64.0,p.y),c);
  b[j]=fieldProbeDensity(vec3(p.x,c.terrain.x-256.0-float(j)*64.0,p.y),c);
 }
 outputValues[i.x+i.y*uProbeSize]=ProbeSample(c.terrain,a,b,c.blend);
}
'''

# A cave that never reaches the ground can pass a population/count test. Probe
# through the original ground plane to identify actual entrances instead.
SURFACE = HEADER + r'''
void main() {
 uvec2 i=gl_GlobalInvocationID.xy;
 if(any(greaterThanEqual(i,uvec2(uProbeSize)))) return;
 vec2 p=uProbeOrigin+vec2(i)*uProbeSpacing;
 FieldColumn c=fieldProbeColumn(p);
 vec4 d;
 const float offset[4]=float[4](8.0,0.0,-8.0,-16.0);
 for(uint j=0u;j<4u;++j)
  d[j]=fieldProbeDensity(vec3(p.x,c.terrain.x+offset[j],p.y),c);
 outputValues[i.x+i.y*uProbeSize]=ProbeSample(c.terrain,d,vec4(0),c.blend);
}
'''


def saved_camera():
    scene=json.loads((ROOT/'ENDURANCE/Scene/Terrain.scene').read_text(),strict=False)
    position=scene['freeCamPos']
    return np.array([position['x'],position['z']],dtype=float)


def surface_openings(after, before):
    # Positive solid at H-8 in the reference must now be air, continuously up
    # through H and H+8. This excludes mere underground changed densities.
    return (before[:,:,6]>0)&np.all(after[:,:,4:7]<0,axis=2)


def local_visibility(gl,baseline_directory,previous_directory,seed):
    camera=saved_camera();extent=4096.;spacing=16.;size=513
    origin=np.floor((camera-extent)/spacing)*spacing
    options=dict(biome=2,size=size,origin=tuple(origin),spacing=spacing,uSeed=seed,**SCENE_OPTIONS)
    current=BiomeProbe(gl,source=SURFACE)
    reference=BiomeProbe(gl,directory=baseline_directory,source=SURFACE)
    after=values(current,**options);before=values(reference,**options)
    apertures=surface_openings(after,before)
    z,x=np.nonzero(apertures)
    assert len(x)>0, 'No actual ground opening in the saved-camera 8-km test region'
    world=np.column_stack((origin[0]+x*spacing,origin[1]+z*spacing))
    distance=np.linalg.norm(world-camera,axis=1)
    assert distance.min()<4000., 'All visible cave entrances are more than 4km from the saved camera'
    labels,count=ndimage.label(apertures)
    populations=np.bincount(labels.ravel());populations[0]=0
    largest_area=float(populations.max(initial=0)*spacing**2)
    assert largest_area>=2048., 'Ground entrances are too small to survive ordinary distant sampling'
    old=None;old_apertures=None
    if previous_directory:
        old=values(BiomeProbe(gl,directory=previous_directory,source=SURFACE),**options)
        old_apertures=surface_openings(old,before)
    metrics=dict(camera_xz_m=camera.tolist(),map_origin_m=origin.tolist(),map_width_m=extent*2,
        map_spacing_m=spacing,ground_aperture_samples=int(apertures.sum()),
        separate_sampled_apertures=int(count),nearest_ground_aperture_m=float(distance.min()),
        largest_sampled_aperture_area_m2=largest_area,
        previous_ground_aperture_samples=int(old_apertures.sum()) if old_apertures is not None else None)
    return metrics,dict(seed=seed,camera=camera,origin=origin,spacing=spacing,
        height=after[:,:,0],apertures=apertures,previous=old_apertures,nearest=world[np.argmin(distance)])


def world_lattice_visibility(actual,baseline,candidate,seed):
    records=[]
    for spacing in (8.,16.,32.,64.):
        origin=np.floor((candidate[:2]-512.)/spacing)*spacing
        size=int(1024./spacing)+1
        columns=actual.sample(2,tuple(origin),spacing,0.,1.,size=size,depth=1,seed=seed,**SCENE_OPTIONS)['columns']
        low=float(np.floor((columns[:,:,0].min()-750.)/spacing)*spacing)
        high=float(np.ceil((columns[:,:,0].max()+100.)/spacing)*spacing)
        options=dict(biome=2,origin=tuple(origin),spacing=spacing,min_y=low,step_y=spacing,
            size=size,depth=int(round((high-low)/spacing))+1,seed=seed,**SCENE_OPTIONS)
        after=actual.sample(**options);before=baseline.sample(**options)
        air=after['density']<0
        excavated=(before['density']>0)&air
        labels,_=ndimage.label(air,structure=ndimage.generate_binary_structure(3,1))
        exterior=np.unique(labels[(before['density']<-.5)&air]);exterior=exterior[exterior>0]
        connected=excavated&np.isin(labels,exterior)
        assert connected.sum()>=8, ('Nearby cave disappears on the world-aligned coarse lattice',spacing,int(connected.sum()))
        records.append(dict(world_lattice_spacing_m=spacing,new_air_nodes=int(excavated.sum()),
            new_air_nodes_connected_to_exterior=int(connected.sum())))
    return records


def render_local(path, maps):
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    from matplotlib.colors import LightSource
    from matplotlib.patches import Circle
    fig,axes=plt.subplots(len(maps),3,figsize=(15,5*len(maps)),squeeze=False,constrained_layout=True)
    for row,m in enumerate(maps):
        h=m['height'];spacing=m['spacing'];origin=m['origin'];n=h.shape[0]
        extent=(origin[0],origin[0]+(n-1)*spacing,origin[1],origin[1]+(n-1)*spacing)
        shade=LightSource(315,40).shade(h,cmap=plt.get_cmap('Greys'),vert_exag=1,dx=spacing,dy=spacing)
        for index,(mask,title) in enumerate(((m['previous'],'Previous cave distribution'),
                                           (m['apertures'],'Corrected ground entrances'),
                                           (m['apertures'],'Nearest ground entrance, zoom'))):
            ax=axes[row,index]
            ax.imshow(shade,origin='lower',extent=extent,aspect='equal')
            if mask is not None:
                color=np.zeros((*mask.shape,4));color[mask]=[.05,.55,.8,1.]
                ax.imshow(color,origin='lower',extent=extent,aspect='equal')
                if index!=2:
                    labels,count=ndimage.label(mask)
                    for z,x in ndimage.center_of_mass(mask,labels,range(1,count+1)):
                        ax.add_patch(Circle((origin[0]+x*spacing,origin[1]+z*spacing),130.,fill=False,color='#00a5da',lw=.8))
            ax.plot(*m['camera'],'+',color='#c64a31',ms=9)
            if index==2:
                center=m['nearest'];ax.set_xlim(center[0]-350,center[0]+350);ax.set_ylim(center[1]-350,center[1]+350)
            ax.set_title(f'{title} — seed {m["seed"]}')
            ax.set_xlabel('World X (m)');ax.set_ylabel('World Z (m)')
    fig.suptitle('Actual ground openings near the saved camera — blue: air at H-8, H and H+8; +: camera',fontsize=12)
    fig.savefig(path,dpi=110);plt.close(fig)


def values(probe, **options):
    return np.asarray(probe.run(**options)['values'],dtype=np.float32).reshape(options['size'],options['size'],16)


class WithoutCaves(DensityProbe):
    """Omit the new call only; keep every existing Tundra 3D construction."""
    def program_for(self, biome):
        assert biome == 2
        if biome not in self.programs:
            directory=self.directory/'biomes'/'Tundra'
            relief=biome_recipe_source(directory/'relief.C.glsl')
            volume=biome_recipe_source(directory/'volume.C.glsl')
            volume=re.sub(r'^#version[^\n]*\n','',volume,count=1)
            volume,count=re.subn(
                r'target\s*=\s*tundraCarveCaves\(p,\s*column,\s*\w+,\s*target\);',
                '/* New cave carving omitted for preservation regression. */',volume)
            assert count == 1, 'Update cave omission adapter after entrypoint change'
            self.programs[biome]=self.gl.compile(
                [(0x91B9,relief+volume+profile_probe_source()+self.main_source)],
                'Tundra with new cave call omitted')
        return self.programs[biome]


class WithoutMarineCarving(DensityProbe):
    """Disable the actual shared Marine density evaluator, not Tundra's caller.

    If Tundra still carves rooms after this mutation, the shared helper is only
    decorative and does not generate its cavities. Existing Tundra sculptures
    do not call this helper and must remain present.
    """
    def program_for(self, biome):
        assert biome == 2
        if biome not in self.programs:
            directory=self.directory/'biomes'/'Tundra'
            relief=biome_recipe_source(directory/'relief.C.glsl')
            volume=re.sub(r'^#version[^\n]*\n','',biome_recipe_source(directory/'volume.C.glsl'),count=1)
            volume,count=re.subn(
                r'float fieldRecipeDensityAtPrepared\([^)]*\)\s*\{[\s\S]*?^\}',
                'float fieldRecipeDensityAtPrepared(vec3 p, vec2 terrain, vec4 volume, vec4 shape, vec4 style) { return terrain.x-p.y; }',
                volume,flags=re.MULTILINE)
            assert count == 1, 'Update Marine omission adapter after shared helper signature change'
            self.programs[biome]=self.gl.compile(
                [(0x91B9,relief+volume+profile_probe_source()+self.main_source)],
                'Tundra with shared Marine carving disabled')
        return self.programs[biome]


def sample_network(actual, baseline, omitted, disabled_marine, center, seed):
    # A four-kilometre region captures many noise cells instead of focusing on
    # one hand-built room. Global Y coordinates preserve the real terrain slope.
    extent=2048.;spacing=16.;size=257
    origin=tuple(np.floor((np.asarray(center)-extent)/spacing)*spacing)
    columns=actual.sample(2,origin,spacing,0.,1.,size=size,depth=1,seed=seed,**SCENE_OPTIONS)['columns']
    step=16.
    low=float(np.floor((columns[:,:,0].min()-750.)/step)*step)
    high=float(np.ceil((columns[:,:,0].max()+100.)/step)*step)
    depth=int(round((high-low)/step))+1
    assert depth<220, 'Unexpectedly large local terrain span; inspect the sampled region'
    options=dict(biome=2,origin=origin,spacing=spacing,min_y=low,step_y=step,
                 size=size,depth=depth,seed=seed,**SCENE_OPTIONS)
    after=actual.sample(**options);before=baseline.sample(**options)
    assert np.array_equal(before['columns'][:,:,0],after['columns'][:,:,0]), 'Network changed existing relief height'
    assert np.array_equal(before['columns'][:,:,2:],after['columns'][:,:,2:]), 'Network changed existing offset or pilot'
    assert np.all(after['columns'][:,:,1]>=before['columns'][:,:,1]), 'Network shrank an existing 3D band'
    legacy=omitted.sample(**options)
    assert np.array_equal(before['density'],legacy['density']), 'Omitting new carving does not restore exact original Tundra density'
    del legacy
    marine_off=disabled_marine.sample(**options)
    assert np.allclose(before['density'],marine_off['density'],rtol=0,atol=0.002), 'Cavities still exist after disabling the shared Marine evaluator'
    after['marine_disabled_max_abs_error']=float(np.max(np.abs(before['density']-marine_off['density'])))
    after['marine_disabled_bit_identical']=bool(np.array_equal(before['density'],marine_off['density']))
    return after,before


def network_metrics(after_sample,before_sample):
    after=after_sample['density'];before=before_sample['density']
    assert np.max(after-before)<=.002, 'Network adds solid terrain'
    removed=(before>0)&(after<0)
    solid=after>0;air=after<0
    above=np.maximum.accumulate(solid[:,::-1,:],axis=1)[:,::-1,:]
    below=np.maximum.accumulate(solid,axis=1)
    roofed=removed&above&below
    assert roofed.sum()>1000, 'Noise field lacks substantial underground rooms with roofs and floors'
    labels,_=ndimage.label(air,structure=ndimage.generate_binary_structure(3,1))
    exterior=np.unique(labels[(before<-.5)&air]);exterior=exterior[exterior>0]
    connected=roofed&np.isin(labels,exterior)
    fraction=float(connected.sum()/roofed.sum())
    assert connected.sum()>1000, 'No substantial roofed network reaches exterior air'
    # Do not claim that arbitrary noise connects every pocket. Record sealed
    # pockets separately instead of silently choosing only connected examples.
    del labels,above,below
    spacing=after_sample['spacing']
    best_radius=0.;best_point=None;best_slice=None;best_lobes=0
    for j in range(after.shape[1]):
        # Count only roofed, floored air that reaches an entrance. Distances on
        # a global horizontal plane distinguish real room width from wavelength.
        section=connected[:,j,:]
        if not section.any(): continue
        edt=ndimage.distance_transform_edt(np.pad(section,1))[1:-1,1:-1]*spacing
        radius=max(float(edt.max())-.5*spacing,0.)
        if radius>best_radius:
            z,x=np.unravel_index(np.argmax(edt),edt.shape)
            best_radius=radius;best_point=(z,j,x);best_slice=section.copy()
        peaks=(edt==ndimage.maximum_filter(edt,size=13))&(edt>=48.)
        _,count=ndimage.label(peaks)
        best_lobes=max(best_lobes,int(count))
    diameter=2*best_radius
    assert diameter>=200., ('No connected roofed chamber has a sampled horizontal inscribed diameter of 200m',diameter)
    assert best_lobes>=2, 'The sampled region is one isolated hollow rather than multiple noise-carved lobes'
    z,x=np.nonzero(removed.any(axis=1))
    span=(np.array([x.max()-x.min(),z.max()-z.min()])+1)*spacing
    assert max(span)>=1500., ('Excavation has not grown into a kilometre-scale network region',span.tolist())
    underground,count=ndimage.label(removed,structure=ndimage.generate_binary_structure(3,1))
    sizes=np.bincount(underground.ravel());sizes[0]=0
    substantial=int((sizes>=100).sum())
    largest_id=int(np.argmax(sizes))
    largest_box=ndimage.find_objects(underground,max_label=count)[largest_id-1]
    largest_span=[(largest_box[2].stop-largest_box[2].start)*spacing,
                  (largest_box[0].stop-largest_box[0].start)*spacing]
    largest_count=int(sizes[largest_id])
    del underground
    voxel=spacing*spacing*after_sample['step_y']
    metrics=dict(removed_samples=int(removed.sum()),roof_and_floor_samples=int(roofed.sum()),
        connected_roofed_samples=int(connected.sum()),connected_roofed_fraction=fraction,
        sealed_roofed_samples=int((roofed&~connected).sum()),removed_volume_m3=float(removed.sum()*voxel),
        largest_connected_roofed_horizontal_inscribed_diameter_m=diameter,
        horizontal_lobes_on_one_level=best_lobes,substantial_underground_components=substantial,
        largest_connected_underground_span_xz_m=largest_span,
        largest_connected_underground_volume_m3=float(largest_count*voxel),
        excavated_region_span_xz_m=span.tolist(),
        horizontal_spacing_m=spacing,vertical_spacing_m=after_sample['step_y'],
        legacy_density_restored_exactly_when_new_call_omitted=True,
        legacy_density_restored_when_shared_marine_evaluator_disabled=True,
        shared_marine_disabled_maximum_error=after_sample['marine_disabled_max_abs_error'],
        shared_marine_disabled_bit_identical=after_sample['marine_disabled_bit_identical'])
    return metrics,removed,roofed,connected,best_point,best_slice


def broad_preservation(gl,baseline_directory,seed):
    options=dict(biome=2,size=385,origin=(-24000.,-24000.),spacing=125.,uSeed=seed,**SCENE_OPTIONS)
    current=BiomeProbe(gl,source=BROAD)
    prior=BiomeProbe(gl,directory=baseline_directory,source=BROAD)
    after=values(current,**options);before=values(prior,**options)
    assert np.array_equal(after[:,:,:3],before[:,:,:3]), 'Broad-map height or climate changed'
    assert np.all(after[:,:,3]>=before[:,:,3]), 'Broad-map caves shrank an existing band'
    assert np.array_equal(after[:,:,12:],before[:,:,12:]), 'Broad-map blend controls changed'
    delta=after[:,:,4:12]-before[:,:,4:12]
    assert np.max(delta)<=0, 'Broad-map cave change adds solid'
    changed=np.any(delta!=0,axis=2)
    excavated=np.any((before[:,:,4:12]>0)&(after[:,:,4:12]<0),axis=2)
    # Regional feathers can change positive solid density without excavating
    # anything. Judge rarity by newly created air, not by those harmless deltas.
    assert 0 < excavated.mean() < .15, 'Actual excavation is missing or covers too much of the sampled biome'
    assert np.array_equal(after,values(current,**options)), 'Tundra cave field is not deterministic'
    # Compare the omitted entrypoint against the old tree over all eight depths.
    omitted=WithoutCaves(gl);omitted.main_source=BROAD
    omitted_data=values(omitted,**options)
    assert np.array_equal(omitted_data[:,:,4:12],before[:,:,4:12]), 'Legacy Tundra changed outside the new carving call'
    independent=changed&(before[:,:,3]==0)
    assert independent.sum()>0, 'New caves still depend on the old sites having a volume band'
    assert independent.sum()==changed.sum(), 'Some new cave changes overlap a previously enabled 3D band'
    assert np.array_equal(after[before[:,:,3]>0],before[before[:,:,3]>0]), 'New cave bands or carving altered original 3D site columns'
    return dict(area_km2=2304.,columns_sampled=int(changed.size),
        sampled_depths_below_surface_m=list(range(0,512,64)),
        altered_column_fraction=float(changed.mean()),exactly_unchanged_column_fraction=float((~changed).mean()),
        excavated_column_fraction=float(excavated.mean()),
        changed_columns_without_previous_band=int(independent.sum()),
        height_and_controls_unchanged_exactly=True,legacy_3d_unchanged_exactly=True)


def marine_preservation(gl,baseline_directory):
    current=BiomeProbe(gl,source=BROAD)
    prior=BiomeProbe(gl,directory=baseline_directory,source=BROAD)
    records=[]
    for seed in (1337,17):
        options=dict(biome=25,size=129,origin=(-1700.25,1300.75),spacing=29.5,uSeed=seed,**SCENE_OPTIONS)
        after=values(current,**options);before=values(prior,**options)
        assert np.array_equal(after,before), 'Extracting shared cavern helper altered Marine density'
        records.append(dict(seed=seed,columns_tested=129**2,density_samples_tested=129**2*8,bit_identical=True))
    return records


def render(path,samples):
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    from matplotlib.colors import ListedColormap
    from matplotlib.patches import Circle
    fig,axes=plt.subplots(len(samples),2,figsize=(12,5.5*len(samples)),squeeze=False,constrained_layout=True)
    for row,(seed,after_sample,before_sample,removed,roofed,metrics) in enumerate(samples):
        after=after_sample['density'];before=before_sample['density']
        n,depth,_=after.shape
        x=after_sample['origin'][0]+np.arange(n)*after_sample['spacing']
        y=after_sample['min_y']+np.arange(depth)*after_sample['step_y']
        zs=after_sample['origin'][1]+np.arange(n)*after_sample['spacing']
        cx,cy,cz=metrics['largest_measured_room_center_xyz_m']
        j=int(round((cy-after_sample['min_y'])/after_sample['step_y']))
        z=int(round((cz-after_sample['origin'][1])/after_sample['spacing']))
        section=np.zeros((n,n),dtype=np.uint8)
        section[after[:,j,:]<0]=1
        section[roofed[:,j,:]]=2
        ax=axes[row,0]
        ax.imshow(section,origin='lower',extent=[x[0],x[-1],zs[0],zs[-1]],
                  cmap=ListedColormap(['#cbd8d5','#6d9ca6','#244b65']),vmin=0,vmax=2,aspect='equal')
        diameter=metrics['largest_connected_roofed_horizontal_inscribed_diameter_m']
        ax.add_patch(Circle((cx,cz),diameter*.5,fill=False,color='#e5953b',lw=1.5))
        ax.plot(cx,cz,'+',color='#e5953b',ms=8)
        ax.set_title(f'Seed {seed}: horizontal slice at Y = {cy:.0f} m\nMeasured inscribed room diameter {diameter:.0f} m')
        ax.set_xlabel('World X (m)');ax.set_ylabel('World Z (m)')
        ax=axes[row,1]
        ax.imshow(after[z]>0,origin='lower',extent=[x[0],x[-1],y[0],y[-1]],
                  cmap=ListedColormap(['#244b65','#cbd8d5']),aspect='equal')
        ax.contour(x,y,before[z],levels=[0],colors='#669589',linewidths=.9)
        ax.plot(cx,cy,'+',color='#e5953b',ms=8)
        ax.set_xlim(max(x[0],cx-1200.),min(x[-1],cx+1200.))
        ax.set_ylim(cy-450.,min(y[-1],cy+350.))
        ax.set_title(f'Seed {seed}: vertical cut through measured chamber\nOriginal surface contour in green')
        ax.set_xlabel('World X (m)');ax.set_ylabel('World height (m)')
    fig.suptitle('Tundra networks from the Marine 3D evaluator — actual GLSL, no vertical exaggeration',fontsize=12)
    fig.savefig(path,dpi=110);plt.close(fig)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--reference-shaders',required=True,type=Path)
    parser.add_argument('--previous-shaders',type=Path,help='Optional snapshot of the previous primitive-based cave implementation')
    parser.add_argument('--output',required=True,type=Path)
    args=parser.parse_args();args.output.mkdir(parents=True,exist_ok=True)
    assert (args.reference_shaders/'biomes/Tundra/volume.C.glsl').is_file()
    fingerprint=source_fingerprint(2)
    before_source=''.join(biome_recipe_source(args.reference_shaders/'biomes/Tundra'/name)
                          for name in ('relief.C.glsl','volume.C.glsl'))
    with GL() as gl:
        actual=DensityProbe(gl);baseline=DensityProbe(gl);baseline.directory=args.reference_shaders
        omitted=WithoutCaves(gl);disabled_marine=WithoutMarineCarving(gl)
        records=[];pictures=[];local_maps=[]
        for seed in (1337,17):
            broad=broad_preservation(gl,args.reference_shaders,seed)
            local,local_map=local_visibility(gl,args.reference_shaders,args.previous_shaders,seed)
            local_maps.append(local_map)
            center=local_map['nearest']
            after,before=sample_network(actual,baseline,omitted,disabled_marine,center,seed)
            metrics,removed,roofed,connected,best_point,best_slice=network_metrics(after,before)
            z,j,x=best_point
            room_xz=(after['origin'][0]+x*after['spacing'],after['origin'][1]+z*after['spacing'])
            metrics['largest_measured_room_center_xyz_m']=[room_xz[0],after['min_y']+j*after['step_y'],room_xz[1]]
            local['world_lattices']=world_lattice_visibility(actual,baseline,np.asarray(room_xz),seed)
            wall=np.where(connected,np.abs(after['density']),np.inf)
            z,j,x=np.unravel_index(np.argmin(wall),wall.shape)
            target=dict(after,density=after['density'][z:z+1,j:j+1,x:x+1],
                columns=after['columns'][z:z+1,x:x+1],
                origin=(after['origin'][0]+x*after['spacing'],after['origin'][1]+z*after['spacing']),
                min_y=after['min_y']+j*after['step_y'])
            assert target['density'][0,0,0]<0 and before['density'][z,j,x]>0
            metrics['pipeline']=check_targeted_pipeline(FieldRunner(gl),actual,target,2,seed)
            metrics['pipeline']['excavated_wall_reference_density']=float(target['density'][0,0,0])
            pictures.append((seed,after,before,removed,roofed,metrics))
            record=dict(seed=seed,broad_preservation=broad,local_visibility=local,network=metrics)
            records.append(record);print(json.dumps(record),flush=True)
        marine=marine_preservation(gl,args.reference_shaders)
        assert source_fingerprint(2)==fingerprint, 'Production source changed during validation'
        report=dict(renderer=gl.renderer,source_sha256=fingerprint,
            test_source_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
            prior_tundra_source_sha256=hashlib.sha256(before_source.encode()).hexdigest(),
            records=records,marine_bit_preservation=marine,
            note='Actual GLSL networks in two nearest-entry regions. Real horizontal room widths are measured on global Y slices of roofed air connected to the exterior, not inferred from a noise wavelength. Sealed pockets are counted explicitly. Disabling the actual shared Marine evaluator must remove all new carving. Original relief/controls/3D sites, independent bands, local surface entries, world-aligned 8/16/32/64m visibility and production/LOD checks are verified. Geometry sampling does not certify player collision or Windows GPU performance.')
        render(args.output/'tundra_caves.png',pictures)
        render_local(args.output/'tundra_caves_local.png',local_maps)
        (args.output/'tundra_caves.json').write_text(json.dumps(report,indent=2)+'\n')
        print('PASS Tundra Marine-network preservation, 200m chambers, regional extent, entrances and production/LOD checks',flush=True)
    return 0


if __name__=='__main__':
    raise SystemExit(main())
