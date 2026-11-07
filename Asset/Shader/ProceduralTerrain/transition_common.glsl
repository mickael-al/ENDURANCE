#ifndef ENDURANCE_PROCEDURAL_TRANSITION_COMMON
#define ENDURANCE_PROCEDURAL_TRANSITION_COMMON

// Transvoxel face frames and boundary deformation: see the algorithm and
// reference-implementation notices in ThirdParty/transvoxel/README.md.

// Face order: -X,+X,-Y,+Y,-Z,+Z. Each (u,v,inward) frame has positive
// determinant, so one pre-oriented transition table serves all six faces.
ivec3 transitionFacePoint(uint face, ivec3 p, uint cells)
{
    int n = int(cells);
    if (face == 0u) return ivec3(p.z, p.x, p.y);
    if (face == 1u) return ivec3(n-p.z, p.y, p.x);
    if (face == 2u) return ivec3(p.y, p.z, p.x);
    if (face == 3u) return ivec3(p.x, n-p.z, p.y);
    if (face == 4u) return p;
    return ivec3(p.y, p.x, n-p.z);
}

ivec3 transitionFacePoint(uint face, ivec2 p, uint cells)
{
    return transitionFacePoint(face, ivec3(p, 0), cells);
}

vec3 transitionFaceVector(uint face, vec3 v)
{
    if (face == 0u) return v.zxy;
    if (face == 1u) return vec3(-v.z, v.y, v.x);
    if (face == 2u) return v.yzx;
    if (face == 3u) return vec3(v.x, -v.z, v.y);
    if (face == 4u) return v;
    return vec3(v.y, v.x, -v.z);
}

uint transitionSampleIndex(uint endpoint)
{
    if (endpoint < 9u) return endpoint;
    uint corner = endpoint - 9u;
    return (corner & 1u)*2u + (corner >> 1u)*6u;
}

uint transitionCaseBit(uint sampleIndex)
{
    const uint bitIndex[9] = uint[9](0u, 1u, 2u, 7u, 8u, 3u, 6u, 5u, 4u);
    return bitIndex[sampleIndex];
}

// Make room inside the fine chunk for a real transition cell. The regular
// mesh and the full-resolution side of the transition use exactly this same
// transform. Its coarse side stays on the shared, unmodified face. All active
// faces participate together, including at edges and corners of the chunk.
vec3 transitionAdjustedPosition(vec3 p, vec3 normal, uint mask, uint cells)
{
    vec3 delta = vec3(0.0);
    float n = float(cells);
    for (uint axis = 0u; axis < 3u; ++axis)
    {
        // A vertex shared with a same-resolution neighbor must keep its
        // primary position. In particular, tangent projection must not move
        // it across that neighbor's independently packed chunk boundary.
        if (p[axis] <= 0.0 && (mask & (1u << (2u*axis))) == 0u) return p;
        if (p[axis] >= n && (mask & (2u << (2u*axis))) == 0u) return p;
        if ((mask & (1u << (2u*axis))) != 0u)
            delta[axis] += 0.25*max(1.0-p[axis], 0.0);
        if ((mask & (2u << (2u*axis))) != 0u)
            delta[axis] -= 0.25*max(p[axis]-(n-1.0), 0.0);
    }
    // Tangent projection preserves the surface to first order. The input
    // normal is normalized by the common edge interpolation routine.
    precise vec3 adjusted = p + delta - normal*dot(normal, delta);
    return adjusted;
}

#endif
