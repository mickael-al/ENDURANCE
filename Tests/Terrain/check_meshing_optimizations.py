#!/usr/bin/env python3
"""Compare packed extraction output with and without workgroup/edge reuse.

An optional original shader directory also verifies the pre-optimization path.
Inputs are independent synthetic densities, so this test does not depend on the
field generation implementation or its quantization. No timings are asserted.
"""

from __future__ import annotations

import argparse
from collections import Counter
from pathlib import Path
import random
import struct
import sys

from check_gpu import GL, GLUnavailable, MeshRunner, SHADERS, packed_tables


def triangles(mesh):
    words = struct.unpack(f'<{len(mesh["packed_vertices"])//4}I', mesh['packed_vertices'])
    return Counter(tuple(words[i:i+9]) for i in range(0, len(words), 9))


def field(gl, cells, values, phase=0):
    side = cells+3
    legacy, packed = bytearray(), bytearray()
    for z in range(side):
        for x in range(side):
            humidity = ((x*x+7*z+phase)%257)/256
            temperature = ((z*z+13*x+phase*3)%251)/250
            legacy.extend(struct.pack('<8f4I', 0, humidity, temperature, 0, 0, 0, 0, 0, 0, 0, 0, 0))
            packed.extend(struct.pack('<5f3I', 0, humidity, temperature, 0, 0, 0, 0, 0))
    raw = struct.pack(f'<{len(values)}f', *values)
    # Select a nonzero state record. Production binds the complete metadata
    # arena, rather than invalid 16-byte ranges on 256-byte-aligned drivers.
    state = gl.buffer(struct.pack('<12I', *([0]*8), 3, 0, 0, 0))
    return {'cells':cells, 'side':side, 'origin':(0, 0, 0), 'lod':0,
            'density_buffer':gl.buffer(raw), 'column_buffer':gl.buffer(bytes(packed)),
            'density_raw':raw, 'columns_raw':bytes(legacy), 'columns_packed_raw':bytes(packed),
            'field_state_buffer':state, 'field_state_offset':32,
            'uniforms':{'uBaseSpacing':('1f', 1.)}}


def sample_index(cells, point):
    side = cells+3
    x,y,z = (p+1 for p in point)
    return x+y*side+z*side*side


def face_point(face, u, v, cells):
    return ((0,u,v),(cells,v,u),(v,0,u),(u,cells,v),(u,v,0),(v,u,cells))[face]


def check_tables():
    words = struct.unpack('<6400I', packed_tables())
    occurrences = unique = 0
    for offset, stride, cases in ((0,5,256),(1280,10,512)):
        for case in range(cases):
            base = offset+case*stride
            edges = [(words[base+1+i//4]>>((i%4)*8))&255 for i in range(words[base])]
            keys = {}
            for edge in edges:
                a,b = edge>>4,edge&15
                if offset == 0:
                    direction = a^b
                    assert direction in (1,2,4) and a<b
                    key = a//2 if direction==1 else (4+(a&1)+((a>>1)&2) if direction==2 else 8+(a&3))
                elif a<9:
                    assert b-a in (1,3)
                    key = 2*(a//3)+a%3 if b-a==1 else 6+a
                else:
                    assert b-a in (1,2)
                    key = 12+(a-9)//2 if b-a==1 else 14+((a-9)&1)
                assert keys.setdefault(key,edge)==edge, ('Edge key aliases distinct edges',case,key,edge)
            occurrences += len(edges)
            unique += len(keys)
    print(f'PASS 768 table cases: {occurrences} triangle occurrences, {unique} unique cell-edge evaluations')


def check(gl, reference_directory=None, quick=False):
    reference = MeshRunner(gl, reference_directory or SHADERS, optimized=False, edge_reuse=False)
    optimized = MeshRunner(gl, optimized=True, edge_reuse=True)
    comparisons = 0

    def compare(f, mask=0):
        nonlocal comparisons
        before,after = reference.run(f,mask),optimized.run(f,mask)
        assert before['count']==after['count'], ('Count differs',comparisons,mask,before['count'],after['count'])
        assert triangles(before)==triangles(after), ('Packed geometry differs',comparisons,mask)
        comparisons += 1
        return after

    for case in range(0,256,17 if quick else 1):
        values = [-1.]*7**3
        for corner in range(8):
            p = tuple(1+((corner>>a)&1) for a in range(3))
            values[sample_index(4,p)] = 1. if case&(1<<corner) else -1.
        compare(field(gl,4,values,case))
    case_bits = (0,1,2,7,8,3,6,5,4)
    for case in range(0,512,17 if quick else 1):
        face = case%6
        values = [-1.]*7**3
        for sample in range(9):
            p = face_point(face,sample%3,sample//3,4)
            values[sample_index(4,p)] = 1. if case&(1<<case_bits[sample]) else -1.
        compare(field(gl,4,values,case),1<<face)
    generator = random.Random(1337)
    for trial in range(12 if quick else 48):
        cells = (4,8,16)[trial%3]
        values = [generator.uniform(-1,1) for _ in range((cells+3)**3)]
        compare(field(gl,cells,values,trial),generator.randrange(64))
    cells,side = 64,67
    values = [3.25-(y-1) for z in range(side) for y in range(side) for x in range(side)]
    plane = field(gl,cells,values)
    result = compare(plane,63)
    regular = result['group_counts'][:result['regular_groups']]
    assert len(regular)==2048 and sum(c>0 for c in regular)==128
    plane['mesh_field_state']=2
    rejected = optimized.run(plane,63)
    assert rejected['count']==0 and not any(rejected['group_counts'])
    print(f'PASS {comparisons} packed-triangle comparisons, all 6 faces, small-N tiles, random fields, nonzero state offset')
    print('PASS plane: 1,920/2,048 regular groups skip emission; homogeneous state skips all extraction groups')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--reference-shaders',type=Path,help='Original ProceduralTerrain shader directory')
    parser.add_argument('--quick',action='store_true',help='Sample table cases during development')
    args = parser.parse_args()
    check_tables()
    try:
        with GL() as gl:
            print(f'OpenGL {gl.version}; renderer: {gl.renderer}')
            check(gl,args.reference_shaders,args.quick)
    except GLUnavailable as error:
        print(f'SKIP: {error}',file=sys.stderr)
        return 77
    return 0


if __name__=='__main__':
    raise SystemExit(main())
