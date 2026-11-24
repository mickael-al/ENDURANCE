# Procedural terrain materials

ENDURANCE's terrain uses authored palettes and procedural surface patterns instead
of imported terrain textures. Material attributes are written into the existing
deferred G-buffer, so they use the same direct lights, shadow maps, atmosphere,
SSAO and environment reflections as other PBR objects.

## Rendering source map

The terrain color pass has **one vertex/fragment program**. The GLSL includes
below are functions compiled into that program, not additional rendering passes.

| File | Responsibility / entry points |
|---|---|
| `Asset/Shader/ProceduralTerrain/render.fs.glsl` | Fragment `main`: material evaluation, final normal and G-buffer writes |
| `Asset/Shader/ProceduralTerrain/materials.glsl` | `terrainEvaluateAppearance` → biome → surface → material: climate, slope, palettes and PBR blending |
| `Asset/Shader/ProceduralTerrain/patterns.glsl` | Shared noise/value gradients, grain spectrum and the eight procedural relief families |
| `Asset/Shader/ProceduralTerrain/fine_surface.glsl` | Filtered stain masks and independent grain facets after material blending |
| `ANTAGONIZ/Header/Miscellaneous/ProceduralTerrainFineSurface.hpp` | One-time C++ bake of the eight-octave stain mask and sparse grain cache |
| `Asset/Shader/ProceduralTerrain/render.vs.glsl` | Packed terrain vertex decoding, world position and climate varyings |
| `ANTAGONIZ/Source/ProjectUser/EnduranceTerrainMaterials.cpp` | `makeTerrainAppearance`: authored ground/wall palettes and parameters for all 26 biomes |
| `ANTAGONIZ/Source/Miscellaneous/ProceduralGeneration.cpp` | Program creation, appearance upload and the existing batched terrain draw |

Terrain-generation compute shaders and the atmosphere's palette compute shader
serve separate systems. They do not add terrain color draws. Shadow draws keep
their existing depth-only path and do not execute the material functions.

## Artistic model

Every one of the 25 climate biomes and the marine biome has a ground profile and
a wall profile. Each profile contains a primary and a secondary material. Each
material has three linear RGB palette stops, with independently authored
roughness at each stop, plus metallic, material AO, normal strength and fine
roughness variation. Palette midpoint, transition softness, contrast and bias
control smooth or nearly discrete color regions.

The masks and materials use stable world-space fields:

1. One shared broad color field with a configurable global period in metres.
2. One intermediate field per active biome, with its own period multiplier and
   seeded offset. Frequencies are never interpolated before evaluating world
   coordinates: the evaluated material responses are blended instead.
3. Directional color strata and ripples with per-surface settings.
4. An independent fine field per active biome, with an authored grain period,
   one to three octaves and a configurable gain. Subpixel octaves fade out.
5. Separate ground/wall normal recipes: fine mineral grain, wind ribs, bedding,
   fractures, organic fibres, wet sediment, ice veins and ropy lava. Their
   feature periods, deformation and grain mix are authored per surface.

Broad palette patches no longer automatically displace the normal. Fine grain
uses centimetre-scale periods where appropriate; metres-wide rounded bumps are
not added to every material. These are stylized material cues, not additional
terrain geometry or a geological simulation.

The intent is to preserve quiet surfaces and readable large formations while
adding local material character. The authored recipes and their visual intent
are documented in [the biome palette guide](TerrainMaterialsPalette.md).

The normal perturbation is calculated from procedural gradients. No normal-map
asset, extra vertex attribute, material texture array or per-biome draw is needed.
Rocks, sand, snow and ice are dielectric: their presets use metallic zero. This
does not disable specular reflections; roughness controls their appearance.
Material AO is separate from SSAO and defaults to one rather than treating color
variation as occlusion.

## Edit and save

Open the `ProceduralTerrainComponent` inspector and expand
**Materiaux proceduraux / palettes**. Select a palette to edit, then expand
**Sol** or **Parois et plafonds**, and its primary or secondary material.

- Color pickers display sRGB; values are stored and mixed in linear RGB.
- Edit color stops, roughness, response, masks, pattern weights and periods live.
- Changing the palette selection does not change the generated biome. The
  existing **Biome a tester** control selects the generation test biome.
- Material edits apply without rebuilding chunks. Geometry/climate generation
  changes still use the existing regeneration button.
- Save the scene to persist all material settings. Old scenes without these
  fields receive the full authored defaults.
- The per-biome restore button restores that biome's authored material profiles;
  the all-presets button deliberately resets the complete appearance.
- Revision-1 scenes upgrade only biome records that match the original defaults.
  Edited records and global settings remain intact. Newly introduced controls
  start from the biome's new authored profile. The migration is versioned and
  saves its revision with the scene.
- Control views expose the palette, surface blend, secondary-material mask,
  roughness, metallic/AO and normals. They use the existing unlit/emissive tag
  with intensity one, so direct lighting and SSAO do not obscure these colors.
  Atmosphere and final display processing still apply. Normal rendering keeps
  its independent geometric-normal tag and full PBR lighting.

The game owns presets, the editor panel and serialization. The engine owns the
generic data contract, validation, GPU buffer and rendering. Appearance changes
can also be submitted through `ProceduralGeneration::setTerrainAppearance`.

## GPU contract and budget

`ProceduralTerrainMaterials.hpp` contains plain, aligned records with compile-time
size and offset assertions. The GLSL table uses std140 at UBO binding 6:

| Record | Size |
|---|---:|
| TerrainMaterial | 112 bytes |
| TerrainSurface, two materials and mask/pattern/normal settings | 272 bytes |
| TerrainBiomeAppearance, ground/wall, slope, spectrum and stain routing | 592 bytes |
| All 26 biomes | 15,392 bytes |

The complete table fits the minimum OpenGL uniform block size. It is uploaded
only when edited; changing global controls only updates their uniforms. The
renderer restores the previous buffer binding, including offset and range.
Shadow rendering does not evaluate these materials.

The existing 12-byte packed vertex layout and batched terrain draws are retained.
Climate interpolation keeps all four matrix corners, with the existing smooth
weights, rather than selecting only the two dominant materials. The matrix IDs
come from the authoritative CPU biome matrix. Coast blending and forced-biome
inspection are preserved.

The macro noise is shared by the whole fragment. The intermediate and fine
fields are evaluated per participating biome, then reused by ground, wall,
primary and accent materials; there is no separate fBm for each material.
Scalar values and analytic gradients are produced together. Quintic
interpolation keeps the gradients smooth at lattice boundaries. Explicit
world-space pixel derivatives are calculated before divergent selection and
passed to the filtering functions. Unresolved detail fades instead of producing
distant shimmer. Adding spectral octaves has a cost; the default recipes use
bounded counts and restrained gains rather than five octaves everywhere.

`TerrainBiomeAppearance::spectrum` contains patch-period multiplier, grain
period in metres, octave count and octave gain. `TerrainSurface::normal`
contains the family ID, feature period in metres, warp fraction and grain mix.
The existing global grain-frequency parameter is retained for scene
compatibility: its historical value `2` is now the neutral multiplier `1`
shown in the editor. A higher multiplier makes the authored grain finer.

## Deferred color and normals

The current deferred lighting shader expects gamma-encoded albedo and decodes it
using the engine gamma setting. The new material shader therefore mixes authored
colors in linear space and encodes the final albedo with the inverse of that same
setting. The renderer updates this uniform if gamma changes. This adapter avoids
a second gamma decode without changing other engine materials or G-buffer
formats.

The material normal remains in world space. Ground/wall masks use the underlying
normal before microdetail, and retain its sign: cave ceilings are walls, not
snow-covered upward-facing ground. The independent geometric-normal metadata
used for shadow bias is preserved.

## Fine grains and surface stains

`TerrainMaterial::fineSurface` is a serialized vec4, editable under the material's
**Details fins** section:

| Component | Meaning | Typical value |
|---|---|---:|
| x | Strength of sparse glossy grains; zero disables them | 0.95 snow, 0.90 sand |
| y | Grain roughness | 0.12 snow, 0.15 sand |
| z | Fractional linear albedo darkening in stains | 0.10, i.e. 10% |
| w | Absolute roughness reduction in stains | 0.06 |

The game classifies **each material**, including accents and walls, once in
`configureTerrainFineMaterials`. Pale neutral materials in the ten cold biomes
receive snow/frost grain; materials authored as sand receive sand grain. For
example, FrozenTundra's secondary snow receives glints, its primary gray sediment
does not. Green palette midpoints select the larger stain scale, including green
wall patinas; all other materials get the finer scale. Classification uses linear
palette values, never the sunlit or tone-mapped screen color. These initial
choices remain artist-editable; changing the palette live does not overwrite
explicit effect controls.

`TerrainBiomeAppearance::stainRouting` contains four vegetation weights: ground
base/accent and wall base/accent. Zero selects fine stains, one selects large
vegetation stains. The editor's **Taches : petite echelle / verdure** slider
exposes each weight. Global controls in **Materiaux proceduraux / palettes** are:

| Setting | Default | Effect |
|---|---:|---|
| `vegetationStainSize` / Taille taches de verdure | 2.5 m | Ten times the former 25 cm base feature size |
| `otherStainSize` / Taille taches autres matieres | 0.25 m | Finer detail on remaining materials |
| `sparkleGrainSize` / Taille grains brillants | 0.01 m | Independent reflective facet spacing |

The sizes describe base features/spacing, not an exact diameter of irregular
patches. These controls change uniforms without remeshing or rebaking anything.
They are independent of the biome relief noise's period and octaves.

Grains now use **independent sparse white noise**, not the smooth relief FBM.
C++ builds a 128² RGBA8 cache with random facet tilts and about 18.75% occupied
texels. One texture lookup after material blending supplies a small tangent-plane
normal perturbation and local roughness reduction. Existing GGX lighting,
shadows and IBL create the highlights; there is no emission, metallic boost,
extra lighting pass or G-buffer channel. This is a cheap stylized approximation,
not a dedicated glint BRDF. Facets fade as their spacing becomes subpixel; beyond
that distance, the shader skips the lookup. Their world-space pattern is stable
across chunk LODs, and changing relief FBM parameters does not move it.

Stains retain eight-octave periodic FBM (lacunarity 2, gain 0.5), thresholded at
0.5 **before** filtering. C++ builds a 512² R8 mask once per process, with four
coverage samples per texel. Each repeat contains 64 base features. The shader
samples the two fixed scales only when their blended weights are nonzero: at
most two stain lookups per fragment, after all biome/ground/wall blending, not
per material. It blends mask contributions rather than interpolating frequencies,
so stains do not slide at biome boundaries. Mip selection uses the world-space
footprint captured before branching. Unresolved masks converge to 50% coverage,
then skip their lookup beyond four times the feature size. Maximum local albedo
reduction is 10%; the distant average is 5%.

The fixed rotated world projection uses X/Z plus a Y shear, so walls also have
vertical detail. It is a cheap planar projection, not full triplanar mapping;
some directional stretching is an intentional performance tradeoff.

Both GPU caches total **415,061 bytes (about 0.396 MiB)** per terrain renderer:
349,525 bytes for stains with mipmaps and 65,536 bytes for facets. The latter
fade before minification, so no unused mip chain is allocated. The retained CPU
bakes total 320 KiB and are reused across renderer restarts. Initialization uploads
each texture once; chunk generation and palette edits do not regenerate them.
Borrowed texture units 0/1, samplers and pixel-unpack state are restored. Shutdown
and failed initialization release the allocations. The table adds only 416 bytes
relative to revision 3 and remains below the minimum 16 KiB uniform-block limit.

Scene revision 4 migrates the previous default fine controls to the new material
selection, using the loaded palettes. Explicitly edited fine controls and all
saved palette/roughness settings are preserved. Old scenes get the new sizes;
saving persists the controls. No binary terrain scene is replaced by this change.

`TERRAIN_FINE_SURFACE_ENABLED=0` compiles out both effects for an A/B test; it does
not reclaim their small cache allocation. Runtime branches skip zero material
weights and unresolved lookups. No extra terrain draw or generation pass is added.

### Validation and timing

The material checks cover C++/GLSL layout, saved-scene migration, live-edit state
restoration, independent grain size, vegetation stains enlarged tenfold, stable
scale blending, unaffected geometry/PBR channels and distant filtering. These
run on the available Mesa llvmpipe software renderer; they do not establish
whole-game performance or the visual result on the target GTX 1660.

The previous iteration's measurements remain in
`docs/terrain-material-validation/fine-surfaces-llvmpipe-2026-11-24.json` as a
historical baseline. Use `--baseline-ref 1a361d0` to compare this revision to the
previous effects, rather than comparing it to flat colors.

Final revision, 256x256 viewport, 15 alternating samples, three draws per sample,
`LP_NUM_THREADS=1`, completed-draw wall clock. The paired median overhead relative
to `1a361d0` is +15.2% for near sand, +18.0% for newly detailed volcanic rock,
+2.3% for one organic biome, +5.4% for four biomes with a slope blend, +1.9% for
far terrain and +6.9% for the coast fixture. Raw samples and source hashes are in
[the revision's timing report](terrain-material-validation/revised-fine-surfaces-llvmpipe-2026-11-24.json).
These are **software material-pass measurements**, not game frame times or GTX
1660 timings. The extra coverage/lookups are measurable; no performance parity
claim is made. Test on the target GPU before choosing the final quality budget.

## Precomputation investigation

A generated RGBA16F 3D noise volume, storing value and gradient with mipmaps, was
implemented as a standalone benchmark. On the available Mesa llvmpipe software
renderer it was slower than analytic evaluation, while also approximating the
gradient and introducing a finite repeat period. It is not part of production.
See [noise measurements and primary references](terrain-material-noise.md).

These measurements are not hardware GPU frame-time claims. Procedural materials
do more work than the previous flat colors; their complete material pass should
also be profiled on the target GPU. The benchmark is retained for reproducing
that comparison. The separate R8 fine-surface cache above serves a different
purpose: it replaces eight new FBM octaves, not the existing analytic normals.

## Reproducible performance measurements

Earlier revisions of this document used only `GL_TIME_ELAPSED` queries on
llvmpipe. Repeated draws with the same program can report substantially less
query time while their completed-render wall time stays unchanged, even with
fresh query objects and timestamp queries. Those old absolute times and ratios
are withdrawn as a performance baseline.

`benchmark_materials.py` now records both clocks. On llvmpipe/softpipe it defaults
to completed-draw wall time, with explicit `glFinish` boundaries; on hardware
renderers it defaults to GPU queries. Alternating sample order, multiple draws
per sample, raw samples, paired ratio quartiles, shader/data hashes and the
renderer/thread configuration are included in the JSON report. Software wall
time includes driver/CPU work: it is not a hardware GPU timing or a game FPS
prediction. No compiler optimization is accepted as faster just because its
source is shorter.

The benchmark loads historical shaders and authored data directly from Git
without switching or modifying the working branch:

```sh
python3 Tests/TerrainMaterials/benchmark_materials.py --size 256 --samples 17 --batch-draws 4 --lp-threads 1 --baseline-ref 5487e13 --json material-timings.json
```

Use `--baseline-ref 426d806` to compare with the earlier shared-noise materials,
or omit the option to compare with the original flat-color shader. These are
different comparisons: preserving the newer per-biome fields and normal recipes
is not equivalent to restoring the earlier material's smaller feature set.
`--current-ref` measures another historical candidate without a checkout change.
Compilation, allocation, uploads and image readbacks are outside the timed draws.

The near/four-biome scenario deliberately fills the viewport with a climate
junction and a ground/wall blend. Its relative cost is useful for detecting a
worst case; it does not measure how much of a gameplay frame that case occupies.
The complete color pass should also be profiled on the target GPU.

### November 20 conservative cleanup: budget still unresolved

The retained changes share lattice-axis hash products and the pixel footprint,
rotate the accumulated fBm gradient once, and omit parameter clamps already
guaranteed by `validTerrainAppearance` before upload. Computed-value safeguards
remain. These are algebraic simplifications, **not a demonstrated frame-time
improvement**. No texture cache, extra pass, derived GPU-table ABI or alternative
noise realization was added. Palettes, world periods and all climate weights
remain intact.

Compared with the published biome-specific implementation `5487e13`, using a
256² viewport, 16 warmup draws, 17 alternating sample pairs and four draws per
sample, the final paired median ratios were:

| Scenario | llvmpipe, one thread | llvmpipe, default threads |
|---|---:|---:|
| Nearby, one biome | 0.988 | 1.017 |
| Nearby, four biomes plus slope | 0.997 | 1.008 |
| Distant, four biomes | 0.986 | 0.974 |
| Coast, four biomes plus marine and slope | 0.996 | 0.987 |

The one-thread interquartile intervals all include 1.0. This does not establish
a meaningful overall speedup or resolve the requested performance budget.
A separate corrected control of `5487e13` versus the earlier shared-field
version `426d806` measured approximately 1.25× / 2.00× / 1.45× / 1.49× for these
four scenarios. The cost of independent per-biome fields is still present.

The validated expanded fragment SHA256 is
`c9a762c8bceb8225c8db7f73a0df9678c35b7a20c3d9eb56188ea76e8e4e991c`.
The optional historical oracle compares 886 authored and valid-parameter boundary
cases, including close-ups where the grain remains visible: color, position and
PBR outputs are identical; the largest normal-component difference is 2.09e-7.

```sh
python3 Tests/TerrainMaterials/check_revision_equivalence.py --baseline-ref 5487e13 --bounds
```

Common-body material loops, a flattened UBO, a larger CPU-derived GPU table,
simplex substitutes and explicit octave unrolling were not retained: they had
no reliable gain, were slower, or changed the appearance unfavorably.

## Validation

Run from the repository root:

```sh
python3 Tests/TerrainMaterials/check_cpu.py
python3 Tests/TerrainMaterials/check_gpu.py
python3 Tests/TerrainMaterials/check_runtime.py
python3 Tests/Atmosphere/run_all.py
python3 Tests/Terrain/check_scheduler.py
```

CPU checks cover the real authored data, ABI, invalid parameter rejection,
JSON persistence and revision-1 migration without losing customized biomes. Headless OpenGL checks exercise the production shaders and
their palette/PBR outputs, climate transitions, marine and forced biomes, gamma
round trips, normals and filtering. Independent finite differences check 512
base-noise gradients and 512 semantic-relief gradients. All 26 profiles are
exercised on ground, walls and cave ceilings. Existing renderer/shadow checks cover the
integration and resource lifecycle. These complement, rather than replace,
visual assessment in the Windows game under its actual lighting.

Sources informing the implementation:

- [John Edwards, Journey sand rendering, SIGGRAPH 2012](https://advances.realtimerendering.com/s2012/).
- [Ken Perlin, improved noise and smooth bump derivatives](https://developer.nvidia.com/gpugems/gpugems/part-i-natural-effects/chapter-5-implementing-improved-perlin-noise).
- [Simon Green, storage versus computation for procedural noise](https://developer.nvidia.com/gpugems/gpugems2/part-iii-high-quality-rendering/chapter-26-implementing-improved-perlin-noise).

The material recipes are an ENDURANCE adaptation, not a reproduction of
Journey's shaders. Dedicated sparkle lighting, displacement and additional
lighting models are outside this change.

Stable terrain debug/detail modes now use a bounded cache of compiled variants;
continuous authoring controls remain uniforms. See
[the second engine optimization pass](performance-engine-pass2.md) for the
cache contract and historical output comparison, including the normal-only
floating-point tolerance.
