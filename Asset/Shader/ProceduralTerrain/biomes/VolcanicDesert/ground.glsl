// Fractured basalt plain, in metres at the reference detailScale of .35.
#ifndef ENDURANCE_VOLCANIC_DESERT_GROUND_GLSL
#define ENDURANCE_VOLCANIC_DESERT_GROUND_GLSL
#include "../../../Noise/noise_2d.glsl"

const float VOLCANIC_GROUND_PLATE_SCALE = 70.0;
const float VOLCANIC_GROUND_WARP_SCALE = 1100.0;
const float VOLCANIC_GROUND_WARP_STRENGTH = 180.0;
// Approximate full width: coordinate distortion and F2-F1 vary it naturally.
const float VOLCANIC_GROUND_CRACK_WIDTH = 4.0;
const float VOLCANIC_GROUND_CRACK_DEPTH = 6.5;

struct VolcanicGroundCrack
{
    vec2 warped;
    float edge;
    float depth;
};

VolcanicGroundCrack volcanicGroundCrack(vec2 p)
{
    // Rotate the cellular domain, then bend both the large plate boundaries
    // and their smaller fractures. Neither warp is a discontinuous cell ID.
    vec2 rotated = vec2(.8 * p.x - .6 * p.y, .6 * p.x + .8 * p.y);
    vec2 broadDomain = rotated / VOLCANIC_GROUND_WARP_SCALE;
    vec2 warped = rotated + VOLCANIC_GROUND_WARP_STRENGTH * vec2(
        noiseSimplex2D(broadDomain, uSeed ^ 0x20a011d1u),
        noiseSimplex2D(broadDomain, uSeed ^ 0x20a011d2u));
    warped += 30.0 * vec2(
        noisePerlin2D(rotated / 160.0, uSeed ^ 0x20a011d3u),
        noisePerlin2D(rotated / 160.0, uSeed ^ 0x20a011d4u));

    // F2-F1 vanishes on Voronoi edges. A single nine-site search supplies
    // both distances; this is a width proxy, not an exact Euclidean SDF.
    vec2 squared = noiseCellularDistancesSquared2D(
        warped / VOLCANIC_GROUND_PLATE_SCALE, uSeed ^ 0x20a011d5u);
    vec2 nearest = sqrt(max(squared, vec2(0.0)));
    float edge = .5 * (nearest.y - nearest.x) * VOLCANIC_GROUND_PLATE_SCALE;
    float depth = VOLCANIC_GROUND_CRACK_DEPTH
        * (.85 + .30 * noisePerlin2D01(rotated / 940.0, uSeed ^ 0x20a011d9u));
    return VolcanicGroundCrack(warped, edge, depth);
}

float volcanicGroundHeight(vec2 p)
{
    VolcanicGroundCrack cracks = volcanicGroundCrack(p);
    vec2 rotated = vec2(.8 * p.x - .6 * p.y, .6 * p.x + .8 * p.y);
    vec2 warped = cracks.warped;
    float halfWidth = .5 * VOLCANIC_GROUND_CRACK_WIDTH;
    float crack = 1.0 - smoothstep(.15 * halfWidth, halfWidth, cracks.edge);

    float broad = noiseSimplexFbm2D(rotated / 3100.0,
        uSeed ^ 0x20a011d6u, 3, 2.03, .48);
    float rubble = noiseSimplexFbm2D(warped / 105.0,
        uSeed ^ 0x20a011d7u, 2, 2.1, .45);
    float fine = noisePerlin2D(warped / 24.0, uSeed ^ 0x20a011d8u);
    // Low ash-covered plates and a lower crack floor, without raised rims.
    // Fade fine rubble in the trench so it cannot close or erase the fissure.
    return 18.0 + 28.0 * broad - cracks.depth * crack
        + (3.0 * rubble + 1.2 * fine) * (1.0 - .65 * crack);
}
#endif
