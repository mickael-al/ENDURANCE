#include "ProceduralTerrainPlanner.hpp"
#include <glm/geometric.hpp>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <utility>
#include <exception>

namespace ProceduralTerrain
{
struct LodPlanner::Impl
{
    LodConfig config;
    mutable std::mutex mutex;
    std::condition_variable wake;
    bool stopping = false, requested = false, occupied = false, completed = false;
    std::vector<LodChunk> previous;
    glm::vec3 camera{};
    std::uint64_t revision = 0;
    LodPlanResult result;
    std::thread worker;

    explicit Impl(const LodConfig& value) : config(value)
    {
        // run() may start immediately: publish this only after every member
        // (including flags, request storage and result) has been initialized.
        worker = std::thread([this] { run(); });
    }
    ~Impl()
    {
        { std::lock_guard<std::mutex> lock(mutex); stopping = true; }
        wake.notify_one();
        worker.join();
    }
    void run()
    {
        std::vector<LodChunk> desired;
        glm::vec3 selectedPosition{};
        bool selectedBefore = false;
        for (;;)
        {
            std::vector<LodChunk> snapshot;
            glm::vec3 position;
            std::uint64_t coverageRevision;
            {
                std::unique_lock<std::mutex> lock(mutex);
                wake.wait(lock, [this] { return stopping || requested; });
                if (stopping) return;
                snapshot = std::move(previous); position = camera; coverageRevision = revision;
                requested = false;
            }
            LodPlanResult plan;
            plan.coverageRevision = coverageRevision;
            const auto start = std::chrono::steady_clock::now();
            try
            {
                const auto delta = glm::dvec3(position) - glm::dvec3(selectedPosition);
                const double step = 32.0 * config.baseSpacing;
                if (!selectedBefore || glm::dot(delta, delta) >= step * step)
                {
                    if (selectLodChunks(position, config, desired, plan.error))
                    {
                        selectedPosition = position;
                        selectedBefore = true;
                        plan.selected = true;
                    }
                }
                if (plan.error.empty())
                    plan.success = planLodTransaction(snapshot, desired, position, config,
                                                      plan.transaction, plan.error);
                plan.selectionPosition = selectedPosition;
                plan.desiredCount = desired.size();
            }
            catch (const std::exception& error) { plan.error = error.what(); }
            catch (...) { plan.error = "Terrain CPU planning failed."; }
            plan.milliseconds = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - start).count();
            {
                std::lock_guard<std::mutex> lock(mutex);
                if (stopping) return;
                result = std::move(plan);
                completed = true;
            }
        }
    }
};

LodPlanner::LodPlanner(const LodConfig& config) : m_impl(std::make_unique<Impl>(config)) {}
LodPlanner::~LodPlanner() = default;
bool LodPlanner::request(const std::vector<LodChunk>& previous, const glm::vec3& camera,
                         std::uint64_t coverageRevision)
{
    auto& s = *m_impl;
    { std::lock_guard<std::mutex> lock(s.mutex);
      if (s.occupied || s.stopping) return false;
      s.previous = previous; s.camera = camera; s.revision = coverageRevision;
      s.occupied = s.requested = true; }
    s.wake.notify_one();
    return true;
}
bool LodPlanner::poll(LodPlanResult& result)
{
    auto& s = *m_impl;
    std::lock_guard<std::mutex> lock(s.mutex);
    if (!s.completed) return false;
    result = std::move(s.result);
    s.completed = s.occupied = false;
    return true;
}
bool LodPlanner::busy() const
{
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    return m_impl->occupied;
}
}
