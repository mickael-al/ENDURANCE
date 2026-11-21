#version 460 core
// Alpha discard must happen before depth writes: deliberately no early_fragment_tests.
layout(binding=0) uniform sampler2D uSurface;
layout(binding=1) uniform sampler2DArray uImpostorAlbedo;
layout(binding=2) uniform sampler2DArray uImpostorNormal;
uniform vec3 uCamera;
uniform vec3 uSun;
uniform int uKind; // 0 wood, 1 foliage, 2 preview-only floor, 3 cached billboard.
uniform int uCapture;
uniform ivec3 uImpostorViews;
uniform vec3 uImpostorWeights;
uniform mat3 uImpostorBases[3];
uniform vec3 uBillboardCentre;
uniform float uBillboardExtent;
uniform float uGroundExtent;
in vec3 position;
in vec3 normal;
in vec2 uv;
in vec4 color;
layout(location=0) out vec4 outColor;
layout(location=1) out vec4 outNormal;

vec3 encodeSRGB(vec3 value)
{
    value=max(value,vec3(0.0));
    return mix(12.92*value,1.055*pow(value,vec3(1.0/2.4))-0.055,
               step(vec3(0.0031308),value));
}

void main()
{
    vec3 albedo;
    vec3 N=normal;
    if(uKind==3)
    {
        vec4 material=vec4(0.0), capturedNormal=vec4(0.0);
        vec3 ray=normalize(position-uCamera);
        vec3 relative=position-uBillboardCentre;
        for(int i=0;i<3;++i)
        {
            if(uImpostorWeights[i]<=0.0) continue;
            mat3 basis=uImpostorBases[i];
            // Intersect the view ray with each capture plane. In particular the
            // zenith image keeps its orientation when the camera rotates.
            float denominator=dot(ray,basis[2]);
            float t=-dot(relative,basis[2])/min(denominator,-0.05);
            vec3 q=relative+ray*t;
            vec2 sampleUV=vec2(dot(q,basis[0]),dot(q,basis[1]))/(2.0*uBillboardExtent)+0.5;
            vec3 address=vec3(sampleUV,float(uImpostorViews[i]));
            float inside=step(0.0,sampleUV.x)*step(sampleUV.x,1.0)
                        *step(0.0,sampleUV.y)*step(sampleUV.y,1.0);
            float weight=uImpostorWeights[i]*inside;
            material+=texture(uImpostorAlbedo,address)*weight;
            capturedNormal+=texture(uImpostorNormal,address)*weight;
        }
        if(material.a<0.5) discard;
        albedo=material.rgb/material.a;
        N=capturedNormal.rgb/max(capturedNormal.a,0.00001)*2.0-1.0;
    }
    else if(uKind==2)
    {
        vec2 cell=position.xz;
        vec2 width=max(fwidth(cell),vec2(0.0001));
        vec2 line=abs(fract(cell-0.5)-0.5)/width;
        float grid=1.0-min(min(line.x,line.y),1.0);
        float fade=1.0-smoothstep(uGroundExtent*0.45,uGroundExtent*0.95,length(cell));
        float contact=1.0-0.28*exp(-dot(cell,cell)*0.6);
        albedo=mix(vec3(0.075,0.087,0.10),vec3(0.115,0.13,0.145),grid*fade)*contact;
    }
    else
    {
        // Both the generated UVs and uploaded pixels use a top-row-first
        // convention. Sampling directly therefore agrees with the glTF bake.
        vec4 surface=texture(uSurface,uv);
        if(uKind==1 && surface.a<0.5) discard;
        albedo=max(color.rgb*surface.rgb,vec3(0.0));
    }
    N=dot(N,N)>0.00001?normalize(N):vec3(0.0,1.0,0.0);
    outNormal=vec4(N*0.5+0.5,1.0);
    if(uCapture==1||uCapture==3)
    {
        // Material bake: no fixed sunlight/tonemapping in the exported albedo.
        outColor=vec4(uCapture==3?albedo:encodeSRGB(albedo),1.0);
        return;
    }
    vec3 V=normalize(uCamera-position);
    vec3 L=normalize(uSun);
    vec3 H=normalize(L+V);
    float NoL=max(dot(N,L),0.0);
    float NoV=max(dot(N,V),0.001);
    float NoH=max(dot(N,H),0.0);
    float VoH=max(dot(V,H),0.0);
    float roughness=uKind==1?0.82:(uKind==3?0.86:0.91);
    float alpha=roughness*roughness;
    float a2=alpha*alpha;
    float denom=NoH*NoH*(a2-1.0)+1.0;
    float D=a2/max(3.14159265*denom*denom,0.0001);
    float k=(roughness+1.0)*(roughness+1.0)*0.125;
    float Gv=NoV/(NoV*(1.0-k)+k);
    float Gl=NoL/(NoL*(1.0-k)+k);
    vec3 F=vec3(0.04)+vec3(0.96)*pow(1.0-VoH,5.0);
    vec3 specular=D*Gv*Gl*F/max(4.0*NoV*max(NoL,0.001),0.0001);
    float sky=0.5+0.5*clamp(N.y,-1.0,1.0);
    vec3 ambient=mix(vec3(0.21,0.20,0.18),vec3(0.52,0.61,0.70),sky);
    vec3 result=albedo*ambient+(albedo*(vec3(1.0)-F)/3.14159265+specular)
        *NoL*vec3(2.5,2.36,2.15);
    if(uKind==1)
    {
        // Bounded thin-leaf transmission; authored crown normals stay intact
        // on backfaces instead of flipping whole foliage volumes per triangle.
        float transmission=pow(max(dot(-V,L),0.0),3.0);
        result+=albedo*vec3(0.55,0.62,0.28)*transmission*0.55;
    }
    vec3 mapped=result/(vec3(1.0)+result);
    outColor=vec4(encodeSRGB(mapped),1.0);
}
