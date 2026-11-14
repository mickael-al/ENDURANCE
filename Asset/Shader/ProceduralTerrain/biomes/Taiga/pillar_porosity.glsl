// Calibrated, large-scale Perlin pores inside a normalized finite cylinder.
#ifndef ENDURANCE_TAIGA_PILLAR_POROSITY_GLSL
#define ENDURANCE_TAIGA_PILLAR_POROSITY_GLSL
#include "../../../Noise/noise_3d.glsl"

// Each threshold is the measured 70th percentile over the unit disk times
// [0,1]. Noise > threshold removes 30% of that volume. Eight independently
// calibrated Perlin fields avoid treating an arbitrary noise value as a
// probability. Periodic Y shifts and disk rotations preserve the distribution.
const uint TAIGA_PORE_SEEDS[8] = uint[8](
    0x51f15e5du, 0x68bc21ebu, 0x02e5be93u, 0x967a889bu,
    0x7f4a7c15u, 0x94d049bbu, 0x6c8e9cf5u, 0xb5297a4du);
const float TAIGA_PORE_THRESHOLDS[8] = float[8](
    0.155926198, 0.141526461, 0.169505566, 0.157774404,
    0.138628960, 0.197405145, 0.163719580, 0.171822593);

// Positive = stone, negative = pore. localNormalized.xz is local XZ/radius;
// localNormalized.y spans exactly [0,1] along the finite cylinder's axis.
// A dimension/tilt change is affine, so it does not change the void fraction.
float taigaPillarPoreField(vec3 localNormalized, uint hash)
{
    uint variant = hash & 7u;
    float angle = noiseInternalUintTo01(noiseInternalHash(hash ^ 0x93ad2167u))
        * 6.28318530718;
    float phase = noiseInternalUintTo01(noiseInternalHash(hash ^ 0x51f2e183u)) * 6.0;
    float c = cos(angle), s = sin(angle);
    vec2 disk = mat2(c, s, -s, c) * localNormalized.xz;
    // Horizontal wavelengths: radius/(4/3) = 75..150 m for 200..400-m
    // diameters. Along the axis there are six broad noise cells per cylinder.
    vec3 p = vec3(disk.x * (4.0 / 3.0) + 13.37,
        localNormalized.y * 6.0 + phase, disk.y * (4.0 / 3.0) + 29.91);
    return TAIGA_PORE_THRESHOLDS[variant]
        - noisePerlinPeriodic3D(p, TAIGA_PORE_SEEDS[variant], ivec3(64, 6, 64));
}
#endif
