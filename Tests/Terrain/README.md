# Terrain validation

Run these commands from the repository root. They test the production C++ and
GLSL files directly. There is no window or full game launch requirement.

## CPU selection, parameters and tables

A C++17 compiler and the bundled GLM headers are sufficient. Release flags are
intentional: `-ffast-math` must not disable NaN/Infinity input validation.

```sh
c++ -std=c++17 -O3 -ffast-math \
  -I ANTAGONIZ/Header/Miscellaneous -I ThirdParty/glm \
  Tests/Terrain/check_cpu.cpp \
  ANTAGONIZ/Source/Miscellaneous/ProceduralTerrainLod.cpp \
  -o /tmp/endurance-check-terrain-cpu
/tmp/endurance-check-terrain-cpu
```

Checks include conservative field bounds, invalid domains, all 768 packed table
cases, deterministic LOD selection, complete/disjoint coverage, negative
coordinates, face balance 2:1, transition masks, full-resolution collision
radius, failure preserving the previous selection, and the demo's chunk budget.

## Frustum culling and incremental LOD publication

```sh
c++ -std=c++17 -O3 -ffast-math \
  -I ANTAGONIZ/Header/Miscellaneous -I ThirdParty/glm \
  Tests/Terrain/check_frustum.cpp -o /tmp/endurance-check-frustum
/tmp/endurance-check-frustum

c++ -std=c++17 -O3 -ffast-math \
  -I ANTAGONIZ/Header/Miscellaneous -I ThirdParty/glm \
  Tests/Terrain/check_visibility.cpp -o /tmp/endurance-check-visibility
/tmp/endurance-check-visibility

c++ -std=c++17 -O3 -ffast-math \
  -I ANTAGONIZ/Header/Miscellaneous -I ThirdParty/glm \
  Tests/Terrain/check_streaming.cpp \
  ANTAGONIZ/Source/Miscellaneous/ProceduralTerrainLod.cpp \
  ANTAGONIZ/Source/Miscellaneous/ProceduralTerrainStreaming.cpp \
  -o /tmp/endurance-check-streaming
/tmp/endurance-check-streaming

c++ -std=c++17 -O3 -ffast-math -pthread \
  -I ANTAGONIZ/Header/Miscellaneous -I ThirdParty/glm \
  Tests/Terrain/check_planner.cpp \
  ANTAGONIZ/Source/Miscellaneous/ProceduralTerrainLod.cpp \
  ANTAGONIZ/Source/Miscellaneous/ProceduralTerrainStreaming.cpp \
  ANTAGONIZ/Source/Miscellaneous/ProceduralTerrainPlanner.cpp \
  -o /tmp/endurance-check-planner
/tmp/endurance-check-planner
```

The frustum fixture exercises the production helper for moving/rotating cameras,
orthographic light projections, conservative tangencies and large positive and
negative coordinates. An independent homogeneous clip-space corner oracle checks
that potentially visible boxes are not rejected. A caster outside the camera
frustum remains eligible for a different light frustum.

The visibility-cache fixture verifies stationary reuse, cumulative translation
thresholds, rotation/projection/camera invalidation and per-light matrix changes.
Independent clip-space tests ensure that padded cached frusta retain visible boxes
through sub-threshold motion, including boundary crossings and large coordinates.

The streaming fixture validates local publication transactions against a separate
geometric oracle: no overlap, face balance at most 2:1, compatible transition
masks, and preservation of shared old/new coverage. It checks fill before
refinement before downgrade, ordering within each category, retirement of the
coarse parent, clipped boundaries and tight budgets. Each transaction requires at
most 32 new meshes. A default-distance route with reversals checks progress while
the desired camera position changes and exact spatial convergence after stopping.
The earlier independent-group tests remain as regression coverage. The worker
fixture checks asynchronous requests, one outstanding result, desired-selection
reuse, coalesced camera movement and destruction with work pending.

## Actual camera / terrain / shadow integration

`check_follow.py` links the fixture against an already built **Linux** engine,
replacing only its `Main.cpp.o`; it requires `compile_commands.json`, desktop
OpenGL 4.6 and `libEGL.so.1`. For a Makefile-based CMake build:

```sh
EGL_PLATFORM=surfaceless python3 Tests/Terrain/check_follow.py --build-dir /path/to/build
```

For Ninja or a build that reuses objects from several output directories, supply
`--link-command /path/to/link-command.json`; the JSON contains `cwd` and the exact
link argument array `argv`. This lets the fixture use the same compiled objects
and libraries as the application. Mesa software validation additionally uses
`MESA_GL_VERSION_OVERRIDE=4.6 MESA_GLSL_VERSION_OVERRIDE=460` as described below.

The fixture creates real engine camera, light and terrain managers in EGL. Only
`prepareRenderer()` advances terrain generation: no behaviour update or game
script runs. The analytic plane starts with partial drawable coverage before the
whole selection is ready. The fixture then approaches a region already published
at LOD2: actual LOD0 chunks must replace its coarse parent before all distant
changes finish. Moving away and returning verifies both downgrade and a second
refinement of the same generated region. A read-only snapshot of published chunks
checks their geometry, not just counters: no overlapping parent/child, 2:1 face
balance, compatible transition masks on every shared face and exact final spatial
selection (including empty chunks). Exterior-face mask bits may remain after
retirement because no neighbour depends on them. In-flight, resident-chunk and vertex-memory budgets are checked throughout.

Camera movement during pending jobs must replan and publish LOD groups; stopping
must converge to the active camera's current selection. The fixture also checks
camera replacement, projection-range restoration on shutdown, main-camera culling
and actual shadow-depth writes from terrain outside the camera frustum. Eight
alternating stationary camera/shadow frames must add no culling updates or indirect
command uploads. Camera movement below and beyond the cumulative culling step
checks reuse and invalidation; stationary converged terrain must not repeat LOD
selection.
Real directional, point and spot lights exercise allocation, shadow toggles,
framebuffer reattachment and destruction as the array changes between 0, 4, 8
and 10 layers. It does not launch the complete editor/window/input loop or the
physics queue.

## Biome-owned volume thickness

```sh
EGL_PLATFORM=surfaceless python3 Tests/Terrain/check_band_control.py
```

This fixture edits `fieldRelief` in a temporary
`biomes/PolarIce/relief.C.glsl` and executes the actual classification, per-biome
accumulation and density initialization passes. Twenty-four width/scale combinations cover negative,
zero, fractional and clamped widths. The band comes only from the biome's clamped
`.y`, quantized once to the finalized storage format and multiplied by
`detailScale`, with no additive base or global noise. The open/closed decision
and very small positive widths are preserved explicitly in the packed metadata.
A spatial-noise recipe must contain both closed and active columns; all density
samples in closed columns equal the unchanged height field. The C++ reload
fixture also enables/disables the band by editing only the 2D shader and checks
that the density program is reused from its binary cache.

## Packed columns and conservative field skips

```sh
EGL_PLATFORM=surfaceless python3 Tests/Terrain/check_field_optimizations.py
```

This fixture checks 432 GPU round trips through the 32-byte column format,
including exact FP32 values and biome IDs, bounded control quantization, and
preservation of the original tiny-band open/closed decision. A custom biome
returns `2*w`; a constant-delta recipe separately verifies that neither the
shared helper nor the accumulator applies a hidden `.w` multiplier. An analytic
thin-band case checks all 6,859 density samples.

Production variants verify homogeneous-field skips, conservative sign unions
including halo-only crossings, vertical column sharing and density aliases.
Another 41,154 densities at origins Y=±2^24 and LOD 0, 6 and 12 must match an
exhaustive GPU launch bit for bit with the same packed inputs and recipes but
without tile/Y skipping. The test requires actual nonzero contributions and a
reduced dispatch, so an empty or inactive recipe cannot satisfy the comparison.

## Polar ice and shader reloading

```sh
EGL_PLATFORM=surfaceless python3 Tests/Terrain/check_polar.py
# Optional diagnostic image from actual sampled shader heights:
EGL_PLATFORM=surfaceless python3 Tests/Terrain/check_polar.py --preview /tmp/polar-relief.png

# Use the PIC GLEW object described in the backend section below:
EGL_PLATFORM=surfaceless python3 Tests/Terrain/check_reload.py --glew-object /tmp/endurance-glew-pic.o
```

The polar fixture measures the current polar relief and volume, checks the
Voronoi primitive against an independent oracle, continuity, finite values,
conservative bounds, seed repeatability and matching chunk/LOD samples. It does
not impose the former sparse-volume mask or an older artistic shape. `--reference-shaders /path/to/baseline/ProceduralTerrain`
additionally compares the other 25 forced biome recipes against a saved baseline.
The optional image requires NumPy and Matplotlib; it visualizes measured geometry,
not final materials or a rendered game screenshot.

The reload fixture changes the merged biome entry files in a temporary shader copy and
verifies that reinitialization changes actual meshed vertex positions without a
process restart. It uses an isolated temporary `ENDURANCE_SHADER_CACHE_DIR`.
With program binaries supported, the first initialization compiles the fixed
61-program family: 26 relief specializations, 26 volume specializations, five
shared field passes and four extraction passes. Unchanged regeneration and
settings-only changes must load all 61 binaries with no compile/link calls.

Editing `biomes/PolarIce/relief.C.glsl` recompiles only the polar 2D program; editing
its `volume.C.glsl` recompiles only the polar 3D program. The other 60 programs must
load from cache. A shared 2D-noise edit rebuilds classification and the 26 relief
programs; a shared 3D-noise edit rebuilds the 26 volume programs. Neither edit
invalidates the extraction programs or the other dimension.

The geometry fixture replaces the polar volume function with a constant density
contribution. Enabling its band through the 2D recipe makes that contribution
visible without recompiling 3D; changing the contribution from 0.5 to 0.75 moves
the meshed plane from 6.75 to 7.0. A zero band suppresses even that custom function.
Actual vertices also verify that binary loads reapply changed uniforms. Invalid
or missing biome GLSL must fail with the merged entry's original path/line instead of using
an obsolete binary, and fixing the file must allow reinitialization. Production
sources and the user's shader cache are not edited by the fixture.

## Compute shaders

`EGL_PLATFORM=surfaceless python3 Tests/Terrain/check_noise2.py` compares the
four-corner vector noise kernel against its former scalar construction. It checks
40,960 values bit for bit across five seeds, negative coordinates, exact lattice
points, near-grid positions and signed zeros. This guards the terrain/seed result
while reducing the branch expansion seen when compiling all biome recipes.

Linux, Python 3.10+, a C++17 compiler, `libEGL.so.1`, `libGL.so.1`, and desktop
OpenGL 4.5 are required. Python uses only the standard library. The C++ compiler
exports the actual vendor tables through a temporary C++ program; no duplicate
table fixture is maintained. If EGL cannot create a suitable context, the test exits 77.

```sh
EGL_PLATFORM=surfaceless python3 Tests/Terrain/check_gpu.py
```

Checks include float density determinism, full halos, shared samples between LODs,
vertical band offsets, zero amplitudes, plane/sphere geometry, normals, regular
winding, empty meshes, 4/8/16/64-cell chunks, and sentinel-protected overflow in
both regular and transition emission. Closed-surface tests count every mesh edge
and verify opposite directions on shared edges. They cover all six transition
faces, asymmetric spheres, floating-point quantization regressions, and a
15-chunk corner with three simultaneous transition faces. Real biome/noise
fields and a dense ambiguous-sign fixture also exercise the joins.

To isolate field generation:

```sh
python3 Tests/Terrain/check_gpu.py --fields-only
```

## Extraction workgroup and edge reuse

```sh
EGL_PLATFORM=surfaceless python3 Tests/Terrain/check_meshing_optimizations.py
# Optional exact comparison with the original branch's extraction shaders:
EGL_PLATFORM=surfaceless python3 Tests/Terrain/check_meshing_optimizations.py \
  --reference-shaders /path/to/baseline/ProceduralTerrain
```

This independent fixture supplies synthetic density and climate buffers. It
compares complete packed triangles, including normals and climate bytes, with
workgroup/edge reuse disabled or with an explicitly supplied original shader
directory. All 256 regular cases, all 512 transition cases, 48 random dense
fields and a 64-cell plane are executed; the faces, small transition tiles and
a nonzero metadata-record index are covered. Triangle order between workgroups
is ignored, but each triangle's vertex order and all packed words must match.

The plane has 128 active regular groups out of 2,048. Retained counts must skip
the other 1,920 emission groups, and a homogeneous field-state record must skip
all extraction. Sentinel words protect the count buffer. A separate lookup-table
check ensures that the sixteen transition edge keys cannot alias fine and
coarse edges. These are correctness and work-count checks, not GPU benchmarks.

## CPU biome layout and accumulated biome shaders

```sh
EGL_PLATFORM=surfaceless python3 Tests/Terrain/check_biome_pipeline.py
# Optional appearance comparison against a pre-accumulation shader revision:
EGL_PLATFORM=surfaceless python3 Tests/Terrain/check_biome_pipeline.py \
  --reference-shaders /path/to/baseline/ProceduralTerrain
EGL_PLATFORM=surfaceless python3 Tests/Terrain/check_parallel_compiler.py \
  --glew-object /tmp/endurance-glew-pic.o
EGL_PLATFORM=surfaceless python3 Tests/Terrain/check_background_compiler.py \
  --glew-object /tmp/endurance-glew-pic.o
```

`field_batch_runner.py` executes the production GLSL pass family and actual
`glDispatchComputeIndirect` commands. It uploads the authoritative C++ matrices
using `biome_test_support.py`, so there are no Python/GLSL matrix copies.
The ordinary GPU and meshing suites use this same runner, without falling back
to a diagnostic pipeline. `LegacyFieldRunner` requires an explicit external
pre-accumulation shader snapshot for historical comparisons; the production
shader directory contains no legacy field entrypoints or all-biome aggregates.

Each biome has exactly two shader files: `relief.C.glsl` defines `fieldRelief`,
and `volume.C.glsl` defines `fieldVolume`. The latter's default recipe controls
are named `FIELD_VOLUME`, `FIELD_SHAPE` and `FIELD_STYLE`. The functions and
entrypoints live together, with no function alias macro or separately included
per-biome implementation.

The pipeline fixture compiles all 52 biome specializations and checks
all 26 forced IDs. Repeated fields are bitwise identical, including neighboring
chunk halos and shared LOD points. Repeated meshes retain their positions and
normals. A forced biome must leave every other biome's indirect work count zero.

A 16-slot batch combines distinct origins, seeds and LODs with inactive holes.
Every active slot must match its independently generated counterpart bit for
bit. Sentinel padding checks both field strides and partial workgroups; inactive
slots must remain untouched. A CPU inspection of test-only readbacks checks
compact, ordered work lists, command dimensions and absent-biome zero dispatches.
Reusing the queue buffers for analytic fields must clear stale dispatch counts.

A separate mathematical oracle replaces all 26 functions in temporary copies
with different analytic 2D and 3D formulas. Independent CPU sums of corner
weights then predict the final columns and `base + sum(weight * delta)` density.
Coverage includes duplicate IDs, eight genuinely different volume contributors,
marine blending, all forced IDs and positive local weights below 0.1%. The test
never calls the production accumulation/weight helper to calculate its expected
answer. Another edit replaces only the polar 3D function with custom radial
geometry and its 2D function with Manhattan Voronoi relief: other biomes and
zero-detail fields must remain unchanged.

`--reference-shaders` compares the 26 forced biomes across two seeds with the
pre-accumulation revision. Numeric columns and density must match within a tight
floating-point tolerance; newly occupied surface-ID metadata is intentionally
excluded. Mixed-biome fields are expected to change, because adding nonlinear
function results is different from evaluating interpolated function parameters.

The fixed terrain family uses the existing driver-assisted parallel compiler at
initialization, with the binary cache checked first. The parallel-extension and
background-worker fixtures remain independent compiler-service tests. The former
checks bounded jobs, nonblocking completion queries, ownership/cancellation,
diagnostics and unsupported-driver behavior. The latter creates a shared EGL
context and checks worker-only compilation/preparation, shared execution,
bounded cancellation and hook failures. Startup batch/cache coverage lives in
[Tests/ShaderCompilation](../ShaderCompilation/README.md).

Runtime construction uses one 48-byte FP32 scratch column array per in-flight
job. Finalized columns use a 32-byte packed view in a bounded XZ cache; density
fields and their conservative sign/tile metadata have a separate bounded cache.
Referenced cache entries cannot be evicted until their GPU consumers finish.
A 4,160-byte queue stores 52 indirect commands and up to 16 job slots per command.
Each job has a 48-byte descriptor; count/emit counters remain 16 bytes. No field
or local biome mask is downloaded in production. A separate finalization/prepare
barrier lets vertical aliases consume one newly built XZ tile safely. Volume
groups share columns/weights across Y lanes, reject inactive tiles and restrict
work to conservative vertical ranges. Padded lanes still reach all required
shared-memory barriers before returning.

Diagnostics sample one biome's actual `fieldRelief` or `fieldVolume` function
at a time, using the same source file as its production specialization. Probe
construction keeps the recipe and its helpers and replaces only the final
accumulation kernel. Complete mixed-biome field checks use `BatchFieldRunner`.
The former all-biome aggregate shaders and standalone column/density diagnostics
have been removed from `Asset`; tests do not reconstruct a combined shader with
all 26 functions or interpolate the default volume constants.
`biome_mask_probe.py` independently checks local weights and masks against CPU
corner sums, including duplicate IDs, marine blending and forced biome IDs.

## Complete biome matrix and diagnostic plots

```sh
EGL_PLATFORM=surfaceless python3 Tests/Terrain/check_biomes.py
```

This test invokes the production GLSL for all 25 land recipes and the marine
recipe. It checks finite/distinct height and volume signatures, real cavities
inside the original solid, conservative vertical bounds, independent band
offsets, all 25 paired matrix cells, smooth climate and shoreline transitions,
and the exclusively marine lowest altitude slice. The accumulated density field
is checked across matrix boundaries using the production batch passes.
Every forced biome is also checked for bit-identical density samples shared by two LODs. A climate survey
across ±16 km and four seeds checks that all 25 dominant matrix cells occur;
it does not promise that every biome occurs near every spawn.

Optional measured plots need NumPy and Matplotlib:

```sh
python3 Tests/Terrain/check_biomes.py --atlas out/biome-validation
```

The height atlas displays 1536 × 1536 metres per biome at 8-metre sample spacing,
with a diagnostic hillshade and locally normalized colors. A separate atlas
shows vertical solid/air slices through the actual density field, at the Z row
containing each tile’s highest sampled point (the selected Z is labeled). The JSON
contains sampled height ranges, interior cavity counts and a slope statistic.
These are visual/debugging aids, not game screenshots. Finite sample coverage
and low-slope fractions cannot prove a minimum traversable corridor width or a
grapple route; those require route constraints and playtesting.

## G-buffer, shadows and world-coordinate decoding

The rendering stages use GLSL 4.60, matching the engine. On a native OpenGL 4.6
driver:

```sh
python3 Tests/Terrain/check_gpu.py --render
```

The software driver available during development exposed OpenGL 4.5. These
explicit Mesa test overrides enabled its GLSL 4.60 implementation:

```sh
MESA_GL_VERSION_OVERRIDE=4.6 MESA_GLSL_VERSION_OVERRIDE=460 \
  python3 Tests/Terrain/check_gpu.py --render
```

The test rasterizes an indirect triangle into four floating-point G-buffer
attachments, checks the camera projection and descriptor index, and verifies a
cascade's depth output. All 26 forced biome palettes must produce finite, distinct outputs. Transform feedback captures the real vertex shader's
world positions: matched coarse/fine positions must have identical float32 bits
at positive and negative coordinates through LOD20. An override is not evidence
of native OpenGL 4.6 support on the host.

## Shadow sampling in the lighting shaders

```sh
EGL_PLATFORM=surfaceless python3 Tests/Terrain/check_shadow_sampling.py
```

This fixture compiles the three production lighting fragment shaders and runs
their shadow-sampling functions against controlled depth textures. It checks
out-of-map XY/Z coordinates, non-positive projection W, bounded slope bias and
comparison-before-filtering PCF. Use the integration fixture above to also test
the real light managers, framebuffer replacement and terrain shadow draw calls.

## C++ GPU backend lifecycle

This wrapper loads the actual backend into the same EGL context. Supply a PIC
GLEW object or archive. If the main build's GLEW object is not PIC, compile the
bundled source once; GL/GLX development headers must be available:

```sh
cc -O2 -fPIC -DGLEW_STATIC -DGLEW_NO_GLU \
  -I ThirdParty/glew/include \
  -c ThirdParty/glew/src/glew.c -o /tmp/endurance-glew-pic.o
python3 Tests/Terrain/check_backend.py --glew-object /tmp/endurance-glew-pic.o
```

For development headers installed in a separate sysroot, append
`-I /path/to/sysroot/usr/include` to the `cc` command. The wrapper needs the
runtime OpenGL library and a compiler able to link `-l:libGL.so.1`. An installed
shared GLEW library is also accepted as `--glew-object`, for example
`/usr/lib/x86_64-linux-gnu/libGLEW.so.2.2`; it avoids building a separate object.

Checks include asynchronous count/emit completions, token rejection, in-flight
saturation, cancellation before and after allocation, slot and vertex memory
budgets, fence retirement, stale handles after reuse/reinitialization, empty
chunks, shutdown with work in flight, and restart. Transform feedback from real
`draw` and `drawMany` calls checks vertex counts, vertex ranges and descriptor
base instances. Test-only readbacks inspect buffer guards; production keeps
density and mesh data on the GPU.

The biome backend cases additionally cover cancellation, visits to different
climate regions and a return to the first one with identical packed geometry,
idle polling without new work, bounded scratch/cache arenas and compact GPU
queue metadata. Four queued chunks with different seeds must form
one GPU batch and match sequentially generated meshes. `dispatchQueued()` must
start that batch once without consuming completions; repeated calls are inert.
Cancellation and slot reuse must preserve tokens, and a fully cancelled queued
batch must issue no field work. Intercepted GL compilation/link entry points verify that no
runtime shader compilation occurs during streaming, revisits or idle polling.
Software-driver first-dispatch JIT is allowed a 60-second correctness timeout;
these lifecycle tests are not performance measurements.
The backend fixtures link `ShaderProgramBatch`, `ParallelShaderCompiler`, the
shared source loader and `ShaderBinaryCache.cpp`. They use isolated temporary disk caches. Shared loader, persistent binary
cache, program ownership and graphics-pipeline coverage is documented separately
in [Tests/ShaderCompilation](../ShaderCompilation/README.md).

## Field-cache ownership and GPU reuse

```sh
c++ -std=c++17 -O3 -ffast-math \
  -I ANTAGONIZ/Header/Miscellaneous \
  Tests/Terrain/check_field_cache.cpp -o /tmp/endurance-check-field-cache
/tmp/endurance-check-field-cache
```

The CPU fixture checks pending aliases, reference pinning, eviction of only
unreferenced slots, LRU replacement, exact coordinate/LOD/seed keys, disabled
completed retention and reset. The real backend fixture above compares cached
output to sequential generation with both completed caches disabled. It checks
full packed triangles, not just vertex counts, across vertical XZ sharing,
mask-only remeshing, changed seeds/LOD, same-batch aliases, owner cancellation
before and after GPU submission, separate batches with pinned fields, LRU
eviction/revisit and homogeneous fields whose density was not materialized.
Zero retention must still allow safe sharing between in-flight requests, then
rebuild after the final consumer releases its slot.

`clearFieldCaches()` must reject uninitialized and busy instances, invalidate
retained fields at idle, and leave already published meshes and their allocation
intact. The next request must rebuild both fields and produce the same encoded
triangles. With `measureGenerationTimings=false`, intercepted GL entry points
must observe no query creation, submission or read; pending, dropped and measured
timing counters must stay zero without GL errors or changed geometry.

## Collision transfer and bounded workers

```sh
EGL_PLATFORM=surfaceless python3 Tests/Terrain/check_collision.py \
  --glew-object /usr/lib/x86_64-linux-gnu/libGLEW.so.2.2
```

This fixture runs the production collision cache with real EGL persistent
mapping and Bullet BVH construction. Small adapters replace only mesh residency
lookup and the physics publication queue. Checks cover absence of GL CPU
readback calls, bounded worker/build queues, staging release before physics
publication, canonical FMA position decoding including negative chunks,
degenerate triangles, mapping growth, replacements, cancellation and shutdown.
It does not replace the full application's physics-queue integration test.

## Interpretation

The engine physics worker now sleeps until a 60 Hz deadline or an incoming
command. Its separate regression test checks exact Bullet steps, retained
inter-tick forces, bounded catch-up after a debugger pause, immediate command
and stop wake-ups, and absence of idle command-pump spinning:

```bash
bash Tests/run-physics-worker.sh <Bullet-build-directory>
```

The argument is the Bullet build directory containing the static libraries under
`src/BulletDynamics`, `src/BulletCollision` and `src/LinearMath`.

Development validation ran on Mesa 25.0.7 llvmpipe (LLVM 19.1.7), a software
renderer. It verifies real shader execution and lifecycle correctness; its GPU
query timings are not representative of a physical GPU. These checks do not
replace profiling, visual inspection, or a full-game smoke test on the target
graphics driver. They also do not exercise the complete physics queue or scene
streaming integration.

For repeatable before/after generation measurements, cold field runs, warmed
shader caches and interpretation of the stage counters, follow
[the comparison guide](../../docs/TERRAIN_OPTIMIZATION_COMPARISON.md). The benchmark
is separate from these correctness fixtures.


## Bibliothèque de bruits 2D/3D

La bibliothèque autonome se trouve dans `Asset/Shader/Noise`. Les tests GPU utilisent
les fonctions de production, sans uniform implicite du terrain :

```sh
EGL_PLATFORM=surfaceless python3 Tests/Terrain/check_noise2.py
EGL_PLATFORM=surfaceless python3 Tests/Terrain/check_noise_library.py
```

Le premier test conserve un oracle scalaire historique indépendant pour les
40 960 valeurs du Perlin2D vectorisé. Le second vérifie les bases signées et 01,
les seeds, les raccords périodiques aux coordonnées négatives, les distances
cellulaires contre une recherche indépendante 7×7/7×7×7, les singularités des
courbes, les octaves des compositions, la déformation et le stencil du curl.
Les deux dimensions peuvent aussi être incluses ensemble.

Pour vérifier la conservation de la génération lors de cette migration, extraire
la référence `847282fbb6bd9ff3a26674b1c37f4311d7033ca9` dans un autre checkout puis :

```sh
EGL_PLATFORM=surfaceless python3 Tests/Terrain/check_noise_library.py \
  --reference-shaders ../ENDURANCE-before-noise/Asset/Shader/ProceduralTerrain
```

Cette option compare bit à bit 3 096 reliefs bruts de PolarIce/FrozenTundra, puis
27 champs complets et leurs colonnes compactées/décodées, sur plusieurs seeds,
origines et LOD, y compris le mélange automatique des biomes. Elle complète
les tests de pipeline et de rechargement ; une modification de `noise_2d.glsl`
invalide la classification et les 26 reliefs, celle de `noise_3d.glsl` les
26 volumes. Les copies temporaires des shaders emportent aussi le dossier `Noise`.

Le [catalogue](../../docs/NOISE_LIBRARY.md) contient les atlas issus de vrais
échantillons GPU et la commande pour les régénérer.
