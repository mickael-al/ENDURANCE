#include "glcore.hpp"
#include "ProceduralTerrainCollision.hpp"
#define CLEAR_REGISTRY
#include "PhysicsWraper.hpp"
#include "StaticMeshCollider.hpp"
#include <BulletCollision/CollisionShapes/btTriangleIndexVertexArray.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <future>
#include <limits>
#include <map>
#include <mutex>
#include <set>
#include <stdexcept>
#include <thread>
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
            bool cancelled = false;
        };

        struct BuildInput
        {
            const PackedVertex* vertices = nullptr;
            std::size_t count = 0;
            std::atomic<bool> consumed{false}, cancelled{false};
        };

        class BuildWorkers
        {
            using Task=std::packaged_task<BuildResult()>;
            std::mutex mutex;
            std::condition_variable wake;
            std::deque<Task> tasks;
            std::vector<std::thread> threads;
            bool stopping=false;
            void stop() noexcept
            {
                { std::lock_guard<std::mutex> lock(mutex); stopping=true; }
                wake.notify_all();
                for (auto& worker:threads) if (worker.joinable()) worker.join();
            }
        public:
            explicit BuildWorkers(std::uint32_t count)
            {
                try
                {
                    threads.reserve(count);
                    for (std::uint32_t i=0;i<count;++i) threads.emplace_back([this]
                    {
                        for (;;)
                        {
                            Task task;
                            {
                                std::unique_lock<std::mutex> lock(mutex);
                                wake.wait(lock,[this]{return stopping || !tasks.empty();});
                                if (tasks.empty()) return;
                                task=std::move(tasks.front()); tasks.pop_front();
                            }
                            task();
                        }
                    });
                }
                catch (...) { stop(); throw; }
            }
            ~BuildWorkers() { stop(); }
            template<class Function> std::future<BuildResult> submit(Function&& function)
            {
                Task task(std::forward<Function>(function));
                auto future=task.get_future();
                {
                    std::lock_guard<std::mutex> lock(mutex);
                    if (stopping) throw std::runtime_error("Terrain collision workers stopped");
                    tasks.push_back(std::move(task));
                }
                wake.notify_one();
                return future;
            }
        };

        BuildResult buildCollision(Ge::PhysicsWraper* physics, MeshHandle mesh,
                                   const std::shared_ptr<BuildInput>& input) noexcept
        {
            struct ReleaseInput
            {
                BuildInput& value;
                ~ReleaseInput() { value.consumed.store(true,std::memory_order_release); }
            } release{*input};
            try
            {
                if (input->cancelled.load(std::memory_order_acquire)) return {nullptr,true,nullptr,true};
                auto geometry = std::make_shared<Geometry>();
                geometry->vertices.reserve(input->count);
                geometry->indices.reserve(input->count);
                // Match the renderer's canonical world-coordinate FMA. This
                // cache only receives LOD 0, so extent is the base chunk extent.
                // Using world vertices and an identity body avoids an extra
                // subtract/add of the chunk origin rounding a border again.
                const float scale = mesh.extent / 65534.0f;
                const std::array<float, 3> base{
                    float(mesh.baseChunkCoordinate[0]), float(mesh.baseChunkCoordinate[1]),
                    float(mesh.baseChunkCoordinate[2])};
                for (std::size_t triangle = 0; triangle + 2 < input->count; triangle += 3)
                {
                    if ((triangle&1023u)==0 && input->cancelled.load(std::memory_order_acquire))
                        return {nullptr,true,nullptr,true};
                    btVector3 points[3];
                    for (unsigned int corner = 0; corner < 3; ++corner)
                    {
                        const auto& v = input->vertices[triangle + corner];
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
                // The mapping is no longer used. The GL thread may recycle this
                // staging slot while this worker builds the BVH or waits for
                // the physics queue. Acquire/release publishes this ownership.
                input->consumed.store(true,std::memory_order_release);
                if (input->cancelled.load(std::memory_order_acquire)) return {nullptr,true,nullptr,true};
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
            const PackedVertex* mapped = nullptr;
            GLsync fence = nullptr;
            MeshHandle mesh;
            bool occupied = false;
            std::shared_ptr<BuildInput> input;
        };

        struct Build
        {
            MeshHandle mesh;
            std::shared_ptr<BuildInput> input;
            std::future<BuildResult> result;
        };

        Ge::PhysicsWraper* physics = nullptr;
        CollisionConfig config;
        CollisionStats counters;
        std::map<Key, Record> records;
        std::map<Key, MeshHandle> rejected;
        std::vector<Pending> pending;
        std::vector<Build> builds;
        std::unique_ptr<BuildWorkers> workers;
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

        bool busy(const Key& key) const
        {
            return std::any_of(pending.begin(),pending.end(),[&](const Pending& job)
                {return job.occupied && keyOf(job.mesh)==key;})
                || std::any_of(builds.begin(),builds.end(),[&](const Build& job)
                {return keyOf(job.mesh)==key;});
        }

        std::size_t pendingNew() const
        {
            std::set<Key> keys;
            for (const auto& job:pending)
                if (job.occupied && !records.count(keyOf(job.mesh))) keys.insert(keyOf(job.mesh));
            for (const auto& job:builds)
                if (!records.count(keyOf(job.mesh))) keys.insert(keyOf(job.mesh));
            return keys.size();
        }

        void finish(const MeshHandle& mesh, BuildResult result, const glm::vec3& position)
        {
            const Key key = keyOf(mesh);
            auto found = records.find(key);
            if (result.success && !result.cancelled && retained(key, position))
            {
                if (found != records.end())
                {
                    // The replacement is already in Bullet before old removal.
                    retire(found->second);
                    found->second = {result.collider, mesh.slot, mesh.generation};
                }
                else if (records.size() < config.maxResidentChunks)
                    records.emplace(key, Record{result.collider, mesh.slot, mesh.generation});
                else if (result.collider)
                    physics->ReleaseStaticMeshCollider(result.collider);
                ++counters.completedChunks;
            }
            else
            {
                if (result.collider) physics->ReleaseStaticMeshCollider(result.collider);
                if (!result.success) reject(mesh, result.error);
            }
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
            config.maxPendingBuilds == 0 || config.maxPendingBuilds > 16 ||
            config.workerCount == 0 || config.workerCount > 8 || config.workerCount > config.maxPendingBuilds ||
            config.maxCopiesPerFrame == 0 || config.maxReadbackMiB == 0 ||
            config.maxCopyMiBPerFrame == 0 || config.maxCopyMiBPerFrame > 1024 ||
            config.maxReadbackMiB > 128) return false;
        m_impl->physics = physics;
        m_impl->config = config;
        m_impl->counters = {};
        m_impl->error.clear();
        try
        {
            m_impl->pending.resize(config.maxPendingReadbacks);
            m_impl->builds.reserve(config.maxPendingBuilds);
            m_impl->workers=std::make_unique<BuildWorkers>(config.workerCount);
        }
        catch (...) { shutdown(); return false; }
        return true;
    }

    void CollisionCache::shutdown() noexcept
    {
        if (!m_impl || !m_impl->physics) return;
        for (auto& job : m_impl->builds) job.input->cancelled.store(true,std::memory_order_release);
        // Workers may still hold a pointer into a persistent mapping. Join them
        // before unmapping/deleting any staging buffer. The physics command
        // queue must still be alive, as with the former asynchronous builds.
        m_impl->workers.reset();
        for (auto& job : m_impl->builds)
        {
            if (job.result.valid())
            {
                try
                {
                    const auto result = job.result.get();
                    if (result.collider) m_impl->physics->ReleaseStaticMeshCollider(result.collider);
                }
                catch (...) {}
            }
        }
        m_impl->builds.clear();
        for (auto& job : m_impl->pending)
        {
            if (job.fence) glDeleteSync(job.fence);
            if (job.mapped) glUnmapNamedBuffer(job.staging);
            if (job.staging) glDeleteBuffers(1,&job.staging);
        }
        for (auto& entry : m_impl->records) m_impl->retire(entry.second);
        m_impl->records.clear();
        m_impl->rejected.clear();
        m_impl->pending.clear();
        m_impl->counters.stagingBytes=0;
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

        for (auto it=state.builds.begin();it!=state.builds.end();)
        {
            if (!state.retained(keyOf(it->mesh),position))
                it->input->cancelled.store(true,std::memory_order_release);
            if (it->result.wait_for(std::chrono::seconds(0))!=std::future_status::ready)
            { ++it; continue; }
            state.finish(it->mesh,it->result.get(),position);
            it=state.builds.erase(it);
        }
        for (auto& job : state.pending)
        {
            if (!job.occupied) continue;
            if (job.input)
            {
                if (job.input->consumed.load(std::memory_order_acquire))
                { job.input.reset(); job.occupied=false; }
                continue;
            }
            if (job.fence)
            {
                const GLenum result=glClientWaitSync(job.fence,0,0);
                if (result==GL_TIMEOUT_EXPIRED) continue;
                glDeleteSync(job.fence); job.fence=nullptr;
                if (result==GL_WAIT_FAILED)
                {
                    state.reject(job.mesh,"GPU terrain collision readback fence failed");
                    job.occupied=false;
                    continue;
                }
            }
            if (!state.retained(keyOf(job.mesh), position))
            {
                job.occupied = false;
                continue;
            }
            if (state.builds.size()>=state.config.maxPendingBuilds)
            { state.counters.capacityLimited=true; continue; }
            try
            {
                auto input=std::make_shared<BuildInput>();
                input->vertices=job.mapped; input->count=job.mesh.vertexCount;
                auto* physics=state.physics;
                const auto mesh=job.mesh;
                auto future=state.workers->submit([physics,mesh,input]
                    {return buildCollision(physics,mesh,input);});
                // Capacity was reserved at initialize(), so publishing the
                // scheduled task cannot allocate or orphan a mapping lease.
                state.builds.push_back({mesh,input,std::move(future)});
                job.input=std::move(input);
            }
            catch (...)
            {
                // Keep the previous collider on allocation/thread failure.
                // In particular, never leave an occupied slot without a fence.
                state.reject(job.mesh,"Terrain collision CPU worker submission failed");
                job.occupied=false;
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
            if (state.busy(key)) continue;
            candidates.push_back(&mesh);
        }
        std::sort(candidates.begin(), candidates.end(), [&](const MeshHandle* a, const MeshHandle* b)
            { return distanceSquared(keyOf(*a), position) < distanceSquared(keyOf(*b), position); });

        std::uint32_t submitted = 0;
        std::size_t frameBytes=0;
        for (const auto* mesh : candidates)
        {
            if (submitted >= state.config.maxCopiesPerFrame) break;
            if (state.busy(keyOf(*mesh))) continue;
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
            // Permit one bounded large mesh so a per-frame bandwidth target
            // smaller than a valid chunk cannot starve that chunk forever.
            if (submitted && frameBytes+bytes>std::size_t(state.config.maxCopyMiBPerFrame)*1024*1024) break;
            const auto pendingNew = state.pendingNew();
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
                    const bool replacementPending = state.busy(it->first);
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
            if (job.capacity < bytes)
            {
                if (job.mapped) glUnmapNamedBuffer(job.staging);
                if (job.staging) glDeleteBuffers(1,&job.staging);
                state.counters.stagingBytes-=job.capacity;
                job.staging=0; job.mapped=nullptr; job.capacity=0;
                glCreateBuffers(1,&job.staging);
                if (job.staging)
                {
                    constexpr GLbitfield flags=GL_MAP_READ_BIT|GL_MAP_PERSISTENT_BIT|GL_MAP_COHERENT_BIT;
                    glNamedBufferStorage(job.staging,static_cast<GLsizeiptr>(bytes),nullptr,
                        flags|GL_CLIENT_STORAGE_BIT);
                    job.mapped=static_cast<const PackedVertex*>(glMapNamedBufferRange(job.staging,0,
                        static_cast<GLsizeiptr>(bytes),flags));
                }
                if (!job.mapped)
                {
                    state.reject(*mesh,"GPU terrain collision persistent staging allocation failed");
                    continue;
                }
                job.capacity=bytes;
                state.counters.stagingBytes+=bytes;
            }
            // DSA leaves every engine buffer binding untouched. Subsequent
            // retirement/reuse of the source range is ordered after this copy.
            glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);
            glCopyNamedBufferSubData(mesher.vertexBuffer(), job.staging,
                static_cast<GLintptr>(std::size_t(mesh->firstVertex) * sizeof(PackedVertex)), 0,
                static_cast<GLsizeiptr>(bytes));
            glMemoryBarrier(GL_CLIENT_MAPPED_BUFFER_BARRIER_BIT);
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
            frameBytes+=bytes;
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
            if (!job.input) ++result.pendingReadbacks;
        }
        result.pendingBuilds=static_cast<std::uint32_t>(m_impl->builds.size());
        return result;
    }

    const std::string& CollisionCache::lastError() const noexcept
    {
        return m_impl->error;
    }
}
