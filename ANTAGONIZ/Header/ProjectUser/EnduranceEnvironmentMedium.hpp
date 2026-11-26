#pragma once
#include <array>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

// Game-authored additions to the existing participating-medium integration.
// Layer0 supports ground fog without counting the biome's base fog twice;
// layer1 adds suspended weather (sand, ash, snow). Heights never follow cameraY.
struct EnduranceEnvironmentMediumLayer
{
    float extinction=0.0f; // inverse metres
    glm::vec3 albedo{1.0f};
    // Ground: offset from the biome fog atlas's base. Weather: above scene sea.
    float baseHeight=0.0f, heightScale=150.0f;
    float noiseAmount=0.0f, noiseScale=0.002f;
    glm::vec2 windOffset{0.0f}; // integrated particle-only XZ wind, metres
};
struct EnduranceEnvironmentMediumState
{
    std::array<EnduranceEnvironmentMediumLayer,2> layers{};
};
