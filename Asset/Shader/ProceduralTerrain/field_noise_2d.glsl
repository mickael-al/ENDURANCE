// Seeded 2D primitives for climate, height and per-column biome controls.
#ifndef ENDURANCE_FIELD_NOISE_2D_GLSL
#define ENDURANCE_FIELD_NOISE_2D_GLSL

#include "field_noise_common.glsl"

uint fieldHash2(ivec2 p, uint salt)
{
    return fieldHash(uint(p.x) * 0x9e3779b9u ^ uint(p.y) * 0x85ebca6bu ^ uSeed ^ salt);
}

float fieldGradient2(uint h, vec2 p)
{
    // Eight fixed directions: no transcendental hash and no gradient table fetch.
    uint g = h & 7u;
    if (g < 4u)
        return ((g & 1u) == 0u ? p.x : -p.x) + ((g & 2u) == 0u ? p.y : -p.y);
    return g == 4u ? p.x : g == 5u ? -p.x : g == 6u ? p.y : -p.y;
}

float fieldNoise2(vec2 p, uint salt)
{
    ivec2 i = ivec2(floor(p));
    vec2 f = fract(p), u = fieldFade(f);
    float a = fieldGradient2(fieldHash2(i, salt), f);
    float b = fieldGradient2(fieldHash2(i + ivec2(1,0), salt), f - vec2(1,0));
    float c = fieldGradient2(fieldHash2(i + ivec2(0,1), salt), f - vec2(0,1));
    float d = fieldGradient2(fieldHash2(i + ivec2(1,1), salt), f - vec2(1,1));
    return clamp(mix(mix(a,b,u.x), mix(c,d,u.x), u.y), -1.0, 1.0);
}

float fieldUnsignedNoise(vec2 p, uint salt) { return 0.5 + 0.5 * fieldNoise2(p,salt); }

// Bounded jitter keeps landmarks separated and makes the 3x3 search exact.
// metric: 0 Euclidean, 1 Manhattan (diamond), 2 Chebyshev (square).
vec2 fieldCellular2Data(vec2 p, uint salt, float metric)
{
    ivec2 cell = ivec2(floor(p));
    vec2 f = fract(p);
    float best = 100.0;
    float site = 0.0;
    for (int z = -1; z <= 1; ++z)
    for (int x = -1; x <= 1; ++x)
    {
        ivec2 q = ivec2(x,z);
        uint h = fieldHash2(cell+q,salt);
        vec2 jitter = 0.35 + 0.30 * vec2(h & 65535u,h >> 16u) * (1.0/65535.0);
        vec2 delta = abs(vec2(q)+jitter-f);
        float d = metric < 0.5 ? dot(delta,delta) :
                  metric < 1.5 ? delta.x+delta.y : max(delta.x,delta.y);
        if(d<best)
        {
            best=d;
            site=float(fieldHash(h^0x68bc21ebu)>>8u)*(1.0/16777215.0);
        }
    }
    return vec2(metric < 0.5 ? sqrt(best) : best,site);
}

// The site value is only continuous inside a site. Multiply it by compact
// landmark support; never use it as an unmasked displacement at Voronoi edges.
float fieldCellular2(vec2 p, uint salt, float metric) { return fieldCellular2Data(p,salt,metric).x; }

// Squared distances to the nearest two Voronoi sites. The same bounded jitter
// as the landmark primitive keeps the 3x3 search sufficient. Keeping squared
// distances avoids two square roots when a relief uses their ratio.
vec2 fieldCellular2Distances(vec2 p, uint salt)
{
    ivec2 cell = ivec2(floor(p));
    vec2 f = fract(p);
    vec2 nearest = vec2(100.0);
    for (int z = -1; z <= 1; ++z)
    for (int x = -1; x <= 1; ++x)
    {
        ivec2 q = ivec2(x,z);
        uint h = fieldHash2(cell+q,salt);
        vec2 jitter = 0.35 + 0.30 * vec2(h & 65535u,h >> 16u) * (1.0/65535.0);
        vec2 delta = vec2(q)+jitter-f;
        float d = dot(delta,delta);
        nearest.y = min(nearest.y,max(nearest.x,d));
        nearest.x = min(nearest.x,d);
    }
    return nearest;
}

float fieldVoronoi(vec2 p) { return fieldCellular2(p,0xb5297a4du,0.0); }
float fieldRidge2(vec2 p, uint salt) { return 1.0-abs(fieldNoise2(p,salt)); }

#endif
