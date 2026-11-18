#pragma once

#include "AtmosphereState.hpp"
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

struct ptrClass;
struct GraphicsDataMisc;

namespace Ge
{
    class Camera;
    class Lights;

    enum class AtmosphereQuality { Low=0, Medium=1, High=2, Ultra=3 };

    struct AtmosphereSettings
    {
        bool enabled=false;
        AtmosphereQuality quality=AtmosphereQuality::Ultra;
        bool physicalAtmosphere=true;
        bool localFog=true;
        bool godRays=true;
        bool clouds=true;
        bool measureGpu=true;
        bool fallbackToFirstDirectional=true;
        float movementThreshold=0.1f;
        float paletteThreshold=0.01f;
        float transitionSeconds=1.0f;
        float environmentInterval=0.20f;
        float environmentBlendSeconds=0.20f;
        float environmentThreshold=0.005f;
        float skySaturation=1.0f;
        float iblSaturation=1.0f;
        float planetRadius=6360000.0f;
        float atmosphereHeight=100000.0f;
        float seaLevel=0.0f;
        glm::vec3 rayleighScattering{5.802e-6f,13.558e-6f,33.1e-6f};
        float rayleighScaleHeight=8000.0f;
        float mieScattering=3.996e-6f;
        float mieAbsorption=0.444e-6f;
        float mieScaleHeight=1200.0f;
        float mieAnisotropy=0.8f;
        glm::vec3 ozoneAbsorption{0.650e-6f,1.881e-6f,0.085e-6f};
        float ozoneCenterHeight=25000.0f;
        float ozoneHalfWidth=15000.0f;
        glm::vec3 groundAlbedo{0.15f};
        float sunAngularRadius=0.266f; // degrees (half of the apparent diameter)
        glm::vec3 sunDiskColor{1.0f,0.96f,0.88f};
        float sunDiskIntensity=25.0f;
        float sunIlluminanceScale=12.0f;
        float moonAngularRadius=0.27f;
        glm::vec3 moonDiskColor{0.55f,0.65f,0.85f};
        float moonDiskIntensity=0.15f;
        float moonIlluminanceScale=0.05f;
        float aerialDistance=32768.0f;
        float altitudeUpdateThreshold=1.0f;
        float shadowCasterMargin=16000.0f;
        float fogDensity=0.0f; // generic fallback; game may supply a world-space medium
        glm::vec3 fogAlbedo{0.78f,0.83f,0.88f};
        float fogBaseHeight=0.0f;
        float fogScaleHeight=150.0f;
        float fogAnisotropy=0.45f;
        float cloudCoverage=0.42f;
        float cloudDensity=0.0012f;
        float cloudBaseHeight=2200.0f;
        float cloudThickness=1800.0f;
        float cloudScale=6000.0f;
        float cloudDetailScale=0.35f;
        float cloudWeatherScale=48000.0f;
        float cloudWeatherStrength=0.65f;
        float cloudWeatherContrast=1.4f;
        float cloudProfileVariation=0.7f;
        glm::vec3 cloudProfileMix{0.25f,0.5f,0.25f}; // stratus, cumulus, towers
        glm::vec2 wind{12.0f,4.0f};
        std::uint32_t cloudSeed=1337;
        // Zero selects the preset. Explicit overrides are clamped on upload.
        int cubemapSize=0, irradianceSize=0, prefilterSize=0;
        int iblSamples=0, skySteps=0, cloudLightSteps=0;
        int volumeWidth=0, volumeHeight=0, volumeDepth=0;
        int cloudViewWidth=0, cloudViewHeight=0;
        int iblMaxWorkUnitsPerFrame=16;
        int iblTileSize=32;
        // Soft submission budget using delayed GPU estimates. Already queued
        // work cannot be preempted; the work-unit limit is a separate hard cap.
        float targetGpuMilliseconds=8.0f;
    };

    struct AtmosphereStats
    {
        double skyMilliseconds=0.0, lutMilliseconds=0.0, iblMilliseconds=0.0;
        double aerialMilliseconds=0.0, fogMilliseconds=0.0, cloudMilliseconds=0.0;
        std::uint64_t transmittanceUpdates=0, multiscatterUpdates=0, skyUpdates=0;
        std::uint64_t environmentUpdates=0, volumeUpdates=0, droppedTimingSamples=0;
        std::uint64_t environmentCapturesStarted=0, environmentCapturesCancelled=0;
        std::uint64_t requestedEnvironmentRevision=0, captureRevision=0, publishedEnvironmentRevision=0;
        std::uint64_t dispatches=0, aerialDispatches=0, cloudVolumeDispatches=0;
        std::uint64_t cloudShadowDispatches=0, visibleCloudDispatches=0, fogDispatches=0;
        // CPU accounting of owned texel storage; excludes driver allocation
        // overhead and includes retained published/capture resources.
        std::uint64_t gpuTextureBytes=0, captureTextureBytes=0;
        unsigned frameDispatches=0, iblWorkUnitsThisFrame=0;
        unsigned captureWorkCompleted=0, captureWorkTotal=0;
        double iblEstimatedMilliseconds=0, iblSubmittedMilliseconds=0;
        float captureProgress=0;
        bool environmentReady=false, environmentCaptureActive=false;
        bool gpuSamplesAvailable=false;
        bool hasSun=false, hasMoon=false;
        float shadowDistance=0.0f;
    };

    // Render-thread owned. All textures are linear HDR; resource publication
    // occurs only after every face and every GGX level has been submitted.
    class Atmosphere final
    {
    public:
        Atmosphere();
        ~Atmosphere();
        Atmosphere(const Atmosphere&)=delete;
        Atmosphere& operator=(const Atmosphere&)=delete;
        void initialize(const ptrClass* pc, GraphicsDataMisc* graphics);
        void release();
        AtmosphereSettings& settings();
        const AtmosphereSettings& settings() const;
        const AtmosphereStats& stats() const;
        const std::string& error() const;
        bool active() const;
        void setPaletteTarget(const AtmospherePalette& palette, bool force=false);
        const AtmospherePalette& displayedPalette() const;
        void invalidate();
        void reloadShaders();
        void setSun(Lights* sun);
        void setMoon(Lights* moon);
        // Optional caller-owned HDR source, excluding directional-source disks.
        // A revision change schedules a complete environment transaction.
        void setExternalEnvironment(unsigned int cubemap, std::uint64_t revision=0);
        // Hook signature: vec3 geCustomizeRadiance(vec3 radiance,vec3 direction).
        // The same hook is compiled into sky and participating-medium scattering.
        void setRadianceShader(const std::string& path);
        // Hook signature: vec4 geGameFog(vec3 worldPosition), RGB sigma_s and A sigma_t.
        void setGameFogShader(const std::string& path);
        void setGameFogBinding(std::function<void(unsigned int)> bind);
        void prepare(Camera* camera, double deltaSeconds);
        void prepareShadows();
        void renderSky(unsigned int cameraSSBO);
        void computeVolumes(Camera* camera);
        // Deferred and forward materials share the same completed IBL/volume set.
        void bindLighting(unsigned int program, bool transparent=false);
        std::string serializeSettings() const;
        bool loadSettings(const std::string& json);
        void onGUI();
    private:
        struct Impl;
        std::unique_ptr<Impl> m;
    };
}
