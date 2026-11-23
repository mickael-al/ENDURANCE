#pragma once

#include "EnduranceBiomeEffectsCatalog.hpp"
#include <memory>
#include <string>

struct ptrClass;

// Small game-owned adapter over the engine's existing GPU ParticleSystem.
// It owns only live effect groups; dormant events allocate no particle slots.
class EnduranceBiomeParticles
{
public:
    EnduranceBiomeParticles();
    ~EnduranceBiomeParticles();
    EnduranceBiomeParticles(const EnduranceBiomeParticles&) = delete;
    EnduranceBiomeParticles& operator=(const EnduranceBiomeParticles&) = delete;
    bool initialize(const ptrClass* engine);
    void update(const std::array<float, EnduranceEnvironment::EffectCount>& strengths,
        const glm::vec3& cameraPosition, const glm::vec3& cameraForward,
        const glm::vec2& wind, float dt, bool teleport = false, float density = 1.0f);
    void release();
    std::size_t liveParticleCount() const;
    std::size_t groupCount() const;
    const std::string& error() const;
    static constexpr std::size_t MaxLiveParticles = 524288;
    static constexpr std::size_t MaxLiveGroups = 8;
private:
    struct Impl;
    std::unique_ptr<Impl> m;
};
