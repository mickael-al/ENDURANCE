#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
namespace Ge
{
    // Only camera ownership/input are replaced. Atmosphere uses real matrices.
    struct Camera
    {
        glm::vec3 position{0,100,0};
        glm::vec3 forward{0,0,-1};
        float farDistance=32768;
        float fov=80.f;
        glm::vec3 getPosition() const { return position; }
        glm::mat4 getViewMatrix() const { return glm::lookAt(position,position+forward,glm::vec3(0,1,0)); }
        glm::mat4 getProjectionMatrix() const { return glm::perspective(glm::radians(fov),16.f/9.f,.1f,farDistance); }
        float getFar() const { return farDistance; }
    };
}
