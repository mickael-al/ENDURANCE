#pragma once

#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>

namespace Ge
{
    // Linear scene-referred RGB. Exposure may raise these colors above one.
    // No game, climate or biome identifiers belong to this interface.
    struct AtmospherePalette
    {
        glm::vec3 horizon{0.58f, 0.69f, 0.76f};
        glm::vec3 middle{0.19f, 0.36f, 0.53f};
        glm::vec3 zenith{0.045f, 0.13f, 0.29f};
        float middleElevation = 0.25f; // sine of elevation, 0=horizon, 1=zenith
        float gradientPower = 1.0f;
        float strength = 0.35f;
        float exposure = 1.0f;
    };

    inline AtmospherePalette mixAtmospherePalette(const AtmospherePalette& a,
        const AtmospherePalette& b, float t)
    {
        AtmospherePalette p;
        p.horizon = glm::mix(a.horizon, b.horizon, t);
        p.middle = glm::mix(a.middle, b.middle, t);
        p.zenith = glm::mix(a.zenith, b.zenith, t);
        p.middleElevation = glm::mix(a.middleElevation, b.middleElevation, t);
        p.gradientPower = glm::mix(a.gradientPower, b.gradientPower, t);
        p.strength = glm::mix(a.strength, b.strength, t);
        p.exposure = glm::mix(a.exposure, b.exposure, t);
        return p;
    }

    inline float atmospherePaletteDifference(const AtmospherePalette& a,
        const AtmospherePalette& b)
    {
        const glm::vec3 d = (glm::max)(glm::abs(a.horizon-b.horizon),
            (glm::max)(glm::abs(a.middle-b.middle), glm::abs(a.zenith-b.zenith)));
        return (std::max)({d.x,d.y,d.z,std::abs(a.middleElevation-b.middleElevation),
            std::abs(a.gradientPower-b.gradientPower),std::abs(a.strength-b.strength),
            std::abs(a.exposure-b.exposure)});
    }

    // Render-time state, independent of behaviour pause and of GPU readback.
    // Rejected sub-threshold samples never replace the accepted target.
    class AtmosphereTransition
    {
    public:
        bool target(const AtmospherePalette& p, float threshold, float seconds, bool force=false,
            const AtmospherePalette* displayedOverride=nullptr)
        {
            if (m_initialized && !force && atmospherePaletteDifference(p,m_target) < (std::max)(0.0f,threshold))
                return false;
            if (!m_initialized)
            {
                m_displayed=m_from=m_target=p;
                m_initialized=true;
                m_elapsed=m_duration=0.0;
                return true;
            }
            if (displayedOverride) m_displayed=*displayedOverride;
            m_from=m_displayed;
            m_target=p;
            m_elapsed=0.0;
            m_duration=(std::max)(0.0f,seconds);
            if (m_duration==0.0) m_displayed=m_target;
            return true;
        }

        bool advance(double seconds)
        {
            if (!active()) return false;
            if (!std::isfinite(seconds) || seconds<=0.0) return false;
            m_elapsed=(std::min)(m_duration,m_elapsed+seconds);
            const float t=static_cast<float>(m_elapsed/m_duration);
            // Fixed endpoint interpolation: subdivision of dt has no effect.
            const float smooth=t*t*(3.0f-2.0f*t);
            m_displayed=mixAtmospherePalette(m_from,m_target,smooth);
            return true;
        }
        bool active() const { return m_initialized && m_elapsed<m_duration; }
        bool initialized() const { return m_initialized; }
        const AtmospherePalette& displayed() const { return m_displayed; }
        const AtmospherePalette& accepted() const { return m_target; }
        void reset() { *this=AtmosphereTransition{}; }
    private:
        AtmospherePalette m_displayed, m_from, m_target;
        double m_elapsed=0.0, m_duration=0.0;
        bool m_initialized=false;
    };
}
