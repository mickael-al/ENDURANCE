// GPU storage only. Biome functions continue to receive decoded float controls.
// Five scalars + one packed word + one aligned uvec2 = 32 bytes/std430.
#ifndef ENDURANCE_FIELD_STORAGE_GLSL
#define ENDURANCE_FIELD_STORAGE_GLSL
#include "field_limits.glsl"
struct PackedFieldColumn
{
    float height;
    float humidity;
    float temperature;
    float altitudeRatio;
    float globalOffset;
    uint controls; // unscaled thickness UNORM16, relative offset SNORM16
    uvec2 detail;  // IDs, control, 6-bit cell; detail.y bits30/31: active/wide band
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
    // Keep the original constant division on the narrow path, including its
    // floating-point rounding at quantization thresholds.
    float normalized=thickness>FIELD_LEGACY_VOLUME_THICKNESS
        ? thickness/FIELD_MAX_VOLUME_THICKNESS : thickness/FIELD_LEGACY_VOLUME_THICKNESS;
    uint width=uint(round(clamp(normalized,0.0,1.0)*65535.0));
    if(thickness>0.0) width=max(width,1u);
    uint shift=packSnorm2x16(vec2(clamp(offset/160.0,-1.0,1.0),0.0))&65535u;
    return width|(shift<<16u);
}

float fieldStoredThickness(PackedFieldColumn column,float detailScale)
{
    if((column.detail.y&(1u<<30u))==0u) return 0.0;
    float quantum=(column.detail.y&(1u<<31u))!=0u
        ? (FIELD_MAX_VOLUME_THICKNESS/65535.0) : (FIELD_LEGACY_VOLUME_THICKNESS/65535.0);
    float width=float(column.controls&65535u)*quantum*detailScale;
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
