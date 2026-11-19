#include "glcore.hpp"
#include "Atmosphere.hpp"
#include "Camera.hpp"
#include "LightManager.hpp"
#include "Lights.hpp"
#include "PointeurClass.hpp"
#include "GraphicsDataMisc.hpp"
#include "ShaderCompilation.hpp"
#include "Debug.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <array>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <limits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Ge
{
namespace
{
    // Exactly 30 16-byte slots followed by three column-major matrices.
    struct alignas(16) AtmosphereGpuParameters
    {
        glm::vec4 radii{},rayleigh{},mie{},absorption{},medium{};
        glm::vec4 sunDirection{},sunRadiance{},sunDisk{};
        glm::vec4 moonDirection{},moonRadiance{},moonDisk{};
        glm::vec4 horizon{},middle{},zenith{},art{},camera{},volume{},fog{},fogNoise{};
        glm::vec4 cloudLayer{},cloudShape{},cloudOffset{},shadowOrigin{};
        glm::ivec4 shadow{},quality{},samples{},control{};
        glm::uvec4 misc{};
        glm::vec4 cloudWeather{},cloudProfiles{};
        glm::mat4 invView{1},invProjection{1},view{1};
    };
    static_assert(sizeof(AtmosphereGpuParameters)==672,"Atmosphere std140 layout drift");
    static_assert(offsetof(AtmosphereGpuParameters,invView)==480,"Atmosphere std140 matrix offset");

    struct Budget
    {
        int transWidth=256,transHeight=64,multiSize=32,skyWidth=256,skyHeight=128;
        int cube=128,irradiance=16,prefilter=128,filterSamples=96;
        int width=128,height=72,depth=80,noise=64,shadow=256;
        int skySteps=48,transSteps=64,multiDirections=24,multiSteps=16;
        int volumeSteps=1,cloudLightSteps=6,cloudShadowSteps=12,cloudSkySteps=28;
        int cloudViewWidth=256,cloudViewHeight=144;
        bool operator==(const Budget& b) const { return std::memcmp(this,&b,sizeof(Budget))==0; }
        bool operator!=(const Budget& b) const { return !(*this==b); }
    };
    int overrideSize(int value,int fallback,int low,int high)
    {
        return std::clamp(value>0?value:fallback,low,high);
    }
    Budget budgetFor(const AtmosphereSettings& s)
    {
        Budget b;
        switch(s.quality)
        {
        case AtmosphereQuality::Low:
            b={128,32,16,128,64,64,8,32,32,64,36,48,32,128,16,32,8,8,1,2,4,12}; break;
        case AtmosphereQuality::Medium:
            b={256,64,24,192,96,128,16,64,64,96,54,64,48,192,32,48,16,12,1,4,8,20}; break;
        case AtmosphereQuality::Ultra:
            b={256,64,32,320,160,256,32,128,128,160,90,96,64,384,64,96,32,24,2,8,16,40}; break;
        case AtmosphereQuality::High: break;
        }
        b.cube=overrideSize(s.cubemapSize,b.cube,16,512);
        b.irradiance=overrideSize(s.irradianceSize,b.irradiance,4,64);
        b.prefilter=overrideSize(s.prefilterSize,b.prefilter,16,256);
        b.filterSamples=overrideSize(s.iblSamples,b.filterSamples,8,1024);
        b.skySteps=overrideSize(s.skySteps,b.skySteps,8,128);
        b.cloudLightSteps=overrideSize(s.cloudLightSteps,b.cloudLightSteps,1,32);
        b.width=overrideSize(s.volumeWidth,b.width,16,320);
        b.height=overrideSize(s.volumeHeight,b.height,9,180);
        b.depth=overrideSize(s.volumeDepth,b.depth,16,192);
        b.cloudViewWidth=overrideSize(s.cloudViewWidth,2*b.width,16,1280);
        b.cloudViewHeight=overrideSize(s.cloudViewHeight,2*b.height,9,720);
        return b;
    }
    int mipCount(int size) { return 1+static_cast<int>(std::floor(std::log2(std::max(size,1)))); }
    void textureUnit(GLuint unit,GLuint texture) { glBindTextureUnit(unit,texture); glBindSampler(unit,0); }
    void textureImage(GLuint unit,GLuint texture,GLenum format,int level=0,bool layered=true)
    {
        glBindImageTexture(unit,texture,level,layered?GL_TRUE:GL_FALSE,0,GL_WRITE_ONLY,format);
    }
    GLuint makeTexture(GLenum target,GLenum format,int width,int height,int depth=1,int levels=1)
    {
        GLuint t=0;
        glCreateTextures(target,1,&t);
        if(target==GL_TEXTURE_3D) glTextureStorage3D(t,levels,format,width,height,depth);
        else glTextureStorage2D(t,levels,format,width,height);
        glTextureParameteri(t,GL_TEXTURE_MIN_FILTER,levels>1?GL_LINEAR_MIPMAP_LINEAR:GL_LINEAR);
        glTextureParameteri(t,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        glTextureParameteri(t,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
        glTextureParameteri(t,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        glTextureParameteri(t,GL_TEXTURE_WRAP_R,GL_CLAMP_TO_EDGE);
        glTextureParameteri(t,GL_TEXTURE_BASE_LEVEL,0);
        glTextureParameteri(t,GL_TEXTURE_MAX_LEVEL,levels-1);
        // Initialize every level, including levels not sampled until later.
        const float black[4]={0,0,0,0};
        for(int level=0;level<levels;++level)
            glClearTexImage(t,level,(format==GL_R32F || format==GL_R16F)?GL_RED:
                ((format==GL_RG16F || format==GL_RG32F)?GL_RG:GL_RGBA),GL_FLOAT,black);
        return t;
    }
    void deleteTexture(GLuint& texture) { if(texture) glDeleteTextures(1,&texture); texture=0; }
    void dispatch(int x,int y,int z,int gx=8,int gy=8,int gz=1)
    {
        glDispatchCompute((x+gx-1)/gx,(y+gy-1)/gy,(z+gz-1)/gz);
        glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT|GL_TEXTURE_FETCH_BARRIER_BIT|GL_TEXTURE_UPDATE_BARRIER_BIT);
    }
    GLint location(GLuint program,const char* name) { return glGetUniformLocation(program,name); }
    void stripVersion(std::string& text)
    {
        const auto start=text.find("#version");
        if(start!=std::string::npos) text.erase(start,text.find('\n',start)-start+1);
    }

    enum Program { Transmittance,MultipleScattering,SkyView,Aerial,CloudVolume,Volume,
        CloudNoise,CloudShadow,VisibleClouds,Environment,Filter,Blend,Brdf,SkyDraw,ProgramCount };
    enum Timing { SkyTime,LutTime,IblTime,AerialTime,FogTime,CloudTime,TimingCount };
    enum WorkKind { SnapshotWork,CaptureSkyWork,CaptureTileWork,MipsWork,DiffuseTileWork,GgxTileWork,PublishWork,WorkKindCount };
    struct WorkItem { WorkKind kind; int x=0,y=0,face=0,level=0,size=0; };
    struct EnvironmentSet
    {
        GLuint sky=0,lighting=0,irradiance=0,prefilter=0;
        int cubeSize=0,irradianceSize=0,prefilterSize=0;
    };
    struct TimerEntry
    {
        GLuint query=0; bool pending=false; unsigned kind=0; std::uint64_t frame=0;
        int work=-1; double scale=1;
    };
    struct EnvironmentCapture
    {
        bool active=false;
        Budget budget;
        AtmosphereGpuParameters parameters{};
        AtmospherePalette palette;
        EnvironmentSet output;
        GLuint uniforms=0,transmittance=0,multiple=0,sky=0,noise=0,shadow=0,lighting=0,external=0;
        std::uint64_t revision=0;
        int tile=32;
        unsigned cursor=0;
        std::vector<WorkItem> work;
    };
    struct TextureAllocation
    {
        GLenum target=0,format=0;
        int width=0,height=0,depth=0,levels=0;
        std::uint64_t bytes=0;
    };
}

struct Atmosphere::Impl
{
    const ptrClass* pc=nullptr;
    GraphicsDataMisc* graphics=nullptr;
    AtmosphereSettings config;
    AtmosphereStats statistics;
    AtmosphereTransition transition;
    std::string failure,gameFogPath,radiancePath;
    std::function<void(unsigned)> bindGameFog;
    Lights* requestedSun=nullptr;
    Lights* requestedMoon=nullptr;
    Lights* sun=nullptr;
    Lights* moon=nullptr;
    std::vector<std::pair<Lights*,float>> managedShadowMargins;
    GLuint externalEnvironment=0;
    std::uint64_t externalRevision=0;
    bool dirty=true,programsDirty=true,ready=false,physicalPrograms=false;
    bool physicalDirty=true,multipleDirty=true,skyDirty=true,noiseDirty=true,volumesReady=false;
    bool hasEnvironment=false,hasDisplayedPalette=false,environmentBlending=false,needsCapture=true;
    bool liveSkyReady=false,visibleCloudValid=false;
    bool allocatedPhysical=false,allocatedClouds=false,allocatedMedia=false;
    unsigned frame=0;
    double time=0.0,environmentElapsed=0.0,blendElapsed=1.0;
    glm::dvec2 cloudDisplacement{0.0};
    float blendDuration=0.0f,lastSkyAltitude=std::numeric_limits<float>::infinity();
    AtmospherePalette lastBakedPalette, lastSkyPalette, lastRequestedPalette, displayedPalette, fromPalette, targetPalette;
    glm::vec3 lastBakedCamera{0};
    Budget budget;
    AtmosphereGpuParameters gpu{},previous{},lastVolumeParameters{},lastAerialParameters{},lastCloudVolumeParameters{};
    AtmosphereGpuParameters lastCloudShadowParameters{},lastVisibleCloudParameters{};
    std::uint64_t lastVolumeShadowRevision=0;
    bool cloudShadowValid=false;
    std::array<GLuint,ProgramCount> programs{};
    GLuint parameters=0,vao=0;
    GLuint transmittance=0,multiple=0,skyView=0,noise=0,cloudShadow=0,visibleClouds=0;
    GLuint physicalSource=0,physicalExtinction=0,cloudSource=0,cloudExtinction=0;
    GLuint scattering=0,transmission=0,baselineScattering=0,baselineTransmission=0;
    GLuint brdf=0;
    // 0 snapshot of current display, 1 completed target, 2 coherent displayed set.
    std::array<EnvironmentSet,3> environments{};
    EnvironmentCapture capture;
    std::unordered_map<GLuint,TextureAllocation> textureSizes;
    std::array<TimerEntry,64> timers{};
    std::array<std::uint64_t,TimingCount> lastTimingFrame{};
    std::array<double,TimingCount> lastTimingValue{};
    int activeTimer=-1;
    unsigned timerCursor=0;
    std::array<double,WorkKindCount> workMilliseconds{{.10,.60,.16,.08,.12,.14,.20}};
    double foregroundMilliseconds=0.0;
    std::uint64_t foregroundFrame=0;

    GLuint compile(const std::string& file,GLenum stage,const std::string& defines={},
        const std::string& vertex={})
    {
        const std::string path="../Asset/Shader/Atmosphere/"+file;
        const bool addFog=file=="volume.C.glsl" && !gameFogPath.empty();
        const bool addRadiance=!radiancePath.empty();
        std::string actual=defines;
        if(addFog) actual+="\n#define GE_GAME_FOG 1\n";
        if(addRadiance) actual+="\n#define GE_CUSTOM_RADIANCE 1\n";
        std::vector<ShaderFile> stages{{stage,path,actual}};
        if(!vertex.empty()) stages.push_back({GL_VERTEX_SHADER,"../Asset/Shader/Atmosphere/"+vertex,{}});
        if(!addFog && !addRadiance)
            return buildShaderProgram(stages,failure,"Atmosphere "+file,ShaderCacheOptions{true,{}});
        // Runtime game hooks are compiled into the same source snapshot. They
        // may include arbitrary game-owned textures; engine code knows no biome.
        GLuint program=glCreateProgram();
        std::vector<GLuint> shaders;
        bool okay=true;
        for(const auto& entry:stages)
        {
            ShaderSource source;
            if(!loadShaderSource(entry.filename,entry.defines,source,failure,"450 core")) { okay=false; break; }
            for(const std::string& hook:{addFog?gameFogPath:std::string{},addRadiance?radiancePath:std::string{}})
            {
                if(hook.empty() || entry.stage==GL_VERTEX_SHADER) continue;
                ShaderSource extra;
                if(!loadShaderSource(hook,{},extra,failure,"450 core")) { okay=false; break; }
                stripVersion(extra.text);
                source.text+="\n"+extra.text;
            }
            if(!okay) break;
            GLuint shader=glCreateShader(entry.stage);
            shaders.push_back(shader);
            const char* sourcePointer=source.text.c_str();
            glShaderSource(shader,1,&sourcePointer,nullptr);
            glCompileShader(shader);
            GLint success=0; glGetShaderiv(shader,GL_COMPILE_STATUS,&success);
            if(!success)
            {
                GLint size=0; glGetShaderiv(shader,GL_INFO_LOG_LENGTH,&size);
                std::string log(static_cast<std::size_t>(std::max(size,1)),0);
                glGetShaderInfoLog(shader,size,nullptr,log.data());
                failure=file+": "+log; okay=false; break;
            }
            glAttachShader(program,shader);
        }
        if(okay)
        {
            glLinkProgram(program);
            GLint success=0; glGetProgramiv(program,GL_LINK_STATUS,&success);
            if(!success)
            {
                GLint size=0; glGetProgramiv(program,GL_INFO_LOG_LENGTH,&size);
                std::string log(static_cast<std::size_t>(std::max(size,1)),0);
                glGetProgramInfoLog(program,size,nullptr,log.data());
                failure=file+": "+log; okay=false;
            }
        }
        for(GLuint shader:shaders) glDeleteShader(shader);
        if(!okay) { glDeleteProgram(program); return 0; }
        return program;
    }

    void releasePrograms()
    {
        for(auto& program:programs) { if(program) glDeleteProgram(program); program=0; }
    }
    bool buildPrograms()
    {
        std::array<GLuint,ProgramCount> next{};
        std::string errors;
        const auto build=[&](const std::string& file,GLenum stage,const std::string& defines=std::string{},const std::string& vertex=std::string{})
        {
            const GLuint result=compile(file,stage,defines,vertex);
            if(!result) errors+=file+": "+failure+"\n";
            return result;
        };
        // A source-only stage-2 checkout remains a functional gradient sky.
        // Adding the physical stages enables them without changing the game API.
        bool physical=std::filesystem::exists("../Asset/Shader/Atmosphere/volume.C.glsl");
        if(physical)
        {
            const std::array<std::pair<Program,const char*>,9> jobs{{
                {Transmittance,"transmittance.C.glsl"},{MultipleScattering,"multiscatter.C.glsl"},
                {SkyView,"skyview.C.glsl"},{Aerial,"aerial.C.glsl"},{CloudVolume,"cloud_volume.C.glsl"},
                {Volume,"volume.C.glsl"},{CloudNoise,"cloud_noise.C.glsl"},{CloudShadow,"cloud_shadow.C.glsl"},
                {VisibleClouds,"visible_clouds.C.glsl"}}};
            for(const auto& job:jobs) next[job.first]=build(job.second,GL_COMPUTE_SHADER);
        }
        next[Environment]=build("environment.C.glsl",GL_COMPUTE_SHADER,physical?"#define GE_PHYSICAL_SKY 1":"");
        next[Filter]=build("filter.C.glsl",GL_COMPUTE_SHADER);
        next[Blend]=build("blend.C.glsl",GL_COMPUTE_SHADER);
        next[Brdf]=build("brdf.C.glsl",GL_COMPUTE_SHADER);
        next[SkyDraw]=build("sky.fs.glsl",GL_FRAGMENT_SHADER,physical?"#define GE_PHYSICAL_SKY 1":"","sky.vs.glsl");
        bool success=true;
        for(std::size_t i=physical?0:Environment;i<next.size();++i) success=success && next[i]!=0;
        if(!success)
        {
            for(GLuint p:next) if(p) glDeleteProgram(p);
            failure=errors;
            return false;
        }
        releasePrograms(); programs=next; physicalPrograms=physical; failure.clear();
        programsDirty=false; volumesReady=false; cloudShadowValid=false; visibleCloudValid=false;
        physicalDirty=multipleDirty=skyDirty=noiseDirty=dirty=true;
        return true;
    }

    bool cloudsEnabled() const
    { return config.clouds && config.cloudCoverage>0.0f && config.cloudDensity>0.0f; }
    const AtmospherePalette& capturePalette() const
    {
        // Until the first complete IBL is ready, the live sky and every
        // restarted capture use the same visible palette. Pending targets
        // remain in transition.accepted(), without advancing out of view.
        return hasEnvironment?transition.displayed():displayedPalette;
    }
    bool mediumEnabled() const
    { return config.physicalAtmosphere || cloudsEnabled() || (config.localFog && (config.fogDensity>0 || !gameFogPath.empty())); }
    GLuint createTexture(GLenum target,GLenum format,int width,int height,int depth=1,int levels=1)
    {
        const GLuint texture=makeTexture(target,format,width,height,depth,levels);
        const int bytes=format==GL_RGBA32F?16:(format==GL_RGB32F?12:(format==GL_RGB16F?6:
            (format==GL_R16F?2:(format==GL_RG16F || format==GL_R32F || format==GL_R11F_G11F_B10F || format==GL_RGBA8?4:8))));
        std::uint64_t size=0;
        for(int level=0;level<levels;++level)
            size+=std::uint64_t(std::max(1,width>>level))*std::max(1,height>>level)*
                (target==GL_TEXTURE_CUBE_MAP?6:(target==GL_TEXTURE_3D?std::max(1,depth>>level):1))*bytes;
        textureSizes[texture]={target,format,width,height,depth,levels,size}; statistics.gpuTextureBytes+=size;
        return texture;
    }
    void destroyTexture(GLuint& texture)
    {
        const auto found=textureSizes.find(texture);
        if(found!=textureSizes.end()) { statistics.gpuTextureBytes-=found->second.bytes; textureSizes.erase(found); }
        deleteTexture(texture);
    }
    void ensureTexture(GLuint& texture,GLenum target,GLenum format,int width,int height,int depth=1,int levels=1)
    {
        const auto found=textureSizes.find(texture);
        if(found!=textureSizes.end())
        {
            const auto& old=found->second;
            if(old.target==target && old.format==format && old.width==width && old.height==height &&
                old.depth==depth && old.levels==levels) return;
        }
        const GLuint replacement=createTexture(target,format,width,height,depth,levels);
        destroyTexture(texture); texture=replacement;
    }
    void releaseSet(EnvironmentSet& set)
    {
        destroyTexture(set.sky); destroyTexture(set.lighting);
        destroyTexture(set.irradiance); destroyTexture(set.prefilter); set={};
    }
    bool matchingSet(const EnvironmentSet& set,const Budget& b) const
    { return set.sky && set.cubeSize==b.cube && set.irradianceSize==b.irradiance && set.prefilterSize==b.prefilter; }
    void allocateSet(EnvironmentSet& set,const Budget& b)
    {
        if(matchingSet(set,b)) return;
        releaseSet(set);
        set.cubeSize=b.cube; set.irradianceSize=b.irradiance; set.prefilterSize=b.prefilter;
        set.sky=createTexture(GL_TEXTURE_CUBE_MAP,GL_RGBA16F,b.cube,b.cube);
        set.lighting=createTexture(GL_TEXTURE_CUBE_MAP,GL_RGBA16F,b.cube,b.cube);
        set.irradiance=createTexture(GL_TEXTURE_CUBE_MAP,GL_RGBA16F,b.irradiance,b.irradiance);
        set.prefilter=createTexture(GL_TEXTURE_CUBE_MAP,GL_RGBA16F,b.prefilter,b.prefilter,1,std::min(5,mipCount(b.prefilter)));
    }
    void captureMemory()
    {
        statistics.captureTextureBytes=0;
        for(GLuint texture:{capture.output.sky,capture.output.lighting,capture.output.irradiance,capture.output.prefilter,
                            capture.transmittance,capture.multiple,capture.sky,capture.noise,capture.shadow,capture.lighting,capture.external})
        {
            const auto found=textureSizes.find(texture);
            if(found!=textureSizes.end()) statistics.captureTextureBytes+=found->second.bytes;
        }
    }
    void cancelCapture()
    {
        if(capture.active) ++statistics.environmentCapturesCancelled;
        capture.active=false; capture.work.clear(); capture.cursor=0;
        statistics.environmentCaptureActive=false;
        statistics.captureWorkCompleted=statistics.captureWorkTotal=0; statistics.captureProgress=0;
        needsCapture=true;
    }
    void releaseCapture()
    {
        cancelCapture(); releaseSet(capture.output);
        for(GLuint* t:{&capture.transmittance,&capture.multiple,&capture.sky,&capture.noise,
                       &capture.shadow,&capture.lighting,&capture.external}) destroyTexture(*t);
        if(capture.uniforms) glDeleteBuffers(1,&capture.uniforms);
        capture.uniforms=0; captureMemory();
    }
    void releaseLive()
    {
        for(GLuint* t:{&transmittance,&multiple,&skyView,&noise,&cloudShadow,&visibleClouds,
                      &physicalSource,&physicalExtinction,&cloudSource,&cloudExtinction,
                      &scattering,&transmission,&baselineScattering,&baselineTransmission}) destroyTexture(*t);
        liveSkyReady=volumesReady=cloudShadowValid=visibleCloudValid=false;
    }
    void releaseTargets()
    {
        releaseCapture(); releaseLive();
        for(auto& set:environments) releaseSet(set);
        hasEnvironment=false; environmentBlending=false; statistics.environmentReady=false;
    }
    void updateEffectResources(bool force=false)
    {
        const bool physical=config.physicalAtmosphere,clouds=cloudsEnabled(),media=mediumEnabled();
        const auto& b=budget;
        const float white[4]={1,1,1,1};
        if(force || physical!=allocatedPhysical)
        {
            for(GLuint* t:{&transmittance,&multiple,&physicalSource,&physicalExtinction}) destroyTexture(*t);
            transmittance=createTexture(GL_TEXTURE_2D,GL_RGBA16F,physical?b.transWidth:1,physical?b.transHeight:1);
            multiple=createTexture(GL_TEXTURE_2D,GL_RGBA16F,physical?b.multiSize:1,physical?b.multiSize:1);
            physicalSource=createTexture(GL_TEXTURE_3D,GL_RGBA32F,physical?b.width:1,physical?b.height:1,physical?b.depth:1);
            physicalExtinction=createTexture(GL_TEXTURE_3D,GL_RGBA32F,physical?b.width:1,physical?b.height:1,physical?b.depth:1);
            glClearTexImage(transmittance,0,GL_RGBA,GL_FLOAT,white);
            physicalDirty=multipleDirty=skyDirty=dirty=true; volumesReady=false;
        }
        if(force || clouds!=allocatedClouds)
        {
            for(GLuint* t:{&noise,&cloudShadow,&visibleClouds,&cloudSource,&cloudExtinction}) destroyTexture(*t);
            const int n=clouds?b.noise:1;
            noise=createTexture(GL_TEXTURE_3D,GL_RGBA16F,n,n,n,mipCount(n));
            for(GLenum parameter:{GL_TEXTURE_WRAP_S,GL_TEXTURE_WRAP_T,GL_TEXTURE_WRAP_R})
                glTextureParameteri(noise,parameter,GL_REPEAT);
            cloudShadow=createTexture(GL_TEXTURE_2D,GL_RG16F,clouds?b.shadow:1,clouds?b.shadow:1);
            glClearTexImage(cloudShadow,0,GL_RG,GL_FLOAT,white);
            visibleClouds=createTexture(GL_TEXTURE_2D,GL_RGBA16F,clouds?b.cloudViewWidth:1,clouds?b.cloudViewHeight:1);
            cloudSource=createTexture(GL_TEXTURE_3D,GL_RGBA32F,clouds?b.width:1,clouds?b.height:1,clouds?b.depth:1);
            cloudExtinction=createTexture(GL_TEXTURE_3D,GL_R32F,clouds?b.width:1,clouds?b.height:1,clouds?b.depth:1);
            noiseDirty=clouds; dirty=true; volumesReady=false; cloudShadowValid=visibleCloudValid=false;
        }
        if(force || media!=allocatedMedia)
        {
            for(GLuint* t:{&scattering,&transmission,&baselineScattering,&baselineTransmission}) destroyTexture(*t);
            scattering=createTexture(GL_TEXTURE_3D,GL_RGBA16F,media?b.width:1,media?b.height:1,media?b.depth:1);
            transmission=createTexture(GL_TEXTURE_3D,GL_RGBA16F,media?b.width:1,media?b.height:1,media?b.depth:1);
            baselineScattering=createTexture(GL_TEXTURE_2D,GL_RGBA16F,media?b.width:1,media?b.height:1);
            baselineTransmission=createTexture(GL_TEXTURE_2D,GL_RGBA16F,media?b.width:1,media?b.height:1);
            glClearTexImage(transmission,0,GL_RGBA,GL_FLOAT,white);
            glClearTexImage(baselineTransmission,0,GL_RGBA,GL_FLOAT,white);
            volumesReady=false;
        }
        allocatedPhysical=physical; allocatedClouds=clouds; allocatedMedia=media;
    }
    bool allocate(const Budget& b)
    {
        // In-flight work is disposable; all three published sets stay alive.
        // A quality change is exposed only when its new capture is complete.
        releaseCapture(); releaseLive(); budget=b;
        skyView=createTexture(GL_TEXTURE_2D,GL_RGBA16F,b.skyWidth,b.skyHeight);
        glTextureParameteri(skyView,GL_TEXTURE_WRAP_S,GL_REPEAT);
        updateEffectResources(true);
        if(!parameters) glCreateBuffers(1,&parameters);
        glNamedBufferData(parameters,sizeof(gpu),&gpu,GL_DYNAMIC_DRAW);
        if(!vao) glCreateVertexArrays(1,&vao);
        if(!brdf)
        {
            brdf=createTexture(GL_TEXTURE_2D,GL_RG16F,128,128);
            glUseProgram(programs[Brdf]); textureImage(0,brdf,GL_RG16F,0,false); submit(128,128,1);
        }
        glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);
        physicalDirty=multipleDirty=skyDirty=dirty=true;
        return true;
    }
    void submit(int x,int y,int z,int gx=8,int gy=8,int gz=1)
    {
        dispatch(x,y,z,gx,gy,gz); ++statistics.dispatches; ++statistics.frameDispatches;
    }

    void upload()
    {
        glNamedBufferSubData(parameters,0,sizeof(gpu),&gpu);
        glBindBufferBase(GL_UNIFORM_BUFFER,6,parameters);
    }
    void commonTextures()
    {
        textureUnit(0,transmittance); textureUnit(1,multiple); textureUnit(2,skyView);
        textureUnit(3,noise); textureUnit(5,cloudShadow);
    }
    void begin(Timing kind,int work=-1,double scale=1.0)
    {
        if(!config.measureGpu || activeTimer!=-1) return;
        for(unsigned i=0;i<timers.size();++i)
        {
            unsigned index=(timerCursor+i)%timers.size();
            auto& entry=timers[index];
            if(entry.pending) continue;
            if(!entry.query) glGenQueries(1,&entry.query);
            entry.kind=kind; entry.frame=frame; entry.work=work; entry.scale=scale;
            glBeginQuery(GL_TIME_ELAPSED,entry.query); activeTimer=static_cast<int>(index);
            timerCursor=(index+1)%timers.size(); return;
        }
        ++statistics.droppedTimingSamples;
    }
    void end()
    {
        if(activeTimer<0) return;
        glEndQuery(GL_TIME_ELAPSED);
        timers[static_cast<std::size_t>(activeTimer)].pending=true; activeTimer=-1;
    }
    void pollTimers()
    {
        for(auto& entry:timers)
        {
            if(!entry.pending) continue;
            GLint available=0;
            glGetQueryObjectiv(entry.query,GL_QUERY_RESULT_AVAILABLE,&available);
            if(!available) continue;
            GLuint64 ns=0; glGetQueryObjectui64v(entry.query,GL_QUERY_RESULT,&ns);
            entry.pending=false;
            const double milliseconds=static_cast<double>(ns)*1e-6;
            if(entry.work>=0)
            {
                auto& estimate=workMilliseconds[static_cast<std::size_t>(entry.work)];
                estimate=0.7*estimate+0.3*std::clamp(milliseconds/std::max(entry.scale,.0001),.001,1000.0);
            }
            else if(entry.frame>=foregroundFrame)
            {
                if(entry.frame!=foregroundFrame) { foregroundMilliseconds=0; foregroundFrame=entry.frame; }
                foregroundMilliseconds+=milliseconds;
            }
            if(entry.frame<lastTimingFrame[entry.kind]) continue;
            if(entry.frame!=lastTimingFrame[entry.kind])
            { lastTimingValue[entry.kind]=0; lastTimingFrame[entry.kind]=entry.frame; }
            lastTimingValue[entry.kind]+=milliseconds;
            statistics.gpuSamplesAvailable=true;
        }
        statistics.skyMilliseconds=lastTimingValue[SkyTime];
        statistics.lutMilliseconds=lastTimingValue[LutTime];
        statistics.iblMilliseconds=lastTimingValue[IblTime];
        statistics.aerialMilliseconds=lastTimingValue[AerialTime];
        statistics.fogMilliseconds=lastTimingValue[FogTime];
        statistics.cloudMilliseconds=lastTimingValue[CloudTime];
    }

    Lights* validateLight(Lights* value) const
    {
        const auto& lights=pc->lightManager->getLights();
        if(!value || std::find(lights.begin(),lights.end(),value)==lights.end()) return nullptr;
        return value->isActive() && value->getStatus()==0 ? value : nullptr;
    }
    void lightParameters(Lights* light,glm::vec4& direction,glm::vec4& radiance,glm::vec4& disk,
        float illumination,const glm::vec3& color,float intensity,float radius)
    {
        direction=glm::vec4(0,1,0,0); radiance=glm::vec4(0);
        disk=glm::vec4(glm::max(color,glm::vec3(0))*std::max(0.0f,intensity),
            glm::radians(std::clamp(radius,0.001f,10.0f)));
        if(!light) return;
        glm::vec3 d=-light->getDirection();
        if(glm::dot(d,d)<1e-10f) return;
        direction=glm::vec4(glm::normalize(d),1.0f);
        radiance=glm::vec4(glm::max(light->getColors(),glm::vec3(0))
            *std::max(0.0f,light->getRange()/10.0f)*std::max(0.0f,illumination),0);
    }
    void fillParameters(Camera* camera)
    {
        sun=validateLight(requestedSun); moon=validateLight(requestedMoon);
        if(!sun && config.fallbackToFirstDirectional)
            for(auto* candidate:pc->lightManager->getLights())
                if(candidate!=moon && validateLight(candidate)) { sun=candidate; break; }
        if(moon==sun) moon=nullptr;
        statistics.hasSun=sun!=nullptr; statistics.hasMoon=moon!=nullptr;
        const float bottom=std::max(1000.0f,config.planetRadius);
        gpu.radii={bottom,bottom+std::max(100.0f,config.atmosphereHeight),config.seaLevel,0};
        gpu.rayleigh={glm::max(config.rayleighScattering,glm::vec3(0)),std::max(1.0f,config.rayleighScaleHeight)};
        gpu.mie={glm::vec3(std::max(0.0f,config.mieScattering)),std::max(1.0f,config.mieScaleHeight)};
        gpu.absorption={glm::max(config.ozoneAbsorption,glm::vec3(0)),config.ozoneCenterHeight};
        gpu.medium={std::max(0.0f,config.mieAbsorption),std::clamp(config.mieAnisotropy,-0.95f,0.95f),
            std::max(1.0f,config.ozoneHalfWidth),1};
        lightParameters(sun,gpu.sunDirection,gpu.sunRadiance,gpu.sunDisk,config.sunIlluminanceScale,
            config.sunDiskColor,config.sunDiskIntensity,config.sunAngularRadius);
        lightParameters(moon,gpu.moonDirection,gpu.moonRadiance,gpu.moonDisk,config.moonIlluminanceScale,
            config.moonDiskColor,config.moonDiskIntensity,config.moonAngularRadius);
        const auto& palette=capturePalette();
        gpu.horizon={palette.horizon,palette.strength};
        gpu.middle={palette.middle,palette.middleElevation};
        gpu.zenith={palette.zenith,palette.gradientPower};
        gpu.art={palette.exposure,std::max(config.skySaturation,0.0f),std::max(config.iblSaturation,0.0f),
            glm::dot(glm::clamp(config.groundAlbedo,0.0f,1.0f),glm::vec3(.2126f,.7152f,.0722f))};
        gpu.camera={camera->getPosition(),static_cast<float>(time)};
        gpu.volume={std::max(100.0f,config.aerialDistance),std::clamp(config.fogAnisotropy,-.95f,.95f),
            std::max(config.fogDensity,0.0f),config.fogBaseHeight};
        gpu.fog={glm::clamp(config.fogAlbedo,0.0f,1.0f),std::max(1.0f,config.fogScaleHeight)};
        gpu.fogNoise={250,0.65f,config.wind.x,config.wind.y};
        gpu.cloudLayer={config.cloudBaseHeight,std::max(1.0f,config.cloudThickness),
            std::clamp(config.cloudCoverage,0.0f,1.0f),std::max(0.0f,config.cloudDensity)};
        gpu.cloudShape={std::max(100.0f,config.cloudScale),1.0f/std::max(.01f,config.cloudDetailScale),.28f,.65f};
        gpu.cloudOffset={float(cloudDisplacement.x),float(cloudDisplacement.y),std::max(config.aerialDistance,1024.0f),0};
        gpu.cloudWeather={std::max(100.0f,config.cloudWeatherScale),std::clamp(config.cloudWeatherStrength,0.0f,1.0f),
            std::clamp(config.cloudWeatherContrast,.05f,8.0f),std::clamp(config.cloudProfileVariation,0.0f,1.0f)};
        glm::vec3 profiles=glm::max(config.cloudProfileMix,glm::vec3(0));
        const float total=profiles.x+profiles.y+profiles.z;
        profiles=total>1e-6f?profiles/total:glm::vec3(.25f,.5f,.25f);
        gpu.cloudProfiles={profiles,0};
        const float shadowTexel=2.0f*gpu.cloudOffset.z/static_cast<float>(budget.shadow);
        gpu.shadowOrigin={std::floor(gpu.camera.x/shadowTexel)*shadowTexel,
            std::floor(gpu.camera.z/shadowTexel)*shadowTexel,0,0};
        gpu.shadow={sun?sun->getShadowIndex():-1,moon?moon->getShadowIndex():-1,0,0};
        gpu.quality={budget.skySteps,budget.transSteps,budget.multiDirections,budget.multiSteps};
        gpu.samples={budget.volumeSteps,budget.cloudLightSteps,budget.cloudShadowSteps,budget.cloudSkySteps};
        gpu.control={config.physicalAtmosphere?1:0,config.localFog?1:0,cloudsEnabled()?1:0,config.godRays?1:0};
        gpu.misc={config.cloudSeed,frame,0,0};
        gpu.view=camera->getViewMatrix(); gpu.invView=glm::inverse(gpu.view);
        gpu.invProjection=glm::inverse(camera->getProjectionMatrix());
        statistics.shadowDistance=camera->getFar();
    }
    void copySet(const EnvironmentSet& from,const EnvironmentSet& to)
    {
        for(const auto& pair:{std::pair<GLuint,GLuint>{from.sky,to.sky},
            {from.lighting,to.lighting},{from.irradiance,to.irradiance},{from.prefilter,to.prefilter}})
        {
            const int size=(pair.first==from.sky || pair.first==from.lighting)?from.cubeSize:
                (pair.first==from.irradiance?from.irradianceSize:from.prefilterSize);
            const int levels=pair.first==from.prefilter?std::min(5,mipCount(size)):1;
            for(int level=0;level<levels;++level)
                glCopyImageSubData(pair.first,GL_TEXTURE_CUBE_MAP,level,0,0,0,
                    pair.second,GL_TEXTURE_CUBE_MAP,level,0,0,0,std::max(1,size>>level),std::max(1,size>>level),6);
        }
        glMemoryBarrier(GL_TEXTURE_UPDATE_BARRIER_BIT|GL_TEXTURE_FETCH_BARRIER_BIT);
    }
    void snapshotInputs()
    {
        // One ordered GPU snapshot freezes every texture the multi-frame job
        // can read. The live LUTs/noise may immediately change afterwards.
        const auto& b=capture.budget;
        allocateSet(capture.output,b);
        const bool physical=capture.parameters.control.x!=0,clouds=capture.parameters.control.z!=0;
        const int tw=physical?b.transWidth:1,th=physical?b.transHeight:1,ms=physical?b.multiSize:1;
        const int n=clouds?b.noise:1,shadowSize=clouds?b.shadow:1;
        ensureTexture(capture.transmittance,GL_TEXTURE_2D,GL_RGBA16F,tw,th);
        ensureTexture(capture.multiple,GL_TEXTURE_2D,GL_RGBA16F,ms,ms);
        ensureTexture(capture.sky,GL_TEXTURE_2D,GL_RGBA16F,b.skyWidth,b.skyHeight);
        glTextureParameteri(capture.sky,GL_TEXTURE_WRAP_S,GL_REPEAT);
        ensureTexture(capture.noise,GL_TEXTURE_3D,GL_RGBA16F,n,n,n,mipCount(n));
        for(GLenum parameter:{GL_TEXTURE_WRAP_S,GL_TEXTURE_WRAP_T,GL_TEXTURE_WRAP_R})
            glTextureParameteri(capture.noise,parameter,GL_REPEAT);
        ensureTexture(capture.shadow,GL_TEXTURE_2D,GL_RG16F,shadowSize,shadowSize);
        ensureTexture(capture.lighting,GL_TEXTURE_CUBE_MAP,GL_RGBA16F,b.cube,b.cube,1,mipCount(b.cube));
        glCopyImageSubData(transmittance,GL_TEXTURE_2D,0,0,0,0,capture.transmittance,GL_TEXTURE_2D,0,0,0,0,tw,th,1);
        glCopyImageSubData(multiple,GL_TEXTURE_2D,0,0,0,0,capture.multiple,GL_TEXTURE_2D,0,0,0,0,ms,ms,1);
        glCopyImageSubData(cloudShadow,GL_TEXTURE_2D,0,0,0,0,capture.shadow,GL_TEXTURE_2D,0,0,0,0,shadowSize,shadowSize,1);
        for(int level=0;level<mipCount(n);++level)
        {
            const int size=std::max(1,n>>level);
            glCopyImageSubData(noise,GL_TEXTURE_3D,level,0,0,0,capture.noise,GL_TEXTURE_3D,level,0,0,0,size,size,size);
        }
        if(externalEnvironment && glIsTexture(externalEnvironment))
        {
            // These queries inspect texture metadata, never GPU image contents.
            GLint size=0,format=GL_RGBA16F;
            glGetTextureLevelParameteriv(externalEnvironment,0,GL_TEXTURE_WIDTH,&size);
            glGetTextureLevelParameteriv(externalEnvironment,0,GL_TEXTURE_INTERNAL_FORMAT,&format);
            if(size>0)
            {
                ensureTexture(capture.external,GL_TEXTURE_CUBE_MAP,static_cast<GLenum>(format),size,size);
                glCopyImageSubData(externalEnvironment,GL_TEXTURE_CUBE_MAP,0,0,0,0,
                    capture.external,GL_TEXTURE_CUBE_MAP,0,0,0,0,size,size,6);
            }
        }
        else destroyTexture(capture.external);
        if(!capture.uniforms) glCreateBuffers(1,&capture.uniforms);
        glNamedBufferData(capture.uniforms,sizeof(capture.parameters),&capture.parameters,GL_STATIC_DRAW);
        glMemoryBarrier(GL_TEXTURE_UPDATE_BARRIER_BIT|GL_TEXTURE_FETCH_BARRIER_BIT);
        captureMemory();
    }
    void captureTextures()
    {
        glBindBufferBase(GL_UNIFORM_BUFFER,6,capture.uniforms);
        textureUnit(0,capture.transmittance); textureUnit(1,capture.multiple); textureUnit(2,capture.sky);
        textureUnit(3,capture.noise); textureUnit(5,capture.shadow); textureUnit(9,capture.external);
    }
    void startCapture(const AtmosphereGpuParameters& candidate)
    {
        capture.active=true; capture.budget=budget; capture.parameters=candidate;
        capture.palette=capturePalette(); capture.revision=statistics.requestedEnvironmentRevision;
        capture.tile=8*std::clamp(config.iblTileSize/8,1,8);
        capture.cursor=0; capture.work.clear();
        capture.work.push_back({SnapshotWork});
        if(physicalPrograms) capture.work.push_back({CaptureSkyWork});
        const auto tiles=[&](WorkKind kind,int size,int level=0)
        {
            for(int face=0;face<6;++face)
                for(int y=0;y<size;y+=capture.tile)
                    for(int x=0;x<size;x+=capture.tile) capture.work.push_back({kind,x,y,face,level,size});
        };
        tiles(CaptureTileWork,budget.cube);
        capture.work.push_back({MipsWork});
        tiles(DiffuseTileWork,budget.irradiance);
        for(int level=0;level<std::min(5,mipCount(budget.prefilter));++level)
            tiles(GgxTileWork,std::max(1,budget.prefilter>>level),level);
        capture.work.push_back({PublishWork});
        ++statistics.environmentCapturesStarted;
        statistics.captureRevision=capture.revision; statistics.environmentCaptureActive=true;
        statistics.captureWorkTotal=static_cast<unsigned>(capture.work.size());
        statistics.captureWorkCompleted=0; statistics.captureProgress=0;
        needsCapture=false; environmentElapsed=0;
        lastBakedCamera=glm::vec3(candidate.camera); lastBakedPalette=capture.palette;
    }
    void publishCapture()
    {
        // Move the completed work set into the target slot. An unfinished job
        // never aliases the source/target/display of an already running blend.
        fromPalette=displayedPalette; targetPalette=capture.palette;
        if(!hasEnvironment)
        {
            std::swap(environments[1],capture.output);
            allocateSet(environments[0],capture.budget); allocateSet(environments[2],capture.budget);
            copySet(environments[1],environments[0]); copySet(environments[1],environments[2]);
            hasEnvironment=true; environmentBlending=false;
            displayedPalette=fromPalette=targetPalette;
            // The first capture may span many frames and several climate
            // requests. Start their fade only now, from the still-visible
            // initial state, toward the most recent accepted target.
            const auto pending=transition.accepted();
            transition.target(pending,0,config.transitionSeconds,true,&displayedPalette);
        }
        else
        {
            // Preserve exactly the image already displayed, including an
            // interrupted fade. Old and new resolutions may differ: blend's
            // directional sampling performs the resolution conversion.
            std::swap(environments[0],environments[2]);
            allocateSet(environments[2],capture.budget);
            std::swap(environments[1],capture.output);
            blendElapsed=0; blendDuration=std::max(0.0f,config.environmentBlendSeconds);
            environmentBlending=true; blendEnvironment(false);
        }
        ++statistics.environmentUpdates;
        statistics.publishedEnvironmentRevision=capture.revision;
        statistics.environmentReady=true; statistics.environmentCaptureActive=false;
        capture.active=false; captureMemory();
    }
    double workScale(const WorkItem& item) const
    {
        const auto& b=capture.budget;
        if(item.kind==CaptureSkyWork)
            return std::max(.01,double(b.skyWidth)*b.skyHeight*b.skySteps/(320.0*160.0*64.0));
        if(item.kind==CaptureTileWork || item.kind==DiffuseTileWork || item.kind==GgxTileWork)
        {
            double scale=double(std::min(capture.tile,item.size-item.x))*std::min(capture.tile,item.size-item.y)/1024.0;
            if(item.kind==CaptureTileWork) scale*=std::max(.25,b.cloudSkySteps/40.0);
            else scale*=b.filterSamples/128.0;
            return std::max(.0001,scale);
        }
        return 1.0;
    }
    void executeWork(const WorkItem& item)
    {
        const auto& b=capture.budget;
        if(item.kind==SnapshotWork) { snapshotInputs(); return; }
        if(item.kind==PublishWork) { publishCapture(); return; }
        captureTextures();
        if(item.kind==CaptureSkyWork)
        {
            glUseProgram(programs[SkyView]); textureImage(0,capture.sky,GL_RGBA16F,0,false);
            submit(b.skyWidth,b.skyHeight,1); return;
        }
        if(item.kind==MipsWork)
        {
            // This ordinary mip chain is input footprint filtering. Every GGX
            // roughness level below is still computed explicitly.
            glGenerateTextureMipmap(capture.lighting);
            glCopyImageSubData(capture.lighting,GL_TEXTURE_CUBE_MAP,0,0,0,0,
                capture.output.lighting,GL_TEXTURE_CUBE_MAP,0,0,0,0,b.cube,b.cube,6);
            glMemoryBarrier(GL_TEXTURE_UPDATE_BARRIER_BIT|GL_TEXTURE_FETCH_BARRIER_BIT); return;
        }
        const bool sky=item.kind==CaptureTileWork;
        const GLuint program=programs[sky?Environment:Filter];
        glUseProgram(program);
        glUniform3i(location(program,"geDispatchOffset"),item.x,item.y,item.face);
        if(sky)
        {
            glUniform1i(location(program,"geUseExternalEnvironment"),capture.external?1:0);
            textureImage(0,capture.output.sky,GL_RGBA16F); textureImage(1,capture.lighting,GL_RGBA16F);
        }
        else
        {
            textureUnit(0,capture.lighting);
            const bool diffuse=item.kind==DiffuseTileWork;
            textureImage(0,diffuse?capture.output.irradiance:capture.output.prefilter,GL_RGBA16F,item.level);
            glUniform1i(location(program,"geFilterMode"),diffuse?0:1);
            glUniform1i(location(program,"geFilterSamples"),b.filterSamples);
            glUniform1f(location(program,"geFilterRoughness"),diffuse?1.0f:
                float(item.level)/float(std::min(5,mipCount(b.prefilter))-1));
        }
        submit(std::min(capture.tile,item.size-item.x),std::min(capture.tile,item.size-item.y),1);
    }
    void serviceEnvironment(const AtmosphereGpuParameters& candidate)
    {
        if(!capture.active && needsCapture && (!hasEnvironment || environmentElapsed>=std::max(0.0f,config.environmentInterval)))
            startCapture(candidate);
        if(!capture.active) return;
        const int limit=std::clamp(config.iblMaxWorkUnitsPerFrame,1,64);
        const double available=std::max(0.0,double(std::max(.01f,config.targetGpuMilliseconds))-foregroundMilliseconds);
        double submitted=0;
        while(capture.active && statistics.iblWorkUnitsThisFrame<static_cast<unsigned>(limit))
        {
            const auto item=capture.work[capture.cursor];
            const double scale=workScale(item),cost=workMilliseconds[item.kind]*scale;
            // One unit is always admitted to prevent starvation when foreground
            // work already exceeds the soft target. No GPU wait or preemption.
            if(statistics.iblWorkUnitsThisFrame>0 && submitted+cost>available) break;
            begin(IblTime,item.kind,scale); executeWork(item); end();
            submitted+=cost; ++statistics.iblWorkUnitsThisFrame; ++capture.cursor;
            statistics.captureWorkCompleted=capture.cursor;
            statistics.captureProgress=float(capture.cursor)/float(capture.work.size());
        }
        statistics.iblSubmittedMilliseconds=submitted;
        statistics.iblEstimatedMilliseconds=0;
        for(std::size_t i=capture.cursor;i<capture.work.size();++i)
            statistics.iblEstimatedMilliseconds+=workMilliseconds[capture.work[i].kind]*workScale(capture.work[i]);
    }
    void blendEnvironment(bool record=true)
    {
        if(!hasEnvironment || !environmentBlending) return;
        if(record) begin(IblTime);
        const float t=blendDuration>0.0f?std::clamp(static_cast<float>(blendElapsed/blendDuration),0.0f,1.0f):1.0f;
        const auto p=programs[Blend]; glUseProgram(p);
        glUniform1f(location(p,"geBlendFactor"),t);
        for(int kind=0;kind<4;++kind)
        {
            const auto texture=[&](const EnvironmentSet& set){return kind==0?set.sky:(kind==1?set.lighting:(kind==2?set.irradiance:set.prefilter));};
            const auto& displayed=environments[2];
            const int size=kind<2?displayed.cubeSize:(kind==2?displayed.irradianceSize:displayed.prefilterSize);
            const int levels=kind==3?std::min(5,mipCount(size)):1;
            textureUnit(0,texture(environments[0])); textureUnit(1,texture(environments[1]));
            for(int level=0;level<levels;++level)
            {
                textureImage(0,texture(environments[2]),GL_RGBA16F,level);
                glUniform1f(location(p,"geBlendLevel"),static_cast<float>(level));
                submit(std::max(1,size>>level),std::max(1,size>>level),6);
            }
        }
        displayedPalette=mixAtmospherePalette(fromPalette,targetPalette,t);
        if(t>=1.0f) environmentBlending=false;
        if(record) end();
    }
};

Atmosphere::Atmosphere():m(std::make_unique<Impl>()) {}
Atmosphere::~Atmosphere()=default;
void Atmosphere::initialize(const ptrClass* pc,GraphicsDataMisc* graphics) { m->pc=pc; m->graphics=graphics; }
AtmosphereSettings& Atmosphere::settings() { return m->config; }
const AtmosphereSettings& Atmosphere::settings() const { return m->config; }
const AtmosphereStats& Atmosphere::stats() const { return m->statistics; }
const std::string& Atmosphere::error() const { return m->failure; }
bool Atmosphere::active() const { return m->config.enabled && m->ready && (m->hasEnvironment || m->liveSkyReady); }
const AtmospherePalette& Atmosphere::displayedPalette() const { return m->hasDisplayedPalette?m->displayedPalette:m->transition.displayed(); }
void Atmosphere::setPaletteTarget(const AtmospherePalette& value,bool force)
{
    AtmospherePalette p=value;
    const AtmospherePalette fallback;
    auto clean=[](glm::vec3 color,const glm::vec3& defaults)
    {
        for(int i=0;i<3;++i) color[i]=std::isfinite(color[i])?std::clamp(color[i],0.0f,65000.0f):defaults[i];
        return color;
    };
    p.horizon=clean(p.horizon,fallback.horizon); p.middle=clean(p.middle,fallback.middle); p.zenith=clean(p.zenith,fallback.zenith);
    p.middleElevation=std::isfinite(p.middleElevation)?std::clamp(p.middleElevation,.01f,.99f):.25f;
    p.gradientPower=std::isfinite(p.gradientPower)?std::clamp(p.gradientPower,.05f,8.0f):1.0f;
    p.strength=std::isfinite(p.strength)?std::clamp(p.strength,0.0f,1.0f):.35f;
    p.exposure=std::isfinite(p.exposure)?std::clamp(p.exposure,0.0f,64.0f):1.0f;
    if(m->transition.target(p,m->config.paletteThreshold,m->config.transitionSeconds,force,m->hasDisplayedPalette?&m->displayedPalette:nullptr)) m->dirty=true;
}
void Atmosphere::reloadShaders() { m->programsDirty=true; m->dirty=true; }
void Atmosphere::invalidate() { m->dirty=m->physicalDirty=m->multipleDirty=m->skyDirty=m->noiseDirty=true; m->volumesReady=false; m->cloudShadowValid=false; }
void Atmosphere::setSun(Lights* sun) { if(m->requestedSun!=sun) { m->requestedSun=sun; m->dirty=m->skyDirty=true; } }
void Atmosphere::setMoon(Lights* moon) { if(m->requestedMoon!=moon) { m->requestedMoon=moon; m->dirty=m->skyDirty=true; } }
void Atmosphere::setExternalEnvironment(unsigned int cube,std::uint64_t revision)
{
    if(cube!=m->externalEnvironment || revision!=m->externalRevision) m->dirty=true;
    m->externalEnvironment=cube; m->externalRevision=revision;
}
void Atmosphere::setRadianceShader(const std::string& path)
{
    if(path!=m->radiancePath) { m->radiancePath=path; m->programsDirty=true; }
}
void Atmosphere::setGameFogShader(const std::string& path)
{
    if(path!=m->gameFogPath) { m->gameFogPath=path; m->programsDirty=true; }
}
void Atmosphere::setGameFogBinding(std::function<void(unsigned)> bind) { m->bindGameFog=std::move(bind); }

void Atmosphere::prepareShadows()
{
    if(!m->pc || !m->pc->lightManager) return;
    Lights* desiredSun=m->config.enabled?m->validateLight(m->requestedSun):nullptr;
    Lights* desiredMoon=m->config.enabled?m->validateLight(m->requestedMoon):nullptr;
    if(m->config.enabled && !desiredSun && m->config.fallbackToFirstDirectional)
        for(auto* light:m->pc->lightManager->getLights())
            if(light!=desiredMoon && m->validateLight(light)) { desiredSun=light; break; }
    auto& managed=m->managedShadowMargins;
    for(auto it=managed.begin();it!=managed.end();)
    {
        if(it->first==desiredSun || it->first==desiredMoon) { ++it; continue; }
        if(std::find(m->pc->lightManager->getLights().begin(),m->pc->lightManager->getLights().end(),it->first)!=m->pc->lightManager->getLights().end())
            it->first->setShadowCasterMargin(it->second);
        it=managed.erase(it);
    }
    for(auto* light:{desiredSun,desiredMoon})
    {
        if(!light) continue;
        if(std::find_if(managed.begin(),managed.end(),[&](const auto& entry){return entry.first==light;})==managed.end())
            managed.emplace_back(light,light->getShadowCasterMargin());
        light->setShadowCasterMargin(m->config.shadowCasterMargin);
    }
}

void Atmosphere::prepare(Camera* camera,double deltaSeconds)
{
    if(!m->config.enabled)
    {
        if(m->capture.active) m->cancelCapture();
        return;
    }
    if(!camera || !m->pc || !m->pc->lightManager) return;
    ++m->frame; m->pollTimers();
    m->statistics.frameDispatches=0; m->statistics.iblWorkUnitsThisFrame=0;
    m->statistics.iblSubmittedMilliseconds=0;
    const double dt=std::isfinite(deltaSeconds)?std::max(deltaSeconds,0.0):0.0;
    m->time+=dt; m->environmentElapsed+=dt; m->blendElapsed+=dt;
    // Integrate velocity, not velocity times the total clock: editing or
    // stopping the wind must preserve the current cloud/weather formation.
    // Keep CPU precision between frames; a capture copies the uploaded offset
    // by value along with its other frozen parameters and shadow map.
    for(int axis=0;axis<2;++axis)
    {
        const double velocity=std::isfinite(m->config.wind[axis])
            ?std::clamp(m->config.wind[axis],-1000.0f,1000.0f):0.0;
        m->cloudDisplacement[axis]+=velocity*dt;
    }
    if(!m->transition.initialized()) m->transition.target(AtmospherePalette{},0,0);
    if(!m->hasDisplayedPalette)
    {
        m->displayedPalette=m->transition.displayed();
        m->hasDisplayedPalette=true;
    }
    if(m->hasEnvironment) m->transition.advance(dt);
    if(m->programsDirty)
    {
        m->cancelCapture();
        if(!m->buildPrograms())
        {
            // Failed reloads leave every previously published texture/program
            // valid. A later explicit reload retries, never a per-frame loop.
            m->programsDirty=false; return;
        }
    }
    if(!m->programs[Environment] || !m->programs[Brdf] || !m->programs[SkyDraw]) return;
    const Budget nextBudget=budgetFor(m->config);
    if(!m->ready || nextBudget!=m->budget)
    {
        if(!m->allocate(nextBudget)) return;
        m->ready=true;
    }
    else m->updateEffectResources();
    m->blendEnvironment();
    m->fillParameters(camera);
    const auto& old=m->previous;
    if(m->gpu.radii.x!=old.radii.x || m->gpu.radii.y!=old.radii.y ||
        m->gpu.rayleigh!=old.rayleigh || m->gpu.mie!=old.mie || m->gpu.absorption!=old.absorption ||
        m->gpu.medium.x!=old.medium.x || m->gpu.medium.z!=old.medium.z || m->gpu.control.x!=old.control.x)
        m->physicalDirty=m->multipleDirty=m->skyDirty=m->dirty=true;
    if(m->gpu.art.w!=old.art.w || m->gpu.medium.y!=old.medium.y)
        m->multipleDirty=m->skyDirty=m->dirty=true;
    if(m->gpu.radii.z!=old.radii.z || m->gpu.sunDirection!=old.sunDirection || m->gpu.sunRadiance!=old.sunRadiance ||
        m->gpu.moonDirection!=old.moonDirection || m->gpu.moonRadiance!=old.moonRadiance)
        m->skyDirty=m->dirty=true;
    if(m->gpu.sunDisk!=old.sunDisk || m->gpu.moonDisk!=old.moonDisk) m->dirty=true;
    if(std::abs(m->gpu.camera.y-m->lastSkyAltitude)>=std::max(.01f,m->config.altitudeUpdateThreshold)) m->skyDirty=true;
    const glm::vec2 cloudMotion(m->gpu.camera.x-m->lastBakedCamera.x,m->gpu.camera.z-m->lastBakedCamera.z);
    if(std::abs(m->gpu.camera.y-m->lastBakedCamera.y)>=std::max(.01f,m->config.altitudeUpdateThreshold) ||
       (m->cloudsEnabled() && glm::dot(cloudMotion,cloudMotion)>=std::pow(std::max(.01f,m->config.movementThreshold),2.0f)))
    { m->dirty=true; m->lastBakedCamera=glm::vec3(m->gpu.camera); }
    if(atmospherePaletteDifference(m->capturePalette(),m->lastRequestedPalette)>=std::max(.000001f,m->config.environmentThreshold))
    { m->dirty=true; m->lastRequestedPalette=m->capturePalette(); }
    if(m->gpu.misc.x!=old.misc.x) m->noiseDirty=m->dirty=true;
    if(std::memcmp(&m->gpu.cloudLayer,&old.cloudLayer,2*sizeof(glm::vec4))!=0 || m->gpu.cloudOffset.z!=old.cloudOffset.z ||
        m->gpu.cloudWeather!=old.cloudWeather || m->gpu.cloudProfiles!=old.cloudProfiles || m->gpu.control.z!=old.control.z)
        m->dirty=true;
    m->upload();
    if(m->physicalPrograms)
    {
        if(m->noiseDirty && m->cloudsEnabled())
        {
            m->begin(CloudTime); glUseProgram(m->programs[CloudNoise]);
            textureImage(0,m->noise,GL_RGBA16F); m->submit(m->budget.noise,m->budget.noise,m->budget.noise,4,4,4);
            glGenerateTextureMipmap(m->noise);
            glMemoryBarrier(GL_TEXTURE_UPDATE_BARRIER_BIT|GL_TEXTURE_FETCH_BARRIER_BIT);
            m->noiseDirty=false; m->dirty=true; m->end();
        }
        if(m->physicalDirty && m->config.physicalAtmosphere)
        {
            m->begin(LutTime); glUseProgram(m->programs[Transmittance]);
            textureImage(0,m->transmittance,GL_RGBA16F,0,false); m->submit(m->budget.transWidth,m->budget.transHeight,1);
            ++m->statistics.transmittanceUpdates; m->multipleDirty=m->skyDirty=true; m->end();
        }
        m->physicalDirty=false;
        if(m->multipleDirty && m->config.physicalAtmosphere)
        {
            m->begin(LutTime); m->commonTextures(); glUseProgram(m->programs[MultipleScattering]);
            textureImage(0,m->multiple,GL_RGBA16F,0,false); m->submit(m->budget.multiSize,m->budget.multiSize,1);
            ++m->statistics.multiscatterUpdates; m->skyDirty=true; m->end();
        }
        m->multipleDirty=false;
        const auto& oldShadow=m->lastCloudShadowParameters;
        const bool cloudShadowChanged=!m->cloudShadowValid ||
            m->gpu.sunDirection!=oldShadow.sunDirection || m->gpu.moonDirection!=oldShadow.moonDirection ||
            m->gpu.misc.x!=oldShadow.misc.x || m->gpu.cloudWeather!=oldShadow.cloudWeather ||
            m->gpu.cloudProfiles!=oldShadow.cloudProfiles ||
            std::memcmp(&m->gpu.cloudLayer,&oldShadow.cloudLayer,4*sizeof(glm::vec4))!=0;
        if(m->cloudsEnabled() && cloudShadowChanged)
        {
            m->begin(CloudTime); m->commonTextures(); glUseProgram(m->programs[CloudShadow]);
            textureImage(0,m->cloudShadow,GL_RG16F,0,false); m->submit(m->budget.shadow,m->budget.shadow,1);
            ++m->statistics.cloudShadowDispatches;
            m->lastCloudShadowParameters=m->gpu; m->cloudShadowValid=true; m->end();
        }
    }
    // Coalesce newer input while a job runs; never restart on camera motion or
    // wind. A complete older capture is published, then the latest is queued.
    const bool cloudDisplacementChanged=m->cloudsEnabled() &&
        glm::vec2(m->gpu.cloudOffset)!=glm::vec2(m->capture.parameters.cloudOffset);
    if((cloudDisplacementChanged || !m->radiancePath.empty()) && !m->capture.active &&
        m->environmentElapsed>=std::max(0.0f,m->config.environmentInterval)) m->dirty=true;
    if(m->dirty)
    {
        ++m->statistics.requestedEnvironmentRevision; m->needsCapture=true; m->dirty=false;
    }
    const auto candidate=m->gpu;
    m->serviceEnvironment(candidate);
    m->previous=candidate;
    // The current camera view is independent of the IBL capture clock. Only
    // its artistic state follows the actually published palette/blend.
    const auto& displayed=m->displayedPalette;
    m->gpu.horizon={displayed.horizon,displayed.strength};
    m->gpu.middle={displayed.middle,displayed.middleElevation};
    m->gpu.zenith={displayed.zenith,displayed.gradientPower}; m->gpu.art.x=displayed.exposure;
    m->upload();
    if(m->physicalPrograms)
    {
        const bool changedPalette=atmospherePaletteDifference(displayed,m->lastSkyPalette)>.000001f;
        if(!m->liveSkyReady || m->skyDirty || changedPalette || !m->radiancePath.empty())
        {
            m->begin(SkyTime); m->commonTextures(); glUseProgram(m->programs[SkyView]);
            textureImage(0,m->skyView,GL_RGBA16F,0,false); m->submit(m->budget.skyWidth,m->budget.skyHeight,1);
            ++m->statistics.skyUpdates; m->lastSkyAltitude=m->gpu.camera.y;
            m->lastSkyPalette=displayed; m->skyDirty=false; m->liveSkyReady=true; m->visibleCloudValid=false; m->end();
        }
        auto viewKey=m->gpu;
        viewKey.misc.y=0;
        if(m->radiancePath.empty()) viewKey.camera.w=0;
        if(m->cloudsEnabled() && (!m->visibleCloudValid || std::memcmp(&viewKey,&m->lastVisibleCloudParameters,sizeof(viewKey))!=0))
        {
            m->begin(CloudTime); m->commonTextures(); glUseProgram(m->programs[VisibleClouds]);
            textureImage(0,m->visibleClouds,GL_RGBA16F,0,false); m->submit(m->budget.cloudViewWidth,m->budget.cloudViewHeight,1);
            ++m->statistics.visibleCloudDispatches;
            m->lastVisibleCloudParameters=viewKey; m->visibleCloudValid=true; m->end();
        }
    }
    glBindVertexArray(0);
}

void Atmosphere::renderSky(unsigned int)
{
    if(!active()) return;
    m->begin(SkyTime);
    glBindBufferBase(GL_UNIFORM_BUFFER,6,m->parameters);
    glUseProgram(m->programs[SkyDraw]);
    // The caller's external cube is already a complete resource. It can be
    // displayed while the first private IBL transaction is still in progress.
    textureUnit(0,m->hasEnvironment?m->environments[2].lighting:m->externalEnvironment);
    textureUnit(1,m->visibleClouds); textureUnit(2,m->skyView); textureUnit(3,m->noise);
    textureUnit(11,m->multiple); textureUnit(14,m->cloudShadow); textureUnit(15,m->transmittance);
    glUniform1i(location(m->programs[SkyDraw],"geCurrentCloudView"),!m->externalEnvironment && m->liveSkyReady);
    glUniform1i(location(m->programs[SkyDraw],"geRawExternalEnvironment"),m->externalEnvironment && !m->hasEnvironment);
    glDisable(GL_CULL_FACE); glDisable(GL_DEPTH_TEST); glDepthMask(GL_FALSE);
    glBindVertexArray(m->vao); glDrawArrays(GL_TRIANGLES,0,3);
    glDepthMask(GL_TRUE); glEnable(GL_DEPTH_TEST);
    m->end();
}

void Atmosphere::computeVolumes(Camera* camera)
{
    if(!active() || !m->physicalPrograms || !camera) return;
    auto* lights=m->pc->lightManager;
    const auto shadowTexture=lights->getTextureShadowArray();
    m->gpu.shadow.z=static_cast<int>(lights->getFrameShadowBuffer().size());
    auto volumeKey=m->gpu;
    volumeKey.camera.w=0; volumeKey.misc.y=0;
    if(!m->cloudsEnabled()) volumeKey.cloudOffset.x=volumeKey.cloudOffset.y=0;
    if(!m->mediumEnabled())
    {
        // The one-voxel identities allocated on the toggle need no compute.
        m->lastVolumeParameters=volumeKey; m->volumesReady=true; return;
    }
    const bool usesTerrainShadows=m->config.godRays && (m->gpu.shadow.x>=0 || m->gpu.shadow.y>=0);
    // This revision is committed after shadow drawing, including conservative
    // redraws for casters without a content revision. Zero means untracked.
    const auto shadowRevision=usesTerrainShadows?lights->getShadowContentRevision():0;
    const bool shadowChanged=usesTerrainShadows &&
        (shadowRevision==0 || shadowRevision!=m->lastVolumeShadowRevision);
    // Fog hooks may animate independently or replace their atlas. Only the
    // integration pass evaluates them; physical/cloud coefficients do not.
    const bool unknownGameMedium=m->config.localFog && (!m->gameFogPath.empty() || bool(m->bindGameFog));
    const bool unknownRadiance=!m->radiancePath.empty();
    auto aerialKey=volumeKey;
    aerialKey.volume.y=aerialKey.volume.z=aerialKey.volume.w=0;
    aerialKey.fog=aerialKey.fogNoise=glm::vec4(0);
    aerialKey.control.y=0; aerialKey.samples.x=0;
    // Disk appearance and specular IBL saturation do not illuminate media.
    aerialKey.sunDisk=aerialKey.moonDisk=glm::vec4(0); aerialKey.art.z=0;
    auto cloudKey=aerialKey;
    // Cloud coefficients trace their own light rays, not the cloud shadow atlas.
    cloudKey.shadowOrigin=glm::vec4(0); cloudKey.cloudOffset.z=0;
    const bool aerialChanged=m->config.physicalAtmosphere && (!m->volumesReady ||
        shadowChanged || unknownRadiance || std::memcmp(&aerialKey,&m->lastAerialParameters,sizeof(aerialKey))!=0);
    const bool cloudChanged=m->cloudsEnabled() && (!m->volumesReady ||
        shadowChanged || unknownRadiance || std::memcmp(&cloudKey,&m->lastCloudVolumeParameters,sizeof(cloudKey))!=0);
    if(m->volumesReady && !aerialChanged && !cloudChanged && !shadowChanged &&
        !unknownGameMedium && !unknownRadiance &&
        std::memcmp(&volumeKey,&m->lastVolumeParameters,sizeof(volumeKey))==0) return;
    m->upload(); m->commonTextures(); textureUnit(4,shadowTexture);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER,5,lights->getSsboShadow());
    const auto& b=m->budget;
    if(aerialChanged)
    {
        m->begin(AerialTime); glUseProgram(m->programs[Aerial]);
        textureImage(0,m->physicalSource,GL_RGBA32F); textureImage(1,m->physicalExtinction,GL_RGBA32F);
        m->submit(b.width,b.height,b.depth,4,4,4); ++m->statistics.aerialDispatches; m->end();
    }
    if(cloudChanged)
    {
        m->begin(CloudTime); glUseProgram(m->programs[CloudVolume]);
        textureImage(0,m->cloudSource,GL_RGBA32F); textureImage(1,m->cloudExtinction,GL_R32F);
        m->submit(b.width,b.height,b.depth,4,4,4); ++m->statistics.cloudVolumeDispatches; m->end();
    }
    m->begin(FogTime); glUseProgram(m->programs[Volume]);
    if(m->config.localFog && m->bindGameFog) m->bindGameFog(m->programs[Volume]);
    textureUnit(9,m->physicalSource); textureUnit(10,m->physicalExtinction);
    textureUnit(11,m->cloudSource); textureUnit(12,m->cloudExtinction);
    textureImage(0,m->scattering,GL_RGBA16F); textureImage(1,m->transmission,GL_RGBA16F);
    textureImage(2,m->baselineScattering,GL_RGBA16F,0,false); textureImage(3,m->baselineTransmission,GL_RGBA16F,0,false);
    m->submit(b.width,b.height,1); ++m->statistics.fogDispatches; m->end();
    m->lastVolumeParameters=volumeKey; m->lastAerialParameters=aerialKey; m->lastCloudVolumeParameters=cloudKey;
    m->lastVolumeShadowRevision=shadowRevision; m->volumesReady=true; ++m->statistics.volumeUpdates;
}

void Atmosphere::bindLighting(unsigned int program,bool)
{
    const bool on=active();
    glProgramUniform1i(program,location(program,"uAtmosphereActive"),on && m->volumesReady);
    glProgramUniform1i(program,location(program,"uAtmosphereIblActive"),on && m->hasEnvironment);
    if(!on) return;
    glBindBufferBase(GL_UNIFORM_BUFFER,6,m->parameters);
    // Materials may still sample the engine's existing environment while the
    // initial transaction is incomplete. Do not replace that complete set with
    // unallocated handles merely because the current sky is already visible.
    if(m->hasEnvironment)
    {
        textureUnit(6,m->environments[2].irradiance); textureUnit(7,m->environments[2].prefilter); textureUnit(8,m->brdf);
    }
    textureUnit(9,m->baselineScattering); textureUnit(10,m->baselineTransmission); textureUnit(11,m->multiple);
    textureUnit(12,m->scattering); textureUnit(13,m->transmission); textureUnit(14,m->cloudShadow); textureUnit(15,m->transmittance);
    glProgramUniform1f(program,location(program,"uAtmosphereIblSaturation"),std::max(0.0f,m->config.iblSaturation));
    glProgramUniform1i(program,location(program,"uAtmosphereSunIndex"),m->sun?static_cast<int>(m->sun->getIndex()):-1);
    glProgramUniform1i(program,location(program,"uAtmosphereMoonIndex"),m->moon?static_cast<int>(m->moon->getIndex()):-1);
    glProgramUniform1f(program,location(program,"uAtmosphereSunScale"),m->config.sunIlluminanceScale);
    glProgramUniform1f(program,location(program,"uAtmosphereMoonScale"),m->config.moonIlluminanceScale);
    glProgramUniform2f(program,location(program,"uAtmosphereInvResolution"),
        1.0f/std::max(m->graphics->str_width,1u),1.0f/std::max(m->graphics->str_height,1u));
}

void Atmosphere::release()
{
    if(!m) return;
    if(m->pc && m->pc->lightManager)
        for(const auto& entry:m->managedShadowMargins)
            if(std::find(m->pc->lightManager->getLights().begin(),m->pc->lightManager->getLights().end(),entry.first)!=m->pc->lightManager->getLights().end())
                entry.first->setShadowCasterMargin(entry.second);
    m->managedShadowMargins.clear();
    m->releaseTargets(); m->releasePrograms(); m->destroyTexture(m->brdf);
    if(m->parameters) glDeleteBuffers(1,&m->parameters);
    if(m->vao) glDeleteVertexArrays(1,&m->vao);
    for(auto& entry:m->timers) if(entry.query) glDeleteQueries(1,&entry.query);
    m->parameters=m->vao=0; m->timers={}; m->ready=false;
    // Resource recreation retains the simulation clock, integrated cloud
    // displacement and last visible palette. A new Atmosphere starts afresh.
    m->programsDirty=true; m->bindGameFog={}; m->pc=nullptr; m->graphics=nullptr;
}
}
