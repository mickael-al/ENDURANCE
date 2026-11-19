"""Headless execution of the production fixed biome dispatch family.

All readbacks below are test instrumentation. Production never reads the biome
queues back to the CPU. The adapter keeps the field/mesh fixtures independent of
the C++ scheduler while executing exactly the same GLSL and indirect commands.
"""
from __future__ import annotations

import ctypes as C
from pathlib import Path
import struct

from biome_test_support import packed_biome_cells
from gl_headless import shader_source


SHADERS = Path(__file__).resolve().parents[2] / "Asset/Shader/ProceduralTerrain"
COLUMN_WORDS = 12
COLUMN_BYTES = 48
PACKED_COLUMN_WORDS = 8
PACKED_COLUMN_BYTES = 32
JOB_BYTES = 48
MAX_JOBS = 16
BIOMES = """
PolarIce FrozenTundra Tundra Taiga BorealBog RockyTundra ColdSteppe
BorealForest ColdForest ColdMarsh DryPlain Meadow TemperateForest
TemperateRainforest TemperateMarsh RockDesert DrySavanna Savanna DryJungle
TropicalJungle VolcanicDesert RedDesert AridPlateau BurningJungle
TropicalMangrove Marine
""".split()
QUEUE_COUNT = 2 * len(BIOMES)
QUEUE_BYTES = QUEUE_COUNT * (16 + MAX_JOBS * 4)
DISPATCH_INDIRECT_BUFFER = 0x90EE


def default_uniforms(cells, mode=0, **overrides):
    result = {
        "uCells": ("1ui", cells),
        "uBaseSpacing": ("1f", 1.0),
        "uBaseHeight": ("1f", 0.0),
        "uHeightAmplitude": ("1f", 320.0),
        "uHeightFrequency": ("1f", 0.00015),
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
        kind = result[name][0]
        result[name] = (kind, *(value if isinstance(value, tuple) else (value,)))
    return result


class BatchFieldRunner:
    def __init__(self, gl, directory=SHADERS, materialize_homogeneous=True):
        self.gl = gl
        self.directory = Path(directory)
        self.mapping = gl.buffer(packed_biome_cells())
        # Equivalent sources in temporary copies need not be relinked for every
        # individual test. Edited source is always reloaded and gets a new key.
        cache = getattr(gl, "_terrain_program_cache", None)
        if cache is None:
            cache = gl._terrain_program_cache = {}

        def compile_source(source, label):
            if source not in cache:
                cache[source] = gl.compile([(0x91B9, source)], label)
            return cache[source]

        def compute(relative, defines=""):
            source = shader_source(self.directory / relative)
            if defines:
                version, body = source.split("\n", 1)
                source = version + "\n" + defines + "\n" + body
            return compile_source(source, str(relative))

        self.classify = compute("field_batch_classify.C.glsl")
        self.finalize = compute("field_batch_finalize.C.glsl")
        self.prepare = compute("field_batch_prepare.C.glsl")
        self.jobs_program = compute("field_batch_jobs.C.glsl")
        # Full-grid oracle tests need the analytic base even for chunks whose
        # production mesher can prove empty before density initialization.
        # Dedicated culling tests use materialize_homogeneous=False.
        self.density_program = compute("field_batch_density.C.glsl",
            "#define FIELD_MATERIALIZE_HOMOGENEOUS 1" if materialize_homogeneous else "")
        self.relief_programs = [compute(f"biomes/{name}/relief.C.glsl") for name in BIOMES]
        self.volume_programs = [compute(f"biomes/{name}/volume.C.glsl") for name in BIOMES]
        self.decode = compile_source("#version 450 core\n" +
            shader_source(self.directory / "field_batch_common.glsl") +
            shader_source(self.directory / "field_weights.glsl") + """
layout(local_size_x=64) in;
layout(std430,binding=1) readonly buffer PackedInput { PackedFieldColumn packedColumns[]; };
layout(std430,binding=2) writeonly buffer DecodedOutput { FieldColumn decodedColumns[]; };
void main()
{
    uint job=gl_WorkGroupID.z;
    uint index=gl_GlobalInvocationID.x;
    uint side=uCells+3u;
    if(job>=uJobSlots || (fieldJobs[job].info.y&FIELD_JOB_ACTIVE)==0u || index>=side*side) return;
    fieldSelectJob(job);
    FieldColumn column=fieldUnpackColumn(packedColumns[fieldJobs[job].storage.x*uColumnStride+index]);
    column.detail.w=uFieldMode==0u ? fieldVolumeMask(column) : 0u;
    decodedColumns[job*uColumnStride+index]=column;
}
""", "test-only final column decoding")
        self.programs = [self.classify, self.finalize, self.prepare, self.jobs_program,
                         self.density_program, *self.relief_programs, *self.volume_programs]
        self.programs.append(self.decode)
        if not hasattr(gl, "glDispatchComputeIndirect"):
            gl._fn("glDispatchComputeIndirect", None, C.c_ssize_t)
        self.queues = gl.buffer(QUEUE_BYTES)
        self.jobs = gl.buffer(MAX_JOBS * JOB_BYTES)
        self.last_commands = []
        self.last_work_items = []
        self.last_jobs = []

    def indirect(self, program, queue):
        gl = self.gl
        gl.glUseProgram(program)
        gl.glBindBuffer(DISPATCH_INDIRECT_BUFFER, self.queues)
        gl.glDispatchComputeIndirect(queue * 16)
        gl.glMemoryBarrier(gl.ALL_BARRIER_BITS)

    def run(self, cells=8, origin=(0, 0, 0), lod=0, seed=0, mode=0, **overrides):
        return self.run_batch([dict(origin=origin, lod=lod, seed=seed)],
                              cells=cells, mode=mode, **overrides)[0]

    def run_batch(self, requests, cells=8, mode=0, padding=False, **overrides):
        """Execute one batch; a None slot remains inactive and must be untouched.

        With padding, every slot has a sentinel tail to catch stride mistakes.
        Reusing this runner reuses the queue buffers, including stale entries.
        """
        assert 0 < len(requests) <= MAX_JOBS
        gl = self.gl
        side = cells + 3
        column_stride = side * side + (1 if padding else 0)
        density_stride = side ** 3 + (4 if padding else 0)
        sentinel = struct.pack("<I", 0x7FC00001)
        scratch = gl.buffer(sentinel * (len(requests) * column_stride * COLUMN_WORDS), 1)
        columns = gl.buffer(sentinel * (len(requests) * column_stride * PACKED_COLUMN_WORDS), 2)
        decoded = gl.buffer(sentinel * (len(requests) * column_stride * COLUMN_WORDS))
        density = gl.buffer(sentinel * (len(requests) * density_stride), 0)
        state_stride = 1 + ((side + 7) // 8) ** 2
        state_initial = struct.pack("<4I", 0, 0xffffffff, 0, 0) + sentinel * ((state_stride-1)*4)
        states = gl.buffer(state_initial * len(requests), 3)
        gl.bind(self.mapping, 7)
        gl.bind(self.queues, 5)
        gl.bind(self.jobs, 6)
        descriptors = bytearray(MAX_JOBS * JOB_BYTES)
        for slot, request in enumerate(requests):
            if request is not None:
                origin = request.get("origin", (0, 0, 0))
                struct.pack_into("<4i8I", descriptors, slot * JOB_BYTES, *origin,
                                 request.get("lod", 0), request.get("seed", 0), request.get("flags", 7), 0, 0,
                                 request.get("column_slot", slot), slot, request.get("density_slot", slot), 0)
        gl.write(self.jobs, bytes(descriptors))
        # Do not clear queues: their production builder must overwrite stale
        # sizes, and commands with no participating jobs must dispatch zero work.
        uniforms = default_uniforms(cells, mode, **overrides)
        uniforms.update({"uJobSlots": ("1ui", len(requests)),
                         "uColumnStride": ("1ui", column_stride),
                         "uDensityStride": ("1ui", density_stride),
                         "uStateStride": ("1ui", state_stride)})
        for program in self.programs:
            for name, values in uniforms.items():
                gl.uniform(program, name, *values)
        gl.dispatch(self.classify, (side + 7) // 8, (side + 7) // 8, len(requests))
        gl.dispatch(self.jobs_program, 1)
        for biome, program in enumerate(self.relief_programs):
            self.indirect(program, biome)
        gl.dispatch(self.finalize, (side + 7) // 8, (side + 7) // 8, len(requests))
        gl.bind(columns, 1)
        gl.dispatch(self.prepare, (side + 7) // 8, (side + 7) // 8, len(requests))
        gl.dispatch(self.jobs_program, 1)
        gl.dispatch(self.density_program, (side + 7) // 8, (side + 3) // 4,
                    ((side + 3) // 4) * len(requests))
        for biome, program in enumerate(self.volume_programs):
            self.indirect(program, len(BIOMES) + biome)
        gl.glBindBuffer(DISPATCH_INDIRECT_BUFFER, 0)
        gl.bind(decoded, 2)
        gl.dispatch(self.decode, (side*side + 63) // 64, 1, len(requests))
        density_pool = gl.read(density, len(requests) * density_stride * 4)
        column_pool = gl.read(columns, len(requests) * column_stride * PACKED_COLUMN_BYTES)
        decoded_pool = gl.read(decoded, len(requests) * column_stride * COLUMN_BYTES)
        state_pool = gl.read(states, len(requests) * state_stride * 16)
        queues = gl.read(self.queues, QUEUE_BYTES)
        self.last_commands = list(struct.iter_unpack("<4I", queues[:QUEUE_COUNT * 16]))
        self.last_work_items = struct.unpack(f"<{QUEUE_COUNT * MAX_JOBS}I", queues[QUEUE_COUNT * 16:])
        self.last_jobs = list(struct.iter_unpack("<4i8I", gl.read(self.jobs, len(requests) * JOB_BYTES)))
        self.last_states = [struct.unpack_from("<4I", state_pool, slot*state_stride*16) for slot in range(len(requests))]
        self.last_tiles = [list(struct.iter_unpack("<4I",state_pool[(slot*state_stride+1)*16:(slot+1)*state_stride*16])) for slot in range(len(requests))]
        results = []
        for slot, request in enumerate(requests):
            column_slot = request.get("column_slot", slot) if request else slot
            density_slot = request.get("density_slot", slot) if request else slot
            column_first = column_slot * column_stride * PACKED_COLUMN_BYTES
            density_first = density_slot * density_stride * 4
            if request is None:
                assert density_pool[density_first:density_first+density_stride*4] == sentinel * density_stride
                assert column_pool[column_first:column_first+column_stride*PACKED_COLUMN_BYTES] == sentinel * (column_stride*PACKED_COLUMN_WORDS)
                results.append(None)
                continue
            raw_density = density_pool[density_first:density_first+side**3*4]
            raw_packed_columns = column_pool[column_first:column_first+side*side*PACKED_COLUMN_BYTES]
            decoded_first = slot * column_stride * COLUMN_BYTES
            raw_columns = decoded_pool[decoded_first:decoded_first+side*side*COLUMN_BYTES]
            assert density_pool[density_first+side**3*4:density_first+density_stride*4] == sentinel * (density_stride-side**3)
            assert column_pool[column_first+side*side*PACKED_COLUMN_BYTES:column_first+column_stride*PACKED_COLUMN_BYTES] == sentinel * ((column_stride-side*side)*PACKED_COLUMN_WORDS)
            job_uniforms = dict(uniforms)
            job_uniforms.update({"uOrigin": ("3i", *request.get("origin", (0, 0, 0))),
                                 "uLod": ("1ui", request.get("lod", 0)),
                                 "uSeed": ("1ui", request.get("seed", 0))})
            results.append({
                "density_buffer": density, "column_buffer": columns,
                "density_offset": density_first, "column_offset": column_first,
                "density_raw": raw_density, "columns_raw": raw_columns,
                "columns_packed_raw": raw_packed_columns, "column_bytes": PACKED_COLUMN_BYTES,
                "field_state_buffer": states, "field_state_index": density_slot*state_stride,
                "field_state_offset": density_slot*state_stride*16,
                "field_state": self.last_states[density_slot], "state_stride": state_stride,
                "density": struct.unpack(f"<{side ** 3}f", raw_density),
                "columns": struct.unpack(f"<{side * side * COLUMN_WORDS}f", raw_columns),
                "side": side, "cells": cells, "origin": request.get("origin", (0, 0, 0)),
                "lod": request.get("lod", 0), "uniforms": job_uniforms,
            })
        gl.check("batched field accumulation")
        return results
