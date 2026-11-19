#ifndef ENDURANCE_STEPPE_CHAIN_GLSL
#define ENDURANCE_STEPPE_CHAIN_GLSL

// Requested width bounds; the spacing/jitter/bend below satisfy them jointly.
// Changing these bounds alone does not resize the neighbor-built footprints.
const float STEPPE_WIDTH_MIN = 1000.0;
const float STEPPE_WIDTH_MAX = 6000.0;
const float STEPPE_GAP_MIN = -500.0;
const float STEPPE_GAP_MAX = 250.0;
const float STEPPE_HEIGHT_MIN = 1400.0;
const float STEPPE_HEIGHT_MAX = 3200.0;

// Lookup anchors only: each site gets independent 2D disk jitter. No shared
// X/Z intervals, repeated row dimensions, or anisotropic world-axis scaling.
const float STEPPE_SITE_SPACING = 2750.0;
const float STEPPE_SITE_JITTER = 620.0;
const float STEPPE_FACE_BEND = 120.0;
const float STEPPE_COL_REACH = 240.0;
const int STEPPE_SITE_SEARCH_RADIUS = 2;
const int STEPPE_FACE_SEARCH_RADIUS = 3;

uint steppeSiteHash(ivec2 site, uint salt)
{
    return noiseInternalHash(uint(site.x) * 0x9e3779b9u
        ^ uint(site.y) * 0x85ebca6bu ^ uSeed ^ salt);
}

float steppeRandom(int x, int z, uint salt)
{
    return noiseInternalUintTo01(steppeSiteHash(ivec2(x, z), salt));
}

vec2 steppeSiteCenter(ivec2 site)
{
    vec2 axis = landSparseDirection(227u);
    vec2 lattice = vec2(float(site.x) + 0.5 * float(site.y),
        0.8660254038 * float(site.y)) * STEPPE_SITE_SPACING;
    float angle = steppeRandom(site.x, site.y, 221u) * 6.2831853072;
    float radius = sqrt(steppeRandom(site.x, site.y, 222u)) * STEPPE_SITE_JITTER;
    return axis * lattice.x + vec2(-axis.y, axis.x) * lattice.y
        + radius * vec2(cos(angle), sin(angle));
}

ivec2 steppeNearestSite(vec2 p)
{
    vec2 axis = landSparseDirection(227u);
    vec2 q = vec2(dot(p, axis), dot(p, vec2(-axis.y, axis.x))) / STEPPE_SITE_SPACING;
    vec2 axial = vec2(q.x - q.y / 1.7320508076, q.y / 0.8660254038);
    vec3 cube = vec3(axial, -axial.x - axial.y);
    vec3 rounded = round(cube);
    vec3 error = abs(rounded - cube);
    if (error.x > error.y && error.x > error.z) rounded.x = -rounded.y - rounded.z;
    else if (error.y > error.z) rounded.y = -rounded.x - rounded.z;
    return ivec2(rounded.xy);
}

bool steppeSiteBefore(ivec2 a, ivec2 b)
{
    return a.x < b.x || (a.x == b.x && a.y < b.y);
}

uint steppePairHash(ivec2 a, ivec2 b)
{
    bool ordered = steppeSiteBefore(a, b);
    ivec2 first = ordered ? a : b;
    ivec2 second = ordered ? b : a;
    return noiseInternalHash(steppeSiteHash(first, 223u)
        ^ (steppeSiteHash(second, 224u) * 0x27d4eb2du));
}

float steppePairGap(ivec2 a, ivec2 b)
{
    float u = noiseInternalUintTo01(steppePairHash(a, b));
    // Same range, biased toward overlap: about 82% negative, mean -250 m.
    return mix(STEPPE_GAP_MIN, STEPPE_GAP_MAX, u * u);
}

float steppeFaceWave(float tangent, uint pair)
{
    float phase = noiseInternalUintTo01(noiseInternalHash(pair ^ 225u)) * 6.2831853072;
    float phase2 = noiseInternalUintTo01(noiseInternalHash(pair ^ 226u)) * 6.2831853072;
    return STEPPE_FACE_BEND * (0.65 * sin(tangent / 950.0 + phase)
        + 0.35 * sin(tangent / 430.0 + phase2));
}

// q is relative to owner; delta points from owner to neighbor. Positive outside.
// Both owners use the SAME curved boundary and gap. At a fixed tangential
// coordinate the two main-footprint boundaries are exactly gap metres apart.
float steppeRelativePlane(vec2 q, vec2 delta, ivec2 owner, ivec2 neighbor)
{
    float distance = length(delta);
    vec2 normal = delta / distance;
    float sign = steppeSiteBefore(owner, neighbor) ? 1.0 : -1.0;
    vec2 tangent = sign * vec2(-normal.y, normal.x);
    uint pair = steppePairHash(owner, neighbor);
    float u = noiseInternalUintTo01(pair);
    float gap = mix(STEPPE_GAP_MIN, STEPPE_GAP_MAX, u * u);
    // dot(delta,tangent)=0. Relative coordinates avoid large midpoint loss.
    float wave = steppeFaceWave(dot(q, tangent), pair);
    return dot(q, normal) - distance * 0.5 + gap * 0.5 - sign * wave;
}

float steppePairPlane(vec2 p, ivec2 owner, ivec2 neighbor)
{
    vec2 center = steppeSiteCenter(owner);
    return steppeRelativePlane(p - center, steppeSiteCenter(neighbor) - center, owner, neighbor);
}

vec2 steppeFootprint(vec2 p, ivec2 site)
{
    vec2 center = steppeSiteCenter(site);
    vec2 q = p - center;
    float margin = 1e20;
    float innerRadius = 1e20;
    for (int z = -STEPPE_FACE_SEARCH_RADIUS; z <= STEPPE_FACE_SEARCH_RADIUS; ++z)
    for (int x = -STEPPE_FACE_SEARCH_RADIUS; x <= STEPPE_FACE_SEARCH_RADIUS; ++x)
    {
        if (abs(x + z) > STEPPE_FACE_SEARCH_RADIUS || (x == 0 && z == 0)) continue;
        ivec2 neighbor = site + ivec2(x, z);
        vec2 delta = steppeSiteCenter(neighbor) - center;
        float distance = length(delta);
        float lowerAtCenter = distance * 0.5 - STEPPE_GAP_MAX * 0.5 - STEPPE_FACE_BEND;
        float lowerAtPoint = lowerAtCenter - dot(q, delta) / distance;
        // Conservative pruning: skip pair hashes/waves only if this face
        // cannot change either minimum, even at maximum gap and bend.
        if (lowerAtPoint >= margin && lowerAtCenter >= innerRadius) continue;
        margin = min(margin, -steppeRelativePlane(q, delta, site, neighbor));
        innerRadius = min(innerRadius, -steppeRelativePlane(vec2(0.0), delta, site, neighbor));
    }
    return vec2(margin, innerRadius);
}

vec2 steppeRidgeSegment(vec2 p, vec2 a, vec2 b)
{
    vec2 ab = b - a;
    float t = clamp(dot(p - a, ab) / max(dot(ab, ab), 0.0001), 0.0, 1.0);
    return vec2(length(p - mix(a, b, t)), t);
}

float steppeCrestGraph(vec2 q, float scale, ivec2 site)
{
    float phase = steppeRandom(site.x, site.y, 240u) * 6.2831853072;
    vec2 direction = vec2(cos(phase), sin(phase));
    // Rotate in physical space, then apply ONE isotropic scale.
    vec2 v = vec2(dot(q, direction), dot(q, vec2(-direction.y, direction.x))) / scale;
    vec2 offset = vec2(steppeRandom(site.x, site.y, 241u),
        steppeRandom(site.x, site.y, 242u)) * 37.0;
    v += 0.12 * vec2(noiseSimplex2D(v * 1.3 + offset, uSeed ^ 243u),
        noiseSimplex2D(v * 1.3 + offset, uSeed ^ 244u));
    vec2 root = (vec2(steppeRandom(site.x, site.y, 245u),
        steppeRandom(site.x, site.y, 246u)) - 0.5) * 0.35;
    float crest = pow(max(1.0 - length(v - root) / 0.90, 0.0), 1.3);
    int arms = 2 + min(3, int(steppeRandom(site.x, site.y, 247u) * 4.0));
    for (int arm = 0; arm < arms; ++arm)
    {
        uint salt = 250u + uint(arm) * 7u;
        float angle = (float(arm) + steppeRandom(site.x, site.y, salt) * 0.8)
            * 6.2831853072 / float(arms);
        float reach = mix(0.65, 1.75, steppeRandom(site.x, site.y, salt + 1u));
        float bend = mix(-0.85, 0.85, steppeRandom(site.x, site.y, salt + 2u));
        vec2 elbow = root + reach * 0.48 * vec2(cos(angle + bend), sin(angle + bend));
        vec2 tip = root + reach * vec2(cos(angle), sin(angle));
        vec2 first = steppeRidgeSegment(v, root, elbow);
        vec2 second = steppeRidgeSegment(v, elbow, tip);
        bool useFirst = first.x < second.x;
        float t = useFirst ? first.y * 0.48 : 0.48 + second.y * 0.52;
        float distance = min(first.x, second.x);
        float width = mix(0.25, 0.62, steppeRandom(site.x, site.y, salt + 3u));
        width *= mix(1.0, 0.42, t);
        float rib = pow(max(1.0 - distance / width, 0.0), 1.15);
        // Independent subsidiary summit and a shallow saddle on each branch.
        float secondary = mix(0.0, 0.36, steppeRandom(site.x, site.y, salt + 4u));
        float along = clamp(1.0 - t * 0.88 + secondary * sin(t * 6.2831853072), 0.0, 1.0);
        crest = max(crest, rib * along);
    }
    return crest;
}

// Main footprint contains a disk >=510 m and fits within radius 2947.8 m:
// minimum site distance =2750-2*620=1510; covering radius =2750/sqrt(3)+620.
// This guarantees 1-6 km widths without a circular clipping silhouette.
// Col support extends 240 m beyond each face, fits within radius3427.8 m.
vec2 steppeMountainParts(vec2 p, ivec2 site)
{
    vec2 q = p - steppeSiteCenter(site);
    if (dot(q, q) >= 3500.0 * 3500.0) return vec2(0.0);
    vec2 footprint = steppeFootprint(p, site);
    float margin = footprint.x;
    if (margin <= -STEPPE_COL_REACH) return vec2(0.0);
    float variant = steppeRandom(site.x, site.y, 236u);
    float height = mix(STEPPE_HEIGHT_MIN, STEPPE_HEIGHT_MAX, sqrt(variant));
    float ribs = landRidges(q / 620.0 + variant * vec2(41.7, 23.1), 237u);
    // Descend across the ENTIRE footprint. A short edge-only ramp would leave
    // flat-topped cell shoulders and expose polygon rims between neighbors.
    float flank = clamp((margin + STEPPE_COL_REACH)
        / (footprint.y + STEPPE_COL_REACH), 0.0, 1.0);
    float foothill = height * 0.60 * pow(flank, 0.90) * mix(0.78, 1.0, ribs);
    if (margin <= 0.0) return vec2(0.0, foothill);
    float crestScale = footprint.y * mix(0.85, 1.30, steppeRandom(site.x, site.y, 238u));
    float crest = steppeCrestGraph(q, crestScale, site);
    float envelope = pow(clamp(margin / footprint.y, 0.0, 1.0), 0.65);
    float main = height * envelope * mix(0.20, 1.0, crest) * mix(0.76, 1.0, ribs);
    return vec2(main, foothill);
}

float steppeMountainSite(vec2 p, ivec2 site)
{
    vec2 parts = steppeMountainParts(p, site);
    return max(parts.x, parts.y);
}

float steppeMountainPass(vec2 p, ivec2 a, ivec2 b)
{
    if (!steppeSiteBefore(a, b))
    {
        ivec2 swapSite = a;
        a = b;
        b = swapSite;
    }
    float gap = steppePairGap(a, b);
    float overlap = smoothstep(80.0, 450.0, -gap);
    if (overlap <= 0.0) return 0.0;
    vec2 ca = steppeSiteCenter(a), cb = steppeSiteCenter(b);
    vec2 delta = cb - ca;
    float distance = length(delta);
    if (distance > 4000.0) return 0.0;
    vec2 normal = delta / distance;
    vec2 tangent = vec2(-normal.y, normal.x);
    vec2 q = p - ca;
    float t = dot(q, normal) / distance;
    if (t <= 0.0 || t >= 1.0) return 0.0;
    float arch = sin(t * 3.1415926536);
    float phase = noiseInternalUintTo01(noiseInternalHash(steppePairHash(a, b) ^ 239u));
    float bend = (phase - 0.5) * 600.0 * arch;
    float across = abs(dot(q, tangent) - bend);
    float width = min(distance * 0.32, 900.0) * mix(0.5, 1.0, arch);
    if (across >= width) return 0.0;
    float ha = mix(STEPPE_HEIGHT_MIN, STEPPE_HEIGHT_MAX, sqrt(steppeRandom(a.x, a.y, 236u)));
    float hb = mix(STEPPE_HEIGHT_MIN, STEPPE_HEIGHT_MAX, sqrt(steppeRandom(b.x, b.y, 236u)));
    float saddle = min(ha, hb) * mix(0.30, 0.62, overlap) * overlap;
    float ribs = landRidges(p / 650.0, 239u);
    return saddle * pow(arch, 0.70) * pow(1.0 - across / width, 1.40)
        * mix(0.85, 1.0, ribs);
}

float steppeChainHeight(vec2 p)
{
    ivec2 middle = steppeNearestSite(p);
    float height = 0.0;
    // Hex ring2: 19 candidates. Face ring3: 36 neighbors. Passes stay within
    // sqrt(4000^2+1200^2)<4177 m of either endpoint, also within query ring2.
    // Expanded-ring tests verify both bounds and shared/negative coordinates.
    for (int z = -STEPPE_SITE_SEARCH_RADIUS; z <= STEPPE_SITE_SEARCH_RADIUS; ++z)
    for (int x = -STEPPE_SITE_SEARCH_RADIUS; x <= STEPPE_SITE_SEARCH_RADIUS; ++x)
    {
        if (abs(x + z) > STEPPE_SITE_SEARCH_RADIUS) continue;
        ivec2 site = middle + ivec2(x, z);
        height = max(height, steppeMountainSite(p, site));
        // Strong overlaps join upper flanks, not only their lowest skirts.
        // Variable curved passes break the closed valley around every site.
        // Three directions cover each nearest lattice pair exactly once.
        height = max(height, steppeMountainPass(p, site, site + ivec2(1, 0)));
        height = max(height, steppeMountainPass(p, site, site + ivec2(0, 1)));
        height = max(height, steppeMountainPass(p, site, site + ivec2(-1, 1)));
    }
    return height;
}
#endif
