#version 450 core
#ifndef GE_BIOME_PARTICLE_KIND
#define GE_BIOME_PARTICLE_KIND 0
#endif
#include "../../../../Asset/Shader/Atmosphere/sample.glsl"

in VS_OUT
{
    vec2 uv;
    vec4 color;
    vec3 worldPosition;
    float viewDepth;
    flat float emission;
    flat float variation;
    flat float aspect;
} fs_in;
layout(binding=0) uniform sampler2D uEnvironmentOpaqueDepth;
uniform vec2 uEnvironmentInvResolution;
out vec4 o_FragColor;

float segmentDistance(vec2 p,vec2 a,vec2 b)
{
    vec2 ab=b-a;
    return length(p-a-ab*clamp(dot(p-a,ab)/dot(ab,ab),0.0,1.0));
}
float stroke(float distance,float width,float aa)
{ return 1.0-smoothstep(width-aa,width+aa,distance); }
float coverage(vec2 p,float aa)
{
    float r2=dot(p,p);
#if GE_BIOME_PARTICLE_KIND == 0
    // Small irregular mineral grains, not luminous circular pollen discs.
    vec2 q=p*vec2(1.12,.86);
    float edge=max(abs(q.x)*.8+abs(q.y)*.56,abs(q.y)*.94- .12*q.x);
    // A compact support is essential: the previous polygon still had nonzero
    // opacity at UV.y=0/1, so the UV discard cut visible bars into large grains.
    float support=max(0.0,1.0-r2);
    return (1.0-smoothstep(.55-aa,.9+aa,edge))*(.65+.35*clamp(.5+.5*p.y,0.0,1.0))*support*support;
#elif GE_BIOME_PARTICLE_KIND == 1
    // Fold into one of six sectors, then grow a trunk and two pairs of branches.
    // Actual open space between dendrites survives nearby views; minification
    // smoothly reduces the fine detail instead of turning it into aliasing.
    float r=sqrt(r2);
    float angle=abs(mod(atan(p.y,p.x)+.523598776,1.047197551)-.523598776);
    vec2 q=vec2(cos(angle),sin(angle))*r;
    float armLength=mix(.81,.97,fs_in.variation);
    float d=segmentDistance(q,vec2(.05,0),vec2(armLength,0));
    float trunk=stroke(d,.026,aa);
    float branch1=stroke(segmentDistance(q,vec2(.29,0),vec2(.48,.145)),.019,aa);
    float branch2=stroke(segmentDistance(q,vec2(.51,0),vec2(.74,.17)),.017,aa);
    float dendrite=max(max(trunk,branch1),branch2);
    dendrite=max(dendrite,1.0-smoothstep(.07,.13,r));
    float farFlake=(1.0-smoothstep(.15,1.0,r2))*.43;
    return mix(dendrite,farFlake,smoothstep(.08,.32,aa))*(1.0-smoothstep(.94,1.04,r));
#elif GE_BIOME_PARTICLE_KIND == 2
    // Gaussian cross-section has no opaque rectangular card at close range.
    return exp(-p.x*p.x*20.0)*(1.0-smoothstep(.60-aa,1.0+aa,abs(p.y)));
#elif GE_BIOME_PARTICLE_KIND == 3
    // Pointed asymmetric leaf with a darker midrib and alternating side veins.
    float width=(1.0-p.y*p.y)*(.65+.12*p.y);
    float leaf=stroke(abs(p.x+.08*sin(p.y*3.0)),max(width,0.0),aa);
    float rib=1.0-.22*stroke(abs(p.x),.025,aa);
    float veins=.84+.16*smoothstep(.05,.13,abs(sin((p.y-abs(p.x)*.7)*15.0)));
    return leaf*rib*veins*(1.0-smoothstep(.9,1.0,abs(p.y)));
#elif GE_BIOME_PARTICLE_KIND == 4
    // A cupped, notched petal, wider above the narrow stem.
    float width=sqrt(max(0.0,1.0-p.y*p.y))*(.49+.25*p.y);
    float notch=1.0-.55*smoothstep(.56,.88,p.y)*(1.0-smoothstep(.03,.20,abs(p.x)));
    return stroke(abs(p.x),width,aa)*notch*(.74+.26*(1.0-abs(p.x)));
#elif GE_BIOME_PARTICLE_KIND == 5
    // Samara wing and its seed pod; distinct from rain capsules.
    float wing=stroke(abs(p.x+.15*p.y),max(0.0,.48*(1.0-p.y*p.y)),aa);
    float pod=1.0-smoothstep(.08,.18,length((p-vec2(0,-.42))*vec2(1,1.7)));
    return max(wing*.62,pod);
#elif GE_BIOME_PARTICLE_KIND == 6
    return exp(-r2*5.5)*(1.0-smoothstep(.7,1.0,r2));
#elif GE_BIOME_PARTICLE_KIND == 7
    // Three overlapping billows with a broad hollow breaking the circular card.
    float a=1.0-smoothstep(.12,1.0,dot(p*vec2(.93,1.12),p*vec2(.93,1.12)));
    vec2 q=p-vec2(.30,.10),r=p+vec2(.30,-.12);
    float b=1.0-smoothstep(.05,.52,dot(q,q));
    float c=1.0-smoothstep(.04,.46,dot(r,r));
    float billow=clamp(a*.46+b*.34+c*.30,0.0,1.0);
    float hollow=.72+.28*smoothstep(.02,.24,dot(p-vec2(.13,-.11),p-vec2(.13,-.11)));
    return billow*billow*hollow;
#elif GE_BIOME_PARTICLE_KIND == 8
    float taper=.21+.32*(1.0-p.y)*.5;
    return stroke(abs(p.x),taper,aa)*(1.0-smoothstep(.18,1.0,r2));
#elif GE_BIOME_PARTICLE_KIND == 9
    float diamond=abs(p.x)+abs(p.y);
    return stroke(diamond,.86,aa)*(.42+.58*smoothstep(-aa,aa,p.y-p.x));
#elif GE_BIOME_PARTICLE_KIND == 10
    // Rounded droplet head with a short trailing neck.
    // +Y is the leading edge because the vertex basis follows velocity.
    float head=1.0-smoothstep(.10,.29,length(vec2(p.x,(p.y-.40)*fs_in.aspect)));
    float width=max(.025,.11*(p.y+1.0));
    float tail=exp(-p.x*p.x/(width*width))*smoothstep(-1.0,-.70,p.y)
        *(1.0-smoothstep(.30,.48,p.y));
    return max(head,tail*.32);
#elif GE_BIOME_PARTICLE_KIND == 11
    vec2 a=(p-vec2(.37,.10))*vec2(2.0,1.45),b=(p+vec2(.37,-.10))*vec2(2.0,1.45);
    float wings=max(1.0-smoothstep(.15,1.0,dot(a,a)),1.0-smoothstep(.15,1.0,dot(b,b)))*.60;
    float body=stroke(abs(p.x),.10,aa)*(1.0-smoothstep(.32,.75,abs(p.y)));
    return max(wings,body)*(1.0-smoothstep(.8,1.05,r2));
#elif GE_BIOME_PARTICLE_KIND == 12
    // A round hot head leads a narrow soft trail; multiplying Y by the
    // authored stretch keeps the head round even on a long meteor streak.
    float headRadius=length(vec2(p.x,(p.y-.63)*fs_in.aspect));
    float head=exp(-headRadius*headRadius*30.0);
    float width=mix(.035,.15,clamp((p.y+1.0)/1.6,0.0,1.0));
    float tail=exp(-p.x*p.x/(width*width))*smoothstep(-1.0,-.83,p.y)
        *(1.0-smoothstep(.58,.72,p.y))*exp((p.y-.63)*1.05);
    return max(head,tail*.75);
#elif GE_BIOME_PARTICLE_KIND == 13
    // Jagged charred sheets rather than a sixfold snow crystal.
    float polygon=max(max(p.x*.86+p.y*.51,-p.x*.68+p.y*.75),max(-p.y*.92+p.x*.3,-p.y*.55-p.x*.77));
    float hole=1.0-.65*(1.0-smoothstep(.055,.13,length(p-vec2(.18,.12))));
    return stroke(polygon,.68,aa)*hole*(.73+.27*smoothstep(-.7,.5,p.y));
#elif GE_BIOME_PARTICLE_KIND == 14
    // Dandelion-like tuft with an attached grain; independent of snow's lattice.
    vec2 q=p-vec2(0,.20);
    float radius=length(q),angle=atan(q.y,q.x);
    float hair=stroke(abs(sin(angle*5.0+fs_in.variation)),.14,aa*4.0)
        *(1.0-smoothstep(.3,.72,radius))*smoothstep(.02,.14,radius);
    float stem=stroke(segmentDistance(p,vec2(0,-.6),vec2(0,.2)),.025,aa);
    float grain=1.0-smoothstep(.04,.14,length((p-vec2(0,-.62))*vec2(1.0,1.6)));
    return max(max(hair*.63,stem*.65),grain);
#elif GE_BIOME_PARTICLE_KIND == 16
    // Compact, twice-smooth optical density. No hard polygon, UV border or
    // opaque card survives at any sprite aspect, rotation or near-plane size.
    float support=max(0.0,1.0-r2);
    vec2 a=(p-vec2(-.18+.16*fs_in.variation,.08))*vec2(1.3,1.7);
    vec2 b=(p+vec2(.23,-.13))*vec2(1.6,1.3);
    float lobeA=max(0.0,1.0-dot(a,a)),lobeB=max(0.0,1.0-dot(b,b));
    return support*support*support*(.38+.40*lobeA*lobeA+.22*lobeB*lobeB);
#elif GE_BIOME_PARTICLE_KIND == 17
    // A low, soft veil with a few fine streaks inside; all microstructure is
    // multiplied by the compact envelope, so no card outline can emerge.
    float support=max(0.0,1.0-r2);
    float cross=max(0.0,1.0-p.y*p.y);
    float fine=.5+.5*sin((p.y+p.x*.19+fs_in.variation)*43.0);
    fine=fine*fine*fine;
    float detail=mix(fine,.35,smoothstep(.025,.12,aa));
    return support*support*cross*(.22+.32*detail);
#else
    // A translucent spore shell with a small off-centre nucleus.
    float radius=length(p*vec2(1.0,.83));
    float shell=stroke(abs(radius-.52),.075,aa)*.62;
    float body=(1.0-smoothstep(.1,.67,radius))*.25;
    float nucleus=1.0-smoothstep(.06,.17,length(p-vec2(.15,-.10)));
    return max(max(shell,body),nucleus*.8)*(1.0-smoothstep(.80,1.0,radius));
#endif
}
void main()
{
    vec2 p=fs_in.uv*2.0-1.0;
    float aa=max(max(fwidth(p.x),fwidth(p.y))*.65,.002);
    if(any(lessThan(fs_in.uv,vec2(0))) || any(greaterThan(fs_in.uv,vec2(1)))) discard;
    // Every analytic shape must reach zero before its billboard UV boundary.
    // This also protects highly minified or very stretched cards where fwidth
    // broadens a shape's own anti-aliasing beyond its authored support.
    float cardFade=smoothstep(0.0,clamp(aa,.025,.15),1.0-max(abs(p.x),abs(p.y)));
    float alpha=fs_in.color.a*coverage(p,aa)*cardFade;
    if(alpha<.0001) discard;
    ivec2 pixel=clamp(ivec2(gl_FragCoord.xy),ivec2(0),textureSize(uEnvironmentOpaqueDepth,0)-1);
    vec2 opaque=texelFetch(uEnvironmentOpaqueDepth,pixel,0).rg;
    float soft=opaque.y==-2.0?1.0:clamp((-opaque.x-fs_in.viewDepth)/.65,0.0,1.0);
    alpha*=soft;
    if(alpha<.0001) discard;
    float daylight=uAtmosphereActive
        ? mix(.10,1.0,smoothstep(-.12,.22,geAtmSunDirection.y)*step(.5,geAtmSunDirection.w)) : 1.0;
    vec3 illumination=mix(vec3(.16,.21,.30),vec3(1.0),daylight);
    vec3 color=fs_in.color.rgb*(illumination+vec3(fs_in.emission));
#if GE_BIOME_PARTICLE_KIND == 6 || GE_BIOME_PARTICLE_KIND == 8
    color*=1.0+2.0*exp(-dot(p,p)*30.0);
#elif GE_BIOME_PARTICLE_KIND == 12
    float hotCore=exp(-dot(vec2(p.x,(p.y-.63)*fs_in.aspect),vec2(p.x,(p.y-.63)*fs_in.aspect))*65.0);
    color=mix(color,color*.45+vec3(1.0,.88,.68)*(1.0+fs_in.emission),hotCore);
    color*=1.0+3.0*hotCore;
#endif
    color=geAtmComposite(color,fs_in.worldPosition,gl_FragCoord.xy*uEnvironmentInvResolution);
    o_FragColor=vec4(color,alpha);
}
