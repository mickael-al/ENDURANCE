// Optional geometric VIEW-space normal in the RG32F gPosition.g metadata channel.
// >0 is emission, -2 is sky, 0 is a legacy/no-normal surface. Encoded normals
// occupy only integer tags [-1048579,-4]: -4 - oct20 (10 bits per oct axis).
// Every tag is exactly representable in float32; keep NEAREST G-buffer reads.
// Compute derivatives in the original primitive's fragment shader, before any
// discard/divergent branch. Deferred derivatives of positions can cross facets.
#ifndef GE_GBUFFER_GEOMETRY_GLSL
#define GE_GBUFFER_GEOMETRY_GLSL

vec2 geGBufferSignNotZero(vec2 value)
{
    return vec2(value.x<0.0?-1.0:1.0,value.y<0.0?-1.0:1.0);
}

float geGBufferGeometryTag(vec3 dx,vec3 dy)
{
    if(any(isnan(dx)) || any(isinf(dx)) || any(isnan(dy)) || any(isinf(dy))) return 0.0;
    // Independent max-component scales keep long/short derivatives valid and
    // avoid evaluating length() on values whose squares overflow/underflow.
    vec3 ax=abs(dx),ay=abs(dy);
    float sx=max(max(ax.x,ax.y),ax.z),sy=max(max(ay.x,ay.y),ay.z);
    if(!(sx>0.0) || !(sy>0.0)) return 0.0;
    vec3 normal=cross(dx/sx,dy/sy);
    float length2=dot(normal,normal);
    if(!(length2>1e-12)) return 0.0;
    normal*=inversesqrt(length2);
    vec2 oct=normal.xy/(abs(normal.x)+abs(normal.y)+abs(normal.z));
    if(normal.z<0.0) oct=(vec2(1.0)-abs(oct.yx))*geGBufferSignNotZero(oct);
    uvec2 encoded=uvec2(floor(clamp(oct*0.5+0.5,0.0,1.0)*1023.0+0.5));
    uint octBits=encoded.x|(encoded.y<<10u);
    return -4.0-float(octBits);
}

bool geGBufferGeometricNormal(float tag,out vec3 viewNormal)
{
    viewNormal=vec3(0.0);
    // Validate before conversion to uint. Other pipelines may still write
    // legacy emission/flags; malformed or interpolated tags must fall back.
    if(isnan(tag) || isinf(tag) || tag> -4.0 || tag< -1048579.0 || tag!=floor(tag)) return false;
    uint octBits=uint(-tag-4.0);
    vec2 oct=vec2(octBits&1023u,octBits>>10u)*(2.0/1023.0)-1.0;
    vec3 normal=vec3(oct,1.0-abs(oct.x)-abs(oct.y));
    if(normal.z<0.0) normal.xy=(vec2(1.0)-abs(normal.yx))*geGBufferSignNotZero(normal.xy);
    viewNormal=normalize(normal);
    return true;
}

#endif
