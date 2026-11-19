#ifndef ENDURANCE_TERRAIN_PATTERNS_GLSL
#define ENDURANCE_TERRAIN_PATTERNS_GLSL

// Shared world-space fields: cost is independent of biome corners/layers.
// Analytic WORLD-unit gradients work on cliffs/caves without triplanar samples.
struct TerrainNoise { float value; vec3 gradient; };

struct TerrainPatternContext
{
    vec3 position;
    vec3 dx;
    vec3 dy;
    float normalWidth;
    float macroPeriod;
    float patchPeriod;
    float grainPeriod;
    TerrainNoise macro;
    TerrainNoise patchNoise;
    TerrainNoise grain;
};

struct TerrainPatterns
{
    vec4 value;
    mat4x3 gradient;
    // Shallow height gradients, with amplitude proportional to wavelength.
    mat4x3 heightGradient;
};

uint terrainHashCell(ivec3 cell)
{
    uvec3 bits = uvec3(cell);
    uint h = (bits.x * 0x8da6b343u) ^ (bits.y * 0xd8163841u)
           ^ (bits.z * 0xcb1ab31fu) ^ worldSeed;
    h ^= h >> 16u;
    h *= 0x7feb352du;
    h ^= h >> 15u;
    h *= 0x846ca68bu;
    return h ^ (h >> 16u);
}

float terrainCornerValue(ivec3 cell)
{
    return float(terrainHashCell(cell) >> 8u) * (1.0 / 16777215.0);
}

// Eight hashes return BOTH value and derivative. Quintic interpolation keeps
// bump normals smooth across lattice boundaries. No periodic texture cache.
TerrainNoise terrainNoiseValueGradient(vec3 p)
{
    // Preserve defined integer conversion at extreme world coordinates.
    if (any(greaterThanEqual(abs(p), vec3(1.0e9))))
        return TerrainNoise(0.5,vec3(0.0));
    ivec3 cell = ivec3(floor(p));
    vec3 f = fract(p);
    vec3 blend = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
    vec3 derivative = 30.0 * f * f * (f - 1.0) * (f - 1.0);
    float a = terrainCornerValue(cell);
    float b = terrainCornerValue(cell + ivec3(1,0,0));
    float c = terrainCornerValue(cell + ivec3(0,1,0));
    float d = terrainCornerValue(cell + ivec3(1,1,0));
    float e = terrainCornerValue(cell + ivec3(0,0,1));
    float f1 = terrainCornerValue(cell + ivec3(1,0,1));
    float g = terrainCornerValue(cell + ivec3(0,1,1));
    float h = terrainCornerValue(cell + ivec3(1,1,1));
    float value = mix(mix(mix(a,b,blend.x),mix(c,d,blend.x),blend.y),
                      mix(mix(e,f1,blend.x),mix(g,h,blend.x),blend.y),blend.z);
    float dx = mix(mix(b-a,d-c,blend.y),mix(f1-e,h-g,blend.y),blend.z);
    float dy = mix(mix(c-a,d-b,blend.x),mix(g-e,h-f1,blend.x),blend.z);
    float dz = mix(mix(e-a,f1-b,blend.x),mix(g-c,h-d,blend.x),blend.y);
    return TerrainNoise(value,vec3(dx,dy,dz) * derivative);
}

// Orthonormal rotation removes alignment with terrain axes; its transpose maps
// the analytic derivative back into world coordinates.
const mat3 TERRAIN_NOISE_ROTATION = mat3(
    vec3(0.80,0.00,0.60),vec3(0.36,0.80,-0.48),vec3(-0.48,0.60,0.64));

TerrainNoise terrainWorldNoise(vec3 p, vec3 dx, vec3 dy, float period, vec3 offset)
{
    float frequency = 1.0 / max(period,0.000001);
    float footprint = max(length(dx),length(dy)) * frequency;
    float fade = 1.0 - smoothstep(0.20,0.80,footprint);
    if (fade <= 0.0001) return TerrainNoise(0.5,vec3(0.0));
    TerrainNoise n = terrainNoiseValueGradient(
        TERRAIN_NOISE_ROTATION * (p * frequency) + offset);
    n.value = mix(0.5,n.value,fade);
    n.gradient = transpose(TERRAIN_NOISE_ROTATION) * n.gradient * (frequency * fade);
    return n;
}

TerrainPatternContext terrainMakePatternContext(vec3 p, vec3 dx, vec3 dy,
    float normalWidth)
{
    TerrainPatternContext c;
    c.position = p;
    c.dx = dx;
    c.dy = dy;
    c.normalWidth = normalWidth;
    c.macroPeriod = max(terrainMacroPeriod,0.001);
    c.patchPeriod = max(terrainPatchPeriod,0.001);
    c.grainPeriod = 1.0 / clamp(microDetailScale,0.0001,1000000.0);
    c.macro = terrainWorldNoise(p,dx,dy,c.macroPeriod,vec3(0.0));
    c.patchNoise = terrainWorldNoise(p,dx,dy,c.patchPeriod,vec3(71.0,19.0,-53.0));
    c.grain = terrainWorldNoise(p,dx,dy,c.grainPeriod,vec3(-29.0,47.0,13.0));
    return c;
}

float terrainFieldWidth(vec3 gradient, TerrainPatternContext c)
{
    // Capture position derivatives before branching. Width remains defined
    // inside material/climate branches, unlike implicit fragment derivatives.
    return abs(dot(gradient,c.dx)) + abs(dot(gradient,c.dy));
}

TerrainNoise terrainWave(float phase, vec3 phaseGradient, TerrainPatternContext c)
{
    const float tau = 6.28318530718;
    float fade = 1.0 - smoothstep(0.20,0.80,terrainFieldWidth(phaseGradient,c));
    float angle = tau * fract(phase);
    return TerrainNoise(0.5 + 0.5 * sin(angle) * fade,
                        0.5 * tau * cos(angle) * phaseGradient * fade);
}

TerrainPatterns terrainSurfacePatterns(vec4 detail, vec4 activeWeights,
    TerrainPatternContext c)
{
    TerrainPatterns p;
    p.value = vec4(c.macro.value,c.patchNoise.value,0.5,0.5);
    p.gradient = mat4x3(c.macro.gradient,c.patchNoise.gradient,vec3(0.0),vec3(0.0));
    p.heightGradient = mat4x3(c.macro.gradient * (c.macroPeriod * 0.012),
        c.patchNoise.gradient * (c.patchPeriod * 0.10),vec3(0.0),vec3(0.0));
    // Base and accent share these fields. Zero weights skip trigonometric work.
    if (activeWeights.z > 0.0 || activeWeights.w > 0.0)
    {
        vec3 direction = vec3(cos(detail.z),0.0,sin(detail.z));
        if (activeWeights.z > 0.0)
        {
            float period = max(detail.x,0.001);
            vec3 axis = vec3(0.0,1.0,0.0) + 0.18 * direction;
            // Two already-shared scales bend the sediment at broad and medium
            // distances, avoiding uniformly spaced contour-like bands.
            float warp = 0.65*c.macro.value + 0.35*c.patchNoise.value - 0.5;
            vec3 warpGradient = 0.65*c.macro.gradient + 0.35*c.patchNoise.gradient;
            float phase = (dot(c.position,axis) + detail.w*warp) / period;
            vec3 phaseGradient = (axis + detail.w*warpGradient) / period;
            TerrainNoise strata = terrainWave(phase,phaseGradient,c);
            p.value.z = strata.value;
            p.gradient[2] = strata.gradient;
            p.heightGradient[2] = strata.gradient * (period * 0.015);
        }
        if (activeWeights.w > 0.0)
        {
            float period = max(detail.y,0.001);
            float phase = (dot(c.position,direction) + detail.w * (c.patchNoise.value-0.5)) / period;
            vec3 phaseGradient = (direction + detail.w * c.patchNoise.gradient) / period;
            TerrainNoise ripple = terrainWave(phase,phaseGradient,c);
            p.value.w = ripple.value;
            p.gradient[3] = ripple.gradient;
            p.heightGradient[3] = ripple.gradient * (period * 0.035);
        }
    }
    return p;
}

#endif
