#version 450 core
// Biome 17: Savanna. World-space XZ is in metres; vertical recipe values
// are multiplied by detailScale. Keep site arguments identical in both stages.
#define FIELD_BIOME_ID 17u
#include "../../field_batch_common.glsl"
#include "../../field_weights.glsl"
#include "../../biome_landforms_2d.glsl"

// Isolated paired mountains, broad saddles and radial drainage buttresses.
vec4 fieldRelief(vec2 p)
{
    LandSite s=landSite(p,2800.0,1701u,0.62);
    float h=32.0*landFbm(p/2400.0,1702u)+6.0*landFbm(p/650.0,1703u);
    if(s.mask>0.0)
    {
        vec2 q=landWarp(s.q,700.0,50.0,1704u)/s.radius;
        vec2 a=q-vec2(-0.23,-0.08), b=q-vec2(0.27,0.13);
        float pa=max(0.0,1.0-length(a/vec2(0.62,0.72)));
        float pb=max(0.0,1.0-length(b/vec2(0.55,0.61)));
        float massif=max(pa*pa,pb*pb*0.79);
        float ridges=landRidges(p/420.0,1705u);
        // Larger ridges are modulated by smaller ridges and confined to slopes.
        float folds=landRidges(p/150.0,1706u);
        float shoulder=max(0.0,1.0-dot(q,q)/0.84);
        h+=s.mask*(massif*(385.0+105.0*ridges+35.0*ridges*folds)+62.0*shoulder);
    }
    return vec4(h,s.variant>=0.62 ? landBand(s,552.0) : 0.0,0.0,s.mask);
}

#include "../../field_surface_accumulate.glsl"
