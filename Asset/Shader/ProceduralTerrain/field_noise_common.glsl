// Small scalar helpers shared by 2D and 3D noise; no dimensional noise kernels.
#ifndef ENDURANCE_FIELD_NOISE_COMMON_GLSL
#define ENDURANCE_FIELD_NOISE_COMMON_GLSL

uint fieldHash(uint x)
{
    x ^= x >> 16u;
    x *= 0x7feb352du;
    x ^= x >> 15u;
    x *= 0x846ca68bu;
    return x ^ (x >> 16u);
}

vec2 fieldFade(vec2 f) { return f * f * f * (f * (f * 6.0 - 15.0) + 10.0); }
vec3 fieldFade(vec3 f) { return f * f * f * (f * (f * 6.0 - 15.0) + 10.0); }

float fieldFade(float f) { return f*f*f*(f*(f*6.0-15.0)+10.0); }

float fieldTerraces(float value, float levels, float softness)
{
    levels = max(levels,0.25);
    float v = value * levels;
    float width = clamp(softness,0.01,1.0);
    return (floor(v) + smoothstep(0.5-0.5*width,0.5+0.5*width,fract(v))) / levels;
}

#endif
