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
    float climateFrequency = 0.0005f;
    float humidityFrequencyMultiplier = 1.0f;
    float temperatureFrequencyMultiplier = 1.0f;
    std::int32_t humidityNoise = 1;       // Simplex
    std::int32_t temperatureNoise = 1;    // Simplex
    std::int32_t octaves = 4;
    float lacunarity = 2.0f;
    float gain = 0.5f;
    float remapMinimum = -0.32f;
    float remapMaximum = 0.32f;
    std::int32_t attractionMode = 1;      // attractNearFifths01, fixed targets
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
