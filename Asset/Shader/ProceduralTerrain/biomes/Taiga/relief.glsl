// Biome 3: Taiga. The caller clamps and accumulates this 2D result.
// x: relative height; y: total 3D band thickness; z: band offset; w: rock mask.
// XYZ are in metres before detailScale; w is in [0,1].
#ifdef FIELD_ENABLE_RELIEF_Taiga
#ifndef ENDURANCE_RELIEF_TAIGA_GLSL
#define ENDURANCE_RELIEF_TAIGA_GLSL

#include "../../biome_relief_common.glsl"

vec4 fieldReliefTaiga(vec2 p)
{
    vec2 q = fieldSurfaceRotate(p, vec2(0.8, -0.6));
    float valley = 1.0 - smoothstep(0.04, 0.26,
        abs(fieldNoise2(q * vec2(0.0006, 0.0020), 130u)));
    float esker = fieldRidge2(q * vec2(0.0014, 0.0050), 131u);
    esker *= esker;
    esker *= esker;
    float shield = fieldSurfaceLandmark(p, 384.0, 132u, 0.0, 0.22, 0.0, 0.68);
    float h = fieldNoise2(p * 0.0012, 133u) * 75.0 - valley * 72.0;
    h += esker * 35.0 + shield * 250.0;
    return vec4(h, 342.0, -68.0, shield);
}

#endif
#endif
