#include "ClimateNoiseLab.hpp"
#define CLEAR_REGISTRY
#include "ClimateNoiseLabPanel.hpp"
#include "Debug.hpp"
#include "Engine.hpp"
#include "Hud.hpp"
#include "PointeurClass.hpp"
#include "imgui-cmake/Header/imgui.h"

REGISTER(Behaviour, ClimateNoiseLabComponent, seed, textureResolution, worldExtent,
    worldOriginX, worldOriginZ, climateFrequency,
    humidityFrequencyMultiplier, temperatureFrequencyMultiplier,
    humidityNoise, temperatureNoise, octaves, lacunarity, gain,
    remapMinimum, remapMaximum, neuralWorldPeriod, neuralReferencePreview,
    postContrastHumidity, postContrastTemperature,
    postGammaHumidity, postGammaTemperature,
    attractionMode, attractionTargetHumidity, attractionTargetTemperature,
    attractionMaxShift, matrixBlend, geneticReferenceChannel,
    geneticSampleGrid, geneticGenerationsPerBatch,
    geneticMutationScale, geneticMinimumMutation,
    autoUpdate, showWindow);

ClimateNoiseLabComponent::ClimateNoiseLabComponent() = default;
ClimateNoiseLabComponent::~ClimateNoiseLabComponent() { stop(); }

void ClimateNoiseLabComponent::start()
{
    stop();
    m_error.clear();
    m_pc=Ge::Engine::getPtrClassAddr();
    if(!m_pc || !m_pc->hud)
    {
        m_error="Climate Noise Lab requires the ImGui HUD.";
        Ge::Debug::Warn("%s",m_error.c_str());
        return;
    }
    m_panel=std::make_unique<ClimateNoiseLabPanel>(*this);
    m_pc->hud->addImgui(m_panel.get());
}

void ClimateNoiseLabComponent::fixedUpdate() {}
void ClimateNoiseLabComponent::update() {}

void ClimateNoiseLabComponent::stop()
{
    if(m_panel && m_pc && m_pc->hud) m_pc->hud->removeImgui(m_panel.get());
    m_panel.reset();
    m_pc=nullptr;
}

void ClimateNoiseLabComponent::onGUI()
{
    ImGui::TextUnformatted("Climate Noise Lab");
    ImGui::Checkbox("Afficher la fenetre",&showWindow);
    ImGui::Checkbox("Mise a jour automatique",&autoUpdate);
    ImGui::TextWrapped("Ouvrez ClimateNoiseLab.scene et lancez Play. Le pattern neuronal scalaire s'entraine directement sur le GPU.");
    if(!m_error.empty()) ImGui::TextWrapped("%s",m_error.c_str());
}

std::string ClimateNoiseLabComponent::serialize() { return JS::serializeStruct(*this); }

void ClimateNoiseLabComponent::load(std::string data)
{
    JS::ParseContext context(data);
    if(context.parseTo(*this)!=JS::Error::NoError)
        Ge::Debug::Warn("Invalid ClimateNoiseLabComponent parameters; check scene script JSON.");
}
