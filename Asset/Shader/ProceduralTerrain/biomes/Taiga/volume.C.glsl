#version 450 core
// Biome 3: Taiga. Existing raw relief is scaled by uDetailScale.
// Added pillar dimensions and placement are expressed directly in world metres.
#define FIELD_BIOME_ID 3u
#include "../../field_batch_common.glsl"
#include "../../field_weights.glsl"
#include "../../biome_landforms_3d.glsl"
#include "pillars.glsl"

float fieldVolume(vec3 p, FieldColumn column)
{
    if (!landVolumeEnabled(column)) return 0.0;
    float base = column.terrain.x - p.y;
    float target = base;
    ivec2 cell = taigaPillarGrid(p.xz);
    for (int z = -1; z <= 1; ++z)
    for (int x = -1; x <= 1; ++x)
    {
        ivec2 bin = cell + ivec2(x, z);
        int count = taigaPillarBinCount(bin);
        for (int i = 0; i < count; ++i)
        {
            TaigaPillarSite site = taigaPillarInBin(bin, i);
            if (taigaPillarFootprint(p.xz, site) >= 32.0) continue;
            float anchor = taigaPillarAnchorHeight(site);
            vec3 local = taigaPillarLocal(p, site, anchor);
            float envelope = taigaPillarEnvelope(local, site);
            if (envelope >= 32.0) continue;
            float stone = taigaPillarGeometry(p, site, anchor);
            float influence = 1.0 - smoothstep(0.0, 32.0, envelope);
            target = max(target, mix(base, max(base, stone), influence));
        }
    }
    // The geometric envelope supplies the fade; using landDelta's generic
    // fade would shorten a tall cylinder leaning across a downhill slope.
    return target - base;
}

#include "../../field_volume_accumulate.glsl"
