#include "MeshColliderMulti.hpp"
#define CLEAR_REGISTRY
#include "Behaviour.hpp"
#include "PathManager.hpp"
#include "ShapeBuffer.hpp"
#include "Vertex.hpp"
#include "Editor.hpp"

#include <glm/gtc/quaternion.hpp>
#include <cstddef>
#include <cmath>
#include <stdexcept>
#include <memory>
#include <utility>
#include <filesystem>
#include <algorithm>

namespace
{
    bool validCollisionMesh(const FBXMeshData& mesh)
    {
        if (mesh.vertices.size() < 3 || mesh.indices.size() < 3 ||
            mesh.indices.size() % 3 != 0) return false;
        for (const auto& vertex : mesh.vertices)
        {
            if (!std::isfinite(vertex.pos.x) || !std::isfinite(vertex.pos.y) ||
                !std::isfinite(vertex.pos.z)) return false;
        }
        for (const auto index : mesh.indices)
        {
            if (index >= mesh.vertices.size()) return false;
        }
        // Degenerate faces can coexist with usable imported faces, but a
        // mesh with no surface at all cannot provide scene collisions.
        for (std::size_t i = 0; i < mesh.indices.size(); i += 3)
        {
            const glm::vec3 a = mesh.vertices[mesh.indices[i]].pos;
            const glm::vec3 b = mesh.vertices[mesh.indices[i + 1]].pos;
            const glm::vec3 c = mesh.vertices[mesh.indices[i + 2]].pos;
            const glm::vec3 area = glm::cross(b - a, c - a);
            if (glm::dot(area, area) > 0.0f) return true;
        }
        return false;
    }
}

void MeshColliderMulti::start()
{
    m_pc = Engine::getPtrClassAddr();
    taskReady.store(false, std::memory_order_relaxed);
    m_loadFailed.store(false, std::memory_order_relaxed);
    m_loadingTicket = Ge::LoadingState::begin();
    try
    {
        std::string meshPath = meshStatic;
        std::replace(meshPath.begin(), meshPath.end(), '\\', '/');
        const std::string cp = (std::filesystem::path(PathManager::getHomeProjectPath()) /
            "Ressources" / meshPath).string();
        const bool fbx = cp.size() >= 4 && cp.compare(cp.size() - 4, 4, ".fbx") == 0;
        const std::vector<GObject*> objs = GObject::FindObjectsWithTag(targetTag);
        std::vector<glm::mat4> objectWorlds;
        objectWorlds.reserve(objs.size());
        for (GObject* obj : objs)
        {
            if (obj != nullptr) objectWorlds.push_back(obj->getModelMatrix());
        }

        // Only immutable transforms cross into the import worker.
        if (threading)
        {
            worker = std::thread([this, cp, fbx, objectWorlds]()
            {
                loadCapturedMeshData(cp, fbx, objectWorlds);
            });
        }
        else
        {
            loadCapturedMeshData(cp, fbx, objectWorlds);
        }
    }
    catch (...)
    {
        finishLoading(true);
    }
}

void MeshColliderMulti::loadMeshData(std::string cp, bool fbx, std::vector<GObject*> objs)
{
    // Compatibility entry point: capture scene data on the calling main thread.
    try
    {
        std::vector<glm::mat4> objectWorlds;
        objectWorlds.reserve(objs.size());
        for (GObject* obj : objs)
        {
            if (obj != nullptr) objectWorlds.push_back(obj->getModelMatrix());
        }
        loadCapturedMeshData(cp, fbx, objectWorlds);
    }
    catch (...)
    {
        finishLoading(true);
    }
}

void MeshColliderMulti::loadCapturedMeshData(const std::string& cp, bool fbx,
    const std::vector<glm::mat4>& objectWorlds) noexcept
{
    try
    {
        if (objectWorlds.empty())
        {
            // An empty tag selection requests no bodies or asset import.
            finishLoading();
            return;
        }
        m_fmds.clear();
        if (fbx)
        {
            m_fmds = m_pc->modelManager->loadFBXData(cp.c_str(), false, {});
        }
        else
        {
            FBXMeshData mesh;
            m_pc->modelManager->loadObjData(mesh.vertices, mesh.indices, cp.c_str(), false);
            m_fmds.push_back(std::move(mesh));
        }
        if (m_fmds.empty()) throw std::runtime_error("empty collision import");
        for (const FBXMeshData& mesh : m_fmds)
        {
            if (!validCollisionMesh(mesh))
                throw std::runtime_error("invalid collision mesh");
        }
        m_shape.resize(m_fmds.size() * objectWorlds.size());
        std::size_t shapeIndex = 0;
        for (const glm::mat4& objectWorld : objectWorlds)
        {
            for (const FBXMeshData& mesh : m_fmds)
            {
                std::shared_ptr<MeshCObjMulti> geometry(
                    new MeshCObjMulti(),
                    [](MeshCObjMulti* data)
                    {
                        delete data->bvh;
                        delete data->meshInterface;
                        delete data;
                    });
                geometry->indices = mesh.indices;
                geometry->triangleMesh.resize(mesh.vertices.size());
                for (std::size_t j = 0; j < mesh.vertices.size(); ++j)
                {
                    const glm::vec3 vertex = glm::vec3(
                        objectWorld * glm::vec4(mesh.vertices[j].pos, 1.0f));
                    if (!std::isfinite(vertex.x) || !std::isfinite(vertex.y) ||
                        !std::isfinite(vertex.z))
                        throw std::runtime_error("invalid collision transform");
                    geometry->triangleMesh[j] = btVector3(vertex.x, vertex.y, vertex.z);
                }
                geometry->meshInterface = new btTriangleIndexVertexArray(
                    static_cast<int>(geometry->indices.size() / 3),
                    reinterpret_cast<int*>(geometry->indices.data()),
                    3 * sizeof(int),
                    static_cast<int>(geometry->triangleMesh.size()),
                    reinterpret_cast<btScalar*>(geometry->triangleMesh.data()),
                    sizeof(btVector3));
                geometry->bvh = new btBvhTriangleMeshShape(geometry->meshInterface, true);
                geometry->bvh->setMargin(0.04f);
                m_shape[shapeIndex].smc = m_pc->physicsEngine->AllocateStaticMeshCollider(
                    geometry->bvh,
                    glm::vec3(0.0f),
                    glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
                    glm::vec3(1.0f),
                    geometry);
                if (m_shape[shapeIndex].smc == nullptr)
                    throw std::runtime_error("collision body creation failed");
                ++shapeIndex;
            }
        }
        m_fmds.clear();
    }
    catch (...)
    {
        finishLoading(true);
        return;
    }
    finishLoading();
}

void MeshColliderMulti::finishLoading(bool failed) noexcept
{
    m_loadFailed.store(failed, std::memory_order_relaxed);
    taskReady.store(true, std::memory_order_release);
    if (failed) m_loadingTicket.fail();
    else m_loadingTicket.complete();
}

void MeshColliderMulti::fixedUpdate()
{
}

void MeshColliderMulti::update()
{
    if (taskReady.load(std::memory_order_acquire) &&
        m_loadFailed.exchange(false, std::memory_order_relaxed))
    {
        Debug::Error("MeshColliderMulti: failed to load collision mesh %s", meshStatic.c_str());
    }
}

void MeshColliderMulti::stop()
{
    if (worker.joinable())
    {
        worker.join();
    }
    m_loadingTicket.complete();
    for (MeshCObjMulti& shape : m_shape)
    {
        if (shape.smc != nullptr)
        {
            // The queued body owns its BVH and arrays until Bullet removes it.
            m_pc->physicsEngine->ReleaseStaticMeshCollider(shape.smc);
            shape.smc = nullptr;
        }
    }
    m_shape.clear();
    m_fmds.clear();
}

void MeshColliderMulti::onGUI()
{
}
