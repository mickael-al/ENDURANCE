#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "MoonGeneratorLabPanel.hpp"
#include "MoonGeneratorLab.hpp"
#include "ProceduralMoonExport.hpp"
#include "glcore.hpp"
#include "imgui-cmake/Header/imgui_internal.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <stdexcept>
#include <tuple>
#include <utility>

namespace
{
bool inputPath(const char* label,std::string& value)
{
    std::array<char,2048> buffer{};
    std::memcpy(buffer.data(),value.data(),(std::min)(value.size(),buffer.size()-1));
    if(!ImGui::InputText(label,buffer.data(),buffer.size())) return false;
    value=buffer.data();
    return true;
}
ImTextureID textureID(unsigned id)
{ return reinterpret_cast<ImTextureID>(static_cast<std::intptr_t>(id)); }
bool resolution(const char* label,int& width,bool exportResolution)
{
    const int count=exportResolution ? 6 : 4;
    int selection=0;
    while(selection+1<count && (128<<(selection+1))<=width) ++selection;
    if(!ImGui::Combo(label,&selection,exportResolution ?
        "128\0" "256\0" "512\0" "1024\0" "2048\0" "4096\0" :
        "128\0" "256\0" "512\0" "1024\0")) return false;
    width=128<<selection;
    return true;
}
bool meshResolution(const char* label,int& value,bool exporting)
{
    const int levels[]{32,48,64,96,128,192,256};
    const int count=exporting ? 7 : 5;
    int selected=0;
    while(selected+1<count && levels[selected+1]<=value) ++selected;
    if(!ImGui::Combo(label,&selected,exporting ?
        "32\0" "48\0" "64\0" "96\0" "128\0" "192\0" "256\0" :
        "32\0" "48\0" "64\0" "96\0" "128\0")) return false;
    value=levels[selected]; return true;
}
bool same(const EnduranceMoon::Params& a,const EnduranceMoon::Params& b)
{
    return std::tie(a.seed,a.width,a.macroScale,a.detailScale,a.octaves,
        a.fractureCount,a.fractureWidth,a.fractureDepth,a.fractureStrength)==
        std::tie(b.seed,b.width,b.macroScale,b.detailScale,b.octaves,
        b.fractureCount,b.fractureWidth,b.fractureDepth,b.fractureStrength);
}
bool same(const EnduranceMoon::GeometryParams& a,const EnduranceMoon::GeometryParams& b)
{
    return std::tie(a.seed,a.resolution,a.excavationStrength,a.cavityCount,a.cavitySize,
        a.cavityDepth,a.excavationYaw,a.excavationPitch,a.erosionStrength,a.erosionScale,
        a.erosionOctaves,a.debrisCount,a.debrisSpread,a.debrisSize)==
        std::tie(b.seed,b.resolution,b.excavationStrength,b.cavityCount,b.cavitySize,
        b.cavityDepth,b.excavationYaw,b.excavationPitch,b.erosionStrength,b.erosionScale,
        b.erosionOctaves,b.debrisCount,b.debrisSpread,b.debrisSize);
}

// DSA preserves texture bindings. CPU uploads still honor the unpack buffer
// and pixel-store state, which can be arbitrary when called from the HUD.
class UnpackState final
{
public:
    UnpackState()
    {
        glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING,&buffer);
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER,0);
        for(std::size_t i=0;i<names.size();++i)
        {
            glGetIntegerv(names[i],&values[i]);
            glPixelStorei(names[i],names[i]==GL_UNPACK_ALIGNMENT ? 1 : 0);
        }
    }
    ~UnpackState()
    {
        for(std::size_t i=0;i<names.size();++i) glPixelStorei(names[i],values[i]);
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER,static_cast<GLuint>(buffer));
    }
private:
    GLint buffer=0;
    const std::array<GLenum,8> names{{GL_UNPACK_ALIGNMENT,GL_UNPACK_ROW_LENGTH,
        GL_UNPACK_IMAGE_HEIGHT,GL_UNPACK_SKIP_PIXELS,GL_UNPACK_SKIP_ROWS,
        GL_UNPACK_SKIP_IMAGES,GL_UNPACK_SWAP_BYTES,GL_UNPACK_LSB_FIRST}};
    std::array<GLint,8> values{};
};
}

MoonGeneratorLabPanel::MoonGeneratorLabPanel(MoonGeneratorLabComponent& owner):m_owner(owner) {}
MoonGeneratorLabPanel::~MoonGeneratorLabPanel()
{
    if(m_cancel) m_cancel->store(true);
    if(m_job.valid()) m_job.wait();
    glDeleteTextures(1,&m_atlas.id);
    // The GPU preview is destroyed on this same main/context thread.
}

bool MoonGeneratorLabPanel::busy() const { return m_jobKind!=JobKind::None; }
void MoonGeneratorLabPanel::init(GraphicsDataMisc*)
{
    m_generateRequested=m_owner.autoGenerate;
    m_status="Sculpture 3D hors jeu. Aucun fichier ecrit avant l'export.";
}
void MoonGeneratorLabPanel::edited()
{
    ++m_revision;
    m_dirty=true;
    m_generationFailed=false;
    m_lastEdit=ImGui::GetTime();
    if(m_jobKind==JobKind::Generate && m_cancel) m_cancel->store(true);
}
void MoonGeneratorLabPanel::cancel()
{
    if(m_cancel) m_cancel->store(true);
    m_generateRequested=false;
    m_generationFailed=true; // Manual cancellation must not auto-retry.
    if(busy()) m_dirty=true;
}

void MoonGeneratorLabPanel::startGenerate(bool forExport)
{
    if(busy()) return;
    if(forExport)
    {
        const auto extension=std::filesystem::path(m_owner.exportPath).extension().string();
        if(extension!=".glb" && extension!=".gltf")
        { m_error="Choisissez un fichier .glb ou .gltf pour le modele."; return; }
    }
    try
    {
        auto shape=EnduranceMoon::sanitizeGeometry(m_owner.geometry);
        m_owner.geometry=shape;
        m_owner.parameters.seed=shape.seed;
        auto params=EnduranceMoon::sanitize(m_owner.parameters);
        m_owner.parameters=params;
        if(!forExport)
        {
            params.width=std::clamp(m_owner.previewWidth,128,1024);
            shape.resolution=std::clamp(m_owner.previewResolution,32,128);
        }
        params=EnduranceMoon::sanitize(params);
        shape=EnduranceMoon::sanitizeGeometry(shape);
        // Material edits reuse the sculpted volume; geometry edits reuse the
        // expensive high-detail atlas. Camera and light never enqueue a job.
        const auto previousSurface=(m_surface && same(params,m_surfaceParameters)) ? m_surface : nullptr;
        const auto previousGeometry=(m_geometry && same(shape,m_geometryParameters)) ? m_geometry : nullptr;
        m_cancel=std::make_shared<std::atomic_bool>(false);
        m_stage=std::make_shared<std::atomic_int>(0);
        const auto cancelToken=m_cancel;
        const auto stage=m_stage;
        const auto path=forExport ? m_owner.exportPath : std::string{};
        m_submittedRevision=m_revision;
        m_job=std::async(std::launch::async,[params,shape,path,cancelToken,stage,previousSurface,previousGeometry]() {
            Result result;
            const auto started=std::chrono::steady_clock::now();
            try
            {
                auto geometry=previousGeometry;
                if(!geometry) geometry=std::make_shared<EnduranceMoon::Geometry>(
                    EnduranceMoon::generateGeometry(shape,cancelToken.get()));
                if(cancelToken->load()) { result.cancelled=true; return result; }
                if(geometry->vertices.empty() || geometry->indices.empty())
                    throw std::runtime_error("La sculpture a produit un maillage vide. Reduisez l'excavation.");
                stage->store(1);
                auto surface=previousSurface;
                if(!surface) surface=std::make_shared<EnduranceMoon::Surface>(
                    EnduranceMoon::generate(params,cancelToken.get()));
                if(cancelToken->load()) { result.cancelled=true; return result; }
                if(surface->pixels.empty()) throw std::runtime_error("La texture lunaire est vide.");
                stage->store(2);
                result.atlas=EnduranceMoon::renderAtlas(*surface,false);
                if(result.atlas.pixels.empty()) throw std::runtime_error("Impossible de preparer l'apercu de la texture.");
                if(cancelToken->load()) { result.cancelled=true; return result; }
                if(!path.empty())
                {
                    stage->store(3);
                    if(!EnduranceMoon::saveModel(*geometry,*surface,path,result.error,&shape,&params)) return result;
                    result.path=path;
                }
                result.surface=std::move(surface);
                result.geometry=std::move(geometry);
                result.surfaceParameters=params;
                result.geometryParameters=shape;
                result.milliseconds=std::chrono::duration<double,std::milli>(
                    std::chrono::steady_clock::now()-started).count();
                stage->store(4);
            }
            catch(const std::exception& e) { result.error=e.what(); }
            catch(...) { result.error="Erreur inconnue pendant la sculpture lunaire."; }
            return result;
        });
        m_jobKind=forExport ? JobKind::Export : JobKind::Generate;
        m_jobStarted=ImGui::GetTime();
        m_generateRequested=false;
        m_generationFailed=false;
        m_error.clear();
        m_status=forExport ? "Calcul du modele a la resolution d'export..." : "Sculpture du volume sur CPU...";
    }
    catch(const std::exception& e)
    {
        m_error=e.what();
        m_generateRequested=false;
        m_generationFailed=true;
        m_dirty=true;
    }
}

bool MoonGeneratorLabPanel::upload(Texture& texture,const EnduranceMoon::PreviewImage& image)
{
    if(image.width<1 || image.height<1 || image.width>1024 || image.height>1024 ||
       image.pixels.size()!=static_cast<std::size_t>(image.width)*image.height*4)
    { m_error="Image d'apercu invalide."; return false; }
    UnpackState state;
    GLuint next=0;
    glCreateTextures(GL_TEXTURE_2D,1,&next);
    glTextureStorage2D(next,1,GL_RGBA8,image.width,image.height);
    glTextureParameteri(next,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
    glTextureParameteri(next,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glTextureParameteri(next,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
    glTextureParameteri(next,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    glTextureSubImage2D(next,0,0,0,image.width,image.height,GL_RGBA,GL_UNSIGNED_BYTE,image.pixels.data());
    if(!next || glGetError()!=GL_NO_ERROR)
    {
        if(next) glDeleteTextures(1,&next);
        m_error="Impossible d'envoyer l'apercu vers le GPU.";
        return false;
    }
    if(texture.id) glDeleteTextures(1,&texture.id);
    texture={next,image.width,image.height};
    return true;
}

void MoonGeneratorLabPanel::poll()
{
    if(!m_job.valid() || m_job.wait_for(std::chrono::seconds(0))!=std::future_status::ready) return;
    const bool cancelled=m_cancel && m_cancel->load();
    m_jobKind=JobKind::None;
    Result result;
    try { result=m_job.get(); }
    catch(const std::exception& e) { result.error=e.what(); }
    catch(...) { result.error="Erreur inconnue pendant le travail CPU."; }
    m_cancel.reset();
    m_stage.reset();
    // An export committed during Cancel remains a successful write.
    if((cancelled || result.cancelled) && result.path.empty())
    {
        m_status="Operation annulee. Le modele precedent est conserve.";
        return;
    }
    if(!result.error.empty())
    {
        m_error=result.error;
        m_status="Echec : le modele precedent est conserve.";
        m_dirty=true; m_generationFailed=true;
        return;
    }
    if(m_submittedRevision!=m_revision)
    {
        if(!result.path.empty()) m_status="Modele exporte : "+result.path+". Les reglages ont ensuite change.";
        return;
    }
    if(!result.surface || !result.geometry)
    {
        m_error="Le calcul n'a produit aucun modele complet.";
        m_dirty=true; m_generationFailed=true;
        return;
    }
    Texture atlas;
    if(!upload(atlas,result.atlas) || !m_preview.initialize() || !m_preview.upload(*result.geometry,*result.surface))
    {
        glDeleteTextures(1,&atlas.id);
        if(!m_preview.error().empty()) m_error=m_preview.error();
        m_generationFailed=true;
        if(!result.path.empty()) m_status="Modele exporte : "+result.path+". Apercu GPU indisponible.";
        return;
    }
    glDeleteTextures(1,&m_atlas.id);
    m_atlas=atlas;
    m_surface=std::move(result.surface);
    m_geometry=std::move(result.geometry);
    m_surfaceParameters=result.surfaceParameters;
    m_geometryParameters=result.geometryParameters;
    m_generationMilliseconds=result.milliseconds;
    m_dirty=false;
    m_status=result.path.empty() ? "Modele pret. Aucun fichier ecrit." : "Modele et texture exportes : "+result.path;
    m_error.clear();
}

void MoonGeneratorLabPanel::buildDock(ImGuiID id,ImVec2 size)
{
    ImGui::DockBuilderRemoveNode(id);
    ImGui::DockBuilderAddNode(id,ImGuiDockNodeFlags_DockSpace|ImGuiDockNodeFlags_NoUndocking);
    ImGui::DockBuilderSetNodeSize(id,size);
    m_previewDock=id;
    m_settingsDock=ImGui::DockBuilderSplitNode(id,ImGuiDir_Left,0.32f,nullptr,&m_previewDock);
    ImGui::DockBuilderDockWindow("Reglages##MoonLab",m_settingsDock);
    ImGui::DockBuilderDockWindow("Apercu lunaire##MoonLab",m_previewDock);
    ImGui::DockBuilderFinish(id);
    m_resetDock=false;
}

void MoonGeneratorLabPanel::render(GraphicsDataMisc*)
{
    poll();
    if(!m_owner.showWindow) return;
    const auto* viewport=ImGui::GetMainViewport();
    ImGui::SetNextWindowViewport(viewport->ID);
    ImGui::SetNextWindowPos(viewport->WorkPos,ImGuiCond_Always);
    ImGui::SetNextWindowSize(viewport->WorkSize,ImGuiCond_Always);
    ImGui::SetNextWindowDockID(0,ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding,0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize,0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,ImVec2(0,0));
    const bool visible=ImGui::Begin("Moon Generator Lab",nullptr,ImGuiWindowFlags_MenuBar|
        ImGuiWindowFlags_NoTitleBar|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoResize|
        ImGuiWindowFlags_NoCollapse|ImGuiWindowFlags_NoDocking|ImGuiWindowFlags_NoSavedSettings|
        ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar(3);
    if(visible)
    {
        if(ImGui::BeginMenuBar())
        {
            if(ImGui::MenuItem("Reinitialiser les panneaux")) m_resetDock=true;
            if(ImGui::MenuItem("Masquer le laboratoire")) m_owner.showWindow=false;
            ImGui::EndMenuBar();
        }
        const auto dock=ImGui::GetID("MoonGeneratorLabDock");
        if(m_resetDock || !ImGui::DockBuilderGetNode(dock)) buildDock(dock,ImGui::GetContentRegionAvail());
        ImGui::DockSpace(dock,ImVec2(0,0),ImGuiDockNodeFlags_NoUndocking);
    }
    ImGui::End();
    if(!visible) return;
    drawSettings();
    drawPreview();
    if(!busy() && (m_generateRequested || (m_dirty && !m_generationFailed && m_owner.autoGenerate &&
        ImGui::GetTime()-m_lastEdit>0.35))) startGenerate(false);
}

void MoonGeneratorLabPanel::drawSettings()
{
    ImGui::SetNextWindowViewport(ImGui::GetMainViewport()->ID);
    ImGui::SetNextWindowDockID(m_settingsDock,ImGuiCond_Always);
    if(!ImGui::Begin("Reglages##MoonLab")) { ImGui::End(); return; }
    ImGui::TextWrapped("Sculptez une lune ouverte : volumes arraches, parois rocheuses et fragments detaches. La lumiere eclaire une vraie geometrie 3D.");
    auto& p=m_owner.parameters;
    auto& g=m_owner.geometry;
    bool changed=false;
    ImGui::BeginDisabled(m_jobKind==JobKind::Export);
    changed|=ImGui::InputScalar("Seed",ImGuiDataType_U32,&g.seed);
    if(ImGui::Button("Variante suivante")) { ++g.seed; changed=true; }
    ImGui::SameLine();
    if(ImGui::Button("Regenerer"))
    {
        if(busy() && m_cancel) m_cancel->store(true);
        m_generateRequested=true; m_generationFailed=false;
    }
    ImGui::Checkbox("Apercu automatique",&m_owner.autoGenerate);
    changed|=meshResolution("Precision de l'apercu",m_owner.previewResolution,false);
    changed|=resolution("Texture d'apercu",m_owner.previewWidth,false);
    const auto flags=ImGuiSliderFlags_AlwaysClamp;
    if(ImGui::CollapsingHeader("Volumes arraches",ImGuiTreeNodeFlags_DefaultOpen))
    {
        changed|=ImGui::SliderFloat("Intensite de destruction",&g.excavationStrength,0,1.5f,"%.2f",flags);
        changed|=ImGui::SliderInt("Nombre de cavites",&g.cavityCount,0,12);
        changed|=ImGui::SliderFloat("Taille des cavites",&g.cavitySize,0.2f,1.1f,"%.2f R",flags);
        changed|=ImGui::SliderFloat("Profondeur des cavites",&g.cavityDepth,0.1f,1.2f,"%.2f R",flags);
        changed|=ImGui::SliderFloat("Direction de destruction",&g.excavationYaw,-180,180,"%.0f deg",flags);
        changed|=ImGui::SliderFloat("Elevation de destruction",&g.excavationPitch,-80,80,"%.0f deg",flags);
        ImGui::TextDisabled("R = rayon de la lune intacte. Intensite 0 : corps intact.");
    }
    if(ImGui::CollapsingHeader("Parois et erosion",ImGuiTreeNodeFlags_DefaultOpen))
    {
        changed|=ImGui::SliderFloat("Amplitude du relief",&g.erosionStrength,0,0.15f,"%.3f R",flags);
        changed|=ImGui::SliderFloat("Frequence du relief",&g.erosionScale,1,14,"%.2f",flags);
        changed|=ImGui::SliderInt("Octaves du relief",&g.erosionOctaves,1,6);
    }
    if(ImGui::CollapsingHeader("Fragments",ImGuiTreeNodeFlags_DefaultOpen))
    {
        changed|=ImGui::SliderInt("Nombre de fragments",&g.debrisCount,0,96);
        changed|=ImGui::SliderFloat("Dispersion",&g.debrisSpread,0.05f,1.8f,"%.2f R",flags);
        changed|=ImGui::SliderFloat("Taille des fragments",&g.debrisSize,0.02f,0.22f,"%.3f R",flags);
    }
    if(ImGui::CollapsingHeader("Roche et taches lunaires"))
    {
        changed|=ImGui::SliderFloat("Frequence des mers sombres",&p.macroScale,0.25f,12,"%.2f",flags);
        changed|=ImGui::SliderFloat("Frequence des details",&p.detailScale,2,96,"%.1f",flags);
        changed|=ImGui::SliderInt("Octaves de texture",&p.octaves,1,10);
        ImGui::TextWrapped("Les petits crateres et les variations de roche sont calcules dans la texture. Changer ces reglages reutilise le maillage existant.");
    }
    if(changed)
    {
        g=EnduranceMoon::sanitizeGeometry(g);
        p.seed=g.seed;
        p=EnduranceMoon::sanitize(p);
        edited();
    }
    ImGui::EndDisabled();
    ImGui::Separator();
    ImGui::TextUnformatted("Export du modele");
    ImGui::BeginDisabled(busy());
    inputPath("Fichier .glb / .gltf",m_owner.exportPath);
    meshResolution("Precision finale",g.resolution,true);
    resolution("Texture finale",p.width,true);
    g=EnduranceMoon::sanitizeGeometry(g);
    p=EnduranceMoon::sanitize(p);
    ImGui::Text("Texture integree : %d x %d",p.width,p.width/2);
    if(ImGui::Button("Generer et exporter le modele")) startGenerate(true);
    ImGui::EndDisabled();
    ImGui::TextWrapped("Le modele exporte contient le corps, les fragments et la texture. Le laboratoire ne remplace pas automatiquement la lune de la scene Terrain.");
    if(busy())
    {
        const int stage=m_stage ? m_stage->load() : 0;
        const char* label=stage==0 ? "Sculpture du volume" : stage==1 ? "Texture de roche" :
            stage==2 ? "Preparation de l'apercu" : "Export du modele";
        ImGui::Text("%s - %.1f s",label,ImGui::GetTime()-m_jobStarted);
        ImGui::ProgressBar((std::min)(1.0f,static_cast<float>(stage)/(m_jobKind==JobKind::Export ? 4.0f : 3.0f)),
            ImVec2(-1,0),"Etapes terminees");
        if(ImGui::Button("Annuler")) cancel();
    }
    if(m_dirty) ImGui::TextDisabled("Reglages modifies : l'apercu affiche peut etre precedent.");
    if(!m_status.empty()) ImGui::TextWrapped("%s",m_status.c_str());
    if(!m_error.empty()) ImGui::TextColored(ImVec4(1,0.4f,0.3f,1),"%s",m_error.c_str());
    if(m_geometry && m_surface)
    {
        ImGui::Separator();
        ImGui::Text("%zu sommets - %zu triangles",m_geometry->vertices.size(),m_geometry->indices.size()/3);
        ImGui::Text("%zu morceaux - precision %d",m_geometry->parts.size(),m_geometryParameters.resolution);
        ImGui::Text("Texture affichee : %d x %d",m_surface->width,m_surface->height);
        ImGui::Text("Dernier calcul CPU : %.1f ms",m_generationMilliseconds);
    }
    ImGui::End();
}

void MoonGeneratorLabPanel::drawPreview()
{
    ImGui::SetNextWindowViewport(ImGui::GetMainViewport()->ID);
    ImGui::SetNextWindowDockID(m_previewDock,ImGuiCond_Always);
    if(!ImGui::Begin("Apercu lunaire##MoonLab")) { ImGui::End(); return; }
    if(ImGui::BeginTabBar("MoonPreviewTabs"))
    {
        if(ImGui::BeginTabItem("Modele 3D"))
        {
            ImGui::SliderFloat("Soleil : direction",&m_view.sunYaw,-180,180,"%.0f deg");
            ImGui::SliderFloat("Soleil : elevation",&m_view.sunElevation,-85,85,"%.0f deg");
            ImGui::SliderFloat("Exposition",&m_view.exposure,0.2f,4.0f,"%.2f");
            if(ImGui::TreeNode("Ambiance du laboratoire"))
            {
                ImGui::SliderFloat("Lumiere ambiante",&m_view.ambient,0,0.25f,"%.3f");
                ImGui::ColorEdit3("Couleur du fond",m_view.background.data());
                ImGui::TextDisabled("Ces reglages d'eclairage ne sont pas graves dans le modele.");
                ImGui::TreePop();
            }
            ImGui::Checkbox("Ombres des cavites",&m_view.shadows);
            ImGui::SameLine(); ImGui::Checkbox("Fil de fer",&m_view.wireframe);
            ImGui::SameLine(); ImGui::Checkbox("Normales",&m_view.normals);
            if(ImGui::Button("Recentrer la vue"))
            { m_view.yaw=0; m_view.pitch=6; m_view.distance=4.6f; }
            ImGui::SameLine(); ImGui::TextDisabled("Glisser : tourner. Molette : zoomer.");
            const auto available=ImGui::GetContentRegionAvail();
            const float width=(std::max)(1.0f,available.x),height=(std::max)(1.0f,available.y);
            const auto scale=ImGui::GetIO().DisplayFramebufferScale;
            const float physicalWidth=(std::max)(1.0f,width*scale.x);
            const float physicalHeight=(std::max)(1.0f,height*scale.y);
            const float renderScale=(std::min)(1.0f,1600.0f/(std::max)(physicalWidth,physicalHeight));
            m_view.width=std::clamp(static_cast<int>(physicalWidth*renderScale),64,1600);
            m_view.height=std::clamp(static_cast<int>(physicalHeight*renderScale),64,1600);
            if(m_geometry)
            {
                if(!m_preview.render(m_view)) m_error=m_preview.error();
                if(m_preview.texture())
                {
                    ImGui::Image(textureID(m_preview.texture()),ImVec2(width,height),ImVec2(0,1),ImVec2(1,0));
                    if(ImGui::IsItemHovered())
                    {
                        const auto& io=ImGui::GetIO();
                        if(ImGui::IsMouseDragging(ImGuiMouseButton_Left))
                        {
                            m_view.yaw=std::fmod(m_view.yaw-io.MouseDelta.x*0.35f,360.0f);
                            m_view.pitch=std::clamp(m_view.pitch+io.MouseDelta.y*0.35f,-85.0f,85.0f);
                        }
                        if(io.MouseWheel!=0) m_view.distance=std::clamp(m_view.distance*std::pow(0.88f,io.MouseWheel),1.8f,15.0f);
                    }
                }
            }
            else ImGui::TextWrapped("Generez un modele pour examiner les excavations et les fragments sous tous les angles.");
            ImGui::EndTabItem();
        }
        if(ImGui::BeginTabItem("Texture de roche"))
        {
            ImGui::TextWrapped("Couleur sans eclairage. Les normales et les ombres de l'apercu 3D proviennent du maillage.");
            const auto available=ImGui::GetContentRegionAvail();
            const float width=(std::max)(1.0f,(std::min)(available.x,available.y*2));
            if(m_atlas.id) ImGui::Image(textureID(m_atlas.id),ImVec2(width,width*0.5f));
            else ImGui::TextUnformatted("Texture non generee.");
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::End();
}
