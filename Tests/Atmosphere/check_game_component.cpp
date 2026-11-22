// Production EnduranceAtmosphere.cpp, Atmosphere.cpp and their shader compiler
// are linked unchanged. Fixtures replace ownership, ImGui and the input clock.
#include "glcore.hpp"
#include "EnduranceAtmosphere.hpp"
#include "Atmosphere.hpp"
#include "FixtureServices.hpp"
#include "GraphicsDataMisc.hpp"
#include "ProceduralGeneration.hpp"
#include "hv/cpputil/json.hpp"
#include <algorithm>
#include <array>
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

    GLuint textureAt(unsigned unit)
    {
        glActiveTexture(GL_TEXTURE0+unit);
        GLint texture=0; glGetIntegerv(GL_TEXTURE_BINDING_2D,&texture);
        glActiveTexture(GL_TEXTURE0);
        return static_cast<GLuint>(texture);
    }

    std::vector<float> readTexture(GLuint texture)
    {
        require(texture!=0,"Game fog did not bind a complete texture");
        GLint width=0,height=0,depth=0;
        glGetTextureLevelParameteriv(texture,0,GL_TEXTURE_WIDTH,&width);
        glGetTextureLevelParameteriv(texture,0,GL_TEXTURE_HEIGHT,&height);
        glGetTextureLevelParameteriv(texture,0,GL_TEXTURE_DEPTH,&depth);
        const std::size_t size=static_cast<std::size_t>(width)*height*std::max(depth,1)*4;
        require(size>0,"Game fog texture has no storage");
        std::vector<float> result(size,std::numeric_limits<float>::quiet_NaN());
        // Blocking readback is confined to tests. The production component only
        // reads climate results after a zero-timeout fence poll has completed.
        glGetTextureImage(texture,0,GL_RGBA,GL_FLOAT,static_cast<GLsizei>(size*sizeof(float)),result.data());
        for(float value:result) require(std::isfinite(value),"Game fog texture contains invalid values");
        checkGL("game fog texture readback");
        return result;
    }

    struct Frame
    {
        std::array<float,3> offset{};
        GLuint mediumTexture=0,shapeTexture=0;
        std::vector<float> medium,shape,transmission;
    };

    float difference(const std::vector<float>& a,const std::vector<float>& b)
    {
        require(a.size()==b.size(),"Compared fog textures have different sizes");
        float result=0;
        for(std::size_t i=0;i<a.size();++i) result=std::max(result,std::abs(a[i]-b[i]));
        return result;
    }

    void expectOffset(const Frame& frame,const std::array<float,3>& expected,const std::string& label)
    {
        for(int i=0;i<3;++i)
            require(std::abs(frame.offset[i]-expected[i])<1e-5f,
                    label+": axis "+std::to_string(i)+" was "+std::to_string(frame.offset[i])+
                    ", expected "+std::to_string(expected[i]));
    }

    void run(const std::filesystem::path& root)
    {
        std::filesystem::current_path(root/"ENDURANCE");
        Ge::LightManager lights;
        Ge::Lights sun;
        lights.lights={&sun};
        Ge::Camera camera;
        camera.position={0,60,0};
        Ge::CameraManager cameras; cameras.current=&camera;
        Ge::GraphiquePipelineManager pipeline;
        Ge::Time clock;
        Ge::Atmosphere atmosphere;
        Ge::RenderingEngine renderer; renderer.atmosphere=&atmosphere;
        SceneData scene; scene.dlight={&sun};
        Ge::Editor editor; editor.scene=&scene; Ge::Editor::current=&editor;
        ptrClass pc;
        pc.lightManager=&lights; pc.cameraManager=&cameras;
        pc.graphiquePipelineManager=&pipeline; pc.renderingEngine=&renderer; pc.time=&clock;
        Ge::Engine::services=&pc;
        GraphicsDataMisc graphics;
        atmosphere.initialize(&pc,&graphics);
        auto& config=atmosphere.settings();
        config.quality=Ge::AtmosphereQuality::Low;
        config.cubemapSize=16; config.prefilterSize=16; config.irradianceSize=4;
        config.iblSamples=8; config.skySteps=8; config.cloudLightSteps=2;
        config.volumeWidth=16; config.volumeHeight=9; config.volumeDepth=16;
        config.physicalAtmosphere=false; config.clouds=false; config.godRays=false;
        config.localFog=true; config.fogDensity=0; config.aerialDistance=2048;
        config.measureGpu=false;
        ProceduralGeneration terrain(scene);
        terrain.config.field.biomeOverride=14;
        pipeline.addCustomRenderer(&terrain);

        EnduranceAtmosphereComponent component;
        const auto emptyShadowToken=component.shadowContentRevision();
        const auto validateEmptyCaster=[&]()
        {
            require(emptyShadowToken!=0 && component.shadowContentRevision()==emptyShadowToken &&
                    component.shadowCasterBoundsRevision()==emptyShadowToken,
                    "Atmosphere input-only renderer invalidated its empty shadow content");
            for(unsigned layer=0;layer<4;++layer)
            {
                glm::mat4 clip(1); clip[3][0]=float(layer*1700);
                require(component.shadowLayerContentRevision(layer,clip)==emptyShadowToken,
                        "Atmosphere input-only renderer marked a cascade as unknown/changing");
            }
            std::vector<Ge::ShadowCasterBounds> casters;
            component.appendShadowCasterBounds(casters);
            require(casters.empty(),"Atmosphere inputs unexpectedly published shadow geometry");
        };
        validateEmptyCaster();
        component.fogWind={{20.0f,1.5f,-7.5f}};
        component.fogAtlasResolution=32;
        component.start();
        require(component.palettes.size()==26,"Actual component failed to load the production palette asset");
        component.palettes[14].fogNoiseAmount=1;
        component.palettes[14].fogDensity=.001f;
        const auto saved=component.serialize();
        const auto json=nlohmann::json::parse(saved);
        require(json.contains("fogWind"),"Component REGISTER omitted fogWind");
        require(json.contains("moonSurfaceFile") && json["moonSurfaceFile"]==component.moonSurfaceFile,
                "Component REGISTER omitted the baked lunar surface path");
        require(json.contains("moonModelFile") && json["moonModelFile"]==component.moonModelFile,
                "Component REGISTER omitted the baked lunar model path");
        require(json["fogWind"]==nlohmann::json({20.0f,1.5f,-7.5f}),"Component serializer lost fog wind axes");
        {
            EnduranceAtmosphereComponent restored;
            restored.load(saved);
            require(restored.shadowContentRevision()!=0 && restored.shadowContentRevision()!=emptyShadowToken &&
                    restored.shadowCasterBoundsRevision()==restored.shadowContentRevision(),
                    "New atmosphere component reused another instance's shadow identity");
            require(restored.fogWind==component.fogWind,"Component wind JSON round trip failed");
            require(restored.moonSurfaceFile==component.moonSurfaceFile,"Lunar surface path failed JSON round trip");
            require(restored.moonModelFile==component.moonModelFile,"Lunar model path failed JSON round trip");
            require(restored.palettes.size()==26 && restored.palettes[14].fogDensity==.001f,
                    "Component JSON round trip lost the actual fog profile");
            EnduranceAtmosphereComponent legacy;
            legacy.load("{\"fogAtlasResolution\":64}");
            require(legacy.shadowContentRevision()!=0 && legacy.shadowContentRevision()!=emptyShadowToken &&
                    legacy.shadowContentRevision()!=restored.shadowContentRevision(),
                    "Legacy/new component shadow identities collided");
            require(legacy.fogWind==std::array<float,3>{{4,0,1}},"Older scenes lost default game fog wind");
            require(legacy.moonModelFile=="../ENDURANCE/Ressources/Model/Moon/Baked/moon.glb",
                    "Older scenes lost the default baked lunar model path");
        }
        std::cout<<"PASS game component JSON: real registration/serialize/load, wind XYZ and legacy defaults\n"<<std::flush;

        const auto prepare=[&](float dt)
        {
            clock.delta=dt;
            component.prepareRenderer();
            validateEmptyCaster();
            atmosphere.prepare(&camera,dt);
            checkGL("game component + atmosphere preparation");
        };
        const auto awaitEnvironment=[&]()
        {
            // Production capture/filtering is budgeted across render frames.
            // Advance those transactions without advancing the fog phase or
            // bypassing the actual game callback / engine preparation path.
            for(int attempt=0;attempt<300;++attempt)
            {
                prepare(0);
                if(atmosphere.active() && atmosphere.stats().environmentReady) return;
            }
            throw std::runtime_error("Game atmosphere did not publish an environment within 300 frames: "+atmosphere.error());
        };
        const auto sampleVolume=[&]()
        {
            require(atmosphere.active(),"Game atmosphere inactive: "+atmosphere.error());
            atmosphere.computeVolumes(&camera);
            checkGL("game component + atmosphere volume dispatch");
            GLint program=0,transmission=0;
            glGetIntegerv(GL_CURRENT_PROGRAM,&program);
            const auto offsetLocation=glGetUniformLocation(program,"uGameFogWindOffset");
            require(offsetLocation>=0,"Real volume shader did not include ENDURANCE game_fog.glsl");
            Frame result;
            glGetUniformfv(program,offsetLocation,result.offset.data());
            result.mediumTexture=textureAt(6); result.shapeTexture=textureAt(7);
            result.medium=readTexture(result.mediumTexture); result.shape=readTexture(result.shapeTexture);
            glGetIntegeri_v(GL_IMAGE_BINDING_NAME,1,&transmission);
            result.transmission=readTexture(static_cast<GLuint>(transmission));
            return result;
        };
        const auto frame=[&](float dt)
        {
            prepare(dt);
            return sampleVolume();
        };

        awaitEnvironment();
        const auto initial=frame(0);
        expectOffset(initial,{{0,0,0}},"initial wind phase");
        const auto moving=frame(4);
        expectOffset(moving,{{80,6,-30}},"stationary camera animation");
        require(initial.mediumTexture==moving.mediumTexture && initial.shapeTexture==moving.shapeTexture,
                "Wind unnecessarily replaced world climate atlas textures");
        require(initial.medium==moving.medium && initial.shape==moving.shape,
                "Wind moved or modified the world climate atlas");
        require(difference(initial.transmission,moving.transmission)>1e-5f,
                "Actual fog volume stayed static with nonzero wind at a stationary camera");
        std::cout<<"PASS game fog render path: stationary camera changes real volume transmission; world atlas is unchanged\n"<<std::flush;

        component.stop(); component.start();
        validateEmptyCaster();
        // A complete prior environment may survive a component restart. The
        // current game resources still prepare once before a frame is sampled.
        awaitEnvironment();
        frame(0);
        frame(1); frame(1); frame(1);
        const auto subdivided=frame(1);
        expectOffset(subdivided,moving.offset,"frame subdivision invariance");
        require(difference(subdivided.transmission,moving.transmission)<2e-5f,
                "Fog transport depends on subdivision of the same elapsed time");
        component.fogWind={{-3,.25f,2}};
        const auto changed=frame(0);
        expectOffset(changed,subdivided.offset,"editing wind must not reset phase");
        const auto advanced=frame(.5f);
        expectOffset(advanced,{{78.5f,6.125f,-29}},"edited wind integration");
        component.fogWind={{0,0,0}};
        const auto stopped=frame(120);
        expectOffset(stopped,advanced.offset,"zero wind freezes phase");
        require(stopped.transmission==advanced.transmission,"Zero wind altered local fog coefficients");
        std::cout<<"PASS atmosphere empty shadow hook: nonzero stable bounds/content/cascade tokens through preparation, restart and weather edits; distinct instance identities\n"<<std::flush;
        std::cout<<"PASS game fog time integration: equal elapsed time, phase-continuous wind edits and zero-wind freeze\n"<<std::flush;

        component.fogWind={{1,2,3}};
        expectOffset(frame(-1),stopped.offset,"negative delta");
        expectOffset(frame(std::numeric_limits<float>::quiet_NaN()),stopped.offset,"NaN delta");
        expectOffset(frame(std::numeric_limits<float>::infinity()),stopped.offset,"infinite delta");
        config.enabled=false;
        clock.delta=2; component.prepareRenderer();
        config.enabled=true;
        const auto reenabled=frame(0);
        expectOffset(reenabled,{{80.5f,10.125f,-23}},"render time while disabled");
        terrain.initialized=false;
        clock.delta=2; component.prepareRenderer();
        terrain.initialized=true;
        expectOffset(frame(0),{{82.5f,14.125f,-17}},"render time across terrain rebuild");
        component.fogWind={{std::numeric_limits<float>::quiet_NaN(),500,-500}};
        expectOffset(frame(.25f),{{82.5f,39.125f,-42}},"sanitized and bounded wind axes");
        std::cout<<"PASS game fog robustness: invalid delta rejection, disabled/rebuild continuity, finite bounded wind\n"<<std::flush;

        // Isolate the game callback from all engine cache keys. No engine
        // prepare occurs between these integrations, so the current camera,
        // atmosphere UBO, lights and cloud settings remain identical. The
        // caller-owned animated medium must still force a fresh integration.
        component.fogWind={{20,0,-10}};
        const auto cachedBaseline=frame(0);
        const auto beforeGameOnlyUpdate=atmosphere.stats().volumeUpdates;
        clock.delta=4; component.prepareRenderer();
        const auto gameOnlyUpdate=sampleVolume();
        require(atmosphere.stats().volumeUpdates==beforeGameOnlyUpdate+1,
                "Engine incorrectly reused a cached volume despite an animated game fog callback");
        expectOffset(gameOnlyUpdate,{{cachedBaseline.offset[0]+80,cachedBaseline.offset[1],cachedBaseline.offset[2]-40}},
                     "game-only wind advancement");
        require(gameOnlyUpdate.mediumTexture==cachedBaseline.mediumTexture &&
                gameOnlyUpdate.shapeTexture==cachedBaseline.shapeTexture &&
                gameOnlyUpdate.medium==cachedBaseline.medium && gameOnlyUpdate.shape==cachedBaseline.shape,
                "Game-only wind advancement changed the stationary climate atlas");
        require(difference(gameOnlyUpdate.transmission,cachedBaseline.transmission)>1e-5f,
                "Unchanged engine parameters froze the actual game fog animation");
        std::cout<<"PASS game fog cache contract: animated caller medium reintegrates with unchanged engine parameters\n"<<std::flush;

        const auto bytesBeforeMoon=atmosphere.stats().gpuTextureBytes;
        const auto modelPath=component.moonModelFile;
        component.moonModelFile.clear(); // Explicitly retain and verify the legacy fallback.
        config.moonSurface=true; clock.delta=0; component.prepareRenderer();
        require(atmosphere.stats().gpuTextureBytes>=bytesBeforeMoon+2048u*1024u*4u,
                "Game component did not lazily load the production lunar surface");
        component.moonModelFile=modelPath; prepare(0);
        require(atmosphere.stats().moonMeshGpuBytes>0 && atmosphere.stats().moonImageUpdates>0,
                "Game component did not load/render the pushed lunar GLB");
        const auto meshBytes=atmosphere.stats().moonMeshGpuBytes;
        const auto imageUpdates=atmosphere.stats().moonImageUpdates;
        const auto shadowUpdates=atmosphere.stats().moonShadowUpdates;
        prepare(0);
        require(atmosphere.stats().moonImageUpdates==imageUpdates &&
                atmosphere.stats().moonShadowUpdates==shadowUpdates,
                "Game component reuploaded an unchanged lunar asset");
        component.moonModelFile="missing-lunar-model-for-regression.glb"; prepare(0);
        require(atmosphere.stats().moonMeshGpuBytes==meshBytes &&
                atmosphere.stats().moonImageUpdates==imageUpdates,
                "Failed lunar replacement discarded/rebuilt the last good mesh");
        component.moonModelFile.clear(); prepare(0);
        require(atmosphere.stats().moonMeshGpuBytes==0 &&
                atmosphere.stats().gpuTextureBytes>=bytesBeforeMoon+2048u*1024u*4u,
                "Explicit legacy fallback did not release the mesh and restore its atlas");
        std::cout<<"PASS game lunar assets: model/legacy serialization, lazy GLB load, cache reuse, failed replacement and fallback\n"<<std::flush;
        component.stop(); component.stop();
        require(pipeline.getCustomRenderer().size()==1,"Game component stop leaked its renderer callback");
        atmosphere.release();
        Ge::Engine::services=nullptr; Ge::Editor::current=nullptr;
        checkGL("game atmosphere release");
    }
}

extern "C" __attribute__((visibility("default"))) int game_atmosphere_runtime_tests(const char* root)
{
    try
    {
        glewExperimental=GL_TRUE;
        const auto status=glewInit();
        require(status==GLEW_OK || status==GLEW_ERROR_NO_GLX_DISPLAY,"GLEW initialization failed");
        while(glGetError()!=GL_NO_ERROR) {}
        require(GLEW_VERSION_4_5 && glCreateTextures,"Game atmosphere requires OpenGL 4.5 DSA");
        run(root);
        return 0;
    }
    catch(const std::exception& error)
    {
        std::cerr<<"FAIL game atmosphere runtime: "<<error.what()<<'\n';
        return 1;
    }
}
