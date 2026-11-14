// Sparse ground caves in the ORIGINAL Tundra volume band. No relief changes.
#ifndef ENDURANCE_TUNDRA_CAVES_GLSL
#define ENDURANCE_TUNDRA_CAVES_GLSL
#include "../../biome_landforms_3d.glsl"
#include "../../biome_volume_common.glsl"

const float TUNDRA_CAVE_REGION_SCALE = 9000.0; // metres; regions permitting caves
const float TUNDRA_CAVE_REGION_THRESHOLD = 0.58; // raise to make them rarer
const float TUNDRA_CAVE_SITE_CHANCE = 0.35; // fraction of existing sites considered
const float TUNDRA_CAVE_WALL_AMPLITUDE = 4.0; // metres at detailScale=.35

float tundraCavePresence(LandSite site)
{
    if (site.mask <= 0.0) return 0.0;
    // One decision per existing 2.6-km site, not per voxel. Sampling the noise
    // at its anchor prevents a regional boundary from sealing half a gallery.
    ivec2 cell = ivec2(floor(site.center / 2600.0));
    if (landPlacementCorner(cell, 331u) >= TUNDRA_CAVE_SITE_CHANCE) return 0.0;
    vec2 p = site.center / TUNDRA_CAVE_REGION_SCALE;
    vec2 warp = vec2(landPlacementValue(p * 0.47, 332u),
                     landPlacementValue(p * 0.47, 333u)) - 0.5;
    float region = landPlacementValue(p + warp * 0.55, 334u);
    // This selection is constant over the whole cave. The cave itself has
    // compact support well inside the cell; no threshold edge reaches terrain.
    return region >= TUNDRA_CAVE_REGION_THRESHOLD ? 1.0 : 0.0;
}

float tundraCaveDistance(vec3 q, LandSite site)
{
    float r = site.radius;
    float depth = mix(32.0, 40.0, site.variant);
    vec3 room = vec3(-0.12 * r, -depth, -0.24 * r);
    vec3 annex = vec3(0.13 * r, -depth - 5.0, -0.20 * r);
    vec3 bend = vec3(-0.25 * r, -16.0, -0.37 * r);
    vec3 mouth = vec3(-0.22 * r, 18.0, -0.48 * r);
    float chamber = landEllipsoid(q - room,
        mix(vec3(90.0, 18.0, 55.0), vec3(125.0, 22.0, 70.0), site.variant));
    float sideRoom = landEllipsoid(q - annex, vec3(65.0, 16.0, 45.0));
    float gallery = min(landCapsule(q, room, bend, 19.0),
                       landCapsule(q, bend, mouth, 22.0));
    gallery = min(gallery, landCapsule(q, room, annex, 16.0));
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
    // Everything lies inside the old site's full mask and 512-unit band.
    if (q.y < -82.0 || q.y > 60.0 || length(q.xz) > 0.63 * site.radius)
        return target;
    float cavity = tundraCaveDistance(q, site);
    // Carve only. Keep the old surface displacement and its original band fade.
    float influence = 1.0 - smoothstep(0.0, 12.0, cavity);
    return mix(target, min(target, cavity * (uDetailScale / 0.35)), influence);
}

#endif
