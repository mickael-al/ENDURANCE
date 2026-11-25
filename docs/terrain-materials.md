# Procedural terrain materials

ENDURANCE's terrain uses authored palettes and procedural surface patterns. Sand
and snow additionally use a shared noise/normal PNG pair. Material attributes
are written into the existing deferred G-buffer, so they use the same direct lights, shadow maps, atmosphere,
SSAO and environment reflections as other PBR objects.

## Rendering source map

The terrain color pass has **one vertex/fragment program**. The GLSL includes
below are functions compiled into that program, not additional rendering passes.

| File | Responsibility / entry points |
|---|---|
| `Asset/Shader/ProceduralTerrain/render.fs.glsl` | Fragment `main`: material evaluation, final normal and G-buffer writes |
| `Asset/Shader/ProceduralTerrain/materials.glsl` | `terrainEvaluateAppearance` → biome → surface → material: climate, slope, palettes and PBR blending |
| `Asset/Shader/ProceduralTerrain/patterns.glsl` | Shared noise/value gradients, grain spectrum and the eight procedural relief families |
| `Asset/Shader/ProceduralTerrain/fine_surface.glsl` | Filtered stain masks and shared sand/snow normal/noise texture after material blending |
| `ANTAGONIZ/Header/Miscellaneous/ProceduralTerrainFineSurface.hpp` | One-time C++ stain bake and the packed grain image data contract |
| `Asset/Shader/ProceduralTerrain/render.vs.glsl` | Packed terrain vertex decoding, world position and climate varyings |
| `ANTAGONIZ/Source/ProjectUser/EnduranceTerrainGrain.cpp` | Decode/cache the two original PNGs and pack RGB normals + A noise once |
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
| `sparkleGrainSize` / Taille grains brillants | 0.01 m | Metres per source texel; a 1024-pixel tile repeats every 10.24 m |

The sizes describe base features/spacing, not an exact diameter of irregular
patches. These controls change uniforms without remeshing or rebaking anything.
They are independent of the biome relief noise's period and octaves.

The sand/snow path now uses the **two supplied 1024² PNGs**, without regenerating
normals or random facets:

- `ENDURANCE/Ressources/Texture/Terrain/grain.png`: the original white noise.
- `ENDURANCE/Ressources/Texture/Terrain/NormalMap.png`: the matching normal map.

`EnduranceTerrainGrain.cpp` decodes the pair once per process. It preserves all
source bytes, packs the normal RGB and noise into alpha, and supplies that shared
CPU image to the renderer. The renderer uploads one linear **RGBA8** texture, not
an sRGB/color texture. No new `STB_IMAGE_IMPLEMENTATION` is added to the engine;
its existing implementation remains in `TextureManager.cpp`.

`terrainApplyGranularSurface` makes **one repeating texture lookup with mipmaps**
after material blending. Both maps always share UVs. The world projection and a
seeded offset are independent of chunk origin, LOD, camera and frame number.
`terrainSparkleGrainSize` describes one source texel, not the whole image. The
finite noise tile is intentional; no anti-tiling cost is added.

**Asset normal encoding matters:** RG store XY remapped to [0,1], but B stores
positive Z directly. This particular normal map must be decoded as
`vec3(sample.rg*2-1, sample.b)`, then normalized after filtering. Decoding all three
channels with `*2-1` would point most of these normals into the surface. The
shader maps the perturbation along the same projection axes as the images and
projects it onto the terrain tangent plane. The authored strength of this very
steep source is moderated by `normalStrength = 0.35` in `fine_surface.glsl`.
The orientation is fixed in world space; selection strength is blended through ground/accent/biome transitions.

The terrain draw receives the current camera position and the normalized sun
vector from `Atmosphere::primarySpecularDirection()` (CPU state only). Outside
an active atmosphere this selects the first active directional light. At night,
with an active atmosphere, it returns zero: the existing lunar IBL is preserved.
Neither forward shader is changed.

Before writing the G-buffer, the terrain evaluates the light/view half vector.
An angular envelope limits fine highlights to the reflected-sun region
(`smoothstep(0.80, 0.98, dot(normal, H))`), and skips the texture lookup elsewhere.
The image's facet normal then selects individual reflections
(`smoothstep(0.990, 0.9995, dot(facetNormal, H))`). The white-noise image controls
participation. Only these selected grains receive the normal perturbation and
lower roughness (`fineSurface.y`); the remaining substrate keeps its authored
normal/roughness. `fineSurface.x` blends this response through material transitions.

This is a deliberate material-side approximation: it cannot introduce an
independent BRDF lobe in the unchanged deferred shader. IBL and other lights also
see the selected G-buffer normal/roughness; exact per-light isolation would
require modifying the lighting shader. No illumination is baked into albedo,
and the existing sun shadow/cloud transmission paths still shade the facets.
The selection is evaluated each terrain draw, because moving the camera or the
sun changes which static grains reflect towards the viewer. Textures are never
rebaked, reuploaded or resized on camera/light movement.

Only the ordinary G-buffer normal and roughness change. **Forward/deferred
lighting shaders, G-buffer formats/tags, passes and light/shadow calculations
remain unchanged.** Nothing is painted into albedo or emission. Granular layers
still skip generic smooth micro-normal/roughness noise and FBM stains, including
saved stain controls. Other materials, macro color/ripple masks, geometry and
the separate geometric-normal tag for shadow bias are preserved.

The original PNG packing, facet selection, normals, roughness and grain scale
from `39a88fa` are restored. The compact-grain bake, slope-moment filtering and
harmonic roughness adjustment from `b28b853` are removed.

The only rendering addition is a complete mip chain, generated once at upload,
with `GL_LINEAR_MIPMAP_LINEAR`, `GL_LINEAR` magnification and `GL_REPEAT`.
`textureGrad` uses world derivatives captured before material branches, so
minification selects and blends the appropriate levels instead of forcing mip 0.
There is **no distance cutoff**, added anisotropy, extra rendering pass or new
material setting. Ordinary normal-map mip averaging softens distant grain; it
is not a statistical glint filter or a guarantee of zero temporal aliasing.

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

GPU memory for the packed 1024² RGBA8 grain pair is **5,592,404 bytes** (about 5.333 MiB) including its 11 mip levels,
plus **349,525 bytes** (about 0.333 MiB) for the stain cache including mipmaps.
The game retains one shared 4 MiB packed CPU image across renderer restarts; the
original decoded PNG arrays are freed after packing. The stain CPU bake retains
256 KiB. Neither chunk generation nor material edits decode/upload these images.
Initialization validates dimensions and data lengths; shutdown/failure frees
both GPU textures. Borrowed texture units 0/1, samplers, UBO range and pixel-unpack
state are restored. No new draw/generation pass or G-buffer attachment is added;
the material UBO stays at 15,392 bytes. Texture-free engine callers use a neutral
1x1 fallback and do not depend on game asset paths.

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

Revision 8 enlarges the previously configured grain scale **fivefold**, including
artist values, capped at the existing 1 m/texel validation bound. It runs once
on scene load; new scenes default to 0.01 m and saved v8 scenes are unchanged.
Palettes, geometry and stain sizes are not changed by this migration.

### Validation and timing

`Tests/TerrainMaterials/check_grain_assets.py` compiles the actual game PNG
loader, checks its process cache, and compares every packed byte to an independent
PNG decode. `check_gpu.py` feeds those same production bytes into actual OpenGL
shaders on Mesa llvmpipe (software rendering). It checks source-pixel normal
encoding/UV alignment, bilinear magnification, trilinear minification/repeat boundaries, normalized directions,
seed/LOD stability, stable large-footprint averages, unaffected albedo/geometry/AO/
metallic, stain isolation and static disable. Generic smooth-relief minification
is checked separately because granular normals intentionally stay active at range.
`check_runtime.py` checks dimension/data validation, uploads, state restoration,
live edits without remeshing, restart and failure cleanup.

`Tests/TerrainMaterials/check_granular_render.py` sends production terrain
fragment output through the **unchanged** production deferred shader, using a
perspective plane and the engine's RG32F/RGB16F/RGBA16F/RGB8 formats. It checks
HDR highlights, their response to sun direction, real shadow-map blocking,
absence of emission and the shared snow/sand path with IBL. Its optional preview
compares against an earlier commit under identical controlled lighting; it is
not an in-game capture or a target-GPU performance measurement.

GTX 1660 frame cost and the result under actual game exposure still need in-game
validation. The timings below predate this PNG-based replacement and must **not**
be used as its performance estimate.

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

The grain case improves by about 22% in this software test after removing its
texture fetch, stains and smooth micro-normal evaluation. Other cases range
from -0.5% to +3.1%; no general or target-GPU speedup is claimed. Raw timings,
quartiles, renderer configuration and source hashes are in
[the comparison report](terrain-material-validation/white-noise-grains-llvmpipe-2026-11-25.json).
Use `--baseline-ref 1ff2aec` to reproduce this comparison. Earlier reports in
the same directory remain historical evidence for their own revisions.

The earlier v4 results below remain a historical comparison only:
`docs/terrain-material-validation/revised-fine-surfaces-llvmpipe-2026-11-24.json`.
For this correction, compare against `--baseline-ref 1ff2aec`. Tests and software
measurements do not establish target-GPU frame time or the final in-game look.

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
