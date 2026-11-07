#ifndef __SLIDER_WRAPER_HPP__
#define __SLIDER_WRAPER_HPP__

#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>
#include "glm/gtc/quaternion.hpp"

enum ModeSlider : int;

namespace Ge
{
    class SliderBody;
    class CommandQueue;
    class RigidWraper;
    class SliderWraper
    {
    public:
        SliderWraper(SliderBody* mu, CommandQueue* queue);
        void setSpring(RigidWraper* bodyA, RigidWraper* bodyB, const glm::quat dir, glm::vec3 limit);
        void updateSpring(float stiffness, float damping);
        void setPull(float maxForce, RigidWraper* body, const glm::quat dir);
        void updateTarget(const glm::vec3 worldTarget); // pour le mode Pull
        void disable();
        bool isActive() const;
        ModeSlider mode() const;
    private:
        friend class PhysicsWraper;
        SliderBody* getSlider() const;
    private:
        SliderBody* m_sb;
        CommandQueue* m_queue;
    };
}

#endif//!__SLIDER_WRAPER_HPP__