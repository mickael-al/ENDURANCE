#ifndef ENDURANCE_TREE_GENERATOR_LAB_PANEL_HPP
#define ENDURANCE_TREE_GENERATOR_LAB_PANEL_HPP

#include "ImguiBlock.hpp"
#include "ProceduralTree.hpp"
#include "TreeLabPreview.hpp"
#include <atomic>
#include <future>
#include <memory>
#include <string>

class TreeGeneratorLabComponent;
class TreeGeneratorLabPanel final : public Ge::ImguiBlock
{
public:
    explicit TreeGeneratorLabPanel(TreeGeneratorLabComponent& owner);
    ~TreeGeneratorLabPanel() override;
    void init(GraphicsDataMisc*) override;
    void render(GraphicsDataMisc*) override;
private:
    enum class JobKind { None, Generate, Export, Load };
    struct Result
    {
        std::shared_ptr<EnduranceTrees::Tree> tree;
        std::vector<EnduranceTrees::ImpostorView> lod3;
        std::string error, path;
        bool cancelled=false;
    };
    TreeGeneratorLabComponent& m_owner;
    EnduranceTrees::TreeLabPreview m_preview;
    EnduranceTrees::PreviewSettings m_view;
    std::shared_ptr<EnduranceTrees::Tree> m_tree;
    std::future<Result> m_job;
    std::shared_ptr<std::atomic_bool> m_cancel;
    JobKind m_jobKind=JobKind::None;
    EnduranceTrees::Params m_batchParams;
    EnduranceTrees::Image m_batchLeafMask, m_embeddedMask;
    std::string m_batchMask, m_batchDirectory;
    unsigned m_batchIndex=0, m_batchCount=0;
    int m_impostorResolution=256;
    bool m_bakeImpostors=true, m_batchImpostors=false, m_previewDirty=true;
    bool m_dirty=true, m_generateRequested=false, m_fitNext=true;
    bool m_generationFailed=false;
    bool m_gpuReady=false, m_resetDock=true;
    ImGuiID m_settingsDock=0, m_previewDock=0;
    unsigned m_revision=0, m_submittedRevision=0;
    double m_lastEdit=0;
    std::string m_status, m_error;
    std::string m_presetPath="../ENDURANCE/Ressources/Trees/Presets/custom.json";
    std::string m_loadPath;

    bool busy() const;
    void edited();
    void startGenerate(const EnduranceTrees::Params& params,const std::string& maskPath);
    void startExport();
    void startBatch(unsigned count);
    void startLoad();
    void poll();
    void fitCamera();
    void drawSettings();
    void drawPreview();
    void buildDock(ImGuiID id,ImVec2 size);
};
#endif
