#include "ClimateNoiseLabPanel.hpp"
#include "ClimateNoiseLab.hpp"
#include "ProceduralTerrainBiomes.hpp"
#include "ShaderCompilation.hpp"
#include "imgui-cmake/Header/imgui.h"
#include "imgui-cmake/Header/imgui_internal.h"
#include "stb-cmake/stb_image.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <limits>
#include <string>
#include <vector>

namespace
{
constexpr std::array<const char*,12> NoiseNames{{
    "Perlin","Simplex","Value quintique","Value cubique","Blanc par cellule",
    "Voronoi","Valeur de cellule","fBm Perlin","fBm Simplex","fBm Value",
    "Patchwork de regions","Neural Pattern GPU"
}};
constexpr std::array<const char*,4> ReferenceChannelNames{{
    "Rouge","Vert","Bleu","Luminance"
}};
constexpr std::array<const char*,3> AttractionNames{{
    "Aucune","attractNearFifths01 (cible fixe)","Niveau matrice le plus proche"
}};
constexpr std::array<int,5> Resolutions{{256,512,1024,1536,2048}};
constexpr int NeuralPatternNoise = 11;
constexpr const char* ReferencePath = "../Asset/Shader/ClimateNoiseLab/climate_reference.png";
constexpr const char* ModelPath = "../Asset/Shader/ClimateNoiseLab/neural_pattern_model.bin";
constexpr std::uint32_t ModelMagic = 0x4d54504eu;
constexpr std::uint32_t ModelVersion = 2u;

struct ModelHeader
{
    std::uint32_t magic=ModelMagic;
    std::uint32_t version=ModelVersion;
    std::uint32_t parameterCount=329u;
    std::uint32_t seed=0u;
    float bestScore=0.0f;
    std::uint32_t generation=0u;
    std::uint32_t referenceChannel=0u;
    std::uint32_t reserved=0u;
};
static_assert(sizeof(ModelHeader)==32,"Stable Neural Pattern model header expected");

// The lab runs inside the HUD; compute dispatches must not leak their program
// into the renderer that called us.
struct ScopedProgram
{
    GLint previous=0;
    ScopedProgram() { glGetIntegerv(GL_CURRENT_PROGRAM,&previous); }
    ~ScopedProgram() { glUseProgram(static_cast<GLuint>(previous)); }
};

bool finiteFloat(float value)
{
    // Release builds enable fast-math, which may fold std::isfinite to true.
    // Inspect the IEEE-754 exponent before trusting floats read from disk/GPU.
    static_assert(sizeof(float)==sizeof(std::uint32_t),"32-bit model floats expected");
    std::uint32_t bits=0;
    std::memcpy(&bits,&value,sizeof(bits));
    return (bits & 0x7f800000u)!=0x7f800000u;
}

bool validWeights(const std::vector<float>& weights)
{
    return std::all_of(weights.begin(),weights.end(),[](float value) {
        return finiteFloat(value) && std::abs(value)<=3.0f;
    });
}

ImTextureID textureId(GLuint texture)
{ return reinterpret_cast<ImTextureID>(static_cast<std::intptr_t>(texture)); }

std::uint32_t hash32(std::uint32_t value)
{
    value ^= value >> 16u;
    value *= 0x7feb352du;
    value ^= value >> 15u;
    value *= 0x846ca68bu;
    return value ^ (value >> 16u);
}
}

ClimateNoiseLabPanel::ClimateNoiseLabPanel(ClimateNoiseLabComponent& owner):m_owner(owner) {}
ClimateNoiseLabPanel::~ClimateNoiseLabPanel() { release(); }

void ClimateNoiseLabPanel::init(GraphicsDataMisc*)
{
    reloadShaders();
    loadReferenceImage();
    allocateTrainingBuffers();
    loadModel(false);
    m_dirty=true;
}

void ClimateNoiseLabPanel::render(GraphicsDataMisc*)
{
    pollTraining();
    if(!m_owner.showWindow) return;

    ImGui::SetNextWindowSize(ImVec2(1600,920),ImGuiCond_FirstUseEver);
    if(!ImGui::Begin("Climate Noise Lab",nullptr,ImGuiWindowFlags_MenuBar|ImGuiWindowFlags_NoCollapse))
    { ImGui::End(); return; }
    if(ImGui::BeginMenuBar())
    {
        if(ImGui::MenuItem("Generer l'apercu")) m_generateRequested=true;
        if(ImGui::MenuItem("Recharger les shaders")) reloadShaders();
        if(ImGui::MenuItem("Reinitialiser le docking")) m_resetDock=true;
        ImGui::EndMenuBar();
    }
    const ImGuiID dockspace=ImGui::GetID("ClimateNoiseLabDockspace");
    const ImVec2 dockSize=ImGui::GetContentRegionAvail();
    if(m_resetDock || ImGui::DockBuilderGetNode(dockspace)==nullptr)
    { buildDockLayout(dockspace,dockSize); m_resetDock=false; }
    ImGui::DockSpace(dockspace,dockSize,ImGuiDockNodeFlags_None);
    ImGui::End();

    drawSettings();
    if((m_owner.autoUpdate&&m_dirty)||m_generateRequested)
    { generate(); m_generateRequested=false; }

    drawTextureWindow("Reference scalaire##ClimateNoiseLab",m_referenceTexture,
        "Image d'entrainement. Un seul canal selectionne sert de cible au pattern.");
    drawTextureWindow("Temperature##ClimateNoiseLab",m_textures[0],
        "Valeur [0,1] en niveaux de gris.");
    drawTextureWindow("Humidite##ClimateNoiseLab",m_textures[1],
        "Valeur [0,1] en niveaux de gris.");
    drawTextureWindow("Climat brut (R=T, G=H)##ClimateNoiseLab",m_textures[2],
        "Sources puis contraste/gamma, avant attraction.");
    drawTextureWindow("Apres attraction (R=T, G=H)##ClimateNoiseLab",m_textures[3],
        "Meme codage apres le traitement selectionne.");
    drawTextureWindow("Resultat matrice##ClimateNoiseLab",m_textures[4],
        "Palette des 25 biomes depuis le climat traite.");

    if(m_training && !m_trainingFence) submitTrainingBatch();
}

void ClimateNoiseLabPanel::release()
{
    m_training=false;
    waitForTraining();
    if(m_trainingFence) { glDeleteSync(m_trainingFence); m_trainingFence=nullptr; }
    if(m_program) glDeleteProgram(m_program);
    if(m_trainingProgram) glDeleteProgram(m_trainingProgram);
    if(m_textures[0]) glDeleteTextures(static_cast<GLsizei>(m_textures.size()),m_textures.data());
    if(m_referenceTexture) glDeleteTextures(1,&m_referenceTexture);
    const GLuint buffers[]={m_populationBuffer,m_scoreBuffer,m_bestBuffer,m_stateBuffer};
    glDeleteBuffers(4,buffers);
    m_program=m_trainingProgram=0;
    m_textures.fill(0);
    m_referenceTexture=0;
    m_populationBuffer=m_scoreBuffer=m_bestBuffer=m_stateBuffer=0;
    m_allocatedResolution=0;
}

bool ClimateNoiseLabPanel::waitForTraining()
{
    if(!m_trainingFence) return true;
    const GLenum result=glClientWaitSync(m_trainingFence,GL_SYNC_FLUSH_COMMANDS_BIT,GL_TIMEOUT_IGNORED);
    if(result!=GL_ALREADY_SIGNALED && result!=GL_CONDITION_SATISFIED)
    {
        m_training=false;
        m_owner.m_error="Echec de la synchronisation de l'entrainement GPU.";
        return false;
    }
    glDeleteSync(m_trainingFence);
    m_trainingFence=nullptr;
    if(m_stateBuffer)
    {
        TrainingState state{};
        glGetNamedBufferSubData(m_stateBuffer,0,sizeof(state),&state);
        m_bestScore=state.bestScore;
        m_completedGeneration=state.generation;
        m_populationSource=state.sourceIndex;
        if(finiteFloat(m_bestScore) && m_bestScore<1.0e20f) m_modelLoaded=true;
        m_dirty=true;
    }
    return true;
}

bool ClimateNoiseLabPanel::reloadShaders()
{
    if(!waitForTraining()) return false;
    std::string previewError,trainingError;
    Ge::ShaderCacheOptions cache; cache.enabled=true;
    const GLuint preview=Ge::buildShaderProgram({
        {GL_COMPUTE_SHADER,"../Asset/Shader/ClimateNoiseLab/climate_noise_lab.C.glsl",{}}
    },previewError,"Climate Noise Lab preview",cache);
    if(!preview)
    {
        m_owner.m_error=previewError.empty()?"Impossible de compiler l'apercu climatique.":previewError;
        return false;
    }
    const GLuint trainer=Ge::buildShaderProgram({
        {GL_COMPUTE_SHADER,"../Asset/Shader/ClimateNoiseLab/neural_genetic_train.C.glsl",{}}
    },trainingError,"Climate Noise Lab genetic trainer",cache);
    if(!trainer)
    {
        glDeleteProgram(preview);
        m_owner.m_error=trainingError.empty()?"Impossible de compiler l'entrainement genetique.":trainingError;
        return false;
    }

    GLint boundProgram=0;
    glGetIntegerv(GL_CURRENT_PROGRAM,&boundProgram);
    if(m_program && static_cast<GLuint>(boundProgram)==m_program)
        glUseProgram(preview);
    else if(m_trainingProgram && static_cast<GLuint>(boundProgram)==m_trainingProgram)
        glUseProgram(trainer);
    if(m_program) glDeleteProgram(m_program);
    if(m_trainingProgram) glDeleteProgram(m_trainingProgram);
    m_program=preview;
    m_trainingProgram=trainer;
    discoverUniforms();
    discoverTrainingUniforms();
    if(m_stateBuffer)
    {
        // The network/features or objective may have changed on disk. Keep the
        // weights, but refresh their score under the new code before exporting.
        m_bestScore=std::numeric_limits<float>::max();
        glNamedBufferSubData(m_stateBuffer,0,sizeof(m_bestScore),&m_bestScore);
    }
    m_owner.m_error.clear();
    m_dirty=true;
    return true;
}

void ClimateNoiseLabPanel::discoverUniforms()
{
    const auto get=[&](const char* name){return glGetUniformLocation(m_program,name);};
    m_uniforms.resolution=get("uResolution"); m_uniforms.seed=get("uSeed");
    m_uniforms.worldOrigin=get("uWorldOrigin"); m_uniforms.worldExtent=get("uWorldExtent");
    m_uniforms.climateFrequency=get("uClimateFrequency");
    m_uniforms.neuralWorldPeriod=get("uNeuralWorldPeriod");
    m_uniforms.humidityFrequencyMultiplier=get("uHumidityFrequencyMultiplier");
    m_uniforms.temperatureFrequencyMultiplier=get("uTemperatureFrequencyMultiplier");
    m_uniforms.humidityNoiseType=get("uHumidityNoiseType");
    m_uniforms.temperatureNoiseType=get("uTemperatureNoiseType");
    m_uniforms.octaves=get("uOctaves"); m_uniforms.lacunarity=get("uLacunarity");
    m_uniforms.gain=get("uGain"); m_uniforms.remapMinimum=get("uRemapMinimum");
    m_uniforms.remapMaximum=get("uRemapMaximum");
    m_uniforms.postContrast=get("uPostContrast"); m_uniforms.postGamma=get("uPostGamma");
    m_uniforms.attractionMode=get("uAttractionMode");
    m_uniforms.attractionTarget=get("uAttractionTarget");
    m_uniforms.attractionMaxShift=get("uAttractionMaxShift");
    m_uniforms.matrixBlend=get("uMatrixBlend"); m_uniforms.biomeMatrix=get("uBiomeMatrix");
}

void ClimateNoiseLabPanel::discoverTrainingUniforms()
{
    const auto get=[&](const char* name){return glGetUniformLocation(m_trainingProgram,name);};
    m_trainingUniforms.pass=get("uPass");
    m_trainingUniforms.populationSize=get("uPopulationSize");
    m_trainingUniforms.sourceIndex=get("uSourceIndex");
    m_trainingUniforms.generation=get("uGeneration");
    m_trainingUniforms.seed=get("uSeed");
    m_trainingUniforms.referenceSize=get("uReferenceSize");
    m_trainingUniforms.cropOffset=get("uCropOffset");
    m_trainingUniforms.cropSize=get("uCropSize");
    m_trainingUniforms.sampleGrid=get("uSampleGrid");
    m_trainingUniforms.mutationScale=get("uMutationScale");
    m_trainingUniforms.referenceImage=get("uReferenceImage");
    m_trainingUniforms.referenceChannel=get("uReferenceChannel");
}

bool ClimateNoiseLabPanel::loadReferenceImage()
{
    if(!waitForTraining()) return false;
    int width=0,height=0,components=0;
    // Other engine loaders change stb's global flip flag. This lab always uses
    // file row zero as y=0 in both training and preview.
    stbi_set_flip_vertically_on_load(false);
    unsigned char* pixels=stbi_load(ReferencePath,&width,&height,&components,4);
    if(!pixels || width<=0 || height<=0)
    {
        stbi_image_free(pixels);
        const char* reason=stbi_failure_reason();
        m_owner.m_error=std::string("Impossible de charger ")+ReferencePath+": "+
            (reason?reason:"dimensions invalides");
        return false;
    }
    GLint maxSize=0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE,&maxSize);
    if(width>maxSize || height>maxSize)
    {
        stbi_image_free(pixels);
        m_owner.m_error="L'image de reference depasse GL_MAX_TEXTURE_SIZE.";
        return false;
    }
    GLuint texture=0;
    glCreateTextures(GL_TEXTURE_2D,1,&texture);
    glTextureStorage2D(texture,1,GL_RGBA8,width,height);
    // A renderer upload may have left an unpack PBO or strided pixel state.
    GLint unpackBuffer=0,alignment=0,rowLength=0,skipRows=0,skipPixels=0;
    glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING,&unpackBuffer);
    glGetIntegerv(GL_UNPACK_ALIGNMENT,&alignment);
    glGetIntegerv(GL_UNPACK_ROW_LENGTH,&rowLength);
    glGetIntegerv(GL_UNPACK_SKIP_ROWS,&skipRows);
    glGetIntegerv(GL_UNPACK_SKIP_PIXELS,&skipPixels);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER,0);
    glPixelStorei(GL_UNPACK_ALIGNMENT,1);
    glPixelStorei(GL_UNPACK_ROW_LENGTH,0);
    glPixelStorei(GL_UNPACK_SKIP_ROWS,0);
    glPixelStorei(GL_UNPACK_SKIP_PIXELS,0);
    glTextureSubImage2D(texture,0,0,0,width,height,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER,static_cast<GLuint>(unpackBuffer));
    glPixelStorei(GL_UNPACK_ALIGNMENT,alignment);
    glPixelStorei(GL_UNPACK_ROW_LENGTH,rowLength);
    glPixelStorei(GL_UNPACK_SKIP_ROWS,skipRows);
    glPixelStorei(GL_UNPACK_SKIP_PIXELS,skipPixels);
    glTextureParameteri(texture,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
    glTextureParameteri(texture,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glTextureParameteri(texture,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
    glTextureParameteri(texture,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    glTextureParameteri(texture,GL_TEXTURE_MAX_LEVEL,0);
    stbi_image_free(pixels);

    if(m_referenceTexture) glDeleteTextures(1,&m_referenceTexture);
    m_referenceTexture=texture;
    m_referenceWidth=width;
    m_referenceHeight=height;
    m_training=false;
    if(m_populationBuffer) allocateTrainingBuffers();
    const std::string loadErrorPrefix=std::string("Impossible de charger ")+ReferencePath+":";
    if(m_owner.m_error.compare(0,loadErrorPrefix.size(),loadErrorPrefix)==0 ||
       m_owner.m_error=="L'image de reference depasse GL_MAX_TEXTURE_SIZE.")
        m_owner.m_error.clear();
    m_dirty=true;
    return true;
}

void ClimateNoiseLabPanel::allocateTextures()
{
    const int resolution=std::clamp(m_owner.textureResolution,64,2048);
    m_owner.textureResolution=resolution;
    if(m_allocatedResolution==resolution && m_textures[0]) return;
    if(m_textures[0]) glDeleteTextures(static_cast<GLsizei>(m_textures.size()),m_textures.data());
    m_textures.fill(0);
    glCreateTextures(GL_TEXTURE_2D,static_cast<GLsizei>(m_textures.size()),m_textures.data());
    for(GLuint texture:m_textures)
    {
        glTextureStorage2D(texture,1,GL_RGBA8,resolution,resolution);
        glTextureParameteri(texture,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
        glTextureParameteri(texture,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        glTextureParameteri(texture,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
        glTextureParameteri(texture,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        glTextureParameteri(texture,GL_TEXTURE_MAX_LEVEL,0);
    }
    m_allocatedResolution=resolution;
}

void ClimateNoiseLabPanel::allocateTrainingBuffers()
{
    if(!waitForTraining()) return;
    const GLuint oldBuffers[]={m_populationBuffer,m_scoreBuffer,m_bestBuffer,m_stateBuffer};
    glDeleteBuffers(4,oldBuffers);
    glCreateBuffers(1,&m_populationBuffer);
    glCreateBuffers(1,&m_scoreBuffer);
    glCreateBuffers(1,&m_bestBuffer);
    glCreateBuffers(1,&m_stateBuffer);

    const GLsizeiptr populationBytes=static_cast<GLsizeiptr>(2ull*GeneticPopulation*
        NeuralParameterCount*sizeof(float));
    glNamedBufferData(m_populationBuffer,populationBytes,nullptr,GL_DYNAMIC_COPY);
    glNamedBufferData(m_scoreBuffer,static_cast<GLsizeiptr>((GeneticPopulation+2u)*sizeof(float)),
                      nullptr,GL_DYNAMIC_COPY);
    std::vector<float> neutral(NeuralParameterCount,0.0f);
    glNamedBufferData(m_bestBuffer,static_cast<GLsizeiptr>(neutral.size()*sizeof(float)),
                      neutral.data(),GL_DYNAMIC_COPY);
    TrainingState state{std::numeric_limits<float>::max(),0u,0u,0u};
    glNamedBufferData(m_stateBuffer,sizeof(state),&state,GL_DYNAMIC_COPY);

    m_training=false;
    m_modelLoaded=false;
    m_populationInitialized=false;
    m_populationSource=0;
    m_submittedGeneration=m_completedGeneration=0;
    m_bestScore=state.bestScore;
}

void ClimateNoiseLabPanel::resetTraining(bool preserveModel)
{
    m_training=false;
    if(!waitForTraining()) return;
    if(!m_trainingProgram || !m_populationBuffer) return;

    std::vector<float> preservedWeights;
    TrainingState preservedState{};
    const bool preserve=preserveModel && m_modelLoaded;
    if(preserve)
    {
        preservedWeights.resize(NeuralParameterCount);
        glGetNamedBufferSubData(m_bestBuffer,0,
            static_cast<GLsizeiptr>(preservedWeights.size()*sizeof(float)),preservedWeights.data());
        glGetNamedBufferSubData(m_stateBuffer,0,sizeof(preservedState),&preservedState);
    }

    TrainingState initial{std::numeric_limits<float>::max(),0u,0u,0u};
    glNamedBufferSubData(m_stateBuffer,0,sizeof(initial),&initial);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER,0,m_populationBuffer);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER,1,m_scoreBuffer);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER,2,m_bestBuffer);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER,3,m_stateBuffer);
    const ScopedProgram programGuard;
    glUseProgram(m_trainingProgram);
    glUniform1ui(m_trainingUniforms.pass,0u);
    glUniform1ui(m_trainingUniforms.populationSize,GeneticPopulation);
    glUniform1ui(m_trainingUniforms.sourceIndex,0u);
    glUniform1ui(m_trainingUniforms.generation,0u);
    glUniform1ui(m_trainingUniforms.seed,m_owner.seed);
    const GLuint values=GeneticPopulation*NeuralParameterCount;
    glDispatchCompute((values+63u)/64u,1u,1u);
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT|GL_BUFFER_UPDATE_BARRIER_BIT);

    if(preserve)
    {
        glNamedBufferSubData(m_bestBuffer,0,
            static_cast<GLsizeiptr>(preservedWeights.size()*sizeof(float)),preservedWeights.data());
        glNamedBufferSubData(m_populationBuffer,0,
            static_cast<GLsizeiptr>(preservedWeights.size()*sizeof(float)),preservedWeights.data());
        preservedState.sourceIndex=0u;
        glNamedBufferSubData(m_stateBuffer,0,sizeof(preservedState),&preservedState);
        m_bestScore=preservedState.bestScore;
        m_submittedGeneration=m_completedGeneration=preservedState.generation;
    }
    else
    {
        m_bestScore=initial.bestScore;
        m_submittedGeneration=m_completedGeneration=0u;
        m_modelLoaded=false;
    }
    m_populationSource=0u;
    m_populationInitialized=true;
    m_dirty=true;
}

void ClimateNoiseLabPanel::submitTrainingBatch()
{
    if(!m_trainingProgram || !m_referenceTexture || m_referenceWidth<=0 ||
       m_referenceHeight<=0 || !m_populationInitialized || m_trainingFence)
        return;

    const int crop=std::max(1,std::min<int>(GeneticCropSize,
        std::min(m_referenceWidth,m_referenceHeight)));
    const int sampleGrid=std::clamp(m_owner.geneticSampleGrid,8,64);
    const int generations=std::clamp(m_owner.geneticGenerationsPerBatch,1,16);

    const ScopedProgram programGuard;
    glUseProgram(m_trainingProgram);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER,0,m_populationBuffer);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER,1,m_scoreBuffer);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER,2,m_bestBuffer);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER,3,m_stateBuffer);
    glBindTextureUnit(0,m_referenceTexture);
    glUniform1i(m_trainingUniforms.referenceImage,0);
    glUniform1ui(m_trainingUniforms.populationSize,GeneticPopulation);
    glUniform1ui(m_trainingUniforms.seed,m_owner.seed);
    glUniform2i(m_trainingUniforms.referenceSize,m_referenceWidth,m_referenceHeight);
    glUniform1i(m_trainingUniforms.cropSize,crop);
    glUniform1ui(m_trainingUniforms.sampleGrid,static_cast<GLuint>(sampleGrid));
    glUniform1i(m_trainingUniforms.referenceChannel,
        std::clamp(m_owner.geneticReferenceChannel,0,3));

    for(int iteration=0;iteration<generations;++iteration)
    {
        const std::uint32_t generation=m_submittedGeneration;
        const std::uint32_t randomX=hash32(m_owner.seed ^ generation*0x9e3779b9u);
        const std::uint32_t randomY=hash32(randomX ^ 0x85ebca6bu);
        const int maxX=std::max(0,m_referenceWidth-crop);
        const int maxY=std::max(0,m_referenceHeight-crop);
        const int offsetX=maxX?static_cast<int>(randomX%static_cast<std::uint32_t>(maxX+1)):0;
        const int offsetY=maxY?static_cast<int>(randomY%static_cast<std::uint32_t>(maxY+1)):0;
        const float decay=1.0f/std::sqrt(1.0f+static_cast<float>(generation)*0.002f);
        const float mutation=std::max(m_owner.geneticMinimumMutation,
                                      m_owner.geneticMutationScale*decay);

        glUniform1ui(m_trainingUniforms.sourceIndex,m_populationSource);
        glUniform1ui(m_trainingUniforms.generation,generation);
        glUniform2i(m_trainingUniforms.cropOffset,offsetX,offsetY);
        glUniform1f(m_trainingUniforms.mutationScale,mutation);

        glUniform1ui(m_trainingUniforms.pass,1u);
        glDispatchCompute(GeneticPopulation,1u,1u);
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

        // Compare the crop winner and incumbent on the same fixed, full-image
        // validation set. Scores from different random crops are not comparable.
        glUniform1ui(m_trainingUniforms.pass,3u);
        glDispatchCompute(2u,1u,1u);
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

        glUniform1ui(m_trainingUniforms.pass,2u);
        glDispatchCompute(1u,1u,1u);
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

        m_populationSource=1u-m_populationSource;
        ++m_submittedGeneration;
    }
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT|GL_BUFFER_UPDATE_BARRIER_BIT);
    m_trainingFence=glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE,0);
    glFlush();
    if(!m_trainingFence)
    {
        m_training=false;
        m_owner.m_error="Impossible de creer la fence d'entrainement GPU.";
    }
}

void ClimateNoiseLabPanel::pollTraining()
{
    if(!m_trainingFence) return;
    const GLenum state=glClientWaitSync(m_trainingFence,0,0);
    if(state==GL_WAIT_FAILED)
    {
        m_training=false;
        m_owner.m_error="Echec de la synchronisation de l'entrainement GPU.";
        return;
    }
    if(state!=GL_ALREADY_SIGNALED && state!=GL_CONDITION_SATISFIED) return;
    glDeleteSync(m_trainingFence);
    m_trainingFence=nullptr;

    TrainingState training{};
    glGetNamedBufferSubData(m_stateBuffer,0,sizeof(training),&training);
    m_bestScore=training.bestScore;
    m_completedGeneration=training.generation;
    m_populationSource=training.sourceIndex;
    if(finiteFloat(m_bestScore) && m_bestScore<1.0e20f) m_modelLoaded=true;
    m_dirty=true;
}

bool ClimateNoiseLabPanel::saveModel()
{
    if(!waitForTraining()) return false;
    if(!m_bestBuffer || !finiteFloat(m_bestScore) || m_bestScore<0.0f || m_bestScore>=1.0e20f)
    {
        m_owner.m_error="Aucun pattern valide a sauvegarder.";
        return false;
    }
    std::vector<float> weights(NeuralParameterCount);
    glGetNamedBufferSubData(m_bestBuffer,0,
        static_cast<GLsizeiptr>(weights.size()*sizeof(float)),weights.data());
    if(!validWeights(weights))
    {
        m_owner.m_error="Le pattern contient des poids invalides; sauvegarde annulee.";
        return false;
    }
    ModelHeader header;
    header.seed=m_owner.seed;
    header.bestScore=m_bestScore;
    header.generation=m_completedGeneration;
    header.referenceChannel=static_cast<std::uint32_t>(
        std::clamp(m_owner.geneticReferenceChannel,0,3));
    std::ofstream output(ModelPath,std::ios::binary|std::ios::trunc);
    if(!output)
    {
        m_owner.m_error=std::string("Impossible d'ecrire ")+ModelPath;
        return false;
    }
    output.write(reinterpret_cast<const char*>(&header),sizeof(header));
    output.write(reinterpret_cast<const char*>(weights.data()),
                 static_cast<std::streamsize>(weights.size()*sizeof(float)));
    if(!output)
    {
        m_owner.m_error="Echec pendant la sauvegarde du Neural Pattern.";
        return false;
    }
    m_owner.m_error.clear();
    m_modelLoaded=true;
    return true;
}

bool ClimateNoiseLabPanel::loadModel(bool reportMissing)
{
    if(!waitForTraining()) return false;
    std::ifstream input(ModelPath,std::ios::binary);
    if(!input)
    {
        if(reportMissing) m_owner.m_error=std::string("Modele absent : ")+ModelPath;
        return false;
    }
    ModelHeader header;
    input.read(reinterpret_cast<char*>(&header),sizeof(header));
    if(!input || header.magic!=ModelMagic || header.version!=ModelVersion ||
       header.parameterCount!=NeuralParameterCount || header.referenceChannel>3u ||
       !finiteFloat(header.bestScore) || header.bestScore<0.0f || header.bestScore>=1.0e20f)
    {
        m_owner.m_error="Le modele Neural Pattern est invalide ou incompatible.";
        return false;
    }
    std::vector<float> weights(NeuralParameterCount);
    input.read(reinterpret_cast<char*>(weights.data()),
               static_cast<std::streamsize>(weights.size()*sizeof(float)));
    if(!input)
    {
        m_owner.m_error="Le modele Neural Pattern est tronque.";
        return false;
    }
    if(!validWeights(weights) || input.peek()!=std::char_traits<char>::eof())
    {
        m_owner.m_error="Le modele Neural Pattern contient des poids invalides ou des donnees inattendues.";
        return false;
    }
    if(!m_bestBuffer || !m_stateBuffer) return false;
    glNamedBufferSubData(m_bestBuffer,0,
        static_cast<GLsizeiptr>(weights.size()*sizeof(float)),weights.data());
    // Keep compatible weights, but never import an error measured on another
    // image, shader revision or (in older files) a lucky random crop.
    TrainingState state{std::numeric_limits<float>::max(),header.generation,header.generation,0u};
    glNamedBufferSubData(m_stateBuffer,0,sizeof(state),&state);
    m_owner.seed=header.seed;
    m_owner.geneticReferenceChannel=static_cast<std::int32_t>(header.referenceChannel);
    m_bestScore=state.bestScore;
    m_submittedGeneration=m_completedGeneration=header.generation;
    m_populationSource=0u;
    m_populationInitialized=false;
    m_modelLoaded=true;
    m_training=false;
    m_owner.m_error.clear();
    m_dirty=true;
    return true;
}

void ClimateNoiseLabPanel::generate()
{
    if(!m_program && !reloadShaders()) return;
    allocateTextures();
    if(!m_textures[0] || !m_bestBuffer) return;
    GLint previousProgram=0; glGetIntegerv(GL_CURRENT_PROGRAM,&previousProgram);
    while(glGetError()!=GL_NO_ERROR) {}
    glUseProgram(m_program);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER,5,m_bestBuffer);
    glUniform1ui(m_uniforms.resolution,static_cast<GLuint>(m_allocatedResolution));
    glUniform1ui(m_uniforms.seed,m_owner.seed);
    glUniform2f(m_uniforms.worldOrigin,m_owner.worldOriginX,m_owner.worldOriginZ);
    glUniform1f(m_uniforms.worldExtent,std::max(m_owner.worldExtent,1.0f));
    glUniform1f(m_uniforms.climateFrequency,std::max(m_owner.climateFrequency,0.0f));
    glUniform1f(m_uniforms.neuralWorldPeriod,std::max(m_owner.neuralWorldPeriod,1.0f));
    glUniform1f(m_uniforms.humidityFrequencyMultiplier,std::max(m_owner.humidityFrequencyMultiplier,0.0f));
    glUniform1f(m_uniforms.temperatureFrequencyMultiplier,std::max(m_owner.temperatureFrequencyMultiplier,0.0f));
    glUniform1i(m_uniforms.humidityNoiseType,std::clamp(m_owner.humidityNoise,0,NeuralPatternNoise));
    glUniform1i(m_uniforms.temperatureNoiseType,std::clamp(m_owner.temperatureNoise,0,NeuralPatternNoise));
    glUniform1i(m_uniforms.octaves,std::clamp(m_owner.octaves,0,8));
    glUniform1f(m_uniforms.lacunarity,std::clamp(m_owner.lacunarity,1.0f,8.0f));
    glUniform1f(m_uniforms.gain,std::clamp(m_owner.gain,0.0f,1.0f));
    glUniform1f(m_uniforms.remapMinimum,m_owner.remapMinimum);
    glUniform1f(m_uniforms.remapMaximum,m_owner.remapMaximum);
    glUniform2f(m_uniforms.postContrast,m_owner.postContrastHumidity,m_owner.postContrastTemperature);
    glUniform2f(m_uniforms.postGamma,m_owner.postGammaHumidity,m_owner.postGammaTemperature);
    glUniform1i(m_uniforms.attractionMode,std::clamp(m_owner.attractionMode,0,2));
    glUniform2f(m_uniforms.attractionTarget,m_owner.attractionTargetHumidity,m_owner.attractionTargetTemperature);
    glUniform1f(m_uniforms.attractionMaxShift,std::clamp(m_owner.attractionMaxShift,0.0f,1.0f));
    glUniform1i(m_uniforms.matrixBlend,m_owner.matrixBlend?1:0);
    std::array<GLint,ProceduralTerrain::BiomeMatrix2D.size()> matrix{};
    for(std::size_t i=0;i<matrix.size();++i) matrix[i]=static_cast<GLint>(ProceduralTerrain::BiomeMatrix2D[i]);
    glUniform1iv(m_uniforms.biomeMatrix,static_cast<GLsizei>(matrix.size()),matrix.data());
    for(std::size_t i=0;i<m_textures.size();++i)
        glBindImageTexture(static_cast<GLuint>(i),m_textures[i],0,GL_FALSE,0,GL_WRITE_ONLY,GL_RGBA8);
    const GLuint groups=static_cast<GLuint>((m_allocatedResolution+7)/8);
    glDispatchCompute(groups,groups,1);
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT|GL_TEXTURE_FETCH_BARRIER_BIT);
    for(std::size_t i=0;i<m_textures.size();++i)
        glBindImageTexture(static_cast<GLuint>(i),0,0,GL_FALSE,0,GL_READ_ONLY,GL_RGBA8);
    glUseProgram(static_cast<GLuint>(previousProgram));
    const GLenum error=glGetError();
    if(error!=GL_NO_ERROR)
    {
        m_owner.m_error="Erreur OpenGL pendant la generation du Climate Noise Lab: "+
            std::to_string(static_cast<unsigned>(error));
        return;
    }
    m_dirty=false;
}

void ClimateNoiseLabPanel::buildDockLayout(ImGuiID dockspace,ImVec2 size)
{
    ImGui::DockBuilderRemoveNode(dockspace);
    ImGui::DockBuilderAddNode(dockspace,ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockspace,size);
    ImGuiID remaining=dockspace;
    const ImGuiID settings=ImGui::DockBuilderSplitNode(remaining,ImGuiDir_Left,.22f,nullptr,&remaining);
    ImGuiID top=ImGui::DockBuilderSplitNode(remaining,ImGuiDir_Up,.50f,nullptr,&remaining);
    const ImGuiID reference=ImGui::DockBuilderSplitNode(top,ImGuiDir_Left,.333f,nullptr,&top);
    const ImGuiID temperature=ImGui::DockBuilderSplitNode(top,ImGuiDir_Left,.50f,nullptr,&top);
    const ImGuiID raw=ImGui::DockBuilderSplitNode(remaining,ImGuiDir_Left,.333f,nullptr,&remaining);
    const ImGuiID attracted=ImGui::DockBuilderSplitNode(remaining,ImGuiDir_Left,.50f,nullptr,&remaining);
    ImGui::DockBuilderDockWindow("Reglages##ClimateNoiseLab",settings);
    ImGui::DockBuilderDockWindow("Reference scalaire##ClimateNoiseLab",reference);
    ImGui::DockBuilderDockWindow("Temperature##ClimateNoiseLab",temperature);
    ImGui::DockBuilderDockWindow("Humidite##ClimateNoiseLab",top);
    ImGui::DockBuilderDockWindow("Climat brut (R=T, G=H)##ClimateNoiseLab",raw);
    ImGui::DockBuilderDockWindow("Apres attraction (R=T, G=H)##ClimateNoiseLab",attracted);
    ImGui::DockBuilderDockWindow("Resultat matrice##ClimateNoiseLab",remaining);
    ImGui::DockBuilderFinish(dockspace);
}

void ClimateNoiseLabPanel::drawSettings()
{
    if(!ImGui::Begin("Reglages##ClimateNoiseLab")) { ImGui::End(); return; }
    const bool seedChanged=ImGui::InputScalar("Seed",ImGuiDataType_U32,&m_owner.seed);
    bool changed=seedChanged;
    if(seedChanged)
    {
        m_training=false;
        m_modelLoaded=false;
        allocateTrainingBuffers();
    }

    int resolutionIndex=0,best=std::numeric_limits<int>::max();
    for(int i=0;i<static_cast<int>(Resolutions.size());++i)
    { const int d=std::abs(Resolutions[i]-m_owner.textureResolution); if(d<best){best=d;resolutionIndex=i;} }
    const char* resolutionNames[]={"256","512","1024","1536","2048"};
    if(ImGui::Combo("Resolution",&resolutionIndex,resolutionNames,5))
    { m_owner.textureResolution=Resolutions[resolutionIndex]; changed=true; }
    changed|=ImGui::DragFloat("Etendue monde (m)",&m_owner.worldExtent,64,64,1000000,"%.0f");
    changed|=ImGui::DragFloat("Origine X",&m_owner.worldOriginX,32,-10000000,10000000,"%.1f");
    changed|=ImGui::DragFloat("Origine Z",&m_owner.worldOriginZ,32,-10000000,10000000,"%.1f");

    ImGui::SeparatorText("Sources de bruit");
    changed|=ImGui::SliderFloat("Climate frequency",&m_owner.climateFrequency,.000001f,.02f,"%.7f",ImGuiSliderFlags_Logarithmic);
    changed|=ImGui::Combo("Humidite",&m_owner.humidityNoise,NoiseNames.data(),static_cast<int>(NoiseNames.size()));
    changed|=ImGui::Combo("Temperature",&m_owner.temperatureNoise,NoiseNames.data(),static_cast<int>(NoiseNames.size()));
    changed|=ImGui::SliderFloat("Multiplicateur H",&m_owner.humidityFrequencyMultiplier,.05f,8,"%.3f",ImGuiSliderFlags_Logarithmic);
    changed|=ImGui::SliderFloat("Multiplicateur T",&m_owner.temperatureFrequencyMultiplier,.05f,8,"%.3f",ImGuiSliderFlags_Logarithmic);
    changed|=ImGui::SliderInt("Octaves fBm",&m_owner.octaves,0,8);
    changed|=ImGui::SliderFloat("Lacunarite",&m_owner.lacunarity,1,8,"%.2f");
    changed|=ImGui::SliderFloat("Gain",&m_owner.gain,0,1,"%.3f");
    changed|=ImGui::DragFloatRange2("Smoothstep",&m_owner.remapMinimum,&m_owner.remapMaximum,.005f,-1,1,"Min %.3f","Max %.3f");
    if(m_owner.humidityNoise==NeuralPatternNoise || m_owner.temperatureNoise==NeuralPatternNoise)
        changed|=ImGui::DragFloat("Periode Neural Pattern (m)",&m_owner.neuralWorldPeriod,64,64,1000000,"%.0f");

    if(ImGui::CollapsingHeader("Entrainement Neural Pattern GPU",ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::Text("Reference: %d x %d",m_referenceWidth,m_referenceHeight);
        ImGui::TextUnformatted("Reseau fixe: 12 -> 8 -> 8 -> 8 -> 8 -> 1");
        ImGui::TextUnformatted("32 tanh / 329 parametres / population 128");
        int previousChannel=m_owner.geneticReferenceChannel;
        if(ImGui::Combo("Canal de reference",&m_owner.geneticReferenceChannel,
                        ReferenceChannelNames.data(),static_cast<int>(ReferenceChannelNames.size())))
        {
            m_owner.geneticReferenceChannel=std::clamp(m_owner.geneticReferenceChannel,0,3);
            if(previousChannel!=m_owner.geneticReferenceChannel)
            {
                m_training=false;
                m_modelLoaded=false;
                allocateTrainingBuffers();
                changed=true;
            }
        }
        ImGui::TextWrapped("L'image apprend un seul pattern scalaire. Ce pattern peut ensuite etre choisi independamment pour l'humidite ou la temperature.");
        changed|=ImGui::SliderInt("Grille d'echantillons",&m_owner.geneticSampleGrid,8,64);
        changed|=ImGui::SliderInt("Generations par lot",&m_owner.geneticGenerationsPerBatch,1,16);
        changed|=ImGui::SliderFloat("Mutation initiale",&m_owner.geneticMutationScale,.005f,.6f,"%.4f",ImGuiSliderFlags_Logarithmic);
        changed|=ImGui::SliderFloat("Mutation minimale",&m_owner.geneticMinimumMutation,.001f,.2f,"%.4f",ImGuiSliderFlags_Logarithmic);
        ImGui::Text("Generation terminee: %u",m_completedGeneration);
        if(finiteFloat(m_bestScore)&&m_bestScore<1.0e20f)
            ImGui::Text("Erreur de validation globale: %.8f",m_bestScore);
        else ImGui::TextUnformatted("Erreur de validation globale: pas encore mesuree");
        if(m_trainingFence) ImGui::TextUnformatted("Lot GPU en cours...");

        if(ImGui::Button(m_training?"Pause":"Lancer / reprendre",ImVec2(-1,0)))
        {
            if(m_training) m_training=false;
            else
            {
                if(!m_referenceTexture || m_referenceWidth<=0 || m_referenceHeight<=0)
                    m_owner.m_error="Chargez une image de reference valide avant l'entrainement.";
                else
                {
                    if(!m_populationInitialized) resetTraining(true);
                    m_training=m_populationInitialized;
                }
            }
        }
        if(ImGui::Button("Reinitialiser depuis zero",ImVec2(-1,0)))
        {
            resetTraining(false);
        }
        if(ImGui::Button("Sauvegarder le meilleur pattern",ImVec2(-1,0))) saveModel();
        if(ImGui::Button("Charger le pattern sauvegarde",ImVec2(-1,0))) loadModel(true);
        if(ImGui::Button("Recharger l'image de reference",ImVec2(-1,0)))
            loadReferenceImage();
    }

    ImGui::SeparatorText("Transformation de la sortie");
    changed|=ImGui::SliderFloat("Contraste humidite",&m_owner.postContrastHumidity,0,4,"%.3f");
    changed|=ImGui::SliderFloat("Contraste temperature",&m_owner.postContrastTemperature,0,4,"%.3f");
    changed|=ImGui::SliderFloat("Gamma humidite",&m_owner.postGammaHumidity,.1f,4,"%.3f",ImGuiSliderFlags_Logarithmic);
    changed|=ImGui::SliderFloat("Gamma temperature",&m_owner.postGammaTemperature,.1f,4,"%.3f",ImGuiSliderFlags_Logarithmic);

    ImGui::SeparatorText("Attraction");
    changed|=ImGui::Combo("Mode",&m_owner.attractionMode,AttractionNames.data(),static_cast<int>(AttractionNames.size()));
    if(m_owner.attractionMode==1)
    {
        changed|=ImGui::SliderFloat("Cible humidite",&m_owner.attractionTargetHumidity,0,1,"%.3f");
        changed|=ImGui::SliderFloat("Cible temperature",&m_owner.attractionTargetTemperature,0,1,"%.3f");
        ImGui::TextWrapped("Six reperes : 0, .2, .4, .6, .8 et 1.");
    }
    changed|=ImGui::SliderFloat("Deplacement maximal",&m_owner.attractionMaxShift,0,.5f,"%.4f");
    changed|=ImGui::Checkbox("Transitions matrice lissees",&m_owner.matrixBlend);
    changed|=ImGui::Checkbox("Mise a jour automatique",&m_owner.autoUpdate);
    if(ImGui::Button("Generer",ImVec2(-1,0))) m_generateRequested=true;
    if(ImGui::Button("Recharger les shaders",ImVec2(-1,0))) reloadShaders();

    if(ImGui::Button("Preset climat actuel",ImVec2(-1,0)))
    {
        m_owner.humidityNoise=m_owner.temperatureNoise=0; m_owner.climateFrequency=.0005f;
        m_owner.humidityFrequencyMultiplier=m_owner.temperatureFrequencyMultiplier=1;
        m_owner.remapMinimum=-.32f; m_owner.remapMaximum=.32f;
        m_owner.postContrastHumidity=m_owner.postContrastTemperature=1;
        m_owner.postGammaHumidity=m_owner.postGammaTemperature=1;
        m_owner.attractionMode=0; m_owner.matrixBlend=true; changed=true;
    }
    if(ImGui::Button("Preset Simplex",ImVec2(-1,0)))
    { m_owner.humidityNoise=m_owner.temperatureNoise=1; changed=true; }
    if(ImGui::Button("Preset Patchwork regions",ImVec2(-1,0)))
    {
        m_owner.humidityNoise=m_owner.temperatureNoise=10;
        m_owner.climateFrequency=.0005f;
        m_owner.humidityFrequencyMultiplier=m_owner.temperatureFrequencyMultiplier=1;
        m_owner.remapMinimum=-1.0f; m_owner.remapMaximum=1.0f;
        m_owner.attractionMode=0; m_owner.matrixBlend=true; changed=true;
    }
    if(ImGui::Button("Neural Pattern -> humidite",ImVec2(-1,0)))
    {
        m_owner.humidityNoise=NeuralPatternNoise;
        m_owner.temperatureNoise=1;
        m_owner.neuralWorldPeriod=32768.0f;
        m_owner.worldExtent=m_owner.neuralWorldPeriod;
        m_owner.worldOriginX=m_owner.worldOriginZ=0.0f;
        m_owner.humidityFrequencyMultiplier=m_owner.temperatureFrequencyMultiplier=1.0f;
        m_owner.postContrastHumidity=m_owner.postContrastTemperature=1;
        m_owner.postGammaHumidity=m_owner.postGammaTemperature=1;
        m_owner.attractionMode=0; m_owner.matrixBlend=true; changed=true;
    }
    if(ImGui::Button("Neural Pattern -> temperature",ImVec2(-1,0)))
    {
        m_owner.humidityNoise=1;
        m_owner.temperatureNoise=NeuralPatternNoise;
        m_owner.neuralWorldPeriod=32768.0f;
        m_owner.worldExtent=m_owner.neuralWorldPeriod;
        m_owner.worldOriginX=m_owner.worldOriginZ=0.0f;
        m_owner.humidityFrequencyMultiplier=m_owner.temperatureFrequencyMultiplier=1.0f;
        m_owner.postContrastHumidity=m_owner.postContrastTemperature=1;
        m_owner.postGammaHumidity=m_owner.postGammaTemperature=1;
        m_owner.attractionMode=0; m_owner.matrixBlend=true; changed=true;
    }
    if(ImGui::Button("Reinitialiser le docking",ImVec2(-1,0))) m_resetDock=true;
    if(!m_owner.m_error.empty())
    { ImGui::SeparatorText("Erreur"); ImGui::TextWrapped("%s",m_owner.m_error.c_str()); }
    if(changed) m_dirty=true;
    if(!m_owner.autoUpdate&&m_dirty) ImGui::TextUnformatted("Parametres modifies : cliquez sur Generer.");
    ImGui::End();
}

void ClimateNoiseLabPanel::drawTextureWindow(const char* name,GLuint texture,const char* description)
{
    if(!ImGui::Begin(name)) { ImGui::End(); return; }
    ImGui::TextWrapped("%s",description);
    const ImVec2 available=ImGui::GetContentRegionAvail();
    const float side=std::max(1.0f,std::min(available.x,available.y));
    const float offset=std::max(0.0f,.5f*(available.x-side));
    if(offset>0) ImGui::SetCursorPosX(ImGui::GetCursorPosX()+offset);
    if(texture) ImGui::Image(textureId(texture),ImVec2(side,side),ImVec2(0,0),ImVec2(1,1));
    else ImGui::TextUnformatted("Texture indisponible.");
    ImGui::End();
}
