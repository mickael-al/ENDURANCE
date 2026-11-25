# Procedural terrain materials

ENDURANCE's terrain uses authored palettes and procedural surface patterns. Sand
and snow additionally use sparse, procedural screen-sized sparkles. Material
attributes are written into the existing deferred G-buffer, so they use the same
direct lights, shadow maps, atmosphere, SSAO and environment reflections as other
PBR objects.

## Rendering source map

The terrain color pass has **one vertex/fragment program**. The GLSL includes
below are functions compiled into that program, not additional rendering passes.

| File | Responsibility / entry points |
|---|---|
| `Asset/Shader/ProceduralTerrain/render.fs.glsl` | Fragment `main`: material evaluation, final normal and G-buffer writes |
| `Asset/Shader/ProceduralTerrain/materials.glsl` | `terrainEvaluateAppearance` → biome → surface → material: climate, slope, palettes and PBR blending |
| `Asset/Shader/ProceduralTerrain/patterns.glsl` | Shared noise/value gradients, grain spectrum and the eight procedural relief families |
| `Asset/Shader/ProceduralTerrain/fine_surface.glsl` | Filtered stain masks, world projection and sparkle uniforms after material blending |
| `Asset/Shader/ProceduralTerrain/sparkle.glsl` | Fixed jittered sparkle centers, screen coverage, grid transitions and the normal/roughness adapter |
| `ANTAGONIZ/Header/Miscellaneous/ProceduralTerrainFineSurface.hpp` | One-time C++ stain-mask bake |
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
| x | Strength of the selected directional facets; zero disables it | 0.95 snow, 0.90 sand |
| y | Grain roughness | 0.12 snow, 0.15 sand |
| z | Fractional linear albedo darkening in stains | 0.10, i.e. 10% |
| w | Absolute roughness reduction in stains | 0.06 |

The game classifies **each material**, including accents and walls, once in
`configureTerrainFineMaterials`. Pale neutral materials in the ten cold biomes
receive snow/frost grain; materials authored as sand receive sand grain. For
example, FrozenTundra's secondary snow receives glints, its primary gray sediment
does not. Green palette midpoints select the larger stain scale, including green
wall patinas; other non-granular materials get the finer scale. Sand and snow
receive no discrete FBM stains, even on accent/wall layers. Classification uses linear
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
| `sparkleGrainSize` / Espacement minimal des eclats | 0.01 m | Minimum world-grid spacing; the actual spacing grows with the projected pixel footprint |

The sizes describe base features/spacing, not an exact diameter of irregular
patches. These controls change uniforms without remeshing or rebaking anything.
They are independent of the biome relief noise's period and octaves.

The sand/snow path is a **procedural adaptation of Wang and Bowles' sparkle
method**, implemented in `sparkle.glsl`. The PNG pair, its C++ loader, packed CPU
image and GPU texture have been removed. This is an artistic point effect, not a
physical simulation of individual sand grains or a reproduction of the paper's
lighting integration.

The shader retains the paper's useful distinctions between fixed centers,
compact shapes, adaptive spacing and grazing-angle coverage. It deliberately
omits their view-vector grid displacement: moving the camera never offsets a
sparkle center. Unlike the paper's stepped-only grid, this version crossfades two
stationary dyadic grids to soften scale transitions.

- Projected world coordinates identify a cell at each of two grid levels. One
  integer hash per level, salted with the world seed and level, chooses a center
  in the range **0.28–0.72** of each cell axis. Chunk IDs and frame numbers do not
  enter this calculation. Changing levels changes the visible population
  gradually; it does not continuously slide either grid over the terrain.
- A separable cubic B-spline provides compact coverage around each center. Its
  analytic integral over each pixel preserves subpixel coverage when a small
  point moves between pixel centers. At the default scale of 0.5, the filtered
  support is +/-1.5 screen pixels in each axis. It does not average an entire
  noise texture into a uniform normal or create contours by thresholding an
  interpolated normal map.
- The full **2x2 world-to-screen metric** uses derivatives captured in
  `render.fs.glsl` before divergent material/reflection branches. It controls
  apparent shape at grazing angles. Degenerate projections are rejected before
  matrix inversion; this numerical guard is not a view-distance cutoff.
- Grid spacing is a power-of-two multiple of `sparkleGrainSize`. This setting is
  the true minimum spacing at the finest level, not a texel size or a pixel
  diameter. Screen footprint selects the level, so distant points remain sparse
  rather than accumulating into a solid band. The two levels are blended with
  `smoothstep` across the fractional level.
- The center margin and spacing bound contain the entire compact kernel within
  its cell. Thus there are **two cell hashes and no neighbor search**, rather
  than an unbounded search over all microscopic grains under a pixel.
- A per-cell random facet direction gives a broad angular brightness response
  (`smoothstep(0.86, 0.995, dot(facet, H))`). It changes participation, not the
  center or compact shape. Random amplitude, material strength and the broad
  reflection envelope keep the surface from becoming uniformly glossy.

Three constants in `sparkle.glsl` control the screen appearance:

| Constant | Default | Meaning |
|---|---:|---|
| `TERRAIN_SPARKLE_WIDTH` | 0.5 | Base kernel screen scale, halved from the initial procedural version; pixel integration limits aliasing of the smaller points |
| `TERRAIN_SPARKLE_SPACING` | 8.0 | Minimum lower-grid spacing relative to the full pixel footprint; larger values make points sparser |
| `TERRAIN_SPARKLE_JITTER` | 0.44 | Center displacement range within a cell, increased from 0.28; centers now span 0.28–0.72 on each axis |

The width is an approximate coverage target, not a guarantee that each final
lit component occupies exactly one pixel. Tone mapping, exposure, bloom and the
PBR adapter affect perceived size. Halving the base kernel width does not halve
every final lit diameter after pixel integration. Spacing is independent of
width at the defaults, preserving the previous grid population. To contain the
kernel without neighbor evaluation, the effective spacing factor is clamped to
`max(SPACING, sqrt(2) * (2 * WIDTH + 0.5) / margin)`, where
`margin = (1 - JITTER) / 2`. Keep `WIDTH > 0` and `0 <= JITTER < 1`.
Larger width or jitter can therefore increase the required grid spacing.
Material `fineSurface.x` and `.y` continue to control strength and selected-facet
roughness independently of these shader constants.

The terrain draw receives the current camera position and the normalized sun
vector from `Atmosphere::primarySpecularDirection()` (CPU state only). Outside
an active atmosphere this selects the first active directional light. At night,
with an active atmosphere, it returns zero: the existing lunar IBL is preserved.
Neither forward shader is changed.

Before writing the G-buffer, the terrain evaluates the light/view half vector.
The existing angular envelope limits the effect to the reflected-sun region
(`smoothstep(0.80, 0.98, dot(normal, H))`) and skips both grid evaluations outside
it. Light/view visibility, material strength and the filtered point coverage
form the selection weight.

The selected G-buffer normal approaches **H**, with a short smooth edge ramp.
Roughness blends approximate GGX peak powers, proportional to **1/r^4**, before
conversion back to roughness; a direct linear roughness blend would sharpen the
filtered coverage into narrow peaks again. The effective target roughness has a
0.10 floor, and cannot exceed the substrate roughness. The broad random-facet
response chooses participation; it is not written directly as the final normal.

This adapter is a deliberate material-side approximation: it cannot introduce
an independent BRDF lobe in the unchanged deferred shader. Diffuse lighting,
IBL and other lights still see the selected G-buffer normal/roughness. The
power blend approximates peak behavior, not an exact integral of final lighting
or an energy-conserving blend of two complete materials. Its visual stability
must therefore be checked through the actual lighting shader. Sun shadow/cloud
transmission paths continue to shade the facets, and no light is painted into
albedo or emission.

Only the ordinary G-buffer normal and roughness change. **Forward/deferred
lighting shaders, G-buffer formats/tags, passes and light/shadow calculations
remain unchanged.** Granular layers still skip generic smooth micro-normal/
roughness noise and FBM stains, including saved stain controls. Other materials,
macro color/ripple masks, geometry and the separate geometric-normal tag for
shadow bias are preserved. There is no distance cutoff and no grain texture or
mipmap chain. The screen footprint adapts grid spacing and shape instead of
fading all grains out at a chosen distance.

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

Fine-detail GPU memory is now **349,525 bytes** (about 0.333 MiB) for the
512² R8 stain cache including mipmaps. Removing the packed 1024² RGBA8 grain
texture saves **4 MiB of GPU memory** and the shared 4 MiB packed CPU image.
There is no grain texture allocation, PNG decode, upload or texture sampling.
The stain CPU bake retains 256 KiB. Material edits and chunk generation do not
rebake this cache. Shutdown/failure cleanup frees the stain texture; borrowed
texture unit 0, sampler, UBO range and pixel-unpack state are restored. Texture
unit 1 is no longer borrowed. No extra draw/generation pass or G-buffer
attachment is added; the material UBO stays at 15,392 bytes. These resource
savings do not by themselves establish a runtime speedup over the former single
bilinear texture fetch.

Scene revision 5 removes saved discrete stain controls from sand and snow,
including previously customized stain strengths, as explicitly requested. Other
fine controls, palettes, roughness, global sizes and v4 routing are retained.
Revision 6 changes saved factory grain sizes of 0.01 m to 0.001 m, while keeping
custom sizes. Already-v5 palettes and effect controls are not reclassified.
Revision 7 doubles the 0.001 m factory size to 0.002 m in saved scenes. Older
factory settings migrate through both steps; other custom sizes remain intact.
The older migrations still apply to old scenes. No binary terrain scene is
replaced. `TERRAIN_FINE_SURFACE_ENABLED=0` compiles out the fine effects for A/B
comparisons and restores the ordinary procedural-normal path.

Revision 8 historically enlarged the PNG grain scale **fivefold**, including
artist values, capped at the existing 1 m validation bound. It still runs once
when loading pre-v8 scenes. New scenes default to 0.01 m and saved v8 values
remain unchanged. The procedural replacement reuses this serialized setting as
minimum grid spacing; it does not introduce a further scene migration. Palettes,
geometry and stain sizes are not changed by this adaptation.

### Validation and timing

`Tests/TerrainMaterials/check_gpu.py` exercises the production procedural
shader: sparse coverage, world/seed anchoring, normalized directions, unchanged
albedo/geometry/AO/metallic, material boundaries, stain isolation, distant
coverage and static disable. Generic smooth-relief minification is checked
separately. `check_runtime.py` covers the single stain texture, state restoration,
live edits and camera/light updates without remeshing, restart and failure
cleanup. `check_grain_assets.py` now reconstructs the removed PNG fixture from
Git history only when a historical rendering comparison requests it; it is not
a production asset loader or a validation of current sparkle shapes.

`Tests/TerrainMaterials/check_granular_render.py` sends production terrain
fragment output through the **unchanged** production deferred shader, using a
perspective plane and the engine's RG32F/RGB16F/RGBA16F/RGB8 formats. It checks
HDR highlights, their response to sun direction, real shadow-map blocking,
absence of emission and the shared snow/sand path with IBL.

`Tests/TerrainMaterials/check_grain_motion.py` is a separate diagnostic for
close-up lateral motion, distant lateral motion and approaching the surface.
It compares actual deferred output to a finite 4x4 spatial sampling reference,
retaining the target-resolution footprint/grid scale in the reference. It
reprojects sampling errors through the world plane and normalizes temporal
residuals by reference glint RMS, while also reporting brightness, coverage
and bright-component shape. This avoids interpreting simple dimming as a
stability improvement. It is not a proof of zero aliasing or a game capture.

```sh
python3 Tests/TerrainMaterials/run_all.py
python3 Tests/TerrainMaterials/check_grain_motion.py --compare-ref ee47948
```

The initial procedural version `52c04cc` was compared against `ee47948` on
November 25 using eight frames per scenario, a 160x100 target and a 4x4 HDR
reference. Normalized temporal residuals were:

| Scenario | Initial procedural (`52c04cc`) | Previous PNG path |
|---|---:|---:|
| Close-up lateral motion | 0.216 | 0.239 |
| Distant lateral motion | 0.236 | 1.931 |
| Approach | 0.279 | 1.765 |

These are errors against the finite reference, not percentages of visible
flicker removed from the game. New bright components are fewer and more compact
(median four pixels above the diagnostic HDR threshold), with brighter
individual spots and comparable bright area. The material appearance deliberately
changes; this is not a pixel-identical replacement. The reference glint RMS is
not reduced. Full metrics, shader hash and settings are in
[the motion report](terrain-material-validation/procedural-sparkle-motion-2026-11-25.json).

The subsequent smaller-point adjustment halves the base kernel scale, widens
the center jitter range and analytically integrates the kernel over each pixel.
The same diagnostic against `52c04cc` measures median bright-component area of
two pixels instead of four in both lateral-motion cases, with similar component
counts (14 vs 15 near; 18 vs 19 far). The smaller points are more sensitive to
sampling: normalized temporal residuals rise from 0.216 to 0.297 near, 0.236 to
0.335 far, and 0.279 to 0.393 during approach. Absolute temporal residuals rise
by about 7–9%; reference glint RMS also decreases. Pixel integration does not
make the nonlinear material/lighting response alias-free. Full measurements are
in [the smaller-point motion report](terrain-material-validation/smaller-sparkle-motion-2026-11-25.json).

The initial version's same-day budget comparison against `ee47948` uses a 256x256 target,
15 alternating sample pairs, three draws per sample and one llvmpipe thread.
Completed-draw paired median ratios (new/old) are **1.098** for sand sparkles,
**1.018** for ordinary stains and **1.023** for distant four-biome terrain.
The sparkle ratio interquartile range is 1.002–1.166. This is software-renderer
material-pass cost, not whole-frame cost or GTX 1660 performance. Removing the
texture saves memory but does not establish a speedup. Raw samples and source
hashes are in [the budget report](terrain-material-validation/procedural-sparkle-budget-2026-11-25.json).
These timings predate the smaller-point adjustment and are not a measurement of
its pixel-integrated kernel.

The motion diagnostic and optional timing experiments are separate from the
normal validation runner. GTX 1660 frame cost and the result under actual game
exposure still need in-game validation. No runtime improvement is inferred from
the removal of textures. All timings below are **historical** and must not be
used as the procedural sparkle adaptation's performance estimate.

The November 25 comparison against published revision `1ff2aec` uses a 256²
viewport, 15 alternating sample pairs, three draws per sample and one llvmpipe
thread. Completed-draw wall-time paired median ratios (new/previous) are:

| Scenario | Ratio |
|---|---:|
| Nearby sand grains | 0.778 |
| Nearby rock stains | 1.031 |
| Nearby, one biome | 1.009 |
| Nearby, four biomes plus slope | 1.000 |
| Distant, four biomes | 1.010 |
| Coast with slope and marine blend | 0.995 |

That historical grain case improved by about 22% in its software test after
removing its texture fetch, stains and smooth micro-normal evaluation. Other cases range
from -0.5% to +3.1%; no general or target-GPU speedup is claimed. Raw timings,
quartiles, renderer configuration and source hashes are in
[the comparison report](terrain-material-validation/white-noise-grains-llvmpipe-2026-11-25.json).
Use `--baseline-ref 1ff2aec` to reproduce this comparison. Earlier reports in
the same directory remain historical evidence for their own revisions.

The earlier v4 results below remain a historical comparison only:
`docs/terrain-material-validation/revised-fine-surfaces-llvmpipe-2026-11-24.json`.
The procedural sparkle replacement should be compared with the preceding PNG implementation
`ee47948`. Tests and software measurements do not establish target-GPU frame
time or the final in-game look.

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

- [Beibei Wang and Huw Bowles, A Robust and Flexible Real-Time Sparkle Effect, EGSR 2016](https://doi.org/10.2312/sre.20161209), especially sections 3.1, 3.2 and 3.5.
- [Wang and Bowles, sparkle presentation and original shader example, SIGGRAPH 2015](https://advances.realtimerendering.com/s2015/).
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
