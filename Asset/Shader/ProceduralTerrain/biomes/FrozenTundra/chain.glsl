#ifndef ENDURANCE_FROZEN_CHAIN_GLSL
#define ENDURANCE_FROZEN_CHAIN_GLSL

// Nominal footprints/gaps on BOTH axes of the placement domain, in metres.
// Lower buttresses join across these footprints to form passes. The continuous
// domain deformation changes world-space contours, not the nominal intervals.
const float FROZEN_WIDTH_MIN = 1000.0;
const float FROZEN_WIDTH_MAX = 6000.0;
const float FROZEN_GAP_MIN = -500.0;
const float FROZEN_GAP_MAX = 250.0;
const float FROZEN_HEIGHT_MIN = 1400.0;
const float FROZEN_HEIGHT_MAX = 3200.0;

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
    vec2 q = vec2(dot(p, axis), dot(p, vec2(-axis.y, axis.x)));
    // Composed shears bend the placement in both directions without folding
    // or changing its area. All sites share the same continuous deformation.
    q.x += (landPlacementValue(vec2(q.y / 9000.0, 17.3), 229u) - 0.5) * 2400.0
         + (landPlacementValue(vec2(q.y / 3200.0, 31.7), 230u) - 0.5) * 600.0;
    q.y += (landPlacementValue(vec2(q.x / 11000.0, 47.1), 233u) - 0.5) * 2400.0
         + (landPlacementValue(vec2(q.x / 3700.0, 61.9), 234u) - 0.5) * 600.0;
    return q;
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

float frozenMountainSite(vec2 q, vec2 widths, ivec2 site)
{
    vec2 v = q / (0.5 * widths);
    float radius = length(v);
    if (radius >= 2.0) return 0.0;
    float variant = frozenRandom(site.x, site.y, 240u);
    float angle = frozenRandom(site.x, site.y, 241u) * 6.2831853;
    vec2 direction = vec2(cos(angle), sin(angle));
    vec2 turned = vec2(dot(v, direction), dot(v, vec2(-direction.y, direction.x)));
    float width = sqrt(widths.x * widths.y);
    // Retain the accepted crest, cirque and subsidiary ridges. Each footprint
    // has its own direction; X/Z sizes vary independently from 1 to 6 km.
    float summit = frozenChainMassif(turned * (0.5 * width), width, variant);

    // Broad descending buttresses overlap in BOTH directions and meet in low
    // passes. No constant floor: their height follows distance to each summit
    // and continuous ridges; the main summit is never raised by addition.
    float ribs = landRidges(turned * 1.7 + variant * vec2(53.1, 17.9), 235u);
    float foothill = frozenSummitHeight(width, variant) * 0.48
        * pow(max(1.0 - radius * 0.5, 0.0), 1.35) * mix(0.68, 1.0, ribs);
    return max(summit, foothill);
}

float frozenChainHeight(vec2 p)
{
    vec2 domain = frozenChainDomain(p);
    vec2 phase = vec2(frozenRowPhase(0), frozenRowPhase(1));
    ivec2 middleBlock = ivec2(floor((domain - phase) / frozenBlockLength()));
    float result = 0.0;
    // Independent variable intervals fill the entire plane, not spaced rows
    // of mountains. Support <=6000 m per axis: adjacent 13500 m blocks suffice.
    for (int zBlockOffset = -1; zBlockOffset <= 1; ++zBlockOffset)
    {
        int zBlock = middleBlock.y + zBlockOffset;
        float zStart = float(zBlock) * frozenBlockLength() + phase.y;
        for (int j = 0; j < 4; ++j)
        {
            vec2 zd = frozenDimensions(1, zBlock, j);
            float dz = domain.y - (zStart + zd.x * 0.5);
            zStart += zd.x + zd.y;
            if (abs(dz) >= zd.x) continue;
            for (int xBlockOffset = -1; xBlockOffset <= 1; ++xBlockOffset)
            {
                int xBlock = middleBlock.x + xBlockOffset;
                float xStart = float(xBlock) * frozenBlockLength() + phase.x;
                for (int i = 0; i < 4; ++i)
                {
                    vec2 xd = frozenDimensions(0, xBlock, i);
                    float dx = domain.x - (xStart + xd.x * 0.5);
                    xStart += xd.x + xd.y;
                    if (abs(dx) >= xd.x) continue;
                    result = max(result, frozenMountainSite(vec2(dx, dz), vec2(xd.x, zd.x),
                        ivec2(xBlock * 4 + i, zBlock * 4 + j)));
                }
            }
        }
    }
    return result;
}
#endif
