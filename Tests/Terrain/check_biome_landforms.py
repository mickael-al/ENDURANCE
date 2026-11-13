#!/usr/bin/env python3
"""Measure actual per-biome GLSL landforms, their support and traversal proxies.

The probes execute the production relief/volume functions on OpenGL. They do not
reimplement noise, SDFs or site hashes on the CPU. Optional figures contain those
samples only. Slope/connectivity on a height grid are diagnostic proxies, not a
claim about the player controller or the full 3D collision surface.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path
import re
import struct
import sys
import os
import tempfile
import zipfile
import zlib

import numpy as np
from scipy import ndimage

from biome_test_support import BIOME_NAMES, biome_recipe_source
from check_biomes import BiomeProbe, BIOMES, profile_probe_source
from check_gpu import SHADERS, FieldRunner, lattice_values
from field_batch_runner import default_uniforms
from gl_headless import GL, GLUnavailable

SCENE_OPTIONS={"uHeightAmplitude":1000.,"uHeightFrequency":.000015,"uDetailScale":.35,
    "uBaseHeight":0.,"uSeaLevel":-100.,"uSeaBlendWidth":64.}

HEIGHT_PROBE = r"""
layout(local_size_x=8,local_size_y=8) in;
struct ProbeSample { vec4 terrain; vec4 volume; vec4 samples; vec4 shape; };
layout(std430,binding=6) writeonly buffer ProbeOutput { ProbeSample outputValues[]; };
uniform uint uProbeSize;
uniform vec2 uProbeOrigin;
uniform float uProbeSpacing;
void main() {
    uvec2 index=gl_GlobalInvocationID.xy;
    if(any(greaterThanEqual(index,uvec2(uProbeSize)))) return;
    vec2 p=uProbeOrigin+vec2(index)*uProbeSpacing;
    FieldColumn c=fieldProbeColumn(p);
    outputValues[index.x+index.y*uProbeSize]=ProbeSample(c.terrain,
        fieldProbeVolume(c),vec4(0),c.blend);
}
"""

SITE_PROBE = r"""
layout(local_size_x=8,local_size_y=8) in;
struct ProbeSample { vec4 terrain; vec4 volume; vec4 samples; vec4 shape; };
layout(std430,binding=6) writeonly buffer ProbeOutput { ProbeSample outputValues[]; };
uniform uint uProbeSize;
uniform vec2 uProbeOrigin;
uniform float uProbeSpacing;
void main() {
    uvec2 index=gl_GlobalInvocationID.xy;
    if(any(greaterThanEqual(index,uvec2(uProbeSize)))) return;
    vec2 p=uProbeOrigin+vec2(index)*uProbeSpacing;
    LandSite s=SITE_CALL;
    p=s.center;
    LandSite centered=SITE_CALL;
    outputValues[index.x+index.y*uProbeSize]=ProbeSample(
        vec4(s.center,s.radius,centered.mask),vec4(s.q,s.mask,s.variant),vec4(0),vec4(0));
}
"""

DENSITY_PROBE = r"""
layout(local_size_x=8,local_size_y=8) in;
layout(std430,binding=6) writeonly buffer ProbeOutput { float values[]; };
layout(std430,binding=8) writeonly buffer ProbeColumns { vec4 columns[]; };
uniform uint uProbeSize;
uniform uint uProbeDepth;
uniform vec2 uProbeOrigin;
uniform float uProbeSpacing;
uniform float uProbeMinY;
uniform float uProbeStepY;
void main() {
    uvec2 index=gl_GlobalInvocationID.xy;
    if(any(greaterThanEqual(index,uvec2(uProbeSize)))) return;
    vec2 xz=uProbeOrigin+vec2(index)*uProbeSpacing;
    FieldColumn c=fieldProbeColumn(xz);
    columns[index.x+index.y*uProbeSize]=vec4(c.terrain.x,c.terrain.w,fieldProbeVolume(c).x,c.blend.w);
    for(uint j=0u;j<uProbeDepth;++j) {
        float y=uProbeMinY+float(j)*uProbeStepY;
        values[index.x+j*uProbeSize+index.y*uProbeSize*uProbeDepth]=fieldProbeDensity(vec3(xz.x,y,xz.y),c);
    }
}
"""


class DensityProbe(BiomeProbe):
    def __init__(self, gl):
        super().__init__(gl,source=DENSITY_PROBE)
        self.column_buffer=gl.buffer(0)

    def sample(self, biome, origin, spacing, min_y, step_y, size=81, depth=129, seed=1337, **overrides):
        gl=self.gl
        program=self.program_for(biome)
        uniforms=default_uniforms(64,uBiomeOverride=biome,**overrides)
        uniforms.update({"uSeed":("1ui",seed),"uProbeSize":("1ui",size),"uProbeDepth":("1ui",depth),
            "uProbeOrigin":("2f",*origin),"uProbeSpacing":("1f",spacing),
            "uProbeMinY":("1f",min_y),"uProbeStepY":("1f",step_y)})
        for name,args in uniforms.items():gl.uniform(program,name,*args)
        gl.bind(self.mapping,7)
        nan=struct.pack("<I",0x7fc00001)
        gl.resize(self.buffer,nan*(size*size*depth))
        gl.resize(self.column_buffer,nan*(size*size*4))
        gl.bind(self.buffer,6);gl.bind(self.column_buffer,8)
        gl.dispatch_extent(program,size,size)
        density=np.frombuffer(gl.read(self.buffer,size*size*depth*4),dtype='<f4').copy().reshape(size,depth,size)
        columns=np.frombuffer(gl.read(self.column_buffer,size*size*16),dtype='<f4').copy().reshape(size,size,4)
        assert np.isfinite(density).all() and np.isfinite(columns).all(),("Nonfinite/unwritten actual field",biome)
        gl.check("landform density probe")
        return {"density":density,"columns":columns,"origin":origin,"spacing":spacing,
                "min_y":min_y,"step_y":step_y,"seed":seed,"biome":biome}


def site_recipe(biome):
    """Read placement inputs from the actual recipe; evaluate the hash on GPU."""
    directory=SHADERS/'biomes'/BIOME_NAMES[biome]
    relief=(directory/'relief.C.glsl').read_text()
    volume=(directory/'volume.C.glsl').read_text()
    pattern=r'landSite\(\s*p(?:\.xz)?\s*,\s*([^\)]+)\)'
    a=re.findall(pattern,relief);b=re.findall(pattern,volume)
    assert a and b,("Missing targeted site recipe",biome)
    normalize=lambda x: re.sub(r'\s+','',x)
    assert {normalize(x) for x in a}=={normalize(x) for x in b},("2D/3D placement mismatch",biome,a,b)
    assert len(set(normalize(x) for x in a))==1,("Multiple site families need distinct diagnostic sampling",biome,a)
    args=a[0]
    spacing,salt,occupancy=[x.strip() for x in args.split(',')]
    return args,float(spacing),float(occupancy)


def find_sites(gl, biome, seed=1337):
    args,spacing,occupancy=site_recipe(biome)
    probe=BiomeProbe(gl,source=SITE_PROBE.replace('SITE_CALL','landSite(p,'+args+')'))
    result=probe.run(biome=biome,size=12,origin=(-5.5*spacing,-5.5*spacing),spacing=spacing,uSeed=seed)
    data=np.asarray(result['values']).reshape(-1,16)
    active=data[data[:,3]>.5]
    assert len(active)>=3,("Too few seeded sites in 144 placement cells",biome,len(active),occupancy)
    # Keep a nearby example plus another shape variant for independent testing.
    distance=np.sum(active[:,:2]**2,axis=1)
    sculpted=active[active[:,7]>.80]
    assert len(sculpted), ("No high-variant site found",biome)
    nearest=sculpted[np.argmin(np.sum(sculpted[:,:2]**2,axis=1))]
    other=active[np.argmin(active[:,7])]
    median=active[np.argmin(np.abs(active[:,7]-.5))]
    sites=[]
    for row in (nearest,other,median):
        sites.append({"center":[float(row[0]),float(row[1])],"radius":float(row[2]),"variant":float(row[7])})
    return sites,{"spacing_m":spacing,"occupancy":occupancy,"active_cells":int(len(active)),"sampled_cells":144}


def source_fingerprint(biome):
    """Identify expanded recipe dependencies, probe and actual pipeline shaders."""
    from gl_headless import shader_source
    directory=SHADERS/'biomes'/BIOME_NAMES[biome]
    source='\n'.join(biome_recipe_source(directory/name) for name in ('relief.C.glsl','volume.C.glsl'))
    source+='\n'+profile_probe_source()+HEIGHT_PROBE+SITE_PROBE+DENSITY_PROBE
    # Include every entry point used by the production comparison, so a recipe
    # image cannot silently inherit a report from an older accumulation path.
    for path in sorted(SHADERS.glob('*.C.glsl')):
        source+='\n'+shader_source(path)
    return hashlib.sha256(source.encode()).hexdigest()


def height_grid(probe,biome,origin,spacing,size=129,seed=1337,**options):
    sample=probe.run(biome=biome,size=size,origin=origin,spacing=spacing,uSeed=seed,**options)
    return np.asarray(sample['values'],dtype=np.float32).reshape(size,size,16)


def traversal_metrics(height,spacing):
    dz,dx=np.gradient(height,spacing,spacing)
    slope=np.degrees(np.arctan(np.hypot(dx,dz)))
    gentle=slope<30.
    labels,count=ndimage.label(gentle,structure=ndimage.generate_binary_structure(2,1))
    populations=np.bincount(labels.ravel());populations[0]=0
    largest=int(np.argmax(populations)) if count else 0
    component=labels==largest if largest else np.zeros_like(gentle)
    return {"slope_below_30deg_fraction":float(gentle.mean()),
        "slope_p50_degrees":float(np.quantile(slope,.50)),"slope_p95_degrees":float(np.quantile(slope,.95)),
        "largest_gentle_component_fraction":float(component.mean()),
        "largest_component_crosses_x":bool(component[:,0].any() and component[:,-1].any()),
        "largest_component_crosses_z":bool(component[0,:].any() and component[-1,:].any()),
        "height_min_m":float(height.min()),"height_max_m":float(height.max())}


def check_site_edges(probe,biome,site,spacing,seed=1337,**options):
    center=np.asarray(site['center']);radius=site['radius']
    jumps=[]
    for angle in np.arange(8)*math.pi/4:
        direction=np.asarray((math.cos(angle),math.sin(angle)))
        inside=height_grid(probe,biome,tuple(center+direction*(radius-.01)),0.,size=1,seed=seed,**options)[0,0]
        outside=height_grid(probe,biome,tuple(center+direction*(radius+.01)),0.,size=1,seed=seed,**options)[0,0]
        assert outside[3]==0.,("3D band leaks outside the site",biome,angle,outside[3])
        jump=abs(float(inside[0]-outside[0]));jumps.append(jump)
        assert jump<.10,("Relief jumps at site support edge",biome,angle,jump)
    cell=np.floor(center/spacing)
    for axis in (0,1):
        point=center.copy();point[axis]=(cell[axis]+1)*spacing
        delta=np.zeros(2);delta[axis]=.001
        left=height_grid(probe,biome,tuple(point-delta),0.,size=1,seed=seed,**options)[0,0]
        right=height_grid(probe,biome,tuple(point+delta),0.,size=1,seed=seed,**options)[0,0]
        assert left[3]==right[3]==0.,("Placement cell edge enables a volume",biome,axis)
        jump=abs(float(left[0]-right[0]));jumps.append(jump)
        assert jump<.05,("Relief jumps across a placement cell",biome,axis,jump)
    return max(jumps)


def sample_site(height_probe,density_probe,biome,site,size=81,depth=129,seed=1337,**options):
    radius=site['radius'];center=np.asarray(site['center'])
    origin=tuple(center-radius)
    spacing=2*radius/(size-1)
    h=height_grid(height_probe,biome,origin,spacing,size=size,seed=seed,**options)
    low=float(np.min(h[:,:,5]))-2.;high=float(np.max(h[:,:,6]))+2.
    return density_probe.sample(biome,origin,spacing,low,(high-low)/(depth-1),size,depth,seed,**options)


def density_metrics(sample):
    density=sample['density'];height=sample['columns'][:,:,0]
    y=sample['min_y']+np.arange(density.shape[1])*sample['step_y']
    base=height[:,None,:]-y[None,:,None]
    added=(base<-.25)&(density>.25)
    removed=(base>.25)&(density<-.25)
    delta=density-base
    # Includes every column and altitude in the field, not only zero crossings.
    outside=np.abs(y[None,:,None]-(height[:,None,:]+sample['columns'][:,None,:,2]))>sample['columns'][:,None,:,1]*.5+1.
    leak=float(np.max(np.abs(delta[outside]))) if outside.any() else 0.
    assert leak<.0002,("Volume changed density outside its band",sample['biome'],leak)
    # An opening can be carved wholly inside an added mass, without cutting
    # the original heightfield. Count open air under a solid roof independently.
    solid=density>0.
    void=~solid
    below=np.maximum.accumulate(solid,axis=1)
    above=np.maximum.accumulate(solid[:,::-1,:],axis=1)[:,::-1,:]
    labels,_=ndimage.label(void,structure=ndimage.generate_binary_structure(3,1))
    boundary=np.unique(np.concatenate((labels[0].ravel(),labels[-1].ravel(),labels[:,:,0].ravel(),labels[:,:,-1].ravel(),labels[:,-1,:].ravel())))
    boundary=boundary[boundary!=0]
    open_air=np.isin(labels,boundary)
    under_roof=void&below&above&open_air
    run=np.zeros(height.shape,dtype=np.int32);longest=np.zeros_like(run)
    for j in range(density.shape[1]):
        run=np.where(under_roof[:,j,:],run+1,0)
        longest=np.maximum(longest,run)
    clearance=np.maximum(longest-1,0)*sample['step_y']
    return {"solid_added_samples":int(added.sum()),"solid_removed_samples":int(removed.sum()),
        "open_air_under_roof_samples":int(under_roof.sum()),
        "sampled_roof_clearance_lower_bound_m":float(clearance.max()),
        "columns_with_open_roof_clearance_at_least_1_8m":int((clearance>=1.8).sum()),
        "maximum_positive_delta_m":float(delta.max()),"maximum_negative_delta_m":float(delta.min()),
        "outside_band_maximum_delta_m":leak,"density_min":float(density.min()),"density_max":float(density.max()),
        "density_grid":[int(x) for x in density.shape],"horizontal_sampling_m":sample['spacing'],
        "vertical_sampling_m":sample['step_y']}


def check_targeted_pipeline(fields,density_probe,sample,biome,seed):
    density=sample['density'];h=sample['columns'][:,:,0]
    y=sample['min_y']+np.arange(density.shape[1])*sample['step_y']
    delta=density-(h[:,None,:]-y[None,:,None])
    z,j,x=np.unravel_index(np.argmax(np.abs(delta)),delta.shape)
    position=(sample['origin'][0]+x*sample['spacing'],float(y[j]),sample['origin'][1]+z*sample['spacing'])
    anchor=tuple(int(math.floor(v/2))*2 for v in position)
    fine_origin=tuple(v-4 for v in anchor);coarse_origin=tuple(v-8 for v in anchor)
    fine=fields.run(cells=8,origin=fine_origin,seed=seed,uBiomeOverride=biome,**SCENE_OPTIONS)
    coarse=fields.run(cells=8,origin=coarse_origin,lod=1,seed=seed,uBiomeOverride=biome,**SCENE_OPTIONS)
    a,b=lattice_values(fine),lattice_values(coarse);shared=a.keys()&b.keys()
    assert len(shared)>=125
    assert all(a[p]==b[p] for p in shared),("Targeted sculpted field changes with LOD",biome,anchor)
    actual=density_probe.sample(biome,(fine_origin[0]-1.,fine_origin[2]-1.),1.,fine_origin[1]-1.,1.,
        size=fine['side'],depth=fine['side'],seed=seed,**SCENE_OPTIONS)['density'].ravel()
    assert np.allclose(actual,np.asarray(fine['density']),rtol=2e-6,atol=2e-5),("Targeted point probe differs from actual production accumulation",biome,float(np.max(np.abs(actual-fine['density']))))
    return {"anchor":anchor,"shared_exact_lod_samples":len(shared),"production_density_samples":int(len(actual))}


def closeup_sample(height_probe,density_probe,sample,biome,seed):
    # Center the detail on actual extra surface height. This is a disclosed
    # targeted view of the sculpture, not a random tile or a hand-made model.
    density=sample['density'];h=sample['columns'][:,:,0]
    y=sample['min_y']+np.arange(density.shape[1])*sample['step_y']
    top=np.max(np.where(density>0,y[None,:,None],-1.e20),axis=1)
    z,x=np.unravel_index(np.argmax(top-h),h.shape)
    center=np.asarray(sample['origin'])+np.asarray((x,z))*sample['spacing']
    origin=tuple(center-72.)
    height=height_grid(height_probe,biome,origin,1.5,size=97,seed=seed,**SCENE_OPTIONS)[:,:,0]
    low=float(height.min())-12.;high=float(height.max())+110.
    return density_probe.sample(biome,origin,1.5,low,(high-low)/128.,size=97,depth=129,seed=seed,**SCENE_OPTIONS)


def valid_saved_sample(output, record):
    path=output/f"biome_{record['id']:02d}_samples.npz"
    required={'density','columns','origin','spacing','min_y','step_y',
        'close_density','close_columns','close_origin','close_spacing','close_min_y','close_step_y',
        'landscape_height','landscape_origin','seed','biome'}
    try:
        with zipfile.ZipFile(path) as archive:
            if archive.testzip() is not None:return False
        with np.load(path,allow_pickle=False) as sample:
            return (required.issubset(sample.files) and int(sample['seed'])==record['seed']
                and int(sample['biome'])==record['id'])
    except (OSError,ValueError,KeyError,EOFError,zipfile.BadZipFile,zlib.error):
        return False


def write_checkpoint(output, records, renderer):
    """Publish complete measurements atomically; a killed run keeps its last JSON."""
    payload={"renderer":renderer,
        "note":"Samples of actual GLSL fields. Height-grid slopes/connectivity are proxies; do not certify gameplay, collisions or GPU hardware performance.",
        "records":sorted(records,key=lambda r:r['id'])}
    temporary=None
    try:
        with tempfile.NamedTemporaryFile(mode='w',encoding='utf-8',dir=output,
                prefix='.biome_metrics_',suffix='.tmp',delete=False) as file:
            temporary=Path(file.name)
            json.dump(payload,file,indent=2,ensure_ascii=False)
            file.write('\n');file.flush();os.fsync(file.fileno())
        temporary.replace(output/'biome_landform_metrics.json')
    finally:
        if temporary is not None:temporary.unlink(missing_ok=True)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,help='Write measured diagnostic figures and JSON here')
    parser.add_argument('--biomes',type=int,nargs='+',default=list(range(25)))
    parser.add_argument('--seed',type=int,default=1337)
    parser.add_argument('--no-render',action='store_true',help='Save measurements without rendering figures')
    continuation=parser.add_mutually_exclusive_group()
    continuation.add_argument('--merge',action='store_true',help='Replace selected records in an existing output; reject stale retained sources')
    continuation.add_argument('--resume',action='store_true',help='Keep completed biomes and continue missing ones; reject stale measurements')
    args=parser.parse_args()
    if not args.biomes or any(b<0 or b>=25 for b in args.biomes):
        parser.error('--biomes must contain terrestrial IDs from 0 to 24')
    args.biomes=list(dict.fromkeys(args.biomes))
    records=[]
    if args.merge or args.resume:
        if not args.output:parser.error('--merge/--resume requires --output')
        checkpoint=args.output/'biome_landform_metrics.json'
        if args.merge or checkpoint.exists():
            old=json.loads(checkpoint.read_text())
            records=[r for r in old['records'] if args.resume or r['id'] not in args.biomes]
        for record in records:
            if (record['seed']!=args.seed or record['scene_uniforms']!=SCENE_OPTIONS
                    or record['source_sha256']!=source_fingerprint(record['id'])):
                parser.error(f"Stale retained measurements for biome {record['id']}; use a new output or --merge --biomes to replace changed records")
        # An interrupted array write is not a completed biome. Recompute selected
        # records with missing samples rather than silently skipping them.
        missing=[r['id'] for r in records if not valid_saved_sample(args.output,r)]
        if any(b not in args.biomes for b in missing):
            parser.error(f'Missing or corrupt retained samples for {missing}; include those IDs in --biomes')
        records=[r for r in records if r['id'] not in missing]
        if args.resume:
            complete={r['id'] for r in records}
            for biome in args.biomes:
                if biome in complete:print(f'REUSE {biome:02d} {BIOME_NAMES[biome]}',flush=True)
            args.biomes=[b for b in args.biomes if b not in complete]
            if not args.biomes:
                if not args.no_render:
                    from render_biome_landforms import render_diagnostics
                    render_diagnostics(args.output,sorted(records,key=lambda r:r['id']))
                return 0
    try:
        with GL() as gl:
            print(f'OpenGL {gl.version}; renderer: {gl.renderer}',flush=True)
            if records and old['renderer']!=gl.renderer:
                parser.error('Renderer changed since checkpoint; use a new output directory')
            height_probe=BiomeProbe(gl,source=HEIGHT_PROBE);density_probe=DensityProbe(gl)
            fields=FieldRunner(gl)
            for biome in args.biomes:
                assert 0<=biome<25
                sites,placement=find_sites(gl,biome,args.seed)
                site=sites[0]
                fingerprint=source_fingerprint(biome)
                sample=sample_site(height_probe,density_probe,biome,site,seed=args.seed,**SCENE_OPTIONS)
                metrics=density_metrics(sample)
                pipeline=check_targeted_pipeline(fields,density_probe,sample,biome,args.seed)
                assert metrics['solid_added_samples']>0,("Targeted sculpture never adds solid geometry",biome,site,metrics)
                assert metrics['columns_with_open_roof_clearance_at_least_1_8m']>0,("Targeted 3D form has no sampled open air under a roof with player-height clearance",biome,site,metrics)
                edge_jump=check_site_edges(height_probe,biome,site,placement['spacing_m'],args.seed,**SCENE_OPTIONS)
                origin=tuple(np.asarray(site['center'])-2048.)
                height=height_grid(height_probe,biome,origin,32.,size=129,seed=args.seed,**SCENE_OPTIONS)[:,:,0]
                traversal=traversal_metrics(height,32.)
                # A very weak minimum is deliberate: measured 2D continuity is
                # useful but is not a proof of collision-level traversability.
                assert traversal['slope_below_30deg_fraction']>.40,("Most of the broad tile is steep",biome,traversal)
                seed_options=dict(SCENE_OPTIONS,uHeightAmplitude=0.)
                repeat=height_grid(height_probe,biome,origin,32.,size=17,seed=args.seed,**seed_options)
                assert np.array_equal(repeat,height_grid(height_probe,biome,origin,32.,size=17,seed=args.seed,**seed_options)),("Nondeterministic seeded field",biome)
                changed=height_grid(height_probe,biome,origin,32.,size=17,seed=args.seed+1,**seed_options)
                assert not np.array_equal(repeat,changed),("Biome ignores seed",biome)
                alternatives=[]
                for label,alternate in zip(('low','middle'),sites[1:]):
                    alternate_sample=sample_site(height_probe,density_probe,biome,alternate,size=41,depth=65,seed=args.seed,**SCENE_OPTIONS)
                    alternate_metrics=density_metrics(alternate_sample)
                    alternate_edge=check_site_edges(height_probe,biome,alternate,placement['spacing_m'],args.seed,**SCENE_OPTIONS)
                    # An absence of sculpture/cavity is valid for low variants.
                    # Test the actual accumulation near the largest modification.
                    alternate_pipeline=check_targeted_pipeline(fields,density_probe,alternate_sample,biome,args.seed)
                    alternatives.append({'selection':label,'site':alternate,'site_geometry':alternate_metrics,
                        'maximum_site_edge_height_jump_m':alternate_edge,'production_pipeline':alternate_pipeline})
                assert source_fingerprint(biome)==fingerprint,('Source changed while measuring',biome)
                record={"id":biome,"name":BIOME_NAMES[biome],"seed":args.seed,"placement":placement,
                    "sites":sites,"site_geometry":metrics,"production_pipeline":pipeline,"maximum_site_edge_height_jump_m":edge_jump,
                    "scene_uniforms":SCENE_OPTIONS,"landscape_extent_m":4096.,"landscape_sampling_m":32.,"traversal_proxy":traversal,
                    "source_sha256":fingerprint,"alternative_sites":alternatives}
                records.append(record)
                print(f"PASS {biome:02d} {BIOME_NAMES[biome]}: +{metrics['solid_added_samples']} / -{metrics['solid_removed_samples']} sign changes; {traversal['slope_below_30deg_fraction']:.1%} gentle; edge jump {edge_jump:.6g} m",flush=True)
                if args.output:
                    args.output.mkdir(parents=True,exist_ok=True)
                    close=closeup_sample(height_probe,density_probe,sample,biome,args.seed)
                    close_data={'close_'+key:value for key,value in close.items()}
                    sample_path=args.output/f'biome_{biome:02d}_samples.npz'
                    temporary=sample_path.with_suffix('.npz.tmp')
                    try:
                        with temporary.open('wb') as file:
                            np.savez_compressed(file,**sample,**close_data,landscape_height=height,landscape_origin=origin)
                            file.flush();os.fsync(file.fileno())
                        temporary.replace(sample_path)
                    finally:
                        temporary.unlink(missing_ok=True)
                    write_checkpoint(args.output,records,gl.renderer)
                    print(f'CHECKPOINT {len(records)}/25 biomes saved',flush=True)
            if args.output:
                records.sort(key=lambda r:r['id'])
                if not args.no_render:
                    from render_biome_landforms import render_diagnostics
                    render_diagnostics(args.output,records)
    except GLUnavailable as error:
        print(f'SKIP: {error}',file=sys.stderr);return 77
    return 0

if __name__=='__main__':raise SystemExit(main())
