#ifndef ENDURANCE_FROZEN_CHAIN_GLSL
#define ENDURANCE_FROZEN_CHAIN_GLSL

// Longitudinal footprints/gaps in the rotated chain domain, in metres.
const float FROZEN_WIDTH_MIN = 1000.0;
const float FROZEN_WIDTH_MAX = 6000.0;
const float FROZEN_GAP_MIN = -500.0;
const float FROZEN_GAP_MAX = 250.0;
const float FROZEN_HEIGHT_MIN = 1400.0;
const float FROZEN_HEIGHT_MAX = 3200.0;
const float FROZEN_CHAIN_SEPARATION = 12000.0;

float frozenRandom(int row, int block, uint salt)
{
    return noiseInternalUintTo01(noiseInternalHash(uint(row) * 0x9e3779b9u
        ^ uint(block) * 0x85ebca6bu ^ uSeed ^ salt));
}

float frozenBlockLength()
{
    // Fixed sums allow direct block addressing without accumulated prefix walks.
    return 2.0 * (FROZEN_WIDTH_MIN + FROZEN_WIDTH_MAX
        + FROZEN_GAP_MIN + FROZEN_GAP_MAX);
}

vec2 frozenDimensions(int row, int block, int index)
{
    int k = (index + int(frozenRandom(row, block, 221u) * 4.0)) % 4;
    float width = mix(FROZEN_WIDTH_MIN, FROZEN_WIDTH_MAX,
        frozenRandom(row, block, 222u + uint(k % 2)));
    float gap = mix(FROZEN_GAP_MIN, FROZEN_GAP_MAX,
        frozenRandom(row, block, 224u + uint(k % 2)));
    if (k >= 2)
    {
        width = FROZEN_WIDTH_MIN + FROZEN_WIDTH_MAX - width;
        gap = FROZEN_GAP_MIN + FROZEN_GAP_MAX - gap;
    }
    return vec2(width, gap);
}

float frozenRowPhase(int row)
{
    return frozenRandom(row, 0, 226u) * frozenBlockLength();
}

vec2 frozenChainDomain(vec2 p)
{
    vec2 axis = landSparseDirection(227u);
    return vec2(dot(p, axis), dot(p, vec2(-axis.y, axis.x)));
}

float frozenChainCenter(float along, int row)
{
    float phase = frozenRandom(row, 0, 228u) * 97.0;
    float broad = landPlacementValue(vec2(along / 18000.0, phase), 229u);
    float detail = landPlacementValue(vec2(along / 6500.0, phase), 230u);
    return float(row) * FROZEN_CHAIN_SEPARATION
        + (broad - 0.5) * 3600.0 + (detail - 0.5) * 650.0;
}

float frozenSummitHeight(float width, float variant)
{
    float size = (width - FROZEN_WIDTH_MIN) / (FROZEN_WIDTH_MAX - FROZEN_WIDTH_MIN);
    return mix(FROZEN_HEIGHT_MIN, FROZEN_HEIGHT_MAX,
        clamp(0.72 * size + 0.28 * variant, 0.0, 1.0));
}

float frozenChainMassif(vec2 q, float width, float variant)
{
    float radius = width * 0.5;
    float x = q.x / radius;
    if (abs(x) >= 1.0 || abs(q.y) >= radius * 1.3) return 0.0;
    float phase = variant * 6.2831853;
    float crest = (sin(x * 2.8 + phase) - sin(phase)) * 0.10 + x * 0.07;
    float y = q.y / radius - crest;
    // Buttresses share a principal crest; no circular peak masks.
    float shoulders = landPlacementValue(vec2(x * 3.2 + variant * 19.0, variant * 41.0), 231u);
    float crossWidth = mix(0.58, 0.98, shoulders)
        * sqrt(max(1.0 - x * x, 0.0001));
    float across = abs(y) / crossWidth;
    float longitudinal = pow(max(1.0 - x * x, 0.0), 0.85);
    float ridge = pow(max(1.0 - across, 0.0), mix(1.05, 1.65, variant));
    float ribs = landRidges(vec2(q.x / 550.0, y * radius / 1100.0)
        + variant * vec2(31.7, 12.9), 232u);
    float detail = mix(1.0, 0.76 + ribs * 0.24,
        smoothstep(0.0, 0.28, length(vec2(x, y))));
    vec2 cirque = (vec2(x, y) - vec2(mix(-0.3, 0.3, variant), 0.40)) / vec2(0.34, 0.24);
    float hollow = exp(-dot(cirque, cirque) * 2.0) * smoothstep(0.12, 0.32, y);
    float profile = longitudinal * ridge * (1.0 - hollow * 0.32);
    // Two oblique subsidiary ridges descend from the main crest. They form
    // buttresses and drainage valleys rather than isolated round satellite peaks.
    for (int branch = 0; branch < 2; ++branch)
    {
        float side = branch == 0 ? 1.0 : -1.0;
        float rootX = branch == 0 ? mix(-0.42, -0.22, variant) : mix(0.22, 0.42, variant);
        vec2 root = vec2(rootX, 0.0);
        vec2 tip = vec2(rootX + side * mix(0.20, 0.38, variant), side * mix(0.60, 0.87, variant));
        vec2 segment = tip - root;
        float t = clamp(dot(vec2(x, y) - root, segment) / dot(segment, segment), 0.0, 1.0);
        float distance = length(vec2(x, y) - mix(root, tip, t));
        float lateral = max(1.0 - distance / mix(0.30, 0.11, t), 0.0);
        float spur = 0.72 * pow(1.0 - t, 0.95) * lateral * lateral;
        // Zero at the longitudinal footprint ends even for wide spur roots.
        profile = max(profile, spur * longitudinal);
    }
    return frozenSummitHeight(width, variant) * profile * detail;
}

float frozenChainHeight(vec2 p)
{
    vec2 domain = frozenChainDomain(p);
    int middleRow = int(floor(domain.y / FROZEN_CHAIN_SEPARATION + 0.5));
    float result = 0.0;
    // Support <=3900m plus <=2125m bend: adjacent rows suffice.
    for (int rowOffset = -1; rowOffset <= 1; ++rowOffset)
    {
        int row = middleRow + rowOffset;
        float across = domain.y - frozenChainCenter(domain.x, row);
        if (abs(across) >= FROZEN_WIDTH_MAX * 0.65) continue;
        float phase = frozenRowPhase(row);
        int middleBlock = int(floor((domain.x - phase) / frozenBlockLength()));
        // Neighbour blocks keep overlaps/saddles continuous across boundaries.
        for (int blockOffset = -1; blockOffset <= 1; ++blockOffset)
        {
            int block = middleBlock + blockOffset;
            float start = float(block) * frozenBlockLength() + phase;
            for (int i = 0; i < 4; ++i)
            {
                vec2 dimensions = frozenDimensions(row, block, i);
                float width = dimensions.x;
                float center = start + width * 0.5;
                float variant = frozenRandom(row, block, 240u + uint(i));
                result = max(result, frozenChainMassif(vec2(domain.x - center, across), width, variant));
                int nextBlock = i == 3 ? block + 1 : block;
                int nextIndex = (i + 1) % 4;
                float nextWidth = frozenDimensions(row, nextBlock, nextIndex).x;
                float nextCenter = start + width + dimensions.y + nextWidth * 0.5;
                float t = (domain.x - center) / (nextCenter - center);
                if (t > 0.0 && t < 1.0)
                {
                    float nextVariant = frozenRandom(row, nextBlock, 240u + uint(nextIndex));
                    float passWidth = min(width, nextWidth) * 0.32;
                    float passAcross = max(1.0 - abs(across) / passWidth, 0.0);
                    float saddle = min(frozenSummitHeight(width, variant), frozenSummitHeight(nextWidth, nextVariant));
                    saddle *= mix(0.34, 0.14, (dimensions.y - FROZEN_GAP_MIN) / (FROZEN_GAP_MAX - FROZEN_GAP_MIN));
                    result = max(result, saddle * pow(sin(t * 3.14159265), 0.70) * passAcross * passAcross);
                }
                start += width + dimensions.y;
            }
        }
    }
    return result;
}
#endif
