// GPU storage only. Biome functions continue to receive decoded float controls.
// Five scalars + one packed word + one aligned uvec2 = 32 bytes/std430.
#ifndef ENDURANCE_FIELD_STORAGE_GLSL
#define ENDURANCE_FIELD_STORAGE_GLSL
struct PackedFieldColumn
{
    float height;
    float humidity;
    float temperature;
    float altitudeRatio;
    float globalOffset;
    uint controls; // unscaled thickness UNORM16, relative offset SNORM16
    uvec2 detail;  // eight 5-bit IDs, control UNORM16, 6-bit cell, active-band bit
};

vec2 fieldStoredClimate(PackedFieldColumn column)
{
    return vec2(column.humidity,column.temperature);
}

// Quantization happens once, after FP32 accumulation. Keep a nonzero code for
// every originally active band; the metadata bit preserves its exact open/closed
// decision around 0.0002 even when detailScale changes the quantization interval.
uint fieldPackControls(float thickness,float offset)
{
    uint width=uint(round(clamp(thickness/576.0,0.0,1.0)*65535.0));
    if(thickness>0.0) width=max(width,1u);
    uint shift=packSnorm2x16(vec2(clamp(offset/160.0,-1.0,1.0),0.0))&65535u;
    return width|(shift<<16u);
}

float fieldStoredThickness(PackedFieldColumn column,float detailScale)
{
    if((column.detail.y&(1u<<30u))==0u) return 0.0;
    float width=float(column.controls&65535u)*(576.0/65535.0)*detailScale;
    return max(width,uintBitsToFloat(floatBitsToUint(0.0002)+1u));
}

float fieldStoredOffset(PackedFieldColumn column)
{
    return unpackSnorm2x16(column.controls>>16u).x*160.0;
}

float fieldStoredControl(PackedFieldColumn column)
{
    return float((column.detail.y>>8u)&65535u)*(1.0/65535.0);
}
#endif
