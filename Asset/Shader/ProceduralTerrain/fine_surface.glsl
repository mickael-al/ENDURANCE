#ifndef ENDURANCE_TERRAIN_FINE_SURFACE_GLSL
#define ENDURANCE_TERRAIN_FINE_SURFACE_GLSL

#if TERRAIN_FINE_SURFACE_ENABLED
layout(binding=0) uniform sampler2D terrainFineSurfaceCache;
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
    // One independent value per world-space cell: nearest-equivalent white
    // noise, with no interpolation, FBM, texture tile or normal perturbation.
    vec3 cells = floor(c.position/terrainSparkleGrainSize);
    if (any(greaterThanEqual(abs(cells),vec3(1.0e9)))) return;
    float grain = terrainCornerValueSalt(ivec3(cells),0x372cb913u);
    // Contrast the raw scalar, as the reference contrasts its specular mask.
    // A bounded roughness modulation keeps real lights, shadows and IBL intact
    // in the existing deferred path without another material channel or pass.
    float glitter = grain*grain;
    glitter *= glitter;
    float amount = material.sparkle.x*resolved*glitter;
    float targetRoughness = material.sparkle.y/material.sparkle.x;
    material.roughness = mix(material.roughness,min(material.roughness,targetRoughness),amount);
}
#endif
#endif
