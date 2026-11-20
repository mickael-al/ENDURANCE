#ifndef ENDURANCE_SEA_WAVES_GLSL
#define ENDURANCE_SEA_WAVES_GLSL

// Keep the shared indexed grid in EnduranceSea.cpp in sync with this value.
const uint SEA_GRID_CELLS = 8u;

vec2 gradientNoise_dir(vec2 p)
{
    p = mod(p, 289.0);
    float x = mod((34.0 * p.x + 1.0) * p.x, 289.0) + p.y;
    x = mod((34.0 * x + 1.0) * x, 289.0);
    x = fract(x / 41.0) * 2.0 - 1.0;
    return normalize(vec2(x - floor(x + 0.5), abs(x) - 0.5));
}

// Same gradient lattice as water_transparency. One evaluation gives the value
// and analytic derivatives, shared by broad vertex waves and fragment ripples.
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

// Artistic large-scale waves, in metres and seconds. Displacement fades in
// world space, so neighbouring LODs evaluate the SAME height at shared points.
// The far early-out avoids evaluating under-sampled waves on distant grids.
vec3 seaLargeWave(vec2 worldXZ, float level, vec3 cameraPosition, float time)
{
    const float amplitude=2.0;
    const float frequency=1.0/320.0;
    const vec2 velocity=vec2(3.5,1.5);
    const float fadeStart=2048.0;
    const float fadeEnd=4096.0;
    vec3 delta=vec3(worldXZ.x,level,worldXZ.y)-cameraPosition;
    float distanceSquared=dot(delta,delta);
    if(distanceSquared>=fadeEnd*fadeEnd) return vec3(0.0);
    float fadeRange=fadeEnd*fadeEnd-fadeStart*fadeStart;
    float t=clamp((distanceSquared-fadeStart*fadeStart)/fadeRange,0.0,1.0);
    float fade=1.0-t*t*(3.0-2.0*t);
    vec2 fadeGradient=(-12.0*t*(1.0-t)/fadeRange)*delta.xz;
    vec3 wave=seaNoiseValueGradient((worldXZ+velocity*time)*frequency);
    return vec3(amplitude*wave.x*fade,
        amplitude*(wave.yz*frequency*fade+wave.x*fadeGradient));
}

#endif
