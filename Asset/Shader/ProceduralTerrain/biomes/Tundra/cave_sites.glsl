// Large, independent regions of Marine-noise caves. Shared by the 2D/3D passes.
#ifndef ENDURANCE_TUNDRA_CAVE_SITES_GLSL
#define ENDURANCE_TUNDRA_CAVE_SITES_GLSL
#include "../../biome_landforms_common.glsl"

const float TUNDRA_CAVE_REGION_SCALE = 2800.0; // extent of groups, NOT room size
const float TUNDRA_CAVE_REGION_THRESHOLD = 0.70; // higher means fewer networks
const float TUNDRA_CAVE_REGION_FEATHER = 0.12;
const float TUNDRA_CAVE_BAND = 4864.0; // raw; 1702.4 m total at detailScale=.35

float tundraCaveRegion(vec2 p)
{
    vec2 q = p / TUNDRA_CAVE_REGION_SCALE;
    vec2 warp = vec2(landPlacementValue(q * 0.47, 332u),
                     landPlacementValue(q * 0.47, 333u)) - 0.5;
    float region = landPlacementValue(q + warp * 0.55, 334u);
    return smoothstep(TUNDRA_CAVE_REGION_THRESHOLD,
        TUNDRA_CAVE_REGION_THRESHOLD + TUNDRA_CAVE_REGION_FEATHER, region);
}

float tundraCaveGroundClearance(vec2 p)
{
    // Protect old sculptures locally, allowing a large network to pass around
    // them. Rejecting the whole region would suppress almost every network.
    float clearance = 150.0;
    ivec2 oldCell = ivec2(floor(p / 2600.0));
    ivec2 rockCell = ivec2(floor(p / 2000.0));
    for (int z = -1; z <= 1; ++z)
    for (int x = -1; x <= 1; ++x)
    {
        vec2 offset = vec2(x, z);
        LandSite old = landSite((vec2(oldCell) + offset + 0.5) * 2600.0,
            2600.0, 307u, 0.58);
        float distanceToOld = length(p - old.center) - old.radius;
        if (distanceToOld < clearance
            && landSite(old.center, 2600.0, 307u, 0.58).mask > 0.0)
            clearance = min(clearance, distanceToOld);
        LandSite rock = landRkmSite((vec2(rockCell) + offset + 0.5) * 2000.0,
            0.5, 322u);
        clearance = min(clearance, length(p - rock.center) - 42.0);
    }
    return smoothstep(0.0, 150.0, clearance);
}

float tundraCaveMask(vec2 p)
{
    float region = tundraCaveRegion(p);
    if (region <= 0.0) return 0.0;
    return region * tundraCaveGroundClearance(p);
}

float tundraCaveBand(float mask)
{
    // The surface recipe opens the new band only where old bands were zero.
    return TUNDRA_CAVE_BAND * smoothstep(0.0, 0.08, mask);
}
#endif
