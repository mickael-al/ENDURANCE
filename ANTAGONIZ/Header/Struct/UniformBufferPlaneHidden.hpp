#ifndef __ENGINE_PLANE_BUFFER_OBJECTS__
#define __ENGINE_PLANE_BUFFER_OBJECTS__

#include "glm/glm.hpp"

struct UniformBufferPlaneHidden
{
	alignas(16) glm::vec4 position;
	alignas(16) glm::vec4 axisX;
	alignas(16) glm::vec4 axisY;
	alignas(16) glm::vec4 axisZ;
};

static_assert(sizeof(UniformBufferPlaneHidden) == 64, "UniformBufferPlaneHidden must match the GLSL std430 layout");

#endif //!__ENGINE_PLANE_BUFFER_OBJECTS__