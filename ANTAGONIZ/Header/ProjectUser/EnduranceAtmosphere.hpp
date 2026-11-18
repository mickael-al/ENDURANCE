#pragma once

#include "Behaviour.hpp"
#include "CustomRenderer.hpp"
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#ifndef JS_STL_ARRAY
#define JS_STL_ARRAY
#endif
#include "json_struct.h"

// All biome-specific data belongs to the game. Colors are linear scene RGB.
struct EnduranceSkyBiomeStyle
{
    std::string biome;
    std::array<float,3> horizon{{0.58f,0.69f,0.76f}};
    std::array<float,3> middle{{0.19f,0.36f,0.53f}};
    std::array<float,3> zenith{{0.045f,0.13f,0.29f}};
    float middleElevation=0.25f, gradientPower=1.0f, strength=0.38f, exposure=1.0f;
    float fogDensity=0.0f;
    std::array<float,3> fogAlbedo{{0.85f,0.90f,0.95f}};
    float fogBaseHeight=0.0f, fogHeightScale=150.0f;
    float fogNoiseAmount=0.0f, fogNoiseScale=0.002f;
    JS_OBJ(biome,horizon,middle,zenith,middleElevation,gradientPower,strength,exposure,
           fogDensity,fogAlbedo,fogBaseHeight,fogHeightScale,fogNoiseAmount,fogNoiseScale);
};

class EnduranceAtmosphereComponent final : public Behaviour, public CustomRenderer
{
public:
    EnduranceAtmosphereComponent();
    ~EnduranceAtmosphereComponent() override;
    void start() override;
    void stop() override;
    void fixedUpdate() override {}
    void update() override {}
    // Renderer callback remains active while the editor pauses behaviours.
    void prepareRenderer() override;
    void drawRenderer() override {}
    void onGUI() override;
    std::string serialize() override;
    void load(std::string data) override;

    std::string paletteFile="../ENDURANCE/Ressources/Atmosphere/biome_palettes.json";
    std::vector<EnduranceSkyBiomeStyle> palettes;
    std::string engineSettings;
    // -2 = first directional fallback; -1 = deliberately unassigned.
    int sunLightIndex=-2, moonLightIndex=-1;
    // Zero follows quality (Ultra=128). Positive values override that budget.
    // Fog coordinates remain fixed between atlas transactions.
    int fogAtlasResolution=0;
    float fogAtlasExtent=81920.0f;

private:
    struct Impl;
    std::unique_ptr<Impl> m;
    bool loadPaletteFile();
};
