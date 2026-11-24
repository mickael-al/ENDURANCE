#ifndef ENDURANCE_TERRAIN_FINE_SURFACE_GLSL
#define ENDURANCE_TERRAIN_FINE_SURFACE_GLSL

#if TERRAIN_FINE_SURFACE_ENABLED
layout(binding=0) uniform sampler2D terrainFineSurfaceCache;
layout(binding=1) uniform sampler2D terrainSparkleCache;
uniform float terrainVegetationStainSize = 2.5;
uniform float terrainOtherStainSize = 0.25;
uniform float terrainSparkleGrainSize = 0.01;

// Fixed world projection: stable through material/LOD transitions and one
// lookup per scale. The Y projection also advances at unit rate along walls.
vec2 terrainFineProjection(vec3 p)
{
    return mat2(0.8,0.6,-0.6,0.8) * (p.xz + p.y*vec2(0.6,0.8));
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

void terrainApplySparkle(inout TerrainAppearanceSample material, TerrainPatternContext c)
{
    if (material.sparkle.x <= 0.0 || c.pixelFootprint >= terrainSparkleGrainSize) return;
    float resolved = 1.0-smoothstep(0.25,1.0,c.pixelFootprint/terrainSparkleGrainSize);
    vec2 offset = vec2((worldSeed >> 16u)&127u,worldSeed&127u)/128.0;
    vec3 facet = textureLod(terrainSparkleCache,
        terrainFineProjection(c.position)/(128.0*terrainSparkleGrainSize)+offset,0.0).rgb;
    float amount = material.sparkle.x*resolved;
    float targetRoughness = material.sparkle.y/material.sparkle.x;
    material.roughness = mix(material.roughness,min(material.roughness,targetRoughness),facet.b*amount);
    // Accumulate small facet slopes before the shared tangent projection and
    // final normal normalization, avoiding a second normal calculation.
    // No emission: highlights use the existing PBR lighting.
    vec2 tilt = (facet.rg*255.0-128.0)/127.0;
    vec3 perturbation = vec3(tilt.x,0.6*tilt.x+0.8*tilt.y,tilt.y);
    material.heightGradient += perturbation*(0.05*amount);
}
#endif
#endif
