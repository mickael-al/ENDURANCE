// The production renderer, source loader and binary cache execute unchanged.
// Only camera/light/editor ownership is replaced by small fixture headers.
#include "glcore.hpp"
#include "Atmosphere.hpp"
#include "Camera.hpp"
#include "LightManager.hpp"
#include "PointeurClass.hpp"
#include "GraphicsDataMisc.hpp"
#include "ShaderCompilation.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
    void require(bool okay,const std::string& message)
    { if(!okay) throw std::runtime_error(message); }
    void checkGL(const std::string& label)
    {
        const auto error=glGetError();
        require(error==GL_NO_ERROR,label+": OpenGL error "+std::to_string(error));
    }
    GLuint textureAt(unsigned unit,GLenum query)
    {
        glActiveTexture(GL_TEXTURE0+unit);
        GLint texture=0; glGetIntegerv(query,&texture);
        glActiveTexture(GL_TEXTURE0);
        return static_cast<GLuint>(texture);
    }
    std::vector<float> readTexture(GLuint texture,GLenum target,int level=0)
    {
        require(texture!=0,"Expected a published texture");
        GLint width=0,height=0,depth=0;
        glGetTextureLevelParameteriv(texture,level,GL_TEXTURE_WIDTH,&width);
        glGetTextureLevelParameteriv(texture,level,GL_TEXTURE_HEIGHT,&height);
        glGetTextureLevelParameteriv(texture,level,GL_TEXTURE_DEPTH,&depth);
        require(width>0 && height>0,"Published unallocated mip level");
        const std::size_t layers=target==GL_TEXTURE_CUBE_MAP?6u:static_cast<std::size_t>(std::max(depth,1));
        std::vector<float> result(static_cast<std::size_t>(width)*height*layers*4,
                                  std::numeric_limits<float>::quiet_NaN());
        glGetTextureImage(texture,level,GL_RGBA,GL_FLOAT,static_cast<GLsizei>(result.size()*sizeof(float)),result.data());
        checkGL("runtime texture readback");
        for(float value:result) require(std::isfinite(value),"Runtime published NaN/infinite/unwritten texture data");
        return result;
    }
    float maximumRGB(const std::vector<float>& values)
    {
        float maximum=0;
        for(std::size_t i=0;i<values.size();++i) if(i%4!=3) maximum=std::max(maximum,values[i]);
        return maximum;
    }
    void closeTextures(const std::vector<float>& a,const std::vector<float>& b,float tolerance,const std::string& message)
    {
        require(a.size()==b.size(),message+": dimensions changed");
        for(std::size_t i=0;i<a.size();++i)
            require(std::abs(a[i]-b[i])<=tolerance,message+": texel "+std::to_string(i));
    }
    void constantCube(GLuint texture,const glm::vec3& expected)
    {
        const auto pixels=readTexture(texture,GL_TEXTURE_CUBE_MAP);
        for(std::size_t i=0;i<pixels.size();++i)
            require(std::abs(pixels[i]-(i%4==3?1.f:expected[static_cast<int>(i%4)]))<.012f,
                    "Multi-frame environment mixed source revisions between faces");
    }
    struct Targets
    {
        GLuint fbo=0;
        GLuint colors[4]{};
        Targets()
        {
            glCreateFramebuffers(1,&fbo);
            glCreateTextures(GL_TEXTURE_2D,4,colors);
            const GLenum outputs[]={GL_COLOR_ATTACHMENT0,GL_COLOR_ATTACHMENT1,GL_COLOR_ATTACHMENT2,GL_COLOR_ATTACHMENT3};
            for(int i=0;i<4;++i)
            {
                glTextureStorage2D(colors[i],1,i==0?GL_RG32F:GL_RGBA32F,32,18);
                glNamedFramebufferTexture(fbo,outputs[i],colors[i],0);
            }
            glNamedFramebufferDrawBuffers(fbo,4,outputs);
            require(glCheckNamedFramebufferStatus(fbo,GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE,"Runtime test framebuffer incomplete");
            glBindFramebuffer(GL_FRAMEBUFFER,fbo); glViewport(0,0,32,18);
        }
        ~Targets()
        {
            glBindFramebuffer(GL_FRAMEBUFFER,0);
            glDeleteFramebuffers(1,&fbo); glDeleteTextures(4,colors);
        }
    };
    GLint uniform(GLuint program,const char* name)
    {
        const GLint location=glGetUniformLocation(program,name);
        require(location>=0,std::string("Runtime fixture missing active uniform ")+name);
        GLint value=-100; glGetUniformiv(program,location,&value); return value;
    }
    std::array<float,168> uploadedParameters()
    {
        GLint buffer=0; glGetIntegeri_v(GL_UNIFORM_BUFFER_BINDING,6,&buffer);
        require(buffer!=0,"Atmosphere parameter UBO is not bound");
        GLint64 bytes=0; glGetNamedBufferParameteri64v(static_cast<GLuint>(buffer),GL_BUFFER_SIZE,&bytes);
        require(bytes==672,"Unexpected atmosphere UBO layout");
        std::array<float,168> data{};
        // Readback belongs exclusively to the test, never to the render loop.
        glGetNamedBufferSubData(static_cast<GLuint>(buffer),0,sizeof(data),data.data());
        checkGL("reading uploaded atmosphere parameters");
        return data;
    }
    Ge::AtmosphereSettings smallSettings()
    {
        Ge::AtmosphereSettings s;
        s.enabled=true; s.quality=Ge::AtmosphereQuality::Low;
        s.cubemapSize=16; s.prefilterSize=16; s.irradianceSize=4;
        s.iblSamples=8; s.skySteps=8; s.cloudLightSteps=2;
        s.volumeWidth=16; s.volumeHeight=9; s.volumeDepth=16;
        s.cloudViewWidth=16; s.cloudViewHeight=9;
        s.localFog=false; s.godRays=false; s.clouds=false;
        s.sunDiskIntensity=0; s.moonDiskIntensity=0;
        s.environmentInterval=0; s.environmentBlendSeconds=.25f;
        s.iblMaxWorkUnitsPerFrame=1; s.targetGpuMilliseconds=1000;
        s.measureGpu=false;
        return s;
    }
    struct ParameterUploadProbe
    {
        static inline PFNGLNAMEDBUFFERDATAPROC data=nullptr;
        static inline PFNGLNAMEDBUFFERSUBDATAPROC subData=nullptr;
        static inline unsigned streamWrites=0,inPlaceWrites=0;
        static void GLAPIENTRY recordData(GLuint buffer,GLsizeiptr size,const void* value,GLenum usage)
        {
            if(size==672 && usage==GL_STREAM_DRAW) ++streamWrites;
            data(buffer,size,value,usage);
        }
        static void GLAPIENTRY recordSubData(GLuint buffer,GLintptr offset,GLsizeiptr size,const void* value)
        {
            if(size==672) ++inPlaceWrites;
            subData(buffer,offset,size,value);
        }
        ParameterUploadProbe()
        {
            data=__glewNamedBufferData; subData=__glewNamedBufferSubData;
            streamWrites=inPlaceWrites=0;
            __glewNamedBufferData=recordData; __glewNamedBufferSubData=recordSubData;
        }
        ~ParameterUploadProbe() { __glewNamedBufferData=data; __glewNamedBufferSubData=subData; }
    };
    void checkParameterStreaming()
    {
        ParameterUploadProbe calls;
        Ge::LightManager lights; Ge::Lights sun; lights.lights={&sun};
        ptrClass pc; pc.lightManager=&lights; GraphicsDataMisc graphics; Ge::Camera camera;
        Ge::Atmosphere atmosphere; atmosphere.initialize(&pc,&graphics);
        atmosphere.settings()=smallSettings();
        std::string error;
        const auto probe=Ge::buildShaderProgram({{GL_COMPUTE_SHADER,"../Tests/Atmosphere/runtime_parameter_probe.C.glsl",{}}},error);
        require(probe!=0,error);
        GLuint output=0; glCreateBuffers(1,&output); glNamedBufferData(output,2*sizeof(glm::vec4),nullptr,GL_STREAM_READ);
        const GLint index=glGetUniformLocation(probe,"resultIndex");
        const auto capture=[&](unsigned destination)
        {
            glUseProgram(probe); glBindBufferBase(GL_SHADER_STORAGE_BUFFER,7,output);
            glUniform1ui(index,destination); glDispatchCompute(1,1,1);
            glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);
        };
        atmosphere.prepare(&camera,0);
        require(atmosphere.active(),atmosphere.error());
        require(calls.streamWrites==1 && calls.inPlaceWrites==0,
                "Atmosphere duplicated live uniform writes or overwrote queued uniforms");
        capture(0); // Intentionally do not wait/read before changing the UBO.
        camera.position={23,456,-7};
        atmosphere.prepare(&camera,.25);
        require(calls.streamWrites==2 && calls.inPlaceWrites==0,
                "Unchanged live palette triggered an unnecessary uniform upload");
        capture(1);
        std::array<glm::vec4,2> values{};
        glGetNamedBufferSubData(output,0,sizeof(values),values.data());
        require(glm::length(values[0]-glm::vec4(0,100,0,0))<.0001f &&
                glm::length(values[1]-glm::vec4(23,456,-7,.25f))<.0001f,
                "Streaming live uniforms changed an earlier queued dispatch or failed to rebind slot6");
        atmosphere.release(); glDeleteBuffers(1,&output); glDeleteProgram(probe);
        checkGL("streamed atmosphere parameters");
        std::cout<<"PASS atmosphere parameter streaming: one live upload per unchanged-palette frame, no in-place writes, queued GPU readers retain their own camera/clock\n"<<std::flush;
    }
    void checkShaderVariants()
    {
        Ge::LightManager lights; Ge::Lights sun; lights.lights={&sun};
        ptrClass pc; pc.lightManager=&lights; GraphicsDataMisc graphics; Ge::Camera camera;
        Ge::Atmosphere atmosphere; atmosphere.initialize(&pc,&graphics);
        auto& config=atmosphere.settings(); config=smallSettings(); config.wind={0,0};
        const auto frame=[&]()
        {
            atmosphere.prepare(&camera,0);
            require(atmosphere.active() && atmosphere.error().empty(),"Feature variant failed: "+atmosphere.error());
            require(atmosphere.stats().residentProgramVariants<=40,"Atmosphere variant cache exceeded 40 programs");
            checkGL("atmosphere feature variants");
            return atmosphere.stats();
        };
        auto previous=frame();
        require(previous.programVariantBuilds==15 && previous.residentProgramVariants==15,
                "Startup compiled speculative atmosphere combinations instead of only active stages");
        camera.position.x+=1; config.fogDensity*=2; sun.colors*=.9f;
        auto palette=atmosphere.displayedPalette(); palette.horizon*=.9f;
        atmosphere.setPaletteTarget(palette,true);
        auto next=frame();
        require(next.programVariantBuilds==previous.programVariantBuilds,
                "Dynamic camera/palette/source/numeric values recompiled atmosphere shaders");
        config.localFog=true; previous=next; next=frame();
        require(next.programVariantBuilds==previous.programVariantBuilds+1 &&
                next.transmittanceUpdates==previous.transmittanceUpdates &&
                next.multiscatterUpdates==previous.multiscatterUpdates,
                "Fog toggle rebuilt unrelated programs or optical LUTs");
        config.localFog=false; previous=next; next=frame();
        require(next.programVariantBuilds==previous.programVariantBuilds && next.programVariantHits>=previous.programVariantHits+15,
                "Returning to an existing feature combination failed to reuse programs");
        config.clouds=true; config.cloudCoverage=0; previous=next; next=frame();
        const auto configuredCloudPrograms=next.programVariantBuilds;
        config.cloudCoverage=.4f; config.cloudDensity=.001f; next=frame();
        config.cloudCoverage=0; next=frame();
        require(next.programVariantBuilds==configuredCloudPrograms,
                "Cloud weather/empty coverage changed static feature specialization");

        std::string error;
        const auto material=Ge::buildShaderProgram({{GL_COMPUTE_SHADER,"../Tests/Atmosphere/runtime_probe.C.glsl",{}}},error);
        require(material!=0,error);
        auto queries=atmosphere.stats().lightingLocationQueries;
        atmosphere.bindLighting(material,false,101);
        require(atmosphere.stats().lightingLocationQueries==queries+8,"Tracked material did not resolve eight locations once");
        queries=atmosphere.stats().lightingLocationQueries;
        atmosphere.bindLighting(material,false,101); atmosphere.bindLighting(material,true,101);
        require(atmosphere.stats().lightingLocationQueries==queries,"Repeated material binds queried linked uniform locations");
        atmosphere.bindLighting(material,false,102);
        require(atmosphere.stats().lightingLocationQueries==queries+8,"Program revision failed to invalidate material locations");
        queries=atmosphere.stats().lightingLocationQueries;
        atmosphere.bindLighting(material); atmosphere.bindLighting(material);
        require(atmosphere.stats().lightingLocationQueries==queries+16,"Untracked external programs unexpectedly cached potentially recycled GL IDs");
        config.enabled=false; atmosphere.bindLighting(material,false,102);
        require(uniform(material,"uAtmosphereActive")==0 && uniform(material,"uAtmosphereIblActive")==0,
                "Tracked uniform cache retained active flags while disabled");
        config.enabled=true;
        for(unsigned flags=0;flags<16;++flags)
        {
            config.physicalAtmosphere=(flags&1)!=0; config.localFog=(flags&2)!=0;
            config.clouds=(flags&4)!=0; config.godRays=(flags&8)!=0;
            frame();
        }
        require(atmosphere.stats().programVariantBuilds>40 && atmosphere.stats().residentProgramVariants==40,
                "Variant eviction fixture did not exercise the bounded cache");
        atmosphere.computeVolumes(&camera); checkGL("final feature variant volumes");
        glDeleteProgram(material); atmosphere.release();
        require(atmosphere.stats().residentProgramVariants==0,"Release leaked tracked atmosphere programs");
        std::cout<<"PASS shader variants: active-stage startup only, per-program feature keys, dynamic values stay uniforms, cache hits, 40-program eviction, revision-safe lighting locations\n"<<std::flush;
    }
    void checkInitialPalette(bool physical,float duration)
    {
        Ge::LightManager lights; Ge::Lights sun; lights.lights={&sun};
        ptrClass pc; pc.lightManager=&lights; GraphicsDataMisc graphics; Ge::Camera camera;
        Ge::Atmosphere atmosphere; atmosphere.initialize(&pc,&graphics);
        auto& config=atmosphere.settings(); config=smallSettings();
        config.physicalAtmosphere=physical; config.transitionSeconds=duration;
        config.environmentBlendSeconds=duration==0?0:.25f;
        Ge::AtmospherePalette initial;
        initial.horizon=initial.middle=initial.zenith=glm::vec3(.5f);
        initial.strength=1; initial.exposure=1;
        atmosphere.setPaletteTarget(initial,true);
        Targets targets;
        const auto frame=[&](double dt)
        {
            atmosphere.prepare(&camera,dt);
            require(atmosphere.active(),"Initial palette fixture did not render: "+atmosphere.error());
            glBindFramebuffer(GL_FRAMEBUFFER,targets.fbo); glViewport(0,0,32,18);
            atmosphere.renderSky(0); checkGL("initial palette sky render");
            const auto gpu=uploadedParameters();
            require(std::abs(gpu[14*4]-atmosphere.displayedPalette().exposure)<1e-6f,
                    "Visible palette API and uploaded artistic exposure disagree");
            require(std::abs(gpu[11*4]-.5f)<1e-6f && std::abs(gpu[12*4]-.5f)<1e-6f &&
                    std::abs(gpu[13*4]-.5f)<1e-6f,"Initial palette test unexpectedly changed gradient stops");
            return readTexture(targets.colors[2],GL_TEXTURE_2D);
        };
        const auto first=frame(0);
        require(!atmosphere.stats().environmentReady && atmosphere.stats().captureWorkCompleted==1,
                "Initial palette fixture did not stop after the first snapshot");
        require(maximumRGB(first)>0,"Initial palette fixture rendered no measurable sky");
        auto target=initial; target.exposure=2;
        atmosphere.setPaletteTarget(target,true);
        require(atmosphere.displayedPalette().exposure==1,
                "A target changed the visible palette before the first capture was published");
        float previousExposure=1,previousRadiance=maximumRGB(first);
        bool retargeted=false,published=false,converged=false;
        for(int i=0;i<300;++i)
        {
            if(i==4)
            {
                require(!atmosphere.stats().environmentReady,"Fixture retarget arrived after initial publication");
                target.exposure=4; atmosphere.setPaletteTarget(target,true); retargeted=true;
            }
            const auto pixels=frame(.1);
            const auto& state=atmosphere.stats();
            const float exposure=atmosphere.displayedPalette().exposure;
            const float radiance=maximumRGB(pixels);
            if(!state.environmentReady)
            {
                require(std::abs(exposure-1)<1e-6f,"Visible palette drifted during the first multi-frame capture");
                closeTextures(pixels,first,.0002f,"Sky radiance drifted while its initial IBL capture was pending");
            }
            require(exposure>=previousExposure-1e-6f,"Publication moved the displayed palette backwards");
            require(radiance>=previousRadiance-.002f,"Publication moved visible sky radiance backwards");
            previousExposure=exposure; previousRadiance=radiance;
            published=published || state.environmentReady;
            if(retargeted && published && state.environmentUpdates>=2 && std::abs(exposure-4)<.0001f)
            { converged=true; break; }
        }
        require(converged,"Initial capture lost the latest palette target instead of converging");
        require(std::abs(previousRadiance/maximumRGB(first)-4)<.015f,
                "Visible sky did not converge to the latest HDR exposure");
        atmosphere.release();
        std::cout<<"PASS initial palette: "<<(physical?"physical":"gradient")<<", duration "<<duration
                 <<", stable API/UBO/HDR sky until initial publication, no rollback, latest target converges\n"<<std::flush;
    }
    void checkVolumeCache()
    {
        Ge::LightManager lights; Ge::Lights sun; sun.shadow=0; lights.lights={&sun};
        lights.shadowFrames.resize(4);
        glCreateTextures(GL_TEXTURE_2D_ARRAY,1,&lights.shadowTexture);
        glTextureStorage3D(lights.shadowTexture,1,GL_DEPTH_COMPONENT32F,8,8,4);
        glTextureParameteri(lights.shadowTexture,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
        glTextureParameteri(lights.shadowTexture,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        const float lit=1,occluded=0;
        glClearTexImage(lights.shadowTexture,0,GL_DEPTH_COMPONENT,GL_FLOAT,&lit);
        struct ShadowMatrix { glm::mat4 matrix{1}; glm::vec3 position{}; float split=0; };
        static_assert(sizeof(ShadowMatrix)==80,"Runtime shadow fixture ABI drift");
        std::array<ShadowMatrix,4> matrices{};
        for(int i=0;i<4;++i)
        {
            matrices[i].matrix=glm::scale(glm::mat4(1),glm::vec3(1.f/8192));
            matrices[i].split=-1024.f*float(1<<i);
        }
        glCreateBuffers(1,&lights.shadowBuffer);
        glNamedBufferData(lights.shadowBuffer,sizeof(matrices),matrices.data(),GL_STATIC_DRAW);
        ptrClass pc; pc.lightManager=&lights; GraphicsDataMisc graphics; Ge::Camera camera;
        Ge::Atmosphere atmosphere; atmosphere.initialize(&pc,&graphics);
        auto& config=atmosphere.settings(); config=smallSettings();
        config.clouds=true; config.godRays=true; config.wind=glm::vec2(0);
        config.aerialDistance=4096; config.environmentBlendSeconds=0;
        const auto frame=[&](double dt)
        {
            atmosphere.prepare(&camera,dt);
            require(atmosphere.active(),"Volume cache fixture inactive: "+atmosphere.error());
            atmosphere.computeVolumes(&camera); checkGL("volume cache runtime frame");
            return atmosphere.stats();
        };
        const auto sameCoefficients=[](const Ge::AtmosphereStats& a,const Ge::AtmosphereStats& b)
        { return a.aerialDispatches==b.aerialDispatches && a.cloudVolumeDispatches==b.cloudVolumeDispatches; };
        const auto changedAll=[](const Ge::AtmosphereStats& a,const Ge::AtmosphereStats& b)
        { return b.aerialDispatches==a.aerialDispatches+1 && b.cloudVolumeDispatches==a.cloudVolumeDispatches+1 &&
                 b.fogDispatches==a.fogDispatches+1; };
        auto previous=frame(0);
        // Check the real C++ allocation/binding path, not just shader fixtures.
        const GLint formats[]={GL_RGBA32F,GL_RG32F,GL_RGBA32F};
        for(unsigned i=0;i<3;++i)
        {
            const auto texture=textureAt(9+i,GL_TEXTURE_BINDING_3D);
            GLint format=0,width=0,height=0,depth=0;
            glGetTextureLevelParameteriv(texture,0,GL_TEXTURE_INTERNAL_FORMAT,&format);
            glGetTextureLevelParameteriv(texture,0,GL_TEXTURE_WIDTH,&width);
            glGetTextureLevelParameteriv(texture,0,GL_TEXTURE_HEIGHT,&height);
            glGetTextureLevelParameteriv(texture,0,GL_TEXTURE_DEPTH,&depth);
            require(format==formats[i] && width==16 && height==9 && depth==16,
                    "Runtime coefficient storage does not use the packed FP32 layout");
        }
        GLint scattering=0; glGetIntegeri_v(GL_IMAGE_BINDING_NAME,0,&scattering);
        const auto litVolume=readTexture(static_cast<GLuint>(scattering),GL_TEXTURE_3D);
        auto next=frame(.5);
        require(sameCoefficients(previous,next) && previous.fogDispatches==next.fogDispatches,
                "A stationary tracked shadow map invalidated unchanged volumetric passes");
        glClearTexImage(lights.shadowTexture,0,GL_DEPTH_COMPONENT,GL_FLOAT,&occluded);
        ++lights.shadowContentRevision; previous=next; next=frame(.1);
        require(changedAll(previous,next),"Changed terrain shadow content did not refresh every affected volume");
        const auto shadowedVolume=readTexture(static_cast<GLuint>(scattering),GL_TEXTURE_3D);
        float shadowDifference=0;
        for(std::size_t i=0;i<litVolume.size();++i)
            if(i%4!=3) shadowDifference=std::max(shadowDifference,litVolume[i]-shadowedVolume[i]);
        require(shadowDifference>.0001f,"Shadow content revision refreshed counters but left stale scattering");
        config.localFog=true; config.fogDensity=.001f; config.fogScaleHeight=1000;
        previous=next; next=frame(.1);
        require(sameCoefficients(previous,next) && next.fogDispatches==previous.fogDispatches+1,
                "Local fog changes unnecessarily rebuilt physical/cloud coefficients");
        unsigned fogBindings=0;
        atmosphere.setGameFogBinding([&](unsigned){++fogBindings;});
        previous=next; next=frame(.1); next=frame(.1);
        require(fogBindings==2 && sameCoefficients(previous,next) && next.fogDispatches==previous.fogDispatches+2,
                "Untracked game fog did not animate independently of invariant coefficients");
        atmosphere.setGameFogBinding({});
        config.wind={5,0}; previous=next; next=frame(1);
        require(changedAll(previous,next),"Moving cloud shadows did not refresh atmospheric/cloud scattering");
        config.wind=glm::vec2(0); previous=next; next=frame(.1);
        require(sameCoefficients(previous,next),"Stopping wind changed coefficients without moving clouds");
        camera.position.x+=10; previous=next; next=frame(.1);
        require(changedAll(previous,next),"Camera movement reused world-space coefficients from the previous view");
        camera.farDistance*=.5f; previous=next; next=frame(.1);
        require(changedAll(previous,next),"Projection changes reused incorrectly located volume samples");
        lights.shadowContentRevision=0; previous=next; next=frame(.1); next=frame(.1);
        require(next.aerialDispatches==previous.aerialDispatches+2 &&
                next.cloudVolumeDispatches==previous.cloudVolumeDispatches+2 && next.fogDispatches==previous.fogDispatches+2,
                "Unknown shadow revision was cached instead of conservatively refreshed");
        config.godRays=false; next=frame(.1); previous=next; ++lights.shadowContentRevision; next=frame(.1);
        require(sameCoefficients(previous,next) && next.fogDispatches==previous.fogDispatches,
                "Disabled terrain god rays still invalidated volumes on shadow revision changes");
        atmosphere.release(); glDeleteTextures(1,&lights.shadowTexture); glDeleteBuffers(1,&lights.shadowBuffer);
        std::cout<<"PASS volume cache: stable tracked shadows, actual shadowed scattering, fog-only integration, unknown hooks/revisions, cloud motion, camera/projection and disabled god rays\n"<<std::flush;
    }
    void checkCelestialSelection()
    {
        Ge::LightManager lights; Ge::Lights sun,moon;
        sun.shadow=0; moon.shadow=4; moon.index=1; moon.direction={0,-1,0};
        lights.lights={&sun,&moon}; lights.shadowFrames.resize(8);
        ptrClass pc; pc.lightManager=&lights; GraphicsDataMisc graphics; Ge::Camera camera;
        Ge::Atmosphere atmosphere; atmosphere.initialize(&pc,&graphics);
        atmosphere.setSun(&sun); atmosphere.setMoon(&moon);
        auto& s=atmosphere.settings(); s=smallSettings();
        s.clouds=true; s.cloudCoverage=1; s.cloudDensity=.01f; s.cloudWeatherStrength=0;
        s.cloudBaseHeight=1000; s.cloudThickness=3000; s.wind={0,0};
        s.moonIlluminanceScale=.03f; s.environmentBlendSeconds=0;
        std::string error;
        const auto program=Ge::buildShaderProgram({{GL_COMPUTE_SHADER,"../Tests/Atmosphere/runtime_probe.C.glsl",{}}},error);
        require(program!=0,error);
        GLuint output=0; glCreateBuffers(1,&output); glNamedBufferData(output,80,nullptr,GL_DYNAMIC_READ);
        const auto check=[&](bool solar,bool lunar) {
            atmosphere.prepare(&camera,0); atmosphere.computeVolumes(&camera);
            const auto atlas=readTexture(textureAt(5,GL_TEXTURE_BINDING_2D),GL_TEXTURE_2D);
            float minimum[2]={1,1};
            for(std::size_t i=0;i<atlas.size();i+=4)
                for(int c=0;c<2;++c) minimum[c]=std::min(minimum[c],atlas[i+c]);
            require((solar?minimum[0]<.98f:minimum[0]==1.f) &&
                    (lunar?minimum[1]<.98f:minimum[1]==1.f),"Cloud atlas evaluated the inactive celestial light");
            atmosphere.bindLighting(program); glUseProgram(program);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER,0,output); glDispatchCompute(1,1,1);
            glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);
            std::array<float,20> values{}; glGetNamedBufferSubData(output,0,80,values.data());
            require(values[11]==float(solar) && values[15]==float(lunar),"Material selected the wrong celestial light: expected "+std::to_string(solar)+","+std::to_string(lunar)+" got "+std::to_string(values[11])+","+std::to_string(values[15]));
            for(int c=0;c<3;++c) require((solar || values[8+c]==0) && (lunar || values[12+c]==0),
                "Inactive celestial source still adds direct material light");
            for(unsigned layer=0;layer<8;++layer)
                if(layer>=4 || sun.active) require(atmosphere.shadowLayerActive(layer)==(layer<4?solar:lunar),"CPU/GPU light selection differs");
            require(atmosphere.shadowLayerActive(8),"Selection disabled an unrelated light's shadow");
            checkGL("celestial source selection");
        };
        check(true,false);
        sun.direction={-1,-.0001f,0}; check(true,false);
        sun.direction={-1,0,0}; check(true,false); // horizon belongs to the day
        sun.direction={-1,.0001f,0}; check(false,true);
        sun.direction={0,1,0}; check(false,true);
        moon.direction={0,1,0}; check(false,false); // both below horizon
        moon.direction={0,-1,0}; sun.active=false; check(false,true);
        sun.active=true; sun.direction={0,-1,0}; check(true,false);
        s.enabled=false;
        require(atmosphere.shadowLayerActive(0) && atmosphere.shadowLayerActive(4),"Disabled atmosphere kept suppressing shadows");
        atmosphere.release(); glDeleteProgram(program); glDeleteBuffers(1,&output);
        std::cout<<"PASS celestial selection: one source at noon/horizon/night, cloud channels, PBR, shadow cascades, missing sun, both below, disable\n"<<std::flush;
    }
    void checkMoonCloudCache()
    {
        Ge::LightManager lights; Ge::Lights sun,moon;
        sun.direction={0,1,0}; // night: only the moon illuminates the cloud layer
        moon.index=1; moon.direction={0,-1,0}; lights.lights={&sun,&moon};
        ptrClass pc; pc.lightManager=&lights; GraphicsDataMisc graphics; Ge::Camera camera;
        Ge::Atmosphere atmosphere; atmosphere.initialize(&pc,&graphics);
        atmosphere.setSun(&sun); atmosphere.setMoon(&moon);
        auto& s=atmosphere.settings(); s=smallSettings();
        s.clouds=true; s.cloudCoverage=1; s.cloudDensity=.01f; s.cloudWeatherStrength=0;
        s.cloudBaseHeight=1000; s.cloudThickness=3000; s.cloudScale=1200;
        s.wind={0,0}; s.aerialDistance=4096; s.environmentBlendSeconds=0;
        s.moonSurface=false; s.moonIlluminanceScale=0;
        const auto frame=[&]() {
            atmosphere.prepare(&camera,0); atmosphere.computeVolumes(&camera);
            require(atmosphere.active(),"Moon cloud cache fixture inactive: "+atmosphere.error());
            checkGL("moon cloud cache frame"); return atmosphere.stats();
        };
        const auto moonShadow=[&]() {
            const auto atlas=readTexture(textureAt(5,GL_TEXTURE_BINDING_2D),GL_TEXTURE_2D);
            float minimum=1;
            for(std::size_t i=1;i<atlas.size();i+=4) minimum=std::min(minimum,atlas[i]);
            return minimum;
        };
        auto previous=frame();
        require(moonShadow()>.999f,"Zero-energy moon performed a cloud-shadow integration");
        auto next=frame();
        require(next.cloudShadowDispatches==previous.cloudShadowDispatches &&
                next.cloudVolumeDispatches==previous.cloudVolumeDispatches,
                "Unchanged lunar cloud state was not cached");
        s.moonIlluminanceScale=.02f; previous=next; next=frame();
        require(next.cloudShadowDispatches==previous.cloudShadowDispatches+1 &&
                next.cloudVolumeDispatches==previous.cloudVolumeDispatches+1,
                "Zero-to-positive lunar energy left cloud lighting stale");
        require(moonShadow()<.98f,"Activated moon reused the clear cloud-shadow channel");
        previous=next; next=frame();
        require(next.cloudShadowDispatches==previous.cloudShadowDispatches &&
                next.cloudVolumeDispatches==previous.cloudVolumeDispatches,
                "Stable illuminated moon rebuilt cached cloud lighting");
        s.moonIlluminanceScale=.03f; previous=next; next=frame();
        require(next.cloudShadowDispatches==previous.cloudShadowDispatches &&
                next.cloudVolumeDispatches==previous.cloudVolumeDispatches+1,
                "Positive lunar gain change unnecessarily rebuilt cloud visibility or left lighting stale");
        s.aerialDistance*=1.5f; previous=next; next=frame();
        require(next.cloudShadowDispatches==previous.cloudShadowDispatches+1 &&
                next.cloudVolumeDispatches==previous.cloudVolumeDispatches+1,
                "Cloud atlas extent changed without rebuilding dependent coefficients");
        camera.position.x+=1024; previous=next; next=frame();
        require(next.cloudShadowDispatches==previous.cloudShadowDispatches+1 &&
                next.cloudVolumeDispatches==previous.cloudVolumeDispatches+1,
                "Cloud atlas origin changed without rebuilding dependent coefficients");
        s.moonIlluminanceScale=0; previous=next; next=frame();
        require(next.cloudShadowDispatches==previous.cloudShadowDispatches+1 && moonShadow()>.999f,
                "Zero-energy moon retained stale cloud shadow opacity");
        atmosphere.release();
        std::cout<<"PASS lunar cloud cache: zero/positive energy, actual shadow channel, stable reuse, atlas extent/origin\n"<<std::flush;
    }
    void checkCloudWind()
    {
        Ge::LightManager lights; Ge::Lights sun; lights.lights={&sun};
        ptrClass pc; pc.lightManager=&lights; GraphicsDataMisc graphics; Ge::Camera camera;
        camera.position={700,1600,200};
        Ge::Atmosphere atmosphere; atmosphere.initialize(&pc,&graphics);
        auto& config=atmosphere.settings(); config=smallSettings();
        config.clouds=true; config.cloudCoverage=.8f; config.cloudDensity=.002f;
        config.cloudBaseHeight=1000; config.cloudThickness=3000; config.cloudScale=1200;
        config.cloudWeatherScale=8000; config.wind={12,4}; config.aerialDistance=4096;
        config.environmentBlendSeconds=0;
        const auto referenceSettings=config;
        Targets targets;
        GLuint cloudCoefficients=0;
        struct Observation
        {
            glm::vec2 offset{};
            std::vector<float> delta,shadow,extinction,sky;
        };
        const auto frame=[&](double dt,bool images=true)
        {
            atmosphere.prepare(&camera,dt);
            require(atmosphere.active(),"Cloud wind fixture did not render: "+atmosphere.error());
            glBindFramebuffer(GL_FRAMEBUFFER,targets.fbo); glViewport(0,0,32,18);
            atmosphere.renderSky(0);
            const auto delta=textureAt(1,GL_TEXTURE_BINDING_2D);
            const auto shadow=textureAt(14,GL_TEXTURE_BINDING_2D);
            const auto previousCloudDispatches=atmosphere.stats().cloudVolumeDispatches;
            atmosphere.computeVolumes(&camera);
            if(atmosphere.stats().cloudVolumeDispatches>previousCloudDispatches)
                cloudCoefficients=textureAt(11,GL_TEXTURE_BINDING_3D);
            Observation value;
            if(images)
            {
                const auto gpu=uploadedParameters();
                value.offset={gpu[21*4],gpu[21*4+1]}; // std140 CloudOffset XY, unchanged ABI.
                value.delta=readTexture(delta,GL_TEXTURE_2D);
                value.shadow=readTexture(shadow,GL_TEXTURE_2D);
                const auto coefficients=readTexture(cloudCoefficients,GL_TEXTURE_3D);
                for(std::size_t i=3;i<coefficients.size();i+=4) value.extinction.push_back(coefficients[i]);
                value.sky=readTexture(targets.colors[2],GL_TEXTURE_2D);
            }
            checkGL("cloud wind runtime frame");
            return value;
        };
        const auto offsetIs=[](const Observation& value,const glm::vec2& expected)
        {
            require(glm::length(value.offset-expected)<.001f,
                    "Uploaded cloud offset is not the accumulated world displacement");
        };
        const auto unchanged=[](const Observation& a,const Observation& b,const std::string& event)
        {
            closeTextures(a.delta,b.delta,.0002f,event+": visible cloud formations teleported");
            closeTextures(a.shadow,b.shadow,.0002f,event+": cloud shadows teleported");
            closeTextures(a.extinction,b.extinction,1e-7f,event+": cloud density teleported");
            closeTextures(a.sky,b.sky,.0002f,event+": visible sky teleported");
        };
        const auto first=frame(137);
        offsetIs(first,{1644,548});
        require(!atmosphere.stats().environmentReady && atmosphere.stats().captureWorkCompleted==1,
                "Cloud snapshot fixture did not freeze its first 137-second offset");
        config.wind={0,0};
        const auto stopped=frame(0); offsetIs(stopped,first.offset); unchanged(first,stopped,"Stopping wind at 137 seconds");
        const auto stationary=frame(25); offsetIs(stationary,first.offset); unchanged(stopped,stationary,"Waiting with wind stopped");
        config.wind={-7,9};
        const auto resumed=frame(0); offsetIs(resumed,first.offset); unchanged(stationary,resumed,"Changing wind direction at rest");
        const auto moved=frame(2.5); offsetIs(moved,first.offset+glm::vec2(-17.5f,22.5f));
        require(moved.delta!=resumed.delta || moved.extinction!=resumed.extinction,
                "Resumed wind did not advect the actual cloud field");
        config.wind={3,-11};
        const auto redirected=frame(0); offsetIs(redirected,moved.offset); unchanged(moved,redirected,"Changing a moving wind");
        const auto changed=frame(1.25); offsetIs(changed,moved.offset+glm::vec2(3.75f,-13.75f));
        // Make the live field substantially different from the captured one.
        config.wind={120,40}; const auto far=frame(19);
        offsetIs(far,changed.offset+glm::vec2(2280,760));
        config.wind={0,0}; frame(0,false);

        std::string error;
        const auto material=Ge::buildShaderProgram({{GL_COMPUTE_SHADER,"../Tests/Atmosphere/runtime_probe.C.glsl",{}}},error);
        require(material!=0,error);
        Ge::Atmosphere reference; reference.initialize(&pc,&graphics); reference.settings()=referenceSettings;
        reference.settings().iblMaxWorkUnitsPerFrame=64;
        reference.prepare(&camera,137); reference.settings().wind={0,0};
        for(int i=0;i<100 && !reference.stats().environmentReady;++i) reference.prepare(&camera,0);
        require(reference.stats().environmentReady,"Static cloud offset reference did not finish its IBL");
        reference.bindLighting(material);
        const auto frozenIbl=readTexture(textureAt(6,GL_TEXTURE_BINDING_CUBE_MAP),GL_TEXTURE_CUBE_MAP);
        reference.release();
        for(int i=0;i<250 && !atmosphere.stats().environmentReady;++i) frame(0,false);
        require(atmosphere.stats().environmentReady,"Moving clouds starved their initial frozen IBL");
        atmosphere.bindLighting(material);
        closeTextures(readTexture(textureAt(6,GL_TEXTURE_BINDING_CUBE_MAP),GL_TEXTURE_CUBE_MAP),frozenIbl,.003f,
                      "IBL capture read a changing cloud offset instead of its own snapshot");
        const auto firstPublication=atmosphere.stats().environmentUpdates;
        config.iblMaxWorkUnitsPerFrame=16;
        for(int i=0;i<200 && atmosphere.stats().environmentUpdates==firstPublication;++i) frame(0,false);
        require(atmosphere.stats().environmentUpdates>firstPublication,
                "Stopping wind lost the final offset revision before IBL publication");
        atmosphere.bindLighting(material);
        const auto latestIbl=readTexture(textureAt(6,GL_TEXTURE_BINDING_CUBE_MAP),GL_TEXTURE_CUBE_MAP);
        require(latestIbl!=frozenIbl,"Cloud offset fixture did not distinguish the new IBL from the frozen one");

        const auto beforePrecision=frame(0);
        config.wind={1,0}; frame(16777216.0,false);
        config.wind={.125f,0}; frame(1,false);
        config.wind={-1,0}; const auto afterPrecision=frame(16777216.0);
        offsetIs(afterPrecision,beforePrecision.offset+glm::vec2(.125f,0));
        atmosphere.release(); glDeleteProgram(material);
        std::cout<<"PASS cloud wind: accumulated offset at 137s, continuous stop/redirection/restart, real visible/shadow/density fields, frozen IBL offset, latest stopped revision, double-precision residue\n"<<std::flush;
    }
    void checkSettings()
    {
        Ge::Atmosphere original,restored;
        original.settings().enabled=true;
        original.settings().quality=Ge::AtmosphereQuality::High;
        original.settings().cloudCoverage=.73f;
        original.settings().cloudSeed=987654321u;
        original.settings().wind={-12.75f,8.125f};
        original.settings().sunDiskColor={12.f,3.5f,.25f};
        original.settings().environmentBlendSeconds=.75f;
        original.settings().volumeWidth=144;
        original.settings().cloudWeatherScale=70000;
        original.settings().cloudWeatherStrength=.4f;
        original.settings().cloudProfileMix={.1f,.2f,.7f};
        original.settings().cloudViewWidth=224;
        original.settings().iblMaxWorkUnitsPerFrame=9;
        original.settings().iblTileSize=24;
        const auto saved=original.serializeSettings();
        require(restored.loadSettings(saved),"Settings did not accept their own serialization");
        require(restored.serializeSettings()==saved,"Settings round trip lost scalar, vector or HDR values");
        for(const std::string invalid:{"{","[]","{\"cloudCoverage\":\"bad\"}",
                                      "{\"wind\":[1,2,3]}","{\"sunDiskColor\":[1,2]}",
                                      "{\"cloudDensity\":1e400}",
                                      "{\"cloudProfileMix\":[0,1e400,0]}",
                                      "{\"cloudWeatherStrength\":\"bad\"}",
                                      "{\"enabled\":false,\"wind\":[1,\"bad\"]}"})
        {
            require(!restored.loadSettings(invalid),"Invalid settings were accepted: "+invalid);
            require(restored.serializeSettings()==saved,"Failed settings load partially changed the scene");
        }
        require(restored.loadSettings("{\"quality\":999,\"movementThreshold\":-1,\"cloudCoverage\":4}"),
                "Finite out-of-range settings were not clamped");
        require(restored.settings().quality==Ge::AtmosphereQuality::Ultra &&
                restored.settings().movementThreshold==0 && restored.settings().cloudCoverage==1,
                "Settings bounds were not enforced");
        std::cout<<"PASS runtime settings: exact JSON round trip, HDR/vector preservation, atomic invalid-input rejection, bounds\n"<<std::flush;
    }
    void validatePublished(Ge::Atmosphere& atmosphere,GLuint material)
    {
        // bindLighting must work even when another program is current.
        glUseProgram(0);
        atmosphere.bindLighting(material);
        checkGL("binding atmosphere to non-current material");
        require(uniform(material,"uAtmosphereActive")==1,"Completed volume not enabled for material");
        if(!atmosphere.stats().environmentReady)
        {
            require(uniform(material,"uAtmosphereIblActive")==0,"Partially computed IBL was exposed to a material");
            return;
        }
        require(uniform(material,"uAtmosphereIblActive")==1,"Completed IBL not enabled for material");
        readTexture(textureAt(6,GL_TEXTURE_BINDING_CUBE_MAP),GL_TEXTURE_CUBE_MAP);
        const auto prefilter=textureAt(7,GL_TEXTURE_BINDING_CUBE_MAP);
        GLint levels=0; glGetTextureParameteriv(prefilter,GL_TEXTURE_IMMUTABLE_LEVELS,&levels);
        require(levels==5,"Runtime did not allocate all five roughness levels");
        for(int level=0;level<levels;++level) readTexture(prefilter,GL_TEXTURE_CUBE_MAP,level);
        readTexture(textureAt(8,GL_TEXTURE_BINDING_2D),GL_TEXTURE_2D);
        readTexture(textureAt(12,GL_TEXTURE_BINDING_3D),GL_TEXTURE_3D);
        const auto transmission=readTexture(textureAt(13,GL_TEXTURE_BINDING_3D),GL_TEXTURE_3D);
        for(float value:transmission) require(value>=0 && value<=1,"Runtime transmittance escaped [0,1]");
    }
    void checkTexturedMoon()
    {
        Ge::LightManager lights; Ge::Lights sun,moon;
        moon.index=1; moon.direction={0,-1,0}; sun.direction={0,1,0};
        lights.lights={&sun,&moon}; ptrClass pc; pc.lightManager=&lights;
        GraphicsDataMisc graphics; Ge::Camera camera; Targets targets;
        Ge::Atmosphere atmosphere; atmosphere.initialize(&pc,&graphics);
        auto& s=atmosphere.settings(); s=smallSettings();
        s.physicalAtmosphere=false; s.moonSurface=true; s.moonAngularRadius=10;
        s.moonDiskIntensity=1; s.moonIlluminanceScale=1; s.moonNormalStrength=0;
        s.moonElevation=90; s.moonAzimuth=0; s.environmentBlendSeconds=0;
        s.iblMaxWorkUnitsPerFrame=64; s.wind={0,0};
        atmosphere.setSun(&sun); atmosphere.setMoon(&moon);
        const std::uint8_t pixels[8]={128,255,128,51,128,255,128,51};
        require(atmosphere.setMoonSurface(pixels,2,1,.2f),"Moon atlas upload rejected");
        const auto allocated=atmosphere.stats().gpuTextureBytes;
        require(!atmosphere.setMoonSurface(pixels,9000,1,.2f) && atmosphere.stats().gpuTextureBytes==allocated,
            "Invalid moon atlas mutated current resources");
        const auto frame=[&]() { atmosphere.prepare(&camera,0); atmosphere.renderSky(0); checkGL("lunar runtime frame"); };
        const auto settle=[&]() {
            for(int i=0;i<100;++i) {
                frame();
                if(atmosphere.stats().environmentReady && !atmosphere.stats().environmentCaptureActive &&
                    atmosphere.stats().publishedEnvironmentRevision==atmosphere.stats().requestedEnvironmentRevision) return;
            }
            throw std::runtime_error("Lunar capture failed to settle: "+atmosphere.error());
        };
        const auto parameters=[&]() {
            GLint buffer=0; glGetIntegeri_v(GL_UNIFORM_BUFFER_BINDING,6,&buffer);
            std::array<float,168> data{}; glGetNamedBufferSubData(GLuint(buffer),0,sizeof(data),data.data()); return data;
        };
        settle(); const auto full=parameters();
        require(full[9*4]>0 && full[9*4+3]==1,"Textured moon has no phase-driven direct irradiance");
        const float expected=12.f*s.moonDiskColor.x*.2f*(2.f/3.f)*std::pow(std::sin(glm::radians(10.f)),2.f);
        require(std::abs(full[9*4]-expected)<1e-5f,"Moon irradiance differs from Lambert sphere integral");
        const auto cachedSky=atmosphere.stats().skyUpdates;
        const auto revision=atmosphere.stats().requestedEnvironmentRevision;
        camera.position.x+=s.movementThreshold*.25f; frame();
        require(atmosphere.stats().requestedEnvironmentRevision==revision,"Tiny lunar parallax bypassed the IBL movement threshold");
        camera.position.x=10000; settle(); const auto moved=parameters();
        require(atmosphere.stats().requestedEnvironmentRevision>revision,"Lunar IBL ignored camera motion with clouds disabled");
        require(atmosphere.stats().skyUpdates==cachedSky,"Lunar parallax invalidated clear-air LUTs");
        GLint moonBuffer=0; glGetIntegeri_v(GL_UNIFORM_BUFFER_BINDING,7,&moonBuffer);
        std::array<float,8> geometry{}; glGetNamedBufferSubData(GLuint(moonBuffer),0,sizeof(geometry),geometry.data());
        require(std::abs(geometry[0]+10000.f)<.01f,"Moon follows the camera instead of its finite world position");
        require(full[9*4]==moved[9*4],"Directional moon illumination follows camera translation");
        sun.direction={-1,0,0}; settle(); const auto quarter=parameters();
        require(std::abs(quarter[9*4]/full[9*4]-1.f/3.14159265f)<1e-4f,"Incorrect quarter-moon phase");
        sun.direction=moon.direction; settle(); const auto dark=parameters();
        require(dark[9*4]<1e-7f,"New moon lights the terrain");
        sun.direction={0,1,0}; atmosphere.setMoon(nullptr); lights.lights={&sun}; settle();
        require(parameters()[9*4+3]==2,"Unassigned moon should use diffuse IBL without a new light");
        s.iblMaxWorkUnitsPerFrame=1; s.moonRotation=45; frame();
        require(atmosphere.stats().environmentCaptureActive,"Expected a pending lunar capture");
        const auto cancelled=atmosphere.stats().environmentCapturesCancelled;
        require(atmosphere.setMoonSurface(pixels,2,1,.2f),"Atlas replacement failed");
        require(atmosphere.stats().environmentCapturesCancelled==cancelled+1,"Atlas replacement mixed capture epochs");
        s.iblMaxWorkUnitsPerFrame=64; settle();
        s.moonSurface=false; settle(); atmosphere.release();
        require(atmosphere.stats().gpuTextureBytes==0,"Lunar surface/reflection resources leaked");
        std::cout<<"PASS textured moon runtime: upload bounds, phase irradiance, finite parallax, stable LUTs, optional source, immutable captures, toggle/release\n"<<std::flush;
    }
    void run(const std::filesystem::path& root)
    {
        std::filesystem::current_path(root/"ENDURANCE");
        checkSettings();
        checkTexturedMoon();
        checkParameterStreaming();
        checkShaderVariants();
        checkInitialPalette(true,.3f);
        checkInitialPalette(false,.3f);
        checkInitialPalette(true,0);
        checkCloudWind();
        checkVolumeCache();
        checkMoonCloudCache();
        checkCelestialSelection();
        std::string error;
        const GLuint material=Ge::buildShaderProgram({{GL_COMPUTE_SHADER,"../Tests/Atmosphere/runtime_probe.C.glsl",{}}},error);
        require(material!=0,error);
        Ge::LightManager lights;
        Ge::Lights sun,moon;
        moon.index=1; moon.colors={.6f,.7f,1.f}; moon.range=2;
        lights.lights={&sun};
        ptrClass pc; pc.lightManager=&lights;
        GraphicsDataMisc graphics;
        Ge::Camera camera;
        Ge::Atmosphere atmosphere;
        require(atmosphere.settings().quality==Ge::AtmosphereQuality::Ultra,"Ultra is not the initial quality");
        atmosphere.initialize(&pc,&graphics);
        auto& config=atmosphere.settings();
        config.enabled=true; config.quality=Ge::AtmosphereQuality::Low;
        config.cubemapSize=16; config.prefilterSize=16; config.irradianceSize=4;
        config.iblSamples=8; config.skySteps=8; config.cloudLightSteps=2;
        config.volumeWidth=16; config.volumeHeight=9; config.volumeDepth=16;
        config.clouds=false; config.localFog=false; config.godRays=false;
        config.environmentInterval=0; config.environmentBlendSeconds=.08f;
        config.targetGpuMilliseconds=1000;
        Targets targets;
        const auto frame=[&](double dt)
        {
            atmosphere.prepareShadows();
            atmosphere.prepare(&camera,dt);
            require(atmosphere.active(),"Runtime atmosphere inactive: "+atmosphere.error());
            checkGL("prepare atmosphere");
            atmosphere.renderSky(0); checkGL("runtime sky draw");
            const auto cloudDispatches=atmosphere.stats().cloudVolumeDispatches;
            atmosphere.computeVolumes(&camera); checkGL("runtime volume dispatch");
            if(atmosphere.stats().cloudVolumeDispatches>cloudDispatches)
            {
                GLint format=0;
                glGetTextureLevelParameteriv(textureAt(11,GL_TEXTURE_BINDING_3D),0,GL_TEXTURE_INTERNAL_FORMAT,&format);
                require(format==GL_RGBA32F,"Cloud source/extinction no longer share an FP32 RGBA texture");
            }
            validatePublished(atmosphere,material);
            require(atmosphere.stats().iblWorkUnitsThisFrame<=static_cast<unsigned>(std::clamp(config.iblMaxWorkUnitsPerFrame,1,64)),
                    "IBL scheduler exceeded its hard per-frame work-unit limit");
        };
        const auto settle=[&](double dt=.1)
        {
            for(int i=0;i<300;++i)
            {
                frame(dt);
                const auto& state=atmosphere.stats();
                if(state.environmentReady && !state.environmentCaptureActive &&
                    state.publishedEnvironmentRevision==state.requestedEnvironmentRevision)
                { frame(.1); return; }
            }
            throw std::runtime_error("Environment scheduler failed to publish the latest stable revision");
        };
        frame(.016);
        require(!atmosphere.stats().environmentReady && atmosphere.stats().environmentCaptureActive,
                "Startup capture did not remain private across multiple frames");
        settle(0);
        require(atmosphere.stats().hasSun && !atmosphere.stats().hasMoon,"Directional fallback assignment failed");
        require(atmosphere.stats().transmittanceUpdates==1 && atmosphere.stats().multiscatterUpdates==1,"Startup LUT build count incorrect");
        const GLuint brdf=textureAt(8,GL_TEXTURE_BINDING_2D);
        const auto firstIrradiance=readTexture(textureAt(6,GL_TEXTURE_BINDING_CUBE_MAP),GL_TEXTURE_CUBE_MAP);
        require(maximumRGB(firstIrradiance)>0,"Runtime IBL is black with a sun");
        readTexture(targets.colors[2],GL_TEXTURE_2D);
        std::cout<<"PASS runtime startup: real source compiler/cache, GLSL dispatch formats, complete cube+mips, material bindings and HDR sky draw\n"<<std::flush;

        const auto initial=atmosphere.stats();
        frame(.1);
        require(atmosphere.stats().transmittanceUpdates==initial.transmittanceUpdates &&
                atmosphere.stats().multiscatterUpdates==initial.multiscatterUpdates &&
                atmosphere.stats().skyUpdates==initial.skyUpdates &&
                atmosphere.stats().environmentUpdates==initial.environmentUpdates,
                "Stationary cloudless frame rebuilt invariant sky/IBL resources");
        require(atmosphere.stats().aerialDispatches==initial.aerialDispatches,
                "Advancing an invisible cloud offset invalidated a stationary cloudless aerial volume");
        camera.forward={1,0,0}; frame(.1);
        require(atmosphere.stats().skyUpdates==initial.skyUpdates,"View rotation rebuilt world-space sky LUT");
        camera.position.y+=2500; frame(.1);
        require(atmosphere.stats().skyUpdates>initial.skyUpdates,"Altitude did not refresh runtime sky");
        require(atmosphere.stats().transmittanceUpdates==initial.transmittanceUpdates,"Altitude rebuilt transmittance LUT");
        require(textureAt(8,GL_TEXTURE_BINDING_2D)==brdf,"View/altitude change rebuilt BRDF integration");
        const auto beforeGround=atmosphere.stats();
        config.groundAlbedo={.3f,.3f,.3f}; frame(.1);
        require(atmosphere.stats().transmittanceUpdates==beforeGround.transmittanceUpdates &&
                atmosphere.stats().multiscatterUpdates==beforeGround.multiscatterUpdates+1,
                "Ground reflectance did not invalidate MS independently of optical depth");
        std::cout<<"PASS runtime invalidation: stationary and rotated sky reuse, altitude-only sky update, BRDF lifetime\n"<<std::flush;

        // An interruption interval longer than the environment cadence must
        // still advance the previously published blend before rebaking.
        Ge::AtmospherePalette target=atmosphere.displayedPalette();
        target.horizon*=4; target.middle*=4; target.zenith*=4; target.strength=1;
        config.transitionSeconds=.5f; config.clouds=true; config.cloudCoverage=.6f;
        const auto before=atmosphere.displayedPalette();
        atmosphere.setPaletteTarget(target,true);
        for(int i=0;i<160 && Ge::atmospherePaletteDifference(atmosphere.displayedPalette(),target)>=.01f;++i) frame(.2);
        require(Ge::atmospherePaletteDifference(atmosphere.displayedPalette(),before)>.05f,
                "Repeated environment updates starved the displayed palette blend");
        require(Ge::atmospherePaletteDifference(atmosphere.displayedPalette(),target)<.01f,
                "Runtime palette did not converge while camera remained fixed");
        require(textureAt(8,GL_TEXTURE_BINDING_2D)==brdf,"Palette/cloud change rebuilt BRDF LUT");
        config.wind={0,0}; settle();
        const auto beforeCloudMove=atmosphere.stats().environmentUpdates;
        camera.position.x+=50; settle();
        require(atmosphere.stats().environmentUpdates>beforeCloudMove,
                "Moving through static clouds failed to update their parallax");
        std::cout<<"PASS runtime transition: repeated cloudy rebakes do not starve the displayed blend, stationary convergence\n"<<std::flush;

        config.clouds=false; config.environmentBlendSeconds=0;
        settle();
        const auto unmodifiedIbl=maximumRGB(readTexture(textureAt(6,GL_TEXTURE_BINDING_CUBE_MAP),GL_TEXTURE_CUBE_MAP));
        const auto unmodifiedScattering=maximumRGB(readTexture(textureAt(12,GL_TEXTURE_BINDING_3D),GL_TEXTURE_3D));
        require(unmodifiedIbl>0 && unmodifiedScattering>0,"Hook fixture has no lighting");
        atmosphere.setRadianceShader("../Tests/Atmosphere/runtime_custom_radiance.glsl");
        settle();
        const auto customIbl=maximumRGB(readTexture(textureAt(6,GL_TEXTURE_BINDING_CUBE_MAP),GL_TEXTURE_CUBE_MAP));
        const auto customScattering=maximumRGB(readTexture(textureAt(12,GL_TEXTURE_BINDING_3D),GL_TEXTURE_3D));
        require(std::abs(customIbl/unmodifiedIbl-.5f)<.015f &&
                std::abs(customScattering/unmodifiedScattering-.5f)<.015f,
                "Radiance customization was not applied equally to environment and aerial scattering");
        const auto validIrradiance=textureAt(6,GL_TEXTURE_BINDING_CUBE_MAP);
        atmosphere.setRadianceShader("../Tests/Atmosphere/missing_runtime_hook.glsl");
        frame(.1);
        require(!atmosphere.error().empty() && textureAt(6,GL_TEXTURE_BINDING_CUBE_MAP)==validIrradiance,
                "Invalid hook discarded the previously complete environment");
        const auto failedBuilds=atmosphere.stats().programVariantBuilds;
        frame(.1); frame(.1);
        require(atmosphere.stats().programVariantBuilds==failedBuilds,
                "Invalid shader reload was recompiled every frame");
        atmosphere.setRadianceShader(""); settle();
        require(atmosphere.error().empty(),"Valid shader reload did not recover from an invalid hook");
        std::cout<<"PASS runtime radiance hook: same correction in IBL/aerial scattering, complete-state retention on failure and reload recovery\n"<<std::flush;

        lights.lights.clear();
        settle();
        require(!atmosphere.stats().hasSun && !atmosphere.stats().hasMoon,"Removed source remained assigned");
        require(maximumRGB(readTexture(textureAt(6,GL_TEXTURE_BINDING_CUBE_MAP),GL_TEXTURE_CUBE_MAP))==0,
                "Runtime retained phantom source after lights were removed");
        config.fallbackToFirstDirectional=false;
        lights.lights={&moon}; atmosphere.setMoon(&moon);
        settle();
        require(!atmosphere.stats().hasSun && atmosphere.stats().hasMoon,"Moon-only assignment failed");
        require(maximumRGB(readTexture(textureAt(6,GL_TEXTURE_BINDING_CUBE_MAP),GL_TEXTURE_CUBE_MAP))>0,"Moon-only runtime has no IBL");
        std::cout<<"PASS runtime optional sources: source removal, empty scene and moon-only lighting\n"<<std::flush;

        config.physicalAtmosphere=false; config.localFog=true;
        config.fogDensity=1e-5f; config.fogAlbedo={0,0,0}; config.fogBaseHeight=1000000;
        config.aerialDistance=30000;
        atmosphere.setGameFogShader("../Tests/Atmosphere/runtime_game_fog.glsl");
        int fogBindings=0;
        atmosphere.setGameFogBinding([&](unsigned program)
        {
            require(glIsProgram(program),"Game fog callback received an invalid program");
            ++fogBindings;
        });
        frame(.1);
        require(fogBindings==1,"Game fog resource callback did not run once before integration");
        const auto fogT=readTexture(textureAt(13,GL_TEXTURE_BINDING_3D),GL_TEXTURE_3D);
        const float expectedT=std::exp(-3e-5f*config.aerialDistance);
        require(std::abs(fogT[fogT.size()-4]-expectedT)<.001f,
                "Generic and game fog extinction were not added before integration");
        require(maximumRGB(readTexture(textureAt(12,GL_TEXTURE_BINDING_3D),GL_TEXTURE_3D))==0,
                "Purely absorptive hook generated phantom radiance");
        atmosphere.setGameFogBinding({}); atmosphere.setGameFogShader("");
        config.physicalAtmosphere=true; config.localFog=false; frame(.1);
        std::cout<<"PASS runtime game fog hook: caller binding, additive media and analytic pure absorption\n"<<std::flush;

        // Freeze the full physical/cloud input snapshot, then replace the
        // live LUTs and noise with their one-texel disabled resources. The
        // unfinished capture must still reproduce the preceding static sky.
        lights.lights={&sun}; sun.colors={1,1,1}; atmosphere.setSun(&sun); atmosphere.setMoon(nullptr);
        config.fallbackToFirstDirectional=true; config.clouds=true; config.cloudCoverage=.65f;
        config.wind={0,0}; config.iblMaxWorkUnitsPerFrame=16; config.environmentBlendSeconds=0;
        settle(0);
        const auto frozenReference=readTexture(textureAt(6,GL_TEXTURE_BINDING_CUBE_MAP),GL_TEXTURE_CUBE_MAP);
        const auto restoredConfig=config; const auto restoredCamera=camera.position;
        config.iblMaxWorkUnitsPerFrame=1; config.iblTileSize=8;
        atmosphere.invalidate();
        const auto frozenPublication=atmosphere.stats().environmentUpdates;
        frame(0);
        require(atmosphere.stats().captureWorkCompleted==1 && atmosphere.stats().environmentCaptureActive,
                "Capture did not atomically snapshot inputs as its first work unit");
        const auto frozenRevision=atmosphere.stats().captureRevision;
        config.physicalAtmosphere=false; config.clouds=false; config.cloudSeed+=817;
        config.rayleighScattering*=3; config.cloudWeatherStrength=.91f; config.cloudProfileMix={1,0,0};
        sun.colors={.2f,1,.1f}; camera.position.y+=10000;
        for(int i=0;i<250 && atmosphere.stats().environmentUpdates==frozenPublication;++i)
        {
            camera.position.y+=2; // Keep requesting newer revisions: no starvation.
            frame(0);
            if(atmosphere.stats().environmentUpdates==frozenPublication)
                closeTextures(readTexture(textureAt(6,GL_TEXTURE_BINDING_CUBE_MAP),GL_TEXTURE_CUBE_MAP),
                              frozenReference,0,"A partial capture altered published IBL");
        }
        require(atmosphere.stats().environmentUpdates==frozenPublication+1 &&
                atmosphere.stats().publishedEnvironmentRevision==frozenRevision,
                "Continuous camera/settings revisions prevented completion of the frozen job");
        closeTextures(readTexture(textureAt(6,GL_TEXTURE_BINDING_CUBE_MAP),GL_TEXTURE_CUBE_MAP),
                      frozenReference,.003f,"Capture read live LUT/noise/source parameters after its snapshot");
        config=restoredConfig; camera.position=restoredCamera; sun.colors={1,1,1}; settle(0);
        require(atmosphere.stats().gpuTextureBytes>atmosphere.stats().captureTextureBytes && atmosphere.stats().captureTextureBytes>0,
                "Texture accounting omitted the private capture or published resources");
        std::cout<<"PASS scheduler freeze: UBO, LUTs and noise survive live replacement; continuous revisions cannot starve completion; partial work stays private\n"<<std::flush;

        // A caller may rewrite a cube in place immediately after incrementing
        // its revision. Copying only its handle would give different face ages.
        GLuint external=0; glCreateTextures(GL_TEXTURE_CUBE_MAP,1,&external);
        glTextureStorage2D(external,1,GL_RGB32F,16,16);
        glTextureParameteri(external,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
        glTextureParameteri(external,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        const float firstExternal[4]={2,4,8,1},secondExternal[4]={10,6,2,1};
        glClearTexImage(external,0,GL_RGBA,GL_FLOAT,firstExternal);
        lights.lights.clear(); config.fallbackToFirstDirectional=false;
        config.physicalAtmosphere=false; config.clouds=false; config.localFog=false;
        config.iblMaxWorkUnitsPerFrame=1; config.transitionSeconds=0;
        Ge::AtmospherePalette neutral; neutral.strength=0; neutral.exposure=1;
        atmosphere.setPaletteTarget(neutral,true); atmosphere.setExternalEnvironment(external,1);
        const auto externalPublication=atmosphere.stats().environmentUpdates;
        frame(0);
        require(atmosphere.stats().captureWorkCompleted==1,"External cube was not snapped before slicing its faces");
        glClearTexImage(external,0,GL_RGBA,GL_FLOAT,secondExternal);
        atmosphere.setExternalEnvironment(external,2); neutral.exposure=.5f; atmosphere.setPaletteTarget(neutral,true);
        for(int i=0;i<250 && atmosphere.stats().environmentUpdates==externalPublication;++i) frame(0);
        require(atmosphere.stats().environmentUpdates==externalPublication+1,"External snapshot never published");
        constantCube(textureAt(6,GL_TEXTURE_BINDING_CUBE_MAP),{2,4,8});
        settle(0);
        constantCube(textureAt(6,GL_TEXTURE_BINDING_CUBE_MAP),{5,3,1});
        std::cout<<"PASS scheduler external snapshot: in-place HDR cube revision and artistic retarget never mix six face ages\n"<<std::flush;

        // Resize and shader reload cancel only private work, preserving the
        // complete visible/lighting set until the replacement is finished.
        atmosphere.invalidate(); frame(0);
        const auto retained=textureAt(6,GL_TEXTURE_BINDING_CUBE_MAP);
        const auto retainedValues=readTexture(retained,GL_TEXTURE_CUBE_MAP);
        const auto cancelled=atmosphere.stats().environmentCapturesCancelled;
        config.cubemapSize=32; config.prefilterSize=32; config.iblTileSize=16; frame(0);
        require(atmosphere.stats().environmentCapturesCancelled>cancelled && glIsTexture(retained) &&
                textureAt(6,GL_TEXTURE_BINDING_CUBE_MAP)==retained,"Resize exposed or destroyed an incomplete environment");
        closeTextures(readTexture(retained,GL_TEXTURE_CUBE_MAP),retainedValues,0,"Resize modified the completed IBL");
        const auto afterResize=atmosphere.stats().environmentCapturesCancelled;
        atmosphere.reloadShaders(); frame(0);
        require(atmosphere.stats().environmentCapturesCancelled>afterResize && glIsTexture(retained),
                "Reload did not safely cancel the private capture");
        settle(0); constantCube(textureAt(6,GL_TEXTURE_BINDING_CUBE_MAP),{5,3,1});
        std::cout<<"PASS scheduler cancellation: reload and resolution change retain the complete environment, then publish a complete replacement\n"<<std::flush;

        // The millisecond target influences submissions independently from the
        // absolute work-unit cap. No blocking query is needed to choose a batch.
        config.iblMaxWorkUnitsPerFrame=8; config.targetGpuMilliseconds=.01f;
        atmosphere.invalidate(); frame(0);
        require(atmosphere.stats().iblWorkUnitsThisFrame==1,"Tiny soft budget did not reduce submission to the progress floor");
        config.targetGpuMilliseconds=1000; frame(0);
        require(atmosphere.stats().iblWorkUnitsThisFrame>1 && atmosphere.stats().iblWorkUnitsThisFrame<=8,
                "GPU target does not influence the submitted work batch");
        settle(0);
        const auto disabledDispatches=atmosphere.stats();
        for(int i=0;i<3;++i) frame(.1);
        require(atmosphere.stats().aerialDispatches==disabledDispatches.aerialDispatches &&
                atmosphere.stats().cloudVolumeDispatches==disabledDispatches.cloudVolumeDispatches &&
                atmosphere.stats().cloudShadowDispatches==disabledDispatches.cloudShadowDispatches &&
                atmosphere.stats().visibleCloudDispatches==disabledDispatches.visibleCloudDispatches &&
                atmosphere.stats().fogDispatches==disabledDispatches.fogDispatches,
                "Disabled media still dispatched atmosphere/cloud/fog work");
        GLint neutralWidth=0;
        glGetTextureLevelParameteriv(textureAt(13,GL_TEXTURE_BINDING_3D),0,GL_TEXTURE_WIDTH,&neutralWidth);
        require(neutralWidth==1,"Absent media retained full-sized integration volumes");
        require(maximumRGB(readTexture(textureAt(12,GL_TEXTURE_BINDING_3D),GL_TEXTURE_3D))==0,
                "Disabled media did not restore zero scattering");
        for(float v:readTexture(textureAt(13,GL_TEXTURE_BINDING_3D),GL_TEXTURE_3D)) require(v==1,"Disabled media lost identity transmission");
        config.clouds=true; config.cloudCoverage=0; frame(.1);
        require(atmosphere.stats().cloudVolumeDispatches==disabledDispatches.cloudVolumeDispatches,
                "Zero cloud coverage dispatched a cloud coefficient volume");
        config.cloudCoverage=1; frame(.1);
        require(atmosphere.stats().cloudVolumeDispatches>disabledDispatches.cloudVolumeDispatches,
                "Enabling a previously empty cloud layer did not rebuild its coefficients");
        config.clouds=false; frame(.1);
        glGetTextureLevelParameteriv(textureAt(13,GL_TEXTURE_BINDING_3D),0,GL_TEXTURE_WIDTH,&neutralWidth);
        require(neutralWidth==1,"Disabling clouds did not restore small identity resources");
        std::cout<<"PASS scheduler budget/disabled effects: bounded submissions, active soft target, packed FP32 cloud extinction, skipped passes and neutral one-voxel resources\n"<<std::flush;

        {
            Ge::Atmosphere startup;
            const auto priorIrradiance=textureAt(6,GL_TEXTURE_BINDING_CUBE_MAP);
            const auto priorPrefilter=textureAt(7,GL_TEXTURE_BINDING_CUBE_MAP);
            startup.initialize(&pc,&graphics); startup.settings()=config;
            startup.settings().iblMaxWorkUnitsPerFrame=1;
            Ge::AtmospherePalette startupPalette; startupPalette.strength=0; startupPalette.exposure=.25f;
            startup.setPaletteTarget(startupPalette,true); startup.setExternalEnvironment(external,1);
            startup.prepare(&camera,0); startup.renderSky(0); startup.computeVolumes(&camera);
            require(startup.active() && !startup.stats().environmentReady && startup.stats().captureWorkCompleted==1,
                    "External startup fixture did not pause before its first complete IBL");
            const auto initialSky=readTexture(targets.colors[2],GL_TEXTURE_2D);
            for(std::size_t i=0;i<initialSky.size();++i)
                require(std::abs(initialSky[i]-(i%4==3?1.f:glm::vec3(2.5f,1.5f,.5f)[static_cast<int>(i%4)]))<.005f,
                        "A preassigned external sky was black or lacked its artistic correction during startup");
            startup.bindLighting(material);
            require(uniform(material,"uAtmosphereIblActive")==0,"External startup exposed incomplete IBL");
            require(textureAt(6,GL_TEXTURE_BINDING_CUBE_MAP)==priorIrradiance &&
                    textureAt(7,GL_TEXTURE_BINDING_CUBE_MAP)==priorPrefilter,
                    "Initial atmosphere capture replaced the engine's existing complete IBL with empty handles");
            startup.release(); require(glIsTexture(external),"Startup release deleted caller-owned environment");
        }
        frame(0);
        std::cout<<"PASS external startup: complete caller sky renders with artistic correction while initial IBL remains private\n"<<std::flush;

        const auto animatedHook=std::filesystem::path(std::getenv("ENDURANCE_SHADER_CACHE_DIR")).parent_path()/"animated_game_fog.glsl";
        {
            std::ofstream file(animatedHook);
            file<<"vec4 geGameFog(vec3 p) { return vec4(vec3(0.0),1e-6*(1.0+geAtmCamera.w)); }\n";
        }
        config.localFog=true; config.fogDensity=0; config.aerialDistance=10000;
        atmosphere.setGameFogBinding({}); atmosphere.setGameFogShader(animatedHook.string());
        frame(.1);
        require(atmosphere.error().empty(),"Animated fog fixture did not compile: "+atmosphere.error());
        const auto animatedBefore=readTexture(textureAt(13,GL_TEXTURE_BINDING_3D),GL_TEXTURE_3D);
        const auto animationUpdates=atmosphere.stats().volumeUpdates;
        const auto animationAerial=atmosphere.stats().aerialDispatches;
        const auto animationCloud=atmosphere.stats().cloudVolumeDispatches;
        frame(5);
        require(atmosphere.stats().volumeUpdates==animationUpdates+1,
                "A time-dependent game fog shader without a binding callback was incorrectly cached");
        require(atmosphere.stats().aerialDispatches==animationAerial &&
                atmosphere.stats().cloudVolumeDispatches==animationCloud,
                "Animated game fog rebuilt unrelated physical/cloud coefficients");
        const auto animatedAfter=readTexture(textureAt(13,GL_TEXTURE_BINDING_3D),GL_TEXTURE_3D);
        require(animatedAfter[animatedAfter.size()-4]<animatedBefore[animatedBefore.size()-4]-.005f,
                "A game fog shader without a callback did not animate at a stationary camera");
        atmosphere.setGameFogShader(""); config.localFog=false; frame(0);
        std::cout<<"PASS generic fog shader: time-dependent medium animates without a texture-binding callback\n"<<std::flush;

        config.enabled=false;
        atmosphere.prepare(&camera,.1); atmosphere.bindLighting(material);
        require(!atmosphere.active() && uniform(material,"uAtmosphereActive")==0 && uniform(material,"uAtmosphereIblActive")==0,
                "Disabling atmosphere left material state active");
        config.enabled=true; frame(.1);
        glFinish(); // Test instrumentation only; runtime polls nonblocking queries.
        frame(.016);
        require(atmosphere.stats().gpuSamplesAvailable,"GPU timer results were never collected");
        for(double value:{atmosphere.stats().skyMilliseconds,atmosphere.stats().lutMilliseconds,
                          atmosphere.stats().iblMilliseconds,atmosphere.stats().aerialMilliseconds,
                          atmosphere.stats().fogMilliseconds,atmosphere.stats().cloudMilliseconds})
            require(std::isfinite(value) && value>=0,"Invalid GPU timing sample");
        const auto ownedTexture=textureAt(6,GL_TEXTURE_BINDING_CUBE_MAP);
        atmosphere.release(); checkGL("runtime release");
        require(!atmosphere.active() && !glIsTexture(ownedTexture) && !glIsTexture(brdf),"Release leaked published resources");
        require(glIsTexture(external) && atmosphere.stats().gpuTextureBytes==0,"Release deleted caller texture or leaked tracked textures");
        atmosphere.initialize(&pc,&graphics); settle(0); atmosphere.release();
        glDeleteTextures(1,&external);
        glDeleteProgram(material); checkGL("runtime restart/release");
        std::cout<<"PASS runtime lifecycle: disabled material state, re-enable, delayed timer collection, release and restart\n"<<std::flush;
    }
}

extern "C" __attribute__((visibility("default"))) int atmosphere_runtime_tests(const char* root)
{
    try
    {
        glewExperimental=GL_TRUE;
        const auto status=glewInit();
        require(status==GLEW_OK || status==GLEW_ERROR_NO_GLX_DISPLAY,"GLEW initialization failed");
        while(glGetError()!=GL_NO_ERROR) {}
        require(GLEW_VERSION_4_5 && glCreateTextures,"Atmosphere requires OpenGL 4.5 DSA");
        run(root);
        return 0;
    }
    catch(const std::exception& error)
    {
        std::cerr<<"FAIL atmosphere runtime: "<<error.what()<<'\n';
        return 1;
    }
}
