#!/usr/bin/env python3
"""Opt-in ALU versus generated 3D value/gradient-noise prototype benchmark.

This file owns a test-only prototype, not the production material shader.
Both paths evaluate the same seeded periodic smooth value-noise field; the
texture path approximates it with RGBA16F samples (value, d/dx, d/dy, d/dz).
Quintic interpolation is the default, matching the new terrain material
shader. The optional cubic mode reproduces the old microdetail interpolant.
Generation, allocation, compilation, readback, and query waits are outside
fragment timing. No timing assertion: Mesa software results do not predict
hardware performance, and the periodic ALU baseline has wrap work absent from
the previous nonperiodic production microdetail. Cache quality is measured,
not assumed identical. No source assets or third-party Python packages needed.
"""
from __future__ import annotations

import argparse
import ctypes as C
import math
from pathlib import Path
import random
import statistics
import struct
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'Tests/Atmosphere'))
from gl_support import (AtmosphereGL, FULLSCREEN_VERTEX, RGBA16F, RGBA32F,
                        TEXTURE_2D, TEXTURE_3D)

PERIOD = 16
NOISE = r"""
#ifndef TERRAIN_NOISE_QUINTIC
#define TERRAIN_NOISE_QUINTIC 1
#endif
const int CELL_PERIOD = 16;
uint hashCell(ivec3 cell) {
    // A power-of-two period gives defined wrapping for negative coordinates.
    uvec3 bits = uvec3(cell) & uvec3(CELL_PERIOD-1);
    uint h=(bits.x*0x8da6b343u)^(bits.y*0xd8163841u)
          ^(bits.z*0xcb1ab31fu)^42u;
    h^=h>>16u; h*=0x7feb352du; h^=h>>15u; h*=0x846ca68bu;
    return h^(h>>16u);
}
float cornerValue(ivec3 p) { return float(hashCell(p)>>8u)/16777215.0; }
vec4 field(vec3 p) {
    ivec3 c=ivec3(floor(p)); vec3 f=fract(p);
#if TERRAIN_NOISE_QUINTIC
    vec3 u=f*f*f*(f*(f*6.0-15.0)+10.0);
    vec3 du=30.0*f*f*(f-1.0)*(f-1.0);
#else
    vec3 u=f*f*(3.0-2.0*f), du=6.0*f*(1.0-f);
#endif
    float a=cornerValue(c), b=cornerValue(c+ivec3(1,0,0));
    float c1=cornerValue(c+ivec3(0,1,0)), d=cornerValue(c+ivec3(1,1,0));
    float e=cornerValue(c+ivec3(0,0,1)), f1=cornerValue(c+ivec3(1,0,1));
    float g=cornerValue(c+ivec3(0,1,1)), h=cornerValue(c+ivec3(1,1,1));
    float value=mix(mix(mix(a,b,u.x),mix(c1,d,u.x),u.y),
                    mix(mix(e,f1,u.x),mix(g,h,u.x),u.y),u.z);
    vec3 derivative=vec3(
      mix(mix(b-a,d-c1,u.y),mix(f1-e,h-g,u.y),u.z),
      mix(mix(c1-a,d-b,u.x),mix(g-e,h-f1,u.x),u.z),
      mix(mix(e-a,f1-b,u.x),mix(g-c1,h-d,u.x),u.y))*du;
    return vec4(value,derivative);
}
"""

GENERATOR = '#version 450 core\n' + NOISE + r"""
layout(local_size_x=4,local_size_y=4,local_size_z=4) in;
layout(rgba16f,binding=0) writeonly uniform image3D outputNoise;
void main() {
    ivec3 pixel=ivec3(gl_GlobalInvocationID), size=imageSize(outputNoise);
    if(any(greaterThanEqual(pixel,size))) return;
    vec3 p=(vec3(pixel)+0.5)*float(CELL_PERIOD)/vec3(size);
    imageStore(outputNoise,pixel,field(p));
}
"""

PROBE = '#version 450 core\n' + NOISE + r"""
layout(local_size_x=64) in;
layout(binding=0) uniform sampler3D noiseTexture;
layout(std430,binding=2) readonly buffer Points { vec4 points[]; };
layout(std430,binding=3) writeonly buffer Result { vec4 values[]; };
uniform uint pointCount;
uniform float probeLod;
void main() {
    uint i=gl_GlobalInvocationID.x;
    if(i>=pointCount) return;
    vec3 p=points[i].xyz/float(CELL_PERIOD);
    values[i*3u]=field(points[i].xyz);
    values[i*3u+1u]=textureLod(noiseTexture,p,probeLod);
    values[i*3u+2u]=textureLod(noiseTexture,p+vec3(1,0,0),probeLod);
}
"""


def fragment(cached):
    evaluate = ('return textureGrad(noiseTexture,p/16.0,dx/16.0,dy/16.0);'
                if cached else 'return field(p);')
    return '#version 450 core\n' + NOISE + r"""
layout(binding=0) uniform sampler3D noiseTexture;
uniform float worldExtent;
uniform int sampleCount;
uniform vec2 resolution;
layout(location=0) out vec4 result;
vec4 sampleField(vec3 p,vec3 dx,vec3 dy) {
""" + evaluate + r"""
}
void main() {
    vec2 uv=gl_FragCoord.xy/resolution;
    vec3 p=vec3(uv.x,0.21*uv.x+0.37*uv.y,uv.y)*worldExtent;
    vec3 dx=dFdx(p), dy=dFdy(p);
    // Shared macro and meso fields, regardless of material/biome count.
    vec4 value=sampleField(p/300.0,dx/300.0,dy/300.0);
    if(sampleCount>1)
        value+=0.35*sampleField(p/23.0+vec3(5.13,1.67,0.71),dx/23.0,dy/23.0);
    float footprint=max(length(dx),length(dy))*2.0;
    float detail=1.0-smoothstep(0.20,0.80,footprint);
    // Gradients are computed above, outside potentially divergent control flow.
    if(sampleCount>2 && detail>0.0001)
        value+=detail*0.1*sampleField(p*2.0+vec3(2.31,4.75,0.12),dx*2.0,dy*2.0);
    result=value;
}
"""


class Benchmark:
    def __init__(self, gl, resolution, cache_sizes, interpolation):
        self.gl, self.resolution = gl, resolution
        for name, signature in {
            'glGenQueries': (None, C.c_int, C.POINTER(C.c_uint)),
            'glDeleteQueries': (None, C.c_int, C.POINTER(C.c_uint)),
            'glBeginQuery': (None, C.c_uint, C.c_uint),
            'glEndQuery': (None, C.c_uint),
            'glGetQueryObjectui64v': (None, C.c_uint, C.c_uint, C.POINTER(C.c_ulonglong)),
        }.items():
            gl._fn(name, *signature)
        self.query = C.c_uint()
        gl.glGenQueries(1, C.byref(self.query))
        def configure(source):
            return source.replace('#version 450 core\n', '#version 450 core\n'
                                  f'#define TERRAIN_NOISE_QUINTIC {int(interpolation == "quintic")}\n', 1)
        self.generator = gl.compile([(0x91B9, configure(GENERATOR))], 'terrain noise prototype generator')
        self.probe = gl.compile([(0x91B9, configure(PROBE))], 'terrain noise prototype quality probe')
        self.programs = {
            'alu': gl.compile([(0x8B31, FULLSCREEN_VERTEX), (0x8B30, configure(fragment(False)))], 'analytic periodic noise'),
            'cache': gl.compile([(0x8B31, FULLSCREEN_VERTEX), (0x8B30, configure(fragment(True)))], 'cached periodic noise'),
        }
        self.caches = {}
        for size in cache_sizes:
            levels = size.bit_length()
            texture = gl.texture((size,)*3, target=TEXTURE_3D,
                                 internal=RGBA16F, levels=levels)
            gl.glBindTexture(TEXTURE_3D, texture)
            for axis in (0x2802, 0x2803, 0x8072):
                gl.glTexParameteri(TEXTURE_3D, axis, 0x2901)  # repeat
            gl.image(0, texture, internal=RGBA16F, layered=True)
            gl.dispatch_extent(self.generator, size, size, size)
            gl.glGenerateMipmap(TEXTURE_3D)
            gl.glMemoryBarrier(gl.ALL_BARRIER_BITS)
            self.caches[size] = texture
        self.output = gl.texture((resolution,)*2, internal=RGBA32F)
        fbo, vao = C.c_uint(), C.c_uint()
        gl.glGenFramebuffers(1, C.byref(fbo)); gl.framebuffers.append(fbo.value)
        gl.glGenVertexArrays(1, C.byref(vao)); gl.arrays.append(vao.value)
        self.fbo, self.vao = fbo.value, vao.value
        gl.glBindFramebuffer(0x8D40, self.fbo)
        gl.glFramebufferTexture2D(0x8D40, 0x8CE0, TEXTURE_2D, self.output, 0)
        gl.glDrawBuffers(1, (C.c_uint*1)(0x8CE0))
        assert gl.glCheckFramebufferStatus(0x8D40) == 0x8CD5
        gl.glBindFramebuffer(0x8D40, 0)
        gl.glDisable(0x0B71); gl.glDisable(0x0BE2)

    def quality(self, size):
        gl, texture = self.gl, self.caches[size]
        rng = random.Random(5821)
        points = [(rng.uniform(-24, 24), rng.uniform(-24, 24), rng.uniform(-24, 24), 0)
                  for _ in range(4096)]
        # Points straddling the period seam with independent y/z, plus corners.
        seam_start = len(points)
        for _ in range(64):
            y, z = rng.uniform(-16, 16), rng.uniform(-16, 16)
            points.extend(((-1e-5, y, z, 0), (1e-5, y, z, 0)))
        points.extend([(0, 0, 0, 0), (16, 16, 16, 0), (-16, -16, -16, 0)])
        flat = [x for point in points for x in point]
        gl.buffer(struct.pack(f'<{len(flat)}f', *flat), 2)
        results = gl.buffer(len(points)*3*16, 3)
        gl.sampler(0, texture, TEXTURE_3D)
        gl.uniform(self.probe, 'pointCount', '1ui', len(points))
        gl.uniform(self.probe, 'probeLod', '1f', 0)
        gl.dispatch_extent(self.probe, len(points))
        values = struct.unpack(f'<{len(points)*12}f', gl.read(results, len(points)*48))
        assert all(math.isfinite(v) for v in values)
        value_errors, derivative_errors, repeat_errors = [], [], []
        for i in range(len(points)):
            exact, cached, repeat = (values[12*i+4*j:12*i+4*j+4] for j in range(3))
            value_errors.append(abs(exact[0]-cached[0]))
            derivative_errors.extend(abs(exact[j]-cached[j]) for j in range(1, 4))
            repeat_errors.extend(abs(cached[j]-repeat[j]) for j in range(4))
        seam_error = max(abs(values[12*i+4+j]-values[12*(i+1)+4+j])
                         for i in range(seam_start, seam_start+128, 2) for j in range(4))
        assert max(repeat_errors) < 1e-5, 'Period wrapping mismatch'
        assert seam_error < 1e-3, 'Discontinuous period seam'
        # Check generated mip levels, including signed derivatives, are written
        # and finite. The final mip must converge to a constant neutral field.
        mip_value_variance, mip_derivative_rms = [], []
        for level in range(size.bit_length()):
            mip = gl.texels(texture, (size,)*3, target=TEXTURE_3D, level=level)
            assert all(math.isfinite(v) for v in mip), 'Unwritten/nonfinite mip texel'
            scalar = mip[0::4]
            mean = statistics.fmean(scalar)
            mip_value_variance.append(statistics.fmean((v-mean)**2 for v in scalar))
            gradients = [v for i, v in enumerate(mip) if i % 4]
            mip_derivative_rms.append(math.sqrt(statistics.fmean(v*v for v in gradients)))
        assert mip_value_variance[-1] == 0
        assert mip_derivative_rms[-1] < 0.001, 'Mean periodic derivative must be near zero'
        assert all(a+1e-6 >= b for a, b in zip(mip_value_variance, mip_value_variance[1:])), 'Mip variance increased'
        rmse = lambda items: math.sqrt(statistics.fmean(v*v for v in items))
        memory = sum(max(1, size >> i)**3 for i in range(size.bit_length()))*8
        print(f'cache {size}^3: {memory/1048576:.3f} MiB, value RMSE/max='
              f'{rmse(value_errors):.6f}/{max(value_errors):.6f}, gradient RMSE/max='
              f'{rmse(derivative_errors):.6f}/{max(derivative_errors):.6f}, '
              f'period max={max(repeat_errors):.8f}, seam delta={seam_error:.8f}', flush=True)
        print('  derivative RMS per mip: '+', '.join(f'{x:.5f}' for x in mip_derivative_rms), flush=True)

    def draw(self, mode, cache_size, sample_count, world_extent):
        gl = self.gl
        program = self.programs[mode]
        gl.sampler(0, self.caches[cache_size], TEXTURE_3D)
        gl.uniform(program, 'resolution', '2f', self.resolution, self.resolution)
        gl.uniform(program, 'sampleCount', '1i', sample_count)
        gl.uniform(program, 'worldExtent', '1f', world_extent)
        gl.glBindFramebuffer(0x8D40, self.fbo)
        gl.glViewport(0, 0, self.resolution, self.resolution)
        gl.glBindVertexArray(self.vao)
        gl.glUseProgram(program)
        gl.glDrawArrays(0x0004, 0, 3)
        gl.glBindFramebuffer(0x8D40, 0)

    def elapsed(self, *args):
        gl = self.gl
        gl.glBeginQuery(0x88BF, self.query.value)
        self.draw(*args)
        gl.glEndQuery(0x88BF)
        elapsed = C.c_ulonglong()
        gl.glGetQueryObjectui64v(self.query.value, 0x8866, C.byref(elapsed))
        return elapsed.value/1e6

    def close(self):
        self.gl.glDeleteQueries(1, C.byref(self.query))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--size', type=int, default=512, help='square fullscreen target')
    parser.add_argument('--cache-sizes', type=int, nargs='+', default=[64, 128])
    parser.add_argument('--samples', type=int, default=9)
    parser.add_argument('--warmup', type=int, default=3)
    parser.add_argument('--interpolation', choices=('quintic', 'cubic'), default='quintic')
    parser.add_argument('--quality-only', action='store_true')
    args = parser.parse_args()
    if not (args.size > 0 and args.samples > 0 and args.warmup >= 0
            and all(s >= 32 and s & (s-1) == 0 for s in args.cache_sizes)):
        parser.error('Positive target/samples and power-of-two caches >=32 required')
    with AtmosphereGL() as gl:
        print(f'OpenGL {gl.version}; renderer: {gl.renderer}', flush=True)
        print('Test-only periodic cache approximation. Timings are not a correctness gate or hardware FPS forecast.', flush=True)
        print(f'Interpolation: {args.interpolation}', flush=True)
        fixture = Benchmark(gl, args.size, args.cache_sizes, args.interpolation)
        try:
            for cache_size in args.cache_sizes:
                fixture.quality(cache_size)
            if args.quality_only:
                return
            print('size scene cache samples alu_ms cache_ms cache/alu', flush=True)
            for scene, sample_count, extent in (('one', 1, 64.), ('near', 3, 64.), ('far', 3, 4096.)):
                for cache_size in args.cache_sizes:
                    for _ in range(args.warmup):
                        for mode in ('alu', 'cache'):
                            fixture.draw(mode, cache_size, sample_count, extent)
                    gl.glFinish()
                    timings = {'alu': [], 'cache': []}
                    for i in range(args.samples):
                        # Alternate ordering to reduce a systematic warm/cache bias.
                        for mode in (('alu', 'cache') if i % 2 == 0 else ('cache', 'alu')):
                            timings[mode].append(fixture.elapsed(mode, cache_size, sample_count, extent))
                    a, t = (statistics.median(timings[m]) for m in ('alu', 'cache'))
                    print(f'{args.size} {scene} {cache_size} {args.samples} {a:.4f} {t:.4f} {t/a:.3f}', flush=True)
                    result = gl.texels(fixture.output, (args.size,)*2)
                    assert all(math.isfinite(v) for v in result), 'Nonfinite fragment output'
            gl.check('terrain noise benchmark')
        finally:
            fixture.close()


if __name__ == '__main__':
    main()
