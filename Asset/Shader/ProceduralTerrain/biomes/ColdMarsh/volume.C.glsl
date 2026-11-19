#version 450 core
// Biome 9: porous sphere additions; the original ground is never excavated.
#define FIELD_BIOME_ID 9u
#include "../../field_batch_common.glsl"
#include "../../field_weights.glsl"
#include "../../biome_landforms_3d.glsl"
#include "spheres.glsl"
#include "porosity.glsl"

float coldMarshSphereGeometry(vec3 p, ColdMarshSphereSite site, float anchor)
{
    vec3 q = coldMarshSphereLocal(p, site, anchor);
    return min(-coldMarshSphereEnvelope(q), coldMarshPorousField(q, site.key));
}

float fieldVolume(vec3 p, FieldColumn column)
{
    if (!landVolumeEnabled(column)) return 0.0;
    ColdMarshSphereSite site = coldMarshSphereSite(p.xz);
    if (coldMarshSphereFootprint(p.xz, site) >= 64.0) return 0.0;
    float weight = fieldSurfaceWeight(column, FIELD_BIOME_ID);
    if (weight <= 0.0) return 0.0;
    // Shared controls are mixed: decode only in a pure biome. Quantizing the
    // recomputed anchor identically also avoids a centre jump at that boundary.
    float anchor = weight >= 1.0 ? coldMarshSphereAnchorFromControl(column.blend.w)
        : coldMarshSphereStoredAnchor(site);
    vec3 q = coldMarshSphereLocal(p, site, anchor);
    float envelope = coldMarshSphereEnvelope(q);
    if (envelope >= 64.0) return 0.0;
    float base = column.terrain.x - p.y;
    // Bound on surviving stone; skips all pore noise where union cannot add.
    if (-envelope <= base) return 0.0;
    float stone = min(-envelope, coldMarshPorousField(q, site.key));
    vec2 band = fieldVolumeEnvelope(column);
    float margin = min(p.y - band.x, band.y - p.y);
    float influence = (1.0 - smoothstep(0.0, 64.0, envelope))
        * smoothstep(8.0, 40.0, margin);
    return max(0.0, stone - base) * influence;
}

#include "../../field_volume_accumulate.glsl"
