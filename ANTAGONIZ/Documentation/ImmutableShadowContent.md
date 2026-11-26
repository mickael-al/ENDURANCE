# Immutable shadow content

Standard models remain uncached by default. The editor's **Immutable shadow content** checkbox saves the `immutableShadowContent` scene field and explicitly promises that a model's mesh and alpha image data do not animate between invalidations. Only the `HumainSize` reference cube opts in in `Terrain.scene`; its shadow, transform and material are unchanged.

Opted-in models are cacheable only with a CPU-loaded `ShapeBufferBase`, a CPU-uploaded texture, the built-in shadow pipeline, and no extra shader buffers or extra instances. Skinned/unknown geometry, external GPU textures, custom shadow programs and legacy rendering callbacks retain conservative redraws. They must not be marked static simply because their transforms did not move.

The renderer compares exact CPU snapshots each frame: resource lifetime tokens and GPU names, uploaded model matrices/material indices, instance indices, material UV offset/tiling, alpha and the active caster set. Transform/parent changes, material changes, hiding, cast-shadow/depth flags, deletion and resource replacement invalidate the map. Camera/light matrices and custom-renderer publications remain tracked by the existing shadow cache. No GPU readback or texture hashing is performed.

Code that modifies raw GPU content must notify the cache **before the next render**:

- After writing a shared mesh VBO/IBO or changing its vertex layout, call `ShapeBuffer::invalidateShadowContent()`.
- After modifying shared alpha texels, mipmaps or sampling state, call `Textures::invalidateShadowContent()`.
- After externally changing material shader data or relinking a program in place, call `Materials::invalidateShadowContent()` or `GraphiquePipeline::invalidateShadowContent()` respectively.
- `Model::invalidateShadowContent()` invalidates only that model; it is insufficient when other opted-in models share the modified resource. Use the resource method above instead.
- For continuously changing geometry/alpha, leave the checkbox disabled. Model transforms can move normally: those are tracked without manual invalidation.

The snapshot assumes ordinary model/material buffers are managed through their public setters; direct writes to the engine's model/material SSBOs bypass their CPU mirrors and require disabling this opt-in. An opt-in is not permission to freeze animation or to discard the object's shadow.

Run `python Tests/Atmosphere/check_immutable_shadows.py` for normal/fast-math invalidation and actual scene serialization coverage, and `python Tests/Atmosphere/check_shadow_cache.py` for production LightManager integration.
