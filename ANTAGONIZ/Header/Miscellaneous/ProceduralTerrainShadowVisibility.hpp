#ifndef ENDURANCE_TERRAIN_SHADOW_VISIBILITY_HPP
#define ENDURANCE_TERRAIN_SHADOW_VISIBILITY_HPP

#include "ProceduralTerrainGpu.hpp"
#include "ProceduralTerrainVisibility.hpp"
#include "ShadowCasterBounds.hpp"
#include <algorithm>
#include <vector>

namespace ProceduralTerrain
{
    // Visibility inputs can change without changing this layer's casters.
    // Keep an exact canonical list, shared by cache decisions and actual draws.
    // Matrix changes are independently tracked by the engine's shadow cache.
    class ShadowMeshVisibilityCache
    {
    public:
        bool update(const glm::mat4& clip, std::uint64_t publication,
                    const std::vector<MeshHandle>& visible)
        {
            if (!m_visibility.update(clip, publication)) return false;
            m_pending.clear();
            for (const auto& mesh : visible)
                if (intersectsFrustum(m_visibility.frustum(), mesh.origin, mesh.extent))
                    m_pending.push_back(mesh);
            std::sort(m_pending.begin(), m_pending.end(), [](const MeshHandle& a, const MeshHandle& b)
            { return a.slot < b.slot || (a.slot == b.slot && a.generation < b.generation); });
            if (m_pending.size() != m_meshes.size()
                || !std::equal(m_pending.begin(), m_pending.end(), m_meshes.begin(), sameMesh))
            {
                m_meshes.swap(m_pending);
                m_revision = Ge::nextShadowCasterBoundsRevision();
            }
            return true;
        }
        const std::vector<MeshHandle>& meshes() const noexcept { return m_meshes; }
        std::uint64_t contentRevision() const noexcept { return m_revision; }
    private:
        static bool sameMesh(const MeshHandle& a, const MeshHandle& b) noexcept
        {
            return a.slot == b.slot && a.generation == b.generation
                && a.firstVertex == b.firstVertex && a.vertexCount == b.vertexCount
                && a.origin == b.origin && a.baseChunkCoordinate == b.baseChunkCoordinate
                && a.extent == b.extent && a.lod == b.lod && a.transitionMask == b.transitionMask;
        }
        ShadowVisibilityCache m_visibility;
        std::vector<MeshHandle> m_meshes, m_pending;
        std::uint64_t m_revision = Ge::nextShadowCasterBoundsRevision();
    };
}
#endif
