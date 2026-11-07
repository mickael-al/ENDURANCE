#include "MeshCollider.hpp"
#define CLEAR_REGISTRY
#include "Behaviour.hpp"
#include "PathManager.hpp"
#include "ShapeBuffer.hpp"
#include "Vertex.hpp"
#include "Editor.hpp"

#include <glm/gtc/matrix_transform.hpp>
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

void MeshCollider::start()
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
        GObject* obj = Editor::instance()->getEntityByScript(this);
        if (obj == nullptr)
        {
            finishLoading();
            return;
        }

        // Scene transforms and editable offsets are main-thread data. The
        // worker receives immutable copies and never dereferences a GObject.
        const glm::mat4 objectWorld = obj->getModelMatrix();
        const std::vector<FbxTarget> offsets = tpos;
        if (threading)
        {
            worker = std::thread([this, cp, fbx, objectWorld, offsets]()
            {
                loadCapturedMeshData(cp, fbx, objectWorld, offsets);
            });
        }
        else
        {
            loadCapturedMeshData(cp, fbx, objectWorld, offsets);
        }
    }
    catch (...)
    {
        finishLoading(true);
    }
}

void MeshCollider::loadMeshData(std::string cp, bool fbx, GObject* obj)
{
    // Compatibility entry point: capture scene data on the calling main thread.
    try
    {
        if (obj == nullptr)
        {
            finishLoading();
            return;
        }
        loadCapturedMeshData(cp, fbx, obj->getModelMatrix(), tpos);
    }
    catch (...)
    {
        finishLoading(true);
    }
}

void MeshCollider::loadCapturedMeshData(const std::string& cp, bool fbx,
    const glm::mat4& objectWorld, const std::vector<FbxTarget>& offsets) noexcept
{
    try
    {
        std::vector<FBXMeshData> meshes;
        if (fbx)
        {
            meshes = m_pc->modelManager->loadFBXData(cp.c_str(), false, {});
        }
        else
        {
            FBXMeshData mesh;
            m_pc->modelManager->loadObjData(mesh.vertices, mesh.indices, cp.c_str(), false);
            meshes.push_back(std::move(mesh));
        }

        if (meshes.empty()) throw std::runtime_error("empty collision import");
        for (const FBXMeshData& mesh : meshes)
        {
            if (!validCollisionMesh(mesh))
                throw std::runtime_error("invalid collision mesh");
        }

        m_shape.resize(meshes.size());
        for (std::size_t i = 0; i < meshes.size(); ++i)
        {
            // Bullet releases static bodies later on its own thread. The body
            // owns the BVH and backing arrays until that removal has executed.
            std::shared_ptr<MeshCObj> geometry(
                new MeshCObj(),
                [](MeshCObj* data)
                {
                    delete data->bvh;
                    delete data->meshInterface;
                    delete data;
                });
            geometry->fmd = std::move(meshes[i]);
            geometry->triangleMesh.resize(geometry->fmd.vertices.size());

            glm::mat4 meshWorld = objectWorld;
            if (fbx && i < offsets.size())
            {
                meshWorld = glm::translate(glm::mat4(1.0f), offsets[i].pos) * objectWorld;
            }
            for (std::size_t j = 0; j < geometry->fmd.vertices.size(); ++j)
            {
                const glm::vec3 vertex = glm::vec3(
                    meshWorld * glm::vec4(geometry->fmd.vertices[j].pos, 1.0f));
                if (!std::isfinite(vertex.x) || !std::isfinite(vertex.y) ||
                    !std::isfinite(vertex.z))
                    throw std::runtime_error("invalid collision transform");
                geometry->triangleMesh[j] = btVector3(vertex.x, vertex.y, vertex.z);
            }
            geometry->fmd.vertices.clear();

            geometry->meshInterface = new btTriangleIndexVertexArray(
                static_cast<int>(geometry->fmd.indices.size() / 3),
                reinterpret_cast<int*>(geometry->fmd.indices.data()),
                3 * sizeof(int),
                static_cast<int>(geometry->triangleMesh.size()),
                reinterpret_cast<btScalar*>(geometry->triangleMesh.data()),
                sizeof(btVector3));
            geometry->bvh = new btBvhTriangleMeshShape(geometry->meshInterface, true);
            geometry->bvh->setMargin(0.04f);

            m_shape[i].smc = m_pc->physicsEngine->AllocateStaticMeshCollider(
                geometry->bvh,
                glm::vec3(0.0f),
                glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
                glm::vec3(1.0f),
                geometry);
            if (m_shape[i].smc == nullptr)
                throw std::runtime_error("collision body creation failed");
        }
    }
    catch (...)
    {
        finishLoading(true);
        return;
    }
    finishLoading();
}

void MeshCollider::finishLoading(bool failed) noexcept
{
    m_loadFailed.store(failed, std::memory_order_relaxed);
    taskReady.store(true, std::memory_order_release);
    if (failed) m_loadingTicket.fail();
    else m_loadingTicket.complete();
}

void MeshCollider::fixedUpdate()
{
}

void MeshCollider::update()
{
    if (taskReady.load(std::memory_order_acquire) &&
        m_loadFailed.exchange(false, std::memory_order_relaxed))
    {
        Debug::Error("MeshCollider: failed to load collision mesh %s", meshStatic.c_str());
    }
}

void MeshCollider::stop()
{
    if (worker.joinable())
    {
        worker.join();
    }
    m_loadingTicket.complete();
    for (MeshCObj& shape : m_shape)
    {
        if (shape.smc != nullptr)
        {
            m_pc->physicsEngine->ReleaseStaticMeshCollider(shape.smc);
            shape.smc = nullptr;
        }
    }
    m_shape.clear();
}

void MeshCollider::onGUI()
{
}
