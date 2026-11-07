#include "PhysicsEngine.hpp"
#include "RigidBody.hpp"
#include "CollisionBody.hpp"
#include "Muscle.hpp"
#include "StaticMeshCollider.hpp"
#include "CommandQueue.hpp"
#include "SliderBody.hpp"
#include "ConvexContactCallback.hpp"
#include "CharacterController.hpp"
#include "CharacterExitQuery.hpp"

#include <cstdint>
#include <cmath>
#include <utility>

namespace
{
	constexpr float FixedPhysicsStep = 1.0f / 60.0f;
	constexpr int MaximumPhysicsSubSteps = 4;
}

namespace Ge
{
	bool PhysicsEngine::Initialize(glm::vec3 const& gravity, CommandQueue* queue)
	{
		auto beginPhysicsInit = std::chrono::steady_clock::now();
		Debug::Info("Initialisation du moteur physique");
		m_queue = queue;
		m_pCollisionCongifuration = new btDefaultCollisionConfiguration();
		m_pCollisionDispatcher = new btCollisionDispatcher(m_pCollisionCongifuration);
		m_pBroadphaseInterface = new btDbvtBroadphase();
		m_pSequentialConstraintSolver = new btSequentialImpulseConstraintSolver();

		m_pDynamicWorld = new btDiscreteDynamicsWorld(
			m_pCollisionDispatcher,
			m_pBroadphaseInterface,
			m_pSequentialConstraintSolver,
			m_pCollisionCongifuration);

		m_pDynamicWorld->setGravity(btVector3(gravity.x, gravity.y, gravity.z));

		auto endPhysicsInit = std::chrono::steady_clock::now();
		auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(endPhysicsInit - beginPhysicsInit);
		m_ghostCallback = new btGhostPairCallback();

		m_pDynamicWorld->getBroadphase()
			->getOverlappingPairCache()
			->setInternalGhostPairCallback(m_ghostCallback);
		return true;
	}

	void PhysicsEngine::Update(float dt,bool engine)
	{
		// Apply the latest gameplay commands before advancing Bullet. Movement
		// commands remain active across every fixed substep until replaced.
		while (!m_queue->empty())
		{
			Command* command = m_queue->pop();
			command->execute();
			delete command;
		}

		if (!m_paused.load(std::memory_order_acquire))
		{
			const float maximumFrameTime = FixedPhysicsStep * MaximumPhysicsSubSteps;
			const float simulationTime = std::clamp(dt, 0.0f, maximumFrameTime);
			m_pDynamicWorld->stepSimulation(
				simulationTime,
				MaximumPhysicsSubSteps,
				FixedPhysicsStep);

			// Rendering and gameplay run on another thread. Publish plain snapshots
			// only after Bullet has finished, so they never touch live objects.
			for (RigidBody* body : m_rigidbody)
			{
				if (body != nullptr)
				{
					body->capturePhysicsSnapshot();
				}
			}
			for (CharacterController* controller : m_characterController)
			{
				if (controller != nullptr)
				{
					controller->capturePhysicsSnapshot();
				}
			}
		}
	}

	void PhysicsEngine::setPaused(bool paused)
	{
		m_paused.store(paused, std::memory_order_release);
	}

	bool PhysicsEngine::isPaused() const
	{
		return m_paused.load(std::memory_order_acquire);
	}

	void PhysicsEngine::Shutdown()
	{
		if (m_pDynamicWorld == nullptr)
		{
			return;
		}
		for (int i = 0; i < m_muscle.size(); i++)
		{
			delete m_muscle[i];
		}
		m_muscle.clear();
		for (int i = 0; i < m_rigidbody.size(); i++)
		{
			m_pDynamicWorld->removeRigidBody(m_rigidbody[i]->m_pBody);			
			delete m_rigidbody[i];
		}
		m_rigidbody.clear();
		for (int i = 0; i < m_collisionbody.size(); i++)
		{
			m_pDynamicWorld->removeCollisionObject(m_collisionbody[i]->m_pBody);
			delete m_collisionbody[i];
		}
		m_collisionbody.clear();
		for (StaticMeshCollider* collider : m_staticMeshCollider)
		{
			delete collider;
		}
		m_staticMeshCollider.clear();
		for (int i = 0; i < m_characterController.size(); i++)
		{
			delete m_characterController[i];
		}
		m_characterController.clear();
		m_pDynamicWorld->getBroadphase()->getOverlappingPairCache()->setInternalGhostPairCallback(nullptr);
		delete m_ghostCallback;
		m_ghostCallback = nullptr;

		// The dynamics world keeps references to the solver, broadphase,
		// dispatcher and collision configuration supplied to its constructor.
		// Destroy it before any of those dependencies.
		delete m_pDynamicWorld;
		m_pDynamicWorld = nullptr;
		delete m_pSequentialConstraintSolver;
		m_pSequentialConstraintSolver = nullptr;
		delete m_pBroadphaseInterface;
		m_pBroadphaseInterface = nullptr;
		delete m_pCollisionDispatcher;
		m_pCollisionDispatcher = nullptr;
		delete m_pCollisionCongifuration;
		m_pCollisionCongifuration = nullptr;
		m_queue = nullptr;
	}

	CollisionBody* PhysicsEngine::AllocateCollision(std::shared_ptr<CollisionShape> shape)
	{
		return new CollisionBody(m_pDynamicWorld, shape);
	}

	void PhysicsEngine::AddCollision(CollisionBody* body)
	{
		if (body != nullptr && body->IsInitialized())
		{
			m_pDynamicWorld->addCollisionObject(body->GetBody());			
			m_collisionbody.push_back(body);
		}
		else
		{
			Debug::Error("PhysicsEngine::AddCollision(): Le corps donné n'est pas initialisé.");
		}
	}

	const std::vector<RigidBody*> PhysicsEngine::getRigidbody() const
	{
		return m_rigidbody;
	}

	const std::vector<CollisionBody*> PhysicsEngine::getCollisionbody() const
	{
		return m_collisionbody;
	}

	bool PhysicsEngine::raycast(const glm::vec3* start, const glm::vec3* end, glm::vec3* hitPoint, glm::vec3* hitNormal)
	{
		if (start == nullptr || end == nullptr || hitPoint == nullptr)
		{
			return false;
		}

		const btVector3 btstart = toBtVec3(*start);
		const btVector3 btend = toBtVec3(*end);
		btCollisionWorld::ClosestRayResultCallback rayCallback(btstart, btend);

		// Static mesh bodies are registered by Bullet in StaticFilter, not
		// DefaultFilter. Restricting the mask to 1 made rays ignore the sea map.
		rayCallback.m_collisionFilterMask =
			btBroadphaseProxy::StaticFilter | btBroadphaseProxy::DefaultFilter;
		m_pDynamicWorld->rayTest(btstart, btend, rayCallback);

		if (!rayCallback.hasHit())
		{
			return false;
		}

		*hitPoint = toGlmVec3(rayCallback.m_hitPointWorld);
		if (hitNormal != nullptr)
		{
			*hitNormal = toGlmVec3(rayCallback.m_hitNormalWorld);
		}
		return true;
	}

	bool PhysicsEngine::raycastStaticSurface(
		const glm::vec3* start,
		const glm::vec3* end,
		glm::vec3* hitPoint,
		glm::vec3* hitNormal,
		int* surfaceType)
	{
		if (start == nullptr || end == nullptr || hitPoint == nullptr)
		{
			return false;
		}

		const btVector3 btstart = toBtVec3(*start);
		const btVector3 btend = toBtVec3(*end);
		btCollisionWorld::ClosestRayResultCallback rayCallback(btstart, btend);
		rayCallback.m_collisionFilterMask = btBroadphaseProxy::StaticFilter;
		m_pDynamicWorld->rayTest(btstart, btend, rayCallback);
		if (!rayCallback.hasHit())
		{
			return false;
		}

		*hitPoint = toGlmVec3(rayCallback.m_hitPointWorld);
		if (hitNormal != nullptr)
		{
			*hitNormal = toGlmVec3(rayCallback.m_hitNormalWorld);
		}
		if (surfaceType != nullptr)
		{
			*surfaceType = 0;
			const btCollisionShape* shape =
				rayCallback.m_collisionObject == nullptr
					? nullptr
					: rayCallback.m_collisionObject->getCollisionShape();
			if (shape != nullptr)
			{
				*surfaceType = static_cast<int>(
					reinterpret_cast<std::intptr_t>(shape->getUserPointer()));
			}
		}
		return true;
	}

	bool PhysicsEngine::raycastSurface(
		const glm::vec3* start,
		const glm::vec3* end,
		glm::vec3* hitPoint,
		glm::vec3* hitNormal,
		int* surfaceType,
		RigidBody** parentBody)
	{
		if (start == nullptr || end == nullptr || hitPoint == nullptr)
		{
			return false;
		}
		if (parentBody != nullptr)
		{
			*parentBody = nullptr;
		}
		const btVector3 btstart = toBtVec3(*start);
		const btVector3 btend = toBtVec3(*end);
		btCollisionWorld::AllHitsRayResultCallback rayCallback(btstart, btend);
		rayCallback.m_collisionFilterMask =
			btBroadphaseProxy::StaticFilter |
			btBroadphaseProxy::DefaultFilter;
		m_pDynamicWorld->rayTest(btstart, btend, rayCallback);
		if (!rayCallback.hasHit())
		{
			return false;
		}

		int closestIndex = -1;
		btScalar closestFraction = btScalar(1.0);
		RigidBody* closestParent = nullptr;
		for (int index = 0; index < rayCallback.m_collisionObjects.size(); ++index)
		{
			const btCollisionObject* object =
				rayCallback.m_collisionObjects[index];
			RigidBody* rigidParent = nullptr;
			for (RigidBody* body : m_rigidbody)
			{
				if (body != nullptr && body->GetBody() == object)
				{
					rigidParent = body;
					break;
				}
			}
			// Character controllers also use DefaultFilter. They are handled by
			// the gameplay hitboxes and must not become a fake wall here.
			if ((object == nullptr || !object->isStaticObject()) &&
				rigidParent == nullptr)
			{
				continue;
			}
			const btScalar fraction = rayCallback.m_hitFractions[index];
			if (fraction < closestFraction)
			{
				closestFraction = fraction;
				closestIndex = index;
				closestParent = rigidParent;
			}
		}
		if (closestIndex < 0)
		{
			return false;
		}

		*hitPoint = toGlmVec3(rayCallback.m_hitPointWorld[closestIndex]);
		if (hitNormal != nullptr)
		{
			*hitNormal = toGlmVec3(
				rayCallback.m_hitNormalWorld[closestIndex]);
		}
		if (surfaceType != nullptr)
		{
			*surfaceType = 0;
			const btCollisionObject* object =
				rayCallback.m_collisionObjects[closestIndex];
			const btCollisionShape* shape =
				object == nullptr ? nullptr : object->getCollisionShape();
			if (shape != nullptr)
			{
				*surfaceType = static_cast<int>(
					reinterpret_cast<std::intptr_t>(shape->getUserPointer()));
			}
		}
		if (parentBody != nullptr)
		{
			*parentBody = closestParent;
		}
		return true;
	}

	bool PhysicsEngine::findCharacterExit(CharacterExitRequest request, glm::vec3* position)
	{
		if (m_pDynamicWorld == nullptr || position == nullptr ||
			!std::isfinite(request.radius) || !std::isfinite(request.halfHeight) ||
			request.radius <= 0.0f || request.halfHeight < request.radius) return false;
		CharacterExitQuery query(*m_pDynamicWorld, request);
		return findCharacterExitPlacement(request, query, *position);
	}

	bool PhysicsEngine::sweepSphere(const glm::vec3* start, const glm::vec3* end, float radius, glm::vec3* hitPoint, glm::vec3* hitNormal)
	{
		if (start == nullptr || end == nullptr || hitPoint == nullptr || radius <= 0.0f)
		{
			return false;
		}

		btSphereShape sphere(radius);
		btTransform from;
		btTransform to;
		from.setIdentity();
		to.setIdentity();
		from.setOrigin(toBtVec3(*start));
		to.setOrigin(toBtVec3(*end));

		btCollisionWorld::ClosestConvexResultCallback sweepCallback(from.getOrigin(), to.getOrigin());
		sweepCallback.m_collisionFilterMask =
			btBroadphaseProxy::StaticFilter | btBroadphaseProxy::DefaultFilter;
		m_pDynamicWorld->convexSweepTest(&sphere, from, to, sweepCallback);

		if (!sweepCallback.hasHit())
		{
			return false;
		}

		*hitPoint = toGlmVec3(sweepCallback.m_hitPointWorld);
		if (hitNormal != nullptr)
		{
			*hitNormal = toGlmVec3(sweepCallback.m_hitNormalWorld);
		}
		return true;
	}

	std::vector<ConvexHit> PhysicsEngine::testConvexShape(std::shared_ptr<CollisionShape> shape,glm::vec3 position,std::vector<const btCollisionObject*> ignoreList)
	{
		btCollisionObject testObject;

		btTransform transform;
		transform.setIdentity();
		transform.setOrigin(btVector3(position.x, position.y, position.z));

		testObject.setCollisionShape(shape->GetShape());
		testObject.setWorldTransform(transform);
		
		std::unordered_set<const btCollisionObject*> ignore(ignoreList.begin(),ignoreList.end());
		std::vector<ConvexHit> hits;		
		ConvexContactCallback callback(&testObject, &hits, ignore);
		
		callback.m_filterGroup = btBroadphaseProxy::DefaultFilter;
		callback.m_filterMask =
			btBroadphaseProxy::StaticFilter |
			btBroadphaseProxy::DefaultFilter;

		m_pDynamicWorld->contactTest(&testObject, callback);

		return hits;
	}

	void PhysicsEngine::ReleaseCollision(CollisionBody* pBody)
	{
		std::vector<CollisionBody*>::iterator position = std::remove(m_collisionbody.begin(), m_collisionbody.end(), pBody);
		if (position != m_collisionbody.end())
		{
			m_collisionbody.erase(position);
		}
		m_pDynamicWorld->removeCollisionObject(pBody->m_pBody);
		delete pBody;
	}

	btFixedConstraint* PhysicsEngine::weldRigidBodiesAuto(RigidBody* bodyA, RigidBody* bodyB)
	{
		if (!bodyA || !bodyB)
		{
			return nullptr;
		}

		const btTransform& worldA = bodyA->GetBody()->getWorldTransform();
		const btTransform& worldB = bodyB->GetBody()->getWorldTransform();

		btVector3 posA = worldA.getOrigin();
		btVector3 posB = worldB.getOrigin();

		btVector3 deltaWorld = (posB - posA)*0.5;

		btTransform frameA, frameB;
		frameA.setIdentity();
		frameB.setIdentity();

		frameA.setOrigin(worldA.getBasis().transpose() * deltaWorld);
		frameB.setOrigin(worldB.getBasis().transpose() * (-deltaWorld));

		btFixedConstraint* weld = new btFixedConstraint(*bodyA->GetBody(), *bodyB->GetBody(), frameA, frameB);

		weld->setBreakingImpulseThreshold(FLT_MAX);
		
		m_pDynamicWorld->addConstraint(weld, true);

		return weld;
	}

	void PhysicsEngine::destroyConstraint(btTypedConstraint* constraint)
	{
		constraint->setEnabled(false);
		m_pDynamicWorld->removeConstraint(constraint);
		m_pDynamicWorld->stepSimulation(0.f);
		delete constraint;
	}

	btPoint2PointConstraint* PhysicsEngine::linkRigidBodiesP2P(RigidBody* bodyA, RigidBody* bodyB,float rigid,float damping, float clamp)
	{
		if (!bodyA || !bodyB)
		{
			return nullptr;
		}

		const btTransform& worldA = bodyA->GetBody()->getWorldTransform();
		const btTransform& worldB = bodyB->GetBody()->getWorldTransform();

		btVector3 posA = worldA.getOrigin();
		btVector3 posB = worldB.getOrigin();

		// Point au milieu (comme ton weld)
		btVector3 pivotWorld = (posA + posB) * 0.5f;

		// Conversion en local space
		btVector3 pivotA = worldA.inverse() * pivotWorld;
		btVector3 pivotB = worldB.inverse() * pivotWorld;

		btPoint2PointConstraint* constraint = new btPoint2PointConstraint(
			*bodyA->GetBody(),
			*bodyB->GetBody(),
			pivotA,
			pivotB
		);

		// Réglages importants pour stabilité
		constraint->m_setting.m_tau = rigid;      // rigidité
		constraint->m_setting.m_damping = damping;  // amortissement
		constraint->m_setting.m_impulseClamp = clamp;

		m_pDynamicWorld->addConstraint(constraint, true); // disable collision

		return constraint;
	}

	btPoint2PointConstraint* PhysicsEngine::linkRigidBodiesP2PAt(
		RigidBody* bodyA,
		RigidBody* bodyB,
		glm::vec3 worldPivot,
		float rigid,
		float damping,
		float clamp)
	{
		if (bodyA == nullptr || bodyB == nullptr ||
			!std::isfinite(worldPivot.x) ||
			!std::isfinite(worldPivot.y) ||
			!std::isfinite(worldPivot.z))
		{
			return nullptr;
		}

		const btTransform& worldA = bodyA->GetBody()->getWorldTransform();
		const btTransform& worldB = bodyB->GetBody()->getWorldTransform();
		const btVector3 pivot(worldPivot.x, worldPivot.y, worldPivot.z);
		const btVector3 pivotA = worldA.inverse() * pivot;
		const btVector3 pivotB = worldB.inverse() * pivot;

		btPoint2PointConstraint* constraint =
			new btPoint2PointConstraint(
				*bodyA->GetBody(),
				*bodyB->GetBody(),
				pivotA,
				pivotB);
		constraint->m_setting.m_tau = rigid;
		constraint->m_setting.m_damping = damping;
		constraint->m_setting.m_impulseClamp = clamp;
		m_pDynamicWorld->addConstraint(constraint, true);
		return constraint;
	}

	btConeTwistConstraint* PhysicsEngine::linkRigidBodiesConeTwistAt(
		RigidBody* bodyA,
		RigidBody* bodyB,
		glm::vec3 worldPivot,
		glm::vec3 worldTwistAxis,
		float swingSpan,
		float twistSpan,
		float softness,
		float biasFactor,
		float relaxationFactor,
		float damping)
	{
		if (bodyA == nullptr || bodyB == nullptr ||
			!std::isfinite(worldPivot.x) ||
			!std::isfinite(worldPivot.y) ||
			!std::isfinite(worldPivot.z) ||
			!std::isfinite(worldTwistAxis.x) ||
			!std::isfinite(worldTwistAxis.y) ||
			!std::isfinite(worldTwistAxis.z) ||
			!std::isfinite(swingSpan) || swingSpan < 0.0f ||
			!std::isfinite(twistSpan) || twistSpan < 0.0f ||
			!std::isfinite(softness) ||
			!std::isfinite(biasFactor) ||
			!std::isfinite(relaxationFactor) ||
			!std::isfinite(damping))
		{
			return nullptr;
		}

		btVector3 twistAxis(
			worldTwistAxis.x,
			worldTwistAxis.y,
			worldTwistAxis.z);
		const btScalar axisLengthSquared = twistAxis.length2();
		if (axisLengthSquared <= SIMD_EPSILON * SIMD_EPSILON)
		{
			return nullptr;
		}
		twistAxis /= btSqrt(axisLengthSquared);

		const btVector3 reference =
			btFabs(twistAxis.dot(btVector3(0.0f, 1.0f, 0.0f))) < 0.9f
				? btVector3(0.0f, 1.0f, 0.0f)
				: btVector3(0.0f, 0.0f, 1.0f);
		btVector3 frameY = reference.cross(twistAxis);
		frameY.normalize();
		const btVector3 frameZ = twistAxis.cross(frameY).normalized();
		const btMatrix3x3 worldBasis(
			twistAxis.x(), frameY.x(), frameZ.x(),
			twistAxis.y(), frameY.y(), frameZ.y(),
			twistAxis.z(), frameY.z(), frameZ.z());

		btTransform worldFrame(worldBasis);
		worldFrame.setOrigin(btVector3(
			worldPivot.x,
			worldPivot.y,
			worldPivot.z));
		const btTransform frameA =
			bodyA->GetBody()->getWorldTransform().inverse() * worldFrame;
		const btTransform frameB =
			bodyB->GetBody()->getWorldTransform().inverse() * worldFrame;

		btConeTwistConstraint* constraint = new btConeTwistConstraint(
			*bodyA->GetBody(),
			*bodyB->GetBody(),
			frameA,
			frameB);
		constraint->setLimit(
			glm::clamp(swingSpan, 0.0f, static_cast<float>(SIMD_PI)),
			glm::clamp(swingSpan, 0.0f, static_cast<float>(SIMD_PI)),
			glm::clamp(twistSpan, 0.0f, static_cast<float>(SIMD_PI)),
			glm::clamp(softness, 0.0f, 1.0f),
			glm::clamp(biasFactor, 0.0f, 1.0f),
			glm::clamp(relaxationFactor, 0.0f, 1.0f));
		constraint->setDamping(glm::clamp(damping, 0.0f, 1.0f));

		// A cone-twist joint is also a fixed translation joint. Do not inherit
		// the world's softer ERP here: errors accumulate along a ragdoll chain
		// and otherwise let knees or hips drift far enough to stretch the skin.
		// CFM stays at zero so the three pivot axes remain non-elastic.
		for (int axis = 0; axis < 3; ++axis)
		{
			constraint->setParam(BT_CONSTRAINT_STOP_ERP, 0.8f, axis);
			constraint->setParam(BT_CONSTRAINT_STOP_CFM, 0.0f, axis);
		}
		constraint->setOverrideNumSolverIterations(24);
		m_pDynamicWorld->addConstraint(constraint, true);
		return constraint;
	}

	SliderBody* PhysicsEngine::allocateSlider()
	{
		return new SliderBody(m_pDynamicWorld);
	}

	void PhysicsEngine::releaseSlider(SliderBody* s)
	{
		delete s;
	}

	CharacterController* PhysicsEngine::allocateCharacterController(float radius, float height, float stepHeight)
	{
		CharacterController* cc = new CharacterController(m_pDynamicWorld, radius,height,stepHeight);
		m_characterController.push_back(cc);
		return cc;
	}

	void PhysicsEngine::releaseCharacterController(CharacterController* cc)
	{
		std::vector<CharacterController*>::iterator position = std::remove(m_characterController.begin(), m_characterController.end(), cc);
		if (position != m_characterController.end())
		{
			m_characterController.erase(position);
		}
		delete cc;
	}

	bool PhysicsEngine::isPointInsideCollision(const glm::vec3 * point,float testRadius)
	{
		// Crée une petite sphère à la position du point
		btSphereShape testShape(testRadius);

		// Transforme le point glm::vec3 en btVector3
		btTransform transform;
		transform.setIdentity();
		transform.setOrigin(btVector3(point->x, point->y, point->z));

		// Crée un objet collision temporaire
		btCollisionObject obj;
		obj.setCollisionShape(&testShape);
		obj.setWorldTransform(transform);

		// Structure pour récupérer le résultat du test
		struct Callback : public btCollisionWorld::ContactResultCallback 
		{
			bool hasHit = false;
			btScalar addSingleResult(btManifoldPoint&, const btCollisionObjectWrapper*, int, int,const btCollisionObjectWrapper*, int, int) override 
			{
				hasHit = true;
				return 0;
			}
		} callback;

		m_pDynamicWorld->contactTest(&obj, callback);

		return callback.hasHit; // true = le point est à l'intérieur (ou touche)
	}

	RigidBody * PhysicsEngine::AllocateRigidbody(std::shared_ptr<CollisionShape> shape, bool hasInertia)
	{
		return new RigidBody(m_pDynamicWorld, shape, hasInertia);
	}

	Muscle* PhysicsEngine::AllocateMuscle(RigidBody* rb1, RigidBody* rb2,float degres,float scale, bool adaptePos)
	{
		Muscle* m = new Muscle(m_pDynamicWorld, rb1->GetBody(), rb2->GetBody(), rb1->getPosition(), rb2->getPosition(), rb1->getScale()* scale,rb2->getScale()* scale,degres, adaptePos);
		m_muscle.push_back(m);
		return m;
	}

	void PhysicsEngine::ReleaseMuscle(Muscle* pBody)
	{
		std::vector<Muscle*>::iterator position = std::remove(m_muscle.begin(), m_muscle.end(), pBody);
		if (position != m_muscle.end())
		{
			m_muscle.erase(position);
		}
		delete pBody;
	}

	StaticMeshCollider* PhysicsEngine::AllocateStaticMeshCollider(btBvhTriangleMeshShape* meshShape)
	{
		StaticMeshCollider* smc = new StaticMeshCollider(m_pDynamicWorld, meshShape);
		m_staticMeshCollider.push_back(smc);
		return smc;
	}

	StaticMeshCollider* PhysicsEngine::AllocateStaticMeshCollider(
		btBvhTriangleMeshShape* meshShape,
		glm::vec3 position,
		glm::quat rotation,
		glm::vec3 scale,
		std::shared_ptr<void> resourceOwner)
	{
		btTransform transform;
		transform.setIdentity();
		transform.setOrigin(toBtVec3(position));
		transform.setRotation(btQuaternion(
			rotation.x,
			rotation.y,
			rotation.z,
			rotation.w));
		StaticMeshCollider* collider = new StaticMeshCollider(
			m_pDynamicWorld,
			meshShape,
			transform,
			toBtVec3(scale),
			std::move(resourceOwner));
		// This overload is used by immutable scene props. Configure the body on
		// the physics thread before publishing it to the gameplay thread.
		collider->setFriction(0.82f);
		collider->setRestitution(0.05f);
		m_staticMeshCollider.push_back(collider);
		return collider;
	}

	void PhysicsEngine::ReleaseStaticMeshCollider(StaticMeshCollider* pBody)
	{
		std::vector<StaticMeshCollider*>::iterator position = std::remove(m_staticMeshCollider.begin(), m_staticMeshCollider.end(), pBody);
		if (position != m_staticMeshCollider.end())
		{
			m_staticMeshCollider.erase(position);
		}
		delete pBody;
	}

	void PhysicsEngine::AddRigidbody(RigidBody* body,int group,int mask)
	{
		if (body != nullptr && body->IsInitialized())
		{				
			m_pDynamicWorld->addRigidBody(body->GetBody(),group,mask);
			m_rigidbody.push_back(body);
			body->setInWorldSnapshot(true);
			body->capturePhysicsSnapshot();
		}
		else
		{
			Debug::Error("PhysicsEngine::AddRigidbody(): Le corps donné n'est pas initialisé.");
		}
	}

	void PhysicsEngine::ReleaseRigidbody(RigidBody * pBody)
	{		
		std::vector<RigidBody*>::iterator position = std::remove(m_rigidbody.begin(), m_rigidbody.end(), pBody);
		if (position != m_rigidbody.end())
		{
			m_rigidbody.erase(position);
		}
		pBody->setInWorldSnapshot(false);
		m_pDynamicWorld->removeRigidBody(pBody->m_pBody);
		delete pBody;
	}
}
