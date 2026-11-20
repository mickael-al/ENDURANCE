// Real production async panel, generator, baker, shaders and ImGui. Only scene
// registration/HUD ownership are stubbed; the test owns a real EGL GL context.
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include "Behaviour.hpp"
#include "ImguiBlock.hpp"
#include "glcore.hpp"
#define private public
#include "TreeGeneratorLab.hpp"
#include "TreeGeneratorLabPanel.hpp"
#undef private
#include "../../ANTAGONIZ/Source/ProjectUser/TreeGeneratorLabPanel.cpp"

TreeGeneratorLabComponent::TreeGeneratorLabComponent()=default;
TreeGeneratorLabComponent::~TreeGeneratorLabComponent()=default;
void TreeGeneratorLabComponent::start() {}
void TreeGeneratorLabComponent::stop() {}
void TreeGeneratorLabComponent::onGUI() {}
std::string TreeGeneratorLabComponent::serialize() { return {}; }
void TreeGeneratorLabComponent::load(std::string) {}

namespace {
void requirePanel(bool condition,const std::string& message)
{ if(!condition) throw std::runtime_error(message); }

void waitPanel(TreeGeneratorLabPanel& panel)
{
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(30);
    do {
        panel.poll();
        requirePanel(std::chrono::steady_clock::now()<deadline,"Async panel operation timed out");
        if(panel.busy()) std::this_thread::sleep_for(std::chrono::milliseconds(2));
    } while(panel.busy());
}

void runPanel()
{
    using namespace EnduranceTrees;
    ImGui::CreateContext();
    struct ReleaseContext { ~ReleaseContext() { ImGui::DestroyContext(); } } context;
    auto& io=ImGui::GetIO(); io.IniFilename=nullptr;
    io.ConfigFlags|=ImGuiConfigFlags_DockingEnable;
    io.DisplaySize={1400,1000}; io.DeltaTime=1.f/60.f;
    unsigned char* font=nullptr; int fw=0,fh=0;
    io.Fonts->GetTexDataAsRGBA32(&font,&fw,&fh);

    TreeGeneratorLabComponent owner;
    owner.autoGenerate=false;
    owner.exportDirectory="../baked";
    owner.parameters.branchCount=5; owner.parameters.branchingLevels=2;
    owner.parameters.leafDensity=.4f;
    TreeGeneratorLabPanel panel(owner);
    panel.init(nullptr);
    requirePanel(panel.m_gpuReady,"Initialization: "+panel.m_error);
    panel.startGenerate(owner.parameters,""); waitPanel(panel);
    requirePanel(panel.m_tree && panel.m_error.empty(),"Initial generation: "+panel.m_error);
    requirePanel(panel.m_tree->statistics.roots==0 && panel.m_tree->statistics.vines==0,"Default tree unexpectedly gained optional structures");
    owner.parameters.rootCount=4; owner.parameters.vineCount=6;
    panel.edited(); panel.startGenerate(owner.parameters,""); waitPanel(panel);
    requirePanel(panel.m_tree->statistics.roots==4 && panel.m_tree->statistics.vines==6,"Edited optional structures were not generated");
    const auto initial=panel.m_tree;

    // An unreadable mask must retain the current preview and forbid stale bake.
    panel.startGenerate(owner.parameters,"missing-mask.png");
    requirePanel(!panel.busy() && !panel.m_error.empty() && panel.m_tree==initial,
                 "Invalid mask replaced the previous preview or silently succeeded");
    panel.startBatch(1);
    requirePanel(!panel.busy(),"Invalid generation allowed exporting a stale preview");

    // Same black/white mask encoded as opaque RGBA or actual alpha must agree.
    panel.startGenerate(owner.parameters,"../mask-opaque.png"); waitPanel(panel);
    requirePanel(panel.m_error.empty(),"Opaque RGBA mask: "+panel.m_error);
    const auto opaque=panel.m_tree->leafMask;
    panel.startGenerate(owner.parameters,"../mask-alpha.png"); waitPanel(panel);
    requirePanel(panel.m_error.empty(),"Alpha mask: "+panel.m_error);
    requirePanel(panel.m_tree->leafMask.rgba==opaque.rgba && opaque.width==7 && opaque.height==11,
                 "Opaque RGBA luminance mask differs from equivalent alpha mask");
    requirePanel(std::count(opaque.rgba.begin(),opaque.rgba.end(),std::uint8_t(0))>0,
                 "Mask lost transparent pixels");
    owner.maskPath="../mask-alpha.png";
    panel.m_bakeImpostors=false;
    panel.m_impostorResolution=64;
    const auto baseSeed=panel.m_tree->params.seed;
    panel.startBatch(2); waitPanel(panel);
    requirePanel(panel.m_error.empty() && panel.m_batchCount==0 && panel.m_batchIndex==2,
                 "Two-tree batch: "+panel.m_error);
    std::vector<std::filesystem::path> exports;
    for(const auto& path:std::filesystem::directory_iterator(panel.m_batchDirectory))
        if(path.is_directory()) exports.push_back(path.path());
    std::sort(exports.begin(),exports.end());
    requirePanel(exports.size()==2,"Batch did not publish exactly two variants");
    for(std::size_t i=0;i<exports.size();++i) {
        Tree loaded; std::string error; std::vector<ImpostorView> views;
        requirePanel(loadBakedTree(exports[i],loaded,error,&views),"Batch unreadable: "+error);
        requirePanel(loaded.params.seed==baseSeed+i && loaded.leafMask.rgba==opaque.rgba,
                     "Batch changed source mask or repeated seed");
        requirePanel(loaded.statistics.roots==4 && loaded.statistics.vines==6,"Batch lost optional structures");
        requirePanel(views.size()==2 && views[0].albedo.width==64,"Ordinary batch export did not save two LOD3 images");
        requirePanel(std::filesystem::is_regular_file(exports[i]/"lod3_0.png") && std::filesystem::is_regular_file(exports[i]/"lod3_45.png"),"Missing automatically saved LOD3 PNGs");
        requirePanel(!std::filesystem::exists(exports[i]/"impostor_0_albedo.png"),"Disabled multi-view bake still generated legacy images");
    }
    requirePanel(owner.parameters.seed==baseSeed+1,"Batch controls disagree with visible variant");
    // Capture stays on the GL thread; the worker writes PNG/mesh files.
    panel.m_bakeImpostors=true; panel.m_impostorResolution=32;
    panel.startBatch(1); waitPanel(panel);
    requirePanel(panel.m_error.empty(),"Capture + asynchronous export: "+panel.m_error);
    requirePanel(std::filesystem::is_regular_file(std::filesystem::u8path(panel.m_loadPath)/"impostor_16_normal.png"),
                 "Full panel export missed elevated/top impostors");
    panel.m_bakeImpostors=false;

    panel.m_loadPath=exports.front().u8string(); panel.startLoad(); waitPanel(panel);
    requirePanel(panel.m_error.empty() && std::filesystem::is_regular_file(owner.maskPath) && validImage(panel.m_embeddedMask),
                 "Bake reload failed to retain embedded mask: "+panel.m_error);
    requirePanel(owner.parameters.seed==baseSeed,"Bake reload did not restore parameters");
    panel.m_view.lod=3;
    requirePanel(panel.m_preview.render(panel.m_view),"Reloaded LOD3 failed to render: "+panel.m_preview.error());
    owner.parameters.seed+=11;
    panel.startGenerate(owner.parameters,owner.maskPath); waitPanel(panel);
    requirePanel(panel.m_error.empty() && panel.m_tree->leafMask.rgba==opaque.rgba,
                 "Regenerating a bake silently replaced its mask");
    const auto retained=panel.m_tree;
    panel.m_loadPath="../missing-bake"; panel.startLoad(); waitPanel(panel);
    requirePanel(!panel.m_error.empty() && panel.m_tree==retained,"Failed load damaged preview");
    panel.m_loadPath=exports.front().u8string(); panel.startLoad(); waitPanel(panel);
    requirePanel(panel.m_error.empty(),"Successful load retained previous error: "+panel.m_error);

    // A late result from settings edited while computing cannot become visible.
    const auto beforeEdit=panel.m_tree;
    auto changed=owner.parameters; changed.seed+=3;
    panel.startGenerate(changed,""); panel.edited(); waitPanel(panel);
    requirePanel(panel.m_tree==beforeEdit && panel.m_dirty,"Stale generation survived a settings edit");
    panel.startGenerate(owner.parameters,""); waitPanel(panel);
    requirePanel(panel.m_error.empty(),"Recovery generation failed: "+panel.m_error);
    panel.startBatch(4); panel.m_cancel->store(true); waitPanel(panel);
    requirePanel(panel.m_batchCount==0 && !panel.busy(),"Batch cancellation continued scheduling work");

    // Exercise actual widgets/dock/image commands with the same GL context.
    for(int frame=0;frame<3;++frame) {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({0,0}); ImGui::SetNextWindowSize({1300,900});
        panel.render(nullptr); ImGui::Render();
        requirePanel(ImGui::GetDrawData()->TotalVtxCount>0,"Lab panel rendered no widgets");
    }
    requirePanel(glGetError()==GL_NO_ERROR,"OpenGL error from production panel lifecycle");
    // Destructor must cancel and join its outstanding worker safely.
    {
        TreeGeneratorLabPanel pending(owner); pending.init(nullptr);
        auto large=owner.parameters; large.branchCount=24; large.branchingLevels=4;
        pending.startGenerate(large,"");
    }
}
}

extern "C" int tree_panel_tests()
{
    try {
        glewExperimental=GL_TRUE; const auto status=glewInit();
        requirePanel(status==GLEW_OK || status==GLEW_ERROR_NO_GLX_DISPLAY,"GLEW initialization failed");
        while(glGetError()!=GL_NO_ERROR) {}
        runPanel();
        std::cout<<"Tree panel: generation, masks, async batch, bake reload, cancellation and ImGui lifecycle PASS\n";
        return 0;
    } catch(const std::exception& error) {
        std::cerr<<"FAIL tree panel: "<<error.what()<<'\n'; return 1;
    }
}
