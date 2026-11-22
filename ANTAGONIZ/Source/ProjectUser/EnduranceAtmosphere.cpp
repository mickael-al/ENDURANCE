#include "glcore.hpp"
#include "EnduranceAtmosphere.hpp"
#include "EnduranceMoonMesh.hpp"
#define CLEAR_REGISTRY
#include "Atmosphere.hpp"
#include "Camera.hpp"
#include "ComputeShader.hpp"
#include "DirectionalLight.hpp"
#include "Editor.hpp"
#include "Engine.hpp"
#include "PointeurClass.hpp"
#include "ProceduralGeneration.hpp"
#include "ProceduralTerrainBiomes.hpp"
#include "RenderingEngine.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>
#include <limits>
#include <utility>

REGISTER(Behaviour,EnduranceAtmosphereComponent,paletteFile,palettes,engineSettings,
         sunLightIndex,moonLightIndex,fogAtlasResolution,fogAtlasExtent,fogWind,moonSurfaceFile,moonModelFile);

namespace
{
    using Clock=std::chrono::steady_clock;
    constexpr const char* ShaderRoot="../ENDURANCE/Ressources/Shader/Atmosphere/";
    EnduranceAtmosphereComponent* activeComponent=nullptr;
    // Render-thread sequence survives component restarts, so subscribers can
    // distinguish a newly completed sample from a previous scene's snapshot.
    std::uint64_t nextBiomeSampleRevision=0;

    bool loadMoonSurface(Ge::Atmosphere& atmosphere,const std::string& path,std::string& error)
    {
        // Small game-owned baked asset; GPU allocation/upload belongs to the engine.
        // Explicit little-endian header, bounded size, exact payload validation.
        std::ifstream file(path,std::ios::binary);
        std::array<unsigned char,20> header{};
        if(!file.read(reinterpret_cast<char*>(header.data()),header.size()) ||
            std::memcmp(header.data(),"EMOON001",8)!=0)
        { error="Cannot read baked moon surface: "+path; return false; }
        const auto u32=[&](unsigned i) { return std::uint32_t(header[i]) | (std::uint32_t(header[i+1])<<8)
            | (std::uint32_t(header[i+2])<<16) | (std::uint32_t(header[i+3])<<24); };
        const auto width=u32(8),height=u32(12),meanBits=u32(16);
        float mean=0; std::memcpy(&mean,&meanBits,sizeof mean);
        if(width<2 || width>4096 || height<1 || height>2048 || width!=height*2 ||
            !std::isfinite(mean) || mean<0 || mean>1)
        { error="Invalid baked moon dimensions/albedo: "+path; return false; }
        std::vector<std::uint8_t> pixels(std::size_t(width)*height*4);
        if(!file.read(reinterpret_cast<char*>(pixels.data()),std::streamsize(pixels.size())) ||
            file.peek()!=std::char_traits<char>::eof())
        { error="Invalid baked moon payload: "+path; return false; }
        return atmosphere.setMoonSurface(pixels.data(),width,height,mean);
    }

    struct PaletteAsset
    {
        int version=1;
        std::string colorSpace="linear-sRGB";
        std::vector<EnduranceSkyBiomeStyle> palettes;
        JS_OBJ(version,colorSpace,palettes);
    };

    struct alignas(16) GpuStyle
    {
        glm::vec4 horizon,middle,zenith,fogMedium,fogShape,controls;
    };
    struct alignas(16) GpuPalette
    {
        glm::vec4 horizon,middle,zenith,controls;
        std::uint32_t biome=0;
        float weight=0.0f;
        glm::vec2 reserved{0.0f};
    };
    static_assert(sizeof(GpuStyle)==96 && sizeof(GpuPalette)==80,"std430 palette ABI");

    glm::vec3 color(const std::array<float,3>& c) { return {c[0],c[1],c[2]}; }
    Ge::AtmospherePalette enginePalette(const EnduranceSkyBiomeStyle& s)
    {
        Ge::AtmospherePalette p;
        p.horizon=color(s.horizon); p.middle=color(s.middle); p.zenith=color(s.zenith);
        p.middleElevation=s.middleElevation; p.gradientPower=s.gradientPower;
        p.strength=s.strength; p.exposure=s.exposure;
        return p;
    }

    bool validStyles(const std::vector<EnduranceSkyBiomeStyle>& styles)
    {
        if(styles.size()!=ProceduralTerrain::BiomeCount) return false;
        const auto range=[](float v,float lo,float hi)
        { return ProceduralTerrain::finiteFieldValue(v) && v>=lo && v<=hi; };
        for(std::size_t i=0;i<styles.size();++i)
        {
            const auto& s=styles[i];
            if(s.biome!=ProceduralTerrain::BiomeNames[i]) return false;
            for(float c:s.horizon) if(!range(c,0.0f,65504.0f)) return false;
            for(float c:s.middle) if(!range(c,0.0f,65504.0f)) return false;
            for(float c:s.zenith) if(!range(c,0.0f,65504.0f)) return false;
            for(float c:s.fogAlbedo) if(!range(c,0.0f,1.0f)) return false;
            if(!range(s.middleElevation,0.01f,0.99f) || !range(s.gradientPower,0.1f,10.0f)
                || !range(s.strength,0.0f,1.0f) || !range(s.exposure,0.0f,1000.0f)
                || !range(s.fogDensity,0.0f,0.05f) || !range(s.fogBaseHeight,-100000.0f,100000.0f)
                || !range(s.fogHeightScale,1.0f,100000.0f) || !range(s.fogNoiseAmount,0.0f,1.0f)
                || !range(s.fogNoiseScale,0.000001f,1.0f)) return false;
        }
        return true;
    }

    // A custom renderer leaves all bindings it touches exactly as it found them.
    struct ComputeGuard
    {
        GLint program=0,storage=0;
        static constexpr std::array<GLuint,3> bindings{{7,11,12}};
        std::array<GLint,3> buffers{};
        std::array<GLint64,3> starts{},sizes{};
        struct Image { GLint name=0,level=0,layered=0,layer=0,access=0,format=0; } images[2];
        ComputeGuard()
        {
            glGetIntegerv(GL_CURRENT_PROGRAM,&program);
            glGetIntegerv(GL_SHADER_STORAGE_BUFFER_BINDING,&storage);
            for(std::size_t i=0;i<bindings.size();++i)
            {
                glGetIntegeri_v(GL_SHADER_STORAGE_BUFFER_BINDING,bindings[i],&buffers[i]);
                glGetInteger64i_v(GL_SHADER_STORAGE_BUFFER_START,bindings[i],&starts[i]);
                glGetInteger64i_v(GL_SHADER_STORAGE_BUFFER_SIZE,bindings[i],&sizes[i]);
            }
            for(GLuint i=0;i<2;++i)
            {
                glGetIntegeri_v(GL_IMAGE_BINDING_NAME,i,&images[i].name);
                glGetIntegeri_v(GL_IMAGE_BINDING_LEVEL,i,&images[i].level);
                glGetIntegeri_v(GL_IMAGE_BINDING_LAYERED,i,&images[i].layered);
                glGetIntegeri_v(GL_IMAGE_BINDING_LAYER,i,&images[i].layer);
                glGetIntegeri_v(GL_IMAGE_BINDING_ACCESS,i,&images[i].access);
                glGetIntegeri_v(GL_IMAGE_BINDING_FORMAT,i,&images[i].format);
            }
        }
        ~ComputeGuard()
        {
            glUseProgram(program);
            for(std::size_t i=0;i<bindings.size();++i)
                if(buffers[i] && sizes[i]>0)
                    glBindBufferRange(GL_SHADER_STORAGE_BUFFER,bindings[i],buffers[i],starts[i],sizes[i]);
                else glBindBufferBase(GL_SHADER_STORAGE_BUFFER,bindings[i],buffers[i]);
            glBindBuffer(GL_SHADER_STORAGE_BUFFER,storage);
            for(GLuint i=0;i<2;++i)
                glBindImageTexture(i,images[i].name,images[i].level,images[i].layered,
                    images[i].layer,images[i].access,images[i].format);
        }
    };

    std::uint64_t climateKey(const ProceduralGeneration& terrain)
    {
        const auto& f=terrain.getConfig().field;
        std::uint64_t key=1469598103934665603ull;
        const auto add=[&](std::uint32_t value) { key=(key^value)*1099511628211ull; };
        add(terrain.getSeed()); add(static_cast<std::uint32_t>(f.mode));
        add(static_cast<std::uint32_t>(f.biomeOverride));
        for(float value:{f.baseHeight,f.heightAmplitude,f.heightFrequency,
                         f.seaLevel,f.seaBlendWidth,f.bandOffset,f.bandOffsetVariation})
        {
            std::uint32_t bits=0; std::memcpy(&bits,&value,sizeof bits); add(bits);
        }
        return key;
    }

    void setFieldUniforms(GLuint program,const ProceduralGeneration& terrain)
    {
        const auto& f=terrain.getConfig().field;
        const auto one=[&](const char* name,float v)
        { glProgramUniform1f(program,glGetUniformLocation(program,name),v); };
        glProgramUniform1ui(program,glGetUniformLocation(program,"uSeed"),terrain.getSeed());
        glProgramUniform1ui(program,glGetUniformLocation(program,"uFieldMode"),static_cast<unsigned int>(f.mode));
        glProgramUniform1i(program,glGetUniformLocation(program,"uBiomeOverride"),f.biomeOverride);
        one("uBaseHeight",f.baseHeight); one("uHeightAmplitude",f.heightAmplitude);
        one("uHeightFrequency",f.heightFrequency);
        one("uSeaLevel",f.seaLevel); one("uSeaBlendWidth",f.seaBlendWidth);
        one("uBandOffset",f.bandOffset); one("uBandOffsetVariation",f.bandOffsetVariation);
    }

    GLuint createAtlasTexture(GLenum format,int resolution)
    {
        GLuint result=0;
        glCreateTextures(GL_TEXTURE_2D,1,&result);
        glTextureStorage2D(result,1,format,resolution,resolution);
        glTextureParameteri(result,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
        glTextureParameteri(result,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        glTextureParameteri(result,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
        glTextureParameteri(result,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        const float zero[4]={0,0,0,0};
        glClearTexImage(result,0,GL_RGBA,GL_FLOAT,zero);
        return result;
    }
}

struct EnduranceAtmosphereComponent::Impl
{
    const ptrClass* pc=nullptr;
    Ge::Atmosphere* atmosphere=nullptr;
    SceneData* lightScene=nullptr;
    Ge::Lights* sunReference=nullptr;
    Ge::Lights* moonReference=nullptr;
    int lastSunIndex=std::numeric_limits<int>::min();
    int lastMoonIndex=std::numeric_limits<int>::min();
    bool lightSelectionDirty=false;
    bool registered=false,stylesDirty=true,forceSample=true,reloadRequested=false;
    bool atlasValid=false,atlasDirty=true,hasPosition=false;
    bool moonLoadAttempted=false,moonReloadRequested=false,moonMeshLoaded=false,moonAtlasLoaded=false;
    std::string loadedMoonFile,loadedMoonModelFile,moonError;
    std::string error;
    GLuint paletteProgram=0,fogProgram=0,mapping=0,styleBuffer=0;
    GLuint fogMedium=0,fogShape=0;
    int allocatedResolution=0;
    glm::vec2 lastSubmittedXZ{0.0f};
    glm::vec4 atlasOriginSize{0.0f};
    glm::dvec3 fogWindOffset{0.0};
    std::uint32_t seed=1337;
    std::uint64_t terrainKey=0,revision=1,nextSequence=0,lastResultSequence=0;
    std::uint64_t submitted=0,completed=0,busyFrames=0,atlasUpdates=0;
    double latencyMilliseconds=0.0,climateMilliseconds=0.0,atlasMilliseconds=0.0;
    glm::vec3 climate{0.5f,0.5f,0.0f};
    EnduranceBiomeSample biomeSample;
    bool hasBiomeSource=false;
    struct Slot
    {
        GLuint buffer=0,queries[2]{0,0};
        GLsync fence=nullptr;
        std::uint64_t revision=0,sequence=0;
        bool force=false,timed=false;
        glm::vec2 worldXZ{0.0f};
        Clock::time_point submitted;
    };
    std::array<Slot,3> slots;
    GLuint atlasQueries[2]{0,0};
    bool atlasTimingPending=false;

    void invalidateBiomeSample()
    {
        biomeSample.valid=false;
        biomeSample.revision=++nextBiomeSampleRevision;
    }

    void loadMoonAssets(const std::string& modelPath,const std::string& surfacePath)
    {
        // Asset work is triggered by an explicit request/path change, never by
        // the animation clock, camera movement or a failed previous attempt.
        moonLoadAttempted=true; moonReloadRequested=false;
        loadedMoonModelFile=modelPath; loadedMoonFile=surfacePath; moonError.clear();
        if(!modelPath.empty())
        {
            Ge::MoonMeshData mesh;
            if(EnduranceMoon::loadMoonMesh(modelPath,mesh,moonError))
            {
                if(atmosphere->setMoonMesh(&mesh))
                {
                    moonMeshLoaded=true; moonAtlasLoaded=false;
                    // The GLB owns its albedo; avoid retaining the old 8 MiB atlas.
                    atmosphere->setMoonSurface(nullptr,0,0,0);
                    return;
                }
                moonError="Cannot upload lunar mesh: "+atmosphere->error();
            }
            // Failed hot reloads leave the complete previously loaded body.
            if(moonMeshLoaded) return;
        }
        else
        {
            atmosphere->setMoonMesh(nullptr);
            moonMeshLoaded=false;
        }
        std::string surfaceError;
        if(!surfacePath.empty() && loadMoonSurface(*atmosphere,surfacePath,surfaceError))
        { moonAtlasLoaded=true; return; }
        if(!surfaceError.empty())
            moonError+=(moonError.empty()?"":"\n")+surfaceError;
        // Preserve a previously valid atlas on failure; an empty path is an
        // explicit request for the renderer's analytic fallback.
        if(surfacePath.empty())
        {
            atmosphere->setMoonSurface(nullptr,0,0,0);
            moonAtlasLoaded=false;
        }
    }

    std::pair<Ge::Lights*,Ge::Lights*> resolveLights(int& sunIndex,int& moonIndex)
    {
        auto* editor=Ge::Editor::instance();
        SceneData* scene=editor ? editor->getCurrentSceneData() : nullptr;
        if(!scene) return {nullptr,nullptr};
        if(scene!=lightScene)
        {
            lightScene=scene; sunReference=moonReference=nullptr;
            lastSunIndex=lastMoonIndex=std::numeric_limits<int>::min();
        }
        const auto resolve=[&](int& index,int& previous,Ge::Lights*& reference)->Ge::Lights*
        {
            if(index<0) { reference=nullptr; previous=index; return nullptr; }
            if(index!=previous || lightSelectionDirty)
                reference=index<int(scene->dlight.size()) ? scene->dlight[index] : nullptr;
            // Indices are only the serialized locator. While running, retain
            // the selected object's identity when another light is removed.
            // Comparing pointer values is safe even after deletion; never
            // dereference the old reference until membership is established.
            const auto found=std::find(scene->dlight.begin(),scene->dlight.end(),reference);
            if(reference && found!=scene->dlight.end()) index=static_cast<int>(found-scene->dlight.begin());
            else { index=-1; reference=nullptr; }
            previous=index;
            return reference;
        };
        auto* resolvedSun=resolve(sunIndex,lastSunIndex,sunReference);
        auto* resolvedMoon=resolve(moonIndex,lastMoonIndex,moonReference);
        lightSelectionDirty=false;
        return {resolvedSun,resolvedMoon};
    }

    bool compilePrograms()
    {
        Ge::ComputeShader camera(std::string(ShaderRoot)+"palette.C.glsl",{},"ENDURANCE climate palette");
        Ge::ComputeShader fog(std::string(ShaderRoot)+"fog_map.C.glsl",{},"ENDURANCE world fog atlas");
        if(!camera.isValid() || !fog.isValid())
        {
            error=camera.isValid() ? fog.getLastError() : camera.getLastError();
            return false;
        }
        if(paletteProgram) glDeleteProgram(paletteProgram);
        if(fogProgram) glDeleteProgram(fogProgram);
        paletteProgram=camera.takeProgram(); fogProgram=fog.takeProgram();
        error.clear(); ++revision; forceSample=true; atlasDirty=true;
        invalidateBiomeSample();
        return true;
    }

    void bindFog(GLuint program) const
    {
        glBindTextureUnit(6,fogMedium);
        glBindTextureUnit(7,fogShape);
        glProgramUniform4fv(program,glGetUniformLocation(program,"uGameFogOriginSize"),1,&atlasOriginSize.x);
        glProgramUniform1i(program,glGetUniformLocation(program,"uGameFogAtlasValid"),atlasValid?1:0);
        glProgramUniform1ui(program,glGetUniformLocation(program,"uGameFogSeed"),seed);
        glProgramUniform3f(program,glGetUniformLocation(program,"uGameFogWindOffset"),
            float(fogWindOffset.x),float(fogWindOffset.y),float(fogWindOffset.z));
    }

    void collect()
    {
        for(auto& slot:slots)
        {
            if(!slot.fence) continue;
            // A zero timeout is a poll. Never wait for current-frame work.
            const GLenum state=glClientWaitSync(slot.fence,0,0);
            if(state==GL_TIMEOUT_EXPIRED) continue;
            if(state==GL_WAIT_FAILED)
            {
                error="GPU climate fence failed; retaining the last complete sky palette.";
                glDeleteSync(slot.fence); slot.fence=nullptr;
                continue;
            }
            GpuPalette value{};
            // GPU completion was established above: this read has no pending
            // producer to wait for, unlike reading immediately after dispatch.
            glGetNamedBufferSubData(slot.buffer,0,sizeof value,&value);
            glDeleteSync(slot.fence); slot.fence=nullptr;
            ++completed;
            latencyMilliseconds=std::chrono::duration<double,std::milli>(Clock::now()-slot.submitted).count();
            if(slot.timed)
            {
                GLuint64 begin=0,end=0;
                glGetQueryObjectui64v(slot.queries[0],GL_QUERY_RESULT,&begin);
                glGetQueryObjectui64v(slot.queries[1],GL_QUERY_RESULT,&end);
                climateMilliseconds=double(end-begin)*1e-6;
            }
            if(slot.revision!=revision || slot.sequence<=lastResultSequence) continue;
            lastResultSequence=slot.sequence;
            // This shared snapshot must reject NaN/Inf even in -ffast-math
            // builds. The terrain helper checks uint32 exponent bits via memcpy.
            if(value.biome<ProceduralTerrain::BiomeCount &&
                ProceduralTerrain::finiteFieldValue(value.weight) && value.weight>0.0f && value.weight<=1.00001f &&
                ProceduralTerrain::finiteFieldValue(slot.worldXZ.x) && ProceduralTerrain::finiteFieldValue(slot.worldXZ.y))
            {
                biomeSample={true,value.biome,std::clamp(value.weight,0.0f,1.0f),
                    slot.worldXZ,++nextBiomeSampleRevision};
            }
            else invalidateBiomeSample();
            Ge::AtmospherePalette p;
            p.horizon=glm::vec3(value.horizon); p.middle=glm::vec3(value.middle);
            p.zenith=glm::vec3(value.zenith); p.middleElevation=value.horizon.w;
            p.gradientPower=value.middle.w; p.strength=value.zenith.w;
            p.exposure=value.controls.x;
            climate=glm::vec3(value.controls.y,value.controls.z,value.controls.w);
            atmosphere->setPaletteTarget(p,slot.force);
        }
        if(atlasTimingPending)
        {
            GLint ready=0;
            glGetQueryObjectiv(atlasQueries[1],GL_QUERY_RESULT_AVAILABLE,&ready);
            if(ready)
            {
                GLuint64 begin=0,end=0;
                glGetQueryObjectui64v(atlasQueries[0],GL_QUERY_RESULT,&begin);
                glGetQueryObjectui64v(atlasQueries[1],GL_QUERY_RESULT,&end);
                atlasMilliseconds=double(end-begin)*1e-6;
                atlasTimingPending=false;
            }
        }
    }
};

EnduranceAtmosphereComponent::EnduranceAtmosphereComponent():m(std::make_unique<Impl>()) {}
EnduranceAtmosphereComponent::~EnduranceAtmosphereComponent() { stop(); }

EnduranceBiomeSample EnduranceAtmosphereComponent::currentBiomeSample()
{
    if(!activeComponent || !activeComponent->m || !activeComponent->m->registered)
        return {};
    auto result=activeComponent->m->biomeSample;
    if(!activeComponent->m->hasBiomeSource) result.valid=false;
    return result;
}

bool EnduranceAtmosphereComponent::loadPaletteFile()
{
    std::ifstream file(paletteFile,std::ios::binary);
    if(!file) { m->error="Cannot read sky palette file: "+paletteFile; return false; }
    std::string text((std::istreambuf_iterator<char>(file)),std::istreambuf_iterator<char>());
    PaletteAsset asset;
    JS::ParseContext context(text);
    if(context.parseTo(asset)!=JS::Error::NoError || asset.version!=1 ||
        asset.colorSpace!="linear-sRGB" || !validStyles(asset.palettes))
    {
        m->error="Invalid sky palette file: require 26 ordered biome profiles, version 1 and linear-sRGB colors.";
        return false;
    }
    palettes=std::move(asset.palettes);
    m->stylesDirty=true; m->forceSample=true; m->atlasDirty=true; m->error.clear();
    ++m->revision;
    m->invalidateBiomeSample();
    return true;
}

void EnduranceAtmosphereComponent::start()
{
    stop();
    m=std::make_unique<Impl>();
    if(activeComponent && activeComponent!=this)
    {
        m->error="Only one ENDURANCE atmosphere component can own the active scene sky.";
        return;
    }
    m->pc=Ge::Engine::getPtrClassAddr();
    if(!m->pc || !m->pc->renderingEngine || !m->pc->graphiquePipelineManager)
    { m->error="Atmosphere requires the rendering engine."; return; }
    if(palettes.empty() && !loadPaletteFile()) return;
    if(!validStyles(palettes)) { m->error="Invalid saved biome palettes; reload the palette file."; return; }
    m->atmosphere=m->pc->renderingEngine->getAtmosphere();
    if(!m->atmosphere) { m->error="The atmosphere service is unavailable."; return; }
    ComputeGuard guard;
    if(!m->compilePrograms()) return;
    activeComponent=this;
    if(!engineSettings.empty())
    {
        if(!m->atmosphere->loadSettings(engineSettings))
            m->error="Invalid saved atmosphere settings; keeping the engine defaults.";
    }
    else m->atmosphere->settings().enabled=true;
    if(m->atmosphere->settings().moonSurface)
        m->loadMoonAssets(moonModelFile,moonSurfaceFile);
    m->atmosphere->setPaletteTarget(enginePalette(palettes[12]),true);
    const auto mapping=ProceduralTerrain::makeBiomeCellTable();
    glCreateBuffers(1,&m->mapping);
    glNamedBufferData(m->mapping,sizeof mapping,mapping.data(),GL_STATIC_DRAW);
    glCreateBuffers(1,&m->styleBuffer);
    for(auto& slot:m->slots)
    {
        glCreateBuffers(1,&slot.buffer);
        glNamedBufferData(slot.buffer,sizeof(GpuPalette),nullptr,GL_STREAM_READ);
        glGenQueries(2,slot.queries);
    }
    glGenQueries(2,m->atlasQueries);
    m->atmosphere->setGameFogShader(std::string(ShaderRoot)+"game_fog.glsl");
    m->atmosphere->setGameFogBinding([this](unsigned int program) { m->bindFog(program); });
    m->pc->graphiquePipelineManager->addCustomRenderer(this);
    m->registered=true;
}

void EnduranceAtmosphereComponent::stop()
{
    if(!m) return;
    if(m->registered && m->pc && m->pc->graphiquePipelineManager)
        m->pc->graphiquePipelineManager->removeCustomRenderer(this);
    m->registered=false;
    if(activeComponent==this)
    {
        if(m->atmosphere)
        {
            engineSettings=m->atmosphere->serializeSettings();
            m->atmosphere->settings().enabled=false;
            m->atmosphere->setGameFogBinding({});
            m->atmosphere->setGameFogShader({});
            m->atmosphere->setSun(nullptr); m->atmosphere->setMoon(nullptr);
            m->atmosphere->setMoonMesh(nullptr);
            m->atmosphere->setMoonSurface(nullptr,0,0,0);
        }
        activeComponent=nullptr;
    }
    for(auto& slot:m->slots)
    {
        if(slot.fence) glDeleteSync(slot.fence);
        if(slot.buffer) glDeleteBuffers(1,&slot.buffer);
        if(slot.queries[0]) glDeleteQueries(2,slot.queries);
        slot={};
    }
    if(m->atlasQueries[0]) glDeleteQueries(2,m->atlasQueries);
    if(m->paletteProgram) glDeleteProgram(m->paletteProgram);
    if(m->fogProgram) glDeleteProgram(m->fogProgram);
    if(m->mapping) glDeleteBuffers(1,&m->mapping);
    if(m->styleBuffer) glDeleteBuffers(1,&m->styleBuffer);
    if(m->fogMedium) glDeleteTextures(1,&m->fogMedium);
    if(m->fogShape) glDeleteTextures(1,&m->fogShape);
    m=std::make_unique<Impl>();
}

void EnduranceAtmosphereComponent::prepareRenderer()
{
    if(!m->registered || !m->atmosphere) return;
    auto* camera=m->pc->cameraManager ? m->pc->cameraManager->getCurrentCamera() : nullptr;
    if(!camera)
    {
        if(m->hasBiomeSource) { ++m->revision; m->invalidateBiomeSample(); }
        m->hasBiomeSource=false; m->forceSample=true;
        return;
    }
    auto* editor=Ge::Editor::instance();
    SceneData* scene=editor ? editor->getCurrentSceneData() : nullptr;
    const auto lights=m->resolveLights(sunLightIndex,moonLightIndex);
    m->atmosphere->setSun(lights.first);
    m->atmosphere->setMoon(lights.second);
    m->atmosphere->settings().fallbackToFirstDirectional=(sunLightIndex==-2);

    // Render time progresses even when behaviours are paused or the camera
    // stays still. Integrate the velocity rather than multiplying the current
    // wind by total time: editing the wind must not relocate existing features.
    const double deltaSeconds=m->pc->time ? m->pc->time->getDeltaTime() : 0.0;
    if(std::isfinite(deltaSeconds) && deltaSeconds>0.0)
        for(int axis=0;axis<3;++axis)
        {
            const double velocity=std::isfinite(fogWind[axis])
                ? std::clamp(fogWind[axis],-100.0f,100.0f) : 0.0;
            m->fogWindOffset[axis]+=velocity*deltaSeconds;
        }

    // The camera climate snapshot is also consumed by biome effects, even
    // when atmospheric rendering itself is disabled.
    if(m->atmosphere->settings().enabled && m->atmosphere->settings().moonSurface && (!m->moonLoadAttempted || m->moonReloadRequested ||
        m->loadedMoonModelFile!=moonModelFile || m->loadedMoonFile!=moonSurfaceFile))
        m->loadMoonAssets(moonModelFile,moonSurfaceFile);

    ProceduralGeneration* terrain=nullptr;
    for(auto* renderer:m->pc->graphiquePipelineManager->getCustomRenderer())
        if(auto* candidate=dynamic_cast<ProceduralGeneration*>(renderer))
            if(candidate->isInitialized() && (!scene || &candidate->getScene()==scene))
            { terrain=candidate; break; }
    // A failed palette edit below must not keep an otherwise stale scene's
    // biome snapshot visible while its terrain has been removed.
    if(!terrain && m->hasBiomeSource)
    {
        m->hasBiomeSource=false;
        m->invalidateBiomeSample();
    }

    ComputeGuard guard;
    if(m->reloadRequested)
    {
        m->reloadRequested=false;
        if(m->compilePrograms()) m->atmosphere->reloadShaders();
    }
    if(m->stylesDirty)
    {
        if(!validStyles(palettes)) { m->error="Invalid palette values; retaining the last uploaded palette."; return; }
        std::array<GpuStyle,ProceduralTerrain::BiomeCount> styles{};
        for(std::size_t i=0;i<styles.size();++i)
        {
            const auto& s=palettes[i];
            styles[i]={glm::vec4(color(s.horizon),s.middleElevation),glm::vec4(color(s.middle),s.gradientPower),
                glm::vec4(color(s.zenith),s.strength),glm::vec4(color(s.fogAlbedo)*s.fogDensity,s.fogDensity),
                glm::vec4(s.fogBaseHeight,s.fogHeightScale,s.fogNoiseAmount,s.fogNoiseScale),
                glm::vec4(s.exposure,0.0f,0.0f,0.0f)};
        }
        // Orphan the small upload buffer rather than waiting for an old atlas dispatch.
        glNamedBufferData(m->styleBuffer,sizeof styles,styles.data(),GL_DYNAMIC_DRAW);
        m->stylesDirty=false; m->forceSample=true; m->atlasDirty=true; ++m->revision;
        m->invalidateBiomeSample();
        if(!terrain) m->atmosphere->setPaletteTarget(enginePalette(palettes[12]),true);
    }
    if(!terrain)
    {
        // A terrain can disappear briefly while its generator is rebuilt.
        // Invalidate pending camera results and rebuild the atlas when it
        // returns, even if its seed, settings and camera position are unchanged.
        if(m->terrainKey!=0) { m->terrainKey=0; ++m->revision; m->invalidateBiomeSample(); }
        m->hasBiomeSource=false;
        m->atlasValid=false;
        m->atlasDirty=true; m->forceSample=true;
        m->collect();
        return;
    }
    m->hasBiomeSource=true;
    const auto key=climateKey(*terrain);
    if(key!=m->terrainKey)
    {
        m->terrainKey=key; ++m->revision; m->forceSample=true; m->atlasDirty=true;
        m->invalidateBiomeSample();
        m->seed=terrain->getSeed();
        m->atmosphere->settings().seaLevel=terrain->getConfig().field.seaLevel;
    }
    m->collect();
    const glm::vec3 position=camera->getPosition();
    const glm::vec2 xz(position.x,position.z);
    const float threshold=std::max(m->atmosphere->settings().movementThreshold,0.0f);
    const glm::vec2 moved=xz-m->lastSubmittedXZ;
    const bool sample=m->forceSample || !m->hasPosition || glm::dot(moved,moved)>=threshold*threshold;
    if(sample)
    {
        auto slot=std::find_if(m->slots.begin(),m->slots.end(),[](const Impl::Slot& s) { return !s.fence; });
        if(slot==m->slots.end()) ++m->busyFrames;
        else
        {
            setFieldUniforms(m->paletteProgram,*terrain);
            glProgramUniform2f(m->paletteProgram,glGetUniformLocation(m->paletteProgram,"uPaletteCameraXZ"),xz.x,xz.y);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER,7,m->mapping);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER,11,slot->buffer);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER,12,m->styleBuffer);
            glUseProgram(m->paletteProgram);
            slot->timed=m->atmosphere->settings().measureGpu;
            if(slot->timed) glQueryCounter(slot->queries[0],GL_TIMESTAMP);
            glDispatchCompute(1,1,1);
            glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);
            if(slot->timed) glQueryCounter(slot->queries[1],GL_TIMESTAMP);
            slot->fence=glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE,0);
            slot->revision=m->revision; slot->sequence=++m->nextSequence;
            slot->worldXZ=xz;
            slot->force=m->forceSample; slot->submitted=Clock::now();
            ++m->submitted;
            m->forceSample=false; m->lastSubmittedXZ=xz; m->hasPosition=true;
        }
    }

    if(!m->atmosphere->settings().enabled || !m->atmosphere->settings().localFog) return;
    // The guard band guarantees coverage of every ray up to aerialDistance.
    static constexpr int atlasPresets[4]={32,64,96,128};
    const int quality=std::clamp(static_cast<int>(m->atmosphere->settings().quality),0,3);
    const int requestedResolution=fogAtlasResolution>0 ? fogAtlasResolution : atlasPresets[quality];
    // Recentring moves 1/16 of the domain. Multiples of 16 ensure an integer
    // number of texels moves, preserving retained world-space sample positions.
    const int resolution=std::clamp((std::clamp(requestedResolution,32,512)+15)/16*16,32,512);
    const float requested=ProceduralTerrain::finiteFieldValue(fogAtlasExtent) ? fogAtlasExtent : 81920.0f;
    const float extent=std::max({requested,1024.0f,m->atmosphere->settings().aerialDistance*2.5f});
    const float snap=extent/16.0f;
    const glm::vec2 origin=glm::floor(xz/snap)*snap-glm::vec2(extent*0.5f);
    if(m->allocatedResolution!=resolution)
    {
        if(m->fogMedium) glDeleteTextures(1,&m->fogMedium);
        if(m->fogShape) glDeleteTextures(1,&m->fogShape);
        m->fogMedium=createAtlasTexture(GL_RGBA16F,resolution);
        m->fogShape=createAtlasTexture(GL_RGBA32F,resolution);
        m->allocatedResolution=resolution; m->atlasDirty=true;
    }
    const glm::vec4 placement(origin,extent,float(resolution));
    if(placement!=m->atlasOriginSize) m->atlasDirty=true;
    if(m->atlasDirty)
    {
        m->atlasOriginSize=placement;
        setFieldUniforms(m->fogProgram,*terrain);
        glProgramUniform4fv(m->fogProgram,glGetUniformLocation(m->fogProgram,"uGameFogOriginSize"),1,&placement.x);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER,7,m->mapping);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER,12,m->styleBuffer);
        glBindImageTexture(0,m->fogMedium,0,GL_FALSE,0,GL_WRITE_ONLY,GL_RGBA16F);
        glBindImageTexture(1,m->fogShape,0,GL_FALSE,0,GL_WRITE_ONLY,GL_RGBA32F);
        glUseProgram(m->fogProgram);
        const bool measure=m->atmosphere->settings().measureGpu && !m->atlasTimingPending;
        if(measure) glQueryCounter(m->atlasQueries[0],GL_TIMESTAMP);
        glDispatchCompute((resolution+7)/8,(resolution+7)/8,1);
        glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT|GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
        if(measure) { glQueryCounter(m->atlasQueries[1],GL_TIMESTAMP); m->atlasTimingPending=true; }
        m->atlasValid=true; m->atlasDirty=false; ++m->atlasUpdates;
    }
}

void EnduranceAtmosphereComponent::onGUI()
{
    ImGui::TextUnformatted("ENDURANCE atmosphere / biome palettes");
    if(ImGui::Button("Reload biome palette asset")) loadPaletteFile();
    if(m->registered)
    {
        ImGui::SameLine();
        if(ImGui::Button("Reload climate / fog shaders")) m->reloadRequested=true;
    }
    if(!m->error.empty()) ImGui::TextWrapped("%s",m->error.c_str());
    if(!m->registered)
        ImGui::TextWrapped("Start the scene to preview the atmosphere. Saved palettes remain editable here.");

    auto* editor=Ge::Editor::instance();
    SceneData* scene=editor ? editor->getCurrentSceneData() : nullptr;
    if(m->registered) m->resolveLights(sunLightIndex,moonLightIndex);
    const auto lightSelector=[&](const char* label,int& index,bool fallback)
    {
        std::string preview=index==-2 ? "First directional light (fallback)" : "Unassigned";
        if(scene && index>=0 && index<int(scene->dlight.size())) preview=*scene->dlight[index]->getName();
        if(ImGui::BeginCombo(label,preview.c_str()))
        {
            if(fallback && ImGui::Selectable("First directional light (fallback)",index==-2))
            { index=-2; m->lightSelectionDirty=true; }
            if(ImGui::Selectable("Unassigned",index==-1))
            { index=-1; m->lightSelectionDirty=true; }
            if(scene) for(int i=0;i<int(scene->dlight.size());++i)
            {
                ImGui::PushID(i);
                if(ImGui::Selectable(scene->dlight[i]->getName()->c_str(),index==i))
                { index=i; m->lightSelectionDirty=true; }
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
    };
    lightSelector("Atmospheric sun",sunLightIndex,true);
    lightSelector("Optional moon light",moonLightIndex,false);
    if(ImGui::TreeNode("Lunar model"))
    {
        ImGui::TextWrapped("Model: %s",moonModelFile.empty()?"(legacy surface)":moonModelFile.c_str());
        if(m->registered && ImGui::Button("Reload lunar asset")) m->moonReloadRequested=true;
        if(m->registered)
        {
            ImGui::TextUnformatted(m->moonMeshLoaded?"Cached excavated mesh":
                (m->moonAtlasLoaded?"Legacy surface atlas":"Analytic fallback"));
            const auto& stats=m->atmosphere->stats();
            ImGui::Text("Mesh image / shadow updates: %llu / %llu",
                static_cast<unsigned long long>(stats.moonImageUpdates),
                static_cast<unsigned long long>(stats.moonShadowUpdates));
        }
        if(!m->moonError.empty()) ImGui::TextWrapped("%s",m->moonError.c_str());
        ImGui::TreePop();
    }
    bool atlasChanged=ImGui::SliderInt("Fog atlas resolution (0 = quality)",&fogAtlasResolution,0,512);
    atlasChanged|=ImGui::DragFloat("Fog atlas extent (m)",&fogAtlasExtent,256.0f,1024.0f,262144.0f,"%.0f");
    if(atlasChanged) m->atlasDirty=true;
    if(ImGui::IsItemHovered())
        ImGui::SetTooltip("World-space grid; actual extent is at least 2.5 times the atmospheric ray distance.\n"
                          "Exact terrain climate is sampled at grid nodes; fog properties interpolate between them.");
    ImGui::DragFloat3("Fog wind XYZ (m/s)",fogWind.data(),0.1f,-100.0f,100.0f,"%.2f");
    if(ImGui::IsItemHovered())
        ImGui::SetTooltip("Advects only the 3D fog structure. Biome regions and the height profile remain fixed.\n"
                          "Set all components to zero to stop motion; changing wind preserves the current noise position.");
    if(ImGui::TreeNode("Biome palette and local medium"))
    {
        static int selected=0;
        if(ImGui::BeginCombo("Biome",ProceduralTerrain::BiomeNames[selected]))
        {
            for(int i=0;i<int(ProceduralTerrain::BiomeCount);++i)
                if(ImGui::Selectable(ProceduralTerrain::BiomeNames[i],selected==i)) selected=i;
            ImGui::EndCombo();
        }
        if(palettes.size()==ProceduralTerrain::BiomeCount)
        {
            auto& s=palettes[selected];
            constexpr ImGuiColorEditFlags hdr=ImGuiColorEditFlags_HDR|ImGuiColorEditFlags_Float;
            bool changed=ImGui::ColorEdit3("Horizon (linear HDR)",s.horizon.data(),hdr);
            changed|=ImGui::ColorEdit3("Middle (linear HDR)",s.middle.data(),hdr);
            changed|=ImGui::ColorEdit3("Zenith (linear HDR)",s.zenith.data(),hdr);
            changed|=ImGui::SliderFloat("Middle elevation (sin)",&s.middleElevation,0.01f,0.99f);
            changed|=ImGui::SliderFloat("Gradient power",&s.gradientPower,0.1f,10.0f);
            changed|=ImGui::SliderFloat("Artistic correction",&s.strength,0.0f,1.0f);
            changed|=ImGui::DragFloat("Palette HDR exposure",&s.exposure,0.05f,0.0f,1000.0f);
            changed|=ImGui::DragFloat("Local extinction (1/m)",&s.fogDensity,0.000001f,0.0f,0.05f,"%.6f");
            changed|=ImGui::ColorEdit3("Fog scattering albedo",s.fogAlbedo.data());
            changed|=ImGui::DragFloat("Fog base above sea (m)",&s.fogBaseHeight,1.0f,-100000.0f,100000.0f);
            changed|=ImGui::DragFloat("Fog scale height (m)",&s.fogHeightScale,1.0f,1.0f,100000.0f);
            changed|=ImGui::SliderFloat("Fog 3D structure",&s.fogNoiseAmount,0.0f,1.0f);
            changed|=ImGui::DragFloat("Fog noise frequency",&s.fogNoiseScale,0.00001f,0.000001f,1.0f,"%.6f");
            if(changed) { m->stylesDirty=true; m->forceSample=true; m->atlasDirty=true; }
            ImGui::TextWrapped("Extinction 0: absent. Structure 0: smooth. Positive structure: world-space 3D noise.");
        }
        ImGui::TreePop();
    }
    if(m->registered)
    {
        ImGui::Text("Climate H %.3f / T %.3f / marine %.3f",m->climate.x,m->climate.y,m->climate.z);
        ImGui::Text("Climate GPU %.3f ms; async latency %.1f ms; %llu/%llu results; busy %llu",
            m->climateMilliseconds,m->latencyMilliseconds,
            static_cast<unsigned long long>(m->completed),static_cast<unsigned long long>(m->submitted),
            static_cast<unsigned long long>(m->busyFrames));
        ImGui::Text("Fog atlas GPU %.3f ms / %llu updates; %d x %d / coverage %.0f m",m->atlasMilliseconds,
            static_cast<unsigned long long>(m->atlasUpdates),m->allocatedResolution,m->allocatedResolution,m->atlasOriginSize.z);
        m->atmosphere->onGUI();
    }
}

std::string EnduranceAtmosphereComponent::serialize()
{
    if(m->registered && m->atmosphere)
    {
        m->resolveLights(sunLightIndex,moonLightIndex);
        engineSettings=m->atmosphere->serializeSettings();
    }
    return JS::serializeStruct(*this);
}

void EnduranceAtmosphereComponent::load(std::string data)
{
    JS::ParseContext context(data);
    if(context.parseTo(*this)!=JS::Error::NoError)
        m->error="Invalid EnduranceAtmosphereComponent scene JSON.";
    m->stylesDirty=true; m->forceSample=true; m->atlasDirty=true;
}
