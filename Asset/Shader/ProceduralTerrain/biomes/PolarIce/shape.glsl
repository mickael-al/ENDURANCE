// The same fissure footprint positions the 2D incision and its 3D floor cave.
// Shared scalar placement noise keeps this independent of either noise library.
#ifndef ENDURANCE_POLAR_ICE_SHAPE_GLSL
#define ENDURANCE_POLAR_ICE_SHAPE_GLSL

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
    float halfWidth = mix(80.0, 105.0, rift.variant) * (0.78 + 0.44 * widthNoise);
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

#endif
