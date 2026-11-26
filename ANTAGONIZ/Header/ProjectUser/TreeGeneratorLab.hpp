#ifndef ENDURANCE_TREE_GENERATOR_LAB_HPP
#define ENDURANCE_TREE_GENERATOR_LAB_HPP

#include "Behaviour.hpp"
#include "ProceduralTree.hpp"
#include <memory>
#include <string>

struct ptrClass;
class TreeGeneratorLabPanel;

// Scene-owned developer tool. It never participates in terrain streaming.
class TreeGeneratorLabComponent final : public Behaviour
{
public:
    TreeGeneratorLabComponent();
    ~TreeGeneratorLabComponent() override;
    void start() override;
    void fixedUpdate() override {}
    void update() override {}
    void stop() override;
    void onGUI() override;
    std::string serialize() override;
    void load(std::string data) override;

    EnduranceTrees::Params parameters;
    std::string presetJson;
    std::string exportDirectory="../ENDURANCE/Ressources/Trees/Baked";
    std::string maskPath;
    std::int32_t bakeCount=10;
    bool autoGenerate=true;
    bool showWindow=true;
private:
    std::unique_ptr<TreeGeneratorLabPanel> m_panel;
    const ptrClass* m_pc=nullptr;
    std::string m_error;
};
#endif
