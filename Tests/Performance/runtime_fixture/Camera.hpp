#pragma once
#include <glm/glm.hpp>
namespace Ge
{
    // Ownership/input fixture only. The actual atmosphere implementation reads
    // exactly the same view/projection matrices as the production tree renderer.
    struct Camera
    {
        glm::vec3 position{0};
        glm::mat4 view{1},projection{1};
        float farDistance=49152;
        glm::vec3 getPosition() const { return position; }
        glm::mat4 getViewMatrix() const { return view; }
        glm::mat4 getProjectionMatrix() const { return projection; }
        float getFar() const { return farDistance; }
    };
}
