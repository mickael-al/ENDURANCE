#ifndef __ENGINE_UNIFORM_BUFFER_DIVER__
#define __ENGINE_UNIFORM_BUFFER_DIVER__

#include <cstddef>
#include <cstdint>

struct UniformBufferDiver
{
    int maxLight;
    float u_time;
    float gamma;
    float ambiant;
	float fov;
    // GLSL std430 bool occupies four bytes, not sizeof(C++ bool).
    std::uint32_t ortho;
};
static_assert(sizeof(UniformBufferDiver) == 24 && offsetof(UniformBufferDiver, ortho) == 20,
    "Per-frame payload must match GLSL std430 layout");

#endif //!__ENGINE_UNIFORM_BUFFER_DIVER__