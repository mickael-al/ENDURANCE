// Shared by the G-buffer and shadow vertex programs. No per-fragment work.
// Compile-time features: the static/wood/impostor paths omit unused work.
uniform vec4 uEffectCamera; // Main camera XYZ, projection scale.
uniform vec4 uEffectLod; // Impostor fade end, orthographic flag, wind distance, unused.
#if TREE_WIND
layout(binding=3) uniform sampler2D uWindNoise;
uniform vec4 uWind; // Wrapped advection UV, inverse noise period, unused.
uniform vec2 uWindDirection;
#endif

float treeEffectWeight(TreeInstance instance,TreeModel model,out float distanceToCamera)
{
    vec3 centre=instance.positionScale.xyz+treeRotate(model.centreRadius.xyz*instance.positionScale.w,instance.rotation);
    distanceToCamera=length(centre-uEffectCamera.xyz);
    float projected=model.centreRadius.w*instance.positionScale.w*uEffectCamera.w
        /max(mix(distanceToCamera,1.0,uEffectLod.y),0.01);
    // Restore the authored pose before the existing baked impostor fades in.
    return mix(1.0,smoothstep(uEffectLod.x,uEffectLod.x*2.0,projected),model.scalesImpostor.w);
}

#if TREE_WIND
vec2 treeWindDisplacement(TreeInstance instance,TreeModel model,float weight,float distanceToCamera)
{
    float gust=textureLod(uWindNoise,instance.positionScale.xz*uWind.z-uWind.xy,0.0).r*2.0-1.0;
    float distanceFade=1.0-smoothstep(uEffectLod.z*0.7,uEffectLod.z,distanceToCamera);
    return uWindDirection*(gust*model.crownShape.w*instance.positionScale.w*weight*distanceFade);
}
void treeBend(vec3 local,TreeInstance instance,TreeModel model,vec2 displacement,
              inout vec3 world,inout vec3 normal)
{
    float height=max(model.crown.w,0.001);
    float h=clamp(local.y/height,0.0,1.0);
    world.xz+=displacement*(h*h);
#if !TREE_SHADOW
    // Inverse-transpose of the height shear: the lighting follows the bend.
    normal.y-=dot(normal.xz,displacement)*(2.0*h/(height*instance.positionScale.w));
#endif
}
#endif
