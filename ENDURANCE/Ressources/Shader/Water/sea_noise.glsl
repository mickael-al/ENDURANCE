#ifndef ENDURANCE_SEA_NOISE_GLSL
#define ENDURANCE_SEA_NOISE_GLSL

vec2 gradientNoise_dir(vec2 p)
{
    p = mod(p, 289.0);
    float x = mod((34.0 * p.x + 1.0) * p.x, 289.0) + p.y;
    x = mod((34.0 * x + 1.0) * x, 289.0);
    x = fract(x / 41.0) * 2.0 - 1.0;
    return normalize(vec2(x - floor(x + 0.5), abs(x) - 0.5));
}

// Same gradient lattice as water_transparency. One evaluation gives the value
// and analytic derivatives for the fragment ripples and intersection foam.
vec3 seaNoiseValueGradient(vec2 p)
{
    vec2 cell=floor(p),f=fract(p);
    vec2 g00=gradientNoise_dir(cell),g01=gradientNoise_dir(cell+vec2(0,1));
    vec2 g10=gradientNoise_dir(cell+vec2(1,0)),g11=gradientNoise_dir(cell+vec2(1,1));
    float a=dot(g00,f),b=dot(g01,f-vec2(0,1));
    float c=dot(g10,f-vec2(1,0)),d=dot(g11,f-vec2(1,1));
    vec2 u=f*f*f*(f*(f*6.0-15.0)+10.0);
    vec2 du=30.0*f*f*(f-1.0)*(f-1.0);
    float lo=mix(a,b,u.y),hi=mix(c,d,u.y);
    vec2 gradient=mix(mix(g00,g01,u.y),mix(g10,g11,u.y),u.x);
    gradient+=vec2(hi-lo,mix(b-a,d-c,u.x))*du;
    return vec3(mix(lo,hi,u.x),gradient);
}

// Virtual height for normal mapping only; the sea mesh stays exactly flat.
// Chain rule: differentiating amplitude * noise(worldXZ * frequency) requires
// amplitude * frequency. The previous unitless 0.22 factor exaggerated slopes.
vec2 seaRippleSlope(vec2 worldXZ, float time, float footprint)
{
    const float frequency=0.125;
    const float heightAmplitude=0.40;
    const vec2 velocity=vec2(0.56,0.28);
    float fade=1.0-smoothstep(0.20,0.80,footprint*frequency);
    if(fade<=0.0001) return vec2(0.0);
    return seaNoiseValueGradient((worldXZ+velocity*time)*frequency).yz
        *(heightAmplitude*frequency*fade);
}

#endif
