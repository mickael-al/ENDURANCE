// Test-only diagnostic views over the production parameter table and blending.
// The independent CPU interpolation oracle lives in biome_mask_probe.py.

vec4 fieldProbeBlendRelief(vec2 p,vec2 climate)
{
    vec2 grid=clamp(climate,0.0,1.0)*4.0;
    uvec2 cell=min(uvec2(floor(grid)),uvec2(3u));
    vec2 f=fieldFade(grid-vec2(cell));
    uvec4 ids=fieldUnpackSurface(fieldBiomeCells[cell.x+4u*cell.y].x);
    return fieldSurfaceRecipes(p,ids,fieldQuadWeights(f),0.0);
}

struct ProbeBiomeVolume { vec4 volume; vec4 shape; vec4 style; };

ProbeBiomeVolume fieldBiomeVolumeAt(uint biome,vec2 p)
{
    return ProbeBiomeVolume(FIELD_BIOME_VOLUME[biome][0],
        FIELD_BIOME_VOLUME[biome][1],FIELD_BIOME_VOLUME[biome][2]);
}

ProbeBiomeVolume fieldBlendVolume(vec3 biomeCoordinates,vec2 p)
{
    vec3 grid=clamp(biomeCoordinates,0.0,1.0)*4.0;
    uvec3 cell=min(uvec3(floor(grid)),uvec3(3u));
    vec3 f=fieldFade(grid-vec3(cell));
    uvec4 mapping=fieldBiomeCells[cell.x+4u*cell.y+16u*cell.z];
    FieldColumn column;
    column.terrain=vec4(0.0,biomeCoordinates.xy,0.0);
    column.blend=vec4(f.z,0,0,0);
    column.detail=uvec4(0u,mapping.yz,mapping.w);
    ProbeBiomeVolume result;
    fieldVolumeParameters(column,f.xy,result.volume,result.shape,result.style);
    return result;
}
