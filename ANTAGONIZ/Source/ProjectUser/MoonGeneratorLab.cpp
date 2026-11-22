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

JS_OBJ_EXT(EnduranceMoon::Params,seed,width,macroScale,detailScale,octaves,
    fractureCount,fractureWidth,fractureDepth,fractureStrength);
REGISTER(Behaviour,MoonGeneratorLabComponent,parameters,exportPath,previewWidth,
    autoGenerate,showWindow);

MoonGeneratorLabComponent::MoonGeneratorLabComponent()=default;
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
    m_panel.reset(); // Cancel and join the CPU job before deleting preview textures.
    m_pc=nullptr;
}

void MoonGeneratorLabComponent::onGUI()
{
    ImGui::TextUnformatted("Moon Generator Lab - creation hors jeu");
    ImGui::Checkbox("Afficher le laboratoire",&showWindow);
    ImGui::TextWrapped("Ouvrez MoonGeneratorLab.scene puis lancez Play. Les reglages et l'apercu restent dans la fenetre principale. Seul le bouton d'export ecrit le fichier lunaire.");
    if(!m_error.empty()) ImGui::TextWrapped("%s",m_error.c_str());
}

std::string MoonGeneratorLabComponent::serialize()
{
    parameters=EnduranceMoon::sanitize(parameters);
    auto preview=parameters;
    preview.width=(std::max)(128,(std::min)(previewWidth,1024));
    previewWidth=EnduranceMoon::sanitize(preview).width;
    return JS::serializeStruct(*this);
}

void MoonGeneratorLabComponent::load(std::string data)
{
    JS::ParseContext context(data);
    if(context.parseTo(*this)!=JS::Error::NoError)
        Ge::Debug::Warn("Invalid MoonGeneratorLabComponent scene settings.");
    parameters=EnduranceMoon::sanitize(parameters);
    auto preview=parameters;
    preview.width=(std::max)(128,(std::min)(previewWidth,1024));
    previewWidth=EnduranceMoon::sanitize(preview).width;
}
