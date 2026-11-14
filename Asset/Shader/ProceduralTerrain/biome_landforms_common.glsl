// Placement shared by independent relief and volume programs. No 2D/3D noise
// library dependency: a 2D-noise edit must not invalidate a pure 3D sculpture.
#ifndef ENDURANCE_BIOME_LANDFORMS_COMMON_GLSL
#define ENDURANCE_BIOME_LANDFORMS_COMMON_GLSL
#include "../Noise/noise_common.glsl"

struct LandSite
{
    vec2 q;       // rotated offset in the input coordinate domain, in metres
    vec2 center;  // anchor in that domain (warped for landSparseSite)
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

// RKM is a nominal occurrence rate per kilometre, not kilometres per site:
// .25 -> 4 km cells, .5 -> 2 km, 1 -> 1 km, 2 -> .5 km.
// One jittered candidate per square cell; a walking route need not intersect
// every footprint. Different feature salts provide independent populations.
LandSite landRkmSite(vec2 p,float rkm,uint salt)
{
    return landSite(p,1000.0/max(rkm,0.001),salt,1.0);
}

// Smooth hash-only noise in [0,1]. Placement is identical in the separate
// relief and volume programs, without importing either noise dimension.
float landPlacementCorner(ivec2 cell,uint salt)
{
    return noiseInternalUintTo01(noiseInternalHash(uint(cell.x)*0x9e3779b9u
        ^uint(cell.y)*0x85ebca6bu^uSeed^salt));
}

float landPlacementValue(vec2 p,uint salt)
{
    ivec2 cell=ivec2(floor(p));
    vec2 f=fract(p);
    f=f*f*f*(f*(f*6.0-15.0)+10.0);
    return mix(mix(landPlacementCorner(cell,salt),
                   landPlacementCorner(cell+ivec2(1,0),salt),f.x),
               mix(landPlacementCorner(cell+ivec2(0,1),salt),
                   landPlacementCorner(cell+ivec2(1,1),salt),f.x),f.y);
}

vec2 landSparseDirection(uint salt)
{
    float angle=noiseInternalUintTo01(noiseInternalHash(uSeed^salt^0x39ad237bu))*6.2831853;
    return vec2(cos(angle),sin(angle));
}

vec2 landSparseDomain(vec2 p,float spacing,uint salt)
{
    float size=max(spacing,16.0);
    vec2 direction=landSparseDirection(salt);
    vec2 r=vec2(dot(p,direction),dot(p,vec2(-direction.y,direction.x)));
    vec2 broad=vec2(landPlacementValue(r/(size*4.0),salt^0x5717u),
                    landPlacementValue(r/(size*4.0),salt^0xa38bu))-0.5;
    vec2 detail=vec2(landPlacementValue(r/(size*1.6),salt^0x137bu),
                     landPlacementValue(r/(size*1.6),salt^0xc943u))-0.5;
    // Jacobian perturbation norm <= .8/4*3.75 + .06/1.6*3.75 < .891.
    // The warp cannot fold, so compact supports remain continuous in world XZ.
    return r+size*(0.8*broad+0.06*detail);
}

// Independent spacing and footprint. Sparse, strongly jittered anchors in a
// rotated, smoothly distorted domain remove aligned rows. The complete support
// still fits its cell with a .025*S margin: crossing cells cannot create seams.
// center is a DOMAIN anchor, never a world position; q already contains warp.
LandSite landSparseSite(vec2 p,float spacing,float footprintScale,uint salt,float occupancy)
{
    float size=max(spacing,16.0);
    float footprint=clamp(footprintScale,0.01,1.0);
    vec2 domain=landSparseDomain(p,size,salt);
    ivec2 cell=ivec2(floor(domain/size));
    uint h=noiseInternalHash(uint(cell.x)*0x9e3779b9u
        ^uint(cell.y)*0x85ebca6bu^uSeed^salt);
    float choice=noiseInternalUintTo01(noiseInternalHash(h^0xa511e9b3u));
    vec2 jitter=vec2(noiseInternalUintTo01(noiseInternalHash(h^0x63d83595u)),
        noiseInternalUintTo01(noiseInternalHash(h^0xb5297a4du)))-0.5;
    float margin=0.5-0.28*footprint-0.025;
    vec2 center=(vec2(cell)+0.5+jitter*(2.0*margin))*size;
    float variant=noiseInternalUintTo01(noiseInternalHash(h^0x1b56c4e9u));
    float radius=size*mix(0.20,0.28,variant)*footprint;
    vec2 direction=vec2(noiseInternalUintTo01(noiseInternalHash(h^0x7f4a7c15u)),
        noiseInternalUintTo01(noiseInternalHash(h^0x94d049bbu)))*2.0-1.0;
    direction=dot(direction,direction)>0.0001 ? normalize(direction) : vec2(1,0);
    vec2 delta=domain-center;
    vec2 q=vec2(dot(delta,direction),dot(delta,vec2(-direction.y,direction.x)));
    float mask=choice<clamp(occupancy,0.0,1.0)
        ? 1.0-smoothstep(0.64,1.0,length(q)/radius) : 0.0;
    return LandSite(q,center,radius,mask,variant);
}

#endif
