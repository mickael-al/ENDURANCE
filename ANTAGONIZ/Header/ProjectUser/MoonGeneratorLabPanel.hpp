#ifndef ENDURANCE_MOON_GENERATOR_LAB_PANEL_HPP
#define ENDURANCE_MOON_GENERATOR_LAB_PANEL_HPP

#include "ImguiBlock.hpp"
#include "ProceduralMoon.hpp"
#include "ProceduralMoonGeometry.hpp"
#include "MoonLabPreview.hpp"
#include "MoonMeshPreview.hpp"
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
    enum class JobKind { None, Generate, Export };
    struct Result
    {
        std::shared_ptr<EnduranceMoon::Surface> surface;
        std::shared_ptr<EnduranceMoon::Geometry> geometry;
        EnduranceMoon::Params surfaceParameters;
        EnduranceMoon::GeometryParams geometryParameters;
        EnduranceMoon::PreviewImage atlas;
        std::string error,path;
        bool cancelled=false;
        double milliseconds=0;
    };
    struct Texture { unsigned id=0; int width=0,height=0; };
    MoonGeneratorLabComponent& m_owner;
    std::shared_ptr<EnduranceMoon::Surface> m_surface;
    std::shared_ptr<EnduranceMoon::Geometry> m_geometry;
    EnduranceMoon::Params m_surfaceParameters;
    EnduranceMoon::GeometryParams m_geometryParameters;
    EnduranceMoon::MoonMeshPreview m_preview;
    EnduranceMoon::MeshPreviewSettings m_view;
    std::future<Result> m_job;
    std::shared_ptr<std::atomic_bool> m_cancel;
    std::shared_ptr<std::atomic_int> m_stage;
    JobKind m_jobKind=JobKind::None;
    Texture m_atlas;
    unsigned m_revision=0,m_submittedRevision=0;
    bool m_dirty=true,m_generateRequested=false,m_generationFailed=false;
    bool m_resetDock=true;
    ImGuiID m_settingsDock=0,m_previewDock=0;
    double m_lastEdit=0,m_jobStarted=0,m_generationMilliseconds=0;
    std::string m_status,m_error;

    bool busy() const;
    void edited();
    void startGenerate(bool forExport);
    void poll();
    void cancel();
    bool upload(Texture& texture,const EnduranceMoon::PreviewImage& image);
    void buildDock(ImGuiID id,ImVec2 size);
    void drawSettings();
    void drawPreview();
};
#endif
