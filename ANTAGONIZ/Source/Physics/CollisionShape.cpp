#include "CollisionShape.hpp"

#include <algorithm>

namespace Ge
{
    CollisionShape::CollisionShape(float mass) : m_mass(mass)
    {
    }

    void CollisionShape::SetMass(float mass)
    {
        m_mass = mass;
    }

    glm::vec3 CollisionShape::GetInertia(void) const
    {
        btVector3 inertia;
        m_pShape->calculateLocalInertia(m_mass, inertia);

        return glm::vec3( inertia.x(), inertia.y(), inertia.z() );
    }

    float CollisionShape::GetMass(void) const
    {
        return m_mass;
    }

     
    btCollisionShape* CollisionShape::GetShape() const
    {
        return m_pShape;
    }

    CollisionShape::~CollisionShape()
    {
        delete m_pShape;
    }

    BoxShape::BoxShape(glm::vec3 halfExtents, float mass) : CollisionShape(mass), m_halfExtents(halfExtents)
    {        
        m_pShape = new btBoxShape(btVector3(halfExtents.x, halfExtents.y, halfExtents.z));    
    }

    CompoundBoxShape::CompoundBoxShape(
        const std::vector<CompoundBoxPart>& boxes,
        float mass)
        : CollisionShape(mass)
    {
        btCompoundShape* compound = new btCompoundShape(true);
        m_pShape = compound;
        m_childShapes.reserve(boxes.size());

        for (const CompoundBoxPart& box : boxes)
        {
            const glm::vec3 halfExtents(
                (std::max)(box.halfExtents.x, 0.001f),
                (std::max)(box.halfExtents.y, 0.001f),
                (std::max)(box.halfExtents.z, 0.001f));
            btBoxShape* child = new btBoxShape(btVector3(
                halfExtents.x,
                halfExtents.y,
                halfExtents.z));
            btTransform childTransform;
            childTransform.setIdentity();
            childTransform.setOrigin(btVector3(
                box.center.x,
                box.center.y,
                box.center.z));
            compound->addChildShape(childTransform, child);
            m_childShapes.push_back(child);
        }
    }

    CompoundBoxShape::~CompoundBoxShape()
    {
        for (btCollisionShape* child : m_childShapes)
        {
            delete child;
        }
        m_childShapes.clear();
    }

    SphereShape::SphereShape(float radius, float mass) : CollisionShape(mass), m_radius(radius)
    {
        m_pShape = new btSphereShape(radius);
    }

    CapsuleShape::CapsuleShape(float radius, float height, float mass) : CollisionShape(mass), m_radius(radius), m_height(height)
    {
        m_pShape = new btCapsuleShape(radius, height);                
    }

    CylinderShape::CylinderShape(glm::vec3 halfExtents, float mass) : CollisionShape(mass), m_halfExtents(halfExtents)
    {
        m_pShape = new btCylinderShape(
            btVector3(
                halfExtents.x,
                halfExtents.y,
                halfExtents.z));
    }

    /*VertexShape::VertexShape(float mass) :
        CollisionShape(mass)
    {
       m_triangleMesh   = new btTriangleMesh();
    }

    VertexShape::VertexShape(float mass, std::vector<Vertex> const& m_triangle) : CollisionShape(mass)
    {
        m_triangleMesh = new btTriangleMesh();
        SetTriangles(m_triangle);
    }

    void VertexShape::SetTriangles(std::vector<Vertex> const& verteces)
    {
        size_t size = verteces.size();

        for (size_t i = 0ul; i < size; i += 3)
        {
            m_triangleMesh->addTriangle( 
                btVector3(verteces[i].pos.x, verteces[i].pos.y, verteces[i].pos.z),
                btVector3(verteces[i + 1].pos.x, verteces[i+ 1].pos.y, verteces[i+ 1].pos.z),
                btVector3(verteces[i + 2].pos.x, verteces[i+ 2].pos.y, verteces[i+ 2].pos.z)
                );
        }
    
        if (m_pShape != nullptr)
        {
            delete m_pShape;
        }

        m_pShape = new btBvhTriangleMeshShape(m_triangleMesh, true, true);
    }

    VertexShape::~VertexShape()
    {
        delete m_triangleMesh;
    }
    */
}
