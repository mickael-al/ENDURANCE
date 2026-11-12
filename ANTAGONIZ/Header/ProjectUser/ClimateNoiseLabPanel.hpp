#ifndef ENDURANCE_CLIMATE_NOISE_LAB_PANEL_HPP
#define ENDURANCE_CLIMATE_NOISE_LAB_PANEL_HPP

#include "ImguiBlock.hpp"
#include "imgui-cmake/Header/imgui.h"
#include "glcore.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

class ClimateNoiseLabComponent;

class ClimateNoiseLabPanel final : public Ge::ImguiBlock
{
public:
    explicit ClimateNoiseLabPanel(ClimateNoiseLabComponent& owner);
    ~ClimateNoiseLabPanel() override;
    void init(GraphicsDataMisc*) override;
    void render(GraphicsDataMisc*) override;

private:
    static constexpr std::size_t TextureCount = 5;
    static constexpr std::uint32_t GeneticPopulation = 128;
    static constexpr std::uint32_t GeneticCropSize = 512;
    static constexpr std::uint32_t NeuralParameterCount = 329;

    struct Uniforms
    {
        GLint resolution=-1, seed=-1, worldOrigin=-1, worldExtent=-1;
        GLint climateFrequency=-1, neuralWorldPeriod=-1, neuralReferencePreview=-1;
        GLint humidityFrequencyMultiplier=-1, temperatureFrequencyMultiplier=-1;
        GLint humidityNoiseType=-1, temperatureNoiseType=-1;
        GLint octaves=-1, lacunarity=-1, gain=-1;
        GLint remapMinimum=-1, remapMaximum=-1;
        GLint postContrast=-1, postGamma=-1;
        GLint attractionMode=-1, attractionTarget=-1, attractionMaxShift=-1;
        GLint matrixBlend=-1, biomeMatrix=-1;
    } m_uniforms;

    struct TrainingUniforms
    {
        GLint pass=-1, populationSize=-1, sourceIndex=-1, generation=-1;
        GLint seed=-1, referenceSize=-1, cropOffset=-1, cropSize=-1;
        GLint sampleGrid=-1, mutationScale=-1, referenceImage=-1;
        GLint referenceChannel=-1;
    } m_trainingUniforms;

    struct TrainingState
    {
        float bestScore=0.0f;
        std::uint32_t bestGeneration=0;
        std::uint32_t generation=0;
        std::uint32_t sourceIndex=0;
    };

    ClimateNoiseLabComponent& m_owner;
    GLuint m_program=0;
    GLuint m_trainingProgram=0;
    std::array<GLuint,TextureCount> m_textures{};
    GLuint m_referenceTexture=0;
    GLuint m_populationBuffer=0;
    GLuint m_scoreBuffer=0;
    GLuint m_bestBuffer=0;
    GLuint m_stateBuffer=0;
    GLsync m_trainingFence=nullptr;

    int m_allocatedResolution=0;
    int m_referenceWidth=0;
    int m_referenceHeight=0;
    std::uint32_t m_populationSource=0;
    std::uint32_t m_submittedGeneration=0;
    std::uint32_t m_completedGeneration=0;
    float m_bestScore=0.0f;

    bool m_dirty=true;
    bool m_generateRequested=false;
    bool m_resetDock=false;
    bool m_training=false;
    bool m_populationInitialized=false;
    bool m_modelLoaded=false;
    std::string m_modelStatus;

    void release();
    bool waitForTraining();
    bool reloadShaders();
    void discoverUniforms();
    void discoverTrainingUniforms();
    bool loadReferenceImage();
    void allocateTextures();
    void allocateTrainingBuffers();
    void resetTraining(bool preserveModel);
    void submitTrainingBatch();
    void pollTraining();
    bool saveModel();
    bool loadModel(bool reportMissing=false);
    void generate();
    void buildDockLayout(ImGuiID dockspace, ImVec2 size);
    void drawSettings();
    void drawTextureWindow(const char* name, GLuint texture, const char* description);
};

#endif
