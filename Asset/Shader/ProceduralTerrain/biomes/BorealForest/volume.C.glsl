#version 450 core
// Biome 7: Voronoi cavities restricted to the added mountain stack.
#define FIELD_BIOME_ID 7u
#include "../../field_batch_common.glsl"
#include "../../field_weights.glsl"
#include "../../biome_landforms_3d.glsl"
#include "height.glsl"
#include "cavities.glsl"

float fieldVolume(vec3 p, FieldColumn column)
{
    if (!landVolumeEnabled(column)) return 0.0;
    float weight = fieldSurfaceWeight(column, FIELD_BIOME_ID);
    if (weight <= 0.0) return 0.0;
    // Decode the transmitted mountain in pure columns. At a climate boundary
    // the shared pilot also contains neighbours' controls: recompute our own
    // four layers there instead of mistaking a neighbour's pilot for a mountain.
    float mountain = weight >= 1.0 ? borealMountainFromControl(column.blend.w)
        : borealMountainHeight(p.xz);
    float thickness = mountain * uDetailScale * weight;
    if (thickness <= 0.0) return 0.0;
    float top = column.terrain.x;
    float ground = top - thickness;
    if (p.y <= ground - 64.0 || p.y >= top + 64.0) return 0.0;
    float base = top - p.y;
    // One gradient across the entire stack, not a restart at each 600-m layer.
    float height01 = clamp((p.y - ground) / thickness, 0.0, 1.0);
    float stone = borealCavityField(p, uSeed, height01);
    // Intersect the added stack with porous rock, then union the original
    // ground back in. Only mountains become air; the soil stays solid.
    float target = max(ground - p.y, min(base, stone));
    float outside = max(ground - p.y, p.y - top);
    float influence = 1.0 - smoothstep(32.0, 64.0, outside);
    return (target - base) * influence;
}

#include "../../field_volume_accumulate.glsl"
