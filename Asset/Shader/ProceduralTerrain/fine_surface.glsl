#ifndef ENDURANCE_TERRAIN_FINE_SURFACE_GLSL
#define ENDURANCE_TERRAIN_FINE_SURFACE_GLSL

#if TERRAIN_FINE_SURFACE_ENABLED
layout(binding=0) uniform sampler2D terrainFineSurfaceCache;
// RGB = supplied normal map, A = supplied white noise, packed once on load.
layout(binding=1) uniform sampler2D terrainGrainTexture;
uniform float terrainVegetationStainSize = 2.5;
uniform float terrainOtherStainSize = 0.25;
uniform float terrainSparkleGrainSize = 0.002;

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
    if (material.sparkle.x <= 0.0) return;
    // grainSize is metres per source texel, NOT metres per whole image. Both
    // maps use identical UVs, fixed in world space across chunks and LODs.
    vec2 texels = terrainFineProjection(c.position)/terrainSparkleGrainSize;
    vec2 offset = vec2(worldSeed & 1023u, (worldSeed >> 10u) & 1023u);
    vec2 uv = (texels+offset)/vec2(textureSize(terrainGrainTexture,0));
    vec4 grain = textureLod(terrainGrainTexture,uv,0.0); // GL_LINEAR + GL_REPEAT

    // This asset stores signed XY in RG, but positive Z DIRECTLY in B.
    // Mapping B through *2-1 would turn most normals inside the terrain.
    vec3 detailNormal = vec3(grain.rg*2.0-1.0,grain.b);
    detailNormal *= inversesqrt(max(dot(detailNormal,detailNormal),1.0e-8));
    float amount = clamp(material.sparkle.x,0.0,1.0);
    const float normalStrength = 0.55;
    detailNormal = mix(vec3(0.0,0.0,1.0),detailNormal,amount*normalStrength);
    // Derivatives of the SAME world projection as the textures. Project onto
    // the terrain tangent plane so slopes/cliffs retain their underlying shape.
    vec3 perturbation = detailNormal.x*vec3(0.8,0.0,-0.6)
                      + detailNormal.y*vec3(0.6,1.0,0.8);
    perturbation -= normal*dot(normal,perturbation);
    normal = normalize(normal*detailNormal.z+perturbation);

    // A continuous grain material, not glossy islands on a flat substrate.
    // The matching noise varies microroughness; it never paints white pixels
    // into the artist's palette. No independent random coverage/mask remains.
    float grainRoughness = material.sparkle.y/material.sparkle.x;
    float texturedRoughness = grainRoughness + (1.0-grain.a)*0.12;
    roughness = mix(roughness,min(roughness,texturedRoughness),amount);
    // Ordinary G-buffer normal/roughness only. The existing, unmodified PBR
    // shader supplies light direction, Fresnel, shadows and IBL, with no glow
    // baked into albedo and no special material tag or extra render pass.
    // No distance/footprint cut-off: the same bilinear sample applies at range.
}
#endif
#endif
