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
// -1 follows the climate matrix; 0..25 isolates a biome for inspection.
uniform int terrainBiomeOverride = -1;
uniform float terrainSeaLevel = 0.0;
uniform float terrainSeaBlendWidth = 64.0;

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

// Provisional ground/rock colors, independent of the eventual terrain textures.
// Row-major: humidity increases left to right, temperature top to bottom.
// The final entry is the marine terrain; it does not represent a water surface.
const vec3 BIOME_GROUND[26] = vec3[26](
    vec3(.73,.83,.86), vec3(.53,.59,.57), vec3(.36,.42,.29), vec3(.19,.29,.23), vec3(.17,.25,.21),
    vec3(.39,.42,.39), vec3(.47,.43,.30), vec3(.23,.32,.23), vec3(.21,.30,.27), vec3(.22,.29,.24),
    vec3(.58,.49,.31), vec3(.39,.45,.23), vec3(.27,.37,.20), vec3(.20,.33,.22), vec3(.29,.35,.22),
    vec3(.55,.43,.28), vec3(.58,.50,.25), vec3(.43,.44,.22), vec3(.37,.39,.19), vec3(.18,.33,.18),
    vec3(.14,.15,.16), vec3(.63,.28,.16), vec3(.64,.53,.35), vec3(.26,.27,.16), vec3(.30,.34,.23),
    vec3(.29,.35,.34)
);
const vec3 BIOME_ROCK[26] = vec3[26](
    vec3(.46,.60,.66), vec3(.35,.41,.42), vec3(.30,.34,.31), vec3(.29,.34,.31), vec3(.23,.30,.29),
    vec3(.29,.32,.32), vec3(.34,.35,.31), vec3(.30,.34,.29), vec3(.29,.35,.36), vec3(.28,.33,.30),
    vec3(.42,.38,.29), vec3(.37,.39,.29), vec3(.33,.34,.27), vec3(.27,.34,.28), vec3(.31,.34,.28),
    vec3(.43,.31,.22), vec3(.43,.36,.24), vec3(.38,.36,.25), vec3(.39,.34,.24), vec3(.29,.35,.25),
    vec3(.10,.11,.13), vec3(.46,.20,.13), vec3(.50,.38,.25), vec3(.24,.22,.18), vec3(.28,.30,.24),
    vec3(.24,.30,.30)
);

vec3 biomeColor(vec3 normal)
{
    float slope = smoothstep(0.28, 0.65, 1.0 - clamp(normal.y, 0.0, 1.0));
    if (terrainBiomeOverride >= 0 && terrainBiomeOverride < 26)
    {
        return mix(BIOME_GROUND[terrainBiomeOverride], BIOME_ROCK[terrainBiomeOverride], slope);
    }

    vec2 cell = clamp(climate, 0.0, 1.0) * 4.0;
    uvec2 i = uvec2(floor(cell));
    uvec2 j = min(i + uvec2(1), uvec2(4));
    vec2 f = fract(cell);
    // Match the field's quintic interpolation, including zero derivatives at
    // matrix nodes; four palette corners only, no per-fragment biome noise.
    f = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
    uvec4 id = uvec4(i.x + i.y*5u, j.x + i.y*5u, i.x + j.y*5u, j.x + j.y*5u);
    vec3 ground = mix(mix(BIOME_GROUND[id.x], BIOME_GROUND[id.y], f.x),
                      mix(BIOME_GROUND[id.z], BIOME_GROUND[id.w], f.x), f.y);
    vec3 rock = mix(mix(BIOME_ROCK[id.x], BIOME_ROCK[id.y], f.x),
                    mix(BIOME_ROCK[id.z], BIOME_ROCK[id.w], f.x), f.y);
    // Provisional seabed tint follows the actual surface altitude. This cheap
    // visual blend is separate from the macro-height biome selection; it avoids
    // evaluating the continental noise again for every fragment.
    float coast = clamp((worldPosition.y-terrainSeaLevel)/max(terrainSeaBlendWidth,0.001)+0.5,0.0,1.0);
    coast = coast*coast*coast*(coast*(coast*6.0-15.0)+10.0);
    ground = mix(BIOME_GROUND[25],ground,coast);
    rock = mix(BIOME_ROCK[25],rock,coast);
    // Steep faces retain their biome's stone tint instead of becoming one gray.
    return mix(ground, rock, slope);
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
