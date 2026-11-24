#ifndef ENDURANCE_TERRAIN_FINE_SURFACE_GLSL
#define ENDURANCE_TERRAIN_FINE_SURFACE_GLSL

#if TERRAIN_FINE_SURFACE_ENABLED
layout(binding=0) uniform sampler2D terrainFineSurfaceCache;

// Ground-only projection: no three-way triplanar lookup. The slight Y shear
// limits stretching on slopes; filtering uses the conservative world footprint.
vec2 terrainFineProjection(vec3 p)
{
    return mat2(0.8,0.6,-0.6,0.8) * (p.xz + p.y*vec2(0.17,-0.11));
}

void terrainApplyFineStains(inout TerrainAppearanceSample material, TerrainPatternContext c)
{
    vec2 controls = material.stains;
    if (controls.x <= 0.0 && controls.y <= 0.0) return;
    float stain = 0.5;
    if (c.pixelFootprint < 1.0)
    {
        // 16 m repeat, 64 base cells: about 25 cm patches, 8 baked octaves.
        // Isotropic LOD from the already-captured world footprint stays defined
        // in divergent branches. No new per-fragment derivative calculation.
        vec2 offset = vec2(worldSeed & 255u,(worldSeed >> 8u) & 255u)/256.0;
        float lod = log2(max(c.pixelFootprint*32.0,1.0));
        float coverage = textureLod(terrainFineSurfaceCache,
            terrainFineProjection(c.position)/16.0+offset,lod).r;
        // Unresolved detail converges to expected 50% coverage. Far fragments
        // keep the mean albedo/roughness and skip the texture entirely.
        stain = mix(coverage,0.5,smoothstep(0.25,1.0,c.pixelFootprint));
    }
    material.color *= 1.0-controls.x*stain;
    material.roughness = max(0.04,material.roughness-controls.y*stain);
}
#endif
#endif
