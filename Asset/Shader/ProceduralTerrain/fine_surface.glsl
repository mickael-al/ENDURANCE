#ifndef ENDURANCE_TERRAIN_FINE_SURFACE_GLSL
#define ENDURANCE_TERRAIN_FINE_SURFACE_GLSL

#if TERRAIN_FINE_SURFACE_ENABLED
layout(binding=0) uniform sampler2D terrainFineSurfaceCache;
uniform float terrainVegetationStainSize = 2.5;
uniform float terrainOtherStainSize = 0.25;
uniform float terrainSparkleGrainSize = 0.002;

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

void terrainApplyGranularSurface(inout vec3 normal, inout float roughness,
    TerrainAppearanceSample material, TerrainPatternContext c)
{
    if (material.sparkle.x <= 0.0) return;
    // Independent world cells, sampled WITHOUT interpolation. No distance or
    // footprint cutoff: this domain never follows the camera or chunk LOD.
    vec3 cells = floor(c.position/terrainSparkleGrainSize);
    if (any(greaterThanEqual(abs(cells),vec3(1.0e9)))) return;
    ivec3 cell = ivec3(cells);
    // Disjoint bits of one avalanche hash: 8 for coverage, 12+12 for the
    // orientation. No extra noise calls or texture fetches per facet.
    uint random = terrainHashCellSalt(cell,0x372cb913u);
    const float facetCoverage = 0.22;
    if ((random>>24u) >= uint(facetCoverage*256.0)) return;

    // Only sparse mineral facets are glossy; the remaining surface retains its
    // authored matte response. Each selected cell has its own orientation, so
    // grains catch the light at DIFFERENT viewing angles instead of exposing
    // one noisy mirror patch with the same normal everywhere.
    float azimuth = (6.28318530718/4096.0)*float(random & 4095u);
    float sampleRadius = float((random>>12u) & 4095u)*(1.0/4096.0);
    const float slopeSpread = 0.28;
    // Truncated isotropic GGX slope distribution. Limit tilts to about 34 deg
    // to retain the terrain's silhouette and prevent near-horizontal facets.
    float u = sampleRadius*0.85;
    float slope = slopeSpread*sqrt(u/(1.0-u));
    vec3 reference = abs(normal.y)<0.999 ? vec3(0.0,1.0,0.0) : vec3(0.0,0.0,1.0);
    vec3 tangent = normalize(cross(reference,normal));
    vec3 bitangent = cross(normal,tangent);
    vec3 facetSlope = slope*(cos(azimuth)*tangent+sin(azimuth)*bitangent);
    float amount = clamp(material.sparkle.x,0.0,1.0);
    normal = normalize(normal+facetSlope*amount);
    float facetRoughness = material.sparkle.y/material.sparkle.x;
    roughness = mix(roughness,min(roughness,facetRoughness),amount);
    // Ordinary G-buffer normal/roughness only. The existing, unmodified PBR
    // shader supplies light direction, Fresnel, shadows and IBL, with no glow
    // baked into albedo and no special material tag or extra render pass.
}
#endif
#endif
