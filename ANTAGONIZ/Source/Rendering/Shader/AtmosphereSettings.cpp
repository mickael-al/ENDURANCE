#include "Atmosphere.hpp"
#include "imgui-cmake/Header/imgui.h"
#include "hv/cpputil/json.hpp"
#include <algorithm>
#include <cmath>
#include <type_traits>

namespace Ge
{
namespace
{
    using Json=nlohmann::json;
    // Keep the serializable settings inventory in one place for both directions.
#define GE_ATMOSPHERE_VALUES(V) \
    V(enabled) V(physicalAtmosphere) V(localFog) V(godRays) V(clouds) V(measureGpu) \
    V(fallbackToFirstDirectional) V(movementThreshold) V(paletteThreshold) \
    V(transitionSeconds) V(environmentInterval) V(environmentBlendSeconds) \
    V(environmentThreshold) V(skySaturation) V(iblSaturation) V(planetRadius) \
    V(atmosphereHeight) V(seaLevel) V(rayleighScaleHeight) V(mieScattering) \
    V(mieAbsorption) V(mieScaleHeight) V(mieAnisotropy) V(ozoneCenterHeight) \
    V(ozoneHalfWidth) V(sunAngularRadius) V(sunDiskIntensity) V(sunIlluminanceScale) \
    V(moonAngularRadius) V(moonDiskIntensity) V(moonIlluminanceScale) V(aerialDistance) \
    V(moonSurface) V(moonDistance) V(moonAzimuth) V(moonElevation) V(moonRotation) V(moonNormalStrength) V(moonMeshResolution) \
    V(altitudeUpdateThreshold) V(shadowCasterMargin) V(fogDensity) V(fogBaseHeight) \
    V(fogScaleHeight) V(fogAnisotropy) V(cloudCoverage) V(cloudDensity) \
    V(cloudBaseHeight) V(cloudThickness) V(cloudScale) V(cloudDetailScale) V(cloudSeed) \
    V(cloudWeatherScale) V(cloudWeatherStrength) V(cloudWeatherContrast) V(cloudProfileVariation) \
    V(cubemapSize) V(irradianceSize) V(prefilterSize) V(iblSamples) V(skySteps) \
    V(cloudLightSteps) V(volumeWidth) V(volumeHeight) V(volumeDepth) V(targetGpuMilliseconds) \
    V(cloudViewWidth) V(cloudViewHeight) V(iblMaxWorkUnitsPerFrame) V(iblTileSize)
#define GE_ATMOSPHERE_VEC3(V) V(rayleighScattering) V(ozoneAbsorption) V(groundAlbedo) V(sunDiskColor) V(moonDiskColor) V(fogAlbedo) V(cloudProfileMix)
    template<class T> bool finiteSetting(T value)
    {
        if constexpr(std::is_floating_point_v<T>) return std::isfinite(value);
        return true;
    }
}
std::string Atmosphere::serializeSettings() const
{
    const auto& s=settings(); Json j; j["version"]=2; j["quality"]=static_cast<int>(s.quality);
#define SAVE(name) j[#name]=s.name;
    GE_ATMOSPHERE_VALUES(SAVE)
#undef SAVE
#define SAVE_VECTOR(name) j[#name]=Json::array({s.name.x,s.name.y,s.name.z});
    GE_ATMOSPHERE_VEC3(SAVE_VECTOR)
#undef SAVE_VECTOR
    j["wind"]=Json::array({s.wind.x,s.wind.y});
    return j.dump();
}
bool Atmosphere::loadSettings(const std::string& text)
{
    if(text.empty()) return true;
    try
    {
        const auto j=Json::parse(text);
        if(!j.is_object()) return false;
        auto s=settings();
#define LOAD(name) if(j.contains(#name)) s.name=j.at(#name).get<decltype(s.name)>(); if(!finiteSetting(s.name)) return false;
        GE_ATMOSPHERE_VALUES(LOAD)
#undef LOAD
#define LOAD_VECTOR(name) if(j.contains(#name)) { const auto& v=j.at(#name); if(!v.is_array() || v.size()!=3) return false; for(int i=0;i<3;++i) { s.name[i]=v[i].get<float>(); if(!std::isfinite(s.name[i])) return false; } }
        GE_ATMOSPHERE_VEC3(LOAD_VECTOR)
#undef LOAD_VECTOR
        if(j.contains("wind"))
        {
            const auto& v=j.at("wind"); if(!v.is_array() || v.size()!=2) return false;
            for(int i=0;i<2;++i) { s.wind[i]=v[i].get<float>(); if(!std::isfinite(s.wind[i])) return false; }
        }
        if(j.contains("quality")) s.quality=static_cast<AtmosphereQuality>(std::clamp(j.at("quality").get<int>(),0,3));
        s.movementThreshold=std::clamp(s.movementThreshold,0.0f,10000.0f);
        s.paletteThreshold=std::clamp(s.paletteThreshold,0.0f,10.0f);
        s.transitionSeconds=std::clamp(s.transitionSeconds,0.0f,60.0f);
        s.environmentInterval=std::clamp(s.environmentInterval,0.0f,60.0f);
        s.environmentBlendSeconds=std::clamp(s.environmentBlendSeconds,0.0f,10.0f);
        s.environmentThreshold=std::clamp(s.environmentThreshold,0.0f,10.0f);
        s.aerialDistance=std::clamp(s.aerialDistance,100.0f,200000.0f);
        s.planetRadius=std::clamp(s.planetRadius,1000.0f,1.0e8f);
        s.atmosphereHeight=std::clamp(s.atmosphereHeight,100.0f,1000000.0f);
        s.sunAngularRadius=std::clamp(s.sunAngularRadius,.001f,10.0f);
        s.moonAngularRadius=std::clamp(s.moonAngularRadius,.001f,25.0f);
        s.moonDistance=std::clamp(s.moonDistance,1000000.0f,1.0e9f);
        s.moonAzimuth=std::remainder(s.moonAzimuth,360.0f);
        s.moonElevation=std::clamp(s.moonElevation,-90.0f,90.0f);
        s.moonRotation=std::remainder(s.moonRotation,360.0f);
        s.moonNormalStrength=std::clamp(s.moonNormalStrength,0.0f,2.0f);
        s.moonMeshResolution=std::clamp(s.moonMeshResolution,512,2048);
        s.cloudCoverage=std::clamp(s.cloudCoverage,0.0f,1.0f);
        s.cloudDensity=std::clamp(s.cloudDensity,0.0f,.1f);
        s.cloudWeatherScale=std::clamp(s.cloudWeatherScale,100.0f,10000000.0f);
        s.cloudWeatherStrength=std::clamp(s.cloudWeatherStrength,0.0f,1.0f);
        s.cloudWeatherContrast=std::clamp(s.cloudWeatherContrast,.05f,8.0f);
        s.cloudProfileVariation=std::clamp(s.cloudProfileVariation,0.0f,1.0f);
        s.cloudProfileMix=glm::clamp(s.cloudProfileMix,glm::vec3(0),glm::vec3(100));
        s.cloudViewWidth=std::clamp(s.cloudViewWidth,0,1280);
        s.cloudViewHeight=std::clamp(s.cloudViewHeight,0,720);
        s.iblMaxWorkUnitsPerFrame=std::clamp(s.iblMaxWorkUnitsPerFrame,1,64);
        s.iblTileSize=8*std::clamp(s.iblTileSize/8,1,8);
        s.targetGpuMilliseconds=std::clamp(s.targetGpuMilliseconds,.01f,1000.0f);
        s.fogDensity=std::clamp(s.fogDensity,0.0f,.1f);
        s.shadowCasterMargin=std::clamp(s.shadowCasterMargin,0.0f,200000.0f);
        s.wind=glm::clamp(s.wind,glm::vec2(-1000),glm::vec2(1000));
        settings()=s; invalidate(); return true;
    }
    catch(const std::exception&) { return false; }
}

void Atmosphere::onGUI()
{
    auto& s=settings(); const auto& metrics=stats();
    ImGui::Checkbox("Atmosphere active",&s.enabled);
    int quality=static_cast<int>(s.quality);
    const char* qualities[]={"Low","Medium","High","Ultra"};
    if(ImGui::Combo("Quality",&quality,qualities,4)) s.quality=static_cast<AtmosphereQuality>(quality);
    ImGui::Checkbox("Physical atmosphere",&s.physicalAtmosphere);
    ImGui::Checkbox("World-space local fog",&s.localFog);
    ImGui::Checkbox("Terrain god rays",&s.godRays);
    ImGui::Checkbox("Volumetric clouds",&s.clouds);
    if(ImGui::TreeNode("Transitions and environment"))
    {
        ImGui::DragFloat("Climate movement threshold (m)",&s.movementThreshold,.1f,0,1000);
        ImGui::DragFloat("Palette threshold",&s.paletteThreshold,.001f,0,1,"%.4f");
        ImGui::DragFloat("Palette transition (s)",&s.transitionSeconds,.05f,0,30);
        ImGui::DragFloat("Environment interval (s)",&s.environmentInterval,.01f,0,5);
        ImGui::DragFloat("Environment blend (s)",&s.environmentBlendSeconds,.01f,0,5);
        ImGui::DragFloat("Environment threshold",&s.environmentThreshold,.001f,0,1,"%.4f");
        ImGui::DragFloat("Visible sky saturation",&s.skySaturation,.01f,0,3);
        ImGui::DragFloat("IBL saturation",&s.iblSaturation,.01f,0,3);
        ImGui::TreePop();
    }
    if(ImGui::TreeNode("Physical medium"))
    {
        ImGui::DragFloat("Planet radius (m)",&s.planetRadius,1000,1000,1e8f);
        ImGui::DragFloat("Atmosphere height (m)",&s.atmosphereHeight,100,100,1000000);
        ImGui::DragFloat("Reference sea level (m)",&s.seaLevel,1,-100000,100000);
        ImGui::DragFloat3("Rayleigh scattering (1/m)",&s.rayleighScattering.x,1e-7f,0,.001f,"%.7f");
        ImGui::DragFloat("Rayleigh scale height (m)",&s.rayleighScaleHeight,10,1,100000);
        ImGui::DragFloat("Mie scattering (1/m)",&s.mieScattering,1e-7f,0,.001f,"%.7f");
        ImGui::DragFloat("Mie absorption (1/m)",&s.mieAbsorption,1e-7f,0,.001f,"%.7f");
        ImGui::DragFloat("Mie scale height (m)",&s.mieScaleHeight,10,1,100000);
        ImGui::SliderFloat("Mie anisotropy",&s.mieAnisotropy,-.95f,.95f);
        ImGui::DragFloat3("Ozone absorption (1/m)",&s.ozoneAbsorption.x,1e-8f,0,.001f,"%.8f");
        ImGui::DragFloat("Ozone center (m)",&s.ozoneCenterHeight,100,0,100000);
        ImGui::DragFloat("Ozone half width (m)",&s.ozoneHalfWidth,100,1,100000);
        ImGui::ColorEdit3("Ground albedo",&s.groundAlbedo.x,ImGuiColorEditFlags_Float);
        ImGui::DragFloat("Perspective distance (m)",&s.aerialDistance,100,100,200000);
        ImGui::DragFloat("Sky altitude threshold (m)",&s.altitudeUpdateThreshold,.1f,.01f,1000);
        ImGui::TreePop();
    }
    if(ImGui::TreeNode("Sun and optional moon"))
    {
        ImGui::Text("Assigned sources: sun %s / moon %s",metrics.hasSun?"yes":"no",metrics.hasMoon?"yes":"no");
        ImGui::DragFloat("Sun angular radius (degrees)",&s.sunAngularRadius,.01f,.001f,10);
        ImGui::ColorEdit3("Sun disk color",&s.sunDiskColor.x,ImGuiColorEditFlags_HDR|ImGuiColorEditFlags_Float);
        ImGui::DragFloat("Sun disk HDR intensity",&s.sunDiskIntensity,.1f,0,10000);
        ImGui::DragFloat("Sun illumination multiplier",&s.sunIlluminanceScale,.1f,0,1000);
        ImGui::Checkbox("Baked lunar body",&s.moonSurface);
        ImGui::DragFloat("Moon angular radius (degrees)",&s.moonAngularRadius,.01f,.001f,25);
        if(s.moonSurface)
        {
            ImGui::DragFloat("Moon centre distance (m)",&s.moonDistance,1000,1000000,1.0e9f);
            ImGui::DragFloat("Moon fallback azimuth (degrees)",&s.moonAzimuth,.1f,-180,180);
            ImGui::DragFloat("Moon fallback elevation (degrees)",&s.moonElevation,.1f,-90,90);
            ImGui::DragFloat("Moon surface rotation (degrees)",&s.moonRotation,.1f,-180,180);
            ImGui::SliderFloat("Legacy sphere relief strength",&s.moonNormalStrength,0,2);
            ImGui::SliderInt("Moon cached image size",&s.moonMeshResolution,512,2048);
            ImGui::Text("Moon cache: %llu images / %llu shadows, %.1f MiB",
                static_cast<unsigned long long>(metrics.moonImageUpdates),
                static_cast<unsigned long long>(metrics.moonShadowUpdates),metrics.moonMeshGpuBytes/1048576.0);
            ImGui::TextWrapped("The baked mesh retains its cavities and fragments. Camera rotation and solar intensity reuse the cache. The assigned moon light keeps the existing night-only terrain lighting.");
        }
        ImGui::ColorEdit3("Moon disk color",&s.moonDiskColor.x,ImGuiColorEditFlags_HDR|ImGuiColorEditFlags_Float);
        ImGui::DragFloat("Moon disk HDR intensity",&s.moonDiskIntensity,.01f,0,1000);
        ImGui::DragFloat("Moon illumination multiplier",&s.moonIlluminanceScale,.01f,0,1000);
        ImGui::DragFloat("Off-screen caster margin (m)",&s.shadowCasterMargin,100,0,200000);
        ImGui::Text("Directional shadow receiver coverage: %.0f m",metrics.shadowDistance);
        ImGui::TreePop();
    }
    if(ImGui::TreeNode("Generic local fog"))
    {
        ImGui::TextWrapped("Biome-specific fog is edited in the game palette profiles. These controls provide an additional generic height medium.");
        ImGui::DragFloat("Fog extinction (1/m)",&s.fogDensity,.00001f,0,.1f,"%.6f");
        ImGui::ColorEdit3("Fog scattering albedo",&s.fogAlbedo.x,ImGuiColorEditFlags_Float);
        ImGui::DragFloat("Fog base altitude (m)",&s.fogBaseHeight,1,-10000,100000);
        ImGui::DragFloat("Fog height scale (m)",&s.fogScaleHeight,1,1,10000);
        ImGui::SliderFloat("Fog anisotropy",&s.fogAnisotropy,-.95f,.95f);
        ImGui::TreePop();
    }
    if(ImGui::TreeNode("Cloud layer"))
    {
        ImGui::SliderFloat("Coverage",&s.cloudCoverage,0,1);
        ImGui::DragFloat("Cloud extinction (1/m)",&s.cloudDensity,.00001f,0,.1f,"%.6f");
        ImGui::DragFloat("Cloud altitude (m)",&s.cloudBaseHeight,10,-10000,100000);
        ImGui::DragFloat("Cloud thickness (m)",&s.cloudThickness,10,1,30000);
        ImGui::DragFloat("Cloud shape scale (m)",&s.cloudScale,10,100,100000);
        ImGui::DragFloat("Cloud detail scale",&s.cloudDetailScale,.01f,.01f,4);
        ImGui::DragFloat("Weather region scale (m)",&s.cloudWeatherScale,100,100,10000000);
        ImGui::SliderFloat("Regional coverage strength",&s.cloudWeatherStrength,0,1);
        ImGui::SliderFloat("Regional coverage contrast",&s.cloudWeatherContrast,.05f,8);
        ImGui::SliderFloat("Cloud profile variation",&s.cloudProfileVariation,0,1);
        ImGui::DragFloat3("Stratus / cumulus / tower weights",&s.cloudProfileMix.x,.01f,0,1);
        ImGui::DragFloat2("Wind XZ (m/s)",&s.wind.x,.1f,-1000,1000);
        ImGui::InputScalar("Cloud noise seed",ImGuiDataType_U32,&s.cloudSeed);
        ImGui::TreePop();
    }
    if(ImGui::TreeNode("GPU budgets and measurements"))
    {
        ImGui::TextUnformatted("Zero overrides use the quality preset.");
        ImGui::DragInt("Cubemap resolution",&s.cubemapSize,8,0,512);
        ImGui::DragInt("Irradiance resolution",&s.irradianceSize,1,0,64);
        ImGui::DragInt("GGX resolution",&s.prefilterSize,8,0,256);
        ImGui::DragInt("IBL samples",&s.iblSamples,8,0,1024);
        ImGui::DragInt("Sky integration steps",&s.skySteps,4,0,128);
        ImGui::DragInt("Cloud light steps",&s.cloudLightSteps,1,0,32);
        ImGui::DragInt("Volume width",&s.volumeWidth,8,0,320);
        ImGui::DragInt("Volume height",&s.volumeHeight,4,0,180);
        ImGui::DragInt("Volume depth",&s.volumeDepth,8,0,192);
        ImGui::DragInt("Visible cloud width",&s.cloudViewWidth,8,0,1280);
        ImGui::DragInt("Visible cloud height",&s.cloudViewHeight,8,0,720);
        ImGui::DragInt("IBL maximum work units/frame",&s.iblMaxWorkUnitsPerFrame,1,1,64);
        ImGui::DragInt("IBL tile size (multiples of 8)",&s.iblTileSize,8,8,64);
        ImGui::DragFloat("Atmosphere GPU target (ms)",&s.targetGpuMilliseconds,.1f,.01f,1000);
        ImGui::TextWrapped("Soft submission budget: delayed GPU timings estimate IBL work. Already submitted GPU work cannot be interrupted. At least one work unit progresses; the work-unit cap is strict. Visible clouds update independently of IBL captures.");
        ImGui::Checkbox("Measure GPU phases",&s.measureGpu);
        if(metrics.gpuSamplesAvailable)
        {
            ImGui::Text("Latest executed phases: sky %.3f / LUT %.3f / IBL %.3f ms",metrics.skyMilliseconds,metrics.lutMilliseconds,metrics.iblMilliseconds);
            ImGui::Text("Aerial %.3f / fog integration %.3f / clouds %.3f ms",metrics.aerialMilliseconds,metrics.fogMilliseconds,metrics.cloudMilliseconds);
            ImGui::TextWrapped("Cached phases retain their last measured cost. Results are read asynchronously; reduce samples, resolution or update frequency to meet the target.");
        }
        ImGui::Text("Updates: transmittance %llu / multiple scattering %llu / sky %llu / IBL %llu",
            static_cast<unsigned long long>(metrics.transmittanceUpdates),static_cast<unsigned long long>(metrics.multiscatterUpdates),
            static_cast<unsigned long long>(metrics.skyUpdates),static_cast<unsigned long long>(metrics.environmentUpdates));
        ImGui::Text("IBL capture: %s, %u / %u units (%.1f%%)",metrics.environmentCaptureActive?"working":"idle",
            metrics.captureWorkCompleted,metrics.captureWorkTotal,100.0f*metrics.captureProgress);
        ImGui::Text("Submitted: %u units, %.3f ms estimated; remaining %.3f ms",metrics.iblWorkUnitsThisFrame,
            metrics.iblSubmittedMilliseconds,metrics.iblEstimatedMilliseconds);
        ImGui::Text("Revision: requested %llu / capture %llu / published %llu",
            static_cast<unsigned long long>(metrics.requestedEnvironmentRevision),
            static_cast<unsigned long long>(metrics.captureRevision),static_cast<unsigned long long>(metrics.publishedEnvironmentRevision));
        ImGui::Text("Dispatches this frame: %u; texture storage estimate %.2f MiB (capture %.2f MiB)",
            metrics.frameDispatches,metrics.gpuTextureBytes/1048576.0,metrics.captureTextureBytes/1048576.0);
        ImGui::TreePop();
    }
    if(!error().empty()) ImGui::TextWrapped("Atmosphere: %s",error().c_str());
}
}
