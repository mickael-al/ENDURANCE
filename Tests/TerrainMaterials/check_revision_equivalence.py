#!/usr/bin/env python3
"""Check a lossless material optimization against a historical complete shader.

This optional Git-backed oracle complements implementation-independent material
checks. It does not prescribe equivalence for intentional artistic changes.
"""
from __future__ import annotations

import argparse
import math
from pathlib import Path
import random
import struct

from benchmark_materials import revision_baseline
from check_gpu import MaterialRig, AtmosphereGL, export_appearance


def cases():
    defaults=dict(terrainBiomeOverride=0, testOrigin=(-.7,180.,-1.8),
                  testAxisX=(8.,0.,0.), testAxisY=(0.,0.,8.), testNormal=(0.,1.,0.),
                  testClimate=(.375,.625), worldSeed=1337, terrainAlbedoGamma=2.2,
                  microDetailStrength=.15, microDetailScale=2., testLod=0,
                  terrainSeaLevel=0., terrainSeaBlendWidth=48., debugLod=0, terrainMaterialDebug=0)
    for biome in range(26):
        for label,normal in (('ground',(0.,1.,0.)),('wall',(1.,0.,0.)),
                             ('ceiling',(0.,-1.,0.)),('slope',(.8,.6,0.))):
            for extent in (.12,8.):
                yield f'biome{biome}/{label}/extent{extent}',dict(defaults,
                    terrainBiomeOverride=biome,testNormal=normal,
                    testAxisX=(extent,0.,0.),testAxisY=(0.,0.,extent))
    for climate in ((0.,0.),(.125,.125),(.375,.625),(.95,.5),(1.,1.)):
        for height in (-60.,0.,180.):
            for extent in (.5,128.,4096.):
                yield f'climate{climate}/height{height}/extent{extent}',dict(defaults,
                    terrainBiomeOverride=-1,testClimate=climate,testOrigin=(-50.,height,20.),
                    testNormal=(.8,.6,0.),testAxisX=(extent,0.,0.),testAxisY=(0.,0.,extent))
    for seed in (0,1,987654321):
        for gamma in (1.,1.6,2.6):
            yield f'offset/seed{seed}/gamma{gamma}',dict(defaults,terrainBiomeOverride=-1,
                testOrigin=(12345.,.5,-4567.),worldSeed=seed,terrainAlbedoGamma=gamma,
                testNormal=(.8,.6,0.),testAxisX=(2.,0.,0.),testAxisY=(0.,0.,2.),testLod=7)
    # Compile-time diagnostics and no-detail variants must preserve exact
    # historical precedence, including LOD overriding material diagnostics.
    for lod in (0,1):
        for debug in range(7):
            for detail in (0.,.15):
                yield f'debugLOD{lod}/material{debug}/detail{detail}',dict(defaults,
                    debugLod=lod,terrainMaterialDebug=debug,microDetailStrength=detail,
                    terrainBiomeOverride=-1,testLod=7)



def valid_bound_tables():
    """Exercise the public validTerrainAppearance domain, not only presets.

    These ranges mirror the upload contract, independently of shader clamps.
    Mix endpoints with deterministic interior values, preserving slope ordering.
    """
    color=[(0.,1.)]*3+[(.04,1.)]
    material=color*3+[(0.,1.),(0.,1.),(0.,4.),(0.,1.),
                      (.05,.95),(0.,1.),(0.,8.),(-1.,1.)]+[(0.,4.)]*4
    surface=material*2+[(0.,1.),(0.,1.),(0.,4.),(0.,4.),
                       (.01,100000.),(.01,100000.),(-6.283186,6.283186),(0.,10000.),
                       (0.,7.),(.01,10000.),(0.,2.),(0.,1.)]
    biome=surface*2+[(0.,1.),(0.,1.),(0.,1.),(0.,4.),
                     (.05,64.),(.01,1000.),(1.,3.),(0.,.5)]
    assert len(biome)==128
    rng=random.Random(92751)
    for mode in ('low','high','alternating','interior'):
        values=[]
        for index in range(26):
            entry=[]
            for offset,(low,high) in enumerate(biome):
                fraction=(0. if mode=='low' else 1. if mode=='high'
                          else float((offset+index)%2) if mode=='alternating'
                          else rng.random())
                entry.append(low+(high-low)*fraction)
            # All eight normal families must stay integer-valued and be tested.
            entry[56]=float(index%8);entry[116]=float((index+3)%8)
            entry[120:122]=((0.,1.) if mode in ('low','high') else
                            (0.,.00001) if index%2 else (.99999,1.))
            values.extend(entry)
        yield mode,struct.pack(f'<{len(values)}f',*values)


def run(baseline_ref='5487e13',epsilon=2e-6,bounds=False,current_fragment=None,normal_epsilon=None):
    sha,old_table,old_settings,fragment=revision_baseline(baseline_ref)
    table,settings=export_appearance()
    if table!=old_table or settings!=old_settings:
        raise AssertionError('Authoring data changed; this oracle requires identical materials and settings')
    maximum=[0.,0.,0.,0.]
    mismatches=[]
    maximum_normal_angle=0.
    if normal_epsilon is None: normal_epsilon=epsilon
    with AtmosphereGL() as gl:
        old=MaterialRig(gl,old_table,(24,18),fragment=fragment)
        new=MaterialRig(gl,table,(24,18),
                        fragment=Path(current_fragment).read_text() if current_fragment else None)
        old.set(**old_settings);new.set(**settings)
        count=0
        def compare(label,controls):
            nonlocal maximum_normal_angle
            old.set(**controls);new.set(**controls)
            before=old.read();after=new.read()
            for attachment,(a,b) in enumerate(zip(before,after)):
                if not all(math.isfinite(v) for v in (*a,*b)):
                    raise AssertionError((label,attachment,'non-finite output'))
                error=max(abs(x-y) for x,y in zip(a,b))
                maximum[attachment]=max(maximum[attachment],error)
                threshold=normal_epsilon if attachment==1 else epsilon
                if error>threshold: mismatches.append((label,attachment,error,threshold))
                if attachment==1:
                    for offset in range(0,len(a),4):
                        x,y=a[offset:offset+3],b[offset:offset+3]
                        cross=(x[1]*y[2]-x[2]*y[1],x[2]*y[0]-x[0]*y[2],x[0]*y[1]-x[1]*y[0])
                        angle=math.degrees(math.atan2(math.sqrt(sum(c*c for c in cross)),sum(p*q for p,q in zip(x,y))))
                        maximum_normal_angle=max(maximum_normal_angle,angle)
        for label,controls in cases():
            compare(label,controls)
            count+=1
        if bounds:
            controls=dict(testOrigin=(-.7,180.,-1.8),testAxisX=(8.,0.,0.),
                          testAxisY=(0.,0.,8.),testClimate=(.375,.625),
                          worldSeed=1337,terrainAlbedoGamma=2.2,testLod=0,
                          debugLod=0,terrainMaterialDebug=0,microDetailStrength=.15)
            for label,bound_table in valid_bound_tables():
                old.upload(bound_table);new.upload(bound_table)
                for biome in range(26):
                    for normal in ((0.,1.,0.),(1.,0.,0.),(.8,.6,0.)):
                        for extent in (.04,8.):
                            compare(f'bounds/{label}/biome{biome}/{normal}/extent{extent}',dict(controls,
                                terrainBiomeOverride=biome,testNormal=normal,
                                testAxisX=(extent,0.,0.),testAxisY=(0.,0.,extent)))
                            count+=1
        gl.check('historical material shader equivalence')
    if mismatches:
        raise AssertionError(dict(cases=count, maximum_errors=maximum, maximum_normal_angle_degrees=maximum_normal_angle, mismatches=len(mismatches),
                                  worst=sorted(mismatches,key=lambda value:value[2],reverse=True)[:8]))
    print(f'PASS {count} cases / four G-buffer attachments against {sha}; maximum errors {maximum}; maximum normal angle {maximum_normal_angle:.8f} degrees',flush=True)
    return maximum


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline-ref',default='5487e13')
    parser.add_argument('--epsilon',type=float,default=2e-6)
    parser.add_argument('--normal-epsilon',type=float,help='Optional normal-component tolerance for compiler reassociation; other attachments retain --epsilon')
    parser.add_argument('--bounds',action='store_true',help='Also compare valid authoring range endpoints')
    parser.add_argument('--current-fragment',type=Path,
                        help='Check an expanded prototype without changing the working tree')
    args=parser.parse_args()
    run(args.baseline_ref,args.epsilon,args.bounds,args.current_fragment,args.normal_epsilon)


if __name__=='__main__':main()
