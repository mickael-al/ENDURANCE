# Engine/tree pipeline optimization — measured second pass

Baseline: `16bb573deb0b5348d4f1f358cc474f6253050da2`, frozen in a separate checkout. Candidate: the working second-pass sources identified by the per-report SHA-256 manifests. Both were built with the **same** instrumented harness, `-O3 -ffast-math`, and executed sequentially on **llvmpipe / LLVM 19.1.7**, not a GTX 1660. No other GPU test or heavy compilation ran during the final measurements.

These results demonstrate saved work and correctness of the bounded production subsystems. They do **not** establish whole-game FPS, the 40-FPS target or complete progressive streaming time. Actual scene timing is available through the new in-engine asynchronous frame profiler; a target-hardware run remains necessary.

## Method and provenance

- `selection-*.json`: actual scene camera, seed, 40,096 m view distance, exact production LOD selection.
- `generation-[12]-*.json`: two alternating warm before/after runs after one full warm-up each; every selected final chunk, 64³ cells, 8 in-flight jobs, object placement enabled. Successful meshes stay resident. The fixture omits temporary parent chunks produced by the scene streaming scheduler.
- `trees-{forced,cached,atmosphere}-[12]-*.json`: two alternating warm pairs, 512 trees across four real Acacia variants, fixed hashed positions/scale, 192×192 G-buffer and four 128² depth layers. Five measured frames per phase, two warm-up frames per phase. This is a controlled multi-model workload, not the density of an entire 40 km forest.
- `trees-shader-cold-*.json`: the same fixture with a fresh engine shader cache directory and `MESA_SHADER_CACHE_DISABLE=true`. This is shader-cold, **not** a cold OS asset/filesystem cache. `trees-warmup-*.json` records the subsequent normal-cache warm-up.
- `summary.json` combines the raw observations without discarding slower runs. Inline test-only GL counters measure API operations; their small CPU cost is present in both builds. Timer intervals include driver/submission gaps and are not isolated shader ALU cost.
- Manifests contain source/header, harness, instrumentation, library, shader, catalogue and selected baked-asset hashes. Builds reject captured inputs changed during compilation; measurements reject changed shaders. The original manifests omitted source-side inline includes; these are explicitly recorded in `additional_include_sha256_at_final_audit` after measurement while sources remained frozen. Future runner builds also include them in the pre-compilation guard. The baseline uses unchanged shared `ThirdParty` and baked assets via symlinks, so its Git `dirty` flag is true despite frozen engine sources. Asset hashes match across the pair.

## Tree rendering and shadow invalidation

Mean elapsed milliseconds per bounded frame, including the final drain of queued work:

| Workload | Phase | Before | After | After / before |
|---|---|---:|---:|---:|
| forced | static | 349.89 | 330.70 | 0.945 |
| forced | camera_motion | 375.36 | 370.17 | 0.986 |
| forced | publication_churn | 398.64 | 382.81 | 0.960 |
| forced | offscreen_page_churn | 377.94 | 401.32 | 1.062 |
| cached | static | 104.77 | 95.37 | 0.910 |
| cached | camera_motion | 336.54 | 330.29 | 0.981 |
| cached | publication_churn | 350.38 | 97.28 | 0.278 |
| cached | offscreen_page_churn | 357.71 | 98.02 | 0.274 |
| atmosphere | static | 120.88 | 106.22 | 0.879 |
| atmosphere | camera_motion | 332.90 | 338.28 | 1.016 |
| atmosphere | publication_churn | 427.76 | 108.71 | 0.254 |
| atmosphere | offscreen_page_churn | 404.62 | 107.59 | 0.266 |

The clear improvement is avoiding invalidation from unchanged or irrelevant tree pages. With the production shadow cache enabled, those phases redraw **four → zero shadow layers per frame** and issue **100 → 12 indirect draw API calls**. A moving camera still invalidates the necessary layers; it was not incorrectly treated as static.

When all four layers are forcibly redrawn, geometry is identical and timings are mixed: one phase is 6.2% slower here. Do not claim a universal shader or frame-time win from the draw-call reduction alone. Stronger deterministic evidence is the operation count:

| Operation, stable frame with four forced shadows | Before | After |
|---|---:|---:|
| Indirect draw API calls | 100 | 60 |
| Indirect commands actually submitted | 100 | 100 |
| State queries | 170 | 102 |
| Texture binds | 150 | 75 |
| Program binds | 15 | 20 |

The extra fixed shader variants trade some program binds for fewer dynamic feature branches. Re-publishing unchanged pages no longer uploads selections or reruns the five culls. Moving the camera still performs culling but avoids the former repeated selection uploads. Full per-frame counts are retained in the raw reports.

Color and all four depth-layer hashes match before/after in every measured phase. Forced-pass primitive counts also match. Cached runs legitimately generate fewer primitives because they reuse the same shadow maps. Separately, `tree-image-comparison.json` records 14 reference views / 1,433,600 pixels with **zero changed pixels and zero channel delta**; it covers geometric/impostor LODs, transitions, orthographic views, fallback and impostor directions.

### Atmospheric integration scope

The atmosphere mode runs the actual production atmosphere, cloud, fog and sky passes using the same camera matrices, light matrices and real tree shadow texture. Ultra ray-step settings are preserved, but target dimensions are deliberately reduced: volumes 32×18×24, visible clouds 64×36, sky/prefilter cubemaps 16, irradiance 4, IBL 8 samples. Wind is zero. This fixture has no terrain draw, deferred lighting/composite, SSAO, sea, postprocessing or editor, and is **not the Ultra scene workload**.

Background environment capture/blending started by camera motion continues into later phases. Consequently the candidate still performs 3/5 then 2/5 volume updates in the publication phases, against 5/5 before. All shadow layers are reused correctly; not every atmospheric update can or should be removed. Initial atmosphere warm/bake time is reported separately.

The real engine also had an additional invalidation issue: the atmosphere hook was registered as a custom renderer but inherited the untracked shadow token despite drawing no shadow geometry. Its new stable empty-content token fixes that engine-level issue. The controlled fixture contains only tree shadow sources, so **no separate measured speed-up is claimed for this additional fix**.

## Complete final-coverage generation

| Metric | Before | After |
|---|---:|---:|
| Selected chunks | 2665 | 2665 |
| Nonempty / empty chunks | 769 / 1,896 | 769 / 1,896 |
| Generated vertices | 27,228,894 | 27,228,894 |
| Resident mesh bytes | 326,759,088 | 326,759,088 |
| Object instances | 312 | 312 |
| Object pool bytes | 53,477,376 | 53,477,376 |
| Object scratch bytes | 1,684,032 | 29,248 |
| GL subdata upload calls | 3643 | 2874 |
| GL subdata upload bytes | 73936 | 49328 |
| Warm generation runs (seconds) | 9.964, 10.667 | 10.731, 10.383 |
| Mean generation time (seconds) | 10.316 | 10.557 |

The second pass demonstrates **no total generation-time improvement on this machine**: the mean is 2.3% higher, with overlapping run-to-run dispersion. Its reliable generation benefit is reduced scratch and driver uploads, while preserving the workload. This scene/camera yields only 312 collected objects; it is not a dense-forest placement stress test.

The 769 removed uploads correspond exactly to the 769 resident chunk descriptors, 32 bytes each. Scratch is reduced by 1,654,784 bytes / 98.3%. The precise new total is `1,152 + 8 × (8 + 256 + 2 × 256) × 4 + 102 × 8 × 4 = 29,248` bytes, including the catalogue prefix and type counters. The persistent instance pool is unchanged. `object-compaction-equivalence.json` separately verifies 5,124 compacted positions across sparse/full/invalid/empty cases, up to 1,024 sites / 255 types.

## Initialization costs and limits

Shader-cold tree initialization was **92.29 → 94.31 ms** in the single controlled pair. Warm initialization medians were **7.60 → 13.57 ms** across the measured tree cases; more fixed programs have a real one-time cost. Asset load/upload wall times varied **255–312 ms before** and **242–316 ms after** in those warm cases, so no asset-loading speed-up is claimed. The cold pair's asset times were 704/369 ms; filesystem state and asynchronous polling make that unsuitable as an isolated loading comparison.

Generation initialization is outside its `wall_ms`: 564/655 ms before versus 993/568 ms after in the two warm runs. Driver work may also be deferred into warm-up/first dispatches. Retain initialization, asset loading and generation together when checking perceived startup on the target machine.

All final fixture executions completed with no OpenGL error; the exact LOD-selection hash is unchanged. Native Windows/MSVC, the complete engine/editor renderer and the GTX 1660 were not available here. The saved tests and in-engine profiler make those remaining validations reproducible; software milliseconds above must not be converted into predicted game FPS.
