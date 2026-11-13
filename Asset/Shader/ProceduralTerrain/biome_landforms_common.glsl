// Placement shared by independent relief and volume programs. No 2D/3D noise
// library dependency: a 2D-noise edit must not invalidate a pure 3D sculpture.
#ifndef ENDURANCE_BIOME_LANDFORMS_COMMON_GLSL
#define ENDURANCE_BIOME_LANDFORMS_COMMON_GLSL
#include "../Noise/noise_common.glsl"

struct LandSite
{
    vec2 q;       // rotated offset from the anchor, world metres in XZ
    vec2 center;  // unrotated world-space anchor
    float radius;
    float mask;
    float variant;
};

// A candidate's complete support fits inside its own placement cell:
// jitter <= .16 S and radius <= .28 S leave a .06 S boundary margin.
// Thus neighboring sites cannot overlap, no neighbor search or SSBO is needed,
// and all site contributions vanish before floor() selects a different cell.
// Empty cells, jitter, radius, orientation and shape variant break regularity.
LandSite landSite(vec2 p,float spacing,uint salt,float occupancy)
{
    float size=max(spacing,16.0);
    ivec2 cell=ivec2(floor(p/size));
    uint h=noiseInternalHash(uint(cell.x)*0x9e3779b9u
        ^uint(cell.y)*0x85ebca6bu^uSeed^salt);
    float choice=noiseInternalUintTo01(noiseInternalHash(h^0xa511e9b3u));
    vec2 jitter=vec2(noiseInternalUintTo01(noiseInternalHash(h^0x63d83595u)),
        noiseInternalUintTo01(noiseInternalHash(h^0xb5297a4du)))-0.5;
    vec2 center=(vec2(cell)+0.5+jitter*0.32)*size;
    float variant=noiseInternalUintTo01(noiseInternalHash(h^0x1b56c4e9u));
    float radius=size*mix(0.20,0.28,variant);
    vec2 direction=vec2(noiseInternalUintTo01(noiseInternalHash(h^0x7f4a7c15u)),
        noiseInternalUintTo01(noiseInternalHash(h^0x94d049bbu)))*2.0-1.0;
    direction=dot(direction,direction)>0.0001 ? normalize(direction) : vec2(1,0);
    vec2 delta=p-center;
    vec2 q=vec2(dot(delta,direction),dot(delta,vec2(-direction.y,direction.x)));
    float mask=choice<clamp(occupancy,0.0,1.0)
        ? 1.0-smoothstep(0.64,1.0,length(q)/radius) : 0.0;
    return LandSite(q,center,radius,mask,variant);
}

#endif
