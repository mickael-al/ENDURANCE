#ifndef __PHYSICS_WRAPER__
#define __PHYSICS_WRAPER__

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <memory>
#include <atomic>
#include <vector>
#include "Behaviour.hpp"

class btBvhTriangleMeshShape;
class btConeTwistConstraint;
class btFixedConstraint;
class btPoint2PointConstraint;
class btTypedConstraint;
class btCollisionObject;
namespace Ge
{
    class CommandQueue;
    class PhysicsEngine;
    class RigidBody;
    class CollisionBody;
    class Muscle;
    class CollisionShape;
    class Model;
    class ShapeBuffer;
    class GObject;
    class Materials;
    class GraphiquePipeline;
    class RigidWraper;
    class CollisionWraper;
    class MuscleWraper;
    class StaticMeshCollider;
    class SliderWraper;
    class CharacterControllerWraper;
    struct ConvexHit;
    struct CharacterExitRequest;

    struct PointToPointLink
    {
        RigidWraper* bodyA = nullptr;
        RigidWraper* bodyB = nullptr;
        glm::vec3 worldPivot{ 0.0f };
        float rigid = 0.5f;
        float damping = 0.1f;
        float clamp = 0.0f;
    };

    struct ConeTwistLink
    {
        RigidWraper* bodyA = nullptr;
        RigidWraper* bodyB = nullptr;
        glm::vec3 worldPivot{ 0.0f };
        glm::vec3 worldTwistAxis{ 1.0f, 0.0f, 0.0f };
        float swingSpan = 0.7853982f;
        float twistSpan = 0.4363323f;
        float softness = 0.9f;
        float biasFactor = 0.3f;
        float relaxationFactor = 1.0f;
        float damping = 0.85f;
    };

    class PhysicsWraper : public Behaviour
    {
    public:
        PhysicsWraper(PhysicsEngine*pe, CommandQueue* queue);
        RigidWraper* AllocateRigidbody(std::shared_ptr<CollisionShape> shape, bool hasInertia = true);
        std::vector<RigidWraper*> AllocateRigidbody(std::shared_ptr<CollisionShape> shape, int nb, bool hasInertia = true);
        std::vector<RigidWraper*> AllocateRigidbody(
            const std::vector<std::shared_ptr<CollisionShape>>& shapes,
            bool hasInertia = true);
        CollisionWraper* AllocateCollision(std::shared_ptr<CollisionShape> shape);
        std::vector<CollisionWraper*> AllocateCollision(std::shared_ptr<CollisionShape> shape, int nb);
        MuscleWraper* AllocateMuscle(RigidWraper* rb1, RigidWraper* rb2, float degres, float scale = 1.0f, bool adaptePos = true);
        void ReleaseMuscle(MuscleWraper* pBody);
        StaticMeshCollider* AllocateStaticMeshCollider(btBvhTriangleMeshShape* meshShape);
        StaticMeshCollider* AllocateStaticMeshCollider(
            btBvhTriangleMeshShape* meshShape,
            glm::vec3 position,
            glm::quat rotation,
            glm::vec3 scale,
            std::shared_ptr<void> resourceOwner = nullptr);
        void ReleaseStaticMeshCollider(StaticMeshCollider* pBody);
        void AddRigidbody(RigidWraper* pbody, int group = 1, int mask = -1);
        void ReleaseRigidbody(RigidWraper* pBody);
        void AddCollision(CollisionWraper* body);
        void ReleaseCollision(CollisionWraper* pBody);
        btFixedConstraint* weldRigidBodiesAuto(RigidWraper* bodyA, RigidWraper* bodyB);
        btPoint2PointConstraint* linkRigidBodiesP2P(RigidWraper* bodyA, RigidWraper* bodyB, float rigid = 0.5f, float damping = 0.1f, float clamp = 0.0f);
        btPoint2PointConstraint* linkRigidBodiesP2PAt(
            RigidWraper* bodyA,
            RigidWraper* bodyB,
            glm::vec3 worldPivot,
            float rigid = 0.5f,
            float damping = 0.1f,
            float clamp = 0.0f);
        std::vector<btPoint2PointConstraint*> linkRigidBodiesP2PAt(
            const std::vector<PointToPointLink>& links);
        std::vector<btConeTwistConstraint*> linkRigidBodiesConeTwistAt(
            const std::vector<ConeTwistLink>& links);
        void destroyConstraint(btTypedConstraint* constraint);
        SliderWraper* allocateSlider();
        CharacterControllerWraper* allocateCharacterController(float radius, float height, float stepHeight);
        void releaseCharacterController(CharacterControllerWraper* cc);
        void releaseSlider(SliderWraper* s);
        bool raycast(const glm::vec3* start, const glm::vec3* end, glm::vec3* hitPoint, glm::vec3* hitNormal = nullptr);
        bool raycastStaticSurface(const glm::vec3* start, const glm::vec3* end, glm::vec3* hitPoint, glm::vec3* hitNormal = nullptr, int* surfaceType = nullptr);
		bool raycastSurface(const glm::vec3* start, const glm::vec3* end, glm::vec3* hitPoint, glm::vec3* hitNormal = nullptr, int* surfaceType = nullptr, RigidBody** parentBody = nullptr);
        bool sweepSphere(const glm::vec3* start, const glm::vec3* end, float radius, glm::vec3* hitPoint, glm::vec3* hitNormal = nullptr);
        std::vector<ConvexHit> testConvexShape(std::shared_ptr<CollisionShape> shape, glm::vec3 position, std::vector<const btCollisionObject*> ignoreList);
        bool isPointInsideCollision(const glm::vec3* point, float testRadius = 0.01f);
        bool findCharacterExit(CharacterExitRequest request, glm::vec3* position);
        void DebugDrawCollider();
        void DebugClearCollider();
        // Acquired by polling on the caller thread; acknowledged after earlier
        // queued commands execute, including while simulation is paused.
        std::shared_ptr<std::atomic<bool>> enqueueBarrier();
        void setPaused(bool paused);
        bool isPaused() const;
        void start();
        void fixedUpdate();
        void update();
        void stop();
        void onGUI();
    private:        
        PhysicsEngine* m_pe;
        CommandQueue* m_queue;
        bool m_debugDraw =false;
        std::vector<Model*> m_models;
        ShapeBuffer * m_shapeBuffer;
        Materials* m_material;
        GraphiquePipeline* m_cubeShader;
    };
}

#endif// !__PHYSICS_WRAPER__
