#pragma once
#include "FixtureServices.hpp"
#include "ProceduralTerrainField.hpp"
class ProceduralGeneration final : public CustomRenderer
{
public:
    struct Config { ProceduralTerrain::FieldParameters field; };
    explicit ProceduralGeneration(SceneData& value):scene(value) {}
    bool initialized=true;
    std::uint32_t seed=1337;
    Config config;
    bool isInitialized() const { return initialized; }
    const Config& getConfig() const { return config; }
    std::uint32_t getSeed() const { return seed; }
    SceneData& getScene() { return scene; }
    void drawRenderer() override {}
private:
    SceneData& scene;
};
