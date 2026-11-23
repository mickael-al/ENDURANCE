// This translation unit receives production optimization flags separately from
// the strict test oracle, including -ffast-math in the release test mode.
#include "EnduranceParticleWind.hpp"

glm::vec2 productionParticleWind(glm::vec2 base, double seconds, float variation, float period)
{
    return EnduranceEnvironment::particleWind(base, seconds, variation, period);
}
