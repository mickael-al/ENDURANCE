// Validated ENDURANCE climate, 2026-11-12. X=humidity, Y=temperature in [0,1].
// World coordinates and world seed are the only runtime inputs. Edit these
// defines and regenerate/reload shaders to change the production recipe.
#ifndef ENDURANCE_CLIMATE_2D_GLSL
#define ENDURANCE_CLIMATE_2D_GLSL

#include "noise_2d.glsl"

#define ENDURANCE_CLIMATE_FREQUENCY 0.0005
#define ENDURANCE_CLIMATE_SCALE 0.1
#define ENDURANCE_CLIMATE_LAYERS 5
#define ENDURANCE_CLIMATE_OCTAVES 8
#define ENDURANCE_CLIMATE_LACUNARITY 2.0
#define ENDURANCE_CLIMATE_GAIN 0.5
#define ENDURANCE_CLIMATE_TRANSITION 0.08
#define ENDURANCE_CLIMATE_CONTRAST 1.5
#define ENDURANCE_CLIMATE_GAMMA_H 1.5
#define ENDURANCE_CLIMATE_GAMMA_T 2.0
#define ENDURANCE_CLIMATE_ATTRACTION 0.0468

// Keep the highest octave * 4 within the CPU's fixed-frequency domain envelope
// in ProceduralTerrainField.hpp when editing frequency/scale/octaves/lacunarity.
// Valid recipe ranges: layers>=1, octaves 1..8, lacunarity 1..8, gain 0..1,
// transition>=0, gamma>0, attraction>=0; finite positive spatial scales.
vec2 noiseClimate2D(vec2 world, uint seed)
{
    uvec2 seeds[ENDURANCE_CLIMATE_LAYERS];
    vec2 sums[ENDURANCE_CLIMATE_LAYERS];
    for (int layer=0; layer<ENDURANCE_CLIMATE_LAYERS; ++layer)
    {
        uint salt=uint(layer+1)*0x9e3779b9u;
        seeds[layer]=uvec2(noiseInternalHash(seed^0x243f6a88u^salt),
                           noiseInternalHash(seed^0x85a308d3u^salt));
        sums[layer]=vec2(0.0);
    }
    // Retain the lab's two spatial multiplications: folding the constants into
    // one multiply changes seeded samples near a climate transition.
    precise vec2 scaledWorld=world*ENDURANCE_CLIMATE_FREQUENCY;
    vec2 p=scaledWorld*ENDURANCE_CLIMATE_SCALE;
    float amplitude=1.0, normalization=0.0;
    for (int octave=0; octave<ENDURANCE_CLIMATE_OCTAVES; ++octave)
    {
        // Same basis as noiseSimplex2D, shared by all ten seeded fields instead
        // of recomputing cell selection, distances and attenuation ten times.
        const float skew=0.36602540378443864676;
        const float unskew=0.21132486540518711775;
        ivec2 cell=ivec2(floor(p+(p.x+p.y)*skew));
        float originOffset=(float(cell.x)+float(cell.y))*unskew;
        vec2 d0=p-(vec2(cell)-originOffset);
        ivec2 middle=d0.x>d0.y ? ivec2(1,0) : ivec2(0,1);
        vec2 d1=d0-vec2(middle)+unskew;
        vec2 d2=d0-1.0+2.0*unskew;
        vec3 attenuation=max(vec3(.5)-vec3(dot(d0,d0),dot(d1,d1),dot(d2,d2)),0.0);
        attenuation*=attenuation;
        attenuation*=attenuation;
        for (int layer=0; layer<ENDURANCE_CLIMATE_LAYERS; ++layer)
            for (int axis=0; axis<2; ++axis)
            {
                uint octaveSeed=seeds[layer][axis]+uint(octave)*0x9e3779b9u;
                vec3 gradients=vec3(
                    noiseInternalGradient2D(noiseInternalHash2D(cell,octaveSeed),d0),
                    noiseInternalGradient2D(noiseInternalHash2D(cell+middle,octaveSeed),d1),
                    noiseInternalGradient2D(noiseInternalHash2D(cell+ivec2(1),octaveSeed),d2));
                sums[layer][axis]+=amplitude*clamp(70.0*dot(attenuation,gradients),-1.0,1.0);
            }
        normalization+=amplitude;
        p*=ENDURANCE_CLIMATE_LACUNARITY;
        amplitude*=ENDURANCE_CLIMATE_GAIN;
    }
    vec2 climate=vec2(0.0);
    for (int layer=0; layer<ENDURANCE_CLIMATE_LAYERS; ++layer)
    {
        vec2 value=clamp(sums[layer]/normalization,-1.0,1.0);
        climate+=ENDURANCE_CLIMATE_TRANSITION>0.0
            ? smoothstep(vec2(-ENDURANCE_CLIMATE_TRANSITION),vec2(ENDURANCE_CLIMATE_TRANSITION),value)
            : vec2(greaterThan(value,vec2(0.0)));
    }
    climate*=1.0/float(ENDURANCE_CLIMATE_LAYERS);
    climate=clamp((climate-.5)*ENDURANCE_CLIMATE_CONTRAST+.5,0.0,1.0);
    // The default powers have inexpensive exact algebraic forms. Changing a
    // gamma define selects the general power at compile time, not at runtime.
    climate.x=ENDURANCE_CLIMATE_GAMMA_H==1.5 ? climate.x*sqrt(climate.x) : pow(climate.x,ENDURANCE_CLIMATE_GAMMA_H);
    climate.y=ENDURANCE_CLIMATE_GAMMA_T==2.0 ? climate.y*climate.y : pow(climate.y,ENDURANCE_CLIMATE_GAMMA_T);
    if (ENDURANCE_CLIMATE_ATTRACTION>0.0)
    {
        vec2 scaled=climate*4.0;
        float band=clamp(ENDURANCE_CLIMATE_ATTRACTION*4.0,0.000001,.5);
        vec2 target=(floor(scaled)+smoothstep(vec2(.5-band),vec2(.5+band),fract(scaled)))*.25;
        climate=clamp(climate+clamp(target-climate,-ENDURANCE_CLIMATE_ATTRACTION,ENDURANCE_CLIMATE_ATTRACTION),0.0,1.0);
    }
    return climate;
}

#endif
