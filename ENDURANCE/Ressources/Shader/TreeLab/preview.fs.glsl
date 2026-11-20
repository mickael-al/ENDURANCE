#version 460 core
// Alpha discard must happen before depth writes: deliberately no early_fragment_tests.
layout(binding=0) uniform sampler2D uSurface;
layout(binding=1) uniform sampler2D uLod3Upper;
uniform vec3 uCamera;
uniform vec3 uSun;
uniform int uKind; // 0 wood, 1 foliage, 2 preview-only floor, 3 cached billboard.
uniform int uCapture;
uniform float uLod3Blend;
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
    if(uKind==3)
    {
        // Cached pixels are linear, premultiplied coverage. Blending in this
        // representation avoids dark fringes and incorrect silhouette colors.
        vec4 sampleColor=mix(texture(uSurface,uv),texture(uLod3Upper,uv),uLod3Blend);
        if(sampleColor.a<0.5) discard;
        outColor=vec4(encodeSRGB(sampleColor.rgb/max(sampleColor.a,0.00001)),1.0);
        outNormal=vec4(0.5,0.5,1.0,1.0);
        return;
    }
    vec3 albedo;
    if(uKind==2)
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
    vec3 N=normalize(normal);
    outNormal=vec4(N*0.5+0.5,1.0);
    if(uCapture==1)
    {
        // Material bake: no fixed sunlight/tonemapping in the exported albedo.
        outColor=vec4(encodeSRGB(albedo),1.0);
        return;
    }
    vec3 V=normalize(uCamera-position);
    vec3 L=normalize(uSun);
    vec3 H=normalize(L+V);
    float NoL=max(dot(N,L),0.0);
    float NoV=max(dot(N,V),0.001);
    float NoH=max(dot(N,H),0.0);
    float VoH=max(dot(V,H),0.0);
    float roughness=uKind==1?0.82:0.91;
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
    // GPU-only LOD3 cache resolves/mips linear premultiplied coverage. Explicit
    // PNG captures use sRGB straight alpha, like the ordinary workshop preview.
    outColor=vec4(uCapture==3?mapped:encodeSRGB(mapped),1.0);
}
