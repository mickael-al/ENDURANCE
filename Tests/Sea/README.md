# Sea surface regression checks

Run from the repository root on the Linux test environment:

```sh
python3 Tests/Sea/check_coverage.py
python3 Tests/Sea/check_streaming.py
python3 Tests/Sea/check_render.py
python3 Tests/Sea/check_runtime.py
python3 Tests/Terrain/check_sea_metadata.py --glew-object /usr/lib/x86_64-linux-gnu/libGLEW.so.2.2
```

`check_coverage` checks half-open vertical ownership, complete squares,
split/merge coverage, invalid inputs and canonical seam positions. It compiles
normally and with `-ffast-math`.

`check_streaming` uses the actual terrain scheduler/planner and controlled GPU
completions. It checks marine chunks without terrain vertices, atomic publication,
failure/retry, stale completions and idle stability, including sea levels outside
the terrain's vertical bounds. Its shared backend hooks are enabled only by
`TERRAIN_SEA_STREAMING`; the ordinary scheduler test remains independent.

`check_render` compiles and renders the production sea shaders with EGL/OpenGL.
It checks finite output, opaque depth, sky tags, LOD invariance and canonical
world-coordinate seams. Blocking readbacks belong only to the test.

`check_runtime` exercises the real C++ sea renderer with controlled GL/engine
services. It verifies revision-based uploads, buffer growth, allocation/upload
failures, restoration of external draw state and resource cleanup. Production
headers are also used in a separate C++ syntax check during integration.

`check_sea_metadata` exercises the actual GPU mesher and cache paths with real
shaders. It covers Ready/Empty, automatic/forced/mixed biomes, disabled relief,
column/density cache hits and footprint/halo boundaries. Its GL instrumentation
rejects blocking waits introduced by this feature.

Existing regressions relevant to the integration:

```sh
python3 Tests/Terrain/check_scheduler.py
python3 Tests/Terrain/check_backend.py --glew-object /usr/lib/x86_64-linux-gnu/libGLEW.so.2.2
python3 Tests/TerrainMaterials/check_runtime.py
```

Software OpenGL checks do not measure hardware performance or replace the
in-game visual checks described in `docs/terrain-sea.md`.
