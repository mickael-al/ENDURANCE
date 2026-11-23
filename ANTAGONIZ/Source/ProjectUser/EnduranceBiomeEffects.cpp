#include "EnduranceBiomeEffects.hpp"
#define CLEAR_REGISTRY
#include "EnduranceAtmosphere.hpp"
#include "EnduranceBiomeEffectsState.hpp"
#include "EnduranceBiomeParticles.hpp"
#include "EnduranceParticleWind.hpp"
#include "Camera.hpp"
#include "Engine.hpp"
#include "PointeurClass.hpp"
#include "EnvironmentScreenEffects.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>

REGISTER(Behaviour, EnduranceBiomeEffectsComponent, enabled, particlesEnabled, screenEnabled,
         passiveIntensity, screenIntensity, particleDensity, wind, windVariation, windPeriod, teleportDistance);

namespace
{
    using namespace EnduranceEnvironment;
    EnduranceBiomeEffectsComponent* activeEffects = nullptr;

    bool finiteFloat(float value)
    {
        // Preserve input validation under the project's Release -ffast-math.
        std::uint32_t bits;
        std::memcpy(&bits, &value, sizeof(bits));
        return (bits & 0x7f800000u) != 0x7f800000u;
    }

    float finiteRange(float value, float fallback, float lo, float hi)
    { return finiteFloat(value) ? std::clamp(value, lo, hi) : fallback; }

    bool finitePosition(const glm::vec3& p)
    { return finiteFloat(p.x) && finiteFloat(p.y) && finiteFloat(p.z); }

    Ge::EnvironmentScreenState screenState(const std::array<float, EffectCount>& strengths, float time)
    {
        Ge::EnvironmentScreenState state;
        glm::vec3 tint(0.0f);
        float tintWeight = 0.0f, styleWeight = 0.0f, scale = 0.0f, speed = 0.0f;
        const auto& definitions = effectDefinitions();
        for (std::size_t i = 0; i < EffectCount; ++i)
        {
            const float strength = strengths[i];
            if (strength <= 0.0f) continue;
            const auto& s = definitions[i].screen;
            if (s.features() == 0) continue;
            state.heat += strength * s.heat;
            state.frost += strength * s.frost;
            state.wet += strength * s.wet;
            state.condensation += strength * s.condensation;
            state.dust += strength * s.dust;
            state.flash += strength * s.flash;
            // Dust owns the veil's tint. Other screen materials keep their own
            // colors in the shader, so a frost fade cannot turn sand white.
            tint += s.tint * (strength * s.dust);
            tintWeight += strength * s.dust;
            scale += s.scale * strength; speed += s.speed * strength; styleWeight += strength;
        }
        if (tintWeight > 0.0f) state.tint = tint / tintWeight;
        if (styleWeight > 0.0f) { state.scale = scale / styleWeight; state.speed = speed / styleWeight; }
        state.time = time;
        return state;
    }
}

struct EnduranceBiomeEffectsComponent::Impl
{
    const ptrClass* pc = nullptr;
    EffectsState state;
    EnduranceBiomeParticles particles;
    bool registered = false, hasPosition = false;
    glm::vec3 previousPosition{};
    glm::vec2 currentWind{};
    double seconds = 0.0;
    float dominantWeight = 0.0f;
    int preview = 0;
    float eventDuration = 15.0f, eventIntensity = 1.0f;
    std::string error;
};

EnduranceBiomeEffectsComponent::EnduranceBiomeEffectsComponent() : m(std::make_unique<Impl>()) {}
EnduranceBiomeEffectsComponent::~EnduranceBiomeEffectsComponent() { stop(); }
EnduranceBiomeEffectsComponent* EnduranceBiomeEffectsComponent::active() { return activeEffects; }

void EnduranceBiomeEffectsComponent::start()
{
    stop();
    if (activeEffects && activeEffects != this)
    { m->error = "Only one biome effects component can own the scene's environment effects."; return; }
    m->pc = Ge::Engine::getPtrClassAddr();
    if (!m->pc || !m->pc->graphiquePipelineManager || !m->pc->postProcess || !m->pc->cameraManager)
    { m->error = "Biome effects require the scene renderer."; return; }
    if (!m->particles.initialize(m->pc))
    { m->error = m->particles.error(); return; }
    m->pc->postProcess->clearEnvironmentEffects();
    m->pc->graphiquePipelineManager->addCustomRenderer(this);
    m->registered = true;
    activeEffects = this;
}

void EnduranceBiomeEffectsComponent::stop()
{
    if (!m) return;
    if (m->registered && m->pc && m->pc->graphiquePipelineManager)
        m->pc->graphiquePipelineManager->removeCustomRenderer(this);
    if (activeEffects == this)
    {
        if (m->pc && m->pc->postProcess) m->pc->postProcess->clearEnvironmentEffects();
        activeEffects = nullptr;
    }
    m->particles.release();
    m = std::make_unique<Impl>();
}

bool EnduranceBiomeEffectsComponent::triggerEvent(EffectId id, float seconds, float intensity)
{ return m->registered && enabled && m->state.triggerEvent(id, seconds, intensity); }

bool EnduranceBiomeEffectsComponent::triggerEvent(const char* key, float seconds, float intensity)
{
    const auto* definition = findEffect(key);
    return definition && triggerEvent(definition->id, seconds, intensity);
}

void EnduranceBiomeEffectsComponent::stopEvent(EffectId id) { m->state.stopEvent(id); }
void EnduranceBiomeEffectsComponent::stopEvents() { m->state.stopEvents(); m->state.stopPreview(); }

void EnduranceBiomeEffectsComponent::prepareRenderer()
{
    if (!m->registered) return;
    auto* camera = m->pc->cameraManager->getCurrentCamera();
    if (!enabled || !camera || !finitePosition(camera->getPosition()))
    {
        m->state.clear(); m->hasPosition = false; m->dominantWeight = 0.0f;
        m->particles.update({}, {}, {0, 0, -1}, {}, 0.0f, true);
        m->pc->postProcess->clearEnvironmentEffects();
        return;
    }
    const glm::vec3 position = camera->getPosition();
    const float distance = finiteRange(teleportDistance, 128.0f, 16.0f, 4096.0f);
    const glm::vec3 moved = position - m->previousPosition;
    const bool teleport = m->hasPosition && glm::dot(moved, moved) > distance * distance;
    if (teleport) m->state.clear();
    m->previousPosition = position; m->hasPosition = true;

    const auto sample = EnduranceAtmosphereComponent::currentBiomeSample();
    const glm::vec2 sampleDelta = glm::vec2(position.x, position.z) - sample.worldXZ;
    // Do not show the old biome's effects at a new teleport destination while
    // the existing asynchronous climate request is still in flight.
    const bool valid = sample.valid && glm::dot(sampleDelta, sampleDelta) <= distance * distance;
    m->state.setBiome(valid ? static_cast<int>(sample.biome) : -1);
    m->dominantWeight = valid ? sample.weight : 0.0f;

    const float dt = finiteRange(m->pc->time ? m->pc->time->getDeltaTime() : 0.0f, 0.0f, 0.0f, 1.0f);
    m->seconds += dt;
    m->state.update(dt);
    auto strengths = m->state.strengths();
    const float passive = finiteRange(passiveIntensity, 1.0f, 0.0f, 1.0f);
    const auto& definitions = effectDefinitions();
    for (std::size_t i = 0; i < EffectCount; ++i)
        if (!definitions[i].activeEvent) strengths[i] *= passive;
    m->currentWind = particleWind({wind[0], wind[1]}, m->seconds, windVariation, windPeriod);
    m->particles.update(particlesEnabled ? strengths : std::array<float, EffectCount>{},
                        position, glm::vec3(0.0f), m->currentWind, dt, teleport,
                        finiteRange(particleDensity, 1.0f, 0.0f, 4.0f));

    if (screenEnabled)
    {
        const float screen = finiteRange(screenIntensity, 1.0f, 0.0f, 1.0f);
        for (float& strength : strengths) strength *= screen;
        m->pc->postProcess->setEnvironmentEffects(screenState(strengths, static_cast<float>(m->seconds)));
    }
    else m->pc->postProcess->clearEnvironmentEffects();
}

void EnduranceBiomeEffectsComponent::onGUI()
{
    ImGui::TextUnformatted("ENDURANCE biome effects");
    ImGui::Checkbox("Enabled##biome_effects", &enabled);
    ImGui::Checkbox("Particles##biome_effects", &particlesEnabled);
    ImGui::SameLine(); ImGui::Checkbox("Screen effects##biome_effects", &screenEnabled);
    const std::string densityLabel = std::to_string(particleDensity) + "x";
    if (ImGui::BeginCombo("Particle density", densityLabel.c_str()))
    {
        constexpr float densities[] = {0.0f, 0.5f, 1.0f, 2.0f, 4.0f};
        constexpr const char* labels[] = {"Off (0x)", "Sparse (0.5x)", "Normal (1x)", "Dense (2x)", "Very dense (4x)"};
        for (int i = 0; i < 5; ++i)
            if (ImGui::Selectable(labels[i], particleDensity == densities[i])) particleDensity = densities[i];
        ImGui::EndCombo();
    }
    ImGui::SliderFloat("Passive intensity", &passiveIntensity, 0.0f, 1.0f);
    ImGui::SliderFloat("Screen intensity", &screenIntensity, 0.0f, 1.0f);
    ImGui::DragFloat2("Particle wind XZ (m/s)", wind.data(), 0.1f, -100.0f, 100.0f);
    ImGui::SliderFloat("Wind variation", &windVariation, 0.0f, 1.0f);
    ImGui::DragFloat("Gust period (seconds)", &windPeriod, 0.5f, 2.0f, 180.0f);
    if (m->registered) ImGui::Text("Current particle wind: %.2f, %.2f m/s", m->currentWind.x, m->currentWind.y);
    ImGui::DragFloat("Teleport reset distance", &teleportDistance, 1.0f, 16.0f, 4096.0f);
    if (!m->error.empty()) ImGui::TextWrapped("%s", m->error.c_str());
    if (!m->particles.error().empty()) ImGui::TextWrapped("%s", m->particles.error().c_str());
    if (!m->registered) { ImGui::TextUnformatted("Start the scene to preview biome effects."); return; }
    const int biome = m->state.biome();
    if (biome >= 0) ImGui::Text("Dominant: %s (%.1f%%)", ProceduralTerrain::BiomeNames[biome], 100.0f * m->dominantWeight);
    else ImGui::TextUnformatted("Waiting for a valid terrain biome sample.");
    ImGui::Text("Live particles: %u / %u; groups: %u; events: %u / %u",
        static_cast<unsigned>(m->particles.liveParticleCount()), static_cast<unsigned>(EnduranceBiomeParticles::MaxLiveParticles),
        static_cast<unsigned>(m->particles.groupCount()), m->state.activeEventCount(), EffectsState::MaxConcurrentEvents);
    const auto& definitions = effectDefinitions();
    if (ImGui::TreeNode("Current passive effects"))
    {
        if (biome >= 0)
            for (const auto& effect : definitions)
                if (biomeEffectMatrix()[biome].passive & effectBit(effect.id)) ImGui::BulletText("%s", effect.name);
        ImGui::TreePop();
    }
    if (ImGui::TreeNode("Active events (manual trigger)"))
    {
        ImGui::SliderFloat("Duration (seconds)", &m->eventDuration, 1.0f, 120.0f);
        ImGui::SliderFloat("Event intensity", &m->eventIntensity, 0.05f, 1.0f);
        if (biome >= 0)
            for (const auto& effect : definitions)
                if (biomeEffectMatrix()[biome].active & effectBit(effect.id))
                {
                    ImGui::PushID(static_cast<int>(effect.id));
                    if (ImGui::Button(effect.name)) triggerEvent(effect.id, m->eventDuration, m->eventIntensity);
                    const float remaining = m->state.remaining(effect.id);
                    if (remaining > 0.0f) { ImGui::SameLine(); ImGui::Text("%.1fs", remaining); }
                    ImGui::PopID();
                }
        if (ImGui::Button("Stop events")) stopEvents();
        ImGui::TreePop();
    }
    if (ImGui::TreeNode("Preview all 40 effects"))
    {
        if (ImGui::BeginCombo("Effect", definitions[m->preview].name))
        {
            for (int i = 0; i < static_cast<int>(EffectCount); ++i)
                if (ImGui::Selectable(definitions[i].name, m->preview == i)) m->preview = i;
            ImGui::EndCombo();
        }
        if (enabled && ImGui::Button("Preview for 24 seconds"))
            m->state.previewEffect(static_cast<EffectId>(m->preview));
        ImGui::SameLine();
        if (ImGui::Button("Stop preview")) m->state.stopPreview();
        ImGui::TextWrapped("Preview temporarily overrides biome selection. Events never start automatically.");
        ImGui::TreePop();
    }
}

std::string EnduranceBiomeEffectsComponent::serialize() { return JS::serializeStruct(*this); }

void EnduranceBiomeEffectsComponent::load(std::string data)
{
    JS::ParseContext context(data);
    if (context.parseTo(*this) != JS::Error::NoError) m->error = "Invalid biome effects scene settings.";
    passiveIntensity = finiteRange(passiveIntensity, 1.0f, 0.0f, 1.0f);
    screenIntensity = finiteRange(screenIntensity, 1.0f, 0.0f, 1.0f);
    particleDensity = finiteRange(particleDensity, 1.0f, 0.0f, 4.0f);
    wind[0] = finiteRange(wind[0], 3.0f, -100.0f, 100.0f);
    wind[1] = finiteRange(wind[1], 1.0f, -100.0f, 100.0f);
    windVariation = finiteRange(windVariation, 0.65f, 0.0f, 1.0f);
    windPeriod = finiteRange(windPeriod, 24.0f, 2.0f, 180.0f);
    teleportDistance = finiteRange(teleportDistance, 128.0f, 16.0f, 4096.0f);
}
