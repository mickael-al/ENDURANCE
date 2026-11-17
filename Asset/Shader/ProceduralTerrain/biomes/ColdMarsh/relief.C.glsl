#version 450 core
// Biome 9: original height, with a separate band/control for porous spheres.
#define FIELD_BIOME_ID 9u
#include "../../field_batch_common.glsl"
#include "../../field_weights.glsl"
#include "spheres.glsl"

vec4 fieldRelief(vec2 p)
{
    float h = coldMarshGroundHeight(p);
    if (uFieldMode != 0u || uDetailScale <= 0.0) return vec4(h, 0.0, 0.0, 0.0);
    ColdMarshSphereSite site = coldMarshSphereSite(p);
    if (coldMarshSphereFootprint(p, site) >= 64.0) return vec4(h, 0.0, 0.0, 0.0);
    float control = coldMarshSphereAnchorControl(site);
    float anchor = coldMarshSphereAnchorFromControl(round(control * 65535.0) * (1.0 / 65535.0));
    float band = coldMarshSphereBand(p, site, anchor, coldMarshWorldGround(p, h));
    return vec4(h, band, 0.0, control);
}

#include "../../field_surface_accumulate.glsl"
