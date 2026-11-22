#ifndef ENDURANCE_MOON_GENERATOR_LAB_HPP
#define ENDURANCE_MOON_GENERATOR_LAB_HPP

#include "Behaviour.hpp"
#include "ProceduralMoon.hpp"
#include "ProceduralMoonGeometry.hpp"
#include <memory>
#include <string>

struct ptrClass;
class MoonGeneratorLabPanel;

// Offline mesh authoring. This component never replaces the live atmosphere
// atlas: explicit export creates a model with its own embedded material.
class MoonGeneratorLabComponent final : public Behaviour
{
public:
    MoonGeneratorLabComponent();
    ~MoonGeneratorLabComponent() override;
    void start() override;
    void stop() override;
    void fixedUpdate() override {}
    void update() override {}
    void onGUI() override;
    std::string serialize() override;
    void load(std::string data) override;

    EnduranceMoon::Params parameters;
    EnduranceMoon::GeometryParams geometry;
    std::string exportPath="../ENDURANCE/Ressources/Model/Moon/Baked/moon.glb";
    int previewWidth=1024;
    int previewResolution=96;
    bool autoGenerate=true;
    bool showWindow=true;
private:
    std::unique_ptr<MoonGeneratorLabPanel> m_panel;
    const ptrClass* m_pc=nullptr;
    std::string m_error;
};
#endif
