#ifndef ENDURANCE_MOON_GENERATOR_LAB_PANEL_HPP
#define ENDURANCE_MOON_GENERATOR_LAB_PANEL_HPP

#include "ImguiBlock.hpp"
#include "ProceduralMoon.hpp"
#include "MoonLabPreview.hpp"
#include <atomic>
#include <future>
#include <memory>
#include <string>

class MoonGeneratorLabComponent;
class MoonGeneratorLabPanel final : public Ge::ImguiBlock
{
public:
    explicit MoonGeneratorLabPanel(MoonGeneratorLabComponent& owner);
    ~MoonGeneratorLabPanel() override;
    void init(GraphicsDataMisc*) override;
    void render(GraphicsDataMisc*) override;
private:
    enum class JobKind { None, Generate, Export, Preview };
    struct Result
    {
        std::shared_ptr<EnduranceMoon::Surface> surface;
        EnduranceMoon::PreviewImage sphere, atlas, normals;
        std::string error, path;
        bool cancelled=false;
        double milliseconds=0;
    };
    struct Texture
    {
        unsigned id=0;
        int width=0,height=0;
    };
    MoonGeneratorLabComponent& m_owner;
    std::shared_ptr<EnduranceMoon::Surface> m_surface;
    std::future<Result> m_job;
    std::shared_ptr<std::atomic_bool> m_cancel;
    std::shared_ptr<std::atomic_int> m_stage;
    JobKind m_jobKind=JobKind::None;
    Texture m_sphere, m_atlas, m_normals;
    unsigned m_revision=0, m_submittedRevision=0;
    unsigned m_viewRevision=0, m_submittedViewRevision=0;
    bool m_dirty=true, m_generateRequested=false, m_generationFailed=false;
    bool m_previewDirty=false, m_resetDock=true;
    ImGuiID m_settingsDock=0, m_previewDock=0;
    float m_yaw=0, m_sun=35;
    double m_lastEdit=0, m_lastViewEdit=0, m_jobStarted=0, m_generationMilliseconds=0;
    std::string m_status, m_error;

    bool busy() const;
    void edited();
    void startGenerate(bool forExport);
    void startPreview();
    void poll();
    void cancel();
    bool upload(Texture& texture,const EnduranceMoon::PreviewImage& image);
    void buildDock(ImGuiID id,ImVec2 size);
    void drawSettings();
    void drawPreview();
};
#endif
