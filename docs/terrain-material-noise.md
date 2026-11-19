# Terrain material noise: analytic field versus generated cache

The terrain material implementation keeps analytic noise. A generated 3D cache
was evaluated as an alternative, but the available measurements showed no
performance benefit and the cache introduces approximation error and periodic
repetition. The prototype remains in `Tests/TerrainMaterials/benchmark_noise.py`;
it is not a runtime resource or an optional production rendering path.

## Shared work and interpolation

The intended material budget is one macro field, one intermediate field, and
microdetail only where its footprint remains visible. Biome/material mixtures
reuse these fields instead of evaluating a separate noise for every layer,
color, roughness value, or normal. Computing a scalar value and its analytic
gradient together reuses the same eight corner hashes; finite-difference normal
sampling is unnecessary.

The new material noise uses quintic interpolation and its analytic derivative:

```glsl
vec3 blend = f*f*f*(f*(f*6.0 - 15.0) + 10.0);
vec3 derivative = 30.0*f*f*(f - 1.0)*(f - 1.0);
```

This removes the second-derivative discontinuity of the previous cubic
interpolant at cell boundaries. It helps bump-mapping continuity; it does not
guarantee that every possible lattice pattern or aliasing artifact disappears.
World-space footprint filtering and restrained detail strength are still needed.

## Cache prototype

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

## Reproduction and measurements

From the repository root, on a Linux environment with EGL and desktop OpenGL:

```sh
python Tests/TerrainMaterials/benchmark_noise.py --samples 7 --warmup 2 --size 512 --cache-sizes 64
```

Quintic interpolation is the default. Use `--interpolation cubic` for the
previous interpolant, `--cache-sizes 64 128` to compare resolutions, or
`--quality-only` to skip timing. The script requires only Python's standard
library and the repository's existing headless OpenGL harness.

Recorded environment: OpenGL 4.5 Core, Mesa 25.0.7-2+deb13u1, llvmpipe
(LLVM 19.1.7, 256 bits). The following are median GPU-query times for a 512×512
fullscreen draw, seven measured samples after two warmups, alternating order:

| Scene | Analytic, ms | Cached 64³, ms | Cache / analytic |
|---|---:|---:|---:|
| One macro field | 0.1556 | 0.3382 | 2.173 |
| Near: macro + intermediate + visible microdetail | 0.4543 | 1.3598 | 2.993 |
| Far: macro + intermediate, microdetail skipped | 0.7511 | 1.3153 | 1.751 |

Allocation, shader compilation, noise-volume generation, mip generation, and
readbacks are outside these fragment timings. The output consumes all four
noise channels so the compiler cannot remove derivative work. Both paths use
the same seeded periodic quintic function; the analytic baseline therefore
contains coordinate wrapping that the nonperiodic production field does not
need. The texture version also benefits from actual mip filtering, so numerical
quality is assessed separately at LOD 0 rather than inferred from timings.

These software-renderer numbers are **not a forecast for a hardware GPU or
complete game frame time**. CPU scheduling and other workloads affect them;
small elapsed times are especially sensitive to measurement conditions. They
provide no reason to add the cache to production, but do not establish that
analytic noise always wins on every GPU. There is deliberately no timing
assertion or automatic performance gate.

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
