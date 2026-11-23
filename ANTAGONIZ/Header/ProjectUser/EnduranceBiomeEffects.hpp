#pragma once

#include "Behaviour.hpp"
#include "CustomRenderer.hpp"
#include "EnduranceBiomeEffectsCatalog.hpp"
#include <array>
#include <memory>
#include <string>
#ifndef JS_STL_ARRAY
#define JS_STL_ARRAY
#endif
#include "json_struct.h"

// Game-level visual policy. Engine particles/post-processing remain biome-agnostic.
class EnduranceBiomeEffectsComponent final : public Behaviour, public CustomRenderer
{
public:
    EnduranceBiomeEffectsComponent();
    ~EnduranceBiomeEffectsComponent() override;
    void start() override;
    void stop() override;
    void update() override {}
    void fixedUpdate() override {}
    void prepareRenderer() override;
    void drawRenderer() override {}
    std::uint64_t shadowCasterBoundsRevision() const override { return m_emptyShadowRevision; }
    std::uint64_t shadowContentRevision() const override { return m_emptyShadowRevision; }
    void onGUI() override;
    std::string serialize() override;
    void load(std::string data) override;

    // Future weather/gameplay entry point. Only declared active events allowed by
    // the current dominant biome can start; nothing invokes this automatically.
    static EnduranceBiomeEffectsComponent* active();
    bool triggerEvent(EnduranceEnvironment::EffectId id, float seconds, float intensity = 1.0f);
    bool triggerEvent(const char* key, float seconds, float intensity = 1.0f);
    void stopEvent(EnduranceEnvironment::EffectId id);
    void stopEvents();

    bool enabled = true, particlesEnabled = true, screenEnabled = true, mediumEnabled = true;
    float passiveIntensity = 1.0f, screenIntensity = 1.0f, particleDensity = 1.0f;
    std::array<float, 2> wind{{3.0f, 1.0f}};
    float windVariation = 0.65f, windPeriod = 24.0f;
    float teleportDistance = 128.0f;
private:
    const std::uint64_t m_emptyShadowRevision = Ge::nextShadowCasterBoundsRevision();
    struct Impl;
    std::unique_ptr<Impl> m;
};
