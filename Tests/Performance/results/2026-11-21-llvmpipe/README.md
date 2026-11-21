# Measured results — 2026-11-21

These are **software OpenGL measurements, not GTX 1660 FPS predictions**. The execution environment reports `llvmpipe (LLVM 19.1.7, 256 bits)`, Mesa 25.0.7, 9 logical CPUs; host CPU identification is AMD EPYC 9V74. No hardware GPU device is exposed. GPU fixtures ran sequentially, without competing team GPU tests. CPU compiler/review activity can still contribute noise.

Baseline: isolated checkout `b4ca0a1c055f1cb95d9f307cb8a8304364068192` (the full hash in raw reports is authoritative). Candidate: the modified working tree. Every raw report records its inputs, direct C++ source hashes, compiled-library hash, runtime shader aggregate and placement catalogue hash. Final tree/full-world cases use `TreeRenderGpu.cpp` SHA-256 `308e1b6919c26d35af43d0c0d4fc4dfbf8b74f82e747e2f17cd4802e6dec2d00`, including both final pre-write SSBO barriers.

The [machine-readable summary](summary.json) is derived from the raw JSON files beside this document. See [runner instructions](../../README.md) for reproducing on Windows/hardware and for the scope of each fixture.

## Complete final terrain coverage

Actual `Terrain.scene` seed, camera, biome matrices and field parameters; view distance **40,096 m**, production **64³ cells/chunk**, **8 jobs in flight**, default **1,024 MiB mesh budget**. All selected final chunks were retained. The scene selector produces 2,665 chunks with LOD histogram `[256,184,229,256,220,224,1296]`; the baseline/candidate selection hash is identical.

| Result | Baseline | Final candidate |
|---|---:|---:|
| Validated completed chunks | 2,665 / 2,665 | 2,665 / 2,665 |
| Ready / empty | 769 / 1,896 | 769 / 1,896 |
| Terrain vertices | 27,228,894 | 27,228,894 |
| Resident mesh bytes | 326,759,088 | 326,759,088 |
| Generated object instances | 1,913 | 312 |
| Object capture jobs | 209 | 209 |
| Object scratch bytes | 6,722,112 | 1,684,032 |
| Generation, objects enabled, run 1 | 13.861 s | 10.961 s |
| Generation, objects enabled, run 2 | 13.545 s | 11.042 s |
| Median of those two runs | **13.703 s** | **11.001 s** |
| Generation, objects disabled, one run | **11.069 s** | **10.035 s** |

The two objects-enabled runs improve by approximately **19.7%**. That includes the intentional distribution change: fewer, more widely separated trees. The separate objects-disabled pair isolates terrain generation and improves by **9.3%**, with identical terrain vertex counts. A single pair is limited evidence; repeat it on hardware before choosing GPU budgets.

The full objects-enabled count-stage GPU interval falls from a median **4.585 s to 2.281 s**. Column/preparation/volume/emit medians are respectively **1.804 / 3.187 / 2.254 / 1.643 s** before, **1.706 / 3.081 / 2.166 / 1.553 s** after. Both sides build 608 column tiles and 2,665 density fields in 430 field batches. No geometry reduction was used to obtain these terrain results.

Initialization is **outside** the generation times: baseline 3.159 / 3.369 s, candidate 1.034 / 0.614 s for the objects-enabled runs. Cache/driver initialization variability makes this a separate result, not evidence of a fixed startup speedup. Earlier first-use software-driver compilation made a much smaller diagnostic take almost 20 seconds; warm-up state matters.

Full backend coverage is not end-to-end scene readiness. This fixture bypasses the frame-cadenced scene scheduler, does not generate temporary progressive parent chunks, does not load/render all tree assets, and does not run the entire deferred/atmosphere/editor pipeline. Therefore it does **not** reproduce or explain every part of the user's reported 5–30 minute load, nor prove the requested 10-second / 40-FPS complete-game target. It shows that the raw final-coverage GPU backend can complete without exhausting the mesh budget in this environment, and directs attention to streaming cadence, asset loading and simultaneous rendering for the remaining gap.

Raw cases: `before-full40km*.json`, `after-final-full40km*.json`.

## Same tree geometry, final renderer

1,024 instances of the same baked Acacia model, scale 1, identical fixed positions, 320×320 G-buffer, shadows disabled. Two runs per revision, five measured frames per phase after two warm-up frames. The table uses the median of the two per-run mean frame times. It is a **single subsystem**, not full-scene frame time.

| Phase | Baseline | Final candidate | Reduction |
|---|---:|---:|---:|
| Static camera | 286.00 ms | 190.83 ms | 33.3% |
| Moving camera | 338.40 ms | 212.17 ms | 37.3% |
| Unchanged pages republished | 290.21 ms | 201.17 ms | 30.7% |

Static and publication phases emit **2,149,344 primitives** per frame. The moving-camera phase emits 2,149,344–2,149,348, with identical counts between revisions for every corresponding frame. The renderer's independent image comparisons also check appearance. Republish-without-content-change causes **5 transform rebuilds per 5 frames before, 0 after**. The number of render API calls remains five per frame; indirect commands are consolidated across the 16 instance pages. This demonstrates less CPU/driver work while keeping the same submitted tree geometry, but llvmpipe timings are not transferable to GTX 1660 FPS.

Raw final cases: `before-trees1024-{1,2}.json` and `after-final-trees1024-{1,2}.json`. Earlier `after-trees1024-*` files precede the final synchronization barriers and are retained for transparency; they are not used in this table.

## Queue-depth diagnostic

Eight nearest final chunks at production 64³ resolution, objects disabled, three warm samples per configuration. Every run completes 8/8 with 2 ready + 6 empty and 40,719 vertices. These are a bounded mostly-empty workload, not complete coverage.

| In-flight jobs | Baseline median | Candidate median |
|---|---:|---:|
| 3 | 105.79 ms | 108.06 ms |
| 8 | 102.93 ms | 100.40 ms |

There is **no convincing overall optimization gain** in this small fixture relative to its 91–129 ms observed variation. It does not justify a general “8 jobs is X times faster” claim. The separate scheduler tests explain why more outstanding jobs and an opportunistic second poll can improve frame-cadenced throughput. Candidate bounded cases were captured before the final tree-only synchronization barriers; those barriers do not change this objects-disabled terrain path.

## Mixed draw/generation probe

64 nearest chunks at 64³, 8 jobs, object collection enabled, small terrain descriptor-buffer draws interleaved at up to 40 Hz. Both versions produce 540,210 terrain vertices. The natural camera/biomes produce **zero tree instances in this bounded subset**, so it is not a dense-forest generation test.

Warm wall times are 459.84 / 579.94 ms before and 453.43 / 481.13 ms after. The number of interleaved frames varies with runtime (7–9 before, 7–8 after), so these are diagnostic observations rather than an equal-render-work speedup claim. They do not establish a need for an additional full-descriptor-table upload optimization.

## Completion and budget guard

A deliberate candidate diagnostic with a **1 MiB** mesh budget requests 64 chunks. It reports `Terrain vertex memory budget exhausted`, only 13 validated successful completions, `completed_fixture: false`, `complete_scene_coverage: false`, and exits with status **2**. The harness does not confuse an empty/stalled queue or exhausted budget with completed coverage. Raw report: `after-final-budget-failure.json`.

## Validation and remaining limits

The Linux shared runners compile and execute against both the frozen baseline and final sources. The standalone GLFW entry point receives a C++17 syntax check; its Windows CMake target includes the bundled GLFW/GLEW, GLM and `NOMINMAX`. CMake itself and a Windows toolchain are unavailable in this execution environment, so a native MSVC build and measurements on the user's GTX 1660 remain required.

The source snapshot in `source-inputs.json` supplements per-run manifests with local include/shader inputs captured after the final measurements, while the team confirmed those implementation files were stable. It is not a substitute for the eventual integration commit. Runtime scene paths in raw reports are machine-specific; regenerate configurations with `--write-config` on the target machine.
