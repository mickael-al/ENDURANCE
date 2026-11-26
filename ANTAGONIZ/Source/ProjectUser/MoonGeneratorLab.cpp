#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "MoonGeneratorLab.hpp"
#define CLEAR_REGISTRY
#include "MoonGeneratorLabPanel.hpp"
#include "Engine.hpp"
#include "PointeurClass.hpp"
#include "Hud.hpp"
#include "Debug.hpp"
#include <algorithm>
#include <filesystem>

JS_OBJ_EXT(EnduranceMoon::Params,seed,width,macroScale,detailScale,octaves,
    fractureCount,fractureWidth,fractureDepth,fractureStrength);
JS_OBJ_EXT(EnduranceMoon::GeometryParams,seed,resolution,excavationStrength,
    cavityCount,cavitySize,cavityDepth,excavationYaw,excavationPitch,
    erosionStrength,erosionScale,erosionOctaves,debrisCount,debrisSpread,debrisSize);
REGISTER(Behaviour,MoonGeneratorLabComponent,parameters,geometry,exportPath,
    previewWidth,previewResolution,autoGenerate,showWindow);

MoonGeneratorLabComponent::MoonGeneratorLabComponent()
{
    // True fractures come from the volume. The old atlas remains useful for
    // craters and lunar deposits, without engraving unrelated surface faults.
    parameters.fractureCount=0;
    parameters.fractureStrength=0;
}
MoonGeneratorLabComponent::~MoonGeneratorLabComponent() { stop(); }

void MoonGeneratorLabComponent::start()
{
    stop();
    m_error.clear();
    m_pc=Ge::Engine::getPtrClassAddr();
    if(!m_pc || !m_pc->hud)
    {
        m_error="Moon Generator Lab requires the ImGui HUD.";
        return;
    }
    m_panel=std::make_unique<MoonGeneratorLabPanel>(*this);
    m_pc->hud->addImgui(m_panel.get());
}

void MoonGeneratorLabComponent::stop()
{
    if(m_panel && m_pc && m_pc->hud) m_pc->hud->removeImgui(m_panel.get());
    m_panel.reset(); // Cancel/join before releasing the owning GL context.
    m_pc=nullptr;
}

void MoonGeneratorLabComponent::onGUI()
{
    ImGui::TextUnformatted("Moon Generator Lab - sculpture 3D hors jeu");
    ImGui::Checkbox("Afficher le laboratoire",&showWindow);
    ImGui::TextWrapped("Ouvrez MoonGeneratorLab.scene puis lancez Play. Le laboratoire sculpte un vrai maillage, ses cavites et ses fragments. L'export GLB contient la geometrie et la texture.");
    if(!m_error.empty()) ImGui::TextWrapped("%s",m_error.c_str());
}

std::string MoonGeneratorLabComponent::serialize()
{
    geometry=EnduranceMoon::sanitizeGeometry(geometry);
    parameters.seed=geometry.seed;
    parameters=EnduranceMoon::sanitize(parameters);
    auto preview=parameters;
    preview.width=(std::max)(128,(std::min)(previewWidth,1024));
    previewWidth=EnduranceMoon::sanitize(preview).width;
    previewResolution=std::clamp(previewResolution,32,128);
    return JS::serializeStruct(*this);
}

void MoonGeneratorLabComponent::load(std::string data)
{
    JS::ParseContext context(data);
    if(context.parseTo(*this)!=JS::Error::NoError)
        Ge::Debug::Warn("Invalid MoonGeneratorLabComponent scene settings.");
    geometry=EnduranceMoon::sanitizeGeometry(geometry);
    parameters.seed=geometry.seed;
    parameters=EnduranceMoon::sanitize(parameters);
    auto preview=parameters;
    preview.width=(std::max)(128,(std::min)(previewWidth,1024));
    previewWidth=EnduranceMoon::sanitize(preview).width;
    previewResolution=std::clamp(previewResolution,32,128);
    // Older saved labs pointed at the game atlas. A mesh export must never
    // overwrite that file, even when opening an old editor scene.
    if(std::filesystem::path(exportPath).extension()==".surface")
        exportPath="../ENDURANCE/Ressources/Model/Moon/Baked/moon.glb";
}
