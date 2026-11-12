// Exercise the production panel's resource/model state machine in a real GL
// context. Only the scene registration lifecycle is stubbed; all panel methods,
// GLSL, buffers, image decoding and model I/O below are the production code.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#include "Behaviour.hpp"
#include "ImguiBlock.hpp"
#include "glcore.hpp"
#define private public
#include "ClimateNoiseLab.hpp"
#include "ClimateNoiseLabPanel.hpp"
#undef private

#define STB_IMAGE_IMPLEMENTATION
#include "stb-cmake/stb_image.h"
#undef STB_IMAGE_IMPLEMENTATION
#include "../../ANTAGONIZ/Source/ProjectUser/ClimateNoiseLabPanel.cpp"

ClimateNoiseLabComponent::ClimateNoiseLabComponent() = default;
ClimateNoiseLabComponent::~ClimateNoiseLabComponent() = default;
void ClimateNoiseLabComponent::start() {}
void ClimateNoiseLabComponent::fixedUpdate() {}
void ClimateNoiseLabComponent::update() {}
void ClimateNoiseLabComponent::stop() {}
void ClimateNoiseLabComponent::onGUI() {}
std::string ClimateNoiseLabComponent::serialize() { return {}; }
void ClimateNoiseLabComponent::load(std::string) {}

namespace
{
void requirePanel(bool condition, const std::string& message)
{
    if (!condition) throw std::runtime_error(message);
}

std::vector<float> readWeights(GLuint buffer)
{
    std::vector<float> weights(ClimateNoiseLabPanel::NeuralParameterCount);
    glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);
    glGetNamedBufferSubData(buffer, 0, weights.size() * sizeof(float), weights.data());
    return weights;
}

void writeModelFile(ModelHeader header, const std::vector<float>& weights)
{
    std::ofstream output(ModelPath, std::ios::binary | std::ios::trunc);
    output.write(reinterpret_cast<const char*>(&header), sizeof(header));
    output.write(reinterpret_cast<const char*>(weights.data()), weights.size() * sizeof(float));
    requirePanel(output.good(), "Cannot write temporary model fixture");
}

void expectRejected(ClimateNoiseLabPanel& panel, const ModelHeader& header,
                    const std::vector<float>& weights, const char* reason)
{
    const auto before = readWeights(panel.m_bestBuffer);
    const auto seed = panel.m_owner.seed;
    const auto generation = panel.m_completedGeneration;
    writeModelFile(header, weights);
    requirePanel(!panel.loadModel(true), std::string("Accepted invalid model: ") + reason);
    requirePanel(readWeights(panel.m_bestBuffer) == before,
                 std::string("Rejected model mutated GPU weights: ") + reason);
    requirePanel(panel.m_owner.seed == seed && panel.m_completedGeneration == generation,
                 std::string("Rejected model mutated CPU state: ") + reason);
}
}

extern "C" int climate_panel_tests()
{
    try
    {
        glewExperimental = GL_TRUE;
        const auto status = glewInit();
        requirePanel(status == GLEW_OK || status == GLEW_ERROR_NO_GLX_DISPLAY,
                     "GLEW initialization failed");
        requirePanel(GLEW_VERSION_4_5, "Panel tests require OpenGL 4.5");
        while (glGetError() != GL_NO_ERROR) {}

        ClimateNoiseLabComponent owner;
        owner.geneticSampleGrid = 8;
        owner.geneticGenerationsPerBatch = 1;
        owner.textureResolution = 64;
        ClimateNoiseLabPanel panel(owner);
        panel.init(nullptr);
        requirePanel(panel.m_program && panel.m_trainingProgram && panel.m_referenceTexture,
                     "Panel initialization failed: " + owner.m_error);
        requirePanel(panel.m_referenceWidth == 8 && panel.m_referenceHeight == 12,
                     "Reference dimensions do not match the non-square fixture");

        // Leave a valid completed/pending GPU batch unpolled, exactly like a
        // reset button clicked between submission and the next render frame.
        panel.resetTraining(false);
        panel.submitTrainingBatch();
        requirePanel(panel.m_trainingFence != nullptr, "No pending training fence");
        panel.resetTraining(false);
        requirePanel(!panel.m_trainingFence && !panel.m_modelLoaded,
                     "Reset from zero resurrected the preceding model");
        requirePanel(panel.m_submittedGeneration == 0 && panel.m_completedGeneration == 0,
                     "Reset from zero retained the preceding generation");
        const auto neutral = readWeights(panel.m_bestBuffer);
        requirePanel(std::all_of(neutral.begin(), neutral.end(), [](float v) { return v == 0; }),
                     "Reset from zero retained preceding weights");

        const GLuint texture = panel.m_referenceTexture;
        const GLuint population = panel.m_populationBuffer;
        panel.m_training = true;
        std::filesystem::rename(ReferencePath, std::string(ReferencePath) + ".backup");
        requirePanel(!panel.loadReferenceImage(), "Missing reference image unexpectedly loaded");
        requirePanel(panel.m_referenceTexture == texture && panel.m_referenceWidth == 8 &&
                     panel.m_referenceHeight == 12 && panel.m_populationBuffer == population &&
                     panel.m_training, "Failed reload damaged the working reference/training state");
        std::filesystem::rename(std::string(ReferencePath) + ".backup", ReferencePath);
        // Loading must not interpret a CPU pointer as a PBO offset or inherit
        // image strides / a global stb flip left by another engine loader.
        GLuint unpackBuffer = 0;
        glCreateBuffers(1, &unpackBuffer);
        glNamedBufferData(unpackBuffer, 2048, nullptr, GL_DYNAMIC_COPY);
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, unpackBuffer);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 8);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 17);
        glPixelStorei(GL_UNPACK_SKIP_ROWS, 1);
        glPixelStorei(GL_UNPACK_SKIP_PIXELS, 2);
        stbi_set_flip_vertically_on_load(true);
        requirePanel(panel.loadReferenceImage(), "Valid reference reload failed: " + owner.m_error);
        requirePanel(owner.m_error.empty(), "Successful reload retained its preceding error");
        requirePanel(!panel.m_training && !panel.m_populationInitialized && !panel.m_modelLoaded,
                     "Reference reload did not pause and invalidate training");
        GLint actual = 0;
        glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, &actual);
        requirePanel(static_cast<GLuint>(actual) == unpackBuffer, "Reload changed unpack buffer");
        const std::array<std::pair<GLenum, GLint>, 4> unpackState{{
            {GL_UNPACK_ALIGNMENT, 8}, {GL_UNPACK_ROW_LENGTH, 17},
            {GL_UNPACK_SKIP_ROWS, 1}, {GL_UNPACK_SKIP_PIXELS, 2}
        }};
        for (const auto& setting : unpackState)
        {
            glGetIntegerv(setting.first, &actual);
            requirePanel(actual == setting.second, "Reload changed pixel unpack settings");
        }
        std::array<unsigned char, 8 * 12 * 4> referencePixels{};
        glGetTextureImage(panel.m_referenceTexture, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                          referencePixels.size(), referencePixels.data());
        requirePanel(referencePixels[1] == 0 && referencePixels[(8 * 11) * 4 + 1] == 231,
                     "Image upload inherited a flip/stride from the engine");
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
        glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
        glDeleteBuffers(1, &unpackBuffer);

        ModelHeader valid;
        valid.seed = 42;
        valid.bestScore = .125f;
        valid.generation = 37;
        valid.referenceChannel = 2;
        std::vector<float> weights(ClimateNoiseLabPanel::NeuralParameterCount, .125f);
        auto bad = valid;
        bad.bestScore = -.5f;
        expectRejected(panel, bad, weights, "negative score");
        bad.bestScore = std::numeric_limits<float>::quiet_NaN();
        expectRejected(panel, bad, weights, "NaN score");
        bad.bestScore = std::numeric_limits<float>::infinity();
        expectRejected(panel, bad, weights, "infinite score");
        auto badWeights = weights;
        badWeights[17] = std::numeric_limits<float>::quiet_NaN();
        expectRejected(panel, valid, badWeights, "NaN weight");
        badWeights[17] = std::numeric_limits<float>::infinity();
        expectRejected(panel, valid, badWeights, "infinite weight");
        badWeights[17] = 3.01f;
        expectRejected(panel, valid, badWeights, "out-of-range weight");
        badWeights.pop_back();
        expectRejected(panel, valid, badWeights, "truncated weights");
        badWeights = weights;
        badWeights.push_back(0);
        expectRejected(panel, valid, badWeights, "trailing data");

        bad=valid;
        bad.flags=2u;
        expectRejected(panel,bad,weights,"unknown model flags");
        bad.flags=0u;
        expectRejected(panel,bad,weights,"unvalidated nonzero score");
        valid.version=2u; // Existing saved weights remain compatible.
        valid.flags=0u;
        owner.autoUpdate=false;
        panel.m_generateRequested=false;
        writeModelFile(valid, weights);
        requirePanel(panel.loadModel(true), "Valid model rejected: " + owner.m_error);
        requirePanel(readWeights(panel.m_bestBuffer) == weights && owner.seed == valid.seed &&
                     owner.geneticReferenceChannel == 2, "Loaded model data changed");
        requirePanel(!std::isfinite(panel.m_bestScore) || panel.m_bestScore >= 1e20f,
                     "Loaded model retained an incomparable score from another reference");
        requirePanel(panel.m_generateRequested,"Loading with auto-update off did not request preview refresh");
        requirePanel(panel.saveModel(),"Loaded weights with unknown score could not be saved: "+owner.m_error);
        requirePanel(!panel.m_modelStatus.empty(),"Successful save did not report a destination");
        {
            std::ifstream saved(ModelPath,std::ios::binary);
            ModelHeader header;
            saved.read(reinterpret_cast<char*>(&header),sizeof(header));
            requirePanel(saved && header.version==3u && header.flags==0u && header.bestScore==0.0f,
                         "Save without validation did not mark its score unknown");
        }
        // A second save replaces the existing file; a failed write keeps it.
        requirePanel(panel.saveModel(),"Replacing an existing pattern failed");
        auto readFile=[]() {
            std::ifstream input(ModelPath,std::ios::binary);
            return std::string(std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>());
        };
        const auto savedBytes=readFile();
        std::filesystem::create_directory(std::string(ModelPath)+".tmp");
        requirePanel(!panel.saveModel(),"Invalid temporary destination unexpectedly writable");
        requirePanel(readFile()==savedBytes,"Failed save damaged previous model");
        std::filesystem::remove(std::string(ModelPath)+".tmp");
        requirePanel(panel.loadModel(true),"Unvalidated v3 model could not be loaded");
        requirePanel(readWeights(panel.m_bestBuffer)==weights,"Model round trip changed weights");
        panel.resetTraining(true);
        requirePanel(readWeights(panel.m_bestBuffer) == weights &&
                     readWeights(panel.m_populationBuffer) == weights,
                     "Resume discarded the loaded candidate before validation");

        glUseProgram(panel.m_program);
        panel.submitTrainingBatch();
        GLint currentProgram = 0;
        glGetIntegerv(GL_CURRENT_PROGRAM, &currentProgram);
        requirePanel(static_cast<GLuint>(currentProgram) == panel.m_program,
                     "Training changed the caller's current GL program");
        panel.waitForTraining();
        requirePanel(std::isfinite(panel.m_bestScore) && panel.m_bestScore >= 0 &&
                     panel.m_bestScore < 1e20f, "Loaded model was not revalidated");
        requirePanel(panel.saveModel(), "Validated model could not be saved: " + owner.m_error);
        panel.generate();
        requirePanel(!panel.m_dirty, "Preview generation failed: " + owner.m_error);
        const auto trainedWeights = readWeights(panel.m_bestBuffer);
        requirePanel(panel.reloadShaders(), "Shader reload failed: " + owner.m_error);
        requirePanel(readWeights(panel.m_bestBuffer) == trainedWeights,
                     "Shader reload discarded trained weights");
        requirePanel(panel.m_bestScore >= 1e20f && panel.saveModel(),
                     "Shader reload blocked saving valid weights without a score");
        panel.resetTraining(false);
        requirePanel(!panel.saveModel(),"An untrained zero-reset pattern overwrote the save");
        requirePanel(panel.loadModel(true) && readWeights(panel.m_bestBuffer)==trainedWeights,
                     "Shader reload/save/reset/load round trip changed the pattern");
        panel.generate();
        requirePanel(!panel.m_dirty, "Preview after shader reload failed: " + owner.m_error);
        // New appended source IDs must pass through the CPU selector clamp;
        // otherwise selecting an experimental noise silently evaluates Neural.
        owner.humidityNoise=12;
        owner.temperatureNoise=13;
        owner.binaryTransitionHumidity=.1f;
        owner.binaryTransitionTemperature=.2f;
        panel.generate();
        GLint selected=0;
        glGetUniformiv(panel.m_program,panel.m_uniforms.humidityNoiseType,&selected);
        requirePanel(selected==12,"CPU rejected the experimental Perlin source");
        glGetUniformiv(panel.m_program,panel.m_uniforms.temperatureNoiseType,&selected);
        requirePanel(selected==13,"CPU rejected the experimental fBm source");
        std::array<GLfloat,2> transition{};
        glGetUniformfv(panel.m_program,panel.m_uniforms.binaryTransition,transition.data());
        requirePanel(transition[0]==.1f && transition[1]==.2f,
                     "Per-axis transition widths did not reach the preview shader");
        owner.binaryTransitionHumidity=-1;
        owner.binaryTransitionTemperature=2;
        panel.generate();
        glGetUniformfv(panel.m_program,panel.m_uniforms.binaryTransition,transition.data());
        requirePanel(transition[0]==0 && transition[1]==1,
                     "Out-of-range transition widths were not bounded");
        requirePanel(glGetError() == GL_NO_ERROR, "OpenGL error in production panel tests");
        std::cout << "PASS climate panel: pending reset, reference reload, model validation/resume, shader reload, preview\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL climate panel: " << error.what() << '\n';
        return 1;
    }
}
