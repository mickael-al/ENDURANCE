#version 450 core
// Biome 8: excavate only. The former alcoves/balconies are removed.
#define FIELD_BIOME_ID 8u
#include "../../field_batch_common.glsl"
#include "../../field_weights.glsl"
#include "../../biome_landforms_3d.glsl"
#include "pits.glsl"
#include "pit_porosity.glsl"

float fieldVolume(vec3 p, FieldColumn column)
{
    if (!landVolumeEnabled(column)) return 0.0;
    float weight = fieldSurfaceWeight(column, FIELD_BIOME_ID);
    if (weight <= 0.0) return 0.0;
    ColdForestPitSite site = coldForestPitSite(p.xz);
    vec2 horizontal = p.xz - site.center;
    float radial = length(horizontal) / COLD_FOREST_PIT_RADIUS;
    if (radial >= 1.0) return 0.0;
    // Shared pilots are weighted sums: only pure columns can decode our depth.
    float control = weight >= 1.0 ? column.blend.w : coldForestPitControl(p.xz);
    float depth = control * COLD_FOREST_PIT_DEPTH * weight;
    if (depth <= 0.0) return 0.0;
    vec3 q = vec3(horizontal.x, p.y - column.terrain.x, horizontal.y);
    float outside = max(q.y, -depth - q.y);
    if (outside >= 64.0) return 0.0;
    float base = -q.y;
    float pore = coldForestPoreField(q, site.key, coldForestPitVoidFraction(radial)) * 160.0;
    // Negative inside the porous cone; the cone floor bounds every cavity.
    float cut = max(-(q.y + depth), pore);
    float target = min(base, cut);
    // Fade before the actual dispatch bounds, including mixed biome columns
    // and the finite maximum band at extreme user-selected scales.
    vec2 envelope = fieldVolumeEnvelope(column);
    float margin = min(p.y - envelope.x, envelope.y - p.y);
    float influence = smoothstep(8.0, 40.0, margin)
        * (1.0 - smoothstep(32.0, 64.0, outside));
    return (target - base) * influence;
}

#include "../../field_volume_accumulate.glsl"
