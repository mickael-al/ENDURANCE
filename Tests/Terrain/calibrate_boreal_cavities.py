#!/usr/bin/env python3
"""Calibrate BorealForest's composed Perlin-domain/Voronoi cavity threshold.

The helper is executed on OpenGL. A seed-varying Sobol ensemble supplies the
60th percentile; independent points and fixed-seed world volumes validate the
actual production threshold. This script prints suggested GLSL, never edits
the production shader. Finite mountains are measured separately by the biome
integration tests; their ratio can fluctuate around the ensemble target.
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
HELPER = ROOT / 'Asset/Shader/ProceduralTerrain/biomes/BorealForest/cavities.glsl'
SETTINGS = HELPER.with_name('settings.glsl')
SOURCE = r'''
layout(local_size_x=256) in;
layout(std430,binding=0) readonly buffer Input { vec4 points[]; };
layout(std430,binding=1) writeonly buffer Output { vec4 results[]; };
uniform uint uCount;
uniform uint uMode;
uniform uint uSeed;
void main() {
    uint i=gl_GlobalInvocationID.x;
    if(i>=uCount) return;
    uint seed=uMode==0u ? noiseInternalHash(i^uSeed) : uSeed;
    vec3 p=points[i].xyz;
    float n=borealCavityNoise(p,seed);
    float shifted=borealCavityNoise(p+vec3(0.0,200.0,0.0),seed);
    results[i]=vec4(n,BOREAL_CAVITY_THRESHOLD,shifted,
        (n-BOREAL_CAVITY_THRESHOLD)*BOREAL_CAVITY_SCALE);
}
'''


def probe(gl, program, buffers, points, mode, seed):
    inputs, output = buffers
    points = np.asarray(points, dtype='<f4')
    gl.resize(inputs, points.tobytes()); gl.bind(inputs, 0)
    gl.resize(output, bytes(len(points)*16)); gl.bind(output, 1)
    gl.uniform(program, 'uCount', '1ui', len(points))
    gl.uniform(program, 'uMode', '1ui', mode)
    gl.uniform(program, 'uSeed', '1ui', seed)
    gl.dispatch(program, (len(points)+255)//256)
    result = np.frombuffer(gl.read(output,len(points)*16),dtype='<f4').copy().reshape(-1,4)
    assert np.isfinite(result).all(), 'Nonfinite cavity noise'
    return result


def points(power, sample_seed):
    # 512 km in XZ and an altitude range covering added mountains. Irrational
    # offsets avoid concentrating samples on lattice/Perlin zero planes.
    unit = qmc.Sobol(3,scramble=True,seed=sample_seed).random_base2(power)
    result = np.zeros((len(unit),4),dtype='<f4')
    result[:,:3] = (unit-.5)*np.asarray((512000.,12000.,512000.))
    result[:,:3] += np.asarray((17.349,29.713,-11.983))
    return result


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--power',type=int,default=20)
    parser.add_argument('--fixed-power',type=int,default=18)
    parser.add_argument('--output',type=Path)
    parser.add_argument('--allow-uncalibrated',action='store_true',
        help='print a replacement threshold without asserting the current one')
    args=parser.parse_args()
    assert 12<=args.power<=22 and 12<=args.fixed_power<=21
    helper = HELPER.read_text()
    threshold=float(re.search(r'BOREAL_CAVITY_THRESHOLD\s*=\s*([0-9.]+)',helper).group(1))
    report=dict(helper_sha256=hashlib.sha256(HELPER.read_bytes()).hexdigest(),
        settings_sha256=hashlib.sha256(SETTINGS.read_bytes()).hexdigest(),
        calibration_samples=2**args.power,threshold=threshold,target_void_fraction=.60,
        interpretation='Ensemble proportion before cropping to each added mountain; finite regions fluctuate.')
    with GL() as gl:
        report['renderer']=gl.renderer
        program=gl.compile([(0x91B9,'#version 450 core\n'+shader_source(HELPER)+SOURCE)],
            'Boreal Perlin/Voronoi cavity calibration')
        buffers=gl.buffer(0),gl.buffer(0)
        sample=points(args.power,76439)
        values=probe(gl,program,buffers,sample,0,11939)[:,0]
        quantile=float(np.quantile(values,.60))
        report['measured_quantile']=quantile
        print(f'const float BOREAL_CAVITY_THRESHOLD = {quantile:.9f};',flush=True)
        independent=probe(gl,program,buffers,points(args.power,23871),0,84627)
        report['independent_void_fraction']=float(np.mean(independent[:,0]<threshold))
        report['independent_void_at_measured_quantile']=float(np.mean(independent[:,0]<quantile))
        records=[]
        for seed in (1337,97,98765,31071):
            sample=points(args.fixed_power,seed+157)
            value=probe(gl,program,buffers,sample,1,seed)
            repeated=probe(gl,program,buffers,sample[:1024],1,seed)
            assert np.array_equal(value[:1024],repeated), 'Noise is not deterministic'
            delta=np.abs(value[:,0]-value[:,2])
            assert np.mean(delta>1e-4)>.99, 'Cavity noise is extruded along Y'
            row=dict(seed=seed,samples=len(sample),void_fraction=float(np.mean(value[:,0]<threshold)),
                mean_noise_delta_after_200m_y=float(delta.mean()),
                unchanged_y_fraction=float(np.mean(delta<=1e-4)))
            records.append(row)
            print(json.dumps(row),flush=True)
        report['fixed_seed_world_volumes']=records
        errors=[abs(report['independent_void_fraction']-.60)]+[abs(row['void_fraction']-.60) for row in records]
        report['maximum_void_fraction_error']=max(errors)
        if not args.allow_uncalibrated:
            assert max(errors)<.01, ('Cavity threshold is not calibrated to 60%',errors)
        gl.check('Boreal cavity calibration')
    if args.output:
        args.output.parent.mkdir(parents=True,exist_ok=True)
        args.output.write_text(json.dumps(report,indent=2)+'\n')


if __name__=='__main__':
    main()
