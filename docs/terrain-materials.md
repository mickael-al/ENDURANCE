# Procedural terrain materials

ENDURANCE's terrain uses authored palettes and procedural surface patterns instead
of imported terrain textures. Material attributes are written into the existing
deferred G-buffer, so they use the same direct lights, shadow maps, atmosphere,
SSAO and environment reflections as other PBR objects.

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
| TerrainMaterial | 96 bytes |
| TerrainSurface, two materials and mask/pattern/normal settings | 240 bytes |
| TerrainBiomeAppearance, ground/wall, slope and spectrum settings | 512 bytes |
| All 26 biomes | 13,312 bytes |

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

## Precomputation investigation

A generated RGBA16F 3D noise volume, storing value and gradient with mipmaps, was
implemented as a standalone benchmark. On the available Mesa llvmpipe software
renderer it was slower than analytic evaluation, while also approximating the
gradient and introducing a finite repeat period. It is not part of production.
See [noise measurements and primary references](terrain-material-noise.md).

These measurements are not hardware GPU frame-time claims. Procedural materials
do more work than the previous flat colors; their complete material pass should
also be profiled on the target GPU. The benchmark is retained for reproducing
that comparison without adding a production cache or extra resource lifecycle.

The initial revision-1 material-pass benchmark used the actual G-buffer formats and a
256 x 256 viewport. Revision-1 medians on Mesa llvmpipe, with 17 alternating samples
after warmup, were:

| Scene | Previous flat colors | Revision-1 materials |
|---|---:|---:|
| Nearby, one biome | 0.429 ms | 1.022 ms |
| Nearby, four biomes and ground/wall blend | 0.596 ms | 3.753 ms |
| Distant, four biomes | 0.496 ms | 2.273 ms |
| Coast, four biomes plus marine and slope blend | 0.543 ms | 5.311 ms |

These software-renderer figures deliberately show the additional cost; they
must not be extrapolated to hardware frame rates. Allocation, compilation,
uploads and readback are outside the measured fragment pass. The original
optimizations avoid reading inactive material records and duplicating inline
material evaluations. Their outputs were compared to the unoptimized version
over 54 varied cases, with zero difference on all four G-buffer attachments.

```sh
python3 Tests/TerrainMaterials/benchmark_materials.py --size 256 --samples 17
```

## Material revision 2 measurements

The biome-specific normal families and independently scaled fields cost more
than revision 1's three universal noises. In a sequential run on the same Mesa
llvmpipe renderer, with the same 256 x 256 G-buffer targets, eight warmups and
17 alternating samples, the complete passes measured:

| Scene | Published revision 1 (`426d806`) | Revision 2 | Ratio |
|---|---:|---:|---:|
| Nearby, one biome | 0.825 ms | 1.003 ms | 1.22x |
| Nearby, four biomes and ground/wall blend | 3.155 ms | 6.433 ms | 2.04x |
| Distant, four biomes | 1.773 ms | 2.557 ms | 1.44x |
| Coast, four biomes plus marine and slope blend | 5.152 ms | 7.941 ms | 1.54x |

This is an explicit quality/cost tradeoff, **not a speed improvement**. The
four-biome case deliberately covers the whole viewport with a climate junction
and a ground/wall transition; it does not measure their coverage in a gameplay
frame. Software-renderer samples vary and these numbers are not hardware FPS
predictions. Profile the material pass on the target GPU before choosing the
shipping quality budget.

Work is shared within each biome; zero climate weights skip that biome, invisible
fine octaves skip noise hashes, unresolved waves skip sine/cosine, and the macro
field is shared globally. No new textures, render targets or per-biome draws are
introduced. The table grows from 12,064 to 13,312 bytes. Four climate contributions
are retained even in the most expensive transition rather than dropping small
weights and creating visible boundaries.

The benchmark can load historical shaders and authored data directly from Git
into a temporary directory without switching or modifying the working branch:

```sh
python3 Tests/TerrainMaterials/benchmark_materials.py --size 256 --samples 17 --baseline-ref 426d806 --json material-timings.json
```

The comparison validates the historical CPU table and expands all historical
shader includes before timing. Compilation, allocation, uploads and readbacks
remain outside the measured draw intervals.

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
