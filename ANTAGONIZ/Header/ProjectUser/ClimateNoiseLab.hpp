#ifndef ENDURANCE_CLIMATE_NOISE_LAB_COMPONENT_HPP
#define ENDURANCE_CLIMATE_NOISE_LAB_COMPONENT_HPP

#include "Behaviour.hpp"
#include <cstdint>
#include <memory>
#include <string>

struct ptrClass;
class ClimateNoiseLabPanel;

// Scene-owned diagnostic tool. It produces five GPU textures and displays them
// in a docked ImGui workspace; no scene renderer or camera is required.
class ClimateNoiseLabComponent final : public Behaviour
{
public:
    ClimateNoiseLabComponent();
    ~ClimateNoiseLabComponent() override;

    void start() override;
    void fixedUpdate() override;
    void update() override;
    void stop() override;
    void onGUI() override;
    std::string serialize() override;
    void load(std::string data) override;

    std::uint32_t seed = 1337;
    std::int32_t textureResolution = 1024;
    float worldExtent = 32768.0f;
    float worldOriginX = 0.0f;
    float worldOriginZ = 0.0f;

    // 0 = two independent scalar noises, 1 = exported coupled neural climate.
    std::int32_t climateSource = 0;
    float climateFrequency = 0.0005f;
    float humidityFrequencyMultiplier = 1.0f;
    float temperatureFrequencyMultiplier = 1.0f;
    std::int32_t humidityNoise = 1;
    std::int32_t temperatureNoise = 1;
    std::int32_t octaves = 4;
    float lacunarity = 2.0f;
    float gain = 0.5f;
    float remapMinimum = -0.32f;
    float remapMaximum = 0.32f;

    // One reference image occupies this many world metres in neural mode.
    float neuralWorldPeriod = 32768.0f;

    // Generic transformations applied after either source and before attraction.
    float postContrastHumidity = 1.0f;
    float postContrastTemperature = 1.0f;
    float postGammaHumidity = 1.0f;
    float postGammaTemperature = 1.0f;

    std::int32_t attractionMode = 1;
    float attractionTargetHumidity = 0.5f;
    float attractionTargetTemperature = 0.5f;
    float attractionMaxShift = 0.08f;
    bool matrixBlend = true;
    bool autoUpdate = true;
    bool showWindow = true;

private:
    friend class ClimateNoiseLabPanel;
    std::unique_ptr<ClimateNoiseLabPanel> m_panel;
    const ptrClass* m_pc = nullptr;
    std::string m_error;
};

#endif
