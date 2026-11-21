#ifndef GE_SHADOW_MAP_CACHE_HPP
#define GE_SHADOW_MAP_CACHE_HPP

#include <glm/mat4x4.hpp>
#include <cstdint>
#include <cstring>
#include <vector>

namespace Ge
{
    // A content token promises that the renderer will emit exactly the same
    // shadow geometry/material state until it changes. Zero is untracked.
    struct ShadowContentSource
    {
        const void* identity = nullptr;
        std::uint64_t revision = 0;
        bool operator==(const ShadowContentSource& other) const noexcept
        { return identity == other.identity && revision == other.revision; }
    };

    // CPU bookkeeping only: no GPU readback and no hash collisions. Matrices
    // are compared exactly; a changed depth transform must never reuse a map.
    class ShadowMapCache
    {
    public:
        void invalidateStorage()
        {
            m_layers.clear();
            m_ready = false;
            m_storageChanged = true;
        }

        void prepare(const std::vector<glm::mat4>& matrices,
            const std::vector<ShadowContentSource>& sources, bool untrackedCasters)
        {
            prepareSources(matrices, [&](std::size_t) -> const auto& { return sources; }, untrackedCasters);
        }

        // Providers may track the exact content selected by each light frustum.
        // A publication outside one layer no longer invalidates that layer. The
        // legacy shared-source entry point retains conservative global behavior.
        void prepareLayerSources(const std::vector<glm::mat4>& matrices,
            const std::vector<std::vector<ShadowContentSource>>& sources, bool untrackedCasters)
        {
            const std::vector<ShadowContentSource> empty;
            prepareSources(matrices, [&](std::size_t i) -> const auto&
                { return i < sources.size() ? sources[i] : empty; },
                untrackedCasters || sources.size() != matrices.size());
        }

        bool needsRender(std::size_t layer) const noexcept
        { return layer < m_layers.size() && m_layers[layer].dirty; }

        // Call only after submitting the complete depth pass for this layer.
        void rendered(std::size_t index)
        {
            if (index >= m_layers.size() || !m_layers[index].dirty) return;
            auto& layer = m_layers[index];
            layer.matrix = layer.pending;
            layer.sources = layer.pendingSources;
            layer.valid = layer.trackable;
            layer.dirty = false;
            m_wrote = true;
            ++m_redrawn;
        }

        // Publish one content revision after all layers have been submitted.
        // Unknown casters redraw every frame, so their content also advances.
        void finish()
        {
            for (const auto& layer : m_layers) if (layer.dirty) return;
            if (!m_ready && (m_wrote || m_storageChanged || m_contentRevision == 0))
                ++m_contentRevision;
            m_storageChanged = false;
            m_ready = true;
        }

        std::uint64_t contentRevision() const noexcept { return m_ready ? m_contentRevision : 0; }
        unsigned redrawnLayers() const noexcept { return m_redrawn; }
        unsigned reusedLayers() const noexcept { return m_reused; }

    private:
        template<class SourceAt>
        void prepareSources(const std::vector<glm::mat4>& matrices, SourceAt sourceAt, bool untrackedCasters)
        {
            if (m_layers.size() != matrices.size()) invalidateStorage();
            m_layers.resize(matrices.size());
            m_redrawn = m_reused = 0;
            m_ready = false;
            m_wrote = false;
            for (std::size_t i = 0; i < matrices.size(); ++i)
            {
                auto& layer = m_layers[i];
                const auto& sources = sourceAt(i);
                layer.pending = matrices[i];
                layer.pendingSources = sources;
                layer.trackable = !untrackedCasters && finiteMatrix(matrices[i]);
                for (const auto& source : sources) layer.trackable &= source.revision != 0;
                layer.dirty = !layer.trackable || !layer.valid || layer.sources != sources ||
                    std::memcmp(&layer.matrix, &matrices[i], sizeof(glm::mat4)) != 0;
                if (!layer.dirty) ++m_reused;
            }
        }
        static bool finiteMatrix(const glm::mat4& matrix) noexcept
        {
            for (unsigned c = 0; c < 4; ++c) for (unsigned r = 0; r < 4; ++r)
            {
                std::uint32_t bits;
                std::memcpy(&bits, &matrix[c][r], sizeof(bits));
                if ((bits & UINT32_C(0x7f800000)) == UINT32_C(0x7f800000)) return false;
            }
            return true;
        }
        struct Layer
        {
            glm::mat4 matrix{1.0f}, pending{1.0f};
            std::vector<ShadowContentSource> sources, pendingSources;
            bool valid = false, dirty = true, trackable = false;
        };
        std::vector<Layer> m_layers;
        std::uint64_t m_contentRevision = 0;
        unsigned m_redrawn = 0, m_reused = 0;
        bool m_ready = false, m_wrote = false, m_storageChanged = true;
    };
}
#endif
