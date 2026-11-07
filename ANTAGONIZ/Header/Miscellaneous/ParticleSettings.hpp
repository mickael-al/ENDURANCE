#ifndef PARASITE_PARTICLE_SETTINGS_HPP
#define PARASITE_PARTICLE_SETTINGS_HPP

#include <cstdint>
#include <glm/glm.hpp>

namespace Ge
{
    enum ParticleFlags : uint32_t
    {
        AttachToEmitPoint = 1u << 0,
        BoxEmit = 1u << 1,
        FollowTarget = 1u << 2,
        Rotate = 1u << 3,
        VelOrientation = 1u << 4,
        AVoidTarget = 1u << 5,
        RotateSprite = 1u << 6,
        CutMoveSprite = 1u << 7,
        InstantParticuleInitialize = 1u << 8,//???
        NotMove = 1u << 9,
        ScaleRNG = 1u << 10,
        Burst = 1u << 11,
        OrbitTarget = 1u << 12, // World-Y orbit; rotationMin/Max are degrees per second.
        // Orbit from the spawn position, without following the live target.
        // Starts on +X; rotationMin/Max = degrees/sec, rotationScaleMin/Max = radius.
        OrbitWorld = 1u << 13,
        // Uniform volume inside an elliptical XZ cylinder, between emitScaleMin/Max.
        // CylinderEmit takes precedence when BoxEmit is also set.
        CylinderEmit = 1u << 14,
        // Transform the birth offset by emitAxisX/Y/Z. Position and forces stay in world space.
        LocalEmitTransform = 1u << 15,
    };

    struct ParticleBaseGroup
    {
        float lifeTimeMin;//0
        float lifeTimeMax;//1
        glm::vec3 emitPosition;//2
        glm::vec3 emitDirection;//5
        float spreadAngle;//8
        uint32_t flags;//9

        float velocityMin;//10
        float velocityMax;//11
        float scaleStart;//12
        float scaleEnd;//13

        glm::vec3 emitScaleMin;//14
        glm::vec3 emitScaleMax;//17

        glm::vec3 gravity;//20
        glm::vec3 externalForce;//23
        float drag;//26

        // Rotation
        float rotationMin;//27
        float rotationMax;//28
        float rotationScaleMin;//29
        float rotationScaleMax;//30

        glm::vec3 target;//31
        float followStrength;//34
        float followRadius;//35

        // Color / Render
        unsigned int nbColor;//36
        glm::vec4 colorGradient[10];//37

        float avoidStrength;//77
        float avoidRadius;//78

        glm::vec2 tilling;//79
        glm::vec2 offset;//81
        float rotateSprite;//83

        // Appended to preserve the existing CPU/GPU settings offsets. These basis
        // columns can include object rotation, scale and the parent transform.
        glm::vec3 emitAxisX;//84
        glm::vec3 emitAxisY;//87
        glm::vec3 emitAxisZ;//90

        ParticleBaseGroup()
        {
            // Durée de vie (1 à 2 secondes)
            lifeTimeMin = 1.0f;
            lifeTimeMax = 2.0f;

            // Position et direction d’émission
            emitPosition = glm::vec3(0.0f);
            emitDirection = glm::vec3(0.0f, 1.0f, 0.0f); // Vers le haut
            spreadAngle = 15.0f; // Petit cône
            flags = 0;

            // Scale initial / final
            scaleStart = 1.0f;
            scaleEnd = 0.0f; // se dissipe

            // Vitesse
            velocityMin = 8.0f;
            velocityMax = 10.0f;

            // Forces
            gravity = glm::vec3(0.0f, -9.81f, 0.0f);
            externalForce = glm::vec3(0.0f); // pas de vent
            drag = 0.1f; // amortissement léger

            // Rotation
            rotationMin = -360.0f;
            rotationMax = 360.0f;
            rotationScaleMin = 1.0f;
            rotationScaleMax = 2.0f;

            nbColor = 2;
            colorGradient[0] = glm::vec4(1.0f, 0.8f, 0.3f, 1.0f);
            colorGradient[1] = glm::vec4(0.2f, 0.2f, 0.2f, 0.0f);

            emitScaleMin = glm::vec3(-1.0f, -1.0f, -1.0f);
            emitScaleMax = glm::vec3(1.0f, 1.0f, 1.0f);
            target = glm::vec3(0, 0, 0);
            followStrength = 0.1f;
            followRadius = 2.0f;
            avoidStrength = 0.1f;
            avoidRadius = 2.0f;

            tilling = glm::vec2(1.0f);
            offset = glm::vec2(0,0);
            rotateSprite = 0.0f;
            emitAxisX = glm::vec3(1.0f, 0.0f, 0.0f);
            emitAxisY = glm::vec3(0.0f, 1.0f, 0.0f);
            emitAxisZ = glm::vec3(0.0f, 0.0f, 1.0f);
        }
    };

}

#endif
