#version 450 core
// Biome 12: TemperateForest. Terrain-only, world-space and deterministic.
#define FIELD_BIOME_ID 12u
#include "../../field_batch_common.glsl"
#include "../../field_weights.glsl"
#include "../../biome_landforms_3d.glsl"

// Thick layered promontory and a lateral recess; no through-floor pits.
float fieldVolume(vec3 p,FieldColumn column)
{
    if(!landVolumeEnabled(column)) return 0.0;
    LandSite s=landSite(p.xz,2050.0,12110u,.67);
    if(s.mask<=0.0) return 0.0;
    vec3 q=landVolumeLocal(p,column,s);
    float base=column.terrain.x-p.y;
    // Bend the plan of the whole headland, then taper its forward end. The
    // ledges follow one curved formation instead of exposing four box faces.
    vec3 bent=q;
    bent.x-=.18*q.y;
    float along=clamp(bent.x/s.radius,-1.0,1.0);
    bent.z-=s.radius*(.27*along*along+.055*along);
    bent.z/=mix(1.12,.72,smoothstep(-.44,.50,along));
    // One broad warp scallops the cliff; a second, much smaller scale only
    // roughens the layer edge. The vertical layers remain thick and readable.
    float broad= noiseSimplex3D(q/vec3(260.0,420.0,220.0),uSeed^12111u);
    float edge= noiseSimplex3D(q/vec3(78.0,190.0,96.0),uSeed^12112u);
    bent.y-=.045*bent.x+2.5*broad;
    float height=mix(49.0,81.0,s.variant);
    float lowerPlan=(length(bent.xz/(s.radius*vec2(.35,.22)))-1.0)*(.22*s.radius);
    lowerPlan+=11.0*broad+1.5*edge;
    float lower=-max(lowerPlan,abs(bent.y-18.0)-height);
    vec2 capXZ=bent.xz-vec2(.045*s.radius,-.025*s.radius);
    float capWidth=mix(.39,.47,s.variant);
    float capDepth=mix(.31,.25,s.variant);
    float capPlan=(length(capXZ/(s.radius*vec2(capWidth,capDepth)))-1.0)
        *(capDepth*s.radius)+14.0*broad+2.0*edge;
    // A resistant bed overhangs the narrower eroded body. Its softly bevelled
    // elliptical edge and tilted roof retain strata without a manufactured lid.
    float capSide=capPlan+6.0;
    float capVertical=abs(bent.y-height-24.0)-16.0;
    float cap=6.0-(length(max(vec2(capSide,capVertical),0.0))
        +min(max(capSide,capVertical),0.0));
    float rock=landUnion(lower,cap,10.0);
    float target=max(base,rock*uDetailScale);
    vec3 hollow=q-vec3(.12*s.radius,24.0,.26*s.radius);
    float cave=landEllipsoid(hollow,vec3(.25*s.radius,36.0,.17*s.radius));
    target=min(target,cave*uDetailScale);
    return landDelta(p,column,target,s.mask);
}

#include "../../field_volume_accumulate.glsl"
