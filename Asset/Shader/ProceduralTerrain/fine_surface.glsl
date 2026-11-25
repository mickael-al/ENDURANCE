#ifndef ENDURANCE_TERRAIN_FINE_SURFACE_GLSL
#define ENDURANCE_TERRAIN_FINE_SURFACE_GLSL

#if TERRAIN_FINE_SURFACE_ENABLED
layout(binding=0) uniform sampler2D terrainFineSurfaceCache;
// RGB = supplied normal map, A = supplied white noise, packed once on load.
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
    if (material.sparkle.x <= 0.0 || dot(terrainGrainLightDirection,terrainGrainLightDirection) < 0.5) return;
    vec3 L = terrainGrainLightDirection; // Normalized once on the CPU, per draw.
    vec3 viewDelta = terrainGrainCameraPosition-c.position;
    vec3 V = viewDelta*inversesqrt(max(dot(viewDelta,viewDelta),1.0e-8));
    float nDotL = dot(normal,L), nDotV = dot(normal,V);
    if (nDotL <= 0.0 || nDotV <= 0.0) return;
    vec3 halfVector = L+V;
    vec3 H = halfVector*inversesqrt(max(dot(halfVector,halfVector),1.0e-8));

    // Keep the glitter in the broad reflected-sun region. This inexpensive
    // angular test also skips the image lookup elsewhere; it is not a distance
    // or pixel-footprint limit. The normal map still chooses the individual hits.
    float reflectionRegion = smoothstep(0.80,0.98,dot(normal,H));
    if (reflectionRegion <= 0.0) return;

    // Metres per source texel, not per entire image. The 1024px repeat now
    // spans 10.24m at the 1cm default (five times the previous 2.048m).
    vec2 texels = terrainFineProjection(c.position)/terrainSparkleGrainSize;
    vec2 offset = vec2(worldSeed & 1023u, (worldSeed >> 10u) & 1023u);
    vec2 textureExtent = vec2(textureSize(terrainGrainTexture,0));
    vec2 uv = (texels+offset)/textureExtent;
    // Use the mip chain with derivatives captured before material branches.
    vec2 dx = terrainFineProjection(c.dx)/(terrainSparkleGrainSize*textureExtent);
    vec2 dy = terrainFineProjection(c.dy)/(terrainSparkleGrainSize*textureExtent);
    vec4 grain = textureGrad(terrainGrainTexture,uv,dx,dy);

    // The supplied normal encodes signed XY in RG, positive Z directly in B.
    vec3 detailNormal = vec3(grain.rg*2.0-1.0,grain.b);
    detailNormal *= inversesqrt(max(dot(detailNormal,detailNormal),1.0e-8));
    detailNormal = mix(vec3(0.0,0.0,1.0),detailNormal,0.35);
    vec3 perturbation = detailNormal.x*vec3(0.8,0.0,-0.6)
                      + detailNormal.y*vec3(0.6,1.0,0.8);
    perturbation -= normal*dot(normal,perturbation);
    vec3 facetNormal = normalize(normal*detailNormal.z+perturbation);

    // Only facets oriented towards this light/view reflection become smooth.
    // Everywhere else retains the authored matte substrate: no field of glossy
    // normals, diffuse speckling or global roughness drop. Noise controls grain
    // participation; it never brightens the albedo or writes emission.
    float alignment = smoothstep(0.990,0.9995,dot(facetNormal,H));
    float population = smoothstep(0.45,0.80,grain.a);
    float visibility = smoothstep(0.0,0.08,min(nDotL,nDotV));
    float weight = clamp(material.sparkle.x,0.0,1.0)*alignment*population*visibility*reflectionRegion;
    float facetRoughness = clamp(material.sparkle.y/material.sparkle.x,0.06,1.0);
    normal = normalize(mix(normal,facetNormal,weight));
    roughness = mix(roughness,min(roughness,facetRoughness),weight);
    // This is a material-side approximation of directional glints, evaluated
    // each draw because H changes with camera/sun motion. The existing PBR still
    // applies all lighting, shadow maps, cloud shadows and aerial perspective.
    // No forward shader change, extra pass, light baked into color, or distance
    // cutoff. As with any G-buffer normal, IBL sees the selected facets too.
}
#endif
#endif
