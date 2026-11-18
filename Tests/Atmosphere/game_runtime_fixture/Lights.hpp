#pragma once
#include <glm/glm.hpp>
#include <string>
namespace Ge
{
    struct Lights
    {
        glm::vec3 direction{0,-.70710678f,-.70710678f};
        glm::vec3 colors{1,1,1};
        float range=10,casterMargin=0;
        int status=0,shadow=-1;
        unsigned index=0;
        bool active=true;
        std::string name="fixture sun";
        bool isActive() const { return active; }
        int getStatus() const { return status; }
        glm::vec3 getDirection() const { return direction; }
        glm::vec3 getColors() const { return colors; }
        float getRange() const { return range; }
        unsigned getIndex() const { return index; }
        int getShadowIndex() const { return shadow; }
        void setShadowCasterMargin(float value) { casterMargin=value; }
        float getShadowCasterMargin() const { return casterMargin; }
        bool getshadow() const { return shadow>=0; }
        std::string* getName() { return &name; }
    };
}
