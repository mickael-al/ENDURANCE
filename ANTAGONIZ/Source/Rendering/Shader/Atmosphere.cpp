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
#include <utility>
#include <vector>

namespace Ge
{
namespace
{
    // Exactly 28 16-byte slots followed by three column-major matrices.
    struct alignas(16) AtmosphereGpuParameters
    {
        glm::vec4 radii{},rayleigh{},mie{},absorption{},medium{};
        glm::vec4 sunDirection{},sunRadiance{},sunDisk{};
        glm::vec4 moonDirection{},moonRadiance{},moonDisk{};
        glm::vec4 horizon{},middle{},zenith{},art{},camera{},volume{},fog{},fogNoise{};
        glm::vec4 cloudLayer{},cloudShape{},cloudWind{},shadowOrigin{};
        glm::ivec4 shadow{},quality{},samples{},control{};
        glm::uvec4 misc{};
        glm::mat4 invView{1},invProjection{1},view{1};
    };
    static_assert(sizeof(AtmosphereGpuParameters)==640,"Atmosphere std140 layout drift");
    static_assert(offsetof(AtmosphereGpuParameters,invView)==448,"Atmosphere std140 matrix offset");

    struct Budget
    {
        int transWidth=256,transHeight=64,multiSize=32,skyWidth=256,skyHeight=128;
        int cube=128,irradiance=16,prefilter=128,filterSamples=96;
        int width=128,height=72,depth=80,noise=64,shadow=256;
        int skySteps=48,transSteps=64,multiDirections=24,multiSteps=16;
        int volumeSteps=1,cloudLightSteps=6,cloudShadowSteps=12,cloudSkySteps=28;
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
            glClearTexImage(t,level,format==GL_RG16F?GL_RG:GL_RGBA,GL_FLOAT,black);
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
        CloudNoise,CloudShadow,Environment,Filter,Blend,Brdf,SkyDraw,ProgramCount };
    enum Timing { SkyTime,LutTime,IblTime,AerialTime,FogTime,CloudTime,TimingCount };
    struct EnvironmentSet { GLuint sky=0,lighting=0,irradiance=0,prefilter=0; };
    struct TimerEntry { GLuint query=0; bool pending=false; unsigned kind=0; std::uint64_t frame=0; };
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
    bool hasEnvironment=false,environmentBlending=false;
    unsigned frame=0;
    double time=0.0,environmentElapsed=0.0,blendElapsed=1.0;
    float blendDuration=0.0f,lastSkyAltitude=std::numeric_limits<float>::infinity();
    AtmospherePalette lastBakedPalette, lastSkyPalette, displayedPalette, fromPalette, targetPalette;
    glm::vec3 lastBakedCamera{0};
    Budget budget;
    AtmosphereGpuParameters gpu{},previous{},lastVolumeParameters{},lastCloudShadowParameters{};
    bool cloudShadowValid=false;
    std::array<GLuint,ProgramCount> programs{};
    GLuint parameters=0,vao=0;
    GLuint transmittance=0,multiple=0,skyView=0,noise=0,cloudShadow=0;
    GLuint physicalSource=0,physicalExtinction=0,cloudSource=0,cloudExtinction=0;
    GLuint scattering=0,transmission=0,baselineScattering=0,baselineTransmission=0;
    GLuint lightingEnvironment=0,brdf=0;
    // 0 snapshot of current display, 1 completed target, 2 coherent displayed set.
    std::array<EnvironmentSet,3> environments{};
    std::array<TimerEntry,64> timers{};
    std::array<std::uint64_t,TimingCount> lastTimingFrame{};
    std::array<double,TimingCount> lastTimingValue{};
    int activeTimer=-1;
    unsigned timerCursor=0;

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
            const std::array<std::pair<Program,const char*>,8> jobs{{
                {Transmittance,"transmittance.C.glsl"},{MultipleScattering,"multiscatter.C.glsl"},
                {SkyView,"skyview.C.glsl"},{Aerial,"aerial.C.glsl"},{CloudVolume,"cloud_volume.C.glsl"},
                {Volume,"volume.C.glsl"},{CloudNoise,"cloud_noise.C.glsl"},{CloudShadow,"cloud_shadow.C.glsl"}}};
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
        programsDirty=false; volumesReady=false; cloudShadowValid=false; physicalDirty=multipleDirty=skyDirty=noiseDirty=dirty=true;
        return true;
    }

    void releaseTargets()
    {
        for(auto& set:environments)
        {
            deleteTexture(set.sky); deleteTexture(set.lighting); deleteTexture(set.irradiance); deleteTexture(set.prefilter);
        }
        for(GLuint* t:{&transmittance,&multiple,&skyView,&noise,&cloudShadow,
            &physicalSource,&physicalExtinction,&cloudSource,&cloudExtinction,
            &scattering,&transmission,&baselineScattering,&baselineTransmission,&lightingEnvironment})
            deleteTexture(*t);
        hasEnvironment=false; environmentBlending=false; volumesReady=false; cloudShadowValid=false;
    }
    bool allocate(const Budget& b)
    {
        releaseTargets(); budget=b;
        const int prefilterLevels=std::min(5,mipCount(b.prefilter));
        for(auto& set:environments)
        {
            set.sky=makeTexture(GL_TEXTURE_CUBE_MAP,GL_RGBA16F,b.cube,b.cube);
            set.lighting=makeTexture(GL_TEXTURE_CUBE_MAP,GL_RGBA16F,b.cube,b.cube);
            set.irradiance=makeTexture(GL_TEXTURE_CUBE_MAP,GL_RGBA16F,b.irradiance,b.irradiance);
            set.prefilter=makeTexture(GL_TEXTURE_CUBE_MAP,GL_RGBA16F,b.prefilter,b.prefilter,1,prefilterLevels);
        }
        lightingEnvironment=makeTexture(GL_TEXTURE_CUBE_MAP,GL_RGBA16F,b.cube,b.cube,1,mipCount(b.cube));
        transmittance=makeTexture(GL_TEXTURE_2D,GL_RGBA16F,b.transWidth,b.transHeight);
        multiple=makeTexture(GL_TEXTURE_2D,GL_RGBA16F,b.multiSize,b.multiSize);
        skyView=makeTexture(GL_TEXTURE_2D,GL_RGBA16F,b.skyWidth,b.skyHeight);
        glTextureParameteri(skyView,GL_TEXTURE_WRAP_S,GL_REPEAT);
        noise=makeTexture(GL_TEXTURE_3D,GL_RGBA16F,b.noise,b.noise,b.noise,mipCount(b.noise));
        for(GLenum parameter:{GL_TEXTURE_WRAP_S,GL_TEXTURE_WRAP_T,GL_TEXTURE_WRAP_R})
            glTextureParameteri(noise,parameter,GL_REPEAT);
        cloudShadow=makeTexture(GL_TEXTURE_2D,GL_RG16F,b.shadow,b.shadow);
        physicalSource=makeTexture(GL_TEXTURE_3D,GL_RGBA32F,b.width,b.height,b.depth);
        physicalExtinction=makeTexture(GL_TEXTURE_3D,GL_RGBA32F,b.width,b.height,b.depth);
        cloudSource=makeTexture(GL_TEXTURE_3D,GL_RGBA32F,b.width,b.height,b.depth);
        cloudExtinction=makeTexture(GL_TEXTURE_3D,GL_RGBA32F,b.width,b.height,b.depth);
        scattering=makeTexture(GL_TEXTURE_3D,GL_RGBA16F,b.width,b.height,b.depth);
        transmission=makeTexture(GL_TEXTURE_3D,GL_RGBA16F,b.width,b.height,b.depth);
        baselineScattering=makeTexture(GL_TEXTURE_2D,GL_RGBA16F,b.width,b.height);
        baselineTransmission=makeTexture(GL_TEXTURE_2D,GL_RGBA16F,b.width,b.height);
        const float white[4]={1,1,1,1};
        glClearTexImage(transmission,0,GL_RGBA,GL_FLOAT,white);
        glClearTexImage(baselineTransmission,0,GL_RGBA,GL_FLOAT,white);
        glClearTexImage(transmittance,0,GL_RGBA,GL_FLOAT,white);
        glClearTexImage(cloudShadow,0,GL_RG,GL_FLOAT,white);
        if(!parameters) glCreateBuffers(1,&parameters);
        glNamedBufferData(parameters,sizeof(gpu),&gpu,GL_DYNAMIC_DRAW);
        if(!vao) glCreateVertexArrays(1,&vao);
        if(!brdf)
        {
            brdf=makeTexture(GL_TEXTURE_2D,GL_RG16F,128,128);
            glUseProgram(programs[Brdf]);
            textureImage(0,brdf,GL_RG16F,0,false); dispatch(128,128,1);
        }
        glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);
        physicalDirty=multipleDirty=skyDirty=noiseDirty=dirty=true;
        return true;
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
    void begin(Timing kind)
    {
        if(!config.measureGpu || activeTimer!=-1) return;
        for(unsigned i=0;i<timers.size();++i)
        {
            unsigned index=(timerCursor+i)%timers.size();
            auto& entry=timers[index];
            if(entry.pending) continue;
            if(!entry.query) glGenQueries(1,&entry.query);
            entry.kind=kind; entry.frame=frame;
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
            if(entry.frame<lastTimingFrame[entry.kind]) continue;
            if(entry.frame!=lastTimingFrame[entry.kind])
            { lastTimingValue[entry.kind]=0; lastTimingFrame[entry.kind]=entry.frame; }
            lastTimingValue[entry.kind]+=static_cast<double>(ns)*1e-6;
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
        const auto& palette=transition.displayed();
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
        gpu.cloudWind={config.wind.x,config.wind.y,std::max(config.aerialDistance,1024.0f),0};
        const float shadowTexel=2.0f*gpu.cloudWind.z/static_cast<float>(budget.shadow);
        gpu.shadowOrigin={std::floor(gpu.camera.x/shadowTexel)*shadowTexel,
            std::floor(gpu.camera.z/shadowTexel)*shadowTexel,0,0};
        gpu.shadow={sun?sun->getShadowIndex():-1,moon?moon->getShadowIndex():-1,0,0};
        gpu.quality={budget.skySteps,budget.transSteps,budget.multiDirections,budget.multiSteps};
        gpu.samples={budget.volumeSteps,budget.cloudLightSteps,budget.cloudShadowSteps,budget.cloudSkySteps};
        gpu.control={config.physicalAtmosphere?1:0,config.localFog?1:0,config.clouds?1:0,config.godRays?1:0};
        gpu.misc={config.cloudSeed,frame,0,0};
        gpu.view=camera->getViewMatrix(); gpu.invView=glm::inverse(gpu.view);
        gpu.invProjection=glm::inverse(camera->getProjectionMatrix());
        statistics.shadowDistance=camera->getFar();
    }
    void filterMap(GLuint output,int size,int level,int mode,float roughness)
    {
        const auto p=programs[Filter]; glUseProgram(p);
        textureUnit(0,lightingEnvironment); textureImage(0,output,GL_RGBA16F,level);
        glUniform1i(location(p,"geFilterMode"),mode);
        glUniform1i(location(p,"geFilterSamples"),budget.filterSamples);
        glUniform1f(location(p,"geFilterRoughness"),roughness);
        dispatch(size,size,6);
    }
    void copySet(const EnvironmentSet& from,const EnvironmentSet& to)
    {
        for(const auto pair:{std::pair<GLuint,GLuint>{from.sky,to.sky},
            {from.lighting,to.lighting},{from.irradiance,to.irradiance},{from.prefilter,to.prefilter}})
        {
            const int size=(pair.first==from.sky || pair.first==from.lighting)?budget.cube:(pair.first==from.irradiance?budget.irradiance:budget.prefilter);
            const int levels=pair.first==from.prefilter?std::min(5,mipCount(size)):1;
            for(int level=0;level<levels;++level)
                glCopyImageSubData(pair.first,GL_TEXTURE_CUBE_MAP,level,0,0,0,
                    pair.second,GL_TEXTURE_CUBE_MAP,level,0,0,0,std::max(1,size>>level),std::max(1,size>>level),6);
        }
        glMemoryBarrier(GL_TEXTURE_UPDATE_BARRIER_BIT|GL_TEXTURE_FETCH_BARRIER_BIT);
    }
    void bakeEnvironment()
    {
        begin(IblTime);
        // Snapshot the currently displayed blend, including interrupted fades.
        if(hasEnvironment) copySet(environments[2],environments[0]);
        fromPalette=displayedPalette; targetPalette=transition.displayed();
        auto& target=environments[1];
        glUseProgram(programs[Environment]); commonTextures();
        textureUnit(9,externalEnvironment);
        glUniform1i(location(programs[Environment],"geUseExternalEnvironment"),externalEnvironment?1:0);
        textureImage(0,target.sky,GL_RGBA16F); textureImage(1,lightingEnvironment,GL_RGBA16F);
        dispatch(budget.cube,budget.cube,6);
        // Ordinary source mipmaps only improve importance-sample footprint.
        // GGX roughness levels are separately integrated below.
        glGenerateTextureMipmap(lightingEnvironment);
        glMemoryBarrier(GL_TEXTURE_UPDATE_BARRIER_BIT|GL_TEXTURE_FETCH_BARRIER_BIT);
        filterMap(target.irradiance,budget.irradiance,0,0,1.0f);
        const int levels=std::min(5,mipCount(budget.prefilter));
        for(int level=0;level<levels;++level)
            filterMap(target.prefilter,std::max(1,budget.prefilter>>level),level,1,
                static_cast<float>(level)/static_cast<float>(levels-1));
        glCopyImageSubData(lightingEnvironment,GL_TEXTURE_CUBE_MAP,0,0,0,0,target.lighting,GL_TEXTURE_CUBE_MAP,0,0,0,0,budget.cube,budget.cube,6);
        glMemoryBarrier(GL_TEXTURE_UPDATE_BARRIER_BIT|GL_TEXTURE_FETCH_BARRIER_BIT);
        // No sampler sees the target until ALL its faces and GGX levels exist.
        if(!hasEnvironment)
        {
            copySet(target,environments[0]); copySet(target,environments[2]);
            hasEnvironment=true; environmentBlending=false; blendElapsed=1.0; blendDuration=0;
            displayedPalette=fromPalette=targetPalette;
        }
        else
        {
            blendElapsed=0.0; blendDuration=std::max(0.0f,config.environmentBlendSeconds);
            environmentBlending=blendDuration>0.0f;
            if(blendDuration==0.0f)
            {
                copySet(target,environments[2]); displayedPalette=targetPalette; blendElapsed=1.0;
            }
        }
        ++statistics.environmentUpdates;
        lastBakedPalette=transition.displayed(); if(!physicalPrograms) lastSkyPalette=lastBakedPalette; lastBakedCamera=glm::vec3(gpu.camera); environmentElapsed=0.0; dirty=false;
        end();
    }
    void blendEnvironment()
    {
        if(!hasEnvironment || !environmentBlending) return;
        begin(IblTime);
        const float t=blendDuration>0.0f?std::clamp(static_cast<float>(blendElapsed/blendDuration),0.0f,1.0f):1.0f;
        const auto p=programs[Blend]; glUseProgram(p);
        glUniform1f(location(p,"geBlendFactor"),t);
        for(int kind=0;kind<4;++kind)
        {
            const auto texture=[&](const EnvironmentSet& set){return kind==0?set.sky:(kind==1?set.lighting:(kind==2?set.irradiance:set.prefilter));};
            const int size=kind<2?budget.cube:(kind==2?budget.irradiance:budget.prefilter);
            const int levels=kind==3?std::min(5,mipCount(size)):1;
            textureUnit(0,texture(environments[0])); textureUnit(1,texture(environments[1]));
            for(int level=0;level<levels;++level)
            {
                textureImage(0,texture(environments[2]),GL_RGBA16F,level);
                glUniform1f(location(p,"geBlendLevel"),static_cast<float>(level));
                dispatch(std::max(1,size>>level),std::max(1,size>>level),6);
            }
        }
        displayedPalette=mixAtmospherePalette(fromPalette,targetPalette,t);
        if(t>=1.0f) environmentBlending=false;
        end();
    }
};

Atmosphere::Atmosphere():m(std::make_unique<Impl>()) {}
Atmosphere::~Atmosphere()=default;
void Atmosphere::initialize(const ptrClass* pc,GraphicsDataMisc* graphics) { m->pc=pc; m->graphics=graphics; }
AtmosphereSettings& Atmosphere::settings() { return m->config; }
const AtmosphereSettings& Atmosphere::settings() const { return m->config; }
const AtmosphereStats& Atmosphere::stats() const { return m->statistics; }
const std::string& Atmosphere::error() const { return m->failure; }
bool Atmosphere::active() const { return m->config.enabled && m->ready && m->hasEnvironment; }
const AtmospherePalette& Atmosphere::displayedPalette() const { return m->hasEnvironment?m->displayedPalette:m->transition.displayed(); }
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
    if(m->transition.target(p,m->config.paletteThreshold,m->config.transitionSeconds,force,m->hasEnvironment?&m->displayedPalette:nullptr)) m->dirty=true;
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
    if(!m->config.enabled || !camera || !m->pc || !m->pc->lightManager) return;
    ++m->frame; m->pollTimers();
    const double dt=std::isfinite(deltaSeconds)?std::max(deltaSeconds,0.0):0.0;
    m->time+=dt; m->environmentElapsed+=dt; m->blendElapsed+=dt;
    if(!m->transition.initialized()) m->transition.target(AtmospherePalette{},0,0);
    m->transition.advance(dt);
    if(m->programsDirty)
    {
        if(!m->buildPrograms())
        {
            // Retain a previously complete environment when a hook reload fails.
            // Explicit reload/setting change retries; never compile every frame.
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
    // Finish the elapsed portion of the previous publication before a new
    // snapshot can restart its clock. This also progresses at low frame rates.
    m->blendEnvironment();
    m->fillParameters(camera);
    // Dependencies deliberately exclude camera XZ, view rotation and palette
    // from the invariant transmittance / multiple-scattering LUTs.
    if(m->gpu.radii.x!=m->previous.radii.x || m->gpu.radii.y!=m->previous.radii.y ||
        m->gpu.rayleigh!=m->previous.rayleigh || m->gpu.mie!=m->previous.mie ||
        m->gpu.absorption!=m->previous.absorption || m->gpu.medium.x!=m->previous.medium.x ||
        m->gpu.medium.z!=m->previous.medium.z || m->gpu.control.x!=m->previous.control.x)
        m->physicalDirty=m->multipleDirty=true;
    // Ground reflectance and phase anisotropy affect MS, not optical depth.
    if(m->gpu.art.w!=m->previous.art.w || m->gpu.medium.y!=m->previous.medium.y)
        m->multipleDirty=true;
    if(m->gpu.radii.z!=m->previous.radii.z || m->gpu.sunDirection!=m->previous.sunDirection ||
        m->gpu.sunRadiance!=m->previous.sunRadiance || m->gpu.moonDirection!=m->previous.moonDirection ||
        m->gpu.moonRadiance!=m->previous.moonRadiance)
        m->skyDirty=m->dirty=true;
    if(m->gpu.sunDisk!=m->previous.sunDisk || m->gpu.moonDisk!=m->previous.moonDisk) m->dirty=true;
    if(std::abs(m->gpu.camera.y-m->lastSkyAltitude)>=std::max(.01f,m->config.altitudeUpdateThreshold))
        m->skyDirty=m->dirty=true;
    if(atmospherePaletteDifference(m->transition.displayed(),m->lastSkyPalette)>=std::max(0.000001f,m->config.environmentThreshold))
        m->skyDirty=m->dirty=true;
    if(m->gpu.misc.x!=m->previous.misc.x) m->noiseDirty=true;
    if(std::memcmp(&m->gpu.cloudLayer,&m->previous.cloudLayer,3*sizeof(glm::vec4))!=0 ||
        m->gpu.control.z!=m->previous.control.z) m->dirty=true;
    // Cloud parallax follows camera XZ even if wind is zero and the biome is uniform.
    const glm::vec2 cloudMotion(m->gpu.camera.x-m->lastBakedCamera.x,m->gpu.camera.z-m->lastBakedCamera.z);
    if(m->config.clouds && glm::dot(cloudMotion,cloudMotion)>=std::pow(std::max(.01f,m->config.movementThreshold),2.0f))
        m->dirty=true;
    const bool animatedClouds=m->config.clouds && m->config.cloudCoverage>0 && m->config.cloudDensity>0 && glm::length(m->config.wind)>0;
    m->upload();
    if(m->physicalPrograms)
    {
        if(m->noiseDirty && m->config.clouds)
        {
            m->begin(CloudTime); glUseProgram(m->programs[CloudNoise]);
            textureImage(0,m->noise,GL_RGBA16F); dispatch(m->budget.noise,m->budget.noise,m->budget.noise,4,4,4);
            glGenerateTextureMipmap(m->noise);
            glMemoryBarrier(GL_TEXTURE_UPDATE_BARRIER_BIT|GL_TEXTURE_FETCH_BARRIER_BIT);
            m->noiseDirty=false; m->dirty=true; m->end();
        }
        if(m->physicalDirty)
        {
            m->begin(LutTime); glUseProgram(m->programs[Transmittance]);
            textureImage(0,m->transmittance,GL_RGBA16F,0,false); dispatch(m->budget.transWidth,m->budget.transHeight,1);
            ++m->statistics.transmittanceUpdates;
            m->physicalDirty=false; m->multipleDirty=m->skyDirty=m->dirty=true; m->end();
        }
        if(m->multipleDirty)
        {
            m->begin(LutTime);
            m->commonTextures(); glUseProgram(m->programs[MultipleScattering]);
            textureImage(0,m->multiple,GL_RGBA16F,0,false); dispatch(m->budget.multiSize,m->budget.multiSize,1);
            ++m->statistics.multiscatterUpdates;
            m->multipleDirty=false; m->skyDirty=m->dirty=true; m->end();
        }
        // A cloud-only refresh must not move the palette's acceptance baseline.
        // If it captures a new interpolated palette, its SkyView must match it.
        const bool willBake=!m->hasEnvironment || ((m->dirty || animatedClouds) &&
            m->environmentElapsed>=std::max(0.0f,m->config.environmentInterval));
        if(willBake && atmospherePaletteDifference(m->transition.displayed(),m->lastSkyPalette)>0.000001f)
            m->skyDirty=true;
        if(m->skyDirty && (!m->hasEnvironment || m->environmentElapsed>=std::max(0.0f,m->config.environmentInterval)))
        {
            m->begin(SkyTime); m->commonTextures(); glUseProgram(m->programs[SkyView]);
            textureImage(0,m->skyView,GL_RGBA16F,0,false); dispatch(m->budget.skyWidth,m->budget.skyHeight,1);
            ++m->statistics.skyUpdates;
            m->lastSkyAltitude=m->gpu.camera.y; m->lastSkyPalette=m->transition.displayed(); m->skyDirty=false; m->end();
        }
        // Cloud shadow animation is independent of climate invalidation. A
        // static layer retains its map until its world footprint/source changes.
        const auto& oldShadow=m->lastCloudShadowParameters;
        const bool cloudShadowChanged=!m->cloudShadowValid || animatedClouds ||
            m->gpu.sunDirection!=oldShadow.sunDirection || m->gpu.moonDirection!=oldShadow.moonDirection ||
            m->gpu.misc.x!=oldShadow.misc.x || m->gpu.control.z!=oldShadow.control.z ||
            std::memcmp(&m->gpu.cloudLayer,&oldShadow.cloudLayer,4*sizeof(glm::vec4))!=0;
        if(cloudShadowChanged)
        {
            m->begin(CloudTime); m->commonTextures(); glUseProgram(m->programs[CloudShadow]);
            textureImage(0,m->cloudShadow,GL_RG16F,0,false); dispatch(m->budget.shadow,m->budget.shadow,1);
            m->lastCloudShadowParameters=m->gpu; m->cloudShadowValid=true; m->end();
        }
    }
    if(!m->hasEnvironment || ((m->dirty || animatedClouds) &&
        m->environmentElapsed>=std::max(0.0f,m->config.environmentInterval))) m->bakeEnvironment();
    m->previous=m->gpu;
    // Use the same published artistic state for the visible environment, IBL
    // and aerial scattering, including an interrupted environment blend.
    const auto& displayed=m->displayedPalette;
    m->gpu.horizon={displayed.horizon,displayed.strength};
    m->gpu.middle={displayed.middle,displayed.middleElevation};
    m->gpu.zenith={displayed.zenith,displayed.gradientPower};
    m->gpu.art.x=displayed.exposure;
    m->upload();
    glBindVertexArray(0);
}

void Atmosphere::renderSky(unsigned int)
{
    if(!active()) return;
    m->begin(SkyTime);
    glBindBufferBase(GL_UNIFORM_BUFFER,6,m->parameters);
    glUseProgram(m->programs[SkyDraw]); textureUnit(0,m->environments[2].lighting);
    textureUnit(3,m->noise); textureUnit(14,m->cloudShadow); textureUnit(15,m->transmittance);
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
    const bool dynamicShadows=m->config.godRays && (m->gpu.shadow.x>=0 || m->gpu.shadow.y>=0);
    const bool animatedClouds=m->config.clouds && m->config.cloudCoverage>0 && m->config.cloudDensity>0 && glm::length(m->config.wind)>0;
    // A game hook may animate independently or replace its world atlas, and
    // moving shadow casters have no scene revision counter: conservatively update.
    const bool unknownGameMedium=m->config.localFog && bool(m->bindGameFog);
    if(m->volumesReady && !dynamicShadows && !animatedClouds && !unknownGameMedium && m->radiancePath.empty() &&
        std::memcmp(&volumeKey,&m->lastVolumeParameters,sizeof(volumeKey))==0) return;
    m->upload(); m->commonTextures(); textureUnit(4,shadowTexture);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER,5,lights->getSsboShadow());
    const auto& b=m->budget;
    m->begin(AerialTime); glUseProgram(m->programs[Aerial]);
    textureImage(0,m->physicalSource,GL_RGBA32F); textureImage(1,m->physicalExtinction,GL_RGBA32F);
    dispatch(b.width,b.height,b.depth,4,4,4); m->end();
    m->begin(CloudTime); glUseProgram(m->programs[CloudVolume]);
    textureImage(0,m->cloudSource,GL_RGBA32F); textureImage(1,m->cloudExtinction,GL_RGBA32F);
    dispatch(b.width,b.height,b.depth,4,4,4); m->end();
    m->begin(FogTime); glUseProgram(m->programs[Volume]);
    if(m->bindGameFog) m->bindGameFog(m->programs[Volume]);
    textureUnit(9,m->physicalSource); textureUnit(10,m->physicalExtinction);
    textureUnit(11,m->cloudSource); textureUnit(12,m->cloudExtinction);
    textureImage(0,m->scattering,GL_RGBA16F); textureImage(1,m->transmission,GL_RGBA16F);
    textureImage(2,m->baselineScattering,GL_RGBA16F,0,false); textureImage(3,m->baselineTransmission,GL_RGBA16F,0,false);
    dispatch(b.width,b.height,1); m->end();
    m->lastVolumeParameters=volumeKey; m->volumesReady=true; ++m->statistics.volumeUpdates;
}

void Atmosphere::bindLighting(unsigned int program,bool)
{
    const bool on=active();
    glProgramUniform1i(program,location(program,"uAtmosphereActive"),on && m->volumesReady);
    glProgramUniform1i(program,location(program,"uAtmosphereIblActive"),on);
    if(!on) return;
    glBindBufferBase(GL_UNIFORM_BUFFER,6,m->parameters);
    textureUnit(6,m->environments[2].irradiance); textureUnit(7,m->environments[2].prefilter); textureUnit(8,m->brdf);
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
    m->releaseTargets(); m->releasePrograms(); deleteTexture(m->brdf);
    if(m->parameters) glDeleteBuffers(1,&m->parameters);
    if(m->vao) glDeleteVertexArrays(1,&m->vao);
    for(auto& entry:m->timers) if(entry.query) glDeleteQueries(1,&entry.query);
    m->parameters=m->vao=0; m->timers={}; m->ready=false;
    m->programsDirty=true; m->bindGameFog={}; m->pc=nullptr; m->graphics=nullptr;
}
}
