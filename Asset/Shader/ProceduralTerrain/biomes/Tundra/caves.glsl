// The Marine 3D recipe itself generates every new Tundra cavity.
#ifndef ENDURANCE_TUNDRA_CAVES_GLSL
#define ENDURANCE_TUNDRA_CAVES_GLSL
#include "../../biome_landforms_3d.glsl"
#include "../../biome_volume_common.glsl"
#include "cave_sites.glsl"

// Independent controls: region extent lives in cave_sites.glsl; these enlarge
// the Marine noise wavelengths. A random network still has narrower necks.
const float TUNDRA_CAVE_HORIZONTAL_SCALE = 8.0;
const float TUNDRA_CAVE_VERTICAL_SCALE = 4.0;
const float TUNDRA_CAVE_DEPTH = 180.0; // reference metres below the local surface
const float TUNDRA_CAVE_HALF_DEPTH = 420.0;
const float TUNDRA_CAVE_FEATHER = 100.0;
const float TUNDRA_CAVE_CARVING = 400.0;
const float TUNDRA_CAVE_SURFACE_RADIUS = 0.065;

float tundraCarveCaves(vec3 p, FieldColumn column, float mask, float target)
{
    if (mask <= 0.0) return target;
    float verticalScale = uDetailScale / 0.35;
    float offset = column.blend.y + clamp(column.blend.z, -160.0, 160.0) * uDetailScale;
    float localY = (p.y - column.terrain.x - offset) / verticalScale;
    // Stay inside the full-strength portion of landDelta's outer band, even
    // at a regional/biome transition where the stored band becomes smaller.
    float outerHalf = 0.5 * column.terrain.w / verticalScale;
    float halfDepth = min(TUNDRA_CAVE_HALF_DEPTH,
        max(0.0, 0.74 * outerHalf - TUNDRA_CAVE_DEPTH));
    if (halfDepth <= 0.0 || abs(localY + TUNDRA_CAVE_DEPTH) >= halfDepth)
        return target;

    vec3 noisePosition = vec3(p.x / TUNDRA_CAVE_HORIZONTAL_SCALE,
        localY / TUNDRA_CAVE_VERTICAL_SCALE, p.z / TUNDRA_CAVE_HORIZONTAL_SCALE);
    // Transform height/band/amplitudes with the noise coordinates, so the
    // original helper still sees a consistent density and finite vertical band.
    vec2 terrain = vec2(-offset / (verticalScale * TUNDRA_CAVE_VERTICAL_SCALE),
        halfDepth / TUNDRA_CAVE_VERTICAL_SCALE);
    vec4 volume = vec4(-TUNDRA_CAVE_DEPTH, 0.0, TUNDRA_CAVE_CARVING,
        min(TUNDRA_CAVE_FEATHER, halfDepth * 0.5)) / TUNDRA_CAVE_VERTICAL_SCALE;
    // Marine shape/style at depth, with a narrower noise threshold near the
    // surface: leave roofs between occasional natural entrances. This remains
    // the shared noise cutter, not a hand-built room or entrance primitive.
    vec4 shape = vec4(0.35, 0.40, 0.25,
        mix(TUNDRA_CAVE_SURFACE_RADIUS, 0.16, smoothstep(25.0, 140.0, -localY)));
    float density = fieldRecipeDensityAtPrepared(noisePosition, terrain, volume,
        shape, vec4(0.60, 0.30, 0.45, 0.70));
    float base = terrain.x - noisePosition.y;
    float delta = min(density - base, 0.0)
        * (TUNDRA_CAVE_VERTICAL_SCALE * verticalScale);
    return target + mask * delta;
}
#endif
