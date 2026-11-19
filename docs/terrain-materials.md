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

The masks and materials reuse a bounded set of world-space fields:

1. Broad color regions, with a configurable global period in metres.
2. Intermediate patches, also shared by all participating biomes.
3. Directional strata and wind ripples, with periods, orientation and deformation
   configured per surface profile.
4. Fine grain close to the camera, evaluated only when its projected size is
   large enough to remain visible.

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
- The per-biome restore button restores that biome's authored material profiles.
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
| TerrainSurface, two materials and mask/pattern settings | 224 bytes |
| TerrainBiomeAppearance, ground/wall and slope settings | 464 bytes |
| All 26 biomes | 12,064 bytes |

The complete table fits the minimum OpenGL uniform block size. It is uploaded
only when edited; changing global controls only updates their uniforms. The
renderer restores the previous buffer binding, including offset and range.
Shadow rendering does not evaluate these materials.

The existing 12-byte packed vertex layout and batched terrain draws are retained.
Climate interpolation keeps all four matrix corners, with the existing smooth
weights, rather than selecting only the two dominant materials. The matrix IDs
come from the authoritative CPU biome matrix. Coast blending and forced-biome
inspection are preserved.

Noise evaluations are shared across the materials. The shader does not evaluate
a separate fBm for each biome, surface or layer. Scalar values and analytic
gradients are produced together. Quintic interpolation keeps the gradients
smooth at lattice boundaries. Procedural filtering uses the world-space pixel
footprint; it is calculated before divergent material selection. Unresolved
fine detail fades instead of producing distant shimmer.

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

The complete material-pass benchmark uses the actual G-buffer formats and a
256 x 256 viewport. Final medians on Mesa llvmpipe, with 17 alternating samples
after warmup, were:

| Scene | Previous flat colors | Procedural materials |
|---|---:|---:|
| Nearby, one biome | 0.429 ms | 1.022 ms |
| Nearby, four biomes and ground/wall blend | 0.596 ms | 3.753 ms |
| Distant, four biomes | 0.496 ms | 2.273 ms |
| Coast, four biomes plus marine and slope blend | 0.543 ms | 5.311 ms |

These software-renderer figures deliberately show the additional cost; they
must not be extrapolated to hardware frame rates. Allocation, compilation,
uploads and readback are outside the measured fragment pass. The retained
optimizations avoid reading inactive material records and duplicating inline
material evaluations. Their outputs were compared to the unoptimized version
over 54 varied cases, with zero difference on all four G-buffer attachments.

```sh
python3 Tests/TerrainMaterials/benchmark_materials.py --size 256 --samples 17
```

## Validation

Run from the repository root:

```sh
python3 Tests/TerrainMaterials/check_cpu.py
python3 Tests/TerrainMaterials/check_gpu.py
python3 Tests/TerrainMaterials/check_runtime.py
python3 Tests/Atmosphere/run_all.py
python3 Tests/Terrain/check_scheduler.py
```

CPU checks cover the real authored data, ABI, invalid parameter rejection and
JSON persistence. Headless OpenGL checks exercise the production shaders and
their palette/PBR outputs, climate transitions, marine and forced biomes, gamma
round trips, normals and filtering. Existing renderer/shadow checks cover the
integration and resource lifecycle. These complement, rather than replace,
visual assessment in the Windows game under its actual lighting.

Sources informing the implementation:

- [John Edwards, Journey sand rendering, SIGGRAPH 2012](https://advances.realtimerendering.com/s2012/).
- [Ken Perlin, improved noise and smooth bump derivatives](https://developer.nvidia.com/gpugems/gpugems/part-i-natural-effects/chapter-5-implementing-improved-perlin-noise).
- [Simon Green, storage versus computation for procedural noise](https://developer.nvidia.com/gpugems/gpugems2/part-iii-high-quality-rendering/chapter-26-implementing-improved-perlin-noise).

The material recipes are an ENDURANCE adaptation, not a reproduction of
Journey's shaders. Dedicated sparkle lighting, displacement and additional
lighting models are outside this change.
