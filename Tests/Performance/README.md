# ENDURANCE performance fixtures

This runner measures the **production terrain GPU backend, tree renderer, shadow cache and optional atmosphere passes**, not a replacement algorithm. It reads the actual `Terrain.scene` settings, seed and camera. The separate `selection` fixture measures the real LOD selector without creating an OpenGL context.

It deliberately does **not** report whole-game FPS. Terrain scheduling, terrain materials, deferred lighting, SSAO, atmosphere, sea, physics and the editor are not all present in this harness. The generation fixture directly submits the selected final coverage; it does not include temporary parent meshes produced by progressive scene streaming. Use the in-game generation telemetry and a GPU profiler to confirm the final 10-second / 40-FPS goals on the target GTX 1660.

## Linux/EGL

Requirements: Python 3, a C++17 compiler, GLEW, OpenGL/EGL development/runtime libraries and the dependencies used by `Tests/Terrain/gl_headless.py`. The Python script compiles once when the requested runner library does not exist. Rebuild explicitly after changing C++ sources:

```sh
python Tests/Performance/run.py --library /tmp/endurance-perf.so --compile-only
python Tests/Performance/run.py --library /tmp/endurance-perf.so --scenario selection --output selection.json
python Tests/Performance/run.py --library /tmp/endurance-perf.so --scenario generation --cells 64 --max-requests 32 --jobs 3 --output terrain.json
python Tests/Performance/run.py --library /tmp/endurance-perf.so --scenario generation --cells 64 --max-requests 32 --jobs 8 --objects --render-during-generation --output mixed.json
python Tests/Performance/run.py --library /tmp/endurance-perf.so --scenario trees --instances 1024 --models 1 --frames 30 --output trees.json
```

`--engine-root` builds against an isolated baseline checkout, including that checkout's shaders and baked assets. Use a different `--library` path for each revision. The runner writes a build manifest beside the library, with source/header, instrumentation and binary hashes, and rejects source changes during compilation. Runtime shader changes during a measurement also invalidate the report. Runtime options and checkout identity are also included in each JSON report. Runtime-loaded shaders/assets can change independently of the compiled library: freeze the checkout while measuring.

## Windows / target hardware

The standalone CMake target uses the repository's bundled GLFW/GLEW and avoids the editor, Qt and Assimp dependencies. From the repository root, using a Visual Studio C++17 toolchain:

```powershell
cmake -S Tests/Performance -B build/performance -A x64
cmake --build build/performance --config Release
python Tests/Performance/run.py --scenario generation --cells 64 --max-requests 0 --jobs 3 --objects --write-config build/performance/generation.json
build/performance/Release/endurance_performance.exe build/performance/generation.json build/performance/generation-report.json
```

`--write-config` needs only Python's standard library; it does not compile or initialize EGL. The JSON contains absolute shader/model paths, so generate it on the machine where it will run. For a Ninja/single-configuration build, select `-DCMAKE_BUILD_TYPE=Release` and run the executable directly under the build directory.

Generate separate configurations for `--jobs 3`, `--jobs 8`, and optionally 4/6/12. Keep all other options identical. The standalone target is prepared for MSVC (including `NOMINMAX` and the GLM include directory); a Windows compile/run still needs to be performed on the target machine.

A useful target-hardware sequence is:

1. Run a cold initialization once and preserve its report.
2. Run the identical configuration three times with driver/shader caches warm.
3. Compare medians **and the slowest run**, allocated memory, identical coverage, identical nonempty/empty counts and identical mesh vertex counts.
4. Repeat with object collection, then with `--render-during-generation`.
5. Measure trees with the same model/instance/resolution/shadow options.
6. Confirm the selected configuration in the actual Ultra scene: total generation time, camera travel, steady frame time and shadow/atmosphere cost. A 40-FPS target means a total frame budget of 25 ms, not 25 ms for trees alone.

Do not run competing GPU tests at the same time. Keep resolution, power mode and driver constant. Report cache state rather than mixing a cold baseline with a warm candidate.

## What each result means

### Generation

- Default `--max-requests 32` is a **bounded subset**, sorted by distance from the scene camera. `--max-requests 0` requests every selected final chunk.
- Production sampling is `--cells 64`. Smaller values preserve the chunk's physical extent but reduce voxel resolution. Such diagnostics always set `same_sampling_as_scene: false` and can never set `complete_scene_coverage: true`.
- `completed_fixture` means every requested chunk produced a `Ready` or `Empty` completion. Failure, timeout or exhausted resident mesh budget is not completion. The process returns a nonzero status for an incomplete generation fixture.
- `complete_scene_coverage` additionally requires every selected chunk and production resolution. This does **not** turn the fixture into a measurement of the scene's complete streaming scheduler.
- Ready mesh allocations are retained until the fixture ends. This exposes an insufficient `vertexBudgetMiB` instead of recycling successful chunks to produce a misleading success. `--vertex-budget-mib` is an explicit diagnostic override.
- `initialize_ms` is separate from generation `wall_ms`. Drivers may defer compilation until the first dispatch: that lazy work can still be included in `wall_ms` and stage query times. Warm repeats are required.
- GPU stage times, CPU submit/poll/dispatch distributions, peak pending jobs, cache hits, field batches, resident bytes, object count and errors are recorded. Asynchronous timer samples dropped/pending are reported.
- `--objects` uses the selected checkout's placement catalogue. Different densities can change object counts and must be reported as an appearance/configuration change, not a pure optimization.
- `--render-during-generation` interleaves a small terrain vertex/descriptor-buffer draw at up to 40 Hz, with at most two outstanding render fences. It is a descriptor/upload contention probe. Its fragment shader is deliberately minimal, so this is **not** a complete terrain rendering benchmark.
- `--plane` replaces the field with a controlled flat surface. Such results are a synthetic backend diagnostic, not the scene's biome workload. `--biome` is another explicit workload override.
- The timeout stops submitting new work; it cannot preempt an already running driver/GPU operation.

### Trees

The fixture uses the actual baked models and production G-buffer shaders, fixed hashed positions and fixed scale 1. Camera, instance count, model count, resolution and zero to four depth-only shadow passes are explicit. Instance pages have stable allocation identities when supported by the tested revision.

Four phases separate steady rendering, moving-camera culling, repeated publication of unchanged instance pages, and adding/removing a genuinely new page outside every light frustum. Each has two warm-up frames. CPU submission and GPU timestamp intervals separate preparation, shadow rendering, atmospheric volumes and geometry/sky. GPU timings can include CPU/driver submission gaps; they are not shader-ALU timings. Asset load/upload wall time and the number of loading polls are separate. Models come from the beginning of the catalogue; allow at least one 64-instance page per model.

```sh
python Tests/Performance/run.py --library /tmp/endurance-perf.so --scenario trees --instances 512 --models 4 --frames 20 --resolution 320 --shadow-layers 4 --shadow-resolution 256 --output trees-forced.json
python Tests/Performance/run.py --library /tmp/endurance-perf.so --scenario trees --instances 512 --models 4 --frames 20 --resolution 320 --shadow-layers 4 --shadow-resolution 256 --cache-shadows --output trees-cached.json
python Tests/Performance/run.py --library /tmp/endurance-perf.so --scenario trees --instances 512 --models 4 --frames 20 --resolution 320 --shadow-layers 4 --shadow-resolution 256 --cache-shadows --atmosphere --output trees-atmosphere.json
```

Without `--cache-shadows`, every shadow layer renders each frame, providing comparable submitted geometry. With it, the fixture calls the production `ShadowMapCache`, uses global renderer/camera revision on older sources and exact per-layer tree revisions when available. A reduction in generated primitives then means reused shadow maps, not necessarily removed visible geometry. `--shadows` remains an alias for one layer.

`gl_call_probe.hpp` instruments selected GL calls in the benchmark translation units only: state queries, uploads, binds, barriers, dispatches and indirect draws/commands. Both baseline and candidate use the same wrappers. Counters have some CPU cost, so prefer their operation counts over small differences in elapsed time. Color/depth hashes and covered pixels are read **after** timed work; use them alongside `Tests/Trees/check_runtime.py`, not as a substitute for its correctness tests.

`--atmosphere` executes real production LUT, sky, aerial perspective, cloud and fog code. A small test camera/light-manager adapter provides actual view matrices, four real tree depth layers, the light-matrix SSBO and shadow content revision. Wind is zero to isolate invalidation. Ultra ray-step settings stay intact, but volumes are explicitly 32×18×24, visible clouds 64×36 and IBL targets 16/16/4 with 8 samples. Those deliberately reduced targets make software-GL comparisons practical; this is **not the scene's Ultra atmospheric workload**. Initial environment bake is warmed and reported separately. There is no terrain/deferred composite/SSAO/postprocessing in this bounded integration fixture.

### Selection

The exact scene selection is hashed before benchmark sorting. `selected_chunks`, LOD histogram, conservative height bounds and `full_resolution_voxel_cells` describe the final workload. Empty-volume rejection can avoid much of this theoretical cell work; the number is not a prediction of GPU execution time.

## Local measurements

See `results/` for saved cases and measured limitations. A `software_renderer: true` report (for example llvmpipe) is useful for regressions and equivalence checks, but must never be extrapolated to GTX 1660 frame rates or full-world completion time.
