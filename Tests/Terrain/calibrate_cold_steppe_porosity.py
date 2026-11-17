#!/usr/bin/env python3
"""Calibrate the actual 3D Perlin CDF and validate the torus Y gradient.

The 11 quantiles describe ensemble proportions, not exact quotas on one ring.
The script prints GLSL replacement values and reports independent random-cell
validation plus finite-ring observations. It never edits production shaders.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re

import numpy as np
from scipy.stats import qmc

from gl_headless import GL, shader_source

ROOT = Path(__file__).resolve().parents[2]
HELPER = ROOT / 'Asset/Shader/ProceduralTerrain/biomes/ColdSteppe/torus_porosity.glsl'
SETTINGS = HELPER.with_name('torus_settings.glsl')

SOURCE = r'''
layout(local_size_x=256) in;
layout(std430,binding=0) readonly buffer Input { vec4 points[]; };
layout(std430,binding=1) writeonly buffer Output { vec4 results[]; };
uniform uint uCount;
uniform uint uMode;
uniform uint uKey;
void main() {
    uint i=gl_GlobalInvocationID.x;
    if(i>=uCount) return;
    vec3 q=points[i].xyz;
    uint key=uMode==0u ? noiseInternalHash(i^uKey) : uint(points[i].w);
    float n=steppeTorusPoreNoise(q,key);
    float f=steppeTorusSolidFraction(q.y);
    results[i]=vec4(n,f,steppeTorusPoreThreshold(f)-n,steppeTorusPoreThreshold(f));
}
'''


def run(gl, program, inputs, output, points, mode, key=0):
    points=np.asarray(points,dtype='<f4')
    gl.resize(inputs,points.tobytes());gl.bind(inputs,0)
    gl.resize(output,bytes(len(points)*16));gl.bind(output,1)
    gl.uniform(program,'uCount','1ui',len(points))
    gl.uniform(program,'uMode','1ui',mode)
    gl.uniform(program,'uKey','1ui',key)
    gl.dispatch(program,(len(points)+255)//256)
    result=np.frombuffer(gl.read(output,len(points)*16),dtype='<f4').copy().reshape(-1,4)
    assert np.isfinite(result).all()
    return result


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--power',type=int,default=21)
    parser.add_argument('--rings',type=int,default=128)
    parser.add_argument('--ring-power',type=int,default=12)
    parser.add_argument('--output',type=Path)
    args=parser.parse_args()
    assert 12<=args.power<=23 and 6<=args.ring_power<=16 and 1<=args.rings<=1024
    probabilities=np.linspace(.30,.80,11)
    settings=SETTINGS.read_text()
    def setting(name):
        match=re.search(r'const float '+name+r'\s*=\s*([0-9.]+)\s*;',settings)
        if not match:raise ValueError(f'Cannot read {name} from torus_settings.glsl')
        return float(match.group(1))
    major_radius=setting('STEPPE_TORUS_RADIUS')
    tube_radius=setting('STEPPE_TORUS_TUBE_RADIUS')
    points=np.zeros((2**args.power,4),dtype='<f4')
    points[:,:3]=(qmc.Sobol(3,scramble=True,seed=1087).random_base2(args.power)-.5)*81920.
    report={'helper_sha256':hashlib.sha256(HELPER.read_bytes()).hexdigest(),
            'settings_sha256':hashlib.sha256(SETTINGS.read_bytes()).hexdigest(),
            'major_radius_m':major_radius,'tube_radius_m':tube_radius,
            'noise_scale_m':setting('STEPPE_TORUS_PORE_SCALE'),
            'calibration_samples':len(points),'ring_count':args.rings,
            'samples_per_ring_slice':2**args.ring_power,
            'probabilities':probabilities.tolist(),
            'interpretation':'Statistical solid fraction of the torus before union with the ground; individual rings fluctuate.'}
    with GL() as gl:
        report['renderer']=gl.renderer
        program=gl.compile([(0x91B9,'#version 450 core\n'+shader_source(HELPER)+SOURCE)],'ColdSteppe Perlin porosity')
        inputs=gl.buffer(0);output=gl.buffer(0)
        result=run(gl,program,inputs,output,points,0,11939)
        thresholds=np.quantile(result[:,0],probabilities)
        report['measured_quantiles']=thresholds.tolist()
        print('const float STEPPE_TORUS_PORE_QUANTILES[11] = float[11](')
        print('    '+', '.join(f'{v:.9f}' for v in thresholds)+');',flush=True)
        points[:,:3]=(qmc.Sobol(3,scramble=True,seed=3841).random_base2(args.power)-.5)*81920.
        independent=run(gl,program,inputs,output,points,0,84627)[:,0]
        report['independent_fraction_at_measured_quantiles']=[float(np.mean(independent<t)) for t in thresholds]
        ring_unit=qmc.Sobol(2,scramble=True,seed=518).random_base2(args.ring_power)
        angle=ring_unit[:,0]*2*np.pi
        keys=np.arange(args.rings,dtype=np.uint32)*1009+107
        slices=[]
        for fraction in probabilities:
            y=((fraction-.30)/.50*2.-1.)*tube_radius
            radial_half=np.sqrt(max(tube_radius**2-y**2,0.))
            # Uniform area in a horizontal ring slice, not uniform radial offset.
            radius=np.sqrt((major_radius-radial_half)**2
                           +ring_unit[:,1]*4.*major_radius*radial_half)
            ring=np.zeros((args.rings,len(ring_unit),4),dtype='<f4')
            ring[:,:,0]=radius*np.cos(angle);ring[:,:,2]=radius*np.sin(angle)
            ring[:,:,1]=y;ring[:,:,3]=keys[:,None]
            r=run(gl,program,inputs,output,ring.reshape(-1,4),1)
            per_ring=(r[:,2]>0).reshape(args.rings,-1).mean(axis=1)
            slices.append({'local_y_m':float(y),'target':float(fraction),
                           'observed':float(per_ring.mean()),
                           'per_ring_min':float(per_ring.min()),'per_ring_max':float(per_ring.max()),
                           'threshold':float(r[0,3])})
            print(f'y={y:6.1f}: target {fraction:.1%}, pooled {per_ring.mean():.3%}, '
                  f'individual range {per_ring.min():.2%}..{per_ring.max():.2%}',flush=True)
        report['ring_slices']=slices
        report['max_pooled_error']=max(abs(row['observed']-row['target']) for row in slices)
        # Includes finite-ring sampling error and the .05-spaced quantile LUT.
        assert report['max_pooled_error']<.025,report['max_pooled_error']
        gl.check('ColdSteppe porosity calibration')
    if args.output:
        args.output.parent.mkdir(parents=True,exist_ok=True)
        args.output.write_text(json.dumps(report,indent=2)+'\n')


if __name__=='__main__':main()
