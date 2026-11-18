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
#include <cmath>
#include <filesystem>
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
                glTextureStorage2D(colors[i],1,GL_RGBA32F,32,18);
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
        const auto saved=original.serializeSettings();
        require(restored.loadSettings(saved),"Settings did not accept their own serialization");
        require(restored.serializeSettings()==saved,"Settings round trip lost scalar, vector or HDR values");
        for(const std::string invalid:{"{","[]","{\"cloudCoverage\":\"bad\"}",
                                      "{\"wind\":[1,2,3]}","{\"sunDiskColor\":[1,2]}",
                                      "{\"cloudDensity\":1e400}",
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
    void run(const std::filesystem::path& root)
    {
        std::filesystem::current_path(root/"ENDURANCE");
        checkSettings();
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
        config.environmentInterval=.05f; config.environmentBlendSeconds=.08f;
        Targets targets;
        const auto frame=[&](double dt)
        {
            atmosphere.prepareShadows();
            atmosphere.prepare(&camera,dt);
            require(atmosphere.active(),"Runtime atmosphere inactive: "+atmosphere.error());
            checkGL("prepare atmosphere");
            atmosphere.renderSky(0); checkGL("runtime sky draw");
            atmosphere.computeVolumes(&camera); checkGL("runtime volume dispatch");
            validatePublished(atmosphere,material);
        };
        frame(.016);
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
        for(int i=0;i<8;++i) frame(.2);
        require(Ge::atmospherePaletteDifference(atmosphere.displayedPalette(),before)>.05f,
                "Repeated environment updates starved the displayed palette blend");
        require(Ge::atmospherePaletteDifference(atmosphere.displayedPalette(),target)<.01f,
                "Runtime palette did not converge while camera remained fixed");
        require(textureAt(8,GL_TEXTURE_BINDING_2D)==brdf,"Palette/cloud change rebuilt BRDF LUT");
        config.wind={0,0}; frame(.1);
        const auto beforeCloudMove=atmosphere.stats().environmentUpdates;
        camera.position.x+=50; frame(.1);
        require(atmosphere.stats().environmentUpdates>beforeCloudMove,
                "Moving through static clouds failed to update their parallax");
        std::cout<<"PASS runtime transition: repeated cloudy rebakes do not starve the displayed blend, stationary convergence\n"<<std::flush;

        config.clouds=false; config.environmentBlendSeconds=0;
        for(int i=0;i<2;++i) frame(.1);
        const auto unmodifiedIbl=maximumRGB(readTexture(textureAt(6,GL_TEXTURE_BINDING_CUBE_MAP),GL_TEXTURE_CUBE_MAP));
        const auto unmodifiedScattering=maximumRGB(readTexture(textureAt(12,GL_TEXTURE_BINDING_3D),GL_TEXTURE_3D));
        require(unmodifiedIbl>0 && unmodifiedScattering>0,"Hook fixture has no lighting");
        atmosphere.setRadianceShader("../Tests/Atmosphere/runtime_custom_radiance.glsl");
        frame(.1);
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
        atmosphere.setRadianceShader(""); frame(.1);
        require(atmosphere.error().empty(),"Valid shader reload did not recover from an invalid hook");
        std::cout<<"PASS runtime radiance hook: same correction in IBL/aerial scattering, complete-state retention on failure and reload recovery\n"<<std::flush;

        lights.lights.clear();
        for(int i=0;i<3;++i) frame(.1);
        require(!atmosphere.stats().hasSun && !atmosphere.stats().hasMoon,"Removed source remained assigned");
        require(maximumRGB(readTexture(textureAt(6,GL_TEXTURE_BINDING_CUBE_MAP),GL_TEXTURE_CUBE_MAP))==0,
                "Runtime retained phantom source after lights were removed");
        config.fallbackToFirstDirectional=false;
        lights.lights={&moon}; atmosphere.setMoon(&moon);
        for(int i=0;i<3;++i) frame(.1);
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
        atmosphere.initialize(&pc,&graphics); frame(.1); atmosphere.release();
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
