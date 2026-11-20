#include "TreeGeneratorLab.hpp"
#define CLEAR_REGISTRY
#include "TreeGeneratorLabPanel.hpp"
#include "ProceduralTreeBake.hpp"
#include "Engine.hpp"
#include "PointeurClass.hpp"
#include "Hud.hpp"
#include "Debug.hpp"
#include <algorithm>

REGISTER(Behaviour, TreeGeneratorLabComponent, presetJson, exportDirectory,
    maskPath, bakeCount, autoGenerate, showWindow);

TreeGeneratorLabComponent::TreeGeneratorLabComponent()=default;
TreeGeneratorLabComponent::~TreeGeneratorLabComponent() { stop(); }

void TreeGeneratorLabComponent::start()
{
    stop();
    m_error.clear();
    m_pc=Ge::Engine::getPtrClassAddr();
    if(!m_pc || !m_pc->hud)
    {
        m_error="Tree Generator Lab requires the ImGui HUD.";
        return;
    }
    m_panel=std::make_unique<TreeGeneratorLabPanel>(*this);
    m_pc->hud->addImgui(m_panel.get());
}

void TreeGeneratorLabComponent::stop()
{
    if(m_panel && m_pc && m_pc->hud) m_pc->hud->removeImgui(m_panel.get());
    m_panel.reset(); // Cancels and joins CPU work before releasing preview GL objects.
    m_pc=nullptr;
}

void TreeGeneratorLabComponent::onGUI()
{
    ImGui::TextUnformatted("Tree Generator Lab - outil de creation hors jeu");
    ImGui::Checkbox("Afficher le laboratoire",&showWindow);
    ImGui::TextWrapped("Ouvrez TreeGeneratorLab.scene et lancez Play. Reglages, apercu 3D, presets et bake sont dans la fenetre du laboratoire.");
    if(!m_error.empty()) ImGui::TextWrapped("%s",m_error.c_str());
}

std::string TreeGeneratorLabComponent::serialize()
{
    presetJson=EnduranceTrees::paramsToJson(parameters);
    return JS::serializeStruct(*this);
}

void TreeGeneratorLabComponent::load(std::string data)
{
    JS::ParseContext context(data);
    if(context.parseTo(*this)!=JS::Error::NoError)
        Ge::Debug::Warn("Invalid TreeGeneratorLabComponent scene settings.");
    if(!presetJson.empty() && !EnduranceTrees::paramsFromJson(presetJson,parameters,m_error))
        Ge::Debug::Warn("Tree Generator Lab preset: %s",m_error.c_str());
    parameters=EnduranceTrees::sanitize(parameters);
    bakeCount=(std::max)(1,(std::min)(bakeCount,32));
}
