#version 450

#define GROUP_SIZE 256
#define MAX_DIM_GROUPS 256
#define MAX_DIM_THREADS (GROUP_SIZE * MAX_DIM_GROUPS)
#define MAX_DIM_THREADS_THREADS (MAX_DIM_THREADS * MAX_DIM_GROUPS)

layout(local_size_x = GROUP_SIZE, local_size_y = 1, local_size_z = 1) in;

layout(std430, binding = 0) buffer ParticleData
{
    float data[];
} particle;

float hash(uint x)
{
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return float(x) * (1.0 / 4294967295.0);
}

float rand(inout uint seed)
{
    seed += (seed << 10u);
    seed ^= (seed >> 6u);
    seed += (seed << 3u);
    seed ^= (seed >> 11u);
    seed += (seed << 15u);
    return hash(seed);
}


uniform uint u_offset;   // offset dans le SSBO
uniform uint u_offsetSettings;   // offset des particulesGroups
uniform uint u_count;    // nombre de particules

const uint FLAG_INSTANT_PARTICLE_INIT = 1u << 8;
const uint FLAG_BURST = 1u << 11;
const float BURST_VALUE = -30000;

void main()
{
    uint id = uint(gl_GlobalInvocationID.x + gl_GlobalInvocationID.y * MAX_DIM_THREADS + gl_GlobalInvocationID.z * MAX_DIM_THREADS_THREADS);

    if (id >= u_count)
    {
        return;
    }

    /*
        glm::vec3 position;
        uint16_t cr;
        uint16_t cg;
        uint16_t cb;
        uint16_t ca;
        float time;
        uint16_t scale;
        uint16_t vx;
        uint16_t vy;
        uint16_t vz;
    */


    uint flags = floatBitsToUint(particle.data[u_offsetSettings + 9]);

    uint bid = u_offset + id * 8u;
    particle.data[bid + 0] = 0.0f;
    particle.data[bid + 1] = 100000.0;
    particle.data[bid + 2] = 0.0;
    particle.data[bid + 3] = uintBitsToFloat(0x04000400u);
    particle.data[bid + 4] = uintBitsToFloat(0x04000400u);
    if ((flags & FLAG_INSTANT_PARTICLE_INIT) != 0u)
    {
        particle.data[bid + 5] = -1.0;//time
    }
    else
    {
        uint sid = u_offset + id * 8u;
        particle.data[bid + 5] = max(particle.data[u_offsetSettings], particle.data[u_offsetSettings + 1]) * rand(sid);//time
    }
    if ((flags & FLAG_BURST) != 0u)
    {
        particle.data[bid + 5] += BURST_VALUE;
    }
    uint scale16 = 0u;         // zero scale
    uint vm16 = 32768u;     // zero velocity

    uint packed1 = (scale16) | (vm16 << 16);
    uint packed2 = (vm16) | (vm16 << 16);

    particle.data[bid + 6] = uintBitsToFloat(packed1);
    particle.data[bid + 7] = uintBitsToFloat(packed2);
}
