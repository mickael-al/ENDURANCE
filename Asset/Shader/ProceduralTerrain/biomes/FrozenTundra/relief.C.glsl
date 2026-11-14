#version 450 core
#define FIELD_BIOME_ID 1u
#include "../../field_batch_common.glsl"
#include "../../field_weights.glsl"
#include "regions.glsl"
#include "chain.glsl"
#include "caves.glsl"

vec4 fieldRelief(vec2 p)
{
    // Edit frozenMountainPresence in regions.glsl to control BOTH terrain and caves.
    float mountainPresence = frozenMountainPresence(p);
    float plain = frozenPlainHeight(p);
    float mountains = mountainPresence > 0.0 ? frozenChainHeight(p) : 0.0;
    float h = plain / 0.35 + mountains * FROZEN_MOUNTAIN_HEIGHT_GAIN * mountainPresence;
    LandSite cave = frozenCaveSite(p);
    float caveSupport = frozenCaveSupport(p, cave, mountainPresence);
    float mountainMetres = mountains * FROZEN_MOUNTAIN_HEIGHT_GAIN * 0.35 * mountainPresence;
    float band = frozenCaveBand(cave, caveSupport, mountainMetres);
    return vec4(h, band, 0.0, mountainPresence);
}

#include "../../field_surface_accumulate.glsl"
