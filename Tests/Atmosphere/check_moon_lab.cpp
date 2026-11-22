// Production component/panel, native generator, CPU preview and real ImGui/GL.
// Only the engine service locator and HUD ownership callbacks are fixtures.
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include "Behaviour.hpp"
#include "ImguiBlock.hpp"
#include "Engine.hpp"
#include "Hud.hpp"
#include "Debug.hpp"
#include "glcore.hpp"
#include "hv/cpputil/json.hpp"
#include "imgui_internal.h"
#define private public
#include "MoonGeneratorLab.hpp"
#include "MoonGeneratorLabPanel.hpp"
#undef private

// The real service table imports the entire game. Its only field used by the
// lab is hud; Engine, Hud, Behaviour and serialization keep their real headers.
#define __ENGINE_POINTEUR_CLASS__
struct ptrClass { Ge::Hud* hud=nullptr; };
namespace
{
const ptrClass* services=nullptr;
unsigned additions=0,removals=0,warnings=0;
}
const ptrClass* Ge::Engine::getPtrClassAddr() { return services; }
Ge::Hud::Hud() : m_gdm(nullptr),m_engineInfo(nullptr),m_console(nullptr),m_io(nullptr) {}
Ge::Hud::~Hud()=default;
bool Ge::Hud::initialize(GraphicsDataMisc*) { return true; }
void Ge::Hud::release() {}
void Ge::Hud::addImgui(ImguiBlock* block)
{ ++additions; m_imguiBlock.push_back(block); block->init(m_gdm); }
void Ge::Hud::removeImgui(ImguiBlock* block)
{ ++removals; m_imguiBlock.erase(std::remove(m_imguiBlock.begin(),m_imguiBlock.end(),block),m_imguiBlock.end()); }
void Ge::Debug::Warn(const char*,...) { ++warnings; }

// Including these untouched implementations exposes private state to the tests
// without adding test hooks to the editor or stubbing its serialization.
#include "../../ANTAGONIZ/Source/ProjectUser/MoonGeneratorLab.cpp"
#include "../../ANTAGONIZ/Source/ProjectUser/MoonGeneratorLabPanel.cpp"

namespace
{
void requireLab(bool condition,const std::string& message)
{ if(!condition) throw std::runtime_error(message); }

void waitLab(MoonGeneratorLabPanel& panel)
{
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(45);
    do
    {
        panel.poll();
        requireLab(std::chrono::steady_clock::now()<deadline,"Moon Lab asynchronous operation timed out");
        if(panel.busy()) std::this_thread::sleep_for(std::chrono::milliseconds(2));
    } while(panel.busy());
}

std::vector<std::uint8_t> fileBytes(const std::filesystem::path& path)
{
    std::ifstream input(path,std::ios::binary);
    requireLab(bool(input),"Cannot read test export: "+path.string());
    return {std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>()};
}

std::uint32_t little32(const std::uint8_t* bytes)
{
    return std::uint32_t(bytes[0]) | (std::uint32_t(bytes[1])<<8) |
        (std::uint32_t(bytes[2])<<16) | (std::uint32_t(bytes[3])<<24);
}

std::vector<std::uint8_t> textureBytes(const MoonGeneratorLabPanel::Texture& texture)
{
    requireLab(texture.id && glIsTexture(texture.id),"Preview texture is missing");
    GLint width=0,height=0;
    glGetTextureLevelParameteriv(texture.id,0,GL_TEXTURE_WIDTH,&width);
    glGetTextureLevelParameteriv(texture.id,0,GL_TEXTURE_HEIGHT,&height);
    requireLab(width==texture.width && height==texture.height && width>0 && height>0,
               "Preview texture metadata differs from GL storage");
    std::vector<std::uint8_t> pixels(std::size_t(width)*height*4);
    glGetTextureImage(texture.id,0,GL_RGBA,GL_UNSIGNED_BYTE,GLsizei(pixels.size()),pixels.data());
    return pixels;
}

void frame(MoonGeneratorLabPanel& panel,ImVec2 size={1400,1000},bool moved=false)
{
    auto& io=ImGui::GetIO(); io.DisplaySize=size;
    ImGui::NewFrame();
    auto* main=ImGui::GetMainViewport();
    if(moved) { main->Pos={120,70}; main->WorkPos={120,95}; main->WorkSize={size.x,size.y-25}; }
    panel.render(nullptr);
    ImGui::Render();
    requireLab(ImGui::GetDrawData()->TotalVtxCount>0,"Moon Lab rendered no widgets");
}

void checkSettings()
{
    std::unique_ptr<Behaviour> registered(FACTORY(Behaviour).create("MoonGeneratorLabComponent"));
    requireLab(bool(registered),"Moon Lab component is absent from the scene factory");
    MoonGeneratorLabComponent settings;
    settings.parameters.seed=271828;
    settings.parameters.width=256;
    settings.parameters.macroScale=1.3f;
    settings.parameters.detailScale=2.4f;
    settings.parameters.octaves=4;
    settings.parameters.fractureCount=13;
    settings.parameters.fractureWidth=.012f;
    settings.parameters.fractureDepth=.15f;
    settings.parameters.fractureStrength=.7f;
    settings.previewWidth=128;
    settings.exportPath="../custom/moon.surface";
    settings.autoGenerate=false; settings.showWindow=false;
    const auto saved=settings.serialize();
    const auto json=nlohmann::json::parse(saved);
    requireLab(json.contains("parameters") && json["parameters"].size()==9,
               "Scene serialization omitted native Moon parameters");
    for(const char* name:{"seed","width","macroScale","detailScale","octaves",
                         "fractureCount","fractureWidth","fractureDepth","fractureStrength"})
        requireLab(json["parameters"].contains(name),std::string("Missing serialized control: ")+name);
    MoonGeneratorLabComponent restored; restored.load(saved);
    requireLab(nlohmann::json::parse(restored.serialize())==json,
               "Moon Lab parameters or editor controls failed the JSON round trip");
    MoonGeneratorLabComponent legacy;
    const auto defaults=nlohmann::json::parse(legacy.serialize());
    legacy.load("{}");
    requireLab(nlohmann::json::parse(legacy.serialize())==defaults,"Legacy scene lost default Moon settings");
    restored.load("{\"previewWidth\":1}");
    requireLab(restored.previewWidth==128,"Scene load accepted an unbounded preview resolution");
    restored.previewWidth=2000000; restored.serialize();
    requireLab(restored.previewWidth==1024,"Scene save accepted an unbounded preview resolution");
    const auto beforeWarnings=warnings;
    restored.load("{not valid json}");
    requireLab(warnings>beforeWarnings,"Invalid scene settings were silently accepted");
    std::cout<<"PASS Moon Lab native registration and scene settings round trip\n"<<std::flush;
}

void runLab()
{
    ImGui::CreateContext();
    struct ReleaseContext { ~ReleaseContext() { ImGui::DestroyContext(); } } context;
    auto& io=ImGui::GetIO(); io.IniFilename=nullptr;
    io.ConfigFlags|=ImGuiConfigFlags_DockingEnable;
    io.DisplaySize={1400,1000}; io.DeltaTime=1.f/60.f;
    unsigned char* font=nullptr; int fw=0,fh=0;
    io.Fonts->GetTexDataAsRGBA32(&font,&fw,&fh);
    checkSettings();

    Ge::Hud hud; ptrClass pc{&hud};
    MoonGeneratorLabComponent owner;
    owner.autoGenerate=false; owner.previewWidth=128;
    owner.parameters.width=256; owner.parameters.octaves=4; owner.parameters.fractureCount=12;
    const auto exportPath=std::filesystem::u8path(owner.exportPath);
    std::filesystem::create_directories(exportPath.parent_path());
    { std::ofstream sentinel(exportPath,std::ios::binary); sentinel<<"Keep the production atlas until explicit export."; }
    const auto original=fileBytes(exportPath);
    owner.start();
    requireLab(!owner.m_panel && !owner.m_error.empty(),"Missing HUD was not reported safely");
    owner.stop();

    services=&pc;
    owner.start();
    requireLab(owner.m_panel && owner.m_error.empty() && hud.m_imguiBlock.size()==1,
               "Start did not attach the native Moon panel to the HUD");
    owner.start();
    requireLab(hud.m_imguiBlock.size()==1 && additions==2 && removals==1,
               "Repeated Play duplicated the Moon panel");
    auto& panel=*owner.m_panel;
    panel.startGenerate(false);
    requireLab(panel.busy() && panel.m_job.valid(),"Native generation did not start asynchronously");
    requireLab(panel.m_job.wait_for(std::chrono::seconds(45))==std::future_status::ready,
               "Native generation worker did not finish");
    requireLab(!panel.m_surface && !panel.m_sphere.id && !panel.m_atlas.id && !panel.m_normals.id,
               "Worker published preview state or GL textures outside the polling thread");

    // Poll in deliberately nondefault renderer state: upload must read the CPU
    // pointer, not an unrelated PBO, and return the caller's GL state intact.
    GLuint retainedTexture=0,retainedPbo=0;
    glGenTextures(1,&retainedTexture); glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D,retainedTexture);
    glGenBuffers(1,&retainedPbo); glBindBuffer(GL_PIXEL_UNPACK_BUFFER,retainedPbo);
    glBufferData(GL_PIXEL_UNPACK_BUFFER,64,nullptr,GL_STATIC_DRAW);
    glPixelStorei(GL_UNPACK_ALIGNMENT,8);
    glPixelStorei(GL_UNPACK_ROW_LENGTH,7); glPixelStorei(GL_UNPACK_IMAGE_HEIGHT,13);
    glPixelStorei(GL_UNPACK_SKIP_PIXELS,2); glPixelStorei(GL_UNPACK_SKIP_ROWS,3);
    glPixelStorei(GL_UNPACK_SKIP_IMAGES,1);
    glPixelStorei(GL_UNPACK_SWAP_BYTES,GL_TRUE); glPixelStorei(GL_UNPACK_LSB_FIRST,GL_TRUE);
    panel.poll();
    GLint active=0,bound=0,pbo=0,alignment=0;
    glGetIntegerv(GL_ACTIVE_TEXTURE,&active); glGetIntegerv(GL_TEXTURE_BINDING_2D,&bound);
    glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING,&pbo); glGetIntegerv(GL_UNPACK_ALIGNMENT,&alignment);
    requireLab(active==GL_TEXTURE3 && GLuint(bound)==retainedTexture && GLuint(pbo)==retainedPbo && alignment==8,
               "Moon preview upload leaked OpenGL texture or pixel-transfer state");
    const std::array<GLenum,7> unpackNames={GL_UNPACK_ROW_LENGTH,GL_UNPACK_IMAGE_HEIGHT,
        GL_UNPACK_SKIP_PIXELS,GL_UNPACK_SKIP_ROWS,GL_UNPACK_SKIP_IMAGES,GL_UNPACK_SWAP_BYTES,GL_UNPACK_LSB_FIRST};
    const std::array<GLint,7> unpackValues={7,13,2,3,1,GL_TRUE,GL_TRUE};
    for(std::size_t i=0;i<unpackNames.size();++i)
    {
        GLint value=0; glGetIntegerv(unpackNames[i],&value);
        requireLab(value==unpackValues[i],"Moon preview upload changed an ambient pixel-unpack setting");
        glPixelStorei(unpackNames[i],0);
    }
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER,0); glPixelStorei(GL_UNPACK_ALIGNMENT,4);
    glBindTexture(GL_TEXTURE_2D,0); glActiveTexture(GL_TEXTURE0);
    glDeleteBuffers(1,&retainedPbo); glDeleteTextures(1,&retainedTexture);
    requireLab(!panel.busy() && panel.m_surface && panel.m_error.empty(),"Initial native preview: "+panel.m_error);
    requireLab(panel.m_surface->width==128 && panel.m_surface->height==64,
               "Preview ignored its separate bounded resolution");
    requireLab(textureBytes(panel.m_atlas)==EnduranceMoon::renderAtlas(*panel.m_surface,false).pixels,
               "Uploaded albedo differs from the native preview bytes");
    requireLab(textureBytes(panel.m_normals)==EnduranceMoon::renderAtlas(*panel.m_surface,true).pixels,
               "Uploaded normals differ from the native preview bytes");
    requireLab(fileBytes(exportPath)==original,"Preview generation overwrote the production atlas");
    std::cout<<"PASS Moon Lab async native generation, GL publication and isolated preview\n"<<std::flush;

    const auto first=panel.m_surface;
    const auto firstSphere=textureBytes(panel.m_sphere);
    owner.parameters.seed+=19;
    panel.startGenerate(false); panel.edited(); waitLab(panel);
    requireLab(panel.m_surface==first && panel.m_dirty && textureBytes(panel.m_sphere)==firstSphere,
               "A generation completed after its settings revision changed");
    panel.startGenerate(false); waitLab(panel);
    requireLab(panel.m_error.empty() && panel.m_surface!=first,"Generation did not recover after a discarded revision");
    const auto current=panel.m_surface;
    const auto currentSphere=textureBytes(panel.m_sphere);
    const auto currentAtlas=textureBytes(panel.m_atlas);
    panel.m_yaw+=67; ++panel.m_viewRevision; panel.m_previewDirty=true;
    panel.startPreview(); waitLab(panel);
    requireLab(panel.m_surface==current && textureBytes(panel.m_atlas)==currentAtlas &&
               textureBytes(panel.m_sphere)!=currentSphere,
               "View changes regenerated the surface or failed to update its lighting preview");
    const auto acceptedSphere=textureBytes(panel.m_sphere);
    panel.m_yaw+=11; ++panel.m_viewRevision; panel.startPreview();
    panel.m_yaw+=23; ++panel.m_viewRevision;
    waitLab(panel);
    requireLab(textureBytes(panel.m_sphere)==acceptedSphere && panel.m_previewDirty,
               "A stale view revision replaced the latest accepted sphere preview");
    panel.startPreview(); waitLab(panel);
    requireLab(textureBytes(panel.m_sphere)!=acceptedSphere && panel.m_surface==current,
               "Sphere preview failed to recover after a stale view revision");
    panel.startGenerate(false); panel.cancel(); waitLab(panel);
    requireLab(panel.m_surface==current && !panel.busy(),"Cancelled generation replaced the retained preview");
    requireLab(fileBytes(exportPath)==original,"Edit or cancellation implicitly wrote the atlas");

    // Export must generate the configured full width from the current controls,
    // independent of the smaller preview and the revision visible on screen.
    owner.parameters.seed+=41;
    panel.startGenerate(true); waitLab(panel);
    requireLab(panel.m_error.empty() && panel.m_surface && panel.m_surface->width==256,
               "Full resolution native export failed: "+panel.m_error);
    const auto exported=fileBytes(exportPath);
    requireLab(exported.size()==20+std::size_t(256)*128*4 &&
               std::memcmp(exported.data(),"EMOON001",8)==0 &&
               little32(exported.data()+8)==256 && little32(exported.data()+12)==128,
               "Explicit export did not produce the engine's EMOON001 binary format");
    requireLab(std::equal(exported.begin()+20,exported.end(),panel.m_surface->pixels.begin()),
               "Exported bytes disagree with the result published by the panel");
    const auto expected=EnduranceMoon::generate(owner.parameters);
    requireLab(panel.m_surface->pixels==expected.pixels,
               "Full-resolution export used stale preview settings instead of current native parameters");
    const auto exportedSurface=panel.m_surface;
    owner.parameters.seed+=7; owner.parameters.width=1024;
    panel.startGenerate(true); panel.cancel(); waitLab(panel);
    requireLab(fileBytes(exportPath)==exported && panel.m_surface==exportedSurface,
               "Cancelled export replaced a previously valid atlas or preview");
    owner.parameters.width=256;
    owner.exportPath="../existing-directory"; std::filesystem::create_directory(owner.exportPath);
    panel.startGenerate(true); waitLab(panel);
    requireLab(!panel.m_error.empty() && std::filesystem::is_directory(owner.exportPath),
               "Export to an invalid destination silently succeeded");
    owner.exportPath=exportPath.u8string();
    panel.startGenerate(false); waitLab(panel);
    requireLab(panel.m_error.empty(),"Successful work retained the previous export error");
    std::cout<<"PASS Moon Lab revisions, cancellation, full binary export and failure recovery\n"<<std::flush;

    for(int i=0;i<6;++i)
    {
        frame(panel,i<3?ImVec2(1400,1000):ImVec2(1920,1080),i>=3);
        const auto* main=ImGui::GetMainViewport();
        const auto* host=ImGui::FindWindowByName("Moon Generator Lab");
        requireLab(host && host->Viewport==main && host->Pos.x==main->WorkPos.x &&
                   host->Pos.y==main->WorkPos.y && host->Size.x==main->WorkSize.x &&
                   host->Size.y==main->WorkSize.y,"Moon Lab host did not follow the main application work area");
        for(const char* name:{"Reglages##MoonLab","Apercu lunaire##MoonLab"})
        {
            const auto* child=ImGui::FindWindowByName(name);
            requireLab(child && child->DockId && child->Viewport==main && child->DockNode &&
                       (child->DockNode->MergedFlags & ImGuiDockNodeFlags_NoUndocking),
                       "Moon Lab child panel escaped the main application dockspace");
        }
    }
    requireLab(glGetError()==GL_NO_ERROR,"OpenGL error from the production Moon Lab lifecycle");
    const std::array<GLuint,3> textures={panel.m_sphere.id,panel.m_atlas.id,panel.m_normals.id};
    owner.previewWidth=512;
    panel.startGenerate(false);
    owner.stop();
    requireLab(!owner.m_panel && hud.m_imguiBlock.empty(),"Stop retained a panel or outstanding HUD callback");
    for(auto texture:textures) requireLab(!glIsTexture(texture),"Stop leaked a Moon preview texture");
    requireLab(fileBytes(exportPath)==exported,"Stop wrote the atlas without an export action");
    owner.stop();

    owner.autoGenerate=true; owner.previewWidth=128;
    owner.start();
    requireLab(owner.m_panel && hud.m_imguiBlock.size()==1,"Reopening the scene failed to recreate the panel");
    auto& reopened=*owner.m_panel;
    for(int i=0;i<180 && !reopened.m_surface;++i)
    {
        frame(reopened); std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    waitLab(reopened);
    requireLab(reopened.m_surface && reopened.m_error.empty(),"Automatic preview failed after close/reopen");
    requireLab(fileBytes(exportPath)==exported,"Automatic preview on reopen overwrote the baked atlas");
    owner.stop();
    requireLab(hud.m_imguiBlock.empty() && additions==removals,"Moon scene lifecycle left unmatched HUD ownership");
    services=nullptr;
    requireLab(glGetError()==GL_NO_ERROR,"OpenGL error after Moon Lab shutdown");
    std::cout<<"PASS Moon Lab ImGui resize, close/reopen, worker join and texture release\n"<<std::flush;
}
}

extern "C" int moon_lab_tests()
{
    try
    {
        glewExperimental=GL_TRUE; const auto status=glewInit();
        requireLab(status==GLEW_OK || status==GLEW_ERROR_NO_GLX_DISPLAY,"GLEW initialization failed");
        while(glGetError()!=GL_NO_ERROR) {}
        runLab();
        return 0;
    }
    catch(const std::exception& error)
    {
        std::cerr<<"FAIL Moon Lab: "<<error.what()<<'\n'; return 1;
    }
}
