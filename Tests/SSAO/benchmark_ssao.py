#!/usr/bin/env python3
"""Measure complete SSAO GPU work with fixed inputs and aligned kernel data.

This is an opt-in benchmark, never a timing assertion in the correctness suite.
The baseline preserves the former inline algorithm (including its defects),
but supplies 64 valid, identically aligned samples to both paths. The production
path is also inline, full-resolution and 64 samples. This compares their AO
work in otherwise identical fragment passes, not complete game frame times.
An optional rejected filtering candidate includes raw, filter and final read;
it is preserved under Tests only, not used by the renderer.
Texture allocation, shader compilation and blocking query readback are outside
the measured interval. Software-renderer results do not predict hardware FPS.
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

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(ROOT / "Tests/Atmosphere"))
from gl_support import (AtmosphereGL, FULLSCREEN_VERTEX, RG32F,
                        R16F, TEXTURE_2D, shader_source)

CAMERA = """
layout(std430,binding=0) readonly buffer Camera {
    vec3 camPos; mat4 view; mat4 invView; mat4 proj;
} ubc;
layout(binding=0) uniform sampler2D gPosition;
layout(binding=1) uniform sampler2D gNormal;
layout(binding=2) uniform sampler2D texNoise;
layout(std430,binding=4) readonly buffer Kernel { vec3 samples[]; } ubssao;
in vec2 TexCoords;
#define v_UV TexCoords
layout(location=0) out float result;
"""


def projection(aspect):
    f = 1.0 / math.tan(math.radians(75) / 2)
    near, far = .1, 50000.
    return (f/aspect, 0, 0, 0, 0, f, 0, 0, 0, 0,
            -(far+near)/(far-near), -1, 0, 0, -2*far*near/(far-near), 0)


def samples():
    """Known deterministic valid kernel, shared by both measured paths."""
    random_source = random.Random(4421)
    result = []
    for i in range(64):
        xyz = [random_source.uniform(-1, 1), random_source.uniform(-1, 1),
               random_source.random()]
        scale = random_source.random() * (.1 + .9*(i/64)**2)
        length = math.sqrt(sum(v*v for v in xyz))
        result.extend(v*scale/length for v in xyz)
        result.append(0.)
    return struct.pack('<256f', *result)


class Benchmark:
    def __init__(self, gl, size, filtered_candidate=False):
        self.gl, self.size = gl, size
        self.filtered_candidate = filtered_candidate
        for name, signature in {
            'glGenQueries': (None, C.c_int, C.POINTER(C.c_uint)),
            'glDeleteQueries': (None, C.c_int, C.POINTER(C.c_uint)),
            'glBeginQuery': (None, C.c_uint, C.c_uint),
            'glEndQuery': (None, C.c_uint),
            'glGetQueryObjectui64v': (None, C.c_uint, C.c_uint, C.POINTER(C.c_ulonglong)),
            'glFinish': (None,),
        }.items():
            gl._fn(name, *signature)
        self.query = C.c_uint()
        gl.glGenQueries(1, C.byref(self.query))
        self.position = gl.texture(size, internal=RG32F, linear=False)
        # Match the actual G-buffer (RGB16F), rather than timing a wider test
        # texture that would exaggerate normal bandwidth in the filter.
        self.normal = gl.texture(size, internal=0x881B, linear=False, value=(0, 0, 1, 1))
        self.noise = gl.texture((4, 4), internal=RG32F, linear=False)
        self.raw = gl.texture(size, internal=R16F, linear=False) if filtered_candidate else 0
        self.filtered = gl.texture(size, internal=R16F, linear=False) if filtered_candidate else 0
        self.output = gl.texture(size, internal=R16F, linear=False)
        for texture in (self.position, self.normal, self.noise):
            gl.glBindTexture(TEXTURE_2D, texture)
            gl.glTexParameteri(TEXTURE_2D, 0x2802, 0x2901)
            gl.glTexParameteri(TEXTURE_2D, 0x2803, 0x2901)
        directions = []
        random_source = random.Random(419)
        for _ in range(16):
            angle = random_source.random()*math.tau
            directions.extend((math.cos(angle), math.sin(angle)))
        self.upload(self.noise, (4, 4), directions)
        identity = (1.,0,0,0, 0,1.,0,0, 0,0,1.,0, 0,0,0,1.)
        self.camera = gl.buffer(struct.pack('<52f', 0, 0, 0, 0,
            *identity, *identity, *projection(size[0]/size[1])), 0)
        self.kernel = gl.buffer(samples(), 4)
        helper = shader_source(ROOT/'Asset/Shader/Geometry/gbuffer_position.glsl')
        legacy = (HERE/'legacy_ssao.glsl').read_text()
        fragment = '#version 450 core\n'+helper+CAMERA+legacy+"""
void main() {
    vec4 position=geGBufferReadPosition(gPosition,ivec2(gl_FragCoord.xy),ubc.proj);
    if(position.a == -2.0 || position.a > 0.0) { result=1.0; return; }
    result=SSAO(position.xyz,texelFetch(gNormal,ivec2(gl_FragCoord.xy),0).xyz);
}
"""
        self.old = gl.compile([(0x8B31, FULLSCREEN_VERTEX), (0x8B30, fragment)], 'old inline SSAO timing')
        declarations = CAMERA.replace('vec3 samples[]', 'vec4 samples[]').replace(
            'binding=2) uniform sampler2D texNoise', 'binding=5) uniform sampler2D texNoise')
        evaluate = shader_source(ROOT/'Asset/Shader/SSAO/evaluate.glsl')
        fragment = '#version 450 core\n'+helper+declarations+evaluate+"""
void main() {
    ivec2 pixel=ivec2(gl_FragCoord.xy);
    vec4 position=geGBufferReadPosition(gPosition,pixel,ubc.proj);
    if(position.a == -2.0 || position.a > 0.0) { result=1.0; return; }
    vec3 normal=mat3(ubc.view)*texelFetch(gNormal,pixel,0).xyz;
    result=geSsaoEvaluate(position.xyz,normal,pixel);
}
"""
        self.current = gl.compile([(0x8B31, FULLSCREEN_VERTEX), (0x8B30, fragment)], 'corrected inline SSAO timing')
        if filtered_candidate:
            directory = HERE/'filtered_candidate'
            self.occlusion = gl.compute(directory/'occlusion.C.glsl')
            self.filter = gl.compute(directory/'filter.C.glsl')
            final = """#version 450 core
layout(binding=5) uniform sampler2D ao;
layout(location=0) out float result;
void main() { result=texelFetch(ao,ivec2(gl_FragCoord.xy),0).r; }
"""
            self.final = gl.compile([(0x8B31, FULLSCREEN_VERTEX), (0x8B30, final)], 'final AO sample timing')
        framebuffer, vao = C.c_uint(), C.c_uint()
        gl.glGenFramebuffers(1, C.byref(framebuffer)); gl.framebuffers.append(framebuffer.value)
        gl.glGenVertexArrays(1, C.byref(vao)); gl.arrays.append(vao.value)
        self.framebuffer, self.vao = framebuffer.value, vao.value
        gl.glBindFramebuffer(0x8D40, self.framebuffer)
        gl.glFramebufferTexture2D(0x8D40, 0x8CE0, TEXTURE_2D, self.output, 0)
        gl.glDrawBuffers(1, (C.c_uint*1)(0x8CE0))
        assert gl.glCheckFramebufferStatus(0x8D40) == 0x8CD5
        gl.glBindFramebuffer(0x8D40, 0)
        gl.glDisable(0x0B71); gl.glDisable(0x0BE2)

    def upload(self, texture, size, values):
        gl = self.gl
        payload = struct.pack(f'<{len(values)}f', *values)
        gl.glBindTexture(TEXTURE_2D, texture)
        gl.glTexImage2D(TEXTURE_2D, 0, RG32F, *size, 0, 0x8227, 0x1406, C.c_char_p(payload))

    def scene(self, kind):
        w, h = self.size
        values = []
        for y in range(h):
            for x in range(w):
                selected = kind
                if kind == 'mixed':
                    selected = 'sky' if y >= h//2 else ('far' if x < w//2 else 'near')
                if selected == 'sky': z, tag = 0., -2.
                elif selected == 'far': z, tag = -10000., 0.
                else:
                    z = -2. + (.35 if (x//max(w//8, 1)+y//max(h//8, 1)) % 2 else 0)
                    tag = 0.
                values.extend((z, tag))
        self.upload(self.position, self.size, values)
        self.gl.bind(self.camera, 0); self.gl.bind(self.kernel, 4)
        self.gl.sampler(0, self.position); self.gl.sampler(1, self.normal)

    def draw(self, program):
        gl = self.gl
        gl.glBindFramebuffer(0x8D40, self.framebuffer)
        gl.glViewport(0, 0, *self.size)
        gl.glBindVertexArray(self.vao)
        gl.glUseProgram(program)
        gl.glDrawArrays(0x0004, 0, 3)
        gl.glBindFramebuffer(0x8D40, 0)

    def run(self, mode):
        gl = self.gl
        gl.sampler(2, self.noise)
        if mode == 'old':
            self.draw(self.old)
            return
        if mode == 'current':
            gl.sampler(5, self.noise)
            self.draw(self.current)
            return
        assert self.filtered_candidate, 'Enable --filtered-candidate to measure its stages'
        if mode == 'read':
            gl.sampler(5, self.filtered)
            self.draw(self.final)
            return
        if mode == 'filter':
            self.filter_pass()
            gl.sampler(5, self.filtered)
            self.draw(self.final)
            return
        gl.image(0, self.raw, internal=R16F)
        gl.glUseProgram(self.occlusion)
        gl.glDispatchCompute((self.size[0]+7)//8, (self.size[1]+7)//8, 1)
        gl.glMemoryBarrier(0x28)
        if mode == 'raw':
            gl.sampler(5, self.raw)
            self.draw(self.final)
            return
        self.filter_pass()
        gl.sampler(5, self.filtered)
        self.draw(self.final)

    def filter_pass(self):
        gl = self.gl
        gl.sampler(2, self.raw)
        gl.image(0, self.filtered, internal=R16F)
        gl.glUseProgram(self.filter)
        gl.glDispatchCompute((self.size[0]+7)//8, (self.size[1]+7)//8, 1)
        gl.glMemoryBarrier(0x28)

    def time(self, mode):
        gl = self.gl
        gl.glBeginQuery(0x88BF, self.query.value)
        self.run(mode)
        gl.glEndQuery(0x88BF)
        elapsed = C.c_ulonglong()
        gl.glGetQueryObjectui64v(self.query.value, 0x8866, C.byref(elapsed))
        return elapsed.value / 1e6

    def close(self):
        self.gl.glDeleteQueries(1, C.byref(self.query))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sizes', type=int, nargs='+', default=[128, 256])
    parser.add_argument('--samples', type=int, default=9)
    parser.add_argument('--warmup', type=int, default=3)
    parser.add_argument('--filtered-candidate', action='store_true', help='Also time the rejected two-pass filtered prototype')
    parser.add_argument('--stages', action='store_true', help='Also measure each compute stage with a consuming draw')
    args = parser.parse_args()
    if args.stages and not args.filtered_candidate:
        parser.error('--stages requires --filtered-candidate')
    assert args.samples > 0 and args.warmup >= 0 and all(size > 0 for size in args.sizes)
    with AtmosphereGL() as gl:
        print(f'OpenGL {gl.version}; renderer: {gl.renderer}', flush=True)
        print('GPU elapsed milliseconds, median. Old/current both inline, full resolution, 64 aligned samples.', flush=True)
        if args.filtered_candidate:
            print('Filtered candidate is test-only and includes raw+filter+finalread; it is NOT production.', flush=True)
        print('No universal speed or hardware-FPS claim; timing is not a correctness gate.', flush=True)
        print('size scene old_ms current_ms current_over_old'+(' candidate_ms' if args.filtered_candidate else ''), flush=True)
        for edge in args.sizes:
            fixture = Benchmark(gl, (edge, edge), args.filtered_candidate)
            try:
                for kind in ('near', 'far', 'sky', 'mixed'):
                    fixture.scene(kind)
                    modes = ('old', 'current', 'candidate') if args.filtered_candidate else ('old', 'current')
                    for _ in range(args.warmup):
                        for mode in modes: fixture.run(mode)
                    gl.glFinish()
                    timings = {mode: [] for mode in modes}
                    for iteration in range(args.samples):
                        for mode in (modes if iteration % 2 == 0 else tuple(reversed(modes))):
                            timings[mode].append(fixture.time(mode))
                    old, current = (statistics.median(timings[key]) for key in ('old', 'current'))
                    candidate = f' {statistics.median(timings["candidate"]):.6f}' if args.filtered_candidate else ''
                    print(f'{edge}x{edge} {kind:5s} {old:.6f} {current:.6f} {current/max(old,1e-9):.3f}'+candidate, flush=True)
                    if args.stages and args.filtered_candidate:
                        fixture.run('candidate'); gl.glFinish()
                        stages = [(mode, statistics.median(fixture.time(mode) for _ in range(args.samples)))
                                  for mode in ('raw', 'filter', 'read')]
                        # llvmpipe can defer compute work until its consuming
                        # draw. A compute-only query may misleadingly return
                        # 1ns; both compute measurements retain that draw.
                        print('  stage plus consuming draw (not additive): '+', '.join(f'{mode}={value:.6f}ms' for mode,value in stages), flush=True)
                    values = gl.texels(fixture.output, fixture.size, channels=1)
                    assert all(math.isfinite(v) for v in values), ('Nonfinite benchmark output', kind)
                    gl.check('complete SSAO benchmark')
            finally: fixture.close()


if __name__ == '__main__': main()
