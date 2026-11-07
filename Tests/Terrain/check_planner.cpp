#include "ProceduralTerrainPlanner.hpp"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <thread>
#include <vector>

using namespace ProceduralTerrain;
namespace
{
void require(bool condition, const char* message)
{
    if (!condition) { std::cerr << "FAIL " << message << '\n'; std::exit(1); }
}

LodPlanResult waitFor(LodPlanner& planner)
{
    LodPlanResult result;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!planner.poll(result))
    {
        require(std::chrono::steady_clock::now() < deadline, "worker did not finish");
        std::this_thread::yield();
    }
    require(!planner.busy(), "poll did not release request slot");
    return result;
}

bool equal(const std::vector<LodChunk>& a, const std::vector<LodChunk>& b)
{
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (a[i].origin != b[i].origin || a[i].lod != b[i].lod ||
            a[i].transitionMask != b[i].transitionMask) return false;
    return true;
}

void sameTransaction(const LodTransaction& actual, const LodTransaction& expected)
{
    require(actual.kind == expected.kind && actual.budgetFallback == expected.budgetFallback &&
            equal(actual.next, expected.next) &&
            actual.previousIndices == expected.previousIndices &&
            actual.nextIndices == expected.nextIndices, "worker differs from synchronous plan");
}
}

int main()
{
    LodConfig config;
    config.cells = 8; config.maxLod = 3; config.viewDistance = 48;
    config.splitDistanceFactor = 0.75f; config.fullResolutionRadius = 4;
    config.minimumY = -16; config.maximumY = 16; config.maxLeaves = 1024;
    const glm::vec3 start(1, 2, 3), nearby(2, 2, 3), moved(41, 2, 3);
    std::vector<LodChunk> desired;
    std::string error;
    require(selectLodChunks(start, config, desired, error), "synchronous selection failed");
    LodTransaction expected;
    require(planLodTransaction({}, desired, start, config, expected, error), "synchronous plan failed");

    LodPlanner planner(config);
    require(!planner.busy(), "new planner is busy");
    LodPlanResult empty;
    require(!planner.poll(empty), "new planner has a result");
    require(planner.request({}, start, 7), "first request was rejected");
    require(planner.busy(), "outstanding request is not busy");
    // Even if computation is already finished, no further request may replace
    // the result until the caller consumes it. There is no stale position queue.
    for (std::uint64_t revision = 8; revision < 40; ++revision)
        require(!planner.request({}, moved, revision), "accepted duplicate outstanding request");
    auto result = waitFor(planner);
    require(result.success && result.error.empty() && result.selected, "initial worker plan failed");
    require(result.coverageRevision == 7 && result.selectionPosition == start &&
            result.desiredCount == desired.size(), "initial result metadata changed");
    require(result.milliseconds >= 0, "negative planning duration");
    sameTransaction(result.transaction, expected);
    require(!planner.poll(empty), "result was published twice");

    // Ownership of the submitted coverage must not depend on caller lifetime.
    auto snapshot = desired;
    require(planner.request(snapshot, nearby, 40), "cached-selection request rejected");
    snapshot.clear();
    result = waitFor(planner);
    require(result.success && !result.selected && result.selectionPosition == start,
            "sub-threshold movement rebuilt the selection");
    require(result.transaction.kind == LodWorkKind::None && equal(result.transaction.next, desired),
            "request did not preserve its copied coverage");

    require(selectLodChunks(moved, config, desired, error), "moved selection failed");
    require(planLodTransaction({}, desired, moved, config, expected, error), "moved plan failed");
    require(planner.request({}, moved, 41), "latest position request rejected");
    result = waitFor(planner);
    require(result.success && result.selected && result.selectionPosition == moved &&
            result.coverageRevision == 41, "worker did not use latest accepted position");
    sameTransaction(result.transaction, expected);

    require(planner.request({}, glm::vec3(std::numeric_limits<float>::quiet_NaN()), 42),
            "invalid input request rejected before worker validation");
    result = waitFor(planner);
    require(!result.success && !result.error.empty() && result.coverageRevision == 42,
            "worker did not report invalid input");
    require(planner.request({}, moved, 43), "worker did not recover after an error");
    require(waitFor(planner).success, "valid planning failed after an error");

    // Concurrent submitters compete for one immutable request slot.
    std::atomic<unsigned> accepted{0};
    std::atomic<std::uint64_t> winner{0};
    std::vector<std::thread> submitters;
    for (std::uint64_t revision = 50; revision < 58; ++revision)
        submitters.emplace_back([&, revision] {
            if (planner.request({}, moved, revision)) { ++accepted; winner = revision; }
        });
    for (auto& submitter : submitters) submitter.join();
    require(accepted == 1, "concurrent requests occupied more than one slot");
    result = waitFor(planner);
    require(result.success && result.coverageRevision == winner,
            "concurrent request overwrote accepted snapshot");

    // Exercise immediate shutdown, queued/running work, and a completed but
    // unconsumed result. The destructor must wake or join in every state.
    for (unsigned iteration = 0; iteration < 64; ++iteration)
    {
        LodPlanner idle(config);
        LodPlanner pending(config);
        require(pending.request({}, start, iteration), "pending shutdown request failed");
    }
    {
        LodPlanner completed(config);
        require(completed.request({}, start, 100), "completed shutdown request failed");
        // Keep the result occupied without reading it; destruction owns cleanup.
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        require(completed.busy(), "unconsumed result released its slot");
    }
    std::cout << "PASS planner: production selection/transactions, cached selection, latest request, "
                 "snapshot ownership, concurrent slot, error recovery and pending shutdown\n";
}
