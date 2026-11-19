#version 450 core
#include "palette_common.glsl"
layout(local_size_x=8,local_size_y=8,local_size_z=1) in;
layout(rgba16f,binding=0) writeonly uniform image2D uFogMediumImage;
layout(rgba32f,binding=1) writeonly uniform image2D uFogShapeImage;
uniform vec4 uGameFogOriginSize; // origin X/Z, side length (metres), resolution

void main()
{
    ivec2 texel=ivec2(gl_GlobalInvocationID.xy);
    ivec2 size=imageSize(uFogMediumImage);
    if(any(greaterThanEqual(texel,size))) return;
    vec2 world=uGameFogOriginSize.xy+(vec2(texel)+0.5)/vec2(size)*uGameFogOriginSize.z;
    FieldColumn column=fieldClimateColumn(world);
    vec4 medium=vec4(0.0), shape=vec4(0.0);
    for(uint biome=0u;biome<FIELD_BIOME_COUNT;++biome)
    {
        float weight=fieldSurfaceWeight(column,biome);
        if(weight<=0.0) continue;
        EnduranceSkyStyle style=enduranceStyles[biome];
        medium+=style.fogMedium*weight;
        // Density-weighted controls keep an absent neighbouring medium from
        // changing the height/structure of the present medium.
        shape+=style.fogShape*(weight*style.fogMedium.a);
    }
    if(medium.a>1e-10)
    {
        shape/=medium.a;
        shape.x+=uSeaLevel;
    }
    else shape=vec4(uSeaLevel,150.0,0.0,0.002);
    imageStore(uFogMediumImage,texel,medium);
    imageStore(uFogShapeImage,texel,shape);
}
