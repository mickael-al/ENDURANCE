#!/usr/bin/env python3
"""Execute the terrain's actual compute shaders in a headless OpenGL context."""

from __future__ import annotations

import argparse
from collections import Counter
import math
import os
from pathlib import Path
import random
import struct
import subprocess
import sys
import tempfile

from gl_headless import GL, GLUnavailable
from biome_test_support import packed_biome_cells


ROOT = Path(__file__).resolve().parents[2]
SHADERS = ROOT / "Asset/Shader/ProceduralTerrain"
COLUMN_WORDS = 12
COLUMN_BYTES = COLUMN_WORDS * 4


class FieldRunner:
    def __init__(self, gl: GL, directory=SHADERS):
        self.gl = gl
        self.mapping = gl.buffer(packed_biome_cells())
        self.columns_program = gl.compute(directory / "field_columns.C.glsl")
        self.density_program = gl.compute(directory / "field_density.C.glsl")

    def run(self, cells=8, origin=(0, 0, 0), lod=0, seed=0, mode=0, **overrides):
        gl = self.gl
        side = cells + 3
        # NaNs reveal missing halo/boundary writes, including partial workgroups.
        nan = struct.pack("<I", 0x7FC00001)
        columns = gl.buffer(nan * (side * side * COLUMN_WORDS), 1)
        density = gl.buffer(nan * (side ** 3), 0)
        gl.bind(self.mapping, 7)
        uniforms = {
            "uCells": ("1ui", cells),
            "uOrigin": ("3i", *origin),
            "uLod": ("1ui", lod),
            "uBaseSpacing": ("1f", 1.0),
            "uSeed": ("1ui", seed),
            "uBaseHeight": ("1f", 0.0),
            "uHeightAmplitude": ("1f", 320.0),
            "uHeightFrequency": ("1f", 0.00015),
            "uClimateFrequency": ("1f", 0.0005),
            "uBandThickness": ("1f", 64.0),
            "uBandVariation": ("1f", 16.0),
            "uBandOffset": ("1f", 0.0),
            "uBandOffsetVariation": ("1f", 0.0),
            "uDetailScale": ("1f", 0.35),
            "uSeaLevel": ("1f", 0.0),
            "uSeaBlendWidth": ("1f", 64.0),
            "uBiomeOverride": ("1i", -1),
            "uFieldMode": ("1ui", mode),
            "uSphereCenter": ("3f", 4.0, 4.0, 4.0),
            "uSphereRadius": ("1f", 3.0),
        }
        for name, value in overrides.items():
            suffix = uniforms[name][0]
            uniforms[name] = (suffix, *(value if isinstance(value, tuple) else (value,)))
        for program in (self.columns_program, self.density_program):
            for name, values in uniforms.items():
                gl.uniform(program, name, *values)
        gl.dispatch_extent(self.columns_program, side, side)
        gl.dispatch_extent(self.density_program, side, side, side)
        raw_density = gl.read(density, side ** 3 * 4)
        raw_columns = gl.read(columns, side * side * COLUMN_BYTES)
        gl.check("field generation")
        return {
            "density_buffer": density, "column_buffer": columns,
            "density_raw": raw_density, "columns_raw": raw_columns,
            "density": struct.unpack(f"<{side ** 3}f", raw_density),
            "columns": struct.unpack(f"<{side * side * COLUMN_WORDS}f", raw_columns),
            "side": side, "cells": cells, "origin": origin, "lod": lod,
            "uniforms": uniforms,
        }


def lattice_values(result):
    side, origin, stride = result["side"], result["origin"], 1 << result["lod"]
    raw = result["density_raw"]
    values = {}
    for z in range(side):
        for y in range(side):
            for x in range(side):
                position = tuple(origin[i] + (value - 1) * stride for i, value in enumerate((x, y, z)))
                index = x + y * side + z * side * side
                values[position] = raw[index * 4 : index * 4 + 4]
    return values


def check_fields(gl: GL):
    fields = FieldRunner(gl)
    # An exact affine field tests the complete halo as well as dispatch tails.
    plane = fields.run(cells=8, origin=(-8, -4, -12), mode=1, uBaseHeight=0.25)
    side = plane["side"]
    for z in range(side):
        for y in range(side):
            for x in range(side):
                index = x + y * side + z * side * side
                expected = 0.25 - (-4 + y - 1)
                assert abs(plane["density"][index] - expected) < 1e-6, ("plane", (x, y, z), plane["density"][index], expected)
    sphere = fields.run(mode=2)
    for position, raw in lattice_values(sphere).items():
        value = struct.unpack("<f", raw)[0]
        expected = 3.0 - math.sqrt(sum((coordinate - 4.0) ** 2 for coordinate in position))
        assert abs(value - expected) < 2e-5, ("sphere", position, value, expected)
    first = fields.run(origin=(-8, -8, -8), seed=123456789)
    again = fields.run(origin=(-8, -8, -8), seed=123456789)
    assert first["density_raw"] == again["density_raw"], "Repeated seed changed the density field"
    assert first["columns_raw"] == again["columns_raw"], "Repeated seed changed the columns"
    different = fields.run(origin=(-8, -8, -8), seed=987654321)
    assert first["columns_raw"] != different["columns_raw"], "Seed has no effect on terrain columns"
    coarse = fields.run(origin=(-16, -16, -16), lod=1, seed=123456789)
    fine_values, coarse_values = lattice_values(first), lattice_values(coarse)
    shared = fine_values.keys() & coarse_values.keys()
    assert len(shared) >= 125
    assert all(fine_values[p] == coarse_values[p] for p in shared), "Shared LOD lattice points disagree"
    neighbor = fields.run(origin=(0, -8, -8), seed=123456789)
    neighbor_values = lattice_values(neighbor)
    shared_neighbor = fine_values.keys() & neighbor_values.keys()
    assert shared_neighbor and all(fine_values[p] == neighbor_values[p] for p in shared_neighbor), "Adjacent chunks disagree"
    offset = fields.run(origin=(-8, -8, -8), seed=123456789, uBandOffset=-96.0)
    assert first["columns"][0::COLUMN_WORDS] == offset["columns"][0::COLUMN_WORDS], "Band offset changed the base height map"
    assert first["columns"][5::COLUMN_WORDS] != offset["columns"][5::COLUMN_WORDS], "Band offset was not recorded"
    full = fields.run(cells=64, origin=(0, -32, 0), mode=1)
    flat = fields.run(origin=(-8, -4, -12), uBaseHeight=0.25,
                      uHeightAmplitude=0, uBandThickness=0, uBandVariation=0, uDetailScale=0)
    # With all amplitudes disabled, the terrain path must reduce to a plane.
    # Its saturated density need not match the analytic plane's magnitude.
    for position, raw in lattice_values(flat).items():
        value = struct.unpack("<f", raw)[0]
        assert (value > 0) == (position[1] < 0.25) and value != 0, ("flat field sign", position, value)
    for result in (plane, sphere, first, again, different, coarse, neighbor, offset, full, flat):
        assert all(math.isfinite(value) for value in result["density"]), "Non-finite or unwritten density value"
        assert finite_columns(result["columns"]), "Non-finite or unwritten column value"
    print(f"PASS fields: plane, sphere, seed, {len(shared)} shared LOD samples, adjacent halo, band offset and 64³ cells")
    return fields


def finite_columns(values):
    # The final uvec4 contains bit-packed biome IDs, not floats. Its NaN-like
    # bit patterns are valid metadata; all eight numeric floats must be finite.
    return all(math.isfinite(value) for index, value in enumerate(values)
               if index % COLUMN_WORDS < 8)


def check_render_stages(gl):
    from check_render import check_render
    check_render(gl, SHADERS)


def packed_tables():
    # Export the actual C++ data, rather than a second independently maintained
    # copy. A temporary binary and table dump never enter the repository.
    with tempfile.TemporaryDirectory(prefix="terrain-tables-") as temporary:
        temporary = Path(temporary)
        source = temporary / "tables.cpp"
        source.write_text('#include "ProceduralTransvoxelTables.hpp"\n#include <cstdio>\n'
                          'int main(){auto t=ProceduralTerrain::Transvoxel::makePackedTables();'
                          'return std::fwrite(t.data(),sizeof(t[0]),t.size(),stdout)==t.size()?0:1;}\n')
        binary = temporary / "tables"
        subprocess.run([os.environ.get("CXX", "c++"), "-std=c++17", "-O2", "-I",
                        str(ROOT / "ANTAGONIZ/Header/Miscellaneous"), str(source), "-o", str(binary)], check=True)
        result = subprocess.run([str(binary)], check=True, stdout=subprocess.PIPE).stdout
        assert len(result) == 6400 * 4
        return result


def decode_normal(word):
    x, y = struct.unpack("<hh", struct.pack("<I", word))
    x, y = max(x / 32767.0, -1.0), max(y / 32767.0, -1.0)
    z = 1.0 - abs(x) - abs(y)
    if z < 0:
        x, y = (1.0 - abs(y)) * (1 if x >= 0 else -1), (1.0 - abs(x)) * (1 if y >= 0 else -1)
    length = math.sqrt(x*x+y*y+z*z)
    return (x/length, y/length, z/length)


class MeshRunner:
    def __init__(self, gl):
        self.gl = gl
        self.regular = [gl.compute(SHADERS / "meshing.C.glsl", f"#define EMIT_PASS {emit}") for emit in (0, 1)]
        self.transition = [gl.compute(SHADERS / "transition.C.glsl", f"#define EMIT_PASS {emit}") for emit in (0, 1)]
        self.tables = gl.buffer(packed_tables())

    def run(self, field, mask=0, capacity=None):
        gl, cells = self.gl, field["cells"]
        gl.bind(field["density_buffer"], 0)
        gl.bind(field["column_buffer"], 1)
        gl.bind(self.tables, 2)
        counter = gl.buffer(bytes(16), 3)
        programs = self.regular + self.transition
        for program in programs:
            span = cells * (1 << field["lod"])
            gl.uniform(program, "uChunkParity", "3ui", *(int(origin // span) & 1 for origin in field["origin"]))
            for name, value in (("uCells", cells), ("uTransitionMask", mask)):
                gl.uniform(program, name, "1ui", value)
        gl.dispatch_extent(self.regular[0], cells, cells, cells)
        gl.dispatch(self.transition[0], ((cells // 2)**2 + 63)//64, 6)
        total, cursor, overflow, _ = struct.unpack("<4I", gl.read(counter, 16))
        assert cursor == overflow == 0, ("count changed emit state", total, cursor, overflow)
        if capacity is None:
            capacity = total
        # Prefix and suffix sentinels detect both base-offset mistakes and writes
        # past capacity. Binding the whole buffer intentionally allows detection.
        prefix, suffix = 5, 7
        sentinel = struct.pack("<I", 0xCDCDCDCD)
        vertex_buffer = gl.buffer(sentinel * ((prefix + capacity + suffix) * 3), 4)
        for program in programs:
            for name, value in (("uVertexBase", prefix), ("uVertexCapacity", capacity)):
                gl.uniform(program, name, "1ui", value)
        gl.dispatch_extent(self.regular[1], cells, cells, cells)
        gl.dispatch(self.transition[1], ((cells // 2)**2 + 63)//64, 6)
        total2, emitted, overflow, _ = struct.unpack("<4I", gl.read(counter, 16))
        raw = gl.read(vertex_buffer, (prefix + capacity + suffix) * 12)
        assert total2 == total == emitted, ("count/emit mismatch", total, total2, emitted)
        assert raw[:prefix*12] == sentinel * (prefix*3), "Vertex prefix overwritten"
        assert raw[(prefix+capacity)*12:] == sentinel * (suffix*3), "Vertex suffix overwritten"
        if total > capacity:
            assert overflow != 0, "Capacity overrun was not reported"
        else:
            assert overflow == 0, "Unexpected overflow"
        # Matches completion acceptance: failed emission must never be drawable.
        command=(emitted,1,prefix,2) if emitted==total and not overflow else (0,1,prefix,2)
        vertices, normals = [], []
        extent = cells * (1 << field["lod"]) * field["uniforms"]["uBaseSpacing"][1]
        for offset in range(prefix*12, (prefix + min(total,capacity))*12, 12):
            a, b, normal = struct.unpack_from("<III", raw, offset)
            packed = a & 65535, a >> 16, b & 65535
            assert all(value <= 65534 for value in packed), ("Invalid quantized vertex", packed)
            vertices.append(tuple(field["origin"][axis] + packed[axis] / 65534.0 * extent for axis in range(3)))
            normals.append(decode_normal(normal))
        gl.check("count/emit")
        return {"vertices": vertices, "normals": normals, "count": total, "overflow": overflow, "command": command, "field": field}


def triangle_edges(meshes):
    # Common rational lattice: all fixtures use base spacing 1 and at most LOD1.
    # Quantization denominator 65534 makes matching seams exactly representable.
    edges = Counter()
    degenerate = 0
    for mesh in meshes:
        points = [tuple(round(v * 65534 / mesh["field"]["cells"]) for v in point) for point in mesh["vertices"]]
        for index in range(0,len(points),3):
            a,b,c = points[index:index+3]
            if len({a,b,c}) < 3:
                degenerate += 1
                continue
            for edge in ((a,b),(b,c),(c,a)):
                edges[tuple(sorted(edge))] += 1
    return edges, degenerate


def consistent_orientation(meshes):
    directed=Counter()
    for mesh in meshes:
        points=[tuple(round(v*65534/mesh["field"]["cells"]) for v in point) for point in mesh["vertices"]]
        for i in range(0,len(points),3):
            a,b,c=points[i:i+3]
            if len({a,b,c})<3:
                continue
            for u,v in ((a,b),(b,c),(c,a)):
                directed[tuple(sorted((u,v)))]+=1 if u<v else -1
    assert all(count==0 for count in directed.values()),"Adjacent triangles disagree about winding"


def outward_winding(mesh):
    checked=0
    for i in range(0,len(mesh["vertices"]),3):
        a,b,c=mesh["vertices"][i:i+3]
        ab=[b[k]-a[k] for k in range(3)]
        ac=[c[k]-a[k] for k in range(3)]
        cross=(ab[1]*ac[2]-ab[2]*ac[1],ab[2]*ac[0]-ab[0]*ac[2],ab[0]*ac[1]-ab[1]*ac[0])
        if sum(v*v for v in cross)<1e-12:
            continue
        normal=[sum(n[k] for n in mesh["normals"][i:i+3]) for k in range(3)]
        # Boundary caps can be tangent to the interpolated surface normal; they
        # close the quantized coarse/fine contours without facing inward.
        dot=sum(cross[k]*normal[k] for k in range(3))
        assert dot>=-1e-6*math.sqrt(sum(v*v for v in cross)),("Inward triangle winding",a,b,c,dot)
        checked+=1
    return checked


def check_meshes(gl, fields):
    meshes = MeshRunner(gl)
    plane = meshes.run(fields.run(mode=1, uBaseHeight=3.25))
    assert plane["count"] == 8*8*6, ("Plane triangle count", plane["count"])
    quantum = 8/65534
    assert all(abs(vertex[1]-3.25) <= quantum for vertex in plane["vertices"])
    assert all(normal[1] > .999 for normal in plane["normals"])
    outward_winding(plane)
    sphere_field = fields.run(mode=2, uSphereRadius=2.75)
    sphere = meshes.run(sphere_field)
    edges, _ = triangle_edges([sphere])
    assert edges and set(edges.values()) == {2}, ("Regular sphere is not closed", Counter(edges.values()))
    assert all(sum((point[a]-4)*normal[a] for a in range(3)) > 2.5 for point,normal in zip(sphere["vertices"],sphere["normals"]))
    outward_winding(sphere)
    assert meshes.run(fields.run(mode=1,uBaseHeight=-10))["count"] == 0
    overflow = meshes.run(sphere_field, capacity=7)
    assert overflow["overflow"]
    transition_overflow=meshes.run(fields.run(mode=2,uSphereCenter=(0.,4.,4.),uSphereRadius=2.75),mask=1,capacity=7)
    assert transition_overflow["overflow"]
    assert meshes.run(fields.run(cells=4,mode=1,uBaseHeight=1.25))["count"]==4*4*6
    full = meshes.run(fields.run(cells=64,mode=1,uBaseHeight=3.25))
    assert full["count"] == 64*64*6
    print("PASS regular mesh: plane, closed sphere, normals, empty field, 64³ cells and guarded overflow")
    seam_fixtures=[(8.,8.,5.5), (7.35,8.72,5.43),
                   (8.349772125987096,7.261767491708243,5.739814888044384)]
    for face in range(6):
        axis, sign = face//2, (-1 if face%2==0 else 1)
        transverse = [a for a in range(3) if a!=axis]
        for transverse_a,transverse_b,radius in seam_fixtures:
            center = [8.,8.,8.]
            center[axis] = 0. if sign < 0 else 16.
            center[transverse[0]],center[transverse[1]]=transverse_a,transverse_b
            options=dict(mode=2,uSphereCenter=tuple(center),uSphereRadius=radius)
            union = [meshes.run(fields.run(lod=1,**options))]
            for i in range(4):
                origin=[0,0,0]
                origin[axis] = -8 if sign<0 else 16
                for bit,a in enumerate(transverse):
                    origin[a]=((i>>bit)&1)*8
                union.append(meshes.run(fields.run(origin=tuple(origin),**options), mask=1 << (face ^ 1)))
            edges, degenerate = triangle_edges(union)
            distribution = Counter(edges.values())
            assert edges and set(edges.values()) == {2}, ("LOD seam is not closed", face, center, distribution, "degenerate", degenerate)
            # The bridge normal need not equal the original field gradient.
            # Its orientation must instead continue the closed regular mesh.
            consistent_orientation(union)
        print(f"PASS Transvoxel face {face}: three closed spheres across one coarse/four fine chunks, including quantization regression")
    # One refined octant meets seven coarse octants. Fine chunks can own three
    # transition faces at once; the sphere crosses their common corner.
    union=[]
    options=dict(mode=2,uSphereCenter=(.2,.3,.4),uSphereRadius=5.43)
    for i in range(7):
        origin=tuple(0 if (i>>a)&1 else -16 for a in range(3))
        union.append(meshes.run(fields.run(lod=1,origin=origin,**options)))
    for i in range(8):
        origin=tuple(8*((i>>a)&1) for a in range(3))
        mask=sum(1<<(a*2) for a in range(3) if origin[a]==0)
        union.append(meshes.run(fields.run(origin=origin,**options),mask))
    edges,degenerate=triangle_edges(union)
    assert edges and set(edges.values())=={2},("Multi-face transition corner",Counter(edges.values()),degenerate)
    consistent_orientation(union)
    print("PASS Transvoxel corner: closed sphere across 15 chunks with three simultaneous transition faces")
    for height in (0.,-24.,24.):
        options=dict(cells=16,seed=1337,uBaseHeight=height)
        survey=fields.run(lod=1,**options)
        heights=sorted(survey["columns"][0::COLUMN_WORDS])
        origin_y=32*math.floor(heights[len(heights)//2]/32)
        union=[meshes.run(fields.run(lod=1,origin=(0,origin_y,0),**options))]
        for y,z in ((0,0),(16,0),(0,16),(16,16)):
            union.append(meshes.run(fields.run(origin=(32,origin_y+y,z),**options),mask=1))
        edges,_=triangle_edges(union)
        assert edges,"Real terrain fixture did not produce a surface"
        for edge,count in edges.items():
            if count==2:
                continue
            assert count==1,("Nonmanifold real terrain edge",edge,count)
            positions=[[coordinate*16/65534 for coordinate in point] for point in edge]
            outer=any(all(abs(point[axis]-bound)<1e-7 for point in positions)
                      for axis,bounds in enumerate(((0,48),(origin_y,origin_y+32),(0,32))) for bound in bounds)
            assert outer,("Real terrain contains an internal open edge",height,positions)
    print("PASS terrain noise: seed 1337, three height offsets; every open edge lies on the outer test boundary")
    # Chaotic finite densities exercise ambiguous sign configurations far more
    # densely than a smooth sphere. A negative outer shell closes the surface.
    generator=random.Random(0)
    samples={}
    def noisy_field(origin,lod):
        field=fields.run(cells=16,origin=origin,lod=lod,mode=1)
        side=field["side"]
        values=[]
        for z in range(side):
            for y in range(side):
                for x in range(side):
                    point=tuple(origin[a]+(q-1)*(1<<lod) for a,q in enumerate((x,y,z)))
                    if point[0]<=-16 or point[0]>=32 or point[1]<=0 or point[1]>=32 or point[2]<=0 or point[2]>=32:
                        values.append(-1.)
                    else:
                        if point not in samples:
                            samples[point]=generator.uniform(-1,1)
                        values.append(samples[point])
        gl.write(field["density_buffer"],struct.pack(f"<{len(values)}f",*values))
        return field
    union=[meshes.run(noisy_field((0,0,0),1))]
    for y,z in ((0,0),(16,0),(0,16),(16,16)):
        union.append(meshes.run(noisy_field((-16,y,z),0),mask=2))
    edges,degenerate=triangle_edges(union)
    assert edges and set(edges.values())=={2},("Ambiguous density cases left a crack",Counter(edges.values()),degenerate)
    consistent_orientation(union)
    print(f"PASS ambiguous density stress: {len(edges)} edges all paired with consistent winding across LOD")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--render", action="store_true", help="Also rasterize the GLSL460 G-buffer/shadow stages and check their coordinate decoding")
    parser.add_argument("--fields-only", action="store_true", help="Run only field generation checks")
    args = parser.parse_args()
    try:
        with GL() as gl:
            print(f"OpenGL {gl.version}; renderer: {gl.renderer}")
            fields = check_fields(gl)
            if not args.fields_only:
                check_meshes(gl, fields)
            if args.render:
                check_render_stages(gl)
    except GLUnavailable as error:
        print(f"SKIP: {error}", file=sys.stderr)
        return 77
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
