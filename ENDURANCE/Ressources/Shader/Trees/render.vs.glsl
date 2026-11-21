#version 460 core
#include "common.glsl"
#if TREE_WIND || TREE_CROWN_NORMALS
#include "deformation.glsl"
#endif
#if TREE_IMPOSTOR
#include "impostor.glsl"
#endif
layout(location=0) in vec3 inPosition;
layout(location=1) in vec3 inNormal;
layout(location=2) in vec2 inUV;
layout(location=3) in vec4 inColor;
layout(std430,binding=4) readonly buffer TreeInstances { TreeInstance instances[]; };
layout(std430,binding=7) readonly buffer VisibleInstances { uvec2 visible[]; };
uniform mat4 uClip, uView;
uniform vec3 uEye; // Viewpoint, or a constant direction for orthographic passes.
#if !TREE_SHADOW
out vec3 viewPosition;
out vec3 worldNormal;
out vec3 color;
#endif
out vec2 uv;
flat out uint fadeMask,siteHash;
#if TREE_IMPOSTOR
flat out vec2 rotation;
flat out ivec3 captureIDs;
flat out vec3 captureWeights;
out vec3 captureUV0, captureUV1, captureUV2;
#endif
void main()
{
    uvec2 reference=visible[gl_BaseInstance+gl_InstanceID];
    TreeInstance instance=instances[reference.x];
    vec2 rotationValue=instance.rotation; fadeMask=reference.y; siteHash=instance.hash;
    vec3 local=inPosition;
    TreeModel model=models[instance.model];
#if TREE_IMPOSTOR
    rotation=rotationValue;
        vec3 centre=instance.positionScale.xyz+treeRotate(model.impostorCentre.xyz*instance.positionScale.w,rotation);
#if TREE_PARALLEL
        vec3 delta=uEye;
#else
        vec3 delta=uEye-centre;
#endif
        vec3 facing=length(delta)>0.00001?normalize(treeUnrotate(delta,rotation)):vec3(0,0,1);
        vec3 right=vec3(facing.z,0,-facing.x);
        right=dot(right,right)>0.00001?normalize(right):vec3(1,0,0);
        vec3 up=cross(facing,right);
        vec3 relative=(right*inPosition.x+up*inPosition.y)*model.scalesImpostor.z;
        local=model.impostorCentre.xyz+relative;
        treeViews(facing,captureIDs,captureWeights);
#if TREE_PARALLEL
        vec3 ray=-facing;
#else
        vec3 ray=relative-treeUnrotate(uEye-centre,rotation)/instance.positionScale.w;
#endif
        // Homogeneous UVs retain the perspective ray/plane intersection after
        // raster interpolation; no trigonometry or basis construction per pixel.
        vec3 coords[3];
        for(int i=0;i<3;++i)
        {
            mat3 basis=treeCaptureBasis(captureIDs[i]);
            float denominator=min(dot(ray,basis[2]),-0.00001);
            vec3 q=relative*denominator-ray*dot(relative,basis[2]);
            coords[i]=vec3(vec2(dot(q,basis[0]),dot(q,basis[1]))/(2.0*model.scalesImpostor.z)+0.5*denominator,denominator);
        }
        captureUV0=coords[0]; captureUV1=coords[1]; captureUV2=coords[2];
#endif
    vec3 world=instance.positionScale.xyz+treeRotate(local*instance.positionScale.w,rotationValue);
    vec3 normal=inNormal;
#if TREE_WIND || TREE_CROWN_NORMALS
    float distanceToCamera;
    float effectWeight=treeEffectWeight(instance,model,distanceToCamera);
#if TREE_CROWN_NORMALS
    normal=treeCrownNormal(local,normal,model,effectWeight);
#endif
#endif
    normal=treeRotate(normal,rotationValue);
#if TREE_WIND
    treeBend(local,instance,model,treeWindDisplacement(instance,model,effectWeight,distanceToCamera),world,normal);
#endif
    uv=inUV;
#if !TREE_SHADOW
    worldNormal=normal; color=inColor.rgb;
    viewPosition=(uView*vec4(world,1)).xyz;
#endif
    gl_Position=uClip*vec4(world,1);
}
