# Engine and tree pipeline optimization — second pass

Reference: `16bb573deb0b5348d4f1f358cc474f6253050da2`, already containing the first terrain/tree optimization pass. This pass keeps scene settings, placement density, biome generation and visible LOD thresholds unchanged. Target hardware remains a GTX 1660, 6 GB, with `viewDistance = 40096`; target-device FPS has not been measured in this environment.

## Work removed from the frame

- Tree wood and foliage geometry from LOD0/LOD1 share two vertex/index allocations. The existing five GPU commands are submitted in **three indirect draw API calls per model/pass**, rather than five. Index offsets and base vertices preserve each mesh; impostors retain a separate draw.
- Tree visibility work shares the CPU-selected pages between shadow-cache queries and actual rendering. Identical publication data does not re-upload page metadata or rerun culling. Page allocation identities distinguish recycled storage from unchanged data.
- Each shadow layer tracks its actual selected terrain meshes and tree pages, resource lifetimes, and LOD/fade inputs. A changed projection still invalidates its depth map. Unknown/animated providers remain conservative; allocation failure makes the affected tree pass untracked until it recovers.
- `EnduranceAtmosphereComponent` is only a preparation hook and emits no depth geometry. Its formerly unknown shadow token forced every cascade to redraw. It now advertises stable empty content, with a distinct token per component lifetime. Actual atmosphere, cloud and light changes remain handled by their existing rendering paths.
- Ordinary material bindings use two GL API calls for five nonzero 2D textures instead of ten, preserving the final active unit. Missing textures retain the conservative binding path because bulk binding a zero name clears other texture targets too.
- Object/shadow uniform locations are cached by pipeline lifetime/revision. Empty material groups no longer trigger unnecessary setup or insertion into the instance map. No unrestricted global GL-state cache was introduced: custom render callbacks can still change state under the existing restoration contract.
- Tiny camera/misc buffers replace their complete storage to permit driver renaming instead of overwriting data still being consumed. Their sizes remain 208 and 24 bytes. The misc projection flag now occupies four initialized bytes as required by GLSL std430, rather than one C++ bool plus padding.

## Placement and memory traffic

At 102 prototype types, 256 candidate sites and eight jobs, temporary object compaction storage falls from **1,684,032 to 29,248 bytes**. Output capacity scales with candidate sites instead of sites multiplied by prototype types. A workgroup histogram/prefix/scatter preserves the packed position/type results; the previous per-capture count clear is unnecessary. The permanent 51 MiB position pool is unchanged.

Terrain descriptor uploads use persistently mapped coherent storage: write one 32-byte descriptor only for a free/new slot. Existing retirement fences prevent reuse while prior GPU commands can still read a slot. Mapping failure falls back to ordinary buffer uploads. Additional counters expose writes, bytes and upload calls. Publication coverage reuses contiguous storage and avoids rebuilding unchanged object lists.

## Shader specialization

The targeted debug and stable rendering modes become compile-time defines. Parameters that vary by fragment, visibility result, weather or continuous editor slider remain runtime data.

| System | Specialized modes | Bound |
| --- | --- | --- |
| Terrain | LOD/material debug, fixed biome, disabled microdetail | 8 lazily compiled render programs; one normally used |
| Trees | Wood/foliage/impostor, perspective/parallel impostor, transition setting | 8 graphics programs and 2 culling programs; 3 graphics programs used per pass |
| Atmosphere | Physical atmosphere, local fog, clouds, god rays; diffuse/GGX IBL filtering | 40 retained programs, including active programs and frozen IBL capture programs |
| Bloom | Stable downsampling/tone-mapping modes | Two variants per affected pass |

Atmosphere keys include only features consumed by each program. An unfinished IBL bake keeps its frozen inputs and shader variants. Atomic reload can temporarily hold up to 15 newly linked programs in addition to the retained cache. Failed compilation preserves a usable previous state and reports an error. Terrain continuous palette/detail parameter edits do not compile new variants. Shader binary cache keys include the defines.

Shared material programs retain the generic atmosphere feature fallback; atmosphere/IBL readiness is runtime state. The legacy standalone fog mode (bypassed when atmosphere is active) and the terrain backend's diagnostic `uFieldMode` remain uniforms. This pass does not claim every static choice in every engine shader has been specialized. Active shader scans found no remaining debug-uniform branches.

## In-game measurements

The terrain inspector's **Mesurer les saccades** control now records both existing CPU phases and an asynchronous GPU timeline: preparation, opaque rendering, shadows, atmosphere, lighting, transparency and postprocessing. The GPU ring has eight samples and never waits for unavailable query results; a full ring drops a sample instead of growing or blocking. Disabled measurement issues no GPU profiler calls. Reset/disable mid-frame cannot publish a partial sample.

GPU intervals can include idle time waiting for CPU submission. HUD/presentation and the camera phase preceding the first timestamp are excluded. These are frame timeline intervals, not isolated shader arithmetic timings. Use the actual scene at the intended resolution to verify the total **25 ms** budget corresponding to 40 FPS.

## Before/after results

Raw data, scope, build/source fingerprints and repeated samples are in
[`Tests/Performance/results/2026-11-21-engine-pass2`](../Tests/Performance/results/2026-11-21-engine-pass2).
The table below uses the same llvmpipe fixture before and after, with 512 trees,
four models, four depth layers, and deliberately reduced target dimensions.

| Measurement | Before | After |
| --- | ---: | ---: |
| Indirect draw API calls, forced stable frame | 100 | 60 |
| GL state queries, forced stable frame | 170 | 102 |
| Shadow layers redrawn for unchanged/offscreen publications | 4 per frame | 0 per frame |
| Unchanged publication, cache + bounded atmosphere | 427.76 ms/frame | 108.71 ms/frame |
| Offscreen publication, cache + bounded atmosphere | 404.62 ms/frame | 107.59 ms/frame |
| Full final-coverage backend generation, warm mean | 10.316 s | 10.557 s |
| Descriptor upload calls in that generation | 769 | 0 |
| Temporary object scratch | 1,684,032 B | 29,248 B |

All tree color/depth hashes match. Forced-shadow timings vary by phase from
0.945 to 1.062 times baseline; moving-camera and mixed-biome shader results do
not establish a universal speedup. The large publication gains come from
avoiding unnecessary shadow work. Atmosphere integration can still execute
passes while an IBL transition is active.

Both full generation runs produce 2,665 completed chunks (769 ready and 1,896
empty), 27,228,894 vertices, 312 instances and 326,759,088 resident mesh bytes.
There is **no demonstrated generation-only speedup**: the two before runs are
9.964/10.667 seconds and the two after runs 10.731/10.383 seconds. This fixture
does not include the full interactive rendering workload or temporary parents
from the streaming scheduler. Extra variants also do not establish a startup
win: with shader caches disabled for this fixture, tree initialization is
92.3 versus 94.3 ms, with disk assets already warm. Warm tree initialization medians are 7.60 versus 13.57 ms; the extra programs have a small one-time cost.

## Validation and measurement limits

Production C++/GLSL fixtures run on Mesa llvmpipe in this environment. They establish correctness, operation counts and regression timings; software-renderer timings are not predictions for the GTX 1660. A full Windows/MSVC editor build and interactive visual assessment still require the target machine.

Checks cover queued camera readers, uniform cache invalidation, texture binding state, tree indexed geometry and image equivalence, projection variants, shadow layer invalidation, allocation failure recovery, deterministic placement, all 16 atmosphere feature combinations, frozen IBL snapshots, bloom output, shader compile failures, and bounded variant caches. Atmosphere specialization is numerically identical in all 16 feature combinations and diffuse/GGX filter checks; Bloom passes 15 HDR image comparisons. The direct old/new compaction oracle checks 24 cases and 5,124 identical packed positions, including 255 prototype types.

The tree image comparison against `16bb573` is pixel-identical across 14 captures (1,433,600 pixels): LOD0/1/3, both transition bands, orthographic projection, instances, fallback and six impostor directions. This is a bounded fixture comparison, not a claim that every possible game view has been inspected.

Across 914 historical terrain shader cases, position and color remain bit-identical. PBR components differ by at most 2.39e-7; the largest normal component difference is below 1.72e-4 (maximum angular difference 0.01585848 degrees), occurring at extreme valid authoring settings after compiler specialization. The test uses an explicit 2e-4 normal-only tolerance and keeps 2e-6 for other outputs.

The standalone benchmark and exact commands are documented in `Tests/Performance/README.md`. It separates forced-shadow work, cache-aware work, atmosphere integration, and terrain generation. Reduced atmospheric texture dimensions used in that fixture must not be confused with the full Ultra scene. Source/build fingerprints accompany the reports.
