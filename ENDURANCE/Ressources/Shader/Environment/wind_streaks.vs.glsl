#version 450 core
#ifndef ENDURANCE_WIND_SEGMENTS
#define ENDURANCE_WIND_SEGMENTS 24
#endif

layout(std430,binding=0) readonly buffer UniformBufferCamera
{ vec3 camPos; mat4 view; mat4 invView; mat4 proj; } ubc;
struct WindStreak { vec4 centerLength; vec4 directionWidth; vec4 curve; vec4 appearance; };
layout(std430,binding=6) readonly buffer WindStreakData { WindStreak streaks[]; };
uniform vec3 uWindAnchor;
uniform vec2 uEnvironmentInvResolution;
out VS_OUT
{
    vec2 uv;
    vec4 color;
    vec3 worldPosition;
    float viewDepth;
    float nearFade;
} vs_out;

void main()
{
    WindStreak streak=streaks[gl_InstanceID];
    float along=float(gl_VertexID/2)/float(ENDURANCE_WIND_SEGMENTS);
    float side=float(gl_VertexID%2)*2.0-1.0;
    vec3 direction=streak.directionWidth.xyz;
    vec3 lateral=vec3(-direction.z,0,direction.x);
    float spatialPhase=along*6.28318530718*streak.curve.z;
    float phase=spatialPhase+streak.curve.w;
    // Both travel-driven phases have period 2*pi, matching the CPU phase wrap.
    // Only the spatial frequency differs between the two bending axes.
    float verticalPhase=spatialPhase*.73+streak.curve.w+1.7;
    vec3 local=streak.centerLength.xyz+direction*((along-.5)*streak.centerLength.w)
        +lateral*(sin(phase)*streak.curve.x)+vec3(0,sin(verticalPhase)*streak.curve.y,0);
    float frequency=6.28318530718*streak.curve.z/streak.centerLength.w;
    vec3 tangent=normalize(direction+lateral*(cos(phase)*streak.curve.x*frequency)
        +vec3(0,cos(verticalPhase)*streak.curve.y*frequency*.73,0));
    // The curved centerline is world-fixed. Only its thin cross-section faces
    // the viewer, with a deterministic perpendicular when viewed end-on.
    vec3 relative=local+(uWindAnchor-ubc.camPos);
    vec3 widthAxis=cross(tangent,-relative);
    if(dot(widthAxis,widthAxis)<1e-8)
        widthAxis=cross(tangent,abs(tangent.y)<.9?vec3(0,1,0):vec3(0,0,1));
    widthAxis=normalize(widthAxis);
    vec3 viewCenter=mat3(ubc.view)*relative;
    float pixelSize=max(-viewCenter.z,0.0)*2.0*uEnvironmentInvResolution.y/max(abs(ubc.proj[1][1]),.001);
    float rasterWidth=max(streak.directionWidth.w,pixelSize*.55);
    local+=widthAxis*(side*rasterWidth);
    vec3 view=mat3(ubc.view)*(local+(uWindAnchor-ubc.camPos));
    vs_out.uv=vec2(along,side);
    vs_out.color=vec4(streak.appearance.yzw,streak.appearance.x*streak.directionWidth.w/rasterWidth);
    vs_out.worldPosition=uWindAnchor+local;
    vs_out.viewDepth=-view.z;
    vs_out.nearFade=smoothstep(.6,2.5,length(relative));
    gl_Position=ubc.proj*vec4(view,1);
}
