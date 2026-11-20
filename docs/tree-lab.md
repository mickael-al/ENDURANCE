# ENDURANCE Tree Lab

The tree lab is an offline authoring scene. It generates reusable tree assets;
opening an ordinary terrain scene does not initialize it, run its generator, or
add a vegetation pass. Forest placement and runtime tree instancing are a
separate integration step.

## Authoring

Open `ENDURANCE/Scene/TreeGeneratorLab.scene` and press Play. The lab owns its preview and
settings windows, docked inside the main application work area. The host follows
window movement and resizing; the two panels cannot become detached OS windows.
The viewport preview fills its panel with a common scale factor while its GPU
render target is limited to 1600 x 1200 pixels. Use **Masquer le laboratoire** to
hide it and the component inspector to reopen it. Start with
a family preset, adjust the seed and structural settings, then generate an
individual tree. The default procedural leaf-cluster mask makes the tool usable
without downloading foliage. A supplied PNG leaf mask can replace it: use alpha
for a cutout image, or grayscale/RGB luminance for an opaque black-and-white
mask. Dimensions must be between 1 and 2048 pixels on each axis and the encoded
file must be at most 32 MiB. The original aspect ratio is retained. Mask loading
uses an isolated PNG decoder and never changes the engine's global image-flip
state.

Use **Sauvegarder preset / Charger preset** for authoring settings and
**Exporter cet arbre** for the currently selected seed. **Generer et exporter le
lot** creates a seed set using the requested count. **Exporter le LOD3 (17 vues)**
is the only impostor export checkbox and is enabled by default. Uncheck it to
export just the meshes and their material textures. The former two-view LOD3
export and renderer have been removed.
**Recharger le modele sauvegarde** reads an already baked tree without running
the generator. Orbit the preview by dragging with the left mouse button, pan
with the middle button, and use its LOD0/LOD1, LOD3 and wireframe controls to inspect
the resulting geometry or distant billboard.

The preview uses neutral workshop lighting with a rough dielectric BRDF and
simple foliage transmission. It does not run the terrain scene's atmosphere,
deferred lighting or shadow cascades. This keeps geometry and palette inspection
independent of the current world and weather.

Fourteen families share the same generator. The original broadleaf, conifer,
weeping and dead tree keep roots and vines disabled. Ten additional presets are
oak, birch, acacia, baobab, mangrove, cypress, palm, aspen, juniper and banyan.
Presets differ in structure as well as color. A preset file stores
the full parameter set, seed, format version, and generator version. Keeping the
preset alongside its baked mesh allows later editing while the baked geometry
remains usable independently of changes to the generator.

The fourteen editable starter files are included in
`ENDURANCE/Ressources/Trees/Presets/`.
They were written through the same serializer as the lab and checked by its
loader. No generated mesh library is committed to the project.

Roots are optional curves attached around the trunk base, tapering outward along
the ground. Their count, length and starting radius are independent controls.
Hanging vines are similarly controlled by count, length and radius and attach to
the existing branch structure. Both are part of the baked wood mesh and have
reduced geometry in LOD1. The skeleton records their kind separately; vines are
excluded from the coarse collision-capsule export. These are stylized shapes,
not a terrain-adapting root solver or a runtime rope simulation.

The core is C++17/GLM without engine or OpenGL dependencies. It builds a branch
hierarchy from guided curves, adds secondary branches, sweeps tapered sections,
and attaches irregular clusters of leaf cards to the branch structure. The
branch hierarchy is retained in the export. Junction collars and normal blending
soften intersecting branch roots; this is not a watertight boolean union of the
whole tree. The current generator uses controlled hierarchical branching, not a
full botanical growth simulation or a reproduction of Disney's proprietary tool.

Two mesh levels are derived from the same structure. LOD1 reduces radial/curve
resolution and secondary wood detail, and thins the foliage while adjusting the
remaining cards. Bounds cover both levels. Cards carry volume-oriented normals,
linear vertex colors, and `_WIND` attributes. Small precomputed bark and leaf
textures keep the preview free of per-pixel procedural FBM.

## Export and reload

Each bake publishes a **new directory**. Existing directories are never replaced;
use another name/seed or move an earlier export intentionally. All files are
written into a sibling staging directory first and published together. Failed
writes leave existing exports intact. Preset file replacement separately keeps
the previous file until the replacement has been written successfully.

The default `ENDURANCE/Ressources/Trees/Baked/` output folder is ignored by Git.
Select and version the approved assets separately when integrating vegetation
into the game; the fourteen supplied family presets are tracked.

| File | Meaning |
| --- | --- |
| `tree.gltf` | Portable glTF 2.0 materials, nodes, and mesh accessors |
| `tree.bin` | Little-endian vertex/index data for both LODs |
| `preset.json` | Complete versioned authoring parameters |
| `manifest.json` | Skeleton, bounds, coarse collision capsules, mip metadata, optional impostor camera metadata |
| `bark.png` | Repeating precomputed bark modulation |
| `leaves.png` | Leaf color modulation and cutout alpha |
| `leaves_mipN.png` | Precomputed coverage-preserving alpha mip levels |
| `impostor_N_albedo.png` | Optional LOD3 unlit, sRGB, straight-alpha capture, N = 0..16 |
| `impostor_N_normal.png` | Matching LOD3 object-space normal capture, linear encoded XYZ, N = 0..16 |

The glTF's default scene contains LOD0. Its second scene contains LOD1; they are
not superimposed and the file does not assume a particular engine LOD extension.
Both use zero metalness. Foliage uses `alphaMode: MASK`, cutoff 0.5, and
`doubleSided: true`. Vertex `COLOR_0` is linear. Base-color PNG RGB is sRGB encoded;
alpha and normal data are linear. `_WIND` components are normalized height,
flexibility, stable seeded phase, and palette weight. Custom wind attributes and
artistic foliage transmission require a matching runtime shader; ordinary glTF
viewers display the standard masked PBR material instead.

The provided `loadBakedTree()` reads this tool's versioned glTF/bin/PNG bundle
without generating new geometry. It validates sizes, indices, finite values,
attribute layouts, skeleton parents, and bounds before replacing the output.
It is intentionally not a general glTF importer. ENDURANCE's generic model
importer currently handles OBJ/FBX, so this does not claim that glTF can already
be dragged into the ordinary world scene. The exported glTF is usable in
compatible DCC software, and the restricted loader provides the data for later
runtime vegetation integration. Asset format is version 3; recipes remain version
2 and the generator remains version 2. Version 1/2 presets and bakes remain readable;
missing root/vine controls default to disabled and missing branch kinds default
to ordinary wood. Existing 17-view sets reload directly; old two-view metadata
is ignored, so those legacy PNGs are no longer required to open a bake.

The manifest records coverage-preserving leaf mips because generic glTF image
loaders commonly generate their own mip chain. A future runtime importer should
upload the supplied levels (or rebuild them with `coverageMipmaps`) instead of
using uncorrected alpha downsampling. RGB conversion to/from 8-bit sRGB can
introduce one quantization step on a reloaded grayscale mask; geometry and
floating-point vertex attributes remain exact.

## 17-view LOD3

LOD3 now renders the same 17 views that are exported, using one view-facing quad
(two triangles). The fixed layout is eight azimuths spaced by 45 degrees at
15-degree elevation, another eight at 60 degrees, and one zenith view. Roots,
hanging vines and foliage are captured together within shared tree bounds.
`manifest.json` records `lod3.type = "multiViewImpostor"`, `viewCount = 17`, and
references the existing `impostors` array. There are 34 PNGs when enabled: 17
colors and 17 normals; no separate two-view files are produced.

Each view records `direction` from tree centre toward the camera, `up`, `centre`,
and orthographic `halfExtent`; both image axes span `[-halfExtent, +halfExtent]`.
Albedo is unlit sRGB with straight alpha and top row first. Normal RGB encodes
object-space XYZ as `normal * 0.5 + 0.5`; its alpha matches the albedo coverage.
The material is relit using the current preview sun. Unlike the old baked-light
billboard, changing sun direction does not require recapturing the tree.

At upload, the preview places the views in two RGBA8 texture arrays. It selects
at most three neighboring captures on CPU using a triangulated angular grid,
then blends those layers in the shader. There are at most six texture reads
per fragment, not 17 pairs. Ray/plane projection aligns the selected capture
frames, including rotation of the zenith view. Colors are blended in linear
space with premultiplied coverage; object normals are decoded and normalized.
A 256-square set occupies approximately 11.3 MiB including both mip chains.
This uses more texture memory than the former two images but retains azimuthal
variation and enables relighting without additional geometry.

A fresh tree builds its inspection cache once on GPU when LOD3 is first selected,
without readback. Explicit export captures the requested resolution with 2x
supersampling, reads back albedo/normal images, then writes them in the CPU worker.
Loading an exported set uses its stored images directly, with alpha-aware mips;
there is no per-frame decoding, capture or mip generation. If the export checkbox
was off, or an old bake has no 17-view set, the lab can rebuild its preview cache
from the loaded mesh. Toggling the export checkbox does not disable inspection.

This remains a distant approximation: blending can soften narrow branches,
there is no depth/parallax texture or reconstructed branch self-shadowing, and
views below 15 degrees use the lower ring. Preview wind is disabled for LOD3;
mesh LODs retain animated wind. Forest streaming, automatic distance selection
and instanced billboard rendering in the world are still the separate
vegetation-integration step.

## Performance and scope

Authoring work can be moved to the CPU worker because the generator and exporter
do not call OpenGL. Uploads, preview draws, and optional impostor capture remain
on the owning GL context. Image readback is an explicit bake operation, never
part of ordinary interactive preview. The preview restores its OpenGL state so
it does not disturb the engine's HUD/render passes.

The generation safety budget bounds the number of branches and cards and reports
when a setting reaches that limit. Instance count, shadow-map overdraw, distance
culling, LOD selection, and terrain placement still need their own runtime
budget. A low draw-call count alone does not make a dense forest inexpensive.

For first art validation, compare a few distinctly different silhouettes and
both mesh LODs before baking a full seed set. Check the result from beneath the
canopy and from above, including leaf cutouts, branch junctions, and silhouette
changes. Portable exports retain shapes and materials, but their lighting will
vary between renderers.

## Validation

Run from the repository root:

```sh
python Tests/Trees/check_core.py
python Tests/Trees/check_bake.py
python Tests/Trees/check_preview.py
python Tests/Trees/check_panel.py
```

The CPU scripts compile the actual generator/exporter in both normal and
`-ffast-math` configurations. They exercise deterministic seeds, all fourteen families,
mesh/bounds/normal invariants, LOD reduction, mask aspect and alpha coverage,
invalid settings, cancellation, preset and exact geometry round trips, and safe
export failure. Bake validation also reads glTF accessors and buffers independently
in Python, checks private PNG import against global image-flip state, and verifies
that captured albedo/normals are not gamma-converted again during publication.
Roots/vines, shared budgets, version 1/2 compatibility, malformed view rejection
and complete 17-view LOD3 round trips are covered as well.

The preview and panel scripts use a real desktop OpenGL context through Linux
EGL. They require C/C++17 compilers, `libGL`, `libEGL`, and an installed GLEW 2.x
library; `--glew-object /path/to/glew.o` can supply a PIC GLEW object instead.
`CC` and `CXX` select the compilers. `--fast-math` is also available for these two
scripts. They use the actual GLSL, ImGui and rendering helper; only the panel's
scene/HUD registration methods are stubbed. Tests cover visible wood/foliage,
LOD and wireframe controls, 17-view capture/reload, dynamic lighting, angular
selection/transition boundaries, an actual two-triangle billboard draw,
restoration of foreign GL state (including array textures), asynchronous
batch/export/reload, export checkbox behavior, main-window anchoring on resize
and movement, stale-result rejection, error recovery and cancellation. All output is isolated in temporary directories.

For visual review, `check_preview.py --captures /tmp/tree-review` writes complete
and wood-only views for the fourteen presets plus LOD3 views at 15, 45 and 80
degrees; this optional conversion needs
Pillow. The validation run used Mesa llvmpipe with GLSL 4.60 enabled. Software GL
establishes functional correctness, not performance on the target GPU. The actual
scene component also passed a C++17 syntax check against the engine headers.
A full Windows/MSVC engine build and in-editor run remain necessary on the target
machine; they were not executed by these standalone tests.

## Research references

These references inform the architecture and controls; no proprietary source
code or production assets are copied:

- Disney, *Art-Directing Disney's Tangled Procedural Trees*:
  https://disneyanimation.com/publications/art-directing-disneys-tangled-procedural-trees/
  — separates artist-directed trunk/branch structure from procedural canopy fill.
- SideFX Labs, *Tree Branch Generator*:
  https://www.sidefx.com/docs/houdini/nodes/sop/labs--tree_branch_generator-.html
  — hierarchical branching, angle/length/radius controls, tropism and junction handling.
- Runions, Lane, Prusinkiewicz, *Modeling Trees with a Space Colonization Algorithm*:
  https://www.algorithmicbotany.org/papers/colonization.egwnp2007.pdf
  — reference for future crown-guided growth; not the current generation algorithm.
- Ignacio Castaño, *Computing Alpha Mipmaps*:
  https://www.ludicon.com/castano/blog/articles/computing-alpha-mipmaps/
  — alpha coverage preservation when foliage is reduced to distant mip levels.
