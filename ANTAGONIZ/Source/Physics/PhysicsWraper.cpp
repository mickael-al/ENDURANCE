#include "PhysicsWraper.hpp"
#include "CharacterExitPlacement.hpp"
#include "CommandQueue.hpp"
#include "PhysicsEngine.hpp"
#include "RigidWraper.hpp"
#include "Engine.hpp"
#include "RigidBody.hpp"
#include "PointeurClass.hpp"
#include "Materials.hpp"
#include "Model.hpp"
#include "CollisionWraper.hpp"
#include "CollisionBody.hpp"
#include "MuscleWraper.hpp"
#include "SliderWraper.hpp"
#include "CharacterControllerWraper.hpp"
#include "ConvexContactCallback.hpp"

#include <utility>

namespace
{
    class PhysicsQueueBarrier final : public Ge::Command
    {
    public:
        explicit PhysicsQueueBarrier(std::shared_ptr<std::atomic<bool>> acknowledged)
            : m_acknowledged(std::move(acknowledged)) {}

        void execute() override
        {
            m_acknowledged->store(true, std::memory_order_release);
        }

    private:
        std::shared_ptr<std::atomic<bool>> m_acknowledged;
    };
}

namespace Ge
{
	PhysicsWraper::PhysicsWraper(PhysicsEngine* pe, CommandQueue* queue)
	{
		m_pe = pe;
		m_queue = queue;
	}

	std::shared_ptr<std::atomic<bool>> PhysicsWraper::enqueueBarrier()
	{
		auto acknowledged = std::make_shared<std::atomic<bool>>(false);
		// Keep ownership if push throws; a queued command is owned by the queue.
		auto command = std::make_unique<PhysicsQueueBarrier>(acknowledged);
		m_queue->push(command.get());
		command.release();
		return acknowledged;
	}

	void PhysicsWraper::setPaused(bool paused)
	{
		m_pe->setPaused(paused);
	}

	bool PhysicsWraper::isPaused() const
	{
		return m_pe->isPaused();
	}

	RigidWraper* PhysicsWraper::AllocateRigidbody(std::shared_ptr<CollisionShape> shape, bool hasInertia)
	{
		std::promise<RigidBody*> * promise = new std::promise<RigidBody*>();
		auto future = promise->get_future();
		MethodCommandReturn<PhysicsEngine, RigidBody*, std::shared_ptr<CollisionShape>, bool> * command = new MethodCommandReturn<PhysicsEngine, RigidBody*, std::shared_ptr<CollisionShape>, bool>(m_pe, &PhysicsEngine::AllocateRigidbody, promise, shape, hasInertia);
		m_queue->push((Command *)command);
		RigidBody * rb = future.get();
		delete promise;
		return new RigidWraper(rb, m_queue);
	}

	std::vector<RigidWraper*> PhysicsWraper::AllocateRigidbody(std::shared_ptr<CollisionShape> shape, int nb, bool hasInertia)
	{
		std::vector<std::promise<RigidBody*>*> promises(nb);
		std::vector<std::future<RigidBody*>> futures;
		futures.reserve(nb);
		for (int i = 0; i < nb; ++i) 
		{
			promises[i] = new std::promise<RigidBody*>();
			futures.push_back(promises[i]->get_future());
			auto command = new Ge::MethodCommandReturn<PhysicsEngine, RigidBody*, std::shared_ptr<CollisionShape>, bool>(m_pe, &PhysicsEngine::AllocateRigidbody, promises[i], shape, hasInertia);
			m_queue->push((Command*)command);
		}

		std::vector<RigidWraper*> results;
		results.reserve(nb);
		for (auto& future : futures) 
		{
			RigidBody* rb = future.get();
			results.push_back(new RigidWraper(rb, m_queue));
		}

		for (auto promise : promises) 
		{
			delete promise;
		}

		return results;
	}

	std::vector<RigidWraper*> PhysicsWraper::AllocateRigidbody(
		const std::vector<std::shared_ptr<CollisionShape>>& shapes,
		bool hasInertia)
	{
		std::vector<std::promise<RigidBody*>*> promises(shapes.size(), nullptr);
		std::vector<std::future<RigidBody*>> futures;
		futures.reserve(shapes.size());
		for (std::size_t index = 0; index < shapes.size(); ++index)
		{
			promises[index] = new std::promise<RigidBody*>();
			futures.push_back(promises[index]->get_future());
			auto* command = new MethodCommandReturn<
				PhysicsEngine,
				RigidBody*,
				std::shared_ptr<CollisionShape>,
				bool>(
					m_pe,
					&PhysicsEngine::AllocateRigidbody,
					promises[index],
					shapes[index],
					hasInertia);
			m_queue->push(static_cast<Command*>(command));
		}

		std::vector<RigidWraper*> results;
		results.reserve(shapes.size());
		for (std::future<RigidBody*>& future : futures)
		{
			RigidBody* body = future.get();
			results.push_back(body == nullptr ? nullptr : new RigidWraper(body, m_queue));
		}
		for (std::promise<RigidBody*>* promise : promises)
		{
			delete promise;
		}
		return results;
	}

	CollisionWraper* PhysicsWraper::AllocateCollision(std::shared_ptr<CollisionShape> shape)
	{
		std::promise<CollisionBody*>* promise = new std::promise<CollisionBody*>();
		auto future = promise->get_future();
		MethodCommandReturn<PhysicsEngine, CollisionBody*, std::shared_ptr<CollisionShape>>* command = new MethodCommandReturn<PhysicsEngine, CollisionBody*, std::shared_ptr<CollisionShape>>(m_pe, &PhysicsEngine::AllocateCollision, promise, shape);
		m_queue->push((Command*)command);
		CollisionBody* cb = future.get();
		delete promise;
		return new CollisionWraper(cb,m_queue);
	}

	std::vector<CollisionWraper*> PhysicsWraper::AllocateCollision(std::shared_ptr<CollisionShape> shape, int nb)
	{
		std::vector<std::promise<CollisionBody*>*> promises(nb);
		std::vector<std::future<CollisionBody*>> futures;
		futures.reserve(nb);
		for (int i = 0; i < nb; ++i) 
		{
			promises[i] = new std::promise<CollisionBody*>();
			futures.push_back(promises[i]->get_future());
			auto command = new Ge::MethodCommandReturn<PhysicsEngine, CollisionBody*, std::shared_ptr<CollisionShape>>(m_pe, &PhysicsEngine::AllocateCollision, promises[i], shape);
			m_queue->push((Command*)command);
		}

		std::vector<CollisionWraper*> results;
		results.reserve(nb);
		for (auto& future : futures) 
		{
			CollisionBody* cb = future.get();
			results.push_back(new CollisionWraper(cb,m_queue));
		}

		for (auto promise : promises) 
		{
			delete promise;
		}

		return results;
	}

	MuscleWraper* PhysicsWraper::AllocateMuscle(RigidWraper* rb1, RigidWraper* rb2, float degres, float scale, bool adaptePos)
	{
		std::promise<Muscle*>* promise = new std::promise<Muscle*>();
		auto future = promise->get_future();
		MethodCommandReturn<PhysicsEngine, Muscle*, RigidBody*, RigidBody*, float, float, bool>* command = new MethodCommandReturn<PhysicsEngine, Muscle*, RigidBody*, RigidBody*, float, float, bool>(m_pe, &PhysicsEngine::AllocateMuscle, promise, rb1->getRigidBody(), rb2->getRigidBody(), degres, scale, adaptePos);
		m_queue->push((Command*)command);
		Muscle* cb = future.get();
		delete promise;
		return new MuscleWraper(cb, m_queue);
	}

	void PhysicsWraper::ReleaseMuscle(MuscleWraper* pBody)
	{
		Muscle* muscle = pBody->getMuscle();
		MethodCommand<PhysicsEngine, Muscle*>* command = new MethodCommand<PhysicsEngine, Muscle*>(m_pe, &PhysicsEngine::ReleaseMuscle, muscle);
		m_queue->push((Command*)command);
		delete pBody;
	}

	StaticMeshCollider* PhysicsWraper::AllocateStaticMeshCollider(btBvhTriangleMeshShape* meshShape)
	{
		std::promise<StaticMeshCollider*>* promise = new std::promise<StaticMeshCollider*>();
		auto future = promise->get_future();
		using AllocateMethod = StaticMeshCollider* (PhysicsEngine::*)(
			btBvhTriangleMeshShape*);
		const AllocateMethod allocate = static_cast<AllocateMethod>(
			&PhysicsEngine::AllocateStaticMeshCollider);
		MethodCommandReturn<PhysicsEngine, StaticMeshCollider*, btBvhTriangleMeshShape*>* command = new MethodCommandReturn<PhysicsEngine, StaticMeshCollider*, btBvhTriangleMeshShape*>(m_pe, allocate, promise, meshShape);
		m_queue->push((Command*)command);
		StaticMeshCollider* smc = future.get();
		delete promise;
		return smc;
	}

	StaticMeshCollider* PhysicsWraper::AllocateStaticMeshCollider(
		btBvhTriangleMeshShape* meshShape,
		glm::vec3 position,
		glm::quat rotation,
		glm::vec3 scale,
		std::shared_ptr<void> resourceOwner)
	{
		std::promise<StaticMeshCollider*>* promise =
			new std::promise<StaticMeshCollider*>();
		auto future = promise->get_future();
		using AllocateMethod = StaticMeshCollider* (PhysicsEngine::*)(
			btBvhTriangleMeshShape*,
			glm::vec3,
			glm::quat,
			glm::vec3,
			std::shared_ptr<void>);
		const AllocateMethod allocate = static_cast<AllocateMethod>(
			&PhysicsEngine::AllocateStaticMeshCollider);
		auto* command = new MethodCommandReturn<
			PhysicsEngine,
			StaticMeshCollider*,
			btBvhTriangleMeshShape*,
			glm::vec3,
			glm::quat,
			glm::vec3,
			std::shared_ptr<void>>(
				m_pe,
				allocate,
				promise,
				meshShape,
				position,
				rotation,
				scale,
				std::move(resourceOwner));
		m_queue->push((Command*)command);
		StaticMeshCollider* collider = future.get();
		delete promise;
		return collider;
	}

	void PhysicsWraper::ReleaseStaticMeshCollider(StaticMeshCollider* pBody)
	{
		MethodCommand<PhysicsEngine, StaticMeshCollider*>* command = new MethodCommand<PhysicsEngine, StaticMeshCollider*>(m_pe, &PhysicsEngine::ReleaseStaticMeshCollider, pBody);
		m_queue->push((Command*)command);
	}

	CharacterControllerWraper* PhysicsWraper::allocateCharacterController(float radius, float height, float stepHeight)
	{
		std::promise<CharacterController*>* promise = new std::promise<CharacterController*>();
		auto future = promise->get_future();
		MethodCommandReturn<PhysicsEngine, CharacterController*,float, float, float>* command = new MethodCommandReturn<PhysicsEngine, CharacterController*,float, float, float>(m_pe, &PhysicsEngine::allocateCharacterController, promise, radius, height, stepHeight);
		m_queue->push((Command*)command);
		CharacterController* cb = future.get();
		delete promise;
		return new CharacterControllerWraper(cb, m_queue);
	}

	void PhysicsWraper::releaseCharacterController(CharacterControllerWraper* ccw)
	{
		CharacterController* cc = ccw->getCharacterController();
		MethodCommand<PhysicsEngine, CharacterController*>* command = new MethodCommand<PhysicsEngine, CharacterController*>(m_pe, &PhysicsEngine::releaseCharacterController, cc);
		m_queue->push((Command*)command);
		delete ccw;
	}

	void PhysicsWraper::AddRigidbody(RigidWraper* pbody, int group, int mask)
	{
		RigidBody * body = pbody->getRigidBody();
		MethodCommand<PhysicsEngine, RigidBody*, int, int>* command = new MethodCommand<PhysicsEngine, RigidBody*, int, int>(m_pe, &PhysicsEngine::AddRigidbody, body, group, mask);
		m_queue->push((Command*)command);
	}

	void PhysicsWraper::ReleaseRigidbody(RigidWraper* pBody)
	{
		RigidBody * body = pBody->getRigidBody();
		MethodCommand<PhysicsEngine, RigidBody*>* command = new MethodCommand<PhysicsEngine, RigidBody*>(m_pe, &PhysicsEngine::ReleaseRigidbody, body);
		m_queue->push((Command*)command);
		delete pBody;
	}

	void PhysicsWraper::AddCollision(CollisionWraper* pbody)
	{
		CollisionBody* body = pbody->getCollisionBody();
		MethodCommand<PhysicsEngine, CollisionBody*>* command = new MethodCommand<PhysicsEngine, CollisionBody*>(m_pe, &PhysicsEngine::AddCollision, body);
		m_queue->push((Command*)command);
	}

	void PhysicsWraper::ReleaseCollision(CollisionWraper* pBody)
	{
		CollisionBody* body = pBody->getCollisionBody();
		MethodCommand<PhysicsEngine, CollisionBody*>* command = new MethodCommand<PhysicsEngine, CollisionBody*>(m_pe, &PhysicsEngine::ReleaseCollision, body);
		m_queue->push((Command*)command);
		delete pBody;
	}

	btFixedConstraint* PhysicsWraper::weldRigidBodiesAuto(RigidWraper* bodyA, RigidWraper* bodyB)
	{
		std::promise<btFixedConstraint*>* promise = new std::promise<btFixedConstraint*>();
		auto future = promise->get_future();
		MethodCommandReturn<PhysicsEngine, btFixedConstraint*, RigidBody*, RigidBody*>* command = new MethodCommandReturn<PhysicsEngine, btFixedConstraint*, RigidBody*, RigidBody*>(m_pe, &PhysicsEngine::weldRigidBodiesAuto,promise, bodyA->getRigidBody(), bodyB->getRigidBody());
		m_queue->push((Command*)command);
		btFixedConstraint* smc = future.get();
		delete promise;
		return smc;
	}

	btPoint2PointConstraint* PhysicsWraper::linkRigidBodiesP2P(RigidWraper* bodyA, RigidWraper* bodyB, float rigid, float damping, float clamp)
	{
		std::promise<btPoint2PointConstraint*>* promise = new std::promise<btPoint2PointConstraint*>();
		auto future = promise->get_future();
		MethodCommandReturn<PhysicsEngine, btPoint2PointConstraint*, RigidBody*, RigidBody*, float, float, float>* command = new MethodCommandReturn<PhysicsEngine, btPoint2PointConstraint*, RigidBody*, RigidBody*, float, float, float>(m_pe, &PhysicsEngine::linkRigidBodiesP2P, promise, bodyA->getRigidBody(), bodyB->getRigidBody(), rigid, damping, clamp);
		m_queue->push((Command*)command);
		btPoint2PointConstraint* smc = future.get();
		delete promise;
		return smc;
	}

	btPoint2PointConstraint* PhysicsWraper::linkRigidBodiesP2PAt(
		RigidWraper* bodyA,
		RigidWraper* bodyB,
		glm::vec3 worldPivot,
		float rigid,
		float damping,
		float clamp)
	{
		if (bodyA == nullptr || bodyB == nullptr)
		{
			return nullptr;
		}
		std::promise<btPoint2PointConstraint*>* promise =
			new std::promise<btPoint2PointConstraint*>();
		auto future = promise->get_future();
		auto* command = new MethodCommandReturn<
			PhysicsEngine,
			btPoint2PointConstraint*,
			RigidBody*,
			RigidBody*,
			glm::vec3,
			float,
			float,
			float>(
				m_pe,
				&PhysicsEngine::linkRigidBodiesP2PAt,
				promise,
				bodyA->getRigidBody(),
				bodyB->getRigidBody(),
				worldPivot,
				rigid,
				damping,
				clamp);
		m_queue->push(static_cast<Command*>(command));
		btPoint2PointConstraint* constraint = future.get();
		delete promise;
		return constraint;
	}

	std::vector<btPoint2PointConstraint*> PhysicsWraper::linkRigidBodiesP2PAt(
		const std::vector<PointToPointLink>& links)
	{
		std::vector<std::promise<btPoint2PointConstraint*>*> promises(
			links.size(),
			nullptr);
		std::vector<std::future<btPoint2PointConstraint*>> futures;
		futures.reserve(links.size());
		for (std::size_t index = 0; index < links.size(); ++index)
		{
			const PointToPointLink& link = links[index];
			if (link.bodyA == nullptr || link.bodyB == nullptr)
			{
				futures.emplace_back();
				continue;
			}
			promises[index] = new std::promise<btPoint2PointConstraint*>();
			futures.push_back(promises[index]->get_future());
			auto* command = new MethodCommandReturn<
				PhysicsEngine,
				btPoint2PointConstraint*,
				RigidBody*,
				RigidBody*,
				glm::vec3,
				float,
				float,
				float>(
					m_pe,
					&PhysicsEngine::linkRigidBodiesP2PAt,
					promises[index],
					link.bodyA->getRigidBody(),
					link.bodyB->getRigidBody(),
					link.worldPivot,
					link.rigid,
					link.damping,
					link.clamp);
			m_queue->push(static_cast<Command*>(command));
		}

		std::vector<btPoint2PointConstraint*> results;
		results.reserve(links.size());
		for (std::size_t index = 0; index < links.size(); ++index)
		{
			if (promises[index] == nullptr)
			{
				results.push_back(nullptr);
				continue;
			}
			results.push_back(futures[index].get());
		}
		for (std::promise<btPoint2PointConstraint*>* promise : promises)
		{
			delete promise;
		}
		return results;
	}

	std::vector<btConeTwistConstraint*>
	PhysicsWraper::linkRigidBodiesConeTwistAt(
		const std::vector<ConeTwistLink>& links)
	{
		std::vector<std::promise<btConeTwistConstraint*>*> promises(
			links.size(),
			nullptr);
		std::vector<std::future<btConeTwistConstraint*>> futures;
		futures.reserve(links.size());
		for (std::size_t index = 0; index < links.size(); ++index)
		{
			const ConeTwistLink& link = links[index];
			if (link.bodyA == nullptr || link.bodyB == nullptr)
			{
				futures.emplace_back();
				continue;
			}
			promises[index] = new std::promise<btConeTwistConstraint*>();
			futures.push_back(promises[index]->get_future());
			auto* command = new MethodCommandReturn<
				PhysicsEngine,
				btConeTwistConstraint*,
				RigidBody*,
				RigidBody*,
				glm::vec3,
				glm::vec3,
				float,
				float,
				float,
				float,
				float,
				float>(
					m_pe,
					&PhysicsEngine::linkRigidBodiesConeTwistAt,
					promises[index],
					link.bodyA->getRigidBody(),
					link.bodyB->getRigidBody(),
					link.worldPivot,
					link.worldTwistAxis,
					link.swingSpan,
					link.twistSpan,
					link.softness,
					link.biasFactor,
					link.relaxationFactor,
					link.damping);
			m_queue->push(static_cast<Command*>(command));
		}

		std::vector<btConeTwistConstraint*> results;
		results.reserve(links.size());
		for (std::size_t index = 0; index < links.size(); ++index)
		{
			if (promises[index] == nullptr)
			{
				results.push_back(nullptr);
				continue;
			}
			results.push_back(futures[index].get());
		}
		for (std::promise<btConeTwistConstraint*>* promise : promises)
		{
			delete promise;
		}
		return results;
	}

	SliderWraper* PhysicsWraper::allocateSlider()
	{
		std::promise<SliderBody*>* promise = new std::promise<SliderBody*>();
		auto future = promise->get_future();
		MethodCommandReturn<PhysicsEngine, SliderBody*>* command = new MethodCommandReturn<PhysicsEngine, SliderBody*>(m_pe, &PhysicsEngine::allocateSlider, promise);
		m_queue->push((Command*)command);
		SliderBody* cb = future.get();
		delete promise;
		return new SliderWraper(cb, m_queue);
	}

	void PhysicsWraper::releaseSlider(SliderWraper* pBody)
	{
		MethodCommand<PhysicsEngine, SliderBody*>* command = new MethodCommand<PhysicsEngine, SliderBody*>(m_pe, &PhysicsEngine::releaseSlider, pBody->getSlider());
		m_queue->push((Command*)command);
		delete pBody;
	}

	void PhysicsWraper::destroyConstraint(btTypedConstraint* constraint)
	{
		MethodCommand<PhysicsEngine, btTypedConstraint*>* command = new MethodCommand<PhysicsEngine, btTypedConstraint*>(m_pe, &PhysicsEngine::destroyConstraint, constraint);
		m_queue->push((Command*)command);
	}

	bool PhysicsWraper::raycast(const glm::vec3* start, const glm::vec3* end, glm::vec3* hitPoint, glm::vec3* hitNormal)
	{
		std::promise<bool>* promise = new std::promise<bool>();
		auto future = promise->get_future();
		MethodCommandReturn<PhysicsEngine, bool, const glm::vec3*, const glm::vec3*, glm::vec3*, glm::vec3*>* command =
			new MethodCommandReturn<PhysicsEngine, bool, const glm::vec3*, const glm::vec3*, glm::vec3*, glm::vec3*>(
				m_pe, &PhysicsEngine::raycast, promise, start, end, hitPoint, hitNormal);
		m_queue->push((Command*)command);
		bool cb = future.get();
		delete promise;
		return cb;
	}

	bool PhysicsWraper::raycastStaticSurface(
		const glm::vec3* start,
		const glm::vec3* end,
		glm::vec3* hitPoint,
		glm::vec3* hitNormal,
		int* surfaceType)
	{
		std::promise<bool>* promise = new std::promise<bool>();
		auto future = promise->get_future();
		MethodCommandReturn<
			PhysicsEngine, bool,
			const glm::vec3*, const glm::vec3*, glm::vec3*, glm::vec3*, int*>* command =
			new MethodCommandReturn<
				PhysicsEngine, bool,
				const glm::vec3*, const glm::vec3*, glm::vec3*, glm::vec3*, int*>(
					m_pe, &PhysicsEngine::raycastStaticSurface, promise,
					start, end, hitPoint, hitNormal, surfaceType);
		m_queue->push((Command*)command);
		const bool hit = future.get();
		delete promise;
		return hit;
	}

	bool PhysicsWraper::raycastSurface(
		const glm::vec3* start,
		const glm::vec3* end,
		glm::vec3* hitPoint,
		glm::vec3* hitNormal,
		int* surfaceType,
		RigidBody** parentBody)
	{
		std::promise<bool>* promise = new std::promise<bool>();
		auto future = promise->get_future();
		MethodCommandReturn<
			PhysicsEngine, bool,
			const glm::vec3*, const glm::vec3*, glm::vec3*, glm::vec3*,
			int*, RigidBody**>* command =
			new MethodCommandReturn<
				PhysicsEngine, bool,
				const glm::vec3*, const glm::vec3*, glm::vec3*, glm::vec3*,
				int*, RigidBody**>(
					m_pe, &PhysicsEngine::raycastSurface, promise,
					start, end, hitPoint, hitNormal, surfaceType, parentBody);
		m_queue->push((Command*)command);
		const bool hit = future.get();
		delete promise;
		return hit;
	}

	bool PhysicsWraper::findCharacterExit(CharacterExitRequest request, glm::vec3* position)
	{
		if (m_pe == nullptr || m_queue == nullptr || position == nullptr) return false;
		// One command for the entire search, not one blocking round trip per ray.
		std::promise<bool> promise;
		auto future = promise.get_future();
		auto* command = new MethodCommandReturn<PhysicsEngine, bool, CharacterExitRequest, glm::vec3*>(
			m_pe, &PhysicsEngine::findCharacterExit, &promise, std::move(request), position);
		m_queue->push(static_cast<Command*>(command));
		return future.get();
	}

	bool PhysicsWraper::sweepSphere(const glm::vec3* start, const glm::vec3* end, float radius, glm::vec3* hitPoint, glm::vec3* hitNormal)
	{
		std::promise<bool>* promise = new std::promise<bool>();
		auto future = promise->get_future();
		MethodCommandReturn<PhysicsEngine, bool, const glm::vec3*, const glm::vec3*, float, glm::vec3*, glm::vec3*>* command =
			new MethodCommandReturn<PhysicsEngine, bool, const glm::vec3*, const glm::vec3*, float, glm::vec3*, glm::vec3*>(
				m_pe, &PhysicsEngine::sweepSphere, promise, start, end, radius, hitPoint, hitNormal);
		m_queue->push((Command*)command);
		bool cb = future.get();
		delete promise;
		return cb;
	}

	std::vector<ConvexHit> PhysicsWraper::testConvexShape(std::shared_ptr<CollisionShape> shape, const glm::vec3 position, const std::vector<const btCollisionObject*> ignoreList)
	{
		std::promise<std::vector<ConvexHit>>* promise = new std::promise<std::vector<ConvexHit>>();
		auto future = promise->get_future();
		MethodCommandReturn<PhysicsEngine, std::vector<ConvexHit>, std::shared_ptr<CollisionShape>, glm::vec3, std::vector<const btCollisionObject*>>* command = new MethodCommandReturn<PhysicsEngine, std::vector<ConvexHit>, std::shared_ptr<CollisionShape>, glm::vec3, std::vector<const btCollisionObject*>>(m_pe, &PhysicsEngine::testConvexShape, promise, shape, position, ignoreList);
		m_queue->push((Command*)command);
		std::vector<ConvexHit> cb = future.get();
		delete promise;
		return cb;
	}

	bool PhysicsWraper::isPointInsideCollision(const glm::vec3* point, float testRadius)
	{
		std::promise<bool>* promise = new std::promise<bool>();
		auto future = promise->get_future();
		MethodCommandReturn<PhysicsEngine, bool, const glm::vec3*, float>* command = new MethodCommandReturn<PhysicsEngine, bool, const glm::vec3*, float>(m_pe, &PhysicsEngine::isPointInsideCollision, promise, point, testRadius);
		m_queue->push((Command*)command);
		bool cb = future.get();
		delete promise;
		return cb;
	}

	void PhysicsWraper::DebugDrawCollider()
	{
		if (!m_debugDraw)
		{
			const ptrClass* pc = Engine::getPtrClassAddr();
			const std::vector<RigidBody*> m_rigidBody = m_pe->getRigidbody();
			const std::vector<CollisionBody*> m_collisionBody = m_pe->getCollisionbody();
			m_cubeShader = pc->graphiquePipelineManager->createPipeline("../Asset/Shader/iso.fs.glsl", "../Asset/Shader/iso.vs.glsl");
			m_shapeBuffer = pc->modelManager->allocateBuffer("../Asset/Model/cube.obj");
			m_material = pc->materialManager->createMaterial();
			pc->behaviourManager->addBehaviour(this);
			m_material->setPipeline(m_cubeShader);
			m_material->setRoughness(0.05f);
			m_material->setCastShadow(false);
			for (int i = 0; i < m_rigidBody.size(); i++)
			{
				const std::shared_ptr<CollisionShape> cs = m_rigidBody[i]->getCollisionShape();
				if (const BoxShape* bs = dynamic_cast<const BoxShape*>(cs.get()))
				{
					glm::vec3 scale = bs->getHalfExtents() *2.0f;
					Model * m = pc->modelManager->createModel(m_shapeBuffer);
					m->setMaterial(m_material);
					m->setScale(scale);
					m_models.push_back(m);					
				}
				else if (const CapsuleShape* cas = dynamic_cast<const CapsuleShape*>(cs.get()))
				{
					glm::vec3 scale = glm::vec3(0.5f) * 2.0f;
					Model* m = pc->modelManager->createModel(m_shapeBuffer);
					m->setMaterial(m_material);
					m->setScale(scale);
					m_models.push_back(m);
				}
			}		
			for (int i = 0; i < m_collisionBody.size(); i++)
			{
				const std::shared_ptr<CollisionShape> cs = m_collisionBody[i]->getCollisionShape();
				if (const BoxShape* bs = dynamic_cast<const BoxShape*>(cs.get()))
				{
					glm::vec3 scale = bs->getHalfExtents() * 2.0f;
					Model* m = pc->modelManager->createModel(m_shapeBuffer);
					m->setMaterial(m_material);
					m->setScale(scale);
					m_models.push_back(m);
				}
				else if (const CapsuleShape* cas = dynamic_cast<const CapsuleShape*>(cs.get()))
				{
					glm::vec3 scale = glm::vec3(0.5f) * 2.0f;
					Model* m = pc->modelManager->createModel(m_shapeBuffer);
					m->setMaterial(m_material);
					m->setScale(scale);
					m_models.push_back(m);
				}
			}
			m_debugDraw = true;
		}
	}

	void PhysicsWraper::DebugClearCollider()
	{
		if (m_debugDraw)
		{
			const ptrClass* pc = Engine::getPtrClassAddr();
			for (int i = 0; i < m_models.size(); i++)
			{
				pc->modelManager->destroyModel(m_models[i]);
			}
			m_models.clear();
			pc->materialManager->destroyMaterial(m_material);
			pc->modelManager->destroyBuffer(m_shapeBuffer);
			pc->graphiquePipelineManager->destroyPipeline(m_cubeShader);			
			pc->behaviourManager->removeBehaviour(this);
			m_debugDraw = false;
		}
	}

	void PhysicsWraper::start()
	{

	}

	void PhysicsWraper::fixedUpdate()
	{

	}

	void PhysicsWraper::update()
	{
		const std::vector<RigidBody*> m_rigidBody = m_pe->getRigidbody();
		for (int i = 0; i < m_rigidBody.size(); i++)
		{
			m_models[i]->setPosition(m_rigidBody[i]->getPosition());
			m_models[i]->setRotation(m_rigidBody[i]->getRotation());
		}
		const std::vector<CollisionBody*> m_collisionBody = m_pe->getCollisionbody();
		for (int i = 0; i < m_collisionBody.size(); i++)
		{
			m_models[i+ m_rigidBody.size()]->setPosition(m_collisionBody[i]->getPosition());
			m_models[i + m_rigidBody.size()]->setRotation(m_collisionBody[i]->getRotation());
		}
	}

	void PhysicsWraper::stop()
	{

	}

	void PhysicsWraper::onGUI()
	{

	}
}
