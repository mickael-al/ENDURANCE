#version 450 core
// Dominant-luminance oil paint, applied to display-referred color after tone
// mapping. Fixed disk kernels: 1/5/13/29/49 samples for radii 0/1/2/3/4.
#ifndef GE_OIL_RADIUS
#define GE_OIL_RADIUS 2
#endif
#if GE_OIL_RADIUS < 0 || GE_OIL_RADIUS > 4
#error Unsupported oil painting radius
#endif
layout(binding=0) uniform sampler2D u_image;
uniform float u_strength;
layout(location=0) out vec4 fragColor;
const int Bins=24;
#if GE_OIL_RADIUS > 0
// Static disk offsets: no runtime radius test and no repeated center fetch.
const ivec2 offsets[]=ivec2[](
    ivec2(1,0),ivec2(-1,0),ivec2(0,1),ivec2(0,-1)
#if GE_OIL_RADIUS >= 2
    ,ivec2(1,1),ivec2(1,-1),ivec2(-1,1),ivec2(-1,-1)
    ,ivec2(2,0),ivec2(-2,0),ivec2(0,2),ivec2(0,-2)
#endif
#if GE_OIL_RADIUS >= 3
    ,ivec2(2,1),ivec2(2,-1),ivec2(-2,1),ivec2(-2,-1)
    ,ivec2(1,2),ivec2(1,-2),ivec2(-1,2),ivec2(-1,-2)
    ,ivec2(2,2),ivec2(2,-2),ivec2(-2,2),ivec2(-2,-2)
    ,ivec2(3,0),ivec2(-3,0),ivec2(0,3),ivec2(0,-3)
#endif
#if GE_OIL_RADIUS >= 4
    ,ivec2(3,1),ivec2(3,-1),ivec2(-3,1),ivec2(-3,-1)
    ,ivec2(1,3),ivec2(1,-3),ivec2(-1,3),ivec2(-1,-3)
    ,ivec2(3,2),ivec2(3,-2),ivec2(-3,2),ivec2(-3,-2)
    ,ivec2(2,3),ivec2(2,-3),ivec2(-2,3),ivec2(-2,-3)
    ,ivec2(4,0),ivec2(-4,0),ivec2(0,4),ivec2(0,-4)
#endif
);
#endif
int colorBin(vec3 color)
{
    // Bound even unexpected HDR/negative input; never index outside the array.
    float luminance=dot(clamp(color,0.0,1.0),vec3(0.2126,0.7152,0.0722));
    return clamp(int(luminance*float(Bins)),0,Bins-1);
}
void main()
{
    ivec2 limit=textureSize(u_image,0)-1;
    ivec2 pixel=clamp(ivec2(gl_FragCoord.xy),ivec2(0),limit);
    vec4 source=texelFetch(u_image,pixel,0);
#if GE_OIL_RADIUS == 0
    fragColor=source;
#else
    vec4 groups[Bins]; // RGB sums and population; keep the original color mode.
    for(int i=0;i<Bins;++i) groups[i]=vec4(0);
    int centre=colorBin(source.rgb),best=centre;
    groups[centre]=vec4(source.rgb,1);
    for(int i=0;i<offsets.length();++i)
    {
        vec3 color=texelFetch(u_image,clamp(pixel+offsets[i],ivec2(0),limit),0).rgb;
        groups[colorBin(color)]+=vec4(color,1);
    }
    // Prefer the center's luminance in ties instead of systematically favoring
    // darker bins. The disk also avoids a square brush footprint at silhouettes.
    for(int i=0;i<Bins;++i)
        if(groups[i].a>groups[best].a ||
           (groups[i].a==groups[best].a && abs(i-centre)<abs(best-centre))) best=i;
    vec3 paint=groups[best].rgb/max(groups[best].a,1.0);
    fragColor=vec4(mix(source.rgb,paint,clamp(u_strength,0.0,1.0)),source.a);
#endif
}
