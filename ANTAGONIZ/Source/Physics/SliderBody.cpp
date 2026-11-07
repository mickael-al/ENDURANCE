#include "SliderBody.hpp"
#include "RigidBody.hpp"

namespace Ge
{

    SliderBody::SliderBody(btDynamicsWorld* world) : m_world(world)
    {}

    SliderBody::~SliderBody()
    {
        disable();
    }    
        

    btQuaternion toBt(const glm::quat& q)
    {
        return btQuaternion(q.x, q.y, q.z, q.w);
    }

    void SliderBody::setSpring(RigidBody* A,RigidBody* B,const glm::quat dir,const glm::vec3 limitRest)
    {
        disable();

        btTransform frameA, frameB;
        frameA.setIdentity();
        frameB.setIdentity();

        btQuaternion btQ = toBt(dir);
        frameA.setRotation(btQ);
        frameB.setRotation(btQ);

        frameA.setOrigin(btVector3(0, 0, 0));
        btVector3 restVec = quatRotate(btQ, btVector3(-limitRest.z, 0, 0));
        frameB.setOrigin(restVec);

        m_constraint = new btSliderConstraint(
            *A->GetBody(),
            *B->GetBody(),
            frameA,
            frameB,
            true // useLinearReferenceFrameA
        );

        m_constraint->setLowerLinLimit(limitRest.x);
        m_constraint->setUpperLinLimit(limitRest.y);

        m_rest = limitRest.z;

        m_constraint->setLowerAngLimit(0.0f);
        m_constraint->setUpperAngLimit(0.0f);

        m_constraint->setPoweredLinMotor(true);
        m_constraint->setMaxLinMotorForce(600.0f);

        m_world->addConstraint(m_constraint, true);

        A->GetBody()->activate(true);
        B->GetBody()->activate(true);

        m_mode = ModeSlider::Spring;
    }


    void SliderBody::updateSpring(float stiffness, float damping)
    {
        if (!m_constraint || m_mode != ModeSlider::Spring)
        {
            return;
        }

        float pos = m_constraint->getLinearPos();
        float error = m_rest - pos;
        
        const btRigidBody& A = m_constraint->getRigidBodyA();
        const btRigidBody& B = m_constraint->getRigidBodyB();

        // AXE RÉEL DU SLIDER (frameA X)
        btVector3 axis =
            A.getWorldTransform().getBasis() *
            m_constraint->getFrameOffsetA().getBasis().getColumn(0);

        btVector3 relVel = A.getLinearVelocity() - B.getLinearVelocity();
        float velAlongAxis = axis.dot(relVel);

        float motorVel = stiffness * error - damping * velAlongAxis;

        m_constraint->setTargetLinMotorVelocity(motorVel);

        A.activate(true);
        B.activate(true);
    }

    void SliderBody::setPull(float maxForce, RigidBody* body,const glm::quat dir)
    {
        disable();

        btTransform frame;
        frame.setIdentity();
        frame.setRotation(toBt(dir));
        m_constraint = new btSliderConstraint(*body->GetBody(), frame, true);

        // Mouvement uniquement linéaire
        m_constraint->setLowerLinLimit(-BT_LARGE_FLOAT);
        m_constraint->setUpperLinLimit(BT_LARGE_FLOAT);

        m_constraint->setLowerAngLimit(0);
        m_constraint->setUpperAngLimit(0);

        // Activer moteur
        m_constraint->setPoweredLinMotor(true);
        m_constraint->setMaxLinMotorForce(maxForce);

        m_world->addConstraint(m_constraint, true);

        m_mode = ModeSlider::Pull;
    }


    void SliderBody::updateTarget(const glm::vec3 worldTarget)
    {
        if (!m_constraint || m_mode != ModeSlider::Pull)
        {
            return;
        }
        
        const btTransform& frameA = m_constraint->getFrameOffsetA();

        // Transform monde -> local slider
        btVector3 localTarget = frameA.inverse() * btVector3(worldTarget.x, worldTarget.y, worldTarget.z);

        // Position scalaire sur l'axe du slider (X local)
        float targetPos = localTarget.getX();

        // Position actuelle
        float current = m_constraint->getLinearPos();

        // Erreur 1D
        float error = targetPos - current;

        // Gain proportionnel (à ajuster)
        constexpr float gain = 10.0f;
        float velocity = error * gain;

        m_constraint->setTargetLinMotorVelocity(velocity);
    }

    void SliderBody::disable()
    {
        if (!m_constraint)
        {
            return;
        }

        m_world->removeConstraint(m_constraint);
        delete m_constraint;
        m_constraint = nullptr;
        m_mode = ModeSlider::None;
    }
}