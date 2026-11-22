#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "MoonGeneratorLabPanel.hpp"
#include "MoonGeneratorLab.hpp"
#include "glcore.hpp"
#include "imgui-cmake/Header/imgui_internal.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <stdexcept>
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

// DSA leaves texture bindings untouched, but CPU uploads still honor unpack
// state and the unpack buffer. The HUD may be called with arbitrary GL state.
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
    const GLuint textures[]{m_sphere.id,m_atlas.id,m_normals.id};
    glDeleteTextures(3,textures);
}

bool MoonGeneratorLabPanel::busy() const { return m_jobKind!=JobKind::None; }
void MoonGeneratorLabPanel::init(GraphicsDataMisc*)
{
    m_generateRequested=m_owner.autoGenerate;
    m_status="Apercu uniquement : aucun fichier n'est ecrit avant l'export.";
}
void MoonGeneratorLabPanel::edited()
{
    ++m_revision;
    m_dirty=true;
    m_generationFailed=false;
    m_lastEdit=ImGui::GetTime();
    if((m_jobKind==JobKind::Generate || m_jobKind==JobKind::Preview) && m_cancel)
        m_cancel->store(true);
}
void MoonGeneratorLabPanel::cancel()
{
    if(m_cancel) m_cancel->store(true);
    m_generateRequested=false;
    m_generationFailed=true; // A manual cancellation must not auto-retry.
    m_previewDirty=false;
    if(m_jobKind==JobKind::Generate || m_jobKind==JobKind::Export) m_dirty=true;
}

void MoonGeneratorLabPanel::startGenerate(bool forExport)
{
    if(busy()) return; // One future and one CPU operation per laboratory.
    if(forExport && m_owner.exportPath.empty())
    { m_error="Indiquez un fichier .surface a exporter."; return; }
    try
    {
        auto params=EnduranceMoon::sanitize(m_owner.parameters);
        m_owner.parameters=params;
        if(!forExport) params.width=(std::max)(128,(std::min)(m_owner.previewWidth,1024));
        params=EnduranceMoon::sanitize(params);
        m_cancel=std::make_shared<std::atomic_bool>(false);
        m_stage=std::make_shared<std::atomic_int>(0);
        const auto cancelToken=m_cancel;
        const auto stage=m_stage;
        const auto path=forExport ? m_owner.exportPath : std::string{};
        const float yaw=m_yaw,sun=m_sun;
        m_submittedRevision=m_revision;
        m_submittedViewRevision=m_viewRevision;
        m_job=std::async(std::launch::async,[params,path,cancelToken,stage,yaw,sun]() {
            Result result;
            const auto started=std::chrono::steady_clock::now();
            try
            {
                auto surface=std::make_shared<EnduranceMoon::Surface>(
                    EnduranceMoon::generate(params,cancelToken.get()));
                if(cancelToken->load()) { result.cancelled=true; return result; }
                if(surface->pixels.empty()) throw std::runtime_error("La generation lunaire a renvoye une surface vide.");
                stage->store(1);
                result.sphere=EnduranceMoon::renderSphere(*surface,yaw,sun,512,cancelToken.get());
                if(cancelToken->load()) { result.cancelled=true; return result; }
                result.atlas=EnduranceMoon::renderAtlas(*surface,false);
                result.normals=EnduranceMoon::renderAtlas(*surface,true);
                if(result.sphere.pixels.empty() || result.atlas.pixels.empty() || result.normals.pixels.empty())
                    throw std::runtime_error("Impossible de preparer les images d'apercu.");
                if(cancelToken->load()) { result.cancelled=true; return result; }
                if(!path.empty())
                {
                    stage->store(2);
                    if(!EnduranceMoon::save(*surface,path,result.error)) return result;
                    result.path=path;
                }
                result.surface=std::move(surface);
                result.milliseconds=std::chrono::duration<double,std::milli>(
                    std::chrono::steady_clock::now()-started).count();
                stage->store(3);
            }
            catch(const std::exception& e) { result.error=e.what(); }
            catch(...) { result.error="Erreur inconnue pendant la generation lunaire."; }
            return result;
        });
        m_jobKind=forExport ? JobKind::Export : JobKind::Generate;
        m_jobStarted=ImGui::GetTime();
        m_generateRequested=false;
        m_generationFailed=false;
        m_error.clear();
        m_status=forExport ? "Generation a la resolution d'export..." : "Generation de l'apercu sur CPU...";
    }
    catch(const std::exception& e)
    {
        m_error=e.what();
        m_generateRequested=false;
        m_generationFailed=true;
        m_dirty=true;
    }
}

void MoonGeneratorLabPanel::startPreview()
{
    if(busy() || !m_surface) return;
    try
    {
        m_cancel=std::make_shared<std::atomic_bool>(false);
        m_stage=std::make_shared<std::atomic_int>(1);
        const auto cancelToken=m_cancel;
        const auto surface=m_surface;
        const float yaw=m_yaw,sun=m_sun;
        m_submittedRevision=m_revision;
        m_submittedViewRevision=m_viewRevision;
        m_job=std::async(std::launch::async,[surface,cancelToken,yaw,sun]() {
            Result result;
            try
            {
                result.sphere=EnduranceMoon::renderSphere(*surface,yaw,sun,512,cancelToken.get());
                result.cancelled=cancelToken->load();
                if(!result.cancelled && result.sphere.pixels.empty())
                    result.error="Impossible de calculer l'apercu de la sphere.";
            }
            catch(const std::exception& e) { result.error=e.what(); }
            catch(...) { result.error="Erreur inconnue pendant l'apercu."; }
            return result;
        });
        m_jobKind=JobKind::Preview;
        m_jobStarted=ImGui::GetTime();
        m_previewDirty=false;
    }
    catch(const std::exception& e) { m_error=e.what(); m_previewDirty=false; }
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
    const auto kind=m_jobKind;
    const bool cancelled=m_cancel && m_cancel->load();
    m_jobKind=JobKind::None;
    Result result;
    try { result=m_job.get(); }
    catch(const std::exception& e) { result.error=e.what(); }
    catch(...) { result.error="Erreur inconnue pendant le travail CPU."; }
    m_cancel.reset();
    m_stage.reset();
    // A successfully committed export remains successful if Stop was pressed
    // during the final atomic save. Cancellation never rolls that file back.
    if((cancelled || result.cancelled) && result.path.empty())
    {
        m_status="Operation annulee. L'apercu precedent est conserve.";
        return;
    }
    if(!result.error.empty())
    {
        m_error=result.error;
        m_status="Echec : l'apercu precedent est conserve.";
        if(kind!=JobKind::Preview) { m_dirty=true; m_generationFailed=true; }
        return;
    }
    if(m_submittedRevision!=m_revision) return;
    if(kind==JobKind::Preview)
    {
        if(m_submittedViewRevision==m_viewRevision) upload(m_sphere,result.sphere);
        else m_previewDirty=true;
        return;
    }
    if(!result.surface)
    {
        m_error="La generation lunaire n'a produit aucune surface.";
        m_dirty=true; m_generationFailed=true;
        return;
    }
    Texture sphere,atlas,normals;
    if(!upload(sphere,result.sphere) || !upload(atlas,result.atlas) || !upload(normals,result.normals))
    {
        const GLuint textures[]{sphere.id,atlas.id,normals.id};
        glDeleteTextures(3,textures);
        m_generationFailed=true;
        if(!result.path.empty()) m_status="Fichier exporte : "+result.path+". Apercu GPU indisponible.";
        return;
    }
    const GLuint previous[]{m_sphere.id,m_atlas.id,m_normals.id};
    glDeleteTextures(3,previous);
    m_sphere=sphere; m_atlas=atlas; m_normals=normals;
    m_surface=std::move(result.surface);
    m_generationMilliseconds=result.milliseconds;
    m_dirty=false;
    m_previewDirty=m_submittedViewRevision!=m_viewRevision;
    m_status=result.path.empty() ? "Apercu pret. Aucun fichier ecrit." : "Fichier exporte : "+result.path;
    m_error.clear();
}

void MoonGeneratorLabPanel::buildDock(ImGuiID id,ImVec2 size)
{
    ImGui::DockBuilderRemoveNode(id);
    ImGui::DockBuilderAddNode(id,ImGuiDockNodeFlags_DockSpace|ImGuiDockNodeFlags_NoUndocking);
    ImGui::DockBuilderSetNodeSize(id,size);
    m_previewDock=id;
    m_settingsDock=ImGui::DockBuilderSplitNode(id,ImGuiDir_Left,0.33f,nullptr,&m_previewDock);
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
    else if(!busy() && m_previewDirty && ImGui::GetTime()-m_lastViewEdit>0.08) startPreview();
}

void MoonGeneratorLabPanel::drawSettings()
{
    ImGui::SetNextWindowViewport(ImGui::GetMainViewport()->ID);
    ImGui::SetNextWindowDockID(m_settingsDock,ImGuiCond_Always);
    if(!ImGui::Begin("Reglages##MoonLab")) { ImGui::End(); return; }
    ImGui::TextWrapped("Lune procedurale calculee en C++. Les fractures sont gravees dans l'albedo et les normales de l'atlas.");
    ImGui::TextWrapped("En jeu : meme sphere, meme texture et meme cout de rendu. Les fractures ne detachent pas de blocs et ne modifient pas la silhouette.");
    auto& p=m_owner.parameters;
    bool changed=false;
    ImGui::BeginDisabled(m_jobKind==JobKind::Export);
    changed|=ImGui::InputScalar("Seed",ImGuiDataType_U32,&p.seed);
    if(ImGui::Button("Variante suivante")) { ++p.seed; changed=true; }
    ImGui::SameLine();
    if(ImGui::Button("Regenerer l'apercu"))
    {
        if(busy() && m_cancel) m_cancel->store(true);
        m_generateRequested=true; m_generationFailed=false;
    }
    ImGui::Checkbox("Apercu automatique",&m_owner.autoGenerate);
    changed|=resolution("Largeur de l'apercu",m_owner.previewWidth,false);
    const auto flags=ImGuiSliderFlags_AlwaysClamp;
    if(ImGui::CollapsingHeader("Surface",ImGuiTreeNodeFlags_DefaultOpen))
    {
        changed|=ImGui::SliderFloat("Echelle des continents",&p.macroScale,0.25f,12.0f,"%.2f",flags);
        changed|=ImGui::SliderFloat("Echelle des details",&p.detailScale,2.0f,96.0f,"%.1f",flags);
        changed|=ImGui::SliderInt("Octaves",&p.octaves,1,10);
    }
    if(ImGui::CollapsingHeader("Fractures",ImGuiTreeNodeFlags_DefaultOpen))
    {
        changed|=ImGui::SliderInt("Nombre de fractures",&p.fractureCount,0,32);
        changed|=ImGui::SliderFloat("Largeur angulaire",&p.fractureWidth,0.001f,0.06f,"%.4f",flags);
        changed|=ImGui::SliderFloat("Profondeur",&p.fractureDepth,0.0f,0.05f,"%.4f",flags);
        changed|=ImGui::SliderFloat("Intensite",&p.fractureStrength,0.0f,3.0f,"%.2f",flags);
        ImGui::TextDisabled("0 fracture ou intensite 0 : lune intacte.");
    }
    if(changed) { p=EnduranceMoon::sanitize(p); edited(); }
    ImGui::EndDisabled();
    ImGui::Separator();
    ImGui::TextUnformatted("Export du fichier lunaire");
    ImGui::BeginDisabled(busy());
    inputPath("Fichier .surface",m_owner.exportPath);
    resolution("Largeur d'export",p.width,true);
    p=EnduranceMoon::sanitize(p);
    ImGui::Text("Atlas exporte : %d x %d RGBA8",p.width,p.width/2);
    if(ImGui::Button("Generer et exporter le fichier .surface")) startGenerate(true);
    ImGui::EndDisabled();
    ImGui::TextWrapped("L'export recalcule la surface a cette resolution puis remplace le fichier indique. Il est charge au prochain demarrage de la scene d'atmosphere.");
    if(busy())
    {
        const int stage=m_stage ? m_stage->load() : 0;
        const char* label=stage==0 ? "Calcul de la surface" : stage==1 ? "Calcul des apercus" : "Ecriture du fichier";
        ImGui::Text("%s - %.1f s",label,ImGui::GetTime()-m_jobStarted);
        // Coarse completed stages, not an estimate of elapsed compute work.
        ImGui::ProgressBar((std::min)(1.0f,static_cast<float>(stage)/(m_jobKind==JobKind::Export ? 3.0f : 2.0f)),
            ImVec2(-1,0),m_jobKind==JobKind::Preview ? "Apercu CPU" : "Etapes terminees");
        if(ImGui::Button("Annuler")) cancel();
    }
    if(m_dirty) ImGui::TextDisabled("Reglages modifies : l'apercu affiche peut etre precedent.");
    if(!m_status.empty()) ImGui::TextWrapped("%s",m_status.c_str());
    if(!m_error.empty()) ImGui::TextColored(ImVec4(1,0.4f,0.3f,1),"%s",m_error.c_str());
    if(m_surface)
    {
        ImGui::Separator();
        ImGui::Text("Surface affichee : %d x %d",m_surface->width,m_surface->height);
        ImGui::Text("Albedo moyen lineaire : %.4f",m_surface->meanAlbedo);
        ImGui::Text("Calcul CPU + apercus : %.1f ms",m_generationMilliseconds);
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
        if(ImGui::BeginTabItem("Sphere eclairee"))
        {
            bool changed=ImGui::SliderFloat("Rotation",&m_yaw,-180,180,"%.0f deg");
            changed|=ImGui::SliderFloat("Angle du soleil",&m_sun,-180,180,"%.0f deg");
            if(changed)
            {
                ++m_viewRevision; m_previewDirty=true; m_lastViewEdit=ImGui::GetTime();
                if(m_jobKind==JobKind::Preview && m_cancel) m_cancel->store(true);
            }
            ImGui::TextDisabled("Apercu CPU des normales et de l'albedo ; exposition de controle.");
            const auto available=ImGui::GetContentRegionAvail();
            const float size=(std::max)(1.0f,(std::min)(available.x,available.y));
            if(m_sphere.id) ImGui::Image(textureID(m_sphere.id),ImVec2(size,size));
            else ImGui::TextUnformatted("Generez un apercu pour examiner la lune.");
            ImGui::EndTabItem();
        }
        if(ImGui::BeginTabItem("Atlas albedo"))
        {
            ImGui::TextWrapped("Albedo lineaire du canal A, affiche en sRGB. Projection equirectangulaire, nord en haut.");
            const auto available=ImGui::GetContentRegionAvail();
            const float width=(std::max)(1.0f,(std::min)(available.x,available.y*2));
            if(m_atlas.id) ImGui::Image(textureID(m_atlas.id),ImVec2(width,width*0.5f));
            else ImGui::TextUnformatted("Atlas non genere.");
            ImGui::EndTabItem();
        }
        if(ImGui::BeginTabItem("Atlas normales"))
        {
            ImGui::TextWrapped("Normales dans le repere local de la lune, encodees dans RGB. Le meme atlas RGBA est utilise en jeu.");
            const auto available=ImGui::GetContentRegionAvail();
            const float width=(std::max)(1.0f,(std::min)(available.x,available.y*2));
            if(m_normals.id) ImGui::Image(textureID(m_normals.id),ImVec2(width,width*0.5f));
            else ImGui::TextUnformatted("Atlas non genere.");
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::End();
}
