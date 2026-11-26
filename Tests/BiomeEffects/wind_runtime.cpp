// This translation unit receives production optimization flags separately from
// the strict test oracle, including -ffast-math in the release test mode.
#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include "EnduranceBiomeEffectsCatalog.hpp"
#include "EnduranceEnvironmentMedium.hpp"

// Reproduce windows.h macro pollution after standard/GLM dependencies are
// loaded. Actual production inline helpers must still compile and execute.
#define min(a,b) ENDURANCE_UNEXPECTED_WINDOWS_MIN(a,b)
#define max(a,b) ENDURANCE_UNEXPECTED_WINDOWS_MAX(a,b)
#include "EnduranceParticleWind.hpp"
#include "EnduranceBiomeMedium.hpp"
#undef min
#undef max

glm::vec2 productionParticleWind(glm::vec2 base, double seconds, float variation, float period)
{
    return EnduranceEnvironment::particleWind(base, seconds, variation, period);
}

EnduranceEnvironmentMediumState productionEnvironmentMedium(
    const std::array<float, EnduranceEnvironment::EffectCount>& strengths, glm::vec2 windOffset)
{
    return EnduranceEnvironment::environmentMediumState(strengths, windOffset);
}
