#include "EnduranceBiomeEffectsState.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace
{
    bool finiteFloat(float value)
    {
        std::uint32_t bits;
        std::memcpy(&bits, &value, sizeof(bits));
        return (bits & 0x7f800000u) != 0x7f800000u;
    }
}

namespace EnduranceEnvironment
{
    void EffectsState::setBiome(int biome)
    {
        m_biome = biome >= 0 && biome < static_cast<int>(ProceduralTerrain::BiomeCount) ? biome : -1;
        const EffectMask allowed = m_biome >= 0 ? biomeEffectMatrix()[m_biome].active : 0;
        for (std::size_t i = 0; i < EffectCount; ++i)
            if ((allowed & effectBit(static_cast<EffectId>(i))) == 0) m_events[i] = {};
    }

    bool EffectsState::triggerEvent(EffectId id, float seconds, float intensity)
    {
        const auto index = static_cast<std::size_t>(id);
        if (index >= EffectCount || m_biome < 0 || !finiteFloat(seconds) || seconds <= 0.0f
            || !finiteFloat(intensity) || intensity <= 0.0f || !effectDefinitions()[index].activeEvent
            || (biomeEffectMatrix()[m_biome].active & effectBit(id)) == 0) return false;
        if (m_events[index].remaining <= 0.0f && activeEventCount() >= MaxConcurrentEvents) return false;
        m_events[index] = {(std::min)(seconds, 600.0f), std::clamp(intensity, 0.0f, 1.0f)};
        return true;
    }

    void EffectsState::stopEvent(EffectId id)
    {
        const auto index = static_cast<std::size_t>(id);
        if (index < EffectCount) m_events[index] = {};
    }

    void EffectsState::stopEvents() { m_events = {}; }

    bool EffectsState::previewEffect(EffectId id, float seconds)
    {
        if (static_cast<std::size_t>(id) >= EffectCount || !finiteFloat(seconds) || seconds <= 0.0f) return false;
        m_preview = id;
        m_previewRemaining = (std::min)(seconds, 60.0f);
        return true;
    }

    void EffectsState::stopPreview() { m_previewRemaining = 0.0f; m_preview = EffectId::Count; }

    void EffectsState::clear()
    {
        m_biome = -1;
        m_strengths = {};
        stopEvents();
        stopPreview();
    }

    unsigned EffectsState::activeEventCount() const noexcept
    {
        unsigned count = 0;
        for (const auto& event : m_events) if (event.remaining > 0.0f) ++count;
        return count;
    }

    float EffectsState::remaining(EffectId id) const noexcept
    {
        const auto index = static_cast<std::size_t>(id);
        return index < EffectCount ? m_events[index].remaining : 0.0f;
    }

    void EffectsState::update(float deltaSeconds)
    {
        if (!finiteFloat(deltaSeconds) || deltaSeconds <= 0.0f) return;
        const auto& definitions = effectDefinitions();
        const EffectMask passive = m_biome >= 0 ? biomeEffectMatrix()[m_biome].passive : 0;
        float remainingTime = deltaSeconds;
        while (remainingTime > 0.0f)
        {
            // Split only at a target change, not into fixed simulation steps.
            // A long frame that crosses an event's end still includes its fade-out.
            float interval = remainingTime;
            for (const auto& event : m_events)
                if (event.remaining > 0.0f) interval = (std::min)(interval, event.remaining);
            if (previewing()) interval = (std::min)(interval, m_previewRemaining);
            for (std::size_t i = 0; i < EffectCount; ++i)
            {
                const auto id = static_cast<EffectId>(i);
                float target = (passive & effectBit(id)) != 0 ? 1.0f : 0.0f;
                if (m_events[i].remaining > 0.0f) target = (std::max)(target, m_events[i].intensity);
                if (previewing()) target = m_preview == id ? 1.0f : 0.0f;
                const float duration = target > m_strengths[i] ? definitions[i].fadeIn : definitions[i].fadeOut;
                const float step = interval / (std::max)(0.05f, duration);
                // Linear, finite fades reach exact zero; dormant renderers can be removed.
                m_strengths[i] += std::clamp(target - m_strengths[i], -step, step);
                m_events[i].remaining = (std::max)(0.0f, m_events[i].remaining - interval);
            }
            m_previewRemaining = (std::max)(0.0f, m_previewRemaining - interval);
            remainingTime = (std::max)(0.0f, remainingTime - interval);
        }
    }
}
