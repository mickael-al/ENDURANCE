#include "ProceduralGeneration.hpp"

ProceduralGeneration::ProceduralGeneration(SceneData& scene, std::uint32_t seed) noexcept
    : m_scene(scene), m_seed(seed)
{
}
