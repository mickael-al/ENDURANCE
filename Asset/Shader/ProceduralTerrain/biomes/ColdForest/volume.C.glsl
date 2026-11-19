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
    float localY = p.y - column.terrain.x;
    // No excavation reaches above this fade, even in a mixed biome column.
    if (localY >= 64.0) return 0.0;
    float weight = fieldSurfaceWeight(column, FIELD_BIOME_ID);
    if (weight <= 0.0) return 0.0;
    vec2 pits;
    if (weight >= 1.0)
    {
        // Only pure columns can decode the two cached values independently.
        pits = vec2(max(0.0, 0.5 * column.terrain.w - COLD_FOREST_PIT_BAND_MARGIN
            - abs(column.blend.y)), column.blend.w);
    }
    else
    {
        pits = coldForestPitFields(p.xz);
        pits.x *= weight;
    }
    vec2 envelope = fieldVolumeEnvelope(column);
    pits.x = min(pits.x, max(0.0, column.terrain.x - envelope.x
        - COLD_FOREST_PIT_BAND_MARGIN));
    if (pits.x <= 0.0 || pits.y <= 0.0) return 0.0;
    float outside = max(localY, -pits.x - localY);
    if (outside >= 64.0) return 0.0;
    float base = -localY;
    // A shared world-XZ noise frame prevents seed jumps at overlap boundaries.
    float pore = coldForestPoreField(vec3(p.x, localY, p.z),
        uSeed ^ 0xc084b9d3u, pits.y) * 160.0;
    float cut = max(-(localY + pits.x), pore);
    float target = min(base, cut);
    float margin = min(p.y - envelope.x, envelope.y - p.y);
    float influence = smoothstep(8.0, 40.0, margin)
        * (1.0 - smoothstep(32.0, 64.0, outside));
    return (target - base) * influence;
}

#include "../../field_volume_accumulate.glsl"
