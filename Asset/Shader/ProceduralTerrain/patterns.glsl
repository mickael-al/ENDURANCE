#ifndef ENDURANCE_TERRAIN_PATTERNS_GLSL
#define ENDURANCE_TERRAIN_PATTERNS_GLSL
#include "render_options.glsl"

// Appearance controls have passed validTerrainAppearance on the CPU before
// upload. Their documented ranges are trusted here; computed-field, frequency
// and filter safeguards remain independent of that data validation.
// One shared macro field, then stable per-biome patch/detail domains.
// Analytic WORLD-unit gradients work on cliffs/caves without triplanar samples.
struct TerrainNoise { float value; vec3 gradient; };

struct TerrainPatternContext
{
    vec3 position;
    vec3 dx;
    vec3 dy;
    float pixelFootprint;
    float normalWidth;
    float macroPeriod;
    float patchPeriod;
    float grainPeriod;
    float domainPhase;
    TerrainNoise macro;
    TerrainNoise patchNoise;
    TerrainNoise grain;
};

struct TerrainPatterns
{
    vec4 value;
    mat4x3 gradient;
    // Albedo variation never becomes bump height implicitly.
    vec3 normalGradient;
};

uint terrainHashCellSalt(ivec3 cell, uint salt)
{
    uvec3 bits = uvec3(cell);
    uint h = (bits.x * 0x8da6b343u) ^ (bits.y * 0xd8163841u)
           ^ (bits.z * 0xcb1ab31fu) ^ worldSeed ^ salt;
    h ^= h >> 16u;
    h *= 0x7feb352du;
    h ^= h >> 15u;
    h *= 0x846ca68bu;
    return h ^ (h >> 16u);
}

uint terrainHashCell(ivec3 cell)
{
    return terrainHashCellSalt(cell,0u);
}

float terrainCornerValueSalt(ivec3 cell, uint salt)
{
    return float(terrainHashCellSalt(cell,salt) >> 8u) * (1.0 / 16777215.0);
}

// Eight hashes return BOTH value and derivative. Quintic interpolation keeps
// bump normals smooth across lattice boundaries. No periodic texture cache.
TerrainNoise terrainNoiseValueGradientSalt(vec3 p, uint salt)
{
    // Preserve defined integer conversion at extreme world coordinates.
    if (any(greaterThanEqual(abs(p), vec3(1.0e9))))
        return TerrainNoise(0.5,vec3(0.0));
    ivec3 cell = ivec3(floor(p));
    vec3 f = fract(p);
    vec3 blend = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
    vec3 derivative = 30.0 * f * f * (f - 1.0) * (f - 1.0);
    // Share the six axis products across the eight integer lattice corners.
    // Unsigned wraparound and the avalanche are identical to the scalar hash.
    uvec3 lower = uvec3(cell);
    uvec3 upper = lower + uvec3(1u);
    uvec3 lowProducts = lower * uvec3(0x8da6b343u,0xd8163841u,0xcb1ab31fu);
    uvec3 highProducts = upper * uvec3(0x8da6b343u,0xd8163841u,0xcb1ab31fu);
    uvec4 xy = uvec4(lowProducts.x,highProducts.x,lowProducts.x,highProducts.x)
        ^ uvec4(lowProducts.y,lowProducts.y,highProducts.y,highProducts.y)
        ^ worldSeed ^ salt;
    uvec4 lowHash = xy ^ lowProducts.z;
    uvec4 highHash = xy ^ highProducts.z;
    lowHash ^= lowHash >> 16u;
    highHash ^= highHash >> 16u;
    lowHash *= 0x7feb352du;
    highHash *= 0x7feb352du;
    lowHash ^= lowHash >> 15u;
    highHash ^= highHash >> 15u;
    lowHash *= 0x846ca68bu;
    highHash *= 0x846ca68bu;
    lowHash ^= lowHash >> 16u;
    highHash ^= highHash >> 16u;
    vec4 lowValues = vec4(lowHash >> 8u) * (1.0 / 16777215.0);
    vec4 highValues = vec4(highHash >> 8u) * (1.0 / 16777215.0);
    float a = lowValues.x;
    float b = lowValues.y;
    float c = lowValues.z;
    float d = lowValues.w;
    float e = highValues.x;
    float f1 = highValues.y;
    float g = highValues.z;
    float h = highValues.w;
    float value = mix(mix(mix(a,b,blend.x),mix(c,d,blend.x),blend.y),
                      mix(mix(e,f1,blend.x),mix(g,h,blend.x),blend.y),blend.z);
    float dx = mix(mix(b-a,d-c,blend.y),mix(f1-e,h-g,blend.y),blend.z);
    float dy = mix(mix(c-a,d-b,blend.x),mix(g-e,h-f1,blend.x),blend.z);
    float dz = mix(mix(e-a,f1-b,blend.x),mix(g-c,h-d,blend.x),blend.y);
    return TerrainNoise(value,vec3(dx,dy,dz) * derivative);
}

TerrainNoise terrainNoiseValueGradient(vec3 p)
{
    return terrainNoiseValueGradientSalt(p,0u);
}

// Orthonormal rotation removes alignment with terrain axes; its transpose maps
// the analytic derivative back into world coordinates.
const mat3 TERRAIN_NOISE_ROTATION = mat3(
    vec3(0.80,0.00,0.60),vec3(0.36,0.80,-0.48),vec3(-0.48,0.60,0.64));

// Private noise-domain result: leave the analytic gradient in rotated domain
// coordinates until the caller decides whether it is a single sample or fBm.
TerrainNoise terrainDomainNoiseFootprint(vec3 p, float pixelFootprint,
    float period, vec3 offset, uint salt, out float gradientScale)
{
    float frequency = 1.0 / max(period,0.000001);
    float footprint = pixelFootprint * frequency;
    float fade = 1.0 - smoothstep(0.20,0.80,footprint);
    gradientScale = frequency * fade;
    if (fade <= 0.0001)
    {
        gradientScale = 0.0;
        return TerrainNoise(0.5,vec3(0.0));
    }
    TerrainNoise n = terrainNoiseValueGradientSalt(
        TERRAIN_NOISE_ROTATION * (p * frequency) + offset,salt);
    n.value = mix(0.5,n.value,fade);
    return n;
}

TerrainNoise terrainWorldNoiseFootprint(vec3 p, float pixelFootprint,
    float period, vec3 offset, uint salt)
{
    float gradientScale;
    TerrainNoise n = terrainDomainNoiseFootprint(p,pixelFootprint,
        period,offset,salt,gradientScale);
    n.gradient = transpose(TERRAIN_NOISE_ROTATION) * n.gradient * gradientScale;
    return n;
}

// Public derivative-based API remains available to isolated noise callers.
TerrainNoise terrainWorldNoiseSalt(vec3 p, vec3 dx, vec3 dy,
    float period, vec3 offset, uint salt)
{
    return terrainWorldNoiseFootprint(p,max(length(dx),length(dy)),period,offset,salt);
}

TerrainNoise terrainWorldNoise(vec3 p, vec3 dx, vec3 dy, float period, vec3 offset)
{
    return terrainWorldNoiseSalt(p,dx,dy,period,offset,0u);
}

TerrainPatternContext terrainMakePatternContext(vec3 p, vec3 dx, vec3 dy,
    float normalWidth)
{
    TerrainPatternContext c;
    c.position = p;
    c.dx = dx;
    c.dy = dy;
    c.pixelFootprint = max(length(dx),length(dy));
    c.normalWidth = normalWidth;
    c.macroPeriod = max(terrainMacroPeriod,0.001);
    c.patchPeriod = max(terrainPatchPeriod,0.001);
    c.grainPeriod = 1.0 / clamp(microDetailScale,0.0001,1000000.0);
    c.domainPhase = 0.0;
    c.macro = terrainWorldNoiseFootprint(p,c.pixelFootprint,c.macroPeriod,vec3(0.0),0u);
    c.patchNoise = TerrainNoise(0.5,vec3(0.0));
    c.grain = TerrainNoise(0.5,vec3(0.0));
    return c;
}

// Integer octave counts give ordinary fBm. Fractional counts fade only the
// last octave; presets mostly use one or two, up to three for coarse rock. A gain
// below 0.5 prevents the high-frequency normal slopes dominating the material.
TerrainNoise terrainDetailSpectrum(TerrainPatternContext c, float octaves,
    float gain, vec3 offset, uint salt)
{
    float period = c.grainPeriod;
    if (c.pixelFootprint >= period*0.8)
        return TerrainNoise(0.5,vec3(0.0));
    float value = 0.0;
    vec3 gradient = vec3(0.0);
    float amplitude = 1.0;
    float sum = 0.0;
    for (int octave=0; octave<3; ++octave)
    {
        float weight = amplitude * clamp(octaves-float(octave),0.0,1.0);
        if (weight <= 0.00001) break;
        float gradientScale;
        TerrainNoise n = terrainDomainNoiseFootprint(c.position,c.pixelFootprint,period,
            offset + fract(float(octave)*vec3(0.37,0.19,0.61)),
            salt ^ (uint(octave)*0x9e3779b9u),gradientScale);
        value += (n.value-0.5)*weight;
        gradient += (n.gradient*gradientScale)*weight;
        sum += weight;
        period *= 0.5;
        amplitude *= gain;
    }
    // The common linear rotation is applied once to the accumulated spectrum.
    // Frequencies, positions and value interpolation remain unchanged.
    gradient = transpose(TERRAIN_NOISE_ROTATION) * gradient;
    return TerrainNoise(0.5+value/max(sum,0.00001),gradient/max(sum,0.00001));
}

TerrainPatternContext terrainBiomePatterns(uint id, vec4 spectrum,
    TerrainPatternContext c)
{
    // Frequencies never depend on climate weights: blend evaluated appearances,
    // not coordinates. This keeps phases stable at large world positions/LODs.
    // Keep offsets near the origin: large floating offsets quantize derivatives.
    // The integer salt distinguishes biomes without losing coordinate precision.
    uint salt = (id+1u)*0x9e3779b9u;
    vec3 offset = fract(float(id+1u)*vec3(0.1713,0.5379,0.3141));
    c.domainPhase = fract(float(id+1u)*0.61803398875);
    c.patchPeriod = max(terrainPatchPeriod*spectrum.x,0.001);
    c.grainPeriod = spectrum.y
        * (2.0/clamp(microDetailScale,0.0001,1000000.0));
    c.patchNoise = terrainWorldNoiseFootprint(c.position,c.pixelFootprint,c.patchPeriod,
        offset + vec3(0.71,0.19,-0.53),salt ^ 0x68bc21ebu);
    c.grain = terrainDetailSpectrum(c,spectrum.z,
        spectrum.w,offset + vec3(-0.29,0.47,0.13),salt ^ 0x02e5be93u);
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
    if (fade == 0.0) return TerrainNoise(0.5,vec3(0.0));
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
    p.normalGradient = vec3(0.0);
    // Base and accent share these fields. Zero weights skip trigonometric work.
    if (activeWeights.z > 0.0 || activeWeights.w > 0.0)
    {
        vec3 direction = vec3(cos(detail.z),0.0,sin(detail.z));
        if (activeWeights.z > 0.0)
        {
            float period = detail.x;
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

        }
        if (activeWeights.w > 0.0)
        {
            float period = detail.y;
            float phase = (dot(c.position,direction) + detail.w * (c.patchNoise.value-0.5)) / period;
            vec3 phaseGradient = (direction + detail.w * c.patchNoise.gradient) / period;
            TerrainNoise ripple = terrainWave(phase,phaseGradient,c);
            p.value.w = ripple.value;
            p.gradient[3] = ripple.gradient;

        }
    }
    return p;
}

// A filtered periodic groove, rather than a hard abs/fract edge. The amplitude
// disappears when the narrow feature is no longer resolved; it never leaves a
// stationary alias pattern on distant terrain.
TerrainNoise terrainGroove(float phase, vec3 phaseGradient, float width,
    TerrainPatternContext c)
{
    float footprint = terrainFieldWidth(phaseGradient,c);
    float fade = 1.0-smoothstep(width*0.5,width*2.0,footprint);
    if (fade <= 0.0001) return TerrainNoise(0.0,vec3(0.0));
    float centered = fract(phase)-0.5;
    float t = clamp(abs(centered)/max(width,0.001),0.0,1.0);
    float value = 1.0-t*t*(3.0-2.0*t);
    float derivative = -6.0*t*(1.0-t)*sign(centered)/max(width,0.001);
    return TerrainNoise(value*fade,phaseGradient*(derivative*fade));
}

TerrainNoise terrainSurfaceRelief(vec4 control, vec4 detail, TerrainPatternContext c)
{
#if !TERRAIN_MICRODETAIL_ENABLED
    return TerrainNoise(0.0,vec3(0.0));
#else
    int family = int(control.x);
    float grainMix = control.w;
    // The small detail field is shared by ground/wall/layers of this biome.
    // Its period is independent of both palette patches and feature spacing.
    vec3 fine = c.grain.gradient*c.grainPeriod;
    float height = 0.0;
    float fineHeight = (c.grain.value-0.5)*c.grainPeriod;
    vec3 gradient = vec3(0.0);
    if (family == 0) // Fine soil, scree and mineral grains: no metre-sized blobs.
    {
        height = fineHeight*(0.12+0.40*grainMix);
        gradient = fine*(0.12+0.40*grainMix);
    }
    else
    {
        float period = control.y;
        float warp = control.z;
        vec3 direction = vec3(cos(detail.z),0.0,sin(detail.z));
        vec3 across = vec3(-direction.z,0.0,direction.x);
        // A fine field cannot displace a metre-wide feature by metre-sized amounts.
        float fineRatio = min(c.grainPeriod/period,1.0);
        // Macro/patch fields only bend and interrupt meaningful structures. Their
        // own gradients are NOT a universal round bump height any more.
        float bend = warp*(c.patchNoise.value-0.5);
        vec3 bendGradient = warp*c.patchNoise.gradient;
        TerrainNoise meander = TerrainNoise(0.5,vec3(0.0));
        if (warp > 0.0001)
        {
            // Bend at a scale near the actual feature, not only at the 100m
            // palette scale. An oblique, incommensurate wave interrupts parallel
            // rails without another noise lookup or a visible world-axis grid.
            vec3 meanderAxis = across+0.17*direction+vec3(0.0,0.39,0.0);
            float meanderPhase = dot(c.position,meanderAxis)/(period*7.31)
                +0.8*(c.patchNoise.value-0.5)+9.73*c.domainPhase;
            vec3 meanderGradient = meanderAxis/(period*7.31)
                +0.8*c.patchNoise.gradient;
            meander = terrainWave(meanderPhase,meanderGradient,c);
            bend += warp*0.85*(meander.value-0.5);
            bendGradient += warp*0.85*meander.gradient;
        }
        float phase = dot(c.position,direction)/period + bend+c.domainPhase;
        vec3 phaseGradient = direction/period + bendGradient;
        if (family == 1) // Wind: unequal crests and a faint secondary ripple.
        {
            TerrainNoise ridge = terrainWave(phase,phaseGradient,c);
            TerrainNoise crest = terrainWave(2.0*phase+0.17,
                2.0*phaseGradient,c);
            float envelope = 0.65+0.35*c.patchNoise.value;
            height = period*0.20*envelope*((ridge.value-0.5)+0.16*(crest.value-0.5));
            gradient = period*0.20*envelope*(ridge.gradient+0.16*crest.gradient);
            gradient += period*0.20*0.35*c.patchNoise.gradient
                *((ridge.value-0.5)+0.16*(crest.value-0.5));
        }
        else if (family == 2) // Sedimentary bedding, irregularly chipped along it.
        {
            vec3 axis = vec3(0.0,1.0,0.0)+0.16*direction;
            phase = dot(c.position,axis)/period + bend+c.domainPhase;
            phaseGradient = axis/period+bendGradient;
            TerrainNoise bed = terrainWave(phase,phaseGradient,c);
            TerrainNoise seam = terrainGroove(phase+0.21,phaseGradient,0.15,c);
            float chip = 0.80+0.20*fineRatio*(c.grain.value-0.5);
            height = period*(0.14*(bed.value-0.5)-0.035*seam.value)*chip;
            gradient = period*(0.14*bed.gradient-0.035*seam.gradient)*chip;
            gradient += period*(0.14*(bed.value-0.5)-0.035*seam.value)
                *(0.20*fineRatio)*c.grain.gradient;
        }
        else if (family == 3) // Unequal, oblique fractures with sparse cross joints.
        {
            vec3 axis = normalize(direction+vec3(0.0,0.73,0.0));
            phase = dot(c.position,axis)/period + bend+c.domainPhase;
            phaseGradient = axis/period+bendGradient;
            TerrainNoise a = terrainGroove(phase,phaseGradient,0.12,c);
            vec3 crossAxis = normalize(across+0.37*direction+vec3(0.0,0.31,0.0));
            float crossPhase = dot(c.position,crossAxis)/(period*1.73)
                -0.61*bend+0.37+0.53*c.domainPhase;
            vec3 crossGradient = crossAxis/(period*1.73)-0.61*bendGradient;
            TerrainNoise b = terrainGroove(crossPhase,crossGradient,0.09,c);
            float gateField = 0.5*(c.patchNoise.value+meander.value);
            float gate = smoothstep(0.38,0.72,gateField);
            float gt = clamp((gateField-0.38)/0.34,0.0,1.0);
            vec3 gateGradient = (c.patchNoise.gradient+meander.gradient)
                *(3.0*gt*(1.0-gt)/0.34);
            float strength = 0.35+0.65*meander.value;
            height = -period*0.045*(a.value*strength+0.55*b.value*gate);
            gradient = -period*0.045*(a.gradient*strength+a.value*0.65*meander.gradient
                +0.55*(b.gradient*gate+b.value*gateGradient));
        }
        else if (family == 4) // Tangled short fibres/litter, not isotropic pebbles.
        {
            vec3 axis = normalize(direction+vec3(0.0,0.23,0.0));
            phase = dot(c.position,axis)/period + 0.45*fineRatio*(c.grain.value-0.5)+bend+c.domainPhase;
            phaseGradient = axis/period + 0.45*fineRatio*c.grain.gradient+bendGradient;
            TerrainNoise fibre = terrainWave(phase,phaseGradient,c);
            float envelope = mix(0.40,c.grain.value*c.grain.value,fineRatio);
            height = period*0.13*(fibre.value-0.5)*envelope;
            gradient = period*0.13*(fibre.gradient*envelope
                +(fibre.value-0.5)*(2.0*fineRatio*c.grain.value*c.grain.gradient));
        }
        else if (family == 5) // Calm wet sediment with shallow meandering streaks.
        {
            phase += 0.7*fineRatio*(c.grain.value-0.5);
            phaseGradient += 0.7*fineRatio*c.grain.gradient;
            TerrainNoise streak = terrainWave(phase,phaseGradient,c);
            height = period*0.045*(streak.value-0.5);
            gradient = period*0.045*streak.gradient;
        }
        else if (family == 6) // Mostly polished ice with sparse, wandering veins.
        {
            vec3 axis = normalize(direction+vec3(0.0,0.41,0.0));
            phase = dot(c.position,axis)/period+2.0*bend+c.domainPhase
                +0.22*fineRatio*(c.grain.value-0.5);
            phaseGradient = axis/period+2.0*bendGradient+0.22*fineRatio*c.grain.gradient;
            TerrainNoise vein = terrainGroove(phase,phaseGradient,0.075,c);
            float veil = 0.35+0.65*c.patchNoise.value;
            height = -period*0.026*vein.value*veil;
            gradient = -period*0.026*(vein.gradient*veil
                +vein.value*0.65*c.patchNoise.gradient);
        }
        else // 7: folded lava ropes, broad nonuniform lobes and tight secondary folds.
        {
            phase += 0.6*fineRatio*(c.grain.value-0.5);
            phaseGradient += 0.6*fineRatio*c.grain.gradient;
            TerrainNoise rope = terrainWave(phase,phaseGradient,c);
            TerrainNoise fold = terrainWave(1.91*phase+0.27,
                1.91*phaseGradient,c);
            height = period*0.16*((rope.value-0.5)+0.23*(fold.value-0.5));
            gradient = period*0.16*(rope.gradient+0.23*fold.gradient);
        }
        height += fineHeight*(0.30*grainMix);
        gradient += fine*(0.30*grainMix);
    }
    float strength = max(microDetailStrength,0.0);
    return TerrainNoise(height*strength,gradient*strength);
#endif
}

vec3 terrainSurfaceNormal(vec4 control, vec4 detail, TerrainPatternContext c)
{
    return terrainSurfaceRelief(control,detail,c).gradient;
}

#endif
