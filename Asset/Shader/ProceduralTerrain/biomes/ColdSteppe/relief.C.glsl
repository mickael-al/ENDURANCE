#version 450 core
#define FIELD_BIOME_ID 6u
#include "../../field_batch_common.glsl"
#include "../../field_weights.glsl"
#include "regions.glsl"
#include "chain.glsl"
#include "caves.glsl"
#include "torus.glsl"

vec4 fieldRelief(vec2 p)
{
    // Edit steppeMountainPresence in regions.glsl to control BOTH terrain and caves.
    float mountainPresence = steppeMountainPresence(p);
    float plain = steppePlainHeight(p);
    float mountains = mountainPresence > 0.0 ? steppeChainHeight(p) : 0.0;
    float h = plain / 0.35 + mountains * STEPPE_MOUNTAIN_HEIGHT_GAIN * mountainPresence;
    LandSite cave = steppeCaveSite(p);
    float caveSupport = steppeCaveSupport(p, cave, mountainPresence);
    float mountainMetres = mountains * STEPPE_MOUNTAIN_HEIGHT_GAIN * 0.35 * mountainPresence;
    float band = steppeCaveBand(cave, caveSupport, mountainMetres);
    float torusControl = steppeTorusControl(p);
    band = max(band, steppeTorusBand(p, torusControl));
    return vec4(h, band, 0.0, torusControl);
}

#include "../../field_surface_accumulate.glsl"
