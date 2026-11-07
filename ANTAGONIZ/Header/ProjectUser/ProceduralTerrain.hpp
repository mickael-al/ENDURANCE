#ifndef ENDURANCE_PROCEDURAL_TERRAIN_COMPONENT_HPP
#define ENDURANCE_PROCEDURAL_TERRAIN_COMPONENT_HPP

#include "Behaviour.hpp"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class ProceduralGeneration;
struct ptrClass;
namespace Ge { class Camera; }

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
    std::uint32_t vertexBudgetMiB = 256;
    std::uint32_t maxResidentChunks = 2048;
    std::uint32_t maxSelectedChunks = 1024;
    float baseHeight = 0.0f;
    float heightAmplitude = 320.0f;
    float heightFrequency = 0.00015f;
    float climateFrequency = 0.0005f;
    float bandThickness = 64.0f;
    float bandVariation = 16.0f;
    float bandOffset = 0.0f;
    float bandOffsetVariation = 0.0f;
    float detailScale = 0.35f;
    float microDetailStrength = 0.15f;
    float microDetailScale = 2.0f;
    bool debugLod = false;
    bool collisionEnabled = true;
    float collisionRadius = 128.0f;

private:
    struct CameraRange
    {
        Ge::Camera* camera;
        float originalFar;
        float appliedFar;
    };
    void extendCameraRange(Ge::Camera* camera);
    std::unique_ptr<ProceduralGeneration> m_generator;
    const ptrClass* m_pc = nullptr;
    std::vector<CameraRange> m_cameraRanges;
    std::string m_error;
};

#endif
