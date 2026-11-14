// Independent ground-cave placement, shared by Tundra's 2D and 3D passes.
#ifndef ENDURANCE_TUNDRA_CAVE_SITES_GLSL
#define ENDURANCE_TUNDRA_CAVE_SITES_GLSL
#include "../../biome_landforms_common.glsl"

const float TUNDRA_CAVE_SPACING = 1800.0; // candidate cells, not distance per cave
const float TUNDRA_CAVE_SITE_CHANCE = 0.85;
const float TUNDRA_CAVE_REGION_SCALE = 3000.0;
const float TUNDRA_CAVE_REGION_THRESHOLD = 0.35;
const float TUNDRA_CAVE_BAND = 768.0; // raw vertical thickness; .35 => 268.8 m
const float TUNDRA_CAVE_SUPPORT = 0.78;

bool tundraCaveClearOfExisting(LandSite cave)
{
    // Keep the WHOLE new cave band clear of the old brows and erratics. This
    // preserves both their geometry and their original packed-band fade.
    float reach = TUNDRA_CAVE_SUPPORT * cave.radius + 4.0;
    ivec2 oldCell = ivec2(floor(cave.center / 2600.0));
    ivec2 rockCell = ivec2(floor(cave.center / 2000.0));
    for (int z = -1; z <= 1; ++z)
    for (int x = -1; x <= 1; ++x)
    {
        vec2 offset = vec2(x, z);
        LandSite old = landSite((vec2(oldCell) + offset + 0.5) * 2600.0,
            2600.0, 307u, 0.58);
        // A cell midpoint can be outside a jittered active site's mask.
        // Re-evaluate at its anchor before deciding whether it is occupied.
        if (length(cave.center - old.center) < reach + old.radius
            && landSite(old.center, 2600.0, 307u, 0.58).mask > 0.0)
            return false;
        LandSite rock = landRkmSite((vec2(rockCell) + offset + 0.5) * 2000.0,
            0.5, 322u);
        if (length(cave.center - rock.center) < reach + 42.0) return false;
    }
    return true;
}

LandSite tundraCaveSite(vec2 p)
{
    LandSite cave = landSite(p, TUNDRA_CAVE_SPACING, 341u, TUNDRA_CAVE_SITE_CHANCE);
    float support = 1.0 - smoothstep(0.74 * cave.radius,
        TUNDRA_CAVE_SUPPORT * cave.radius, length(cave.q));
    if (cave.mask <= 0.0 || support <= 0.0)
    {
        cave.mask = 0.0;
        return cave;
    }
    vec2 regionP = cave.center / TUNDRA_CAVE_REGION_SCALE;
    vec2 warp = vec2(landPlacementValue(regionP * 0.47, 332u),
                     landPlacementValue(regionP * 0.47, 333u)) - 0.5;
    float region = landPlacementValue(regionP + warp * 0.55, 334u);
    // One regional decision for the whole cave; never cut an entrance in half.
    if (region < TUNDRA_CAVE_REGION_THRESHOLD || !tundraCaveClearOfExisting(cave))
        support = 0.0;
    cave.mask = support;
    return cave;
}

float tundraCaveBand(LandSite cave)
{
    // Open the full band before reaching any excavated geometry.
    return TUNDRA_CAVE_BAND * smoothstep(0.0, 0.08, cave.mask);
}

float tundraCavePresence(LandSite cave)
{
    return cave.mask;
}
#endif
