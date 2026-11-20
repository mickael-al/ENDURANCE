# Terrain material patterns, filtering, and noise storage

The revised materials separate broad color variation from local surface relief.
A color patch no longer automatically produces a rounded bump. A surface selects
a normal family appropriate to its material, while its base and accent materials
control palette, roughness, and normal strength. This addresses the repeated
half-metre pockmarks produced by the initial shared grain and by converting
macro/patch color gradients directly into bump gradients.

## Field sharing and stable coordinates

One broad macro field is shared across the fragment. Each contributing biome
then evaluates its own intermediate field and fine-detail spectrum once; ground,
wall, base, and accent reuse that biome context. This gives different biomes
different scales without evaluating an independent fBm for every material layer.
The intermediate period is the global patch period multiplied by the biome's
`spectrum.x`. The other spectrum components select the fine-grain period in
metres, a fractional octave count from 1 to 3, and gain from 0 to 0.5.

Climate blending combines the **evaluated results** from these stable domains.
It must not first interpolate two frequencies and then evaluate noise at
`worldPosition * mixedFrequency`. In that alternative, even a small change of
climate weight changes phase proportionally to the absolute world position;
patterns can stretch or move through the biome transition. Evaluating each
biome at its own fixed world-space scale and blending its values/gradients keeps
the material attached to the terrain. The four climate-corner weights remain
available rather than dropping secondary biomes to hide the additional work.

Computing scalar value and analytic gradient together reuses the same eight
corner hashes; finite-difference normal sampling is unnecessary. The elementary
field is **value noise**, not Perlin gradient noise. Its interpolation is quintic:

```glsl
vec3 blend = f*f*f*(f*(f*6.0 - 15.0) + 10.0);
vec3 derivative = 30.0*f*f*(f - 1.0)*(f - 1.0);
```

This avoids the second-derivative discontinuity of cubic interpolation at cell
boundaries. It improves bump continuity but does not, by itself, make a material
look like stone, snow, or soil. Increasing the octave count alone also does not
provide that identity.

## Independent normal families

Each ground/wall surface has `normal = (family, featurePeriod, warp, grainMix)`.
The family is an integer ID from 0 to 7, the feature period is in metres, warp
controls distortion, and grain mix controls the contribution of the biome's
fine-detail spectrum. The existing orientation and material normal strengths
remain separate artistic controls. Feature relief and fine grain are not the
same scale, and neither is tied to the size of the broad albedo patches.

| ID | Family | Intended material cues |
|---:|---|---|
| 0 | Grain | Restrained granular soil, gravel, frost crust, or cinders |
| 1 | Wind ribs | Directional sand/current ripples or longer snow ridges |
| 2 | Bedding | Tilted, distorted sediment layers on exposed rock |
| 3 | Fractures | Sparse mineral grooves and chipped or cracked crust |
| 4 | Organic | Fine fibrous ground cover and decomposed litter |
| 5 | Wet sediment | Mostly calm surfaces with low folds and fine silt |
| 6 | Ice | Smooth areas interrupted by restrained veins or grooves |
| 7 | Lava | Folded, corded cooled crust, distinct from fine volcanic ash |

These are lightweight artistic approximations, not simulations of erosion,
vegetation, or lava flow. One family can serve several biomes when periods,
orientation, grain spectrum, amplitude, and roughness differ. For example,
marine current ripples and red-desert sand share a directional construction;
they do not need identical scale, roughness, palette, or orientation. The
complete authored mapping is in `TerrainMaterialsPalette.md`.

Normal contributions are combined as surface gradients and normalized only
after composition. This permits smooth material/biome mixtures without blending
encoded normal-map colors or introducing a tangent-space requirement for caves
and cliffs. The geometric normal still drives ground/wall classification.

## Detail filtering and octave budget

World-position derivatives are captured before material/climate branches. They
provide an explicit pixel footprint for all subsequent procedural evaluations;
implicit derivatives inside nonuniform branches would be undefined in GLSL.
Detail at frequencies too high for that footprint fades toward its mean value
and zero gradient. The last partially visible octave fades gradually rather
than appearing or disappearing in one step. Fractional authored octave counts
also allow a partial contribution from the highest requested octave.

For a height spectrum `h(p) = sum(gain^i * noise(lacunarity^i * p))`, each octave's
gradient contains an additional `lacunarity^i` factor. Thus lacunarity 2 and gain
0.5 give approximately equal slope amplitude at every octave, even though the
height amplitude decreases. Increasing octaves with those values can make
normals unnecessarily busy and prone to shimmer. Restrained authored gains,
typically around 0.28–0.42, leave more of the fine appearance to roughness while
keeping the larger material cue legible. These are artistic starting points,
not universal measured physical constants.

The maximum of three octaves limits possible shader cost; footprint rejection
avoids evaluating invisible fine detail. Distorted waves and narrow grooves
still need their own filtering: filtering the input noise alone does not
band-limit nonlinear operations applied afterward. Preview both close surfaces
and distant oblique views when changing feature width or contrast.

## Historical generated-cache experiment

Before the independent-family revision, a generated 3D cache was compared with
the earlier shared analytic fields. It was not adopted. The isolated prototype
remains in `Tests/TerrainMaterials/benchmark_noise.py`; it is not a runtime
resource or an optional production rendering path. Its original llvmpipe GPU
query timings are withdrawn: the material benchmark subsequently exposed
software-driver timer under-counting. The numerical quality measurements below
remain separate from that timing problem. The original benchmark's timing mode
must not be used to infer a hardware performance advantage for either path.

A bounded follow-up used completed-draw wall time with `glFinish` boundaries,
one llvmpipe thread, a 384² target, seven alternating sample pairs and eight
draws per sample. The 64³ cache remained slower than the current nonperiodic
analytic primitive: 1.242 versus 0.860 ms for one field, and 3.129 versus
1.822 ms for three fields. This software result does not predict hardware GPU
performance; it gives no measured reason to add a cache and its approximation
to the present implementation.

The test generates a seeded periodic value-noise volume on the GPU, at texel
centers, with 16 noise cells along each axis. RGBA16F stores scalar value in R
and signed analytic derivatives with respect to noise-cell coordinates in GBA.
All axes use repeat wrapping, linear magnification, and trilinear mip filtering.
Mips are generated after the compute pass and its memory barrier. There are no
imported image assets or GPU-to-CPU transfers in the proposed runtime approach;
blocking readbacks in the benchmark are validation instrumentation only.

One lookup supplies both the scalar and gradient. However, interpolation of
stored gradients is an approximation, and is not exactly the derivative of the
linearly interpolated scalar channel. Higher-resolution volumes reduce that
error at a cubic memory cost. The last mip approaches the finite-volume mean
(close to 0.5, not necessarily exactly 0.5) and a zero mean gradient.

| Volume | Texels per noise cell | Memory including every mip |
|---|---:|---:|
| 64³ RGBA16F | 4 | 2.286 MiB |
| 128³ RGBA16F | 8 | 18.286 MiB |

The existing atmosphere noise volume has different semantics: its channels
contain cloud/fog scalar fields, not a value plus its gradient. Reusing that
resource would not directly preserve the terrain's analytic normal detail.

## Reproducing the historical cache measurements

From the repository root, on a Linux environment with EGL and desktop OpenGL:

```sh
python Tests/TerrainMaterials/benchmark_noise.py --samples 7 --warmup 2 --size 512 --cache-sizes 64
```

Quintic interpolation is the default. Use `--interpolation cubic` for the
previous interpolant, `--cache-sizes 64 128` to compare resolutions, or
`--quality-only` to skip timing. The script requires only Python's standard
library and the repository's existing headless OpenGL harness.

Allocation, shader compilation, noise-volume generation, mip generation, and
readbacks are outside the historical fragment timings. Both paths used the same
seeded periodic quintic function; the analytic baseline therefore included
coordinate wrapping that the nonperiodic production field does not need.
The texture version also uses actual mip filtering, so numerical quality is
assessed separately at LOD 0 rather than inferred from timings.

Quintic 64³ quality measurements, against direct evaluation at deterministic
points including negative coordinates and period boundaries:

| Metric | Result |
|---|---:|
| Scalar RMS error / maximum error | 0.014752 / 0.077049 |
| Gradient-component RMS error / maximum error | 0.047429 / 0.282312 |
| Maximum repeated-coordinate difference | 0.00000751 |
| Maximum delta across ±0.00001 at a period seam | 0.00003435 |

All sampled outputs and all mip texels were finite. Scalar variance decreased
through the mip chain; gradient RMS per mip was 0.38101, 0.30676, 0.20418,
0.07876, 0.02589, 0.00634, and approximately zero. Seam continuity does not imply
identical visual quality: the approximation errors above remain present.

## References and scope

- [Pharr, Jakob, and Humphreys, PBRT: Noise](https://pbr-book.org/3ed-2018/Texture/Noise)
  describes value noise, fBm, and footprint-limited octave summation with a smooth
  final-octave transition. The three-octave limit and authored gains here are
  project choices; the reference does not claim that this exact budget is
  optimal for every GPU or material.
- [Morten S. Mikkelsen, Surface Gradient–Based Bump Mapping Framework](https://jcgt.org/published/0009/03/04/)
  provides the mathematical basis for composing bump gradients, including
  procedurally defined volume functions. The terrain uses that principle rather
  than importing the reference renderer.
- [NSIDC, Sastrugi](https://nsidc.org/learn/cryosphere-glossary/sastrugi) and
  [Filhol and Sturm, Snow bedforms: A review, new data, and a formation model](https://agupubs.onlinelibrary.wiley.com/doi/full/10.1002/2015JF003529)
  motivate elongated, interrupted wind-shaped snow features instead of uniform
  rounded pores. Snow ridges often follow the wind; the shader's orientation is
  an artistic control, not a simulated wind history.
- [USGS, Eolian Processes](https://pubs.usgs.gov/gip/deserts/eolian/)
  describes sand ripples with crest axes transverse to wind, a useful distinction
  from the longer wind-aligned snow features.
- [National Park Service, Cross-stratified Sand](https://www.nps.gov/places/cross-stratified-sand.htm)
  motivates oblique, varied sediment bedding rather than identical horizontal
  contour bands on every cliff.
- [National Park Service, Lava Flow Forms](https://www.nps.gov/articles/000/lava-flow-forms.htm)
  distinguishes smooth/corded pāhoehoe from fragmented, jagged ʻaʻā. This supports
  separate folded-crust and granular/fragmented treatments within volcanic terrain.
- [USDA NRCS, The Color of Soil](https://www.nrcs.usda.gov/sites/default/files/2022-11/color-of-soil.pdf)
  describes dark organic humus, iron-derived earth colors, and changes associated
  with wet conditions. These inform restrained soil palettes; palette narrowing
  and darker non-icy presets are artistic decisions, not a global exposure change.
- [USGS, Mangrove sedimentation and response to relative sea-level rise](https://www.usgs.gov/publications/mangrove-sedimentation-and-response-relative-sea-level-rise)
  describes mudbanks and organic sediment, supporting a wet-sediment treatment
  rather than generic porous rock for the mangrove ground.

- [Ken Perlin, GPU Gems chapter 5: Implementing Improved Perlin Noise](https://developer.nvidia.com/gpugems/gpugems/part-i-natural-effects/chapter-5-implementing-improved-perlin-noise)
  explains the cubic interpolant's second-derivative discontinuities and the
  quintic alternative, as well as volume-texture approximations. The prototype
  here is **value noise**, not Perlin gradient noise; it borrows the smooth
  interpolation principle rather than claiming an identical noise algorithm.
- [Simon Green, GPU Gems 2 chapter 26: Implementing Improved Perlin Noise](https://developer.nvidia.com/gpugems/gpugems2/part-iii-high-quality-rendering/chapter-26-implementing-improved-perlin-noise)
  discusses storage versus computation, repeating volumes, interpolation quality,
  and memory cost. Its historical instruction counts do not predict modern GPU
  timings.
- [Khronos, OpenGL Shading Language 4.60 specification](https://registry.khronos.org/OpenGL/specs/gl/GLSLangSpec.4.60.html)
  specifies that implicit derivatives are undefined in nonuniform control flow.
  The prototype calculates coordinate derivatives before its detail branch and
  passes them explicitly to `textureGrad`. Coordinates remain unwrapped at that
  point; sampler repeat handles wrapping without a derivative discontinuity.
