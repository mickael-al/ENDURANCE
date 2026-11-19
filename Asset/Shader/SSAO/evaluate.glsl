// Corrected classic 64-sample SSAO. The caller supplies VIEW-space position
// and normal; gPosition is RG32F (view Z + tag), texNoise is RG32F XY rotation,
// and ubssao.samples is a 16-byte-stride vec4 array. No extra render pass.
#ifndef GE_SSAO_EVALUATE_GLSL
#define GE_SSAO_EVALUATE_GLSL
const float GE_SSAO_RADIUS = 0.5;
const float GE_SSAO_BIAS = 0.025;
const int GE_SSAO_SAMPLES = 64;
#ifndef GE_SSAO_DISABLE_SUBPIXEL_SKIP
#define GE_SSAO_DISABLE_SUBPIXEL_SKIP 0
#endif

bool geSsaoGeometry(vec2 encoded)
{
    return encoded.x < 0.0 && encoded.y != -2.0
        && !any(isnan(encoded)) && !any(isinf(encoded));
}

bool geSsaoSphereInsidePixel(vec3 position, ivec2 size)
{
    // For N/W, a sphere offset d changes one projected coordinate by
    // (a*dx+(b-N/W*c)*dz)/(W+c*dz). Bound the numerator with the triangle
    // inequality and the denominator by its minimum over the entire sphere.
    // This is conservative for both Camera perspective and orthographic forms.
    float w = ubc.proj[2][3] * position.z + ubc.proj[3][3];
    float denominator = w - GE_SSAO_RADIUS * abs(ubc.proj[2][3]);
    if (!(denominator > 0.0)) return false;
    vec2 diagonal = vec2(ubc.proj[0][0], ubc.proj[1][1]);
    vec2 ndc = (diagonal * position.xy + ubc.proj[2].xy * position.z + ubc.proj[3].xy) / w;
    vec2 bound = 0.5 * vec2(size) * GE_SSAO_RADIUS *
        (abs(diagonal) + abs(ubc.proj[2].xy - ndc * ubc.proj[2][3])) / denominator;
    // Reserve float rounding in projection/reconstruction. All valid samples
    // then hit the receiver texel and are discarded by the loop below anyway.
    vec2 rounding = 8.0 * 1.1920928955078125e-7 * vec2(size);
    return all(lessThan(bound + rounding, vec2(0.5)));
}

float geSsaoEvaluate(vec3 position, vec3 viewNormal, ivec2 pixel)
{
    ivec2 size = textureSize(gPosition, 0);
    if (!(position.z < 0.0) || any(isnan(position)) || any(isinf(position))) return 1.0;
#if !GE_SSAO_DISABLE_SUBPIXEL_SKIP
    if (geSsaoSphereInsidePixel(position, size)) return 1.0;
#endif
    float normalLength2 = dot(viewNormal, viewNormal);
    if (!(normalLength2 > 1e-12) || any(isnan(viewNormal)) || any(isinf(viewNormal))) return 1.0;
    vec3 normal = viewNormal * inversesqrt(normalLength2);
    vec3 random = vec3(texelFetch(texNoise, pixel & ivec2(3), 0).xy, 0.0);
    vec3 tangent = random - normal * dot(random, normal);
    float tangentLength2 = dot(tangent, tangent);
    if (!(tangentLength2 > 1e-8))
    {
        vec3 axis = abs(normal.z) < 0.9 ? vec3(0, 0, 1) : vec3(0, 1, 0);
        tangent = cross(axis, normal);
        tangentLength2 = dot(tangent, tangent);
    }
    tangent *= inversesqrt(tangentLength2);
    mat3 basis = mat3(tangent, cross(normal, tangent), normal);
    vec2 diagonal = vec2(ubc.proj[0][0], ubc.proj[1][1]);
    float occlusion = 0.0;
    for (int i = 0; i < GE_SSAO_SAMPLES; ++i)
    {
        vec3 samplePosition = position + (basis * ubssao.samples[i].xyz) * GE_SSAO_RADIUS;
        // Camera projections have no XY cross terms. Only project XY/W;
        // computing and dividing the unused clip Z would be redundant.
        float w = ubc.proj[2][3] * samplePosition.z + ubc.proj[3][3];
        if (!(w > 0.0) || !(samplePosition.z < 0.0)) continue;
        vec2 uv = (diagonal * samplePosition.xy + ubc.proj[2].xy * samplePosition.z +
            ubc.proj[3].xy) * (0.5 / w) + 0.5;
        if (any(lessThan(uv, vec2(0))) || any(greaterThanEqual(uv, vec2(1)))) continue;
        ivec2 samplePixel = ivec2(uv * vec2(size));
        if (all(equal(samplePixel, pixel))) continue;
        vec2 neighbour = texelFetch(gPosition, samplePixel, 0).rg;
        if (!geSsaoGeometry(neighbour) || neighbour.x < samplePosition.z + GE_SSAO_BIAS) continue;
        float separation = abs(position.z - neighbour.x);
        // Finite support avoids foreground/background halos beyond the AO
        // radius; radius/separation never reaches zero at any distance.
        float rangeWeight = 1.0 - smoothstep(0.5 * GE_SSAO_RADIUS, GE_SSAO_RADIUS, separation);
        occlusion += rangeWeight;
    }
    return clamp(1.0 - occlusion / float(GE_SSAO_SAMPLES), 0.0, 1.0);
}

#endif
