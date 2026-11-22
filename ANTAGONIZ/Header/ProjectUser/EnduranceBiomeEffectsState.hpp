#pragma once

#include "EnduranceBiomeEffectsCatalog.hpp"
#include <array>

namespace EnduranceEnvironment
{
    // CPU-only selection/lifetime state. Biome weights never multiply effect
    // populations: only the selected biome contributes new passive effects.
    class EffectsState
    {
    public:
        static constexpr unsigned MaxConcurrentEvents = 2;
        void setBiome(int biome);
        int biome() const noexcept { return m_biome; }
        bool triggerEvent(EffectId id, float seconds, float intensity = 1.0f);
        void stopEvent(EffectId id);
        void stopEvents();
        // Deliberate editor preview, never called by biome selection or a timer.
        bool previewEffect(EffectId id, float seconds = 12.0f);
        void stopPreview();
        void update(float deltaSeconds);
        void clear();
        const std::array<float, EffectCount>& strengths() const noexcept { return m_strengths; }
        unsigned activeEventCount() const noexcept;
        float remaining(EffectId id) const noexcept;
        bool previewing() const noexcept { return m_previewRemaining > 0.0f; }
    private:
        struct Event { float remaining = 0.0f, intensity = 0.0f; };
        int m_biome = -1;
        std::array<Event, EffectCount> m_events{};
        std::array<float, EffectCount> m_strengths{};
        EffectId m_preview = EffectId::Count;
        float m_previewRemaining = 0.0f;
    };
}
