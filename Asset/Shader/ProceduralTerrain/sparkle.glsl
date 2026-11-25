#ifndef ENDURANCE_TERRAIN_SPARKLE_GLSL
#define ENDURANCE_TERRAIN_SPARKLE_GLSL

// Artistic terrain adaptation of Wang & Bowles, EGSR 2016, sections 3.1/3.2/3.5.
// Fixed jittered centers, compact shapes, adaptive spacing and grazing coverage.
// Unlike their example, no view/time offset moves the grid. Two stationary
// levels crossfade; no normal-map mip averages away the discrete highlights.
// Controls are independent: screen width, screen spacing, material strength.
const float TERRAIN_SPARKLE_WIDTH = 0.1; // half-size kernel, filtered over each pixel
const float TERRAIN_SPARKLE_SPACING = 2.0; // minimum grid width in pixels
const float TERRAIN_SPARKLE_JITTER = 0.64; // center range 0.28..0.72 of a cell

float terrainSparkleIntegral(float x)
{
    // Analytic CDF of the same compact cubic B-spline. Integrating the smaller
    // point over a pixel keeps it from flashing as it crosses pixel centers.
    bool negative = x < 0.0;
    x = abs(x);
    float a = max(2.0-x,0.0);
    float value = x < 1.0
        ? 0.5 + x*((2.0/3.0) + x*x*(-1.0/3.0 + x*0.125))
        : 1.0 - (a*a)*(a*a)/24.0;
    return negative ? 1.0-value : value;
}

float terrainSparklePixelCoverage(float x)
{
    float upper = terrainSparkleIntegral((x+0.5)/TERRAIN_SPARKLE_WIDTH);
    float lower = terrainSparkleIntegral((x-0.5)/TERRAIN_SPARKLE_WIDTH);
    // Preserve the peak control; the integral over the screen stays constant
    // under subpixel translation. This denominator is compile-time constant.
    float peak = 2.0*terrainSparkleIntegral(0.5/TERRAIN_SPARKLE_WIDTH)-1.0;
    return max(upper-lower,0.0)/peak;
}

float terrainSparkleLevel(vec2 p, mat2 screenFromWorld, float spacing,
    int level, vec3 N, vec3 H)
{
    vec2 q = p/spacing;
    if (any(greaterThanEqual(abs(q),vec2(1.0e9)))) return 0.0;
    ivec2 cell = ivec2(floor(q));
    uint h = terrainHashCellSalt(ivec3(cell,level),0x51a7c3e9u);
    vec3 random = vec3(h & 1023u,(h >> 10u) & 1023u,h >> 20u)
        / vec3(1023.0,1023.0,4095.0);
    // More placement freedom, still hashed only from the fixed world cell.
    // The spacing bound below also contains the pixel filter's extra support.
    vec2 center = vec2(0.5)+TERRAIN_SPARKLE_JITTER*(random.xy-0.5);
    vec2 delta = screenFromWorld*((fract(q)-center)*spacing);
    float support = 2.0*TERRAIN_SPARKLE_WIDTH+0.5;
    if (any(greaterThanEqual(abs(delta),vec2(support)))) return 0.0;
    float coverage = terrainSparklePixelCoverage(delta.x)*terrainSparklePixelCoverage(delta.y);
    if (coverage <= 0.0) return 0.0;

    // Fixed broad orientation response per point, not a contour cut out of
    // interpolated normals. This affects brightness, never its center/shape.
    vec3 tangent = normalize(cross(abs(N.y)<0.9 ? vec3(0,1,0) : vec3(1,0,0),N));
    vec3 bitangent = cross(N,tangent);
    vec3 facet = normalize(N+0.5*((random.x-0.5)*tangent+(random.y-0.5)*bitangent));
    float angular = smoothstep(0.86,0.995,dot(facet,H));
    return coverage*angular*mix(0.5,1.0,random.z);
}

void terrainApplyGranularSurface(inout vec3 normal, inout float roughness,
    TerrainAppearanceSample material, TerrainPatternContext c)
{
    if (material.sparkle.x <= 0.0 || dot(terrainGrainLightDirection,terrainGrainLightDirection) < 0.5) return;
    vec3 L = terrainGrainLightDirection;
    vec3 viewDelta = terrainGrainCameraPosition-c.position;
    vec3 V = viewDelta*inversesqrt(max(dot(viewDelta,viewDelta),1.0e-8));
    float nDotL = dot(normal,L), nDotV = dot(normal,V);
    if (nDotL <= 0.0 || nDotV <= 0.0) return;
    vec3 H = normalize(L+V);
    float reflectionRegion = smoothstep(0.80,0.98,dot(normal,H));
    if (reflectionRegion <= 0.0) return;

    // Derivatives were captured before material/lighting divergence in render.fs.
    // Full 2x2 metric keeps points round on screen even on grazing surfaces.
    vec2 dx = terrainFineProjection(c.dx), dy = terrainFineProjection(c.dy);
    float footprint = sqrt(dot(dx,dx)+dot(dy,dy));
    float determinant = dx.x*dy.y-dy.x*dx.y;
    if (abs(determinant) <= max(1.0e-20,footprint*footprint*1.0e-7)) return;
    mat2 screenFromWorld = mat2(dy.y,-dx.y,-dy.x,dx.x)/determinant;
    float minimumSpacing = max(terrainSparkleGrainSize,0.001);
    // Decouple point width from density. At the defaults this retains the
    // previous spacing; the bound also keeps future width/jitter edits safe.
    float margin = 0.5*(1.0-TERRAIN_SPARKLE_JITTER);
    float support = 2.0*TERRAIN_SPARKLE_WIDTH+0.5;
    float gridWidth = max(TERRAIN_SPARKLE_SPACING,1.414214*support/margin);
    float level = max(log2(max(gridWidth*footprint/minimumSpacing,1.0e-8))+1.0,0.0);
    float lower = floor(level);
    float spacing = minimumSpacing*exp2(lower);
    // +1 in the level calculation makes even the LOWER grid satisfy the
    // containment bound: margin*spacing >= sqrt(2)*support*footprint.
    vec2 p = terrainFineProjection(c.position);
    float first = terrainSparkleLevel(p,screenFromWorld,spacing,int(lower),normal,H);
    float second = terrainSparkleLevel(p,screenFromWorld,2.0*spacing,int(lower)+1,normal,H);
    float coverage = mix(first,second,smoothstep(0.0,1.0,fract(level)));
    float visibility = smoothstep(0.0,0.08,min(nDotL,nDotV));
    float weight = clamp(material.sparkle.x,0.0,1.0)*coverage*visibility*reflectionRegion;
    if (weight <= 0.0) return;

    // Material-side adapter for the UNCHANGED engine PBR. Aim selected normals
    // at H, then blend GGX peak powers (~1/r^4), not roughness linearly: a linear
    // roughness blend turns smooth coverage into needle-like, unstable peaks.
    // The small edge ramp avoids a normal discontinuity at compact support.
    // This is an approximation: IBL/diffuse still see the selected G-buffer normal.
    float facetRoughness = min(roughness,clamp(material.sparkle.y/material.sparkle.x,0.10,1.0));
    normal = normalize(mix(normal,H,smoothstep(0.0,0.06,weight)));
    float basePower = 1.0/pow(max(roughness,0.04),4.0);
    float facetPower = 1.0/pow(facetRoughness,4.0);
    roughness = inversesqrt(sqrt(mix(basePower,facetPower,weight)));
}
#endif
