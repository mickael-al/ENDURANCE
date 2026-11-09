// Optional shared starting point for the independent biome 3D recipes.
// A biome may replace the helper call with its own density construction.
#ifndef ENDURANCE_BIOME_VOLUME_COMMON_GLSL
#define ENDURANCE_BIOME_VOLUME_COMMON_GLSL

#include "field_noise_3d.glsl"

// The original single-recipe evaluator, with already scaled column controls.
// Positive density is solid. This internal helper returns a complete density;
// public fieldVolume functions return only a DELTA to height - p.y.
float fieldRecipeDensityAtPrepared(vec3 p,vec2 terrain,vec4 volume,vec4 shape,vec4 style)
{
    if(uFieldMode==1u) return uBaseHeight-p.y;
    if(uFieldMode==2u) return uSphereRadius-length(p-uSphereCenter);
    float base=terrain.x-p.y;
    if(terrain.y<=0.0001 || volume.y+volume.z<=0.0) return base;
    float edgeDistance=terrain.y-abs(p.y-volume.x);
    if(edgeDistance<=0.0) return base;
    float influence=smoothstep(0.0,volume.w,edgeDistance);
    vec3 n=vec3(fieldNoise3(p*0.0125,0x299f31d0u),
        fieldNoise3(p*vec3(0.00625,0.020,0.00625),0x082efa98u),
        fieldNoise3(p*vec3(0.018,0.005,0.018),0xec4e6c89u));
    float a=dot(shape.xyz,n);
    float displacement=mix(a,0.6*a+0.4*n.x,style.z)*volume.y;
    float density=base+influence*displacement;
    if(volume.z<=0.0) return density;
    float b=0.75*n.y-0.65*n.z;
    vec2 d=abs(vec2(a,b));
    float tube=mix(length(d),(d.x+d.y)*0.75,clamp(style.y*2.0,0.0,1.0));
    tube=mix(tube,max(d.x,d.y),clamp(style.y*2.0-1.0,0.0,1.0));
    float cavity=mix(abs(a),tube,style.x);
    float layer=abs(2.0*fract(p.y*0.025+0.08*n.x)-1.0);
    float layerWidth=mix(0.55,1.35,smoothstep(0.2,0.8,layer));
    float radius=shape.w*mix(1.0,layerWidth,style.w);
    float cavityDensity=(cavity-radius)*volume.z;
    return mix(density,min(density,cavityDensity),influence);
}

// Default band contract shared with CPU conservative height bounds. The total
// thickness belongs to the accumulated 2D relief. Zero closes the band. Keep
// custom geometry inside the same envelope unless the CPU bounds are extended.
vec2 fieldVolumeBand(FieldColumn column, float recipeOffset)
{
    float offset=column.blend.y
        +clamp(column.blend.z+recipeOffset,-160.0,160.0)*uDetailScale;
    return vec2(column.terrain.x+offset,0.5*column.terrain.w);
}

// Controls belong to this biome only; none are interpolated with another biome.
// volume: offset/displacement/carving/feather; shape: noise weights/radius;
// style: tubes/angularity/roughness/strata. The owning biome prepares displacement
// and carving amplitudes in world metres. This helper never interprets blend.w;
// each independent fieldVolume decides what its extra 2D control means.
float fieldVolumeRecipeDelta(vec3 p, FieldColumn column,
                            vec4 volume, vec4 shape, vec4 style)
{
    if(uFieldMode!=0u || uDetailScale<=0.0 || column.terrain.w<=0.0002)
        return 0.0;
    vec2 band=fieldVolumeBand(column,volume.x);
    if(abs(p.y-band.x)>=band.y) return 0.0;
    volume.x=band.x;
    volume.w=max(0.0001,band.y*volume.w);
    float base=column.terrain.x-p.y;
    return fieldRecipeDensityAtPrepared(p,vec2(column.terrain.x,band.y),
                                       volume,shape,style)-base;
}

#endif
