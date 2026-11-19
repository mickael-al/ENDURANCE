// Reproduce Windows headers included AFTER STL/GLM. GLM only removes the
// min/max macros on its first inclusion; public engine headers must not rely
// on that side effect or on the caller defining NOMINMAX.
#include <glm/glm.hpp>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

#define min(a, b) (((a) < (b)) ? (a) : (b))
#define max(a, b) (((a) > (b)) ? (a) : (b))
#include "ShadowCasterBounds.hpp"
#include "ShadowMapCache.hpp"
#include "AtmosphereState.hpp"

#if !defined(min) || !defined(max)
#error Engine headers must not remove macros belonging to their caller
#endif
static_assert(min(2, 3) == 2 && max(2, 3) == 3, "Caller macros changed");

int main()
{
    Ge::DirectionalShadowVolume volume;
    volume.radius = 10.0;
    if (Ge::fitDirectionalShadowCasterMargin(volume, 4000.0f, {}) != 4000.0f) return 1;
    Ge::AtmospherePalette from, to;
    to.exposure += 1.0f;
    if (Ge::atmospherePaletteDifference(from, to) != 1.0f) return 2;
    Ge::AtmosphereTransition transition;
    transition.target(from, 0.01f, 1.0f);
    if (!transition.target(to, 0.01f, 1.0f) || !transition.advance(1.0)) return 3;
    if (transition.active() || transition.displayed().exposure != to.exposure) return 4;
    return 0;
}
