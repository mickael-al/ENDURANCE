#ifndef ENDURANCE_CLIMATE_NOISE_LAB_COMPONENT_HPP
#define ENDURANCE_CLIMATE_NOISE_LAB_COMPONENT_HPP

#include "Behaviour.hpp"
#include <cstdint>
#include <memory>
#include <string>

struct ptrClass;
class ClimateNoiseLabPanel;

// Scene-owned diagnostic tool. It displays climate textures and owns a small
// GPU genetic trainer for one fixed scalar 4x8 tanh pattern network.
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
    float humidityFrequencyMultiplier = 0.1f;
    float temperatureFrequencyMultiplier = 0.1f;
    std::int32_t humidityNoise = 13;
    std::int32_t temperatureNoise = 13;
    std::int32_t octaves = 8;
    float lacunarity = 2.0f;
    float gain = 0.5f;
    // Adjustable reference for the validated x5 Simplex climate.
    // Zero restores the binary threshold; 0.08 blends over [-0.08, +0.08].
    float binaryTransitionHumidity = 0.08f;
    float binaryTransitionTemperature = 0.08f;
    float remapMinimum = -1.0f;
    float remapMaximum = 1.0f;

    // Reference period in metres when climateFrequency is 0.0005.
    float neuralWorldPeriod = 32768.0f;
    // Diagnostic view of the learned reference, before per-axis seed variation.
    bool neuralReferencePreview = false;

    // Generic transformations applied after either source and before attraction.
    float postContrastHumidity = 1.5f;
    float postContrastTemperature = 1.5f;
    float postGammaHumidity = 1.5f;
    float postGammaTemperature = 2.0f;

    std::int32_t attractionMode = 2;
    float attractionTargetHumidity = 0.5f;
    float attractionTargetTemperature = 0.5f;
    float attractionMaxShift = 0.0468f;
    bool matrixBlend = true;

    // 0=R, 1=G, 2=B, 3=luminance. Only one channel trains the scalar pattern.
    std::int32_t geneticReferenceChannel = 0;
    std::int32_t geneticSampleGrid = 32;
    std::int32_t geneticGenerationsPerBatch = 2;
    float geneticMutationScale = 0.18f;
    float geneticMinimumMutation = 0.015f;

    bool autoUpdate = true;
    bool showWindow = true;

private:
    friend class ClimateNoiseLabPanel;
    std::unique_ptr<ClimateNoiseLabPanel> m_panel;
    const ptrClass* m_pc = nullptr;
    std::string m_error;
};

#endif
