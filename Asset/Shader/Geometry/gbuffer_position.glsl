// Compact G-buffer position contract: R = signed view-space Z in metres,
// G = emission/sky/geometric-normal tag. Both channels retain float32 precision.
// Pixel X/Y and the camera projection uniquely determine the other coordinates;
// no hardware-depth linearization or world-space half-float quantization is used.
#ifndef GE_GBUFFER_POSITION_GLSL
#define GE_GBUFFER_POSITION_GLSL

vec2 geGBufferPackPosition(vec3 viewPosition, float tag)
{
    return vec2(viewPosition.z, tag);
}

vec3 geGBufferViewPositionNDC(vec2 ndc, float viewZ, mat4 projection)
{
    // Camera::getProjectionMatrix uses GLM perspective or orthographic matrices.
    // This also supports off-centre frusta, reversed axes and projection jitter.
    // Solve clip.xy = ndc.xy * clip.w at known view Z, avoiding an inverse matrix
    // and the near/far precision loss of reconstructing Z from the depth buffer.
    float clipW = projection[2][3] * viewZ + projection[3][3];
    vec2 clipXY = ndc * clipW
        - projection[2].xy * viewZ - projection[3].xy;
    return vec3(clipXY / vec2(projection[0][0], projection[1][1]), viewZ);
}

vec3 geGBufferViewPosition(vec2 screenUV, float viewZ, mat4 projection)
{
    return geGBufferViewPositionNDC(screenUV * 2.0 - 1.0, viewZ, projection);
}

vec4 geGBufferReadPosition(sampler2D positions, ivec2 pixel, mat4 projection)
{
    ivec2 size = textureSize(positions, 0);
    // Match the attachment's GL_REPEAT reads for derivative helper invocations
    // outside an odd viewport. Wrap BOTH the depth sample and its pixel ray;
    // clamping just depth invents a different surface tangent at screen edges.
    if (any(lessThan(pixel, ivec2(0))) || any(greaterThanEqual(pixel, size)))
        // GLSL integer remainder with negative operands is not portable.
        pixel = ivec2(mod(vec2(pixel), vec2(size)));
    vec2 encoded = texelFetch(positions, pixel, 0).rg;
    // Reconstruct at the fetched pixel centre, never at unquantized UVs.
    // Centre in integer pixel space before dividing. UV*2-1 loses relative
    // precision near the screen centre, particularly in odd-sized viewports.
    vec2 ndc = vec2(pixel * 2 + 1 - size) / vec2(size);
    return vec4(geGBufferViewPositionNDC(ndc, encoded.r, projection), encoded.g);
}

#endif
