#ifndef ENDURANCE_PROCEDURAL_GENERATION_HPP
#define ENDURANCE_PROCEDURAL_GENERATION_HPP

#include <cstdint>

struct SceneData;

class ProceduralGeneration
{
public:
    // The scene is borrowed and must outlive this generator.
    ProceduralGeneration(SceneData& scene, std::uint32_t seed) noexcept;

    SceneData& getScene() noexcept { return m_scene; }
    const SceneData& getScene() const noexcept { return m_scene; }
    std::uint32_t getSeed() const noexcept { return m_seed; }

private:
    SceneData& m_scene;
    std::uint32_t m_seed;
};

#endif // ENDURANCE_PROCEDURAL_GENERATION_HPP
