#!/usr/bin/env python3
"""Calibrate and validate ColdForest's actual radial Perlin void gradient.

The quantiles encode ensemble probabilities, not exact quotas per finite pit.
This uses the production GLSL helper, held-out seeds, sub-knot probabilities and
finite conical shells. It prints replacement values but never edits shaders.
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
HELPER = ROOT / 'Asset/Shader/ProceduralTerrain/biomes/ColdForest/pit_porosity.glsl'
SETTINGS = HELPER.with_name('pit_settings.glsl')
SOURCE = r'''
layout(local_size_x=256) in;
layout(std430,binding=0) readonly buffer Input { vec4 points[]; };
layout(std430,binding=1) writeonly buffer Output { vec4 results[]; };
uniform uint uCount;
uniform uint uMode;
uniform uint uKey;
uniform uint uSamplesPerPit;
void main() {
    uint i=gl_GlobalInvocationID.x;
    if(i>=uCount) return;
    uint key=uMode==0u ? noiseInternalHash(i^uKey)
        : noiseInternalHash((i/uSamplesPerPit)^uKey);
    vec3 q=points[i].xyz;
    float fraction=points[i].w;
    results[i]=vec4(coldForestPoreNoise(q,key),
        coldForestPoreThreshold(fraction),
        coldForestPoreField(q,key,fraction),fraction);
}
'''


def run(gl, program, inputs, output, points, mode=0, key=0, samples_per_pit=1):
    points=np.asarray(points,dtype='<f4')
    gl.resize(inputs,points.tobytes());gl.bind(inputs,0)
    gl.resize(output,bytes(len(points)*16));gl.bind(output,1)
    gl.uniform(program,'uCount','1ui',len(points))
    gl.uniform(program,'uMode','1ui',mode)
    gl.uniform(program,'uKey','1ui',key)
    gl.uniform(program,'uSamplesPerPit','1ui',samples_per_pit)
    gl.dispatch(program,(len(points)+255)//256)
    result=np.frombuffer(gl.read(output,len(points)*16),dtype='<f4').copy().reshape(-1,4)
    assert np.isfinite(result).all()
    assert np.max(np.abs((result[:,0]-result[:,1])-result[:,2]))<1.e-6
    return result


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--power',type=int,default=21)
    parser.add_argument('--pits',type=int,default=128)
    parser.add_argument('--shell-power',type=int,default=12)
    parser.add_argument('--output',type=Path)
    args=parser.parse_args()
    assert 12<=args.power<=23 and 6<=args.shell_power<=16 and 1<=args.pits<=1024
    probabilities=np.r_[.5*(np.arange(33)/32)**2,
                         1.-.5*(np.arange(31,-1,-1)/32)**2]
    settings=SETTINGS.read_text()
    scale=float(re.search(r'COLD_FOREST_PORE_SCALE\s*=\s*([0-9.]+)',settings).group(1))
    points=np.zeros((2**args.power,4),dtype='<f4')
    points[:,:3]=(qmc.Sobol(3,scramble=True,seed=6871).random_base2(args.power)-.5)*81920.
    points[:,3]=.5
    report={'helper_sha256':hashlib.sha256(HELPER.read_bytes()).hexdigest(),
            'settings_sha256':hashlib.sha256(SETTINGS.read_bytes()).hexdigest(),
            'noise_scale_m':scale,'calibration_samples':len(points),
            'pit_count':args.pits,'samples_per_shell':2**args.shell_power,
            'probabilities':probabilities.tolist(),
            'interpretation':'Void probability of the 3D pore field within its cone envelope; individual pits fluctuate.'}
    with GL() as gl:
        report['renderer']=gl.renderer
        program=gl.compile([(0x91B9,'#version 450 core\n'+shader_source(HELPER)+SOURCE)],'ColdForest radial Perlin porosity')
        inputs=gl.buffer(0);output=gl.buffer(0)
        result=run(gl,program,inputs,output,points,key=91271)
        thresholds=np.quantile(result[:,0],probabilities)
        thresholds[0]=-1.0001;thresholds[-1]=1.0001
        report['measured_quantiles']=thresholds.tolist()
        print('const float COLD_FOREST_PORE_QUANTILES[65] = float[65](')
        print('    '+', '.join(f'{v:.9f}' for v in thresholds)+');',flush=True)
        points[:,:3]=(qmc.Sobol(3,scramble=True,seed=7459).random_base2(args.power)-.5)*81920.
        independent=np.sort(run(gl,program,inputs,output,points,key=84627)[:,0])
        # Probe interpolation between knots as well as the exact endpoints.
        test_probabilities=np.linspace(0.,1.,1025)
        queries=np.zeros((len(test_probabilities),4),dtype='<f4')
        queries[:,3]=test_probabilities
        actual_thresholds=run(gl,program,inputs,output,queries)[:,1]
        observed=np.searchsorted(independent,actual_thresholds,side='left')/len(independent)
        report['max_independent_probability_error']=float(np.max(np.abs(observed-test_probabilities)))
        assert np.all(np.diff(actual_thresholds)>=0.)
        assert observed[0]==0. and observed[-1]==1.
        assert report['max_independent_probability_error']<.004
        print('Held-out maximum probability error:',report['max_independent_probability_error'],flush=True)
        shell_unit=qmc.Sobol(2,scramble=True,seed=3518).random_base2(args.shell_power)
        angle=shell_unit[:,0]*2.*np.pi
        slices=[]
        # This validates the pore helper in its finite pit frame. The separate
        # ColdForest geometry test checks envelope depth and spiral admission.
        for radius01 in (0.,.01,.05,.1,.2,.3,.4,.5,.6,.7,.8,.9,.95,.99,1.):
            shell=np.zeros((args.pits,len(shell_unit),4),dtype='<f4')
            shell[:,:,0]=1000.*radius01*np.cos(angle)
            shell[:,:,2]=1000.*radius01*np.sin(angle)
            shell[:,:,1]=-800.*(1.-radius01)*shell_unit[:,1]
            shell[:,:,3]=1.-radius01
            r=run(gl,program,inputs,output,shell.reshape(-1,4),1,42791,len(shell_unit))
            per_pit=(r[:,2]<0.).reshape(args.pits,-1).mean(axis=1)
            target=1.-radius01
            slices.append({'radius01':radius01,'target_void':target,
                           'observed_void':float(per_pit.mean()),
                           'per_pit_min':float(per_pit.min()),'per_pit_max':float(per_pit.max())})
            print(f'r={radius01:.2f}: target {target:.1%}, pooled {per_pit.mean():.3%}, '
                  f'individual range {per_pit.min():.2%}..{per_pit.max():.2%}',flush=True)
        report['radial_shells']=slices
        report['max_shell_probability_error']=max(abs(row['observed_void']-row['target_void']) for row in slices)
        assert slices[0]['observed_void']==1. and slices[-1]['observed_void']==0.
        assert report['max_shell_probability_error']<.025
        gl.check('ColdForest porosity calibration')
    if args.output:
        args.output.parent.mkdir(parents=True,exist_ok=True)
        args.output.write_text(json.dumps(report,indent=2)+'\n')


if __name__=='__main__':main()
