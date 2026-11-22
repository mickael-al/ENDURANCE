#ifndef ENDURANCE_MOON_GENERATOR_LAB_HPP
#define ENDURANCE_MOON_GENERATOR_LAB_HPP

#include "Behaviour.hpp"
#include "ProceduralMoon.hpp"
#include <memory>
#include <string>

struct ptrClass;
class MoonGeneratorLabPanel;

// Offline authoring only. The game keeps reading its baked moon.surface atlas.
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
    std::string exportPath="../ENDURANCE/Ressources/Atmosphere/moon.surface";
    int previewWidth=512;
    bool autoGenerate=true;
    bool showWindow=true;
private:
    std::unique_ptr<MoonGeneratorLabPanel> m_panel;
    const ptrClass* m_pc=nullptr;
    std::string m_error;
};
#endif
