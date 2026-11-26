// Production component/panel, procedural solid, GL mesh preview and real ImGui.
// Only the engine service locator and HUD ownership callbacks are fixtures.
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <iterator>
#include <memory>
#include <set>
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
#include "ProceduralMoonExport.hpp"
#define private public
#include "MoonGeneratorLab.hpp"
#include "MoonGeneratorLabPanel.hpp"
#undef private

#define __ENGINE_POINTEUR_CLASS__
struct ptrClass { Ge::Hud* hud=nullptr; };
namespace {
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
#include "../../ANTAGONIZ/Source/ProjectUser/MoonGeneratorLab.cpp"
#include "../../ANTAGONIZ/Source/ProjectUser/MoonGeneratorLabPanel.cpp"

namespace {
using Json=nlohmann::json;
void requireLab(bool condition,const std::string& message)
{ if(!condition) throw std::runtime_error(message); }
void waitLab(MoonGeneratorLabPanel& panel)
{
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(90);
    do {
        panel.poll();
        requireLab(std::chrono::steady_clock::now()<deadline,"Moon Lab asynchronous operation timed out");
        if(panel.busy()) std::this_thread::sleep_for(std::chrono::milliseconds(2));
    } while(panel.busy());
}
std::vector<std::uint8_t> fileBytes(const std::filesystem::path& path)
{
    std::ifstream input(path,std::ios::binary);
    requireLab(bool(input),"Cannot read export: "+path.string());
    return {std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>()};
}
std::uint32_t le32(const std::uint8_t* p)
{ return std::uint32_t(p[0])|(std::uint32_t(p[1])<<8)|(std::uint32_t(p[2])<<16)|(std::uint32_t(p[3])<<24); }
std::uint32_t be32(const std::uint8_t* p)
{ return std::uint32_t(p[3])|(std::uint32_t(p[2])<<8)|(std::uint32_t(p[1])<<16)|(std::uint32_t(p[0])<<24); }
std::vector<std::uint8_t> base64(const std::string& text)
{
    const char* digits="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::vector<std::uint8_t> result;
    unsigned accum=0,bits=0;
    for(char c:text) {
        if(c=='=') break;
        const auto* p=std::strchr(digits,c);
        requireLab(p!=nullptr && c!=0,"Invalid embedded buffer encoding");
        accum=(accum<<6)|unsigned(p-digits); bits+=6;
        if(bits>=8) { bits-=8; result.push_back(std::uint8_t(accum>>bits)); }
    }
    return result;
}
void verifyModel(const std::filesystem::path& path,const EnduranceMoon::Geometry& geometry,int atlasWidth)
{
    const auto contents=fileBytes(path);
    Json json; std::vector<std::uint8_t> binary;
    if(path.extension()==".glb") {
        requireLab(contents.size()>28 && le32(contents.data())==0x46546c67u && le32(contents.data()+4)==2 &&
                   le32(contents.data()+8)==contents.size(),"Invalid GLB header");
        const auto jsonBytes=le32(contents.data()+12);
        requireLab(le32(contents.data()+16)==0x4e4f534au && 20ull+jsonBytes+8<=contents.size(),"Invalid GLB JSON chunk");
        json=Json::parse(contents.begin()+20,contents.begin()+20+jsonBytes);
        const auto offset=20+jsonBytes;
        requireLab(le32(contents.data()+offset+4)==0x004e4942u && offset+8ull+le32(contents.data()+offset)==contents.size(),"Invalid GLB binary chunk");
        binary.assign(contents.begin()+offset+8,contents.end());
    } else {
        json=Json::parse(contents.begin(),contents.end());
        const auto uri=json.at("buffers").at(0).at("uri").get<std::string>();
        const auto comma=uri.find(',');
        requireLab(uri.find("data:")==0 && comma!=std::string::npos,"glTF buffer is not self-contained");
        binary=base64(uri.substr(comma+1));
    }
    requireLab(json.at("asset").at("version")=="2.0","Unexpected glTF asset version");
    const auto declared=json.at("buffers").at(0).at("byteLength").get<std::size_t>();
    requireLab(binary.size()>=declared && binary.size()-declared<4,"glTF buffer size mismatch");
    for(const auto& view:json.at("bufferViews"))
        requireLab(view.value("byteOffset",std::size_t(0))+view.at("byteLength").get<std::size_t>()<=declared,"glTF buffer view out of bounds");
    std::size_t totalIndices=0,partIndex=0;
    requireLab(json.at("meshes").size()==geometry.parts.size(),"glTF does not retain separate fragment parts");
    for(const auto& mesh:json.at("meshes")) for(const auto& primitive:mesh.at("primitives")) {
        const auto position=primitive.at("attributes").at("POSITION").get<unsigned>();
        const auto& part=geometry.parts.at(partIndex++);
        std::set<std::uint32_t> used(geometry.indices.begin()+part.firstIndex,geometry.indices.begin()+part.firstIndex+part.indexCount);
        const auto vertexCount=json.at("accessors").at(position).at("count").get<std::size_t>();
        requireLab(vertexCount==used.size(),"glTF part duplicates or omits vertices");
        const auto index=primitive.at("indices").get<unsigned>();
        const auto& accessor=json.at("accessors").at(index);
        const auto count=accessor.at("count").get<std::size_t>(); totalIndices+=count;
        const auto& view=json.at("bufferViews").at(accessor.at("bufferView").get<unsigned>());
        const auto start=view.value("byteOffset",std::size_t(0))+accessor.value("byteOffset",std::size_t(0));
        requireLab(start+count*4<=declared,"glTF index range out of bounds");
        for(std::size_t i=0;i<count;++i) requireLab(le32(binary.data()+start+i*4)<vertexCount,"glTF index not local to fragment");
        requireLab(primitive.at("attributes").contains("NORMAL") && primitive.at("attributes").contains("TEXCOORD_0"),"glTF omits normals or UVs");
    }
    requireLab(totalIndices==geometry.indices.size(),"glTF primitive ranges differ from generated geometry");
    std::vector<std::uint8_t> png;
    const auto& image=json.at("images").at(0);
    if(image.contains("bufferView")) {
        const auto& view=json.at("bufferViews").at(image.at("bufferView").get<unsigned>());
        const auto start=view.value("byteOffset",std::size_t(0)),size=view.at("byteLength").get<std::size_t>();
        png.assign(binary.begin()+start,binary.begin()+start+size);
    } else {
        const auto uri=image.at("uri").get<std::string>(); png=base64(uri.substr(uri.find(',')+1));
    }
    requireLab(png.size()>24 && std::memcmp(png.data(),"\x89PNG\r\n\x1a\n",8)==0 &&
               be32(png.data()+16)==unsigned(atlasWidth) && be32(png.data()+20)==unsigned(atlasWidth/2),"Exported albedo PNG is missing or has wrong dimensions");
}
std::vector<std::uint8_t> textureBytes(unsigned texture)
{
    requireLab(texture && glIsTexture(texture),"Preview texture missing");
    GLint width=0,height=0;
    glGetTextureLevelParameteriv(texture,0,GL_TEXTURE_WIDTH,&width);
    glGetTextureLevelParameteriv(texture,0,GL_TEXTURE_HEIGHT,&height);
    requireLab(width>0 && height>0,"Preview texture has no storage");
    glBindBuffer(GL_PIXEL_PACK_BUFFER,0);
    glPixelStorei(GL_PACK_ALIGNMENT,1); glPixelStorei(GL_PACK_ROW_LENGTH,0);
    glPixelStorei(GL_PACK_SKIP_PIXELS,0); glPixelStorei(GL_PACK_SKIP_ROWS,0);
    std::vector<std::uint8_t> result(std::size_t(width)*height*4);
    glGetTextureImage(texture,0,GL_RGBA,GL_UNSIGNED_BYTE,GLsizei(result.size()),result.data());
    return result;
}
void dumpPreview(const EnduranceMoon::MoonMeshPreview& preview,const char* name)
{
    const char* folder=std::getenv("ENDURANCE_MOON_CAPTURE_DIR");
    if(!folder || !*folder) return;
    std::filesystem::create_directories(folder);
    const auto bytes=textureBytes(preview.texture());
    std::ofstream output(std::filesystem::path(folder)/(std::string(name)+".ppm"),std::ios::binary);
    output<<"P6\n"<<preview.width()<<' '<<preview.height()<<"\n255\n";
    for(int y=preview.height()-1;y>=0;--y) for(int x=0;x<preview.width();++x)
        output.write(reinterpret_cast<const char*>(bytes.data()+(std::size_t(y)*preview.width()+x)*4),3);
}
void frame(MoonGeneratorLabPanel& panel,ImVec2 size={1400,1000},bool moved=false)
{
    auto& io=ImGui::GetIO(); io.DisplaySize=size;
    ImGui::NewFrame();
    auto* main=ImGui::GetMainViewport();
    if(moved) { main->Pos={120,70}; main->WorkPos={120,95}; main->WorkSize={size.x,size.y-25}; }
    panel.render(nullptr); ImGui::Render();
    requireLab(ImGui::GetDrawData()->TotalVtxCount>0,"Moon Lab rendered no widgets");
}
void checkSettings()
{
    std::unique_ptr<Behaviour> registered(FACTORY(Behaviour).create("MoonGeneratorLabComponent"));
    requireLab(bool(registered),"Moon Lab component absent from factory");
    MoonGeneratorLabComponent settings;
    settings.parameters.seed=271828; settings.parameters.width=256;
    settings.geometry.resolution=48; settings.geometry.cavityCount=4; settings.geometry.cavityDepth=.66f;
    settings.geometry.debrisCount=7; settings.geometry.excavationYaw=57;
    settings.previewWidth=128; settings.previewResolution=32;
    settings.exportPath="../custom/moon.glb"; settings.autoGenerate=false; settings.showWindow=false;
    const auto saved=settings.serialize(); const auto json=Json::parse(saved);
    requireLab(json.contains("parameters") && json.contains("geometry"),"Scene serialization omitted material or solid parameters");
    for(const char* name:{"seed","resolution","excavationStrength","cavityCount","cavitySize","cavityDepth","excavationYaw",
                         "excavationPitch","erosionStrength","erosionScale","erosionOctaves","debrisCount","debrisSpread","debrisSize"})
        requireLab(json["geometry"].contains(name),std::string("Missing serialized geometry control: ")+name);
    MoonGeneratorLabComponent restored; restored.load(saved);
    requireLab(Json::parse(restored.serialize())==json,"Moon scene round trip changed controls");
    MoonGeneratorLabComponent legacy; const auto defaults=Json::parse(legacy.serialize());
    legacy.load("{}"); requireLab(Json::parse(legacy.serialize())==defaults,"Empty legacy scene lost defaults");
    restored.load("{\"previewWidth\":1,\"previewResolution\":2000000}");
    requireLab(restored.previewWidth>=64 && restored.previewResolution<=256,"Scene load accepted unbounded preview settings");
    const auto count=warnings; restored.load("{not valid json}");
    requireLab(warnings>count,"Invalid scene silently accepted");
    std::cout<<"PASS Moon Lab registration, geometry/material serialization and legacy defaults\n"<<std::flush;
}

// Deliberately adversarial ambient HUD state: the mesh renderer must reset all
// state it needs and restore it on return, including depth conventions and PBOs.
void checkGlState(EnduranceMoon::MoonMeshPreview& preview,const EnduranceMoon::MeshPreviewSettings& view,
                  const EnduranceMoon::Geometry& geometry,const EnduranceMoon::Surface& surface)
{
    GLuint pbo=0,vao=0,texture=0,sampler=0;
    glCreateBuffers(1,&pbo); glNamedBufferData(pbo,256,nullptr,GL_STATIC_DRAW);
    glCreateVertexArrays(1,&vao); glBindVertexArray(vao);
    glCreateTextures(GL_TEXTURE_2D,1,&texture); glTextureStorage2D(texture,1,GL_RGBA8,2,2);
    glCreateSamplers(1,&sampler); glBindSampler(0,sampler);
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D,texture);
    glActiveTexture(GL_TEXTURE3);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER,pbo); glBindBuffer(GL_PIXEL_PACK_BUFFER,pbo);
    glPixelStorei(GL_UNPACK_ALIGNMENT,8); glPixelStorei(GL_UNPACK_ROW_LENGTH,11);
    glPixelStorei(GL_PACK_ALIGNMENT,8); glPixelStorei(GL_PACK_ROW_LENGTH,11);
    glViewport(11,12,13,14); glScissor(1,2,3,4); glEnable(GL_SCISSOR_TEST);
    glEnable(GL_RASTERIZER_DISCARD); glDisable(GL_DEPTH_TEST); glDepthMask(GL_FALSE);
    glDepthFunc(GL_GREATER); glDepthRange(.2,.8);
    glClipControl(GL_UPPER_LEFT,GL_ZERO_TO_ONE);
    glPolygonMode(GL_FRONT_AND_BACK,GL_LINE); glColorMaski(0,GL_FALSE,GL_TRUE,GL_FALSE,GL_FALSE);
    glEnablei(GL_BLEND,0); glEnable(GL_CULL_FACE); glCullFace(GL_FRONT); glFrontFace(GL_CW);
    requireLab(preview.upload(geometry,surface),"Upload failed with nondefault pixel-transfer state: "+preview.error());
    auto changed=view; changed.yaw+=7.f;
    requireLab(preview.render(changed),"Preview failed with nondefault HUD state: "+preview.error());
    GLint current=0; GLboolean write=GL_TRUE; std::array<GLint,4> viewport{};
    glGetIntegerv(GL_ACTIVE_TEXTURE,&current); requireLab(current==GL_TEXTURE3,"Preview leaked active texture");
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING,&current); requireLab(GLuint(current)==vao,"Preview leaked VAO");
    glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING,&current); requireLab(GLuint(current)==pbo,"Preview leaked unpack PBO");
    glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING,&current); requireLab(GLuint(current)==pbo,"Preview leaked pack PBO");
    glGetIntegerv(GL_UNPACK_ROW_LENGTH,&current); requireLab(current==11,"Preview leaked unpack stride");
    glGetIntegerv(GL_PACK_ROW_LENGTH,&current); requireLab(current==11,"Preview leaked pack stride");
    glGetIntegerv(GL_VIEWPORT,viewport.data()); requireLab(viewport==std::array<GLint,4>{{11,12,13,14}},"Preview leaked viewport");
    glGetBooleanv(GL_DEPTH_WRITEMASK,&write); requireLab(write==GL_FALSE,"Preview leaked depth writes");
    glGetIntegerv(GL_CULL_FACE_MODE,&current); requireLab(current==GL_FRONT && glIsEnabled(GL_CULL_FACE),"Preview leaked cull mode");
    glGetIntegerv(GL_FRONT_FACE,&current); requireLab(current==GL_CW,"Preview leaked front winding");
    glGetIntegerv(GL_CLIP_ORIGIN,&current); requireLab(current==GL_UPPER_LEFT,"Preview leaked clip origin");
    glGetIntegerv(GL_CLIP_DEPTH_MODE,&current); requireLab(current==GL_ZERO_TO_ONE,"Preview leaked depth convention");
    requireLab(glIsEnabled(GL_SCISSOR_TEST) && glIsEnabled(GL_RASTERIZER_DISCARD) && !glIsEnabled(GL_DEPTH_TEST),"Preview leaked raster capabilities");
    glGetIntegeri_v(GL_SAMPLER_BINDING,0,&current); requireLab(GLuint(current)==sampler,"Preview leaked sampler");
    glActiveTexture(GL_TEXTURE0); glGetIntegerv(GL_TEXTURE_BINDING_2D,&current); requireLab(GLuint(current)==texture,"Preview leaked material binding");
    glBindSampler(0,0); glBindTexture(GL_TEXTURE_2D,0); glBindVertexArray(0);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER,0); glBindBuffer(GL_PIXEL_PACK_BUFFER,0);
    glPixelStorei(GL_UNPACK_ALIGNMENT,4); glPixelStorei(GL_UNPACK_ROW_LENGTH,0);
    glPixelStorei(GL_PACK_ALIGNMENT,4); glPixelStorei(GL_PACK_ROW_LENGTH,0);
    glDisable(GL_SCISSOR_TEST); glDisable(GL_RASTERIZER_DISCARD); glDisablei(GL_BLEND,0);
    glDisable(GL_CULL_FACE); glCullFace(GL_BACK); glFrontFace(GL_CCW);
    glColorMaski(0,GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE); glDepthMask(GL_TRUE); glDepthFunc(GL_LESS);
    glDepthRange(0,1); glClipControl(GL_LOWER_LEFT,GL_NEGATIVE_ONE_TO_ONE); glPolygonMode(GL_FRONT_AND_BACK,GL_FILL);
    glDeleteTextures(1,&texture); glDeleteSamplers(1,&sampler); glDeleteVertexArrays(1,&vao); glDeleteBuffers(1,&pbo);
    requireLab(glGetError()==GL_NO_ERROR,"Preview generated a GL error under adversarial state");
}

void runLab()
{
    ImGui::CreateContext();
    struct ReleaseContext { ~ReleaseContext() { ImGui::DestroyContext(); } } context;
    auto& io=ImGui::GetIO(); io.IniFilename=nullptr; io.ConfigFlags|=ImGuiConfigFlags_DockingEnable;
    io.DisplaySize={1400,1000}; io.DeltaTime=1.f/60.f;
    unsigned char* font=nullptr; int fw=0,fh=0; io.Fonts->GetTexDataAsRGBA32(&font,&fw,&fh);
    checkSettings();
    Ge::Hud hud; ptrClass pc{&hud}; MoonGeneratorLabComponent owner;
    owner.autoGenerate=false; owner.previewWidth=128; owner.previewResolution=32;
    owner.geometry.resolution=48; owner.geometry.debrisCount=5;
    owner.parameters.width=256; owner.parameters.octaves=4;
    owner.exportPath="../baked/moon.gltf";
    const auto exportPath=std::filesystem::u8path(owner.exportPath);
    std::filesystem::create_directories(exportPath.parent_path());
    { std::ofstream sentinel(exportPath); sentinel<<"Keep old asset until explicit export."; }
    const auto original=fileBytes(exportPath);
    owner.start(); requireLab(!owner.m_panel && !owner.m_error.empty(),"Missing HUD not reported"); owner.stop();
    services=&pc; owner.start(); owner.start();
    requireLab(owner.m_panel && hud.m_imguiBlock.size()==1 && additions==2 && removals==1,"Repeated Play duplicated panel");
    auto& panel=*owner.m_panel;
    panel.startGenerate(false);
    requireLab(panel.busy() && panel.m_job.valid(),"Generation not asynchronous");
    requireLab(panel.m_job.wait_for(std::chrono::seconds(90))==std::future_status::ready,"Worker did not finish");
    requireLab(!panel.m_surface && !panel.m_geometry,"Worker published CPU/GL state before poll");
    panel.poll();
    requireLab(!panel.busy() && panel.m_geometry && panel.m_surface && panel.m_error.empty(),"Native mesh preview failed: "+panel.m_error);
    requireLab(panel.m_surface->width==128,"Preview ignored material resolution");
    requireLab(fileBytes(exportPath)==original,"Preview overwrote export");
    auto& preview=panel.m_preview;
    EnduranceMoon::MeshPreviewSettings view; view.width=view.height=640; view.yaw=15; view.sunYaw=-35;
    requireLab(preview.render(view),"Initial real mesh render: "+preview.error());
    const auto first=textureBytes(preview.texture()); dumpPreview(preview,"moon-mesh-lit");
    requireLab(preview.render(view) && textureBytes(preview.texture())==first,"Unchanged cached render differs");
    view.shadows=false;
    requireLab(preview.render(view),"Unshadowed view failed"); const auto unshadowed=textureBytes(preview.texture()); dumpPreview(preview,"moon-mesh-unshadowed");
    std::size_t darker=0;
    for(std::size_t i=0;i<first.size();i+=4) if(int(unshadowed[i])+int(unshadowed[i+1])+int(unshadowed[i+2])>
        int(first[i])+int(first[i+1])+int(first[i+2])+6) ++darker;
    requireLab(darker>100,"Real cavity shadows have no measurable effect");
    view.shadows=true; view.normals=true;
    requireLab(preview.render(view) && textureBytes(preview.texture())!=first,"Normals debug view not active");
    dumpPreview(preview,"moon-mesh-normals"); view.normals=false;
    checkGlState(preview,view,*panel.m_geometry,*panel.m_surface);
    std::cout<<"PASS Moon Lab async solid/atlas publication, actual mesh GL rendering, local shadows and state restoration\n"<<std::flush;

    const auto firstGeometry=panel.m_geometry; const auto firstSurface=panel.m_surface;
    owner.geometry.cavityDepth+=.02f; panel.startGenerate(false); waitLab(panel);
    requireLab(panel.m_surface==firstSurface && panel.m_geometry!=firstGeometry,"Shape-only edit unnecessarily rebuilt material atlas");
    const auto shapeOnly=panel.m_geometry;
    owner.parameters.macroScale+=.1f; panel.startGenerate(false); waitLab(panel);
    requireLab(panel.m_geometry==shapeOnly && panel.m_surface!=firstSurface,"Material-only edit unnecessarily rebuilt geometry");
    const auto geometry=panel.m_geometry; const auto surface=panel.m_surface;
    owner.geometry.seed+=19; panel.startGenerate(false); panel.edited(); waitLab(panel);
    requireLab(panel.m_geometry==geometry && panel.m_surface==surface && panel.m_dirty,"Stale revision was published");
    panel.startGenerate(false); waitLab(panel);
    requireLab(panel.m_geometry!=geometry && panel.m_error.empty(),"Generation did not recover after stale revision");
    const auto currentGeometry=panel.m_geometry; const auto currentSurface=panel.m_surface;
    view.yaw+=90; view.sunYaw+=30;
    requireLab(preview.render(view) && panel.m_geometry==currentGeometry && panel.m_surface==currentSurface,"Orbit or sun regenerated the solid/atlas");
    dumpPreview(preview,"moon-mesh-side");
    panel.startGenerate(false); panel.cancel(); waitLab(panel);
    requireLab(panel.m_geometry==currentGeometry && panel.m_surface==currentSurface,"Cancellation published stale geometry");
    owner.parameters.seed+=41; owner.geometry.seed+=41;
    panel.startGenerate(true); waitLab(panel);
    requireLab(panel.m_error.empty() && panel.m_surface->width==256,"Export generation failed: "+panel.m_error);
    verifyModel(exportPath,*panel.m_geometry,256);
    const auto exported=fileBytes(exportPath); const auto exportedGeometry=panel.m_geometry;
    std::string exportError;
    requireLab(EnduranceMoon::saveModel(*panel.m_geometry,*panel.m_surface,"../baked/moon.glb",exportError),"GLB export failed: "+exportError);
    verifyModel("../baked/moon.glb",*panel.m_geometry,256);
    EnduranceMoon::Geometry malformed=*panel.m_geometry; malformed.indices[0]=std::uint32_t(malformed.vertices.size());
    requireLab(!EnduranceMoon::saveModel(malformed,*panel.m_surface,owner.exportPath,exportError) && fileBytes(exportPath)==exported,
               "Invalid export damaged a valid asset");
    owner.geometry.seed+=7; owner.geometry.resolution=128; owner.parameters.width=1024;
    panel.startGenerate(true); panel.cancel(); waitLab(panel);
    requireLab(fileBytes(exportPath)==exported && panel.m_geometry==exportedGeometry,"Cancelled export replaced asset or preview");
    owner.geometry.resolution=48; owner.parameters.width=256;
    owner.exportPath="../directory.gltf"; std::filesystem::create_directory(owner.exportPath);
    panel.startGenerate(true); waitLab(panel);
    requireLab(!panel.m_error.empty() && std::filesystem::is_directory(owner.exportPath),"Export to directory silently succeeded");
    owner.exportPath=exportPath.u8string(); panel.startGenerate(false); waitLab(panel);
    requireLab(panel.m_error.empty(),"Successful retry kept stale error");
    std::cout<<"PASS Moon Lab stale/cancel handling, embedded glTF/GLB geometry + PNG export, safe failures\n"<<std::flush;

    for(int i=0;i<6;++i) {
        frame(panel,i<3?ImVec2(1400,1000):ImVec2(1920,1080),i>=3);
        const auto* main=ImGui::GetMainViewport(); const auto* host=ImGui::FindWindowByName("Moon Generator Lab");
        requireLab(host && host->Viewport==main && host->Pos.x==main->WorkPos.x && host->Pos.y==main->WorkPos.y &&
                   host->Size.x==main->WorkSize.x && host->Size.y==main->WorkSize.y,"Lab did not follow main work area");
        for(const char* name:{"Reglages##MoonLab","Apercu lunaire##MoonLab"}) {
            const auto* child=ImGui::FindWindowByName(name);
            requireLab(child && child->DockId && child->Viewport==main && child->DockNode &&
                       (child->DockNode->MergedFlags&ImGuiDockNodeFlags_NoUndocking),"Child escaped main dockspace");
        }
    }
    requireLab(glGetError()==GL_NO_ERROR,"GL error from lab lifecycle");
    const GLuint texture=preview.texture(); owner.previewResolution=96; panel.startGenerate(false); owner.stop();
    requireLab(!owner.m_panel && hud.m_imguiBlock.empty() && !glIsTexture(texture),"Stop leaked panel/worker/preview texture");
    requireLab(fileBytes(exportPath)==exported,"Stop changed exported asset"); owner.stop();
    owner.autoGenerate=true; owner.previewResolution=32; owner.start();
    auto& reopened=*owner.m_panel;
    for(int i=0;i<360 && !reopened.m_geometry;++i) { frame(reopened); std::this_thread::sleep_for(std::chrono::milliseconds(2)); }
    waitLab(reopened);
    requireLab(reopened.m_geometry && reopened.m_error.empty(),"Automatic preview failed after reopen");
    requireLab(fileBytes(exportPath)==exported,"Reopening wrote the model"); owner.stop(); services=nullptr;
    requireLab(hud.m_imguiBlock.empty() && additions==removals && glGetError()==GL_NO_ERROR,"Lab ownership/GL shutdown failure");
    std::cout<<"PASS Moon Lab docking, resize, reopen, worker join and GL resource release\n"<<std::flush;
    if(std::getenv("ENDURANCE_MOON_CAPTURE_DIR")) {
        EnduranceMoon::GeometryParams gp; gp.resolution=96;
        EnduranceMoon::Params mp; mp.width=1024; mp.fractureCount=0; mp.fractureStrength=0;
        const auto model=EnduranceMoon::generateGeometry(gp);
        const auto material=EnduranceMoon::generate(mp);
        EnduranceMoon::MoonMeshPreview hero;
        requireLab(hero.initialize() && hero.upload(model,material),"Hero capture initialization failed: "+hero.error());
        EnduranceMoon::MeshPreviewSettings shot; shot.width=1024; shot.height=1024;
        requireLab(hero.render(shot),"Hero preview failed"); dumpPreview(hero,"moon-final-default");
        shot.sunYaw=60;
        requireLab(hero.render(shot),"Hero relighting failed"); dumpPreview(hero,"moon-final-interior");
        shot.shadows=false;
        requireLab(hero.render(shot),"Hero unshadowed failed"); dumpPreview(hero,"moon-final-unshadowed");
        shot.normals=true;
        requireLab(hero.render(shot),"Hero normal preview failed"); dumpPreview(hero,"moon-final-normals");
        std::cout<<"PASS final96 capture: "<<model.vertices.size()<<" vertices, "<<model.indices.size()/3<<" triangles\n";
    }
}
}
extern "C" int moon_lab_tests()
{
    try {
        glewExperimental=GL_TRUE; const auto status=glewInit();
        requireLab(status==GLEW_OK || status==GLEW_ERROR_NO_GLX_DISPLAY,"GLEW initialization failed");
        while(glGetError()!=GL_NO_ERROR) {}
        runLab(); return 0;
    } catch(const std::exception& error) { std::cerr<<"FAIL Moon Lab: "<<error.what()<<'\n'; return 1; }
}
