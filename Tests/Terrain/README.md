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
  Tests/Terrain/check_streaming.cpp \
  ANTAGONIZ/Source/Miscellaneous/ProceduralTerrainLod.cpp \
  ANTAGONIZ/Source/Miscellaneous/ProceduralTerrainStreaming.cpp \
  -o /tmp/endurance-check-streaming
/tmp/endurance-check-streaming
```

The frustum fixture exercises the production helper for moving/rotating cameras,
orthographic light projections, conservative tangencies and large positive and
negative coordinates. An independent homogeneous clip-space corner oracle checks
that potentially visible boxes are not rejected. A caster outside the camera
frustum remains eligible for a different light frustum.

The streaming fixture validates independent publication groups against a separate
geometric oracle: no overlap, face balance at most 2:1, compatible transition
masks, and preservation of shared old/new coverage. It covers split/merge,
changed transition masks, negative coordinates and moving-camera replanning.

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
script runs. Camera movement during pending jobs must replan and publish LOD
groups; stopping must converge to the active camera's current selection. It also
checks camera replacement, projection-range restoration on shutdown, main-camera
culling and actual shadow-depth writes from terrain outside the camera frustum.
Real directional, point and spot lights exercise allocation, shadow toggles,
framebuffer reattachment and destruction as the array changes between 0, 4, 8
and 10 layers. It does not launch the complete editor/window/input loop or the
physics queue.

## Compute shaders

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

## Complete biome matrix and diagnostic plots

```sh
EGL_PLATFORM=surfaceless python3 Tests/Terrain/check_biomes.py
```

This test invokes the production GLSL for all 25 land recipes and the marine
recipe. It checks finite/distinct height and volume signatures, real cavities
inside the original solid, conservative vertical bounds, independent band
offsets, all 25 paired matrix cells, smooth climate and shoreline transitions,
and the exclusively marine lowest altitude slice. Every forced biome is also
checked for bit-identical density samples shared by two LODs. A climate survey
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
runtime OpenGL library and a compiler able to link `-l:libGL.so.1`.

Checks include asynchronous count/emit completions, token rejection, in-flight
saturation, cancellation before and after allocation, slot and vertex memory
budgets, fence retirement, stale handles after reuse/reinitialization, empty
chunks, shutdown with work in flight, and restart. Test-only readbacks inspect
indirect commands and buffer guards; the production graphics path keeps density
and mesh data on the GPU.

## Interpretation

Development validation ran on Mesa 25.0.7 llvmpipe (LLVM 19.1.7), a software
renderer. It verifies real shader execution and lifecycle correctness; its GPU
query timings are not representative of a physical GPU. These checks do not
replace profiling, visual inspection, or a full-game smoke test on the target
graphics driver. They also do not exercise the complete physics queue or scene
streaming integration.
