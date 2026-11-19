#!/usr/bin/env python3
"""Calibrate a Y-phase-conditional Voronoi CDF and mountain solidity gradient.

The production domain/noise stay unchanged. Since Voronoi feature points are
jittered inside the middle of each cell, its distance distribution depends on
vertical phase. Thirty-three calibrated phase nodes avoid biasing mountains
that occupy only a few vertical cells. The script prints GLSL and reports
independent phase, seed and normalized-height checks; it never edits shaders.
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

ROOT=Path(__file__).resolve().parents[2]
HELPER=ROOT/'Asset/Shader/ProceduralTerrain/biomes/BorealForest/cavities.glsl'
SETTINGS=HELPER.with_name('settings.glsl')
SOURCE=r'''
layout(local_size_x=256) in;
layout(std430,binding=0) readonly buffer Input { vec4 points[]; };
layout(std430,binding=1) writeonly buffer Output { vec4 results[]; };
uniform uint uCount;
uniform uint uMode;
uniform uint uSeed;
uniform float uPhase;
void main() {
    uint i=gl_GlobalInvocationID.x;
    if(i>=uCount)return;
    uint seed=(uMode==0u || uMode==2u) ? noiseInternalHash(i^uSeed) : uSeed;
    vec3 p=points[i].xyz;
    if(uMode>=2u) {
        // Only the Y-domain warp depends exclusively on XZ. Invert that one
        // component in this TEST probe to sample exact domain phases. Nothing
        // in production coordinates or noise is replaced.
        float warpY=borealCavityCoordinates(vec3(p.x,0.,p.z),seed).y;
        float phase=.5+((i&1u)==0u ? -uPhase : uPhase);
        p.y=(floor(p.y/BOREAL_CAVITY_SCALE)+phase-warpY)*BOREAL_CAVITY_SCALE;
    }
    vec3 domain=borealCavityCoordinates(p,seed);
    float solid=borealCavitySolidFraction(points[i].w);
    results[i]=vec4(borealCavityNoise(p,seed),domain.y,
        borealCavityThreshold(1.-solid,domain.y),borealCavityField(p,seed,points[i].w));
}
'''

def probe(gl,program,buffers,points,mode,seed,phase=0.):
    inputs,output=buffers; points=np.asarray(points,dtype='<f4')
    gl.resize(inputs,points.tobytes()); gl.bind(inputs,0)
    gl.resize(output,bytes(len(points)*16)); gl.bind(output,1)
    for name,kind,value in [('uCount','1ui',len(points)),('uMode','1ui',mode),
                            ('uSeed','1ui',seed),('uPhase','1f',phase)]:
        gl.uniform(program,name,kind,value)
    gl.dispatch(program,(len(points)+255)//256)
    result=np.frombuffer(gl.read(output,len(points)*16),dtype='<f4').copy().reshape(-1,4)
    assert np.isfinite(result).all(),'Nonfinite cavity field'
    return result

def points(power,sample_seed):
    unit=qmc.Sobol(3,scramble=True,seed=sample_seed).random_base2(power)
    result=np.zeros((len(unit),4),dtype='<f4')
    result[:,:3]=(unit-.5)*np.asarray((512000.,12000.,512000.))
    result[:,:3]+=np.asarray((17.349,29.713,-11.983))
    return result

def thresholds(table,phase,probability):
    phase=np.asarray(phase)*64.; probability=np.asarray(probability)*20.
    i=np.minimum(np.floor(phase).astype(int),31)
    j=np.minimum(np.floor(probability).astype(int),19)
    fp=phase-i; fq=probability-j
    return ((1.-fp)*((1.-fq)*table[i,j]+fq*table[i,j+1])
              +fp*((1.-fq)*table[i+1,j]+fq*table[i+1,j+1]))

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--phase-power',type=int,default=16)
    parser.add_argument('--phase-validation-power',type=int,default=15)
    parser.add_argument('--fixed-power',type=int,default=18)
    parser.add_argument('--output',type=Path)
    parser.add_argument('--calibrate-only',action='store_true')
    parser.add_argument('--allow-uncalibrated',action='store_true')
    args=parser.parse_args()
    assert 12<=args.phase_power<=20 and 12<=args.phase_validation_power<=19 and 12<=args.fixed_power<=21
    match=re.search(r'BOREAL_CAVITY_QUANTILES\[693\]\s*=.*?\((.*?)\);',HELPER.read_text(),re.S)
    table=np.asarray([float(v.strip()) for v in match.group(1).split(',')]).reshape(33,21)
    assert np.all(np.diff(table,axis=1)>0.)
    settings=SETTINGS.read_text()
    def setting(name):return float(re.search(r'const float '+name+r'\s*=\s*([0-9.]+)',settings).group(1))
    solid_base=setting('BOREAL_SOLID_BASE');solid_top=setting('BOREAL_SOLID_TOP')
    probabilities=np.linspace(0.,1.,21); phases=np.linspace(0.,.5,33)
    report=dict(helper_sha256=hashlib.sha256(HELPER.read_bytes()).hexdigest(),
        settings_sha256=hashlib.sha256(SETTINGS.read_bytes()).hexdigest(),
        samples_per_phase=2**args.phase_power,calibration_samples=33*2**args.phase_power,
        probabilities=probabilities.tolist(),folded_y_phase_nodes=phases.tolist(),
        production_quantiles=table.tolist(),solid_base=solid_base,solid_top=solid_top,
        cavity_scale_m=setting('BOREAL_CAVITY_SCALE'),warp_scale_m=setting('BOREAL_CAVITY_WARP_SCALE'),
        warp_strength=setting('BOREAL_CAVITY_WARP_STRENGTH'),
        interpretation='Y-phase-conditional statistical solid fraction over the full mountain stack; finite regions fluctuate.')
    with GL() as gl:
        report['renderer']=gl.renderer
        program=gl.compile([(0x91B9,'#version 450 core\n'+shader_source(HELPER)+SOURCE)],'Boreal conditional Voronoi CDF')
        buffers=gl.buffer(0),gl.buffer(0)
        sample=points(args.phase_power,76439); quantiles=[]; errors=[]
        for phase in phases:
            value=probe(gl,program,buffers,sample,2,11939,phase)
            folded=np.abs(np.mod(value[:,1],1.)-.5)
            assert np.max(np.abs(folded-phase))<1e-4,'Conditional phase probe failed'
            row=np.quantile(value[:,0],probabilities); row[0]=0.;row[-1]=1.
            quantiles.append(row.tolist())
        report['measured_quantiles']=quantiles
        print('const float BOREAL_CAVITY_QUANTILES[693] = float[693](',flush=True)
        print(',\n'.join('    '+', '.join(f'{v:.9f}' for v in row) for row in quantiles)+');',flush=True)
        if not args.calibrate_only:
            # Interleaved validation phases include LUT nodes and midpoints.
            sample=points(args.phase_validation_power,23871); rows=[]
            for phase in np.linspace(0.,.5,65):
                value=probe(gl,program,buffers,sample,2,84627,phase)
                lut=thresholds(table,phase,probabilities)
                fractions=np.asarray([np.mean(value[:,0]<t) for t in lut])
                errors.extend(np.abs(fractions-probabilities))
                expected=thresholds(table,np.abs(np.mod(value[:,1],1.)-.5),1.-solid_base)
                assert np.max(np.abs(value[:,2]-expected))<2e-6,'GPU phase interpolation differs'
                rows.append(dict(folded_y_phase=float(phase),void_fractions=fractions.tolist(),
                    max_error=float(np.max(np.abs(fractions-probabilities)))))
            report['independent_phase_checks']=rows
            records=[]
            for seed in (1337,97,98765,31071):
                sample=points(args.fixed_power,seed+157)
                value=probe(gl,program,buffers,sample,1,seed)
                repeated=probe(gl,program,buffers,sample[:1024],1,seed)
                assert np.array_equal(value[:1024],repeated),'Noise is not deterministic'
                moved=sample[:16384].copy(); moved[:,1]+=200.
                shifted=probe(gl,program,buffers,moved,1,seed)
                delta=np.abs(value[:len(moved),0]-shifted[:,0])
                assert np.mean(delta>1e-4)>.99,'Noise is extruded along Y'
                fractions=[]
                for probability in probabilities:
                    limit=thresholds(table,np.abs(np.mod(value[:,1],1.)-.5),probability)
                    fractions.append(float(np.mean(value[:,0]<limit)))
                errors.extend(abs(a-b) for a,b in zip(fractions,probabilities))
                records.append(dict(seed=seed,samples=len(sample),void_fractions=fractions,
                    mean_noise_delta_after_200m_y=float(delta.mean())))
            report['fixed_seed_world_volumes']=records
            sample=points(args.fixed_power,87213); slices=[]
            for t in (-.2,0.,.1,.2,.3,.4,.5,.6,.7,.8,.9,1.,1.2):
                sample[:,3]=t
                value=probe(gl,program,buffers,sample,0,54173)
                target=solid_base+(solid_top-solid_base)*np.clip(t,0.,1.)
                observed=float(np.mean(value[:,3]>=0.));errors.append(abs(observed-target))
                slices.append(dict(height01=float(t),target_solid=float(target),observed_solid=observed))
                print(f'height={t:4.1f}: target={target:.1%}, solid={observed:.4%}',flush=True)
            report['height_slices']=slices
            report['maximum_probability_error']=float(max(errors))
            if not args.allow_uncalibrated:
                assert max(errors)<.015,('Conditional cavity CDF is not calibrated',max(errors))
            gl.check('Boreal conditional cavity gradient')
    if args.output:
        args.output.parent.mkdir(parents=True,exist_ok=True)
        args.output.write_text(json.dumps(report,indent=2)+'\n')

if __name__=='__main__':main()
