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
layout(binding=2) uniform sampler2D uFrostPattern; // signed offset to a baked Voronoi facet centre
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
        // Half the spatial frequency doubles each thermal cell's width/height.
        // Advection is halved too, preserving its screen-space travel speed.
        vec2 q = p * vec2(4.5, 11.0) * uClock.y + vec2(.07, -.42) * t;
        float broad = noise21(q);
        float fine = noise21(q * 2.17 + vec2(27.4, 8.2) + vec2(-.155, -.22) * t);
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

#ifdef GE_ENV_FROST
    // Crystallization groups the existing picture into irregular polygonal
    // facets, without frosted-glass dispersion or an added white layer.
    // Fixed coordinates intentionally ignore shared speed/scale parameters, so
    // entering another biome cannot slide an already formed sheet of ice.
    vec2 aspect = vec2(uClock.w, 1.0);
    // Reuse the 32-cell cache at twice its frequency: half-size facets, with
    // their displacement decoded at the same scale.
    vec2 patternUV = (uv - .5) * aspect * 2.0 + .5;
    vec2 facetOffset = (texture(uFrostPattern, patternUV).rg - .5) * (3.0 / 64.0) / aspect;
    // A radial mask in normalized screen coordinates forms an ellipse on a
    // widescreen viewport, instead of tracing its rectangular edges.
    float edgeDistance = .5 - length(uv - .5);
    float arrival = edgeDistance + length(facetOffset * aspect) * .35;
    // Four times the former .07-wide transition. Limit the advancing front to
    // keep the central view clear even at full strength, without a second mask
    // that would introduce a narrower transition inside this broad fade.
    float front = .005 + .22 * uStrengths.y;
    float frost = (1.0 - smoothstep(front - .18, front + .10, arrival)) *
        smoothstep(0.0, .06, uStrengths.y);
    // A broad coherent rejection avoids the extra scene read in the clear centre.
    // The mask tends continuously to zero at the rejection boundary.
    if (frost > 0.0)
    {
        vec2 facetUV = sampleUV + facetOffset * .96;
        // The postprocess color target has one level. Explicit LOD also avoids
        // implicit derivatives across the edge of this spatial rejection.
        vec3 crystal = textureLod(uColor, clamp(facetUV, pixel * .5, 1.0 - pixel * .5), 0.0).rgb;
        // Keep the original palette and luminance, including uniform sky.
        color = mix(color, crystal, frost * .97);
    }
#endif

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

    color += wetHighlight;
    // Cheap compositing remains shared. These are screen veils, not replacement
    // atmospheric scattering; the world and its lighting have already rendered.
    float edgeDust = 0.18 + 0.28 * dot(p / vec2(uClock.w, 1.0), p / vec2(uClock.w, 1.0));
    color = mix(color, uTint, clamp(uVeil.x * edgeDust, 0.0, 0.55));
    color = mix(color, vec3(0.88, 0.93, 1.0), clamp(uVeil.y * 0.72, 0.0, 0.72));
    FragColor = vec4(clamp(color, 0.0, 1.0), source.a);
}
