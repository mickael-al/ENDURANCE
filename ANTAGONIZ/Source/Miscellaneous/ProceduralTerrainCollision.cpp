#include "glcore.hpp"
#include "ProceduralTerrainCollision.hpp"
#define CLEAR_REGISTRY
#include "PhysicsWraper.hpp"
#include "StaticMeshCollider.hpp"
#include <BulletCollision/CollisionShapes/btTriangleIndexVertexArray.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <future>
#include <limits>
#include <map>
#include <utility>

namespace ProceduralTerrain
{
    namespace
    {
        using Key = std::array<float, 4>;
        using PackedVertex = std::array<std::uint32_t, 3>;
        static_assert(sizeof(PackedVertex) == 12, "Terrain vertex ABI mismatch");

        Key keyOf(const MeshHandle& mesh)
        {
            return {mesh.origin[0], mesh.origin[1], mesh.origin[2], mesh.extent};
        }

        bool finiteMesh(const MeshHandle& mesh)
        {
            return std::isfinite(mesh.extent) && mesh.extent > 0.0f &&
                std::isfinite(mesh.origin[0]) && std::isfinite(mesh.origin[1]) &&
                std::isfinite(mesh.origin[2]);
        }

        float distanceSquared(const Key& key, const glm::vec3& position)
        {
            float result = 0.0f;
            for (int axis = 0; axis < 3; ++axis)
            {
                const float delta = (std::max)(key[axis] - position[axis],
                    (std::max)(position[axis] - key[axis] - key[3], 0.0f));
                result += delta * delta;
            }
            return result;
        }

        struct Geometry
        {
            std::vector<btVector3> vertices;
            std::vector<int> indices;
            std::unique_ptr<btTriangleIndexVertexArray> triangles;
            std::unique_ptr<btBvhTriangleMeshShape> shape;
            // Destruction order: shape, triangle interface, backing arrays.
        };

        struct BuildResult
        {
            Ge::StaticMeshCollider* collider = nullptr;
            bool success = false;
            const char* error = nullptr;
        };

        BuildResult buildCollision(Ge::PhysicsWraper* physics, MeshHandle mesh,
                                   std::vector<PackedVertex> packed) noexcept
        {
            try
            {
                auto geometry = std::make_shared<Geometry>();
                geometry->vertices.reserve(packed.size());
                geometry->indices.reserve(packed.size());
                // Match the renderer's canonical world-coordinate FMA. This
                // cache only receives LOD 0, so extent is the base chunk extent.
                // Using world vertices and an identity body avoids an extra
                // subtract/add of the chunk origin rounding a border again.
                const float scale = mesh.extent / 65534.0f;
                const std::array<float, 3> base{
                    float(mesh.baseChunkCoordinate[0]), float(mesh.baseChunkCoordinate[1]),
                    float(mesh.baseChunkCoordinate[2])};
                for (std::size_t triangle = 0; triangle + 2 < packed.size(); triangle += 3)
                {
                    btVector3 points[3];
                    for (unsigned int corner = 0; corner < 3; ++corner)
                    {
                        const auto& v = packed[triangle + corner];
                        points[corner] = btVector3(
                            std::fma(base[0], 65534.0f, float(v[0] & 65535u)) * scale,
                            std::fma(base[1], 65534.0f, float(v[0] >> 16u)) * scale,
                            std::fma(base[2], 65534.0f, float(v[1] & 65535u)) * scale);
                    }
                    // Transition tables and quantization may produce collapsed
                    // triangles. They carry no collision surface.
                    if ((points[1] - points[0]).cross(points[2] - points[0]).length2() == 0)
                        continue;
                    for (const auto& point : points)
                    {
                        geometry->indices.push_back(static_cast<int>(geometry->vertices.size()));
                        geometry->vertices.push_back(point);
                    }
                }
                if (geometry->indices.empty()) return {nullptr, true};
                geometry->triangles = std::make_unique<btTriangleIndexVertexArray>(
                    static_cast<int>(geometry->indices.size() / 3),
                    geometry->indices.data(), 3 * sizeof(int),
                    static_cast<int>(geometry->vertices.size()),
                    reinterpret_cast<btScalar*>(geometry->vertices.data()), sizeof(btVector3));
                geometry->shape = std::make_unique<btBvhTriangleMeshShape>(geometry->triangles.get(), true);
                geometry->shape->setMargin(0.02f);
                // This wrapper waits for the physics thread. Execute it in this
                // worker, never in the render/update thread.
                auto* collider = physics->AllocateStaticMeshCollider(geometry->shape.get(),
                    glm::vec3(0.0f),
                    glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::vec3(1.0f), geometry);
                return {collider, collider != nullptr,
                    collider ? nullptr : "Physics engine rejected the terrain collider"};
            }
            catch (...) { return {nullptr, false, "CPU terrain collision build or physics allocation failed"}; }
        }
    }

    struct CollisionCache::Impl
    {
        struct Record
        {
            Ge::StaticMeshCollider* collider = nullptr;
            std::uint32_t slot = UINT32_MAX;
            std::uint32_t generation = 0;
        };

        struct Pending
        {
            GLuint staging = 0;
            std::size_t capacity = 0;
            GLsync fence = nullptr;
            MeshHandle mesh;
            bool occupied = false;
            std::future<BuildResult> build;
        };

        Ge::PhysicsWraper* physics = nullptr;
        CollisionConfig config;
        CollisionStats counters;
        std::map<Key, Record> records;
        std::map<Key, MeshHandle> rejected;
        std::vector<Pending> pending;
        std::string error;

        bool retained(const Key& key, const glm::vec3& position) const
        {
            const float keepRadius = config.radius + key[3];
            return distanceSquared(key, position) <= keepRadius * keepRadius;
        }

        void retire(Record& record) noexcept
        {
            if (record.collider) physics->ReleaseStaticMeshCollider(record.collider);
            record.collider = nullptr;
        }

        void reject(const MeshHandle& mesh, const char* reason)
        {
            rejected[keyOf(mesh)] = mesh;
            ++counters.failedChunks;
            error = reason ? reason : "Terrain collision generation failed";
            error += " (chunk " + std::to_string(mesh.origin[0]) + ", " +
                std::to_string(mesh.origin[1]) + ", " + std::to_string(mesh.origin[2]) + ")";
        }

        void finish(Pending& job, BuildResult result, const glm::vec3& position)
        {
            const Key key = keyOf(job.mesh);
            auto found = records.find(key);
            if (result.success && retained(key, position))
            {
                if (found != records.end())
                {
                    // The replacement is already in Bullet before old removal.
                    retire(found->second);
                    found->second = {result.collider, job.mesh.slot, job.mesh.generation};
                }
                else if (records.size() < config.maxResidentChunks)
                    records.emplace(key, Record{result.collider, job.mesh.slot, job.mesh.generation});
                else if (result.collider)
                    physics->ReleaseStaticMeshCollider(result.collider);
                ++counters.completedChunks;
            }
            else
            {
                if (result.collider) physics->ReleaseStaticMeshCollider(result.collider);
                if (!result.success) reject(job.mesh, result.error);
            }
            job.occupied = false;
        }
    };

    CollisionCache::CollisionCache() : m_impl(std::make_unique<Impl>()) {}
    CollisionCache::~CollisionCache() { shutdown(); }

    bool CollisionCache::initialize(Ge::PhysicsWraper* physics, const CollisionConfig& config)
    {
        shutdown();
        if (!physics || !std::isfinite(config.radius) || config.radius <= 0.0f ||
            config.maxResidentChunks == 0 || config.maxResidentChunks > 4096 ||
            config.maxPendingReadbacks == 0 || config.maxPendingReadbacks > 8 ||
            config.maxCopiesPerFrame == 0 || config.maxReadbackMiB == 0 ||
            config.maxReadbackMiB > 128) return false;
        m_impl->physics = physics;
        m_impl->config = config;
        m_impl->counters = {};
        m_impl->error.clear();
        m_impl->pending.resize(config.maxPendingReadbacks);
        return true;
    }

    void CollisionCache::shutdown() noexcept
    {
        if (!m_impl || !m_impl->physics) return;
        for (auto& job : m_impl->pending)
        {
            if (job.fence) glDeleteSync(job.fence);
            if (job.staging) glDeleteBuffers(1, &job.staging);
            if (job.build.valid())
            {
                // Scene teardown occurs while the physics command queue runs.
                // Joining here prevents a late worker from publishing a body
                // after this cache has been destroyed.
                try
                {
                    const auto result = job.build.get();
                    if (result.collider) m_impl->physics->ReleaseStaticMeshCollider(result.collider);
                }
                catch (...) {}
            }
        }
        for (auto& entry : m_impl->records) m_impl->retire(entry.second);
        m_impl->records.clear();
        m_impl->rejected.clear();
        m_impl->pending.clear();
        m_impl->physics = nullptr;
    }

    void CollisionCache::update(const glm::vec3& position, const std::vector<MeshHandle>& meshes,
                                const GpuMesher& mesher)
    {
        auto& state = *m_impl;
        if (!state.physics || !std::isfinite(position.x) || !std::isfinite(position.y) ||
            !std::isfinite(position.z)) return;
        state.counters.capacityLimited = false;

        for (auto it = state.records.begin(); it != state.records.end();)
        {
            if (!state.retained(it->first, position))
            {
                state.retire(it->second);
                it = state.records.erase(it);
            }
            else ++it;
        }
        for (auto it = state.rejected.begin(); it != state.rejected.end();)
        {
            if (!mesher.valid(it->second) || !state.retained(it->first, position))
                it = state.rejected.erase(it);
            else ++it;
        }

        for (auto& job : state.pending)
        {
            if (!job.occupied) continue;
            if (job.build.valid())
            {
                if (job.build.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
                    state.finish(job, job.build.get(), position);
                continue;
            }
            const GLenum result = glClientWaitSync(job.fence, 0, 0);
            if (result == GL_TIMEOUT_EXPIRED) continue;
            glDeleteSync(job.fence);
            job.fence = nullptr;
            if (result == GL_WAIT_FAILED)
            {
                state.finish(job, {nullptr, false, "GPU terrain collision readback fence failed"}, position);
                continue;
            }
            if (!state.retained(keyOf(job.mesh), position))
            {
                job.occupied = false;
                continue;
            }
            try
            {
                std::vector<PackedVertex> data(job.mesh.vertexCount);
                // The fence guarantees the staging copy is complete; this read
                // never waits for mesh generation or the source VBO to become idle.
                glGetNamedBufferSubData(job.staging, 0,
                    static_cast<GLsizeiptr>(data.size() * sizeof(PackedVertex)), data.data());
                job.build = std::async(std::launch::async, buildCollision,
                    state.physics, job.mesh, std::move(data));
            }
            catch (...)
            {
                // Keep the previous collider on allocation/thread failure.
                // In particular, never leave an occupied slot without a fence.
                state.finish(job, {nullptr, false, "Terrain collision CPU readback allocation or worker launch failed"}, position);
            }
        }

        std::vector<const MeshHandle*> candidates;
        const float radiusSquared = state.config.radius * state.config.radius;
        for (const auto& mesh : meshes)
        {
            if (mesh.lod != 0 || mesh.vertexCount == 0 || mesh.vertexCount % 3 != 0 ||
                !finiteMesh(mesh) || !mesher.valid(mesh)) continue;
            const Key key = keyOf(mesh);
            if (distanceSquared(key, position) > radiusSquared) continue;
            const auto rejected = state.rejected.find(key);
            if (rejected != state.rejected.end() && rejected->second.slot == mesh.slot &&
                rejected->second.generation == mesh.generation) continue;
            const auto existing = state.records.find(key);
            if (existing != state.records.end() && existing->second.slot == mesh.slot &&
                existing->second.generation == mesh.generation) continue;
            if (std::any_of(state.pending.begin(), state.pending.end(), [&](const Impl::Pending& job)
                { return job.occupied && keyOf(job.mesh) == key; })) continue;
            candidates.push_back(&mesh);
        }
        std::sort(candidates.begin(), candidates.end(), [&](const MeshHandle* a, const MeshHandle* b)
            { return distanceSquared(keyOf(*a), position) < distanceSquared(keyOf(*b), position); });

        std::uint32_t submitted = 0;
        for (const auto* mesh : candidates)
        {
            if (submitted >= state.config.maxCopiesPerFrame) break;
            if (std::any_of(state.pending.begin(), state.pending.end(), [&](const Impl::Pending& job)
                { return job.occupied && keyOf(job.mesh) == keyOf(*mesh); })) continue;
            auto freeSlot = std::find_if(state.pending.begin(), state.pending.end(),
                [](const Impl::Pending& job) { return !job.occupied; });
            if (freeSlot == state.pending.end()) break;
            const std::size_t bytes = std::size_t(mesh->vertexCount) * sizeof(PackedVertex);
            if (bytes > std::size_t(state.config.maxReadbackMiB) * 1024 * 1024 ||
                mesh->vertexCount > static_cast<std::uint32_t>((std::numeric_limits<int>::max)()))
            {
                state.reject(*mesh, "Terrain collision mesh exceeds the per-chunk CPU readback budget");
                continue;
            }
            const auto pendingNew = std::count_if(state.pending.begin(), state.pending.end(),
                [&](const Impl::Pending& job)
                { return job.occupied && state.records.find(keyOf(job.mesh)) == state.records.end(); });
            if (state.records.find(keyOf(*mesh)) == state.records.end() &&
                state.records.size() + static_cast<std::size_t>(pendingNew) >= state.config.maxResidentChunks)
            {
                // Hysteresis must not let a distant retained collider prevent
                // creation of a closer one when the local cache fills up.
                auto farthest = state.records.end();
                float farthestDistance = distanceSquared(keyOf(*mesh), position);
                for (auto it = state.records.begin(); it != state.records.end(); ++it)
                {
                    const float distance = distanceSquared(it->first, position);
                    const bool replacementPending = std::any_of(state.pending.begin(), state.pending.end(),
                        [&](const Impl::Pending& job) { return job.occupied && keyOf(job.mesh) == it->first; });
                    if (distance > farthestDistance && !replacementPending)
                    { farthest = it; farthestDistance = distance; }
                }
                if (farthest == state.records.end())
                {
                    state.counters.capacityLimited = true;
                    continue;
                }
                state.retire(farthest->second);
                state.records.erase(farthest);
            }
            auto& job = *freeSlot;
            if (!job.staging) glCreateBuffers(1, &job.staging);
            if (!job.staging)
            {
                state.reject(*mesh, "GPU terrain collision staging buffer creation failed");
                continue;
            }
            if (job.capacity < bytes)
            {
                glNamedBufferData(job.staging, static_cast<GLsizeiptr>(bytes), nullptr, GL_STREAM_READ);
                GLint64 allocated = 0;
                glGetNamedBufferParameteri64v(job.staging, GL_BUFFER_SIZE, &allocated);
                job.capacity = allocated > 0 ? static_cast<std::size_t>(allocated) : 0;
                if (job.capacity < bytes)
                {
                    state.reject(*mesh, "GPU terrain collision staging buffer allocation failed");
                    continue;
                }
            }
            // DSA leaves every engine buffer binding untouched. Subsequent
            // retirement/reuse of the source range is ordered after this copy.
            glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);
            glCopyNamedBufferSubData(mesher.vertexBuffer(), job.staging,
                static_cast<GLintptr>(std::size_t(mesh->firstVertex) * sizeof(PackedVertex)), 0,
                static_cast<GLsizeiptr>(bytes));
            job.fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
            if (!job.fence)
            {
                state.reject(*mesh, "GPU terrain collision readback fence creation failed");
                continue;
            }
            job.mesh = *mesh;
            job.occupied = true;
            state.counters.copiedBytes += bytes;
            ++submitted;
        }
        if (submitted) glFlush();
    }

    CollisionStats CollisionCache::stats() const noexcept
    {
        CollisionStats result = m_impl->counters;
        result.residentChunks = static_cast<std::uint32_t>(m_impl->records.size());
        for (const auto& job : m_impl->pending)
        {
            if (!job.occupied) continue;
            if (job.build.valid()) ++result.pendingBuilds;
            else ++result.pendingReadbacks;
        }
        return result;
    }

    const std::string& CollisionCache::lastError() const noexcept
    {
        return m_impl->error;
    }
}
