#version 450 core

// Bounded precompiled variants remove absent expensive effects from the program.
in vec2 texCoord;
layout(location=0) out vec4 FragColor;
layout(binding=0) uniform sampler2D uColor;
#ifdef GE_ENV_HEAT
layout(binding=1) uniform sampler2D uDepth;
uniform vec4 uProjectionZ; // projection[2][2], [3][2], [2][3], [3][3]
#endif
#ifdef GE_ENV_FROST
layout(binding=2) uniform sampler2D uFrostPattern; // coverage, premultiplied growth arrival
#endif
uniform vec4 uStrengths; // heat, frost, wet, condensation
uniform vec2 uVeil; // dust, current lightning pulse (evaluated once on CPU)
uniform vec3 uTint;
uniform vec4 uClock; // seconds, pattern scale, speed, aspect ratio

float hash21(vec2 p)
{
    vec3 q = fract(vec3(p.xyx) * 0.1031);
    q += dot(q, q.yzx + 33.33);
    return fract((q.x + q.y) * q.z);
}

float noise21(vec2 p)
{
    vec2 c = floor(p), f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash21(c), hash21(c + vec2(1,0)), f.x),
               mix(hash21(c + vec2(0,1)), hash21(c + 1.0), f.x), f.y);
}

#ifdef GE_ENV_HEAT
float eyeDistance(float depth)
{
    float ndc = depth * 2.0 - 1.0;
    float denominator = ndc * uProjectionZ.z - uProjectionZ.x;
    return abs((uProjectionZ.y - ndc * uProjectionZ.w) /
        (abs(denominator) > 1e-8 ? denominator : 1e-8));
}
#endif

void main()
{
    vec2 size = vec2(textureSize(uColor, 0));
    vec2 pixel = 1.0 / size;
    vec2 uv = clamp(texCoord, pixel * 0.5, 1.0 - pixel * 0.5);
    vec2 p = (uv - 0.5) * vec2(uClock.w, 1.0);
    float t = uClock.x * uClock.z;
    vec2 displacement = vec2(0.0);

#ifdef GE_ENV_HEAT
    float depth = texture(uDepth, uv).r;
    float distance = eyeDistance(depth);
    // This is a spatial visibility test, not a feature toggle: broad sky/near
    // regions skip both procedural noises coherently across neighbouring pixels.
    // The cleared sky is exactly1.0. A tolerance would incorrectly discard
    // real terrain near the horizon with the engine's tens-of-kilometres range.
    if (depth < 1.0 && distance > 4.0)
    {
        // Two advected scales create rising thermal cells and smaller shimmers,
        // not a rigid translation or a color overlay. Both refract the scene UV.
        vec2 q = p * vec2(9.0, 22.0) * uClock.y + vec2(.14, -.84) * t;
        float broad = noise21(q);
        float fine = noise21(q * 2.17 + vec2(27.4, 8.2) + vec2(-.31, -.44) * t);
        vec2 heatFlow = vec2(broad + .32 * fine - .66,
            fine - .5 + .22 * sin(q.y * 1.8 + broad * 4.0));
        float distant = smoothstep(4.0, 45.0, distance);
        float nearGround = 1.0 - smoothstep(0.62, 0.94, uv.y);
        vec2 heatOffset = heatFlow * vec2(0.018 / uClock.w, 0.011) *
            uStrengths.x * distant * nearGround;
        vec2 candidate = clamp(uv + heatOffset, pixel * 0.5, 1.0 - pixel * 0.5);
        // Include the leading half-texel of the color filtering footprint. Testing
        // only its centre can still blend a near object's color into hot background
        // air when the depth texture uses nearest filtering.
        vec2 guardUV = clamp(candidate + sign(heatOffset) * pixel * 0.5,
            pixel * 0.5, 1.0 - pixel * 0.5);
        float sampleDepth = texture(uDepth, guardUV).r;
        float sampleDistance = eyeDistance(sampleDepth);
        // Reject distortion across foreground/background silhouettes. Do not pull
        // foreground objects into distant hot air or smear the sky into the ground.
        float relativeJump = abs(sampleDistance - distance) / max(distance, 1.0);
        float consistent = (1.0 - smoothstep(0.015, 0.08, relativeJump)) * float(sampleDepth < 1.0);
        displacement += heatOffset * consistent;
    }
#endif

    float frostCoverage = 0.0;
    float frostHighlight = 0.0;
#ifdef GE_ENV_FROST
    float edgeDistance = min(min(uv.x, 1.0 - uv.x), min(uv.y, 1.0 - uv.y));
    vec2 crystal = texture(uFrostPattern, uv).rg;
    float arrival = crystal.y / max(crystal.x, 0.002);
    float front = mix(.008, .40, sqrt(uStrengths.y));
    float vignetteGrowth = 1.0 - smoothstep(front - .025, front + .025, edgeDistance);
    // The fade is a growth front along each baked stem, rather than merely
    // changing a vignette's opacity. The crystal topology never moves with time.
    float grown = smoothstep(arrival - .025, arrival + .025, uStrengths.y * .95);
    float centerClear = 1.0 - smoothstep(.28, .43, edgeDistance);
    float crystalMask = crystal.x * grown * vignetteGrowth * centerClear;
    vec2 facet = clamp(vec2(dFdx(crystal.x), dFdy(crystal.x)) * 2.2, -.8, .8);
    displacement += facet * crystalMask * vec2(.002 / uClock.w, .002);
    frostCoverage = crystalMask * .82;
    frostHighlight = max(0.0, dot(facet, vec2(-.6, .8))) * crystalMask * .18;
    // A thin etched film links the dendrites without replacing them with a
    // white border. Intensity controls the film and successive inward growth.
    frostCoverage += .10 * uStrengths.y * vignetteGrowth * centerClear;
#endif

    float wetHighlight = 0.0;
#ifdef GE_ENV_WET
    // Sparse, independently delayed beads slide down the lens; no scene geometry
    // is needed, and their refraction never samples beyond the image boundary.
    vec2 grid = p * vec2(16.0, 10.0) * uClock.y;
    vec2 cell = floor(grid);
    float seed = hash21(cell);
    vec2 bead = fract(grid) - vec2(0.25 + seed * 0.5,
        fract(t * (0.11 + seed * 0.06) + seed * 9.0));
    bead.x *= 1.9;
    float droplet = (1.0 - smoothstep(0.065, 0.19, length(bead))) * step(0.54, seed);
    displacement += bead * droplet * vec2(0.011 / uClock.w, 0.008) * uStrengths.z;
    wetHighlight = droplet * uStrengths.z * max(0.0, -bead.y) * 0.35;
#endif

    vec2 sampleUV = clamp(uv + displacement, pixel * 0.5, 1.0 - pixel * 0.5);
    vec4 source = texture(uColor, sampleUV);
    vec3 color = source.rgb;

#ifdef GE_ENV_CONDENSATION
    float edge = smoothstep(0.12, 0.62, length(p / vec2(uClock.w, 1.0)));
    float film = uStrengths.w * edge * (0.65 + 0.35 * noise21(p * 9.0 * uClock.y + vec2(0.015, 0.0) * t));
    // Three taps total for this variant; share the main sample with the other
    // effects and keep the centre legible instead of blurring the whole image.
    vec2 blur = vec2(0.0022 / uClock.w, 0.0016) * film;
    vec3 blurred = (color + texture(uColor, clamp(sampleUV + blur, pixel * 0.5, 1.0 - pixel * 0.5)).rgb +
        texture(uColor, clamp(sampleUV - blur, pixel * 0.5, 1.0 - pixel * 0.5)).rgb) / 3.0;
    color = mix(color, mix(blurred, vec3(0.72,0.78,0.80), 0.14), film * 0.7);
#endif

    color = mix(color, vec3(0.72, 0.84, 0.91), clamp(frostCoverage,0.0,.92)) + wetHighlight + frostHighlight;
    // Cheap compositing remains shared. These are screen veils, not replacement
    // atmospheric scattering; the world and its lighting have already rendered.
    float edgeDust = 0.18 + 0.28 * dot(p / vec2(uClock.w, 1.0), p / vec2(uClock.w, 1.0));
    color = mix(color, uTint, clamp(uVeil.x * edgeDust, 0.0, 0.55));
    color = mix(color, vec3(0.88, 0.93, 1.0), clamp(uVeil.y * 0.72, 0.0, 0.72));
    FragColor = vec4(clamp(color, 0.0, 1.0), source.a);
}
