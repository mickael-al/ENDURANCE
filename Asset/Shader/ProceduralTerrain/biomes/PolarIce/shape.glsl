// The same fissure footprint positions the 2D incision and its 3D floor cave.
// Shared scalar placement noise keeps this independent of either noise library.
#ifndef ENDURANCE_POLAR_ICE_SHAPE_GLSL
#define ENDURANCE_POLAR_ICE_SHAPE_GLSL

// Shared placement and dimensions: edit once for both relief and volume.
// landSparseSite currently caps the footprint scale at 1.0; retain the user's
// requested value here without silently changing the established placement.
const float POLAR_RIFT_SPACING = 8000.0;
const float POLAR_RIFT_FOOTPRINT = 1.5;
const uint POLAR_RIFT_SALT = 209u;
const float POLAR_RIFT_OCCUPANCY = 0.80;
const float POLAR_RIFT_HALF_WIDTH_MIN = 80.0;
const float POLAR_RIFT_HALF_WIDTH_MAX = 405.0;

float polarRiftHalfWidth(LandSite rift)
{
    return mix(POLAR_RIFT_HALF_WIDTH_MIN, POLAR_RIFT_HALF_WIDTH_MAX, rift.variant);
}

// Wall displacement in local XZ metres, independent of the cut's dimensions.
// Strength 0 disables the wall sculpture and its additional volume band.
const float POLAR_RIFT_WALL_STRENGTH = 1.0;
// Larger wavelengths spread the same wall displacement over broader forms.
const float POLAR_RIFT_WALL_NOISE_SCALE = 2.5;
const vec2 POLAR_RIFT_WALL_WARP = vec2(22.0, 44.0);
const vec2 POLAR_RIFT_WALL_DETAIL = vec2(4.0, 7.0);

float polarRiftDepth(LandSite rift)
{
    return mix(700.0, 900.0, rift.variant);
}

vec2 polarRiftWarpLimit()
{
    return max(POLAR_RIFT_WALL_STRENGTH, 0.0)
        * (abs(POLAR_RIFT_WALL_WARP) + abs(POLAR_RIFT_WALL_DETAIL));
}

// Independent per-site parameters: changing width or depth does not select a
// different branch family. The warped-domain anchor is identical in 2D and 3D.
float polarRiftRandom(LandSite rift, uint salt)
{
    uint key = noiseInternalHash(floatBitsToUint(rift.center.x)
        ^ noiseInternalHash(floatBitsToUint(rift.center.y)) ^ uSeed ^ 0x784bd719u);
    return noiseInternalUintTo01(noiseInternalHash(key ^ salt));
}

vec4 polarRiftTrunkParameters(LandSite rift)
{
    return vec4(mix(-0.10, 0.10, polarRiftRandom(rift, 141u)),
        mix(-0.15, 0.15, polarRiftRandom(rift, 142u)),
        mix(-0.11, 0.11, polarRiftRandom(rift, 143u)),
        mix(2.2, 4.0, polarRiftRandom(rift, 144u)));
}

vec4 polarRiftTrunkNoise(LandSite rift)
{
    return vec4(mix(0.10, 0.18, polarRiftRandom(rift, 145u)),
        mix(0.48, 0.80, polarRiftRandom(rift, 146u)),
        polarRiftRandom(rift, 147u) * 37.0,
        polarRiftRandom(rift, 148u) * 41.0);
}

float polarRiftMainLength(LandSite rift)
{
    return mix(0.72, 0.92, polarRiftRandom(rift, 149u));
}

float polarRiftCenterline(float x, LandSite rift)
{
    float u = clamp(x / rift.radius, -1.25, 1.25);
    vec4 shape = polarRiftTrunkParameters(rift);
    vec4 n = polarRiftTrunkNoise(rift);
    float broad = landPlacementValue(vec2(u / n.y + n.z, n.w), 150u)
        - landPlacementValue(n.zw, 150u);
    // Linear drift, unequal bending and a broad S-curve vary independently.
    // Every trunk still passes through q=0; the floor cave follows this curve.
    return rift.radius * (shape.x * u + shape.y * u * u
        + shape.z * sin(shape.w * u) + n.x * broad);
}

float polarRiftCenterlineSlopeBound(LandSite rift)
{
    vec4 shape = polarRiftTrunkParameters(rift);
    vec4 n = polarRiftTrunkNoise(rift);
    // |u|<=1.25, |cos|<=1, and a quintic value-noise derivative is <=1.875.
    return abs(shape.x) + 2.5 * abs(shape.y) + abs(shape.z) * shape.w
        + 1.875 * n.x / n.y;
}

int polarRiftBranchCount(LandSite rift)
{
    float choice = polarRiftRandom(rift, 151u);
    // A simple tear is common. Forks are optional, not a fixed Y template.
    return choice < 0.38 ? 0 : choice < 0.72 ? 1 : choice < 0.93 ? 2 : 3;
}

// start X/r, signed longitudinal run/r, signed lateral spread/r, width factor.
vec4 polarRiftBranchLayout(LandSite rift, int index)
{
    uint salt = 160u + uint(index) * 16u;
    float direction = polarRiftRandom(rift, salt) < 0.5 ? -1.0 : 1.0;
    float side = polarRiftRandom(rift, salt + 1u) < 0.5 ? -1.0 : 1.0;
    return vec4(mix(-0.30, 0.30, polarRiftRandom(rift, salt + 2u)),
        direction * mix(0.28, 0.57, polarRiftRandom(rift, salt + 3u)),
        side * mix(0.16, 0.42, polarRiftRandom(rift, salt + 4u)),
        mix(0.28, 0.56, polarRiftRandom(rift, salt + 5u)));
}

float polarRiftBranchOffset(float x, LandSite rift, vec4 branch)
{
    float along = (x / rift.radius - branch.x) / branch.y;
    return rift.radius * branch.z * smoothstep(0.0, 1.0, along);
}

float polarRiftMask(LandSite rift)
{
    if (rift.mask <= 0.0) return 0.0;
    float r = rift.radius;
    float x = rift.q.x;
    float centerline = polarRiftCenterline(x, rift);
    float widthNoise = landPlacementValue(vec2(x / 140.0,
        rift.variant * 23.0), 115u);
    float halfWidth = polarRiftHalfWidth(rift) * (0.78 + 0.44 * widthNoise);
    float edge = (landPlacementValue(rift.q / 95.0, 116u) - 0.5) * 0.22
        + (landPlacementValue(rift.q / 37.0, 117u) - 0.5) * 0.06;
    float mainDistance = length(vec2(x / (r * polarRiftMainLength(rift)),
        (rift.q.y - centerline) / halfWidth));
    float tear = 1.0 - smoothstep(0.91, 1.07, mainDistance + edge);

    int branches = polarRiftBranchCount(rift);
    for (int i = 0; i < branches; ++i)
    {
        vec4 branch = polarRiftBranchLayout(rift, i);
        float along = ((x / r - branch.x) / branch.y - 0.5) / 0.62;
        if (abs(along) >= 1.22) continue;
        float split = polarRiftBranchOffset(x, rift, branch);
        float distanceToBranch = length(vec2(along,
            (rift.q.y - centerline - split) / (halfWidth * branch.w)));
        float branchTear = 1.0 - smoothstep(0.88, 1.08, distanceToBranch + edge);
        float branchDepth = mix(0.90, 1.0, polarRiftRandom(rift, 166u + uint(i) * 16u));
        tear = max(tear, branchTear * branchDepth);
    }
    return rift.mask * tear;
}

// Conservative XZ corridor for the displaced trunk and every optional branch.
// Main/branch edge noise stays in [-.14,.14], widths in [.78,1.22]*base.
// Full coverage includes the swept contour; fading occurs only where no shape
// can change. A guaranteed full main-cut interior still needs only the cave band.
float polarRiftWallCoverage(LandSite rift)
{
    if (rift.mask <= 0.0 || POLAR_RIFT_WALL_STRENGTH <= 0.0) return 0.0;
    vec2 limit = polarRiftWarpLimit();
    float r = rift.radius;
    float baseWidth = polarRiftHalfWidth(rift);
    float center = polarRiftCenterline(rift.q.x, rift);
    float centerSlope = polarRiftCenterlineSlopeBound(rift);
    float mainLength = polarRiftMainLength(rift);
    vec2 distanceMain = abs(vec2(rift.q.x, rift.q.y - center));
    vec2 mainError = vec2(limit.x, limit.y + centerSlope * limit.x);
    float minimumDistance = length(max(distanceMain - mainError, vec2(0.0))
        / vec2(r * mainLength, baseWidth * 1.22));
    float maximumMain = length((distanceMain + mainError)
        / vec2(r * mainLength, baseWidth * 0.78));

    int branches = polarRiftBranchCount(rift);
    for (int i = 0; i < branches; ++i)
    {
        vec4 branch = polarRiftBranchLayout(rift, i);
        float split = polarRiftBranchOffset(rift.q.x, rift, branch);
        float splitSlope = 1.5 * abs(branch.z / branch.y);
        vec2 distanceBranch = abs(vec2(rift.q.x - r * (branch.x + 0.5 * branch.y),
            rift.q.y - center - split));
        vec2 branchError = vec2(limit.x, limit.y + (centerSlope + splitSlope) * limit.x);
        float branchDistance = length(max(distanceBranch - branchError, vec2(0.0))
            / vec2(r * abs(branch.y) * 0.62, baseWidth * 1.22 * branch.w));
        minimumDistance = min(minimumDistance, branchDistance);
    }

    float outside = 1.0 - smoothstep(1.22, 1.38, minimumDistance);
    // maximumMain + .14 <= .91 guarantees an unchanged, flat main-cut floor.
    float outsideCore = smoothstep(0.65, 0.77, maximumMain);
    return outside * outsideCore * smoothstep(0.0, 0.08, rift.mask);
}

float polarRiftWallBand(LandSite rift)
{
    // landDelta is fully active through 76% of the half band. A full cut's
    // depth plus padding must fit there even at a column on the opposite rim.
    float rawHalfBand = (polarRiftDepth(rift) / 0.35 + 128.0) / 0.76;
    return min(2.0 * rawHalfBand, FIELD_MAX_VOLUME_THICKNESS)
        * polarRiftWallCoverage(rift);
}

#endif
