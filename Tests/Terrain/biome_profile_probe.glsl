// Test-only diagnostics for the former parameter blend. Runtime evaluates
// independent density functions and never calls these helpers.

BiomeVolume fieldBiomeVolume(uint biome)
{
    if(biome==0u) return polarIceVolume();
    if(biome==1u) return frozenTundraVolume();
    if(biome==2u) return tundraVolume();
    if(biome==3u) return taigaVolume();
    if(biome==4u) return borealBogVolume();
    if(biome==5u) return rockyTundraVolume();
    if(biome==6u) return coldSteppeVolume();
    if(biome==7u) return borealForestVolume();
    if(biome==8u) return coldForestVolume();
    if(biome==9u) return coldMarshVolume();
    if(biome==10u) return dryPlainVolume();
    if(biome==11u) return meadowVolume();
    if(biome==12u) return temperateForestVolume();
    if(biome==13u) return temperateRainforestVolume();
    if(biome==14u) return temperateMarshVolume();
    if(biome==15u) return rockDesertVolume();
    if(biome==16u) return drySavannaVolume();
    if(biome==17u) return savannaVolume();
    if(biome==18u) return dryJungleVolume();
    if(biome==19u) return tropicalJungleVolume();
    if(biome==20u) return volcanicDesertVolume();
    if(biome==21u) return redDesertVolume();
    if(biome==22u) return aridPlateauVolume();
    if(biome==23u) return burningJungleVolume();
    if(biome==24u) return tropicalMangroveVolume();
    return marineVolume();
}

BiomeVolume fieldBiomeVolumeAt(uint biome, vec2 p)
{
    BiomeVolume result = fieldBiomeVolume(biome);
    if(biome==0u)
    {
        float activity = polarVolumeActivity(p);
        result.volume.yz *= activity;
        // Scaling carving strength alone does not remove its negative sign.
        // Also close the cavity radius smoothly towards inactive columns.
        result.shape.w *= activity;
    }
    return result;
}

BiomeVolume fieldBlendVolume(vec3 biomeCoordinates,vec2 p)
{
    vec3 grid=clamp(biomeCoordinates,0.0,1.0)*4.0;
    uvec3 cell=min(uvec3(floor(grid)),uvec3(3u));
    vec3 f=fieldFade(grid-vec3(cell));
    uvec4 mapping=fieldBiomeCells[cell.x+4u*cell.y+16u*cell.z];
    FieldColumn column;
    column.terrain=vec4(0.0,biomeCoordinates.xy,0.0);
    column.blend=vec4(f.z,0,0,0);
    column.detail=uvec4(0u,mapping.yz,0u);
    FieldBiomeWeights weights=fieldVolumeWeights(column,f.xy);
    // This explicit coordinate probe is independent of the scene override.
    weights.low=fieldQuadWeights(f.xy)*(1.0-f.z);
    weights.high=fieldQuadWeights(f.xy)*f.z;
    BiomeVolume result;
    result.volume=vec4(0.0); result.shape=vec4(0.0); result.style=vec4(0.0);
    for(uint biome=0u;biome<FIELD_BIOME_COUNT;++biome)
    {
        float weight=fieldVolumeWeight(weights,biome);
        if(weight<=0.0) continue;
        BiomeVolume value=fieldBiomeVolumeAt(biome,p);
        result.volume+=value.volume*weight;
        result.shape+=value.shape*weight;
        result.style+=value.style*weight;
    }
    return result;
}
