#!/usr/bin/env python3
"""Render measured GLSL fields in gray, at equal physical X/Y/Z scale.

These images are offline diagnostic reconstructions, not screenshots of the
Windows game. Marching tetrahedra linearly extracts the zero set of saved GPU
samples; it does not invent topography. The production marching-cubes path has
its separate GPU correctness suite. No textures, water or vegetation are added.
"""
from __future__ import annotations
import argparse
import json
from pathlib import Path

import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from matplotlib.colors import LightSource

CORNERS=np.asarray(((0,0,0),(1,0,0),(1,1,0),(0,1,0),(0,0,1),(1,0,1),(1,1,1),(0,1,1)),dtype=np.int32)
TETRA=((0,5,1,6),(0,1,2,6),(0,2,3,6),(0,3,7,6),(0,7,4,6),(0,4,5,6))
EDGES=((0,1),(1,2),(2,0),(0,3),(1,3),(2,3))
TRIANGLES=((),(0,3,2),(0,1,4),(1,4,2,2,4,3),(1,2,5),(0,3,5,0,5,1),
    (0,2,5,0,5,4),(5,4,3),(3,4,5),(4,5,0,5,2,0),(1,5,0,5,3,0),
    (5,2,1),(3,4,2,2,4,1),(4,1,0),(2,3,0),())


def isosurface(sample):
    field=sample['density'];nz,ny,nx=field.shape
    values=np.stack([field[z:z+nz-1,y:y+ny-1,x:x+nx-1] for x,y,z in CORNERS],axis=-1)
    crossing=(values.min(axis=-1)<=0)&(values.max(axis=-1)>0)
    z,y,x=np.nonzero(crossing)
    if not len(x):return np.empty((0,3,3),dtype=float)
    cell=np.stack((x,y,z),axis=-1)
    values=values[crossing]
    spacing=np.asarray((float(sample['spacing']),float(sample['step_y']),float(sample['spacing'])))
    origin=np.asarray((float(sample['origin'][0]),float(sample['min_y']),float(sample['origin'][1])))
    positions=(cell[:,None,:]+CORNERS[None,:,:])*spacing+origin
    result=[]
    for tetra in TETRA:
        v=values[:,tetra];p=positions[:,tetra,:]
        cases=np.sum((v>0)*np.asarray((1,2,4,8)),axis=1)
        for case,edges in enumerate(TRIANGLES):
            if not edges:continue
            use=cases==case
            if not use.any():continue
            vp=v[use];pp=p[use]
            points=[]
            for edge in edges:
                a,b=EDGES[edge]
                t=vp[:,a]/(vp[:,a]-vp[:,b])
                points.append(pp[:,a]+t[:,None]*(pp[:,b]-pp[:,a]))
            triangles=np.stack(points,axis=1).reshape(-1,3,3)
            # Cases with exactly-zero corners can create degenerate triangles.
            area=np.linalg.norm(np.cross(triangles[:,1]-triangles[:,0],triangles[:,2]-triangles[:,0]),axis=1)
            result.append(triangles[area>1e-10])
    return np.concatenate(result) if result else np.empty((0,3,3))


def self_test():
    density=np.broadcast_to((1.25-np.arange(4))[None,:,None],(4,4,4)).copy()
    sample={'density':density,'spacing':2.,'step_y':1.,'origin':(0.,0.),'min_y':0.}
    triangles=isosurface(sample)
    assert len(triangles)>0 and np.isfinite(triangles).all()
    assert np.allclose(triangles[:,:,1],1.25), 'Diagnostic extraction moved an affine zero surface'
    area=np.linalg.norm(np.cross(triangles[:,1]-triangles[:,0],triangles[:,2]-triangles[:,0]),axis=1).sum()/2
    assert abs(area-36.)<1e-8,('Diagnostic plane area mismatch',area)
    basis=camera_basis()
    assert np.allclose(np.linalg.norm(np.eye(3)@basis,axis=1),1.), 'Projection changes physical axis scale'
    marker=np.concatenate(marker_segments(0.,0.,0.))
    assert np.isclose(np.ptp(marker[:,2]),1.8), 'Human reference is not 1.80 m'


def surface_height(sample,z,x):
    d=sample['density'][z,:,x]
    above=np.flatnonzero((d[:-1]>=0)&(d[1:]<0))
    if len(above):
        j=int(above[-1]);t=float(d[j]/(d[j]-d[j+1]))
        return float(sample['min_y'])+(j+t)*float(sample['step_y'])
    return float(sample['columns'][z,x,0])


def camera_basis(close=False):
    """Orthonormal view: one world metre has the same scale on all axes."""
    azimuth=np.radians(-57.);elevation=np.radians(25. if close else 32.)
    toward=np.array((np.cos(elevation)*np.cos(azimuth),np.cos(elevation)*np.sin(azimuth),np.sin(elevation)))
    right=np.array((-np.sin(azimuth),np.cos(azimuth),0.))
    up=np.cross(toward,right)
    basis=np.column_stack((right,up,toward))
    assert np.allclose(basis.T@basis,np.eye(3),atol=1e-12)
    return basis


def marker_segments(x,z,y):
    """A physical 1.80 m reference; never enlarge its coordinates to be visible."""
    theta=np.linspace(0,2*np.pi,17)
    return [np.array(((x,z,y+.65),(x,z,y+1.5))),
        np.array(((x-.23,z,y),(x,z,y+.70),(x+.23,z,y))),
        np.array(((x-.32,z,y+1.10),(x,z,y+1.38),(x+.32,z,y+1.10))),
        np.column_stack((x+.15*np.cos(theta),np.full(theta.shape,z),y+1.65+.15*np.sin(theta)))]


def load_sample(path,prefix=''):
    with np.load(path) as data:
        names=('density','columns','origin','spacing','min_y','step_y')
        return {name:data[prefix+name] for name in names}


def overview_sample(sample):
    # Contact-sheet tiles use an exact subset, never invented/interpolated relief.
    result=dict(sample)
    result['density']=sample['density'][::2,::2,::2]
    result['columns']=sample['columns'][::2,::2]
    result['spacing']=float(sample['spacing'])*2
    result['step_y']=float(sample['step_y'])*2
    return result


def draw_projected_mesh(ax,sample,triangles,close=False):
    from matplotlib.collections import PolyCollection
    points=triangles[:,:,(0,2,1)].copy()
    center=np.asarray(sample['origin'])+(sample['density'].shape[0]-1)*float(sample['spacing'])*.5
    base_y=float(np.median(sample['columns'][:,:,0]))
    offset=np.asarray((center[0],center[1],base_y))
    points-=offset
    basis=camera_basis(close)
    projected=points@basis
    normals=np.cross(points[:,1]-points[:,0],points[:,2]-points[:,0])
    normals/=np.maximum(np.linalg.norm(normals,axis=1,keepdims=True),1e-10)
    light=np.asarray((-.45,-.55,.70));light/=np.linalg.norm(light)
    brightness=.24+.65*np.abs(normals@light)
    colors=np.column_stack((brightness,brightness,brightness,np.ones_like(brightness)))
    order=np.argsort(projected[:,:,2].mean(axis=1))
    ax.add_collection(PolyCollection(projected[order,:,:2],facecolors=colors[order],
        edgecolors='none',linewidths=0,antialiaseds=False,rasterized=True))
    lo=projected[:,:,:2].min(axis=(0,1));hi=projected[:,:,:2].max(axis=(0,1));span=hi-lo
    # Fit actual geometry directly. This removes mplot3d's empty cubic viewport
    # without changing physical X/Y/Z proportions or the sampled zero surface.
    ax.set_xlim(lo[0]-.025*span[0],hi[0]+.025*span[0])
    ax.set_ylim(lo[1]-.15*span[1],hi[1]+.03*span[1])
    ax.set_aspect('equal');ax.axis('off')
    z=int(sample['density'].shape[0]*.28);x=int(sample['density'].shape[2]*.38)
    y=surface_height(sample,z,x)
    x=float(sample['origin'][0])+x*float(sample['spacing'])
    z=float(sample['origin'][1])+z*float(sample['spacing'])
    for segment in marker_segments(x,z,y):
        projected_segment=(segment-offset)@basis
        ax.plot(projected_segment[:,0],projected_segment[:,1],color='#121212',lw=.9,zorder=3)
    desired=span[0]/5.;power=10.**np.floor(np.log10(desired))
    scale=max(value*power for value in (1,2,5,10) if value*power<=desired)
    x=lo[0]+span[0]*.04;y=lo[1]-span[1]*.08
    ax.plot((x,x+scale),(y,y),color='#303030',lw=2)
    ax.text(x+scale*.5,y+span[1]*.018,f'{scale:g} m',ha='center',va='bottom',fontsize=9,color='#303030')
    # The horizontal ruler is in the camera-right XZ direction (unit vector),
    # so its label is a true horizontal world distance, independent of azimuth.


def mesh_tile(sample,title,subtitle,destination,close=False,overview=True):
    sampled=overview_sample(sample) if overview else sample
    triangles=isosurface(sampled)
    assert len(triangles)>0,('No visible zero surface',title)
    fig=plt.figure(figsize=(6.4,4.2),facecolor='white')
    ax=fig.add_axes((.025,.12,.95,.73))
    draw_projected_mesh(ax,sampled,triangles,close)
    fig.text(.045,.96,title,fontsize=12,fontweight='bold',va='top')
    fig.text(.045,.90,subtitle,fontsize=9,color='#555555',va='top')
    fig.text(.045,.055,'XYZ 1:1 · projection orthographique · repère noir : 1,80 m',fontsize=8,color='#555555')
    fig.savefig(destination,dpi=140,facecolor='white');plt.close(fig)


def landscape_tile(data,record,destination):
    h=data['landscape_height'];half=record['landscape_extent_m']*.5;step=record['landscape_sampling_m']
    fig,ax=plt.subplots(figsize=(6.4,5.2));fig.subplots_adjust(left=.13,right=.95,bottom=.12,top=.80)
    shade=LightSource(azdeg=315,altdeg=38).hillshade(h,vert_exag=1,dx=step,dy=step)
    ax.imshow(shade,origin='lower',extent=(-half,half,-half,half),cmap='gray',vmin=0,vmax=1)
    ax.set_xticks((-half,0,half));ax.set_yticks((-half,0,half));ax.tick_params(labelsize=9)
    ax.set_xlabel('X relatif (m)',fontsize=9);ax.set_ylabel('Z relatif (m)',fontsize=9)
    fig.text(.06,.96,f"{record['id']:02d}  {record['name']}",fontsize=12,fontweight='bold',va='top')
    fig.text(.06,.90,f"Hauteur {h.min():.0f}–{h.max():.0f} m · pente <30° : {record['traversal_proxy']['slope_below_30deg_fraction']:.1%}",fontsize=9,color='#555555',va='top')
    fig.savefig(destination,dpi=140);plt.close(fig)


def section_tile(sample,record,destination):
    solid=sample['density']>0
    roof=(~solid)&np.maximum.accumulate(solid,axis=1)&np.maximum.accumulate(solid[:,::-1,:],axis=1)[:,::-1,:]
    row=int(np.argmax(roof.sum(axis=(1,2)))) if roof.any() else solid.shape[0]//2
    radius=(solid.shape[2]-1)*float(sample['spacing'])*.5
    dy=float(sample['step_y']);dx=float(sample['spacing']);low=float(sample['min_y'])
    top=low+(solid.shape[1]-1)*dy
    base=float(np.median(sample['columns'][row,:,0]))
    fig,ax=plt.subplots(figsize=(6.4,3.9));fig.subplots_adjust(left=.14,right=.96,bottom=.18,top=.80)
    ax.imshow(solid[row],origin='lower',cmap='gray_r',interpolation='nearest',
        extent=(-radius-dx*.5,radius+dx*.5,low-base-dy*.5,top-base+dy*.5),aspect='equal')
    ax.plot(np.linspace(-radius,radius,solid.shape[2]),sample['columns'][row,:,0]-base,color='#888888',lw=1,ls='--')
    ax.tick_params(labelsize=8);ax.set_xlabel('X relatif (m)',fontsize=9);ax.set_ylabel('Y relatif (m)',fontsize=9)
    fig.text(.06,.96,f"{record['id']:02d}  {record['name']}",fontsize=12,fontweight='bold',va='top')
    z=float(sample['origin'][1])+row*dx
    fig.text(.06,.90,f'Coupe ciblée · Z monde {z:.0f} m · XYZ 1:1',fontsize=9,color='#555555',va='top')
    fig.text(.06,.045,'Noir : solide · blanc : air · tirets : hauteur 2D initiale',fontsize=8,color='#555555')
    fig.savefig(destination,dpi=140);plt.close(fig)


def contact_sheet(paths,destination,title,subtitle):
    # Assemble independent scientific plots so memory is bounded by one mesh.
    from PIL import Image,ImageDraw,ImageFont
    columns=5;rows=(len(paths)+columns-1)//columns
    with Image.open(paths[0]) as first:width,height=first.size
    header=130;gap=14
    canvas=Image.new('RGB',(columns*width+(columns+1)*gap,header+rows*(height+gap)+gap),'white')
    draw=ImageDraw.Draw(canvas)
    font='/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf'
    bold='/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf'
    draw.text((32,24),title,font=ImageFont.truetype(bold,34),fill='#181818')
    draw.text((32,80),subtitle,font=ImageFont.truetype(font,23),fill='#555555')
    for i,path in enumerate(paths):
        with Image.open(path) as tile:
            canvas.paste(tile.convert('RGB'),(gap+(i%columns)*(width+gap),header+gap+(i//columns)*(height+gap)))
    canvas.save(destination)


def render_diagnostics(directory,records):
    self_test();camera_basis();camera_basis(True)
    directory=Path(directory);tiles=directory/'tiles';tiles.mkdir(exist_ok=True)
    paths={name:[] for name in ('landscape','landform','closeup','sections')}
    for record in records:
        biome=record['id'];path=directory/f'biome_{biome:02d}_samples.npz'
        data=np.load(path);sample=load_sample(path);radius=record['sites'][0]['radius']
        for name in paths:paths[name].append(tiles/f'{biome:02d}_{name}.png')
        landscape_tile(data,record,paths['landscape'][-1])
        mesh_tile(sample,f"{biome:02d}  {record['name']}",
            f"Fenêtre {2*radius:.0f} × {2*radius:.0f} m · variante {record['sites'][0]['variant']:.2f}",paths['landform'][-1])
        section_tile(sample,record,paths['sections'][-1])
        if 'close_density' in data:
            close=load_sample(path,'close_')
            mesh_tile(close,f"{biome:02d}  {record['name']}",'Détail 144 × 144 m · grille horizontale 3 m',paths['closeup'][-1],close=True)
        # Selected individual diagnostics use the complete measured lattice.
        if biome in (0,5,10,12,15,20):
            mesh_tile(sample,f"ENDURANCE · {biome:02d}  {record['name']}",
                f'Site complet · fenêtre {2*radius:.0f} m · grille mesurée {float(sample["spacing"]):.1f} m',
                directory/f'biome_{biome:02d}_whole.png',overview=False)
            if 'close_density' in data:
                mesh_tile(close,f"ENDURANCE · {biome:02d}  {record['name']}",'Détail 144 × 144 m · grille horizontale 1,5 m',
                    directory/f'biome_{biome:02d}_close.png',close=True,overview=False)
        data.close()
        print(f'RENDER {biome:02d} {record["name"]}',flush=True)
    subtitle=f"Biomes forcés · seed {records[0]['seed']} · champs GLSL réels · diagnostic hors jeu"
    contact_sheet(paths['landscape'],directory/'biome_landscape_atlas.png',f'ENDURANCE — {len(records)} paysages de 4,096 × 4,096 km',subtitle+' · relief 2D')
    contact_sheet(paths['landform'],directory/'biome_landform_atlas.png',f'ENDURANCE — {len(records)} sites sculptés',subtitle+' · échelle XYZ identique')
    contact_sheet(paths['closeup'],directory/'biome_closeup_atlas.png',f'ENDURANCE — détails avec repère de 1,80 m',subtitle+' · fenêtres 144 m · échelle XYZ identique')
    contact_sheet(paths['sections'],directory/'biome_sections_atlas.png',f'ENDURANCE — coupes des cavités et surplombs',subtitle+' · échelle XYZ identique')
    print(f'WROTE actual GLSL geometry diagnostics: {directory}',flush=True)


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('directory',type=Path)
    args=parser.parse_args()
    records=json.loads((args.directory/'biome_landform_metrics.json').read_text())['records']
    render_diagnostics(args.directory,records)

if __name__=='__main__':main()
