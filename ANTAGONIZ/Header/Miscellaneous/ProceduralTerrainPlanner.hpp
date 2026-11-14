#ifndef ENDURANCE_PROCEDURAL_TERRAIN_PLANNER_HPP
#define ENDURANCE_PROCEDURAL_TERRAIN_PLANNER_HPP

#include "ProceduralTerrainStreaming.hpp"
#include <cstddef>
#include <memory>
#include <cstdint>
#include <string>

namespace ProceduralTerrain
{
    struct LodPlanResult
    {
        LodTransaction transaction;
        // The submitted camera, distinct from the cached selection's anchor.
        glm::vec3 requestPosition{};
        glm::vec3 selectionPosition{};
        std::uint64_t coverageRevision = 0;
        std::size_t desiredCount = 0;
        double milliseconds = 0.0;
        bool selected = false;
        bool success = false;
        std::string error;
    };

    // One persistent CPU worker; never owns a GL context or touches live meshes.
    // At most one request/result is outstanding. The render thread freezes a
    // small returned transaction while its meshes are generated asynchronously.
    // A caller may request against that transaction's immutable future coverage
    // to prepare one successor; it must validate the revision before using it.
    class LodPlanner
    {
    public:
        explicit LodPlanner(const LodConfig& config);
        ~LodPlanner();
        LodPlanner(const LodPlanner&) = delete;
        LodPlanner& operator=(const LodPlanner&) = delete;
        // False while a request/result is outstanding. Retry with the latest
        // camera/coverage after poll; stale intermediate positions are not queued.
        bool request(const std::vector<LodChunk>& previous, const glm::vec3& camera,
                     std::uint64_t coverageRevision);
        bool poll(LodPlanResult& result);
        bool busy() const;
    private:
        struct Impl;
        std::unique_ptr<Impl> m_impl;
    };
}
#endif
