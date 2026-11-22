#version 450 core
#include "palette_common.glsl"
layout(local_size_x=1,local_size_y=1,local_size_z=1) in;
layout(std430,binding=11) writeonly buffer EndurancePaletteResult
{
    vec4 resultHorizon;
    vec4 resultMiddle;
    vec4 resultZenith;
    vec4 resultControls; // exposure, humidity, temperature, marine weight
    uint resultBiome;
    float resultWeight;
    vec2 resultReserved;
};
uniform vec2 uPaletteCameraXZ;

void main()
{
    FieldColumn column=fieldClimateColumn(uPaletteCameraXZ);
    vec4 horizon=vec4(0.0),middle=vec4(0.0),zenith=vec4(0.0);
    vec4 controls=vec4(0.0,column.terrain.yz,0.0);
    uint dominantBiome=0u;
    float dominantWeight=0.0;
    for(uint biome=0u;biome<FIELD_BIOME_COUNT;++biome)
    {
        float weight=fieldSurfaceWeight(column,biome);
        if(weight<=0.0) continue;
        // Use the same accumulated per-biome weights as the sky/terrain,
        // including the marine layer. Equal weights keep the lower stable ID.
        if(weight>dominantWeight) { dominantWeight=weight; dominantBiome=biome; }
        EnduranceSkyStyle style=enduranceStyles[biome];
        horizon+=style.horizon*weight;
        middle+=style.middle*weight;
        zenith+=style.zenith*weight;
        controls.x+=style.controls.x*weight;
        if(biome==FIELD_MARINE_BIOME) controls.w=weight;
    }
    resultHorizon=horizon;
    resultMiddle=middle;
    resultZenith=zenith;
    resultControls=controls;
    resultBiome=dominantBiome;
    resultWeight=dominantWeight;
    resultReserved=vec2(0.0);
}
