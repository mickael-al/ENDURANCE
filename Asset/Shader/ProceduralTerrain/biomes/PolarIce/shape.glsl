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

float polarRiftCenterline(float x, LandSite rift)
{
    float broad = landPlacementValue(vec2(x / (rift.radius * 0.50),
        rift.variant * 19.0), 113u) - 0.5;
    float detail = landPlacementValue(vec2(x / (rift.radius * 0.19),
        rift.variant * 31.0), 114u) - 0.5;
    return rift.radius * (0.14 * broad + 0.055 * detail);
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
    // An elliptical longitudinal envelope closes to a point naturally. Noise
    // changes the entire contour, rather than perturbing a rectangular slot.
    float mainDistance = length(vec2(x / (r * 0.86),
        (rift.q.y - centerline) / halfWidth));
    float mainTear = 1.0 - smoothstep(0.91, 1.07, mainDistance + edge);

    // A narrower, tapering split departs from the main seam on either side.
    float side = rift.variant < 0.5 ? -1.0 : 1.0;
    float split = side * r * 0.35 * smoothstep(-0.22, 0.62, x / r);
    float branchDistance = length(vec2((x - r * 0.22) / (r * 0.42),
        (rift.q.y - centerline - split) / (halfWidth * 0.42)));
    float branchTear = 1.0 - smoothstep(0.88, 1.08, branchDistance + edge);
    return rift.mask * max(mainTear, branchTear * 0.96);
}

// Conservative XZ corridor for every possible displaced wall. The bounded
// noise in polarRiftCenterline has |dY/dX| <= 1.875*(.14/.50+.055/.19).
// Main and branch edge noise lie in [-.14,.14]; widths in [.78,1.22]*base.
// Full coverage includes the complete swept contour; fading occurs only where
// the cut cannot change. Flat floor interiors keep the ordinary cave band.
float polarRiftWallCoverage(LandSite rift)
{
    if (rift.mask <= 0.0 || POLAR_RIFT_WALL_STRENGTH <= 0.0) return 0.0;
    vec2 limit = polarRiftWarpLimit();
    float r = rift.radius;
    float baseWidth = polarRiftHalfWidth(rift);
    float center = polarRiftCenterline(rift.q.x, rift);
    float centerSlope = 1.875 * (0.14 / 0.50 + 0.055 / 0.19);
    vec2 distanceMain = abs(vec2(rift.q.x, rift.q.y - center));
    vec2 mainError = vec2(limit.x, limit.y + centerSlope * limit.x);
    float minimumMain = length(max(distanceMain - mainError, vec2(0.0))
        / vec2(r * 0.86, baseWidth * 1.22));
    float maximumMain = length((distanceMain + mainError)
        / vec2(r * 0.86, baseWidth * 0.78));

    float side = rift.variant < 0.5 ? -1.0 : 1.0;
    float split = side * r * 0.35 * smoothstep(-0.22, 0.62, rift.q.x / r);
    float splitSlope = 0.35 * 1.5 / (0.62 + 0.22);
    vec2 distanceBranch = abs(vec2(rift.q.x - r * 0.22,
        rift.q.y - center - split));
    vec2 branchError = vec2(limit.x,
        limit.y + (centerSlope + splitSlope) * limit.x);
    float minimumBranch = length(max(distanceBranch - branchError, vec2(0.0))
        / vec2(r * 0.42, baseWidth * 1.22 * 0.42));

    float outside = 1.0 - smoothstep(1.22, 1.38, min(minimumMain, minimumBranch));
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
