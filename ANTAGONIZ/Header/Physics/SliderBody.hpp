#ifndef __SLIDER_BODY__
#define __SLIDER_BODY__

#include "glm/glm.hpp"
#include "btBulletDynamicsCommon.h"

enum ModeSlider : int
{
    None,
    Spring,   // ressort physique
    Pull     // tirer vers une position cible
};

namespace Ge
{
    class RigidBody;
    class SliderBody
    {
    public:


        SliderBody(btDynamicsWorld* world);
        ~SliderBody();

        void setSpring(RigidBody* A, RigidBody* B, const glm::quat dir,const glm::vec3 pos);
        void updateSpring(float stiffness,float damping);
        void setPull(float maxForce, RigidBody* body, const glm::quat dir);
        void updateTarget(const glm::vec3 worldTarget); // pour le mode Pull
        void disable();

        bool isActive() const { return m_constraint != nullptr; }
        ModeSlider mode() const { return m_mode; }

    private:
        btDynamicsWorld* m_world = nullptr;
        btSliderConstraint* m_constraint = nullptr;

        ModeSlider m_mode = ModeSlider::None;

        float m_rest = 0.0f;
    };
}
#endif //!__SLIDER_BODY__