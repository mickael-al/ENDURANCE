#include "ClimateNoiseLabPanel.hpp"
#include "ClimateNoiseLab.hpp"
#include "ProceduralTerrainBiomes.hpp"
#include "ShaderCompilation.hpp"
#include "imgui-cmake/Header/imgui.h"
#include "imgui-cmake/Header/imgui_internal.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>

namespace
{
constexpr std::array<const char*,2> SourceNames{{
    "Deux bruits independants", "Neural Climate (couple H/T)"
}};
constexpr std::array<const char*,11> NoiseNames{{
    "Perlin","Simplex","Value quintique","Value cubique","Blanc par cellule",
    "Voronoi","Valeur de cellule","fBm Perlin","fBm Simplex","fBm Value",
    "Patchwork de regions"
}};
constexpr std::array<const char*,3> AttractionNames{{
    "Aucune","attractNearFifths01 (cible fixe)","Niveau matrice le plus proche"
}};
constexpr std::array<int,5> Resolutions{{256,512,1024,1536,2048}};
ImTextureID textureId(GLuint texture)
{ return reinterpret_cast<ImTextureID>(static_cast<std::intptr_t>(texture)); }
}

ClimateNoiseLabPanel::ClimateNoiseLabPanel(ClimateNoiseLabComponent& owner):m_owner(owner) {}
ClimateNoiseLabPanel::~ClimateNoiseLabPanel() { release(); }
void ClimateNoiseLabPanel::init(GraphicsDataMisc*) { reloadShader(); m_dirty=true; }

void ClimateNoiseLabPanel::render(GraphicsDataMisc*)
{
    if(!m_owner.showWindow) return;
    ImGui::SetNextWindowSize(ImVec2(1500,900),ImGuiCond_FirstUseEver);
    if(!ImGui::Begin("Climate Noise Lab",nullptr,ImGuiWindowFlags_MenuBar|ImGuiWindowFlags_NoCollapse))
    { ImGui::End(); return; }
    if(ImGui::BeginMenuBar())
    {
        if(ImGui::MenuItem("Generer")) m_generateRequested=true;
        if(ImGui::MenuItem("Recharger le shader")) reloadShader();
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
    drawTextureWindow("Temperature##ClimateNoiseLab",m_textures[0],"Valeur [0,1] en niveaux de gris.");
    drawTextureWindow("Humidite##ClimateNoiseLab",m_textures[1],"Valeur [0,1] en niveaux de gris.");
    drawTextureWindow("Climat brut (R=T, G=H)##ClimateNoiseLab",m_textures[2],"Source puis contraste/gamma, avant attraction.");
    drawTextureWindow("Apres attraction (R=T, G=H)##ClimateNoiseLab",m_textures[3],"Meme codage apres le traitement selectionne.");
    drawTextureWindow("Resultat matrice##ClimateNoiseLab",m_textures[4],"Palette des 25 biomes depuis le climat traite.");
}

void ClimateNoiseLabPanel::release()
{
    if(m_program) glDeleteProgram(m_program);
    if(m_textures[0]) glDeleteTextures(static_cast<GLsizei>(m_textures.size()),m_textures.data());
    m_program=0; m_textures.fill(0); m_allocatedResolution=0;
}

bool ClimateNoiseLabPanel::reloadShader()
{
    std::string error;
    Ge::ShaderCacheOptions cache; cache.enabled=true;
    const GLuint next=Ge::buildShaderProgram({
        {GL_COMPUTE_SHADER,"../Asset/Shader/ClimateNoiseLab/climate_noise_lab.C.glsl",{}}
    },error,"Climate Noise Lab",cache);
    if(!next)
    {
        m_owner.m_error=error.empty()?"Impossible de compiler le shader Climate Noise Lab.":error;
        return false;
    }
    if(m_program) glDeleteProgram(m_program);
    m_program=next; discoverUniforms(); m_owner.m_error.clear(); m_dirty=true;
    return true;
}

void ClimateNoiseLabPanel::discoverUniforms()
{
    const auto get=[&](const char* name){return glGetUniformLocation(m_program,name);};
    m_uniforms.resolution=get("uResolution"); m_uniforms.seed=get("uSeed");
    m_uniforms.worldOrigin=get("uWorldOrigin"); m_uniforms.worldExtent=get("uWorldExtent");
    m_uniforms.climateSource=get("uClimateSource");
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

void ClimateNoiseLabPanel::generate()
{
    if(!m_program && !reloadShader()) return;
    allocateTextures();
    if(!m_textures[0]) return;
    GLint previousProgram=0; glGetIntegerv(GL_CURRENT_PROGRAM,&previousProgram);
    while(glGetError()!=GL_NO_ERROR) {}
    glUseProgram(m_program);
    glUniform1ui(m_uniforms.resolution,static_cast<GLuint>(m_allocatedResolution));
    glUniform1ui(m_uniforms.seed,m_owner.seed);
    glUniform2f(m_uniforms.worldOrigin,m_owner.worldOriginX,m_owner.worldOriginZ);
    glUniform1f(m_uniforms.worldExtent,std::max(m_owner.worldExtent,1.0f));
    glUniform1i(m_uniforms.climateSource,std::clamp(m_owner.climateSource,0,1));
    glUniform1f(m_uniforms.climateFrequency,std::max(m_owner.climateFrequency,0.0f));
    glUniform1f(m_uniforms.neuralWorldPeriod,std::max(m_owner.neuralWorldPeriod,1.0f));
    glUniform1f(m_uniforms.humidityFrequencyMultiplier,std::max(m_owner.humidityFrequencyMultiplier,0.0f));
    glUniform1f(m_uniforms.temperatureFrequencyMultiplier,std::max(m_owner.temperatureFrequencyMultiplier,0.0f));
    glUniform1i(m_uniforms.humidityNoiseType,std::clamp(m_owner.humidityNoise,0,10));
    glUniform1i(m_uniforms.temperatureNoiseType,std::clamp(m_owner.temperatureNoise,0,10));
    glUniform1i(m_uniforms.octaves,std::clamp(m_owner.octaves,0,8));
    glUniform1f(m_uniforms.lacunarity,std::clamp(m_owner.lacunarity,1.0f,8.0f));
    glUniform1f(m_uniforms.gain,std::clamp(m_owner.gain,0.0f,1.0f));
    glUniform1f(m_uniforms.remapMinimum,m_owner.remapMinimum);
    glUniform1f(m_uniforms.remapMaximum,m_owner.remapMaximum);
    glUniform2f(m_uniforms.postContrast,
        m_owner.postContrastHumidity,m_owner.postContrastTemperature);
    glUniform2f(m_uniforms.postGamma,
        m_owner.postGammaHumidity,m_owner.postGammaTemperature);
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
    m_owner.m_error.clear(); m_dirty=false;
}

void ClimateNoiseLabPanel::buildDockLayout(ImGuiID dockspace,ImVec2 size)
{
    ImGui::DockBuilderRemoveNode(dockspace);
    ImGui::DockBuilderAddNode(dockspace,ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockspace,size);
    ImGuiID remaining=dockspace;
    const ImGuiID settings=ImGui::DockBuilderSplitNode(remaining,ImGuiDir_Left,.22f,nullptr,&remaining);
    ImGuiID top=ImGui::DockBuilderSplitNode(remaining,ImGuiDir_Up,.50f,nullptr,&remaining);
    const ImGuiID temperature=ImGui::DockBuilderSplitNode(top,ImGuiDir_Left,.333f,nullptr,&top);
    const ImGuiID humidity=ImGui::DockBuilderSplitNode(top,ImGuiDir_Left,.50f,nullptr,&top);
    const ImGuiID attracted=ImGui::DockBuilderSplitNode(remaining,ImGuiDir_Left,.50f,nullptr,&remaining);
    ImGui::DockBuilderDockWindow("Reglages##ClimateNoiseLab",settings);
    ImGui::DockBuilderDockWindow("Temperature##ClimateNoiseLab",temperature);
    ImGui::DockBuilderDockWindow("Humidite##ClimateNoiseLab",humidity);
    ImGui::DockBuilderDockWindow("Climat brut (R=T, G=H)##ClimateNoiseLab",top);
    ImGui::DockBuilderDockWindow("Apres attraction (R=T, G=H)##ClimateNoiseLab",attracted);
    ImGui::DockBuilderDockWindow("Resultat matrice##ClimateNoiseLab",remaining);
    ImGui::DockBuilderFinish(dockspace);
}

void ClimateNoiseLabPanel::drawSettings()
{
    if(!ImGui::Begin("Reglages##ClimateNoiseLab")) { ImGui::End(); return; }
    bool changed=ImGui::InputScalar("Seed",ImGuiDataType_U32,&m_owner.seed);
    int resolutionIndex=0,best=std::numeric_limits<int>::max();
    for(int i=0;i<static_cast<int>(Resolutions.size());++i)
    { const int d=std::abs(Resolutions[i]-m_owner.textureResolution); if(d<best){best=d;resolutionIndex=i;} }
    const char* resolutionNames[]={"256","512","1024","1536","2048"};
    if(ImGui::Combo("Resolution",&resolutionIndex,resolutionNames,5))
    { m_owner.textureResolution=Resolutions[resolutionIndex]; changed=true; }
    changed|=ImGui::DragFloat("Etendue monde (m)",&m_owner.worldExtent,64,64,1000000,"%.0f");
    changed|=ImGui::DragFloat("Origine X",&m_owner.worldOriginX,32,-10000000,10000000,"%.1f");
    changed|=ImGui::DragFloat("Origine Z",&m_owner.worldOriginZ,32,-10000000,10000000,"%.1f");

    ImGui::SeparatorText("Source climatique");
    changed|=ImGui::Combo("Source",&m_owner.climateSource,SourceNames.data(),static_cast<int>(SourceNames.size()));
    if(m_owner.climateSource==0)
    {
        changed|=ImGui::SliderFloat("Climate frequency",&m_owner.climateFrequency,.000001f,.02f,"%.7f",ImGuiSliderFlags_Logarithmic);
        changed|=ImGui::Combo("Humidite",&m_owner.humidityNoise,NoiseNames.data(),static_cast<int>(NoiseNames.size()));
        changed|=ImGui::Combo("Temperature",&m_owner.temperatureNoise,NoiseNames.data(),static_cast<int>(NoiseNames.size()));
        changed|=ImGui::SliderFloat("Multiplicateur H",&m_owner.humidityFrequencyMultiplier,.05f,8,"%.3f",ImGuiSliderFlags_Logarithmic);
        changed|=ImGui::SliderFloat("Multiplicateur T",&m_owner.temperatureFrequencyMultiplier,.05f,8,"%.3f",ImGuiSliderFlags_Logarithmic);
        changed|=ImGui::SliderInt("Octaves fBm",&m_owner.octaves,0,8);
        changed|=ImGui::SliderFloat("Lacunarite",&m_owner.lacunarity,1,8,"%.2f");
        changed|=ImGui::SliderFloat("Gain",&m_owner.gain,0,1,"%.3f");
        changed|=ImGui::DragFloatRange2("Smoothstep",&m_owner.remapMinimum,&m_owner.remapMaximum,.005f,-1,1,"Min %.3f","Max %.3f");
    }
    else
    {
        changed|=ImGui::DragFloat("Periode image neuronale (m)",&m_owner.neuralWorldPeriod,64,64,1000000,"%.0f");
        ImGui::TextWrapped("Le PNG d'entrainement occupe une periode. Les Fourier se repetent, mais les features Perlin utilisent les coordonnees monde non repliees.");
        ImGui::TextWrapped("Apres un nouvel entrainement, cliquez sur Recharger le shader. Le fichier de poids par defaut renvoie un climat neutre.");
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
    if(ImGui::Button("Recharger le shader / poids",ImVec2(-1,0))) reloadShader();

    if(ImGui::Button("Preset climat actuel",ImVec2(-1,0)))
    {
        m_owner.climateSource=0;
        m_owner.humidityNoise=m_owner.temperatureNoise=0; m_owner.climateFrequency=.0005f;
        m_owner.humidityFrequencyMultiplier=m_owner.temperatureFrequencyMultiplier=1;
        m_owner.remapMinimum=-.32f; m_owner.remapMaximum=.32f;
        m_owner.postContrastHumidity=m_owner.postContrastTemperature=1;
        m_owner.postGammaHumidity=m_owner.postGammaTemperature=1;
        m_owner.attractionMode=0; m_owner.matrixBlend=true; changed=true;
    }
    if(ImGui::Button("Preset Simplex",ImVec2(-1,0)))
    { m_owner.climateSource=0; m_owner.humidityNoise=m_owner.temperatureNoise=1; changed=true; }
    if(ImGui::Button("Preset Patchwork regions",ImVec2(-1,0)))
    {
        m_owner.climateSource=0;
        m_owner.humidityNoise=m_owner.temperatureNoise=10;
        m_owner.climateFrequency=.0005f;
        m_owner.humidityFrequencyMultiplier=m_owner.temperatureFrequencyMultiplier=1;
        m_owner.remapMinimum=-1.0f; m_owner.remapMaximum=1.0f;
        m_owner.attractionMode=0; m_owner.matrixBlend=true; changed=true;
    }
    if(ImGui::Button("Preset Neural Climate",ImVec2(-1,0)))
    {
        m_owner.climateSource=1;
        m_owner.neuralWorldPeriod=32768.0f;
        m_owner.postContrastHumidity=m_owner.postContrastTemperature=1;
        m_owner.postGammaHumidity=m_owner.postGammaTemperature=1;
        m_owner.attractionMode=0; m_owner.matrixBlend=true; changed=true;
    }
    if(ImGui::Button("Reinitialiser le docking",ImVec2(-1,0))) m_resetDock=true;
    if(!m_owner.m_error.empty()) { ImGui::SeparatorText("Erreur"); ImGui::TextWrapped("%s",m_owner.m_error.c_str()); }
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
    if(texture) ImGui::Image(textureId(texture),ImVec2(side,side),ImVec2(0,1),ImVec2(1,0));
    else ImGui::TextUnformatted("Texture indisponible.");
    ImGui::End();
}
