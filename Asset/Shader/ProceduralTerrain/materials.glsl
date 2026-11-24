#ifndef ENDURANCE_TERRAIN_MATERIALS_GLSL
#define ENDURANCE_TERRAIN_MATERIALS_GLSL
#include "render_options.glsl"

// std140 ABI: Material112, Surface272, Biome592 bytes; table26 = 15,392 bytes.
// The CPU rejects invalid appearance records before upload; material controls
// below already satisfy validTerrainAppearance's ranges. Computed values still
// keep their filtering/normalization guards and roughness noise clamp.
// Palette RGB is linear. The game supplies presets; the renderer needs no
// per-biome shader variants, extra mesh attributes or additional terrain draws.
struct TerrainMaterial
{
    vec4 colorLow; // linear RGB, roughness at this stop
    vec4 colorMid;
    vec4 colorHigh;
    vec4 response; // metallic, AO, normal strength, grain roughness variation
    vec4 ramp;     // midpoint, softness, contrast, bias
    vec4 pattern;  // macro, patch, strata, ripple weights (normalized below)
    vec4 fineSurface; // sparkle coverage/roughness, stain darkening/roughness drop
};

struct TerrainSurface
{
    TerrainMaterial base;
    TerrainMaterial accent;
    vec4 blend;  // mask threshold, softness, macro weight, patch weight
    vec4 detail; // strata/ripple metres, azimuth radians, warp metres
    vec4 normal; // family, feature period metres, warp fraction, fine grain mix
};

struct TerrainBiomeAppearance
{
    TerrainSurface ground;
    TerrainSurface wall;
    vec4 slope; // wall blend start/end, patch distortion, normal multiplier
    vec4 spectrum; // patch period scale, detail metres, detail octaves, detail gain
    vec4 stainRouting; // vegetation weight: ground base/accent, wall base/accent
};

layout(std140,binding=6) uniform TerrainMaterialTable
{
    TerrainBiomeAppearance terrainBiomes[26];
};
uniform uint terrainBiomeMatrix[25];

struct TerrainAppearanceSample
{
    vec3 color;
    float roughness;
    float metallic;
    float ao;
    vec3 heightGradient;
    float wallMask;
    float accentMask;
    vec4 stains; // large darkening/roughness drop, then fine darkening/roughness drop
    vec2 sparkle; // strength, strength * target roughness (premultiplied)
};

TerrainAppearanceSample terrainMixAppearance(TerrainAppearanceSample a,
    TerrainAppearanceSample b, float t)
{
    return TerrainAppearanceSample(mix(a.color,b.color,t),
        mix(a.roughness,b.roughness,t),mix(a.metallic,b.metallic,t),
        mix(a.ao,b.ao,t),mix(a.heightGradient,b.heightGradient,t),
        mix(a.wallMask,b.wallMask,t),mix(a.accentMask,b.accentMask,t),
        mix(a.stains,b.stains,t),mix(a.sparkle,b.sparkle,t));
}

float terrainFilteredStep(float threshold, float halfWidth, float value,
    float screenWidth)
{
    float width = max(max(halfWidth,screenWidth * 0.5),0.00001);
    return smoothstep(threshold-width,threshold+width,value);
}

TerrainAppearanceSample terrainEvaluateMaterial(TerrainMaterial m,
    TerrainPatterns p, TerrainPatternContext c, float vegetation)
{
    vec4 weights = m.pattern;
    float weightSum = dot(weights,vec4(1.0));
    weights /= max(weightSum,0.00001);
    float value = weightSum > 0.00001 ? dot(weights,p.value) : 0.5;
    float contrast = m.ramp.z;
    value = clamp((value-0.5) * contrast + 0.5 + m.ramp.w,0.0,1.0);
    float screenWidth = terrainFieldWidth(p.gradient * weights,c) * contrast;
    float midpoint = m.ramp.x;
    float softness = m.ramp.y;
    // softness1: continuous low/mid/high ramps. softness0: three bands with
    // derivative-filtered edges, never a hard unfiltered step on the screen.
    float lowMid = terrainFilteredStep(midpoint*0.5,
        midpoint*0.5*softness,value,screenWidth);
    float midHigh = terrainFilteredStep((1.0+midpoint)*0.5,
        (1.0-midpoint)*0.5*softness,value,screenWidth);
    vec4 palette = mix(mix(m.colorLow,m.colorMid,lowMid),m.colorHigh,midHigh);
    float roughness = palette.a + m.response.w * (c.grain.value*2.0-1.0);
    vec3 heightGradient = p.normalGradient * m.response.z;
    return TerrainAppearanceSample(palette.rgb,
        clamp(roughness,0.04,1.0),m.response.x,
        m.response.y,heightGradient,0.0,0.0,
        vec4(m.fineSurface.zw*vegetation,m.fineSurface.zw*(1.0-vegetation)),
        vec2(m.fineSurface.x,m.fineSurface.x*m.fineSurface.y));
}

TerrainMaterial terrainLoadMaterial(uint id, bool wall, bool accent)
{
    if (wall)
        return accent ? terrainBiomes[id].wall.accent : terrainBiomes[id].wall.base;
    return accent ? terrainBiomes[id].ground.accent : terrainBiomes[id].ground.base;
}

TerrainAppearanceSample terrainEvaluateSurface(uint id, bool wall, TerrainPatternContext c)
{
    // Read controls first. Copying an entire biome/surface eagerly can load all
    // inactive records on a driver; load only material records selected below.
    vec4 blend = wall ? terrainBiomes[id].wall.blend : terrainBiomes[id].ground.blend;
    vec4 detail = wall ? terrainBiomes[id].wall.detail : terrainBiomes[id].ground.detail;
    vec2 maskWeights = blend.zw;
    float maskSum = maskWeights.x + maskWeights.y;
    maskWeights /= max(maskSum,0.00001);
    float mask = maskSum > 0.00001
        ? dot(maskWeights,vec2(c.macro.value,c.patchNoise.value)) : 0.5;
    vec3 maskGradient = c.macro.gradient * maskWeights.x
                      + c.patchNoise.gradient * maskWeights.y;
    float accent = terrainFilteredStep(blend.x,blend.y,
        mask,terrainFieldWidth(maskGradient,c));
    // Exact endpoints load/evaluate only their active material. Keep the common
    // evaluation outside branches so drivers do not inline three copies of it.
    bool partial = accent > 0.0 && accent < 1.0;
    TerrainMaterial base = terrainLoadMaterial(id,wall,accent >= 1.0);
    TerrainMaterial top = base;
    vec4 activeWeights = base.pattern;
    if (partial)
    {
        top = terrainLoadMaterial(id,wall,true);
        activeWeights = activeWeights*(1.0-accent) + top.pattern*accent;
    }
    TerrainPatterns patterns = terrainSurfacePatterns(detail,activeWeights,c);
    vec4 normalControl = wall ? terrainBiomes[id].wall.normal : terrainBiomes[id].ground.normal;
    patterns.normalGradient = terrainSurfaceNormal(normalControl,detail,c);
    int route = wall ? 2 : 0;
    vec4 routing = terrainBiomes[id].stainRouting;
    TerrainAppearanceSample appearance = terrainEvaluateMaterial(base,patterns,c,
        routing[route + (accent >= 1.0 ? 1 : 0)]);
    if (partial)
        appearance = terrainMixAppearance(appearance,
            terrainEvaluateMaterial(top,patterns,c,routing[route+1]),accent);
    appearance.accentMask = accent;
    return appearance;
}

TerrainAppearanceSample terrainEvaluateBiome(uint id, vec3 baseNormal,
    TerrainPatternContext c)
{
    id = min(id,25u);
    c = terrainBiomePatterns(id,terrainBiomes[id].spectrum,c);
    vec4 slopeControl = terrainBiomes[id].slope;
    // Use the signed BASE normal. Cave ceilings are walls, and grain must not
    // move material coverage. abs(normal.y) would incorrectly snow the ceilings.
    float upward = clamp(baseNormal.y,0.0,1.0);
    float distortion = (c.patchNoise.value-0.5)*slopeControl.z;
    // Vanish the distortion at vertical faces; even extreme artist settings
    // cannot expose a ground layer on the underside of an overhang.
    float slope = 1.0-upward + distortion*upward;
    float screenWidth = c.normalWidth*abs(1.0-distortion)
        + upward*abs(slopeControl.z)*terrainFieldWidth(c.patchNoise.gradient,c);
    float start = slopeControl.x;
    float end = max(slopeControl.y,start+0.00001);
    float wall = terrainFilteredStep((start+end)*0.5,(end-start)*0.5,slope,screenWidth);
    // A short continuous saturation also keeps derivative-expanded mask edges
    // from leaking ground onto vertical/downward faces; no hard switch at Y=0.
    wall = mix(1.0,wall,smoothstep(0.0,0.05,upward));
    TerrainAppearanceSample appearance;
    if (wall <= 0.0)
        appearance = terrainEvaluateSurface(id,false,c);
    else if (wall >= 1.0)
        appearance = terrainEvaluateSurface(id,true,c);
    else
        appearance = terrainMixAppearance(terrainEvaluateSurface(id,false,c),
            terrainEvaluateSurface(id,true,c),wall);
    appearance.wallMask = wall;
    appearance.heightGradient *= slopeControl.w;
    return appearance;
}

TerrainAppearanceSample terrainEvaluateAppearance(vec3 baseNormal,
    TerrainPatternContext c)
{
#if TERRAIN_FIXED_BIOME >= 0
    return terrainEvaluateBiome(uint(TERRAIN_FIXED_BIOME),baseNormal,c);
#else
    float coast = clamp((worldPosition.y-terrainSeaLevel)
        / max(terrainSeaBlendWidth,0.001)+0.5,0.0,1.0);
    coast = coast*coast*coast*(coast*(coast*6.0-15.0)+10.0);
    if (coast <= 0.0) return terrainEvaluateBiome(25u,baseNormal,c);
    vec2 cell = clamp(climate,0.0,1.0)*4.0;
    uvec2 i = uvec2(floor(cell));
    uvec2 j = min(i+uvec2(1),uvec2(4));
    vec2 f = fract(cell);
    f = f*f*f*(f*(f*6.0-15.0)+10.0);
    uvec4 id = uvec4(terrainBiomeMatrix[i.x+i.y*5u],terrainBiomeMatrix[j.x+i.y*5u],
        terrainBiomeMatrix[i.x+j.y*5u],terrainBiomeMatrix[j.x+j.y*5u]);
    vec4 weights = vec4((1.0-f.x)*(1.0-f.y),f.x*(1.0-f.y),(1.0-f.x)*f.y,f.x*f.y);
    // Preserve all four weights. Only EXACT zero skips work, never top-two IDs.
    TerrainAppearanceSample appearance = TerrainAppearanceSample(vec3(0.0),
        0.0,0.0,0.0,vec3(0.0),0.0,0.0,vec4(0.0),vec2(0.0));
    for (int corner=0; corner<4; ++corner)
    {
        if (weights[corner] <= 0.0) continue;
        TerrainAppearanceSample cornerAppearance = terrainEvaluateBiome(id[corner],baseNormal,c);
        float weight = weights[corner];
        appearance.color += cornerAppearance.color*weight;
        appearance.roughness += cornerAppearance.roughness*weight;
        appearance.metallic += cornerAppearance.metallic*weight;
        appearance.ao += cornerAppearance.ao*weight;
        appearance.heightGradient += cornerAppearance.heightGradient*weight;
        appearance.wallMask += cornerAppearance.wallMask*weight;
        appearance.accentMask += cornerAppearance.accentMask*weight;
        appearance.stains += cornerAppearance.stains*weight;
        appearance.sparkle += cornerAppearance.sparkle*weight;
    }
    if (coast < 1.0)
        appearance = terrainMixAppearance(terrainEvaluateBiome(25u,baseNormal,c),appearance,coast);
    return appearance;
#endif
}

#endif
