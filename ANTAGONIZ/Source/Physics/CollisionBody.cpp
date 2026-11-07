#include "CollisionBody.hpp"

namespace Ge
{

    CollisionBody::CollisionBody(btDynamicsWorld* world) : m_pWorld(world), GObject(false)
    {
        setName("CollisionBody");
    }

    CollisionBody::CollisionBody(btDynamicsWorld* world, std::shared_ptr<CollisionShape> shape) : m_pWorld(world), GObject(false)
    {
        setName("CollisionBody");        
        Build(shape);
    }

    void CollisionBody::setPosition(glm::vec3 pos)
    {
		if (m_pBody == nullptr)
		{
			return;
		}
		setTransform(pos, getRotation());
    }
    
    void CollisionBody::setScale(glm::vec3 scale)
    {
		if (m_pShape == nullptr || m_pShape->GetShape() == nullptr)
		{
			return;
		}
        m_pShape->GetShape()->setLocalScaling(btVector3(scale.x, scale.y, scale.z));
		if (m_pWorld != nullptr && m_pBody != nullptr &&
			m_pBody->getBroadphaseHandle() != nullptr)
		{
			m_pWorld->updateSingleAabb(m_pBody);
		}
    }

    glm::vec3 CollisionBody::getScale() const
    {
        btVector3 vec = m_pShape->GetShape()->getLocalScaling();
        return glm::vec3(vec.x(), vec.y(), vec.z());
    }

    void CollisionBody::setRotation(glm::quat rot)
    {
		if (m_pBody == nullptr)
		{
			return;
		}
		setTransform(getPosition(), rot);
	}

	void CollisionBody::setTransform(glm::vec3 pos, glm::quat rot)
	{
		if (m_pBody == nullptr)
		{
			return;
		}
		const float lengthSquared = glm::dot(rot, rot);
		if (lengthSquared <= 0.000001f)
		{
			rot = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
		}
		else
		{
			rot = glm::normalize(rot);
		}
		m_transform.position = pos;
		m_transform.rotation = rot;
		m_eulerAngles = GObject::getEulerAngles();
		btTransform transform;
		transform.setIdentity();
		transform.setOrigin(btVector3(pos.x, pos.y, pos.z));
		transform.setRotation(btQuaternion(rot.x, rot.y, rot.z, rot.w));
		m_pBody->setWorldTransform(transform);
		m_pBody->setInterpolationWorldTransform(transform);
		if (m_pWorld != nullptr && m_pBody->getBroadphaseHandle() != nullptr)
		{
			m_pWorld->updateSingleAabb(m_pBody);
		}
    }

    void CollisionBody::setEulerAngles(glm::vec3 eul)
    {
		if (m_pBody == nullptr)
		{
			return;
		}
		setTransform(getPosition(), glm::quat(glm::radians(eul)));
    }

    glm::vec3 CollisionBody::getPosition() const
    {
        btVector3 pos = m_pBody->getWorldTransform().getOrigin();
        return glm::vec3(pos.getX(), pos.getY(), pos.getZ());
    }
    glm::quat CollisionBody::getRotation() const
    {
        btQuaternion rot = m_pBody->getWorldTransform().getRotation();
        return glm::quat(rot.w(), rot.x(), rot.y(), rot.z());
    }

    glm::vec3 CollisionBody::getEulerAngles()
    {
        m_transform.rotation = getRotation();
        m_eulerAngles = GObject::getEulerAngles();
        return GObject::getEulerAngles();
    }

    void CollisionBody::Build(std::shared_ptr<CollisionShape> shape)
    {
        if (IsInitialized())
        {
            return;
        }                
        m_pShape = shape;
        m_pBody = new btCollisionObject();
        m_pBody->setCollisionShape(m_pShape->GetShape());
    }

    void CollisionBody::setFriction(float friction)
    {
        m_pBody->setFriction(friction);
    }

    void CollisionBody::setRestitution(float restitution)
    {
        m_pBody->setRestitution(restitution);
    }

    bool CollisionBody::IsInitialized()
    {
        return m_pBody != nullptr;
    }

    const std::shared_ptr<CollisionShape> CollisionBody::getCollisionShape() const
    {
        return m_pShape;
    }

    void CollisionBody::mapMemory() {}

    CollisionBody::~CollisionBody(void)
    {        
        delete m_pBody;
    }

}