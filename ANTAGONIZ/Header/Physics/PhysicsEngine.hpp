#ifndef _PHYSICS_ENGINE_HPP__
#define _PHYSICS_ENGINE_HPP__

#include "glm/glm.hpp"
#include "glm/gtc/quaternion.hpp"
#include <chrono>
#define BT_USE_STATIC_LIBS
#include "btBulletDynamicsCommon.h"
#include "Debug.hpp"
#include <vector>
#include <memory>
#include <algorithm>
#include <atomic>
#include "BulletCollision/CollisionDispatch/btGhostObject.h"

namespace Ge
{

class RigidBody;
class CollisionBody;
class Muscle;
class CollisionShape;
class StaticMeshCollider;
class GObject;
class CommandQueue;
class SliderBody;
class CharacterController;
struct ConvexHit;
struct CharacterExitRequest;

class PhysicsEngine
{
public:
    bool Initialize(glm::vec3 const& gravity, CommandQueue* queue);
    void Update(float dt,bool engine = true);
    void setPaused(bool paused);
    bool isPaused() const;
    void Shutdown();
public:
    RigidBody* AllocateRigidbody(std::shared_ptr<CollisionShape> shape, bool hasInertia = true);
    CollisionBody* AllocateCollision(std::shared_ptr<CollisionShape> shape);
    Muscle* AllocateMuscle(RigidBody* rb1, RigidBody* rb2, float degres, float scale = 1.0f, bool adaptePos = true);
    void ReleaseMuscle(Muscle* pBody);
    StaticMeshCollider * AllocateStaticMeshCollider(btBvhTriangleMeshShape* meshShape);
    StaticMeshCollider* AllocateStaticMeshCollider(
        btBvhTriangleMeshShape* meshShape,
        glm::vec3 position,
        glm::quat rotation,
        glm::vec3 scale,
        std::shared_ptr<void> resourceOwner = nullptr);
    void ReleaseStaticMeshCollider(StaticMeshCollider* pBody);
    void AddRigidbody(RigidBody* body,int group = 1,int mask = -1);
    void ReleaseRigidbody(RigidBody * pBody);
    void AddCollision(CollisionBody* body);
    void ReleaseCollision(CollisionBody* pBody);    
    btFixedConstraint* weldRigidBodiesAuto(RigidBody* bodyA, RigidBody* bodyB);
    void destroyConstraint(btTypedConstraint* constraint);
    btPoint2PointConstraint* linkRigidBodiesP2P(RigidBody* bodyA, RigidBody* bodyB, float rigid = 0.5f, float damping = 0.1f,float clamp = 0.0f);
    btPoint2PointConstraint* linkRigidBodiesP2PAt(
        RigidBody* bodyA,
        RigidBody* bodyB,
        glm::vec3 worldPivot,
        float rigid = 0.5f,
        float damping = 0.1f,
        float clamp = 0.0f);
    btConeTwistConstraint* linkRigidBodiesConeTwistAt(
        RigidBody* bodyA,
        RigidBody* bodyB,
        glm::vec3 worldPivot,
        glm::vec3 worldTwistAxis,
        float swingSpan,
        float twistSpan,
        float softness = 0.9f,
        float biasFactor = 0.3f,
        float relaxationFactor = 1.0f,
        float damping = 0.85f);
    SliderBody* allocateSlider();
    void releaseSlider(SliderBody* s);
    CharacterController* allocateCharacterController(float radius, float height, float stepHeight = 0.3f);
    void releaseCharacterController(CharacterController* cc);
    std::vector<ConvexHit> testConvexShape(std::shared_ptr<CollisionShape> shape, glm::vec3 position,std::vector<const btCollisionObject*> ignoreList);
    bool isPointInsideCollision(const glm::vec3 * point, float testRadius = 0.01f);
    bool findCharacterExit(CharacterExitRequest request, glm::vec3* position);
    bool raycast(const glm::vec3* start, const glm::vec3* end, glm::vec3* hitPoint, glm::vec3* hitNormal = nullptr);
    bool raycastStaticSurface(const glm::vec3* start, const glm::vec3* end, glm::vec3* hitPoint, glm::vec3* hitNormal = nullptr, int* surfaceType = nullptr);
	bool raycastSurface(const glm::vec3* start, const glm::vec3* end, glm::vec3* hitPoint, glm::vec3* hitNormal = nullptr, int* surfaceType = nullptr, RigidBody** parentBody = nullptr);
    bool sweepSphere(const glm::vec3* start, const glm::vec3* end, float radius, glm::vec3* hitPoint, glm::vec3* hitNormal = nullptr);    
    const std::vector<RigidBody*> getRigidbody() const;
    const std::vector<CollisionBody*> getCollisionbody() const;
private:
    std::vector<RigidBody*> m_rigidbody;
    std::vector<CollisionBody*> m_collisionbody;
    std::vector<Muscle*> m_muscle;
    std::vector<StaticMeshCollider*> m_staticMeshCollider;
    std::vector<btFixedConstraint*> m_constraint;
    std::vector<SliderBody*> m_slider;
    std::vector<CharacterController*> m_characterController;
    CommandQueue* m_queue = nullptr;
    btDynamicsWorld* m_pDynamicWorld = nullptr;
    btBroadphaseInterface               * m_pBroadphaseInterface = nullptr;
    btCollisionDispatcher               * m_pCollisionDispatcher = nullptr;
    btDefaultCollisionConfiguration     * m_pCollisionCongifuration = nullptr;
    btSequentialImpulseConstraintSolver * m_pSequentialConstraintSolver = nullptr;
    btGhostPairCallback* m_ghostCallback = nullptr;
    std::atomic<bool> m_paused{false};
};

static glm::vec3 toGlmVec3(const btVector3& vec)
{
    return glm::vec3(vec.getX(), vec.getY(), vec.getZ());
}

static btVector3 toBtVec3(const glm::vec3& vec)
{
    return btVector3(vec.x, vec.y, vec.z);
}

}

#endif
