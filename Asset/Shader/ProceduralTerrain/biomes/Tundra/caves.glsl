// Additional ground caves with their own placement and compact 3D band.
#ifndef ENDURANCE_TUNDRA_CAVES_GLSL
#define ENDURANCE_TUNDRA_CAVES_GLSL
#include "../../biome_landforms_3d.glsl"
#include "../../biome_volume_common.glsl"
#include "cave_sites.glsl"

const float TUNDRA_CAVE_WALL_AMPLITUDE = 4.0; // metres at detailScale=.35

float tundraCaveDistance(vec3 q, LandSite site)
{
    float r = site.radius;
    float depth = mix(48.0, 60.0, site.variant);
    vec3 room = vec3(-0.12 * r, -depth, -0.24 * r);
    vec3 annex = vec3(0.13 * r, -depth - 5.0, -0.20 * r);
    vec3 bend = vec3(-0.27 * r, -20.0, -0.38 * r);
    vec3 mouth = vec3(-0.22 * r, 30.0, -0.48 * r);
    float chamber = landEllipsoid(q - room,
        mix(vec3(100.0, 26.0, 65.0), vec3(135.0, 34.0, 85.0), site.variant));
    float sideRoom = landEllipsoid(q - annex, vec3(80.0, 24.0, 55.0));
    float gallery = min(landCapsule(q, room, bend, 38.0),
                       landCapsule(q, bend, mouth, 60.0));
    gallery = min(gallery, landCapsule(q, room, annex, 26.0));
    float cavity = min(min(chamber, sideRoom), gallery);

    // Reuse the original Marine cavern noise at a broader scale. Its bounded
    // wall modulation cannot close the connected core of the entrance/rooms.
    vec3 noiseP = q * 0.40 + vec3(site.variant * 719.0, 37.0, 113.0);
    float pattern = fieldRecipeCavity(fieldRecipeNoise(noiseP), noiseP.y,
        vec4(0.35, 0.40, 0.25, 0.16), vec4(0.60, 0.30, 0.45, 0.0));
    return cavity + clamp(pattern * 18.0, -TUNDRA_CAVE_WALL_AMPLITUDE,
        TUNDRA_CAVE_WALL_AMPLITUDE);
}

float tundraCarveCaves(vec3 p, FieldColumn column, LandSite site, float target)
{
    if (tundraCavePresence(site) <= 0.0) return target;
    vec3 q = landVolumeLocal(p, column, site);
    q.y *= 0.35; // reference metres; XZ is always in world metres
    // Conservative bounds contain rooms, gallery, mouth and all wall noise.
    // The independent 2D cave support opens its own 768-unit band here.
    if (q.y < -120.0 || q.y > 120.0 || length(q.xz) > TUNDRA_CAVE_SUPPORT * site.radius)
        return target;
    float cavity = tundraCaveDistance(q, site);
    // Carve only. Keep the old surface displacement and its original band fade.
    float influence = 1.0 - smoothstep(0.0, 12.0, cavity);
    return mix(target, min(target, cavity * (uDetailScale / 0.35)), influence);
}

#endif
