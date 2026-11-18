#version 450 core
#include "palette_common.glsl"
layout(local_size_x=1,local_size_y=1,local_size_z=1) in;
layout(std430,binding=11) writeonly buffer EndurancePaletteResult
{
    vec4 resultHorizon;
    vec4 resultMiddle;
    vec4 resultZenith;
    vec4 resultControls; // exposure, humidity, temperature, marine weight
};
uniform vec2 uPaletteCameraXZ;

void main()
{
    FieldColumn column=fieldClimateColumn(uPaletteCameraXZ);
    vec4 horizon=vec4(0.0),middle=vec4(0.0),zenith=vec4(0.0);
    vec4 controls=vec4(0.0,column.terrain.yz,0.0);
    for(uint biome=0u;biome<FIELD_BIOME_COUNT;++biome)
    {
        float weight=fieldSurfaceWeight(column,biome);
        if(weight<=0.0) continue;
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
}
