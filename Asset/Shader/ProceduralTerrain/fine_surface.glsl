#ifndef ENDURANCE_TERRAIN_FINE_SURFACE_GLSL
#define ENDURANCE_TERRAIN_FINE_SURFACE_GLSL

#if TERRAIN_FINE_SURFACE_ENABLED
layout(binding=0) uniform sampler2D terrainFineSurfaceCache;
// Linear moments of compact, fixed facets (C++ bake), with a full mip chain.
#define TERRAIN_GRAIN_SLOPE_MOMENTS 1
layout(binding=1) uniform sampler2D terrainGrainTexture;
uniform float terrainVegetationStainSize = 2.5;
uniform float terrainOtherStainSize = 0.25;
uniform float terrainSparkleGrainSize = 0.01;
uniform vec3 terrainGrainLightDirection = vec3(0.0);
uniform vec3 terrainGrainCameraPosition = vec3(0.0);

// Fixed world projection: stable through material/LOD transitions and one
// lookup per scale. The Y projection also advances at unit rate along walls.
vec2 terrainFineProjection(vec3 p)
{
    // Expanded rotation/shear avoids cancelling two large height terms in U.
    return vec2(0.8*p.x-0.6*p.z,0.6*p.x+0.8*p.z+p.y);
}

float terrainStainCoverage(vec2 p, float footprint, float featureSize)
{
    // Unresolved FBM converges to its expected 50% coverage without a fetch.
    if (footprint >= 4.0*featureSize) return 0.5;
    vec2 offset = vec2(worldSeed & 255u,(worldSeed >> 8u) & 255u)/256.0;
    // 64 base cells in a 512-pixel, eight-octave, thresholded FBM cache.
    float lod = log2(max(footprint*8.0/featureSize,1.0));
    float coverage = textureLod(terrainFineSurfaceCache,p/(64.0*featureSize)+offset,lod).r;
    return mix(coverage,0.5,smoothstep(featureSize,4.0*featureSize,footprint));
}

void terrainApplyFineStains(inout TerrainAppearanceSample material, TerrainPatternContext c)
{
    vec2 correction = vec2(0.0);
    vec2 p = terrainFineProjection(c.position);
    // Blend mask contributions, never frequencies: patterns do not slide when
    // crossing a biome boundary. At most two fetches AFTER all material mixes.
    if (any(greaterThan(material.stains.xy,vec2(0.0))))
        correction += material.stains.xy*terrainStainCoverage(p,c.pixelFootprint,terrainVegetationStainSize);
    if (any(greaterThan(material.stains.zw,vec2(0.0))))
        correction += material.stains.zw*terrainStainCoverage(p,c.pixelFootprint,terrainOtherStainSize);
    material.color *= 1.0-correction.x;
    material.roughness = max(0.04,material.roughness-correction.y);
}

void terrainApplyGranularSurface(inout vec3 normal, inout float roughness,
    TerrainAppearanceSample material, TerrainPatternContext c)
{
    if (material.sparkle.x <= 0.0 || dot(terrainGrainLightDirection,terrainGrainLightDirection)<0.5) return;
    vec3 L = terrainGrainLightDirection;
    vec3 viewDelta = terrainGrainCameraPosition-c.position;
    vec3 V = viewDelta*inversesqrt(max(dot(viewDelta,viewDelta),1.0e-8));
    float nDotL = dot(normal,L), nDotV = dot(normal,V);
    if (nDotL <= 0.0 || nDotV <= 0.0) return;
    vec3 halfVector = L+V;
    vec3 H = halfVector*inversesqrt(max(dot(halfVector,halfVector),1.0e-8));
    float nDotH = dot(normal,H);
    float reflectionRegion = smoothstep(0.8,0.98,nDotH);
    if (reflectionRegion <= 0.0) return;
    float strength = clamp(material.sparkle.x,0.0,1.0);
    vec2 texels = terrainFineProjection(c.position)/terrainSparkleGrainSize;
    vec2 offset = vec2(worldSeed & 1023u, (worldSeed >> 10u) & 1023u);
    vec2 inverseSize = 1.0/vec2(textureSize(terrainGrainTexture,0));
    vec2 uv = (texels+offset)*inverseSize;
    // Derivatives were captured before divergent biome/material branches.
    vec2 dx = terrainFineProjection(c.dx)*inverseSize/terrainSparkleGrainSize;
    vec2 dy = terrainFineProjection(c.dy)*inverseSize/terrainSparkleGrainSize;
    vec4 grain = textureGrad(terrainGrainTexture,uv,dx,dy);
    // Most resolved texels are uncovered; their authored material is exact.
    if (grain.a <= 0.0) return;

    // Decode the covered-facet distribution, independently of its soft edge.
    // This preserves ONE orientation across each grain, instead of cutting
    // thin highlight contours out of continuously changing noise normals.
    float inverseCoverage = 1.0/max(grain.a,0.03);
    vec2 meanSlope = (grain.rg-vec2(128.0/255.0))*(153.0/127.0)*inverseCoverage;
    float variance = max(grain.b*0.36*inverseCoverage-dot(meanSlope,meanSlope),0.0);
    vec3 tangent = vec3(0.8,0.0,-0.6);
    vec3 bitangent = vec3(0.6,1.0,0.8);
    float nDotT = dot(normal,tangent), nDotB = dot(normal,bitangent);
    tangent -= normal*nDotT;
    bitangent -= normal*nDotB;
    float slopeMetric = 0.5*(3.0-nDotT*nDotT-nDotB*nDotB);
    vec3 slope = meanSlope.x*tangent+meanSlope.y*bitangent;
    float slopeSquared = dot(slope,slope), hDotSlope = dot(H,slope);
    // A broad, continuous angular response; the compact coverage mask gives
    // the grain its shape. Filtering broadens the lobe through slope variance.
    float alignment = smoothstep(0.94,0.995,(nDotH+hDotSlope)*inversesqrt(1.0+slopeSquared));
    float participation = strength*alignment*reflectionRegion*smoothstep(0.0,0.08,min(nDotL,nDotV));
    float weight = participation*grain.a;
    float facetRoughness = clamp(material.sparkle.y/material.sparkle.x,0.06,1.0);
    float alpha = facetRoughness*facetRoughness;
    float baseAlpha = roughness*roughness;
    float baseAlphaSquared = max(baseAlpha*baseAlpha,1.0e-6);
    float facetAlphaSquared = min(baseAlphaSquared,alpha*alpha+0.5*variance*slopeMetric);
    // A coverage blend can rotate THROUGH the reflection direction and away
    // again, drawing a bright rim around a dark centre. Stop at its closest
    // approach to H: a stylized material-side correction, bounded by the baked
    // facet slope. Coverage stays fixed in world space; only lighting responds.
    float closestApproach = max(hDotSlope,0.0)/max(nDotH*slopeSquared,1.0e-6);
    normal = normalize(normal+slope*min(weight,closestApproach));
    // Approximate the covered/uncovered GGX peak (proportional to 1/roughness^4).
    // Linear roughness averaging erases the bright subpixel population. This
    // bounded harmonic blend retains a soft distant sheen without forcing LOD0.
    float mixedAlphaSquared = baseAlphaSquared*facetAlphaSquared
        /max(mix(facetAlphaSquared,baseAlphaSquared,weight),1.0e-8);
    roughness = sqrt(sqrt(mixedAlphaSquared));
    // A single-lobe material approximation, not an extra glint BRDF. Subpixel
    // grains become a rougher averaged reflection, with no distance cutoff,
    // emission or color injection. Lighting/shadows remain in forward.fs.glsl.
}
#endif
#endif
