#ifndef __ENGINE_UNIFORM_BUFFER_LIGHT__
#define __ENGINE_UNIFORM_BUFFER_LIGHT__

#include "glm/glm.hpp"
#include <cstddef>

struct UniformBufferLight
{
    alignas(16) glm::vec3 position;
    alignas(16) glm::vec3 color;
    alignas(16) glm::vec3 direction;
    float range;
    float spotAngle;
    int status;    // DirLight = 0 ; PointLight = 1 ; SpotLight = 2
    int shadowId;
    int minPlaneID; // -1 when the light has no blocker plane.
    int maxPlaneID; // Exclusive end of the range in the plane buffer.
    // Directional bias in shadow texels; reuse the existing std430 tail padding.
    float shadowDepthBias = 0.5f;
    float shadowNormalBias = 1.5f;
};

static_assert(sizeof(UniformBufferLight) == 80, "UniformBufferLight must match the GLSL std430 layout");
static_assert(offsetof(UniformBufferLight, shadowDepthBias) == 68, "Directional depth bias offset must match GLSL");
static_assert(offsetof(UniformBufferLight, shadowNormalBias) == 72, "Directional normal bias offset must match GLSL");

#endif //!__ENGINE_UNIFORM_BUFFER_LIGHT__
