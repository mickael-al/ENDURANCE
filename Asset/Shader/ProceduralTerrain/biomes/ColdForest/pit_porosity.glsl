// One broad 3D Perlin sample. Positive field is surviving stone.
#ifndef ENDURANCE_COLD_FOREST_PIT_POROSITY_GLSL
#define ENDURANCE_COLD_FOREST_PIT_POROSITY_GLSL
#include "../../../Noise/noise_3d.glsl"
#include "pit_settings.glsl"

// Actual Perlin quantiles, not an affine remapping of its nonuniform output.
// 65 knots: p=.5*(k/32)^2, k=0..32, then their upper-tail counterparts.
// Extra tail resolution preserves the smooth radial gradient near 0/100%.
// noisePerlin3D is clamped to [-1,1]; endpoints guarantee all solid/all void.
// Reproduce with Tests/Terrain/calibrate_cold_forest_porosity.py.
const float COLD_FOREST_PORE_QUANTILES[65] = float[65](
    -1.000100000, -0.753724257, -0.683164019, -0.638682257, -0.603057685,
    -0.571899661, -0.543465288, -0.517170813, -0.493852780, -0.471805533,
    -0.449441464, -0.427086750, -0.404940242, -0.383183752, -0.361640905,
    -0.340934270, -0.321061242, -0.301790840, -0.282849682, -0.263949162,
    -0.245225861, -0.226263913, -0.206948267, -0.187383752, -0.167541248,
    -0.147504427, -0.127109189, -0.106463854, -0.085521932, -0.064261817,
    -0.043078721, -0.021484118, -0.000047519, 0.021239137, 0.042696015,
    0.063818336, 0.085165011, 0.106237437, 0.126847132, 0.147292043,
    0.167405789, 0.187246631, 0.206854081, 0.226243318, 0.245169986,
    0.264070120, 0.282834085, 0.301688409, 0.321024392, 0.340894871,
    0.361708306, 0.383249735, 0.405275092, 0.427468623, 0.449888020,
    0.472466521, 0.494530754, 0.517231017, 0.543508213, 0.572355212,
    0.603585200, 0.639375705, 0.684599702, 0.755322218, 1.000100000);

float coldForestPoreThreshold(float voidFraction)
{
    float f = clamp(voidFraction, 0.0, 1.0);
    float tail = min(f, 1.0-f);
    int k = min(int(floor(sqrt(tail * 2048.0))), 31);
    float fk = float(k);
    float t = clamp((tail * 2048.0-fk*fk)/(2.0*fk+1.0), 0.0, 1.0);
    if(f <= 0.5)
        return mix(COLD_FOREST_PORE_QUANTILES[k],
            COLD_FOREST_PORE_QUANTILES[k+1], t);
    return mix(COLD_FOREST_PORE_QUANTILES[64-k],
        COLD_FOREST_PORE_QUANTILES[63-k], t);
}

float coldForestPoreNoise(vec3 localPosition, uint key)
{
    // Independent phases keep the center/depth profile from locking onto the
    // integer lattice when every pit uses the same local coordinate frame.
    vec3 phase = vec3(
        noiseInternalUintTo01(noiseInternalHash(key ^ 0x93ad2167u)),
        noiseInternalUintTo01(noiseInternalHash(key ^ 0x51f2e183u)),
        noiseInternalUintTo01(noiseInternalHash(key ^ 0x68bc21ebu))) * 256.0;
    return noisePerlin3D(localPosition / COLD_FOREST_PORE_SCALE + phase,
        noiseInternalHash(key ^ 0x641ac379u));
}

float coldForestPoreField(vec3 localPosition, uint key, float voidFraction)
{
    // Ensemble proportion inside the geometric cone; a finite pit fluctuates.
    return coldForestPoreNoise(localPosition, key)
        - coldForestPoreThreshold(voidFraction);
}
#endif
