#ifndef ENDURANCE_PROCEDURAL_TERRAIN_COMPONENT_HPP
#define ENDURANCE_PROCEDURAL_TERRAIN_COMPONENT_HPP

#include "Behaviour.hpp"
#include "ProceduralTerrainMaterials.hpp"
#include <cstdint>
#include <memory>
#include <string>

class ProceduralGeneration;
namespace Endurance { class SeaRenderer; class TreeRenderer; }
struct ptrClass;

// Scene-owned runtime component. Parameters are saved in the editor's script
// JSON; runtime buffers and generated geometry never become scene assets.
class ProceduralTerrainComponent final : public Behaviour
{
public:
    ProceduralTerrainComponent();
    ~ProceduralTerrainComponent() override;
    void start() override;
    void fixedUpdate() override;
    void update() override;
    void stop() override;
    void onGUI() override;
    std::string serialize() override;
    void load(std::string data) override;

    std::uint32_t seed = 1337;
    float cellSize = 1.0f;
    std::uint32_t maxLod = 6;
    float viewDistance = 4096.0f;
    float lodDistanceFactor = 1.25f;
    std::uint32_t maxChunksPerFrame = 2;
    std::uint32_t maxInFlight = 2;
    std::uint32_t columnCacheEntries = 128;
    std::uint32_t densityCacheEntries = 32;
    bool measureGenerationTimings = true;
    std::uint32_t vertexBudgetMiB = 256;
    std::uint32_t maxResidentChunks = 2048;
    std::uint32_t maxSelectedChunks = 2048;
    float baseHeight = 0.0f;
    float heightAmplitude = 320.0f;
    float heightFrequency = 0.00015f;
    float bandOffset = 0.0f;
    float bandOffsetVariation = 0.0f;
    float detailScale = 0.35f;
    float seaLevel = 0.0f;
    float seaBlendWidth = 64.0f;
    // -1 uses the matrices; 0..24 follows the image row by row; 25 is marine.
    std::int32_t biomeOverride = -1;
    float microDetailStrength = 0.15f;
    float microDetailScale = 2.0f;
    ProceduralTerrain::TerrainAppearance appearance;
    std::uint32_t terrainAppearanceRevision = 0;
    bool debugLod = false;
    bool collisionEnabled = true;
    float collisionRadius = 128.0f;
    bool objectPlacementEnabled = true;
    std::string objectPlacementFile = "../ENDURANCE/Ressources/Trees/object_placement.json";
    bool treeRenderingEnabled = true;
    float treeRenderDistance = 10240.0f;
    float treeLod0Coverage = 0.25f;
    float treeImpostorCoverage = 0.06f;
    float treeLodTransition = 0.15f;
    std::uint32_t treeModelBudgetMiB = 512;
    float treeFoliageNormalBlend = 0.8f;
    bool treeWindEnabled = true;
    float treeWindStrength = 0.015f;
    float treeWindSpeedX = 6.0f, treeWindSpeedZ = 2.0f;
    float treeWindScale = 64.0f, treeWindDistance = 256.0f;

private:
    std::unique_ptr<ProceduralGeneration> m_generator;
    std::unique_ptr<Endurance::SeaRenderer> m_sea;
    std::unique_ptr<Endurance::TreeRenderer> m_trees;
    const ptrClass* m_pc = nullptr;
    std::string m_error;
    std::string m_materialError;
    std::string m_seaError;
    std::string m_objectError;
    std::string m_treeError;
    bool m_showSea = true;
    int m_materialBiome = 0;
};

#endif
