#version 460 core
#include "../Geometry/gbuffer_position.glsl"
#include "../Geometry/gbuffer_geometry.glsl"

layout(location = 0) in vec3 viewPosition;
layout(location = 1) in vec3 worldPosition;
layout(location = 2) in vec3 worldNormal;
layout(location = 3) in vec2 climate;
layout(location = 4) flat in uint chunkLod;

// The engine stores view-space positions but WORLD-space normals in its G-buffer.
layout(location = 0) out vec2 gPosition;
layout(location = 1) out vec3 gNormal;
layout(location = 2) out vec4 gColor;
layout(location = 3) out vec3 gOther;

uniform float microDetailStrength;
uniform float microDetailScale;
uniform uint worldSeed;
uniform bool debugLod;
// -1 follows the climate matrix; 0..25 isolates a biome for inspection.
uniform int terrainBiomeOverride = -1;
uniform float terrainSeaLevel = 0.0;
uniform float terrainSeaBlendWidth = 64.0;
uniform float terrainMacroPeriod = 384.0;
uniform float terrainPatchPeriod = 32.0;
// Deferred lighting decodes G-buffer albedo using this engine gamma. All
// palette, layer and climate arithmetic stays linear until the final store.
uniform float terrainAlbedoGamma = 2.2;
// 0 final; 1 palette; 2 wall; 3 accent; 4 roughness; 5 metallic/AO; 6 normal.
uniform int terrainMaterialDebug = 0;

#include "patterns.glsl"
#include "materials.glsl"

void main()
{
    // Capture derivatives before climate/material branching. The geometry tag
    // must describe the triangle independently of the material's bump normal.
    float geometryTag=geGBufferGeometryTag(dFdx(viewPosition),dFdy(viewPosition));
    vec3 baseNormal = normalize(worldNormal);
    vec3 dx = dFdx(worldPosition);
    vec3 dy = dFdy(worldPosition);
    float normalWidth = abs(dFdx(baseNormal.y)) + abs(dFdy(baseNormal.y));
    TerrainPatternContext patterns = terrainMakePatternContext(worldPosition,dx,dy,normalWidth);
    TerrainAppearanceSample appearance = terrainEvaluateAppearance(baseNormal,patterns);

    // Blend height gradients first, project tangentially and normalize once.
    vec3 gradient = appearance.heightGradient;
    gradient -= baseNormal * dot(baseNormal,gradient);
    vec3 normal = normalize(baseNormal-gradient);
    vec3 color = appearance.color;
    if (terrainMaterialDebug == 2)
        color = vec3(appearance.wallMask);
    else if (terrainMaterialDebug == 3)
        color = vec3(appearance.accentMask);
    else if (terrainMaterialDebug == 4)
        color = vec3(appearance.roughness);
    else if (terrainMaterialDebug == 5)
        color = vec3(appearance.metallic,appearance.ao,0.0);
    else if (terrainMaterialDebug == 6)
        color = normal*0.5+0.5;
    if (debugLod)
    {
        const vec3 colors[7] = vec3[7](vec3(0.22, 0.75, 0.34), vec3(0.25, 0.67, 0.84),
            vec3(0.37, 0.40, 0.88), vec3(0.73, 0.34, 0.83), vec3(0.88, 0.38, 0.35),
            vec3(0.92, 0.68, 0.28), vec3(0.78, 0.82, 0.32));
        color = colors[chunkLod % 7u];
    }
    // Existing positive tags take the unlit/emission path in deferred lighting.
    // Only inspection views use it; production geometry/shadow tags are intact.
    if (terrainMaterialDebug > 0 && !debugLod) geometryTag = 1.0;
    gPosition = geGBufferPackPosition(viewPosition,geometryTag);
    gNormal = normal;
    gColor = vec4(pow(max(color,vec3(0.0)),vec3(1.0/max(terrainAlbedoGamma,0.001))),1.0);
    gOther = vec3(appearance.metallic,appearance.roughness,appearance.ao);
}
