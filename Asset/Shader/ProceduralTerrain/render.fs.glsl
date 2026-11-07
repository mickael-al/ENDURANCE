#version 460 core

layout(location = 0) in vec3 viewPosition;
layout(location = 1) in vec3 worldPosition;
layout(location = 2) in vec3 worldNormal;
layout(location = 3) in vec2 climate;
layout(location = 4) flat in uint chunkLod;

// The engine stores view-space positions but WORLD-space normals in its G-buffer.
layout(location = 0) out vec4 gPosition;
layout(location = 1) out vec3 gNormal;
layout(location = 2) out vec4 gColor;
layout(location = 3) out vec3 gOther;

uniform float microDetailStrength;
uniform float microDetailScale;
uniform uint worldSeed;
uniform bool debugLod;

uint hashCell(ivec3 cell)
{
    uvec3 bits = uvec3(cell);
    uint h = (bits.x * 0x8da6b343u) ^ (bits.y * 0xd8163841u)
           ^ (bits.z * 0xcb1ab31fu) ^ worldSeed;
    h ^= h >> 16u;
    h *= 0x7feb352du;
    h ^= h >> 15u;
    h *= 0x846ca68bu;
    return h ^ (h >> 16u);
}

float cornerValue(ivec3 cell)
{
    return float(hashCell(cell) >> 8u) * (1.0 / 16777215.0);
}

// Analytic gradient of one smooth 3D value-noise octave: eight scalar hashes,
// no density-buffer fetches, extra vertices, or finite-difference evaluations.
vec3 detailGradient(vec3 p)
{
    ivec3 cell = ivec3(floor(p));
    vec3 f = fract(p);
    vec3 blend = f * f * (3.0 - 2.0 * f);
    vec3 derivative = 6.0 * f * (1.0 - f);
    float a = cornerValue(cell);
    float b = cornerValue(cell + ivec3(1, 0, 0));
    float c = cornerValue(cell + ivec3(0, 1, 0));
    float d = cornerValue(cell + ivec3(1, 1, 0));
    float e = cornerValue(cell + ivec3(0, 0, 1));
    float f1 = cornerValue(cell + ivec3(1, 0, 1));
    float g = cornerValue(cell + ivec3(0, 1, 1));
    float h = cornerValue(cell + ivec3(1, 1, 1));
    float dx = mix(mix(b - a, d - c, blend.y),
                   mix(f1 - e, h - g, blend.y), blend.z);
    float dy = mix(mix(c - a, d - b, blend.x),
                   mix(g - e, h - f1, blend.x), blend.z);
    float dz = mix(mix(e - a, f1 - b, blend.x),
                   mix(g - c, h - d, blend.x), blend.y);
    return vec3(dx, dy, dz) * derivative;
}

vec3 biomeColor(vec3 normal)
{
    float humidity = clamp(climate.x, 0.0, 1.0);
    float temperature = clamp(climate.y, 0.0, 1.0);
    vec3 cold = mix(vec3(0.49, 0.49, 0.43), vec3(0.34, 0.42, 0.32), humidity);
    vec3 temperate = mix(vec3(0.59, 0.51, 0.31), vec3(0.25, 0.39, 0.20), humidity);
    vec3 hot = mix(vec3(0.75, 0.62, 0.40), vec3(0.21, 0.36, 0.16), humidity);
    vec3 ground = mix(mix(cold, temperate, smoothstep(0.12, 0.52, temperature)),
                      hot, smoothstep(0.55, 0.95, temperature));
    float slope = 1.0 - clamp(normal.y, 0.0, 1.0);
    ground = mix(ground, vec3(0.40, 0.39, 0.37), smoothstep(0.28, 0.65, slope));
    float snow = (1.0 - smoothstep(0.08, 0.25, temperature))
               * (1.0 - smoothstep(0.25, 0.70, slope));
    return mix(ground, vec3(0.86, 0.89, 0.91), snow);
}

void main()
{
    vec3 normal = normalize(worldNormal);
    vec3 p = worldPosition * max(microDetailScale, 0.0001);
    // Fade detail before it becomes subpixel; distant chunks do no noise work.
    float footprint = max(length(dFdx(p)), length(dFdy(p)));
    float detail = microDetailStrength * (1.0 - smoothstep(0.20, 0.80, footprint));
    vec3 color = biomeColor(normal);
    // Preserve defined float-to-int noise hashing even at extreme coordinates.
    if (detail > 0.0001 && all(lessThan(abs(p), vec3(1.0e9))))
    {
        vec3 gradient = detailGradient(p);
        gradient -= normal * dot(normal, gradient);
        normal = normalize(normal - detail * gradient);
    }
    if (debugLod)
    {
        const vec3 colors[7] = vec3[7](vec3(0.22, 0.75, 0.34), vec3(0.25, 0.67, 0.84),
            vec3(0.37, 0.40, 0.88), vec3(0.73, 0.34, 0.83), vec3(0.88, 0.38, 0.35),
            vec3(0.92, 0.68, 0.28), vec3(0.78, 0.82, 0.32));
        color = colors[chunkLod % 7u];
    }
    gPosition = vec4(viewPosition, 0.0);
    gNormal = normal;
    gColor = vec4(color, 1.0);
    gOther = vec3(0.0, 0.88, 1.0);
}
