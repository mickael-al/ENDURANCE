#include "TreeGeneratorLabPanel.hpp"
#include "TreeGeneratorLab.hpp"
#include "ProceduralTreeBake.hpp"
#include "imgui-cmake/Header/imgui_internal.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <stdexcept>

namespace
{
using namespace EnduranceTrees;
bool inputPath(const char* label,std::string& text)
{
    std::array<char,2048> buffer{};
    std::memcpy(buffer.data(),text.data(),(std::min)(text.size(),buffer.size()-1));
    if(!ImGui::InputText(label,buffer.data(),buffer.size())) return false;
    text=buffer.data();
    return true;
}
bool color(const char* label,glm::vec3& value)
{
    float display[3];
    for(int i=0;i<3;++i)
        display[i]=value[i]<=0.0031308f ? value[i]*12.92f : 1.055f*std::pow(value[i],1.0f/2.4f)-0.055f;
    if(!ImGui::ColorEdit3(label,display)) return false;
    for(int i=0;i<3;++i)
    {
        const float v=std::clamp(display[i],0.0f,1.0f);
        value[i]=v<=0.04045f ? v/12.92f : std::pow((v+0.055f)/1.055f,2.4f);
    }
    return true;
}
Image readMask(const std::string& path)
{
    if(path.empty()) return {};
    Image image;
    std::string error;
    // A private PNG decoder leaves the engine's global image-loader state alone.
    if(!loadLeafMask(std::filesystem::u8path(path),image,error))
        throw std::runtime_error(error);
    return image;
}
}

TreeGeneratorLabPanel::TreeGeneratorLabPanel(TreeGeneratorLabComponent& owner):m_owner(owner) {}
TreeGeneratorLabPanel::~TreeGeneratorLabPanel()
{
    if(m_cancel) m_cancel->store(true);
    if(m_job.valid()) m_job.wait();
}
bool TreeGeneratorLabPanel::busy() const { return m_jobKind!=JobKind::None; }
void TreeGeneratorLabPanel::init(GraphicsDataMisc*)
{
    m_gpuReady=m_preview.initialize();
    if(!m_gpuReady) m_error=m_preview.error();
    m_generateRequested=true;
}
void TreeGeneratorLabPanel::edited()
{
    m_dirty=true;
    m_generationFailed=false;
    m_lastEdit=ImGui::GetTime();
    ++m_revision;
    if(m_jobKind==JobKind::Generate && !m_batchCount && m_cancel) m_cancel->store(true);
}
void TreeGeneratorLabPanel::startGenerate(const EnduranceTrees::Params& params,const std::string& maskPath)
{
    if(busy()) return;
    try
    {
        auto mask=m_batchCount ? m_batchLeafMask :
            (maskPath.empty() && EnduranceTrees::validImage(m_embeddedMask) ? m_embeddedMask : readMask(maskPath));
        m_cancel=std::make_shared<std::atomic_bool>(false);
        const auto cancel=m_cancel;
        m_submittedRevision=m_revision;
        m_job=std::async(std::launch::async,[params,mask=std::move(mask),cancel]() {
            Result result;
            try
            {
                result.tree=std::make_shared<EnduranceTrees::Tree>(
                    EnduranceTrees::generate(params,mask.rgba.empty()?nullptr:&mask,cancel.get()));
                result.cancelled=result.tree->cancelled || cancel->load();
            }
            catch(const std::exception& e) { result.error=e.what(); }
            return result;
        });
        m_jobKind=JobKind::Generate;
        m_dirty=false; m_generateRequested=false; m_generationFailed=false;
        m_status="Generation CPU en cours..."; m_error.clear();
    }
    catch(const std::exception& e)
    {
        m_error=e.what(); m_dirty=true; m_generationFailed=true;
        m_generateRequested=false; m_batchCount=0;
    }
}
void TreeGeneratorLabPanel::startBatch(unsigned count)
{
    if(busy() || !m_tree || m_dirty) return;
    m_batchParams=m_tree->params;
    m_batchLeafMask=m_tree->leafMask;
    m_batchMask=m_owner.maskPath;
    m_batchCount=std::clamp(count,1u,32u); m_batchIndex=0;
    m_batchImpostors=m_bakeImpostors;
    const auto stamp=std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    m_batchDirectory=(std::filesystem::u8path(m_owner.exportDirectory)/
        ("batch_"+std::to_string(stamp)+"_"+std::to_string(m_batchParams.seed))).u8string();
    startExport(); // The first variant is exactly the visible, already generated tree.
}
void TreeGeneratorLabPanel::startExport()
{
    if(!m_tree || busy()) return;
    try
    {
        std::vector<EnduranceTrees::ImpostorView> views, lod3;
        if(!m_preview.captureLod3(lod3,m_impostorResolution))
            throw std::runtime_error(m_preview.error());
        if(m_batchImpostors && !m_preview.captureImpostors(views,m_impostorResolution,8,true))
            throw std::runtime_error(m_preview.error());
        m_previewDirty=true;
        const auto tree=m_tree;
        const auto path=std::filesystem::u8path(m_batchDirectory)/
            ("tree_"+std::to_string(m_batchIndex+1)+"_seed_"+std::to_string(tree->params.seed));
        m_cancel=std::make_shared<std::atomic_bool>(false);
        const auto cancel=m_cancel;
        m_job=std::async(std::launch::async,[tree,path,views=std::move(views),lod3=std::move(lod3),cancel]() {
            Result result;
            try
            {
                if(cancel->load()) { result.cancelled=true; return result; }
                if(EnduranceTrees::bake(*tree,path,result.error,views.empty()?nullptr:&views,&lod3))
                    result.path=path.u8string();
            }
            catch(const std::exception& e) { result.error=e.what(); }
            return result;
        });
        m_jobKind=JobKind::Export;
        m_status="Ecriture du bake "+std::to_string(m_batchIndex+1)+" / "+std::to_string(m_batchCount);
        m_error.clear();
    }
    catch(const std::exception& e) { m_error=e.what(); m_batchCount=0; }
}
void TreeGeneratorLabPanel::startLoad()
{
    if(busy()) return;
    try
    {
        const auto path=std::filesystem::u8path(m_loadPath);
        m_cancel=std::make_shared<std::atomic_bool>(false);
        m_job=std::async(std::launch::async,[path]() {
            Result result;
            try
            {
                auto tree=std::make_shared<EnduranceTrees::Tree>();
                if(EnduranceTrees::loadBakedTree(path,*tree,result.error,&result.lod3))
                { result.tree=std::move(tree); result.path=path.u8string(); }
            }
            catch(const std::exception& e) { result.error=e.what(); }
            return result;
        });
        m_jobKind=JobKind::Load; m_status="Chargement du bake..."; m_error.clear();
    }
    catch(const std::exception& e) { m_error=e.what(); }
}
void TreeGeneratorLabPanel::poll()
{
    if(!m_job.valid() || m_job.wait_for(std::chrono::seconds(0))!=std::future_status::ready) return;
    Result result;
    const auto kind=m_jobKind;
    const bool stop=m_cancel && m_cancel->load();
    m_jobKind=JobKind::None;
    try { result=m_job.get(); }
    catch(const std::exception& e) { result.error=e.what(); }
    if(!result.path.empty()) m_loadPath=result.path;
    if(result.cancelled || stop)
    {
        m_status="Operation arretee. Les exports termines sont conserves.";
        if(m_batchCount && m_tree) m_owner.parameters=m_tree->params;
        m_batchCount=0; return;
    }
    if(!result.error.empty())
    {
        if(kind==JobKind::Generate) { m_dirty=true; m_generationFailed=true; }
        if(m_batchCount && m_tree) m_owner.parameters=m_tree->params;
        m_error=result.error; m_batchCount=0; m_status="Echec : l'apercu precedent est conserve."; return;
    }
    if(result.tree)
    {
        if(kind==JobKind::Generate && !m_batchCount && m_submittedRevision!=m_revision) return;
        if(!m_gpuReady || !m_preview.upload(*result.tree,result.lod3.empty()?nullptr:&result.lod3))
        {
            if(kind==JobKind::Generate) { m_dirty=true; m_generationFailed=true; }
            m_error=m_preview.error(); m_batchCount=0; return;
        }
        m_tree=std::move(result.tree);
        if(m_batchCount) m_owner.parameters=m_tree->params;
        m_previewDirty=true;
        if(m_fitNext || kind==JobKind::Load) { fitCamera(); m_fitNext=false; }
        if(kind==JobKind::Load)
        {
            m_owner.parameters=m_tree->params;
            // Scene serialization must retain the imported mask across Stop/Play.
            m_owner.maskPath=(std::filesystem::u8path(m_loadPath)/"leaves.png").u8string();
            m_embeddedMask=m_tree->leafMask;
            m_dirty=false; m_generateRequested=false; m_generationFailed=false;
            m_error.clear();
            m_status="Bake charge sans regeneration. Le masque source est integre au bake.";
        }
        else m_status="Apercu pret.";
        if(m_batchCount) startExport();
    }
    else if(kind==JobKind::Export)
    {
        ++m_batchIndex;
        if(m_batchIndex<m_batchCount)
        {
            auto p=m_batchParams; p.seed+=m_batchIndex;
            startGenerate(p,m_batchMask);
        }
        else
        {
            m_status=std::to_string(m_batchCount)+" arbre(s) exporte(s) dans "+m_batchDirectory;
            m_batchCount=0;
            // Keep controls in sync with the last variant shown after a batch.
            if(m_tree) m_owner.parameters=m_tree->params;
        }
    }
}
void TreeGeneratorLabPanel::fitCamera()
{
    if(!m_tree) return;
    m_view.target=(m_tree->boundsMin+m_tree->boundsMax)*0.5f;
    m_view.distance=(std::max)(2.0f,glm::length(m_tree->boundsMax-m_tree->boundsMin)*1.35f);
    m_previewDirty=true;
}
void TreeGeneratorLabPanel::buildDock(ImGuiID id,ImVec2 size)
{
    ImGui::DockBuilderRemoveNode(id);
    ImGui::DockBuilderAddNode(id,ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(id,size);
    ImGuiID right=id;
    const ImGuiID left=ImGui::DockBuilderSplitNode(right,ImGuiDir_Left,0.32f,nullptr,&right);
    ImGui::DockBuilderDockWindow("Reglages##TreeLab",left);
    ImGui::DockBuilderDockWindow("Apercu 3D##TreeLab",right);
    ImGui::DockBuilderFinish(id); m_resetDock=false;
}
void TreeGeneratorLabPanel::render(GraphicsDataMisc*)
{
    poll();
    if(!m_owner.showWindow) return;
    ImGui::SetNextWindowSize(ImVec2(1450,900),ImGuiCond_FirstUseEver);
    const bool visible=ImGui::Begin("Tree Generator Lab",nullptr,ImGuiWindowFlags_MenuBar);
    if(visible)
    {
        if(ImGui::BeginMenuBar())
        {
            if(ImGui::MenuItem("Reinitialiser les panneaux")) m_resetDock=true;
            if(ImGui::MenuItem("Recharger les shaders"))
            {
                if(m_preview.reloadShaders()) { m_gpuReady=true; m_error.clear(); }
                else m_error=m_preview.error(); // Preserve a working previous program.
                m_previewDirty=true;
            }
            ImGui::EndMenuBar();
        }
        const auto dock=ImGui::GetID("TreeGeneratorLabDock");
        if(m_resetDock || !ImGui::DockBuilderGetNode(dock)) buildDock(dock,ImGui::GetContentRegionAvail());
        ImGui::DockSpace(dock,ImVec2(0,0));
    }
    ImGui::End();
    if(!visible) return;
    drawSettings();
    if(!busy() && !m_batchCount && (m_generateRequested ||
       (m_dirty && !m_generationFailed && m_owner.autoGenerate && ImGui::GetTime()-m_lastEdit>0.30)))
        startGenerate(m_owner.parameters,m_owner.maskPath);
    drawPreview();
}
void TreeGeneratorLabPanel::drawSettings()
{
    if(!ImGui::Begin("Reglages##TreeLab")) { ImGui::End(); return; }
    auto& p=m_owner.parameters;
    ImGui::TextWrapped("Generation hors jeu : courbes, branches et feuillage sont calcules sur CPU puis sauvegardes.");
    bool changed=false;
    ImGui::BeginDisabled(m_batchCount>0 || m_jobKind==JobKind::Load);
    int family=static_cast<int>(p.family);
    if(ImGui::Combo("Type d'arbre",&family,
        "Feuillu etale\0Conifere\0Retombant\0Arbre mort\0Chene\0Bouleau\0Acacia\0Baobab\0"
        "Mangrove\0Cypres\0Palmier\0Tremble\0Genevrier\0Banyan\0"))
    {
        const auto seed=p.seed;
        p=EnduranceTrees::preset(static_cast<EnduranceTrees::Family>(family)); p.seed=seed;
        changed=true; m_fitNext=true;
    }
    changed|=ImGui::InputScalar("Seed",ImGuiDataType_U32,&p.seed);
    if(ImGui::Button("Variante suivante")) { ++p.seed; changed=true; }
    ImGui::SameLine();
    if(ImGui::Button("Regenerer")) m_generateRequested=true;
    ImGui::Checkbox("Apercu automatique",&m_owner.autoGenerate);
    const auto flags=ImGuiSliderFlags_AlwaysClamp;
    if(ImGui::CollapsingHeader("Tronc et silhouette",ImGuiTreeNodeFlags_DefaultOpen))
    {
        changed|=ImGui::SliderFloat("Hauteur (m)",&p.height,2,100,"%.1f",flags);
        changed|=ImGui::SliderFloat("Rayon du tronc (m)",&p.trunkRadius,0.04f,p.height*0.14f,"%.2f",flags);
        changed|=ImGui::SliderFloat("Courbure du tronc",&p.trunkBend,0,0.45f,"%.2f",flags);
        changed|=ImGui::SliderFloat("Largeur de couronne (m)",&p.crownWidth,1,p.height*2.0f,"%.1f",flags);
        changed|=ImGui::SliderFloat("Hauteur de couronne (m)",&p.crownHeight,1,p.height*1.5f,"%.1f",flags);
        changed|=ImGui::SliderFloat("Debut des branches",&p.branchStart,0.12f,0.80f,"%.2f",flags);
    }
    if(ImGui::CollapsingHeader("Ramifications",ImGuiTreeNodeFlags_DefaultOpen))
    {
        changed|=ImGui::SliderInt("Branches principales",&p.branchCount,3,24);
        changed|=ImGui::SliderInt("Niveaux de branches",&p.branchingLevels,1,4);
        changed|=ImGui::SliderFloat("Angle des branches",&p.branchAngle,15,110,"%.0f deg",flags);
        changed|=ImGui::SliderFloat("Rayon relatif des branches",&p.branchTaper,0.25f,0.76f,"%.2f",flags);
        changed|=ImGui::SliderFloat("Courbure des branches",&p.curvature,0,1,"%.2f",flags);
        changed|=ImGui::SliderFloat("Densite des rameaux",&p.twigDensity,0.2f,2.5f,"%.2f",flags);
    }
    if(ImGui::CollapsingHeader("Feuillage",ImGuiTreeNodeFlags_DefaultOpen))
    {
        changed|=ImGui::SliderFloat("Grand cote des cartes (m)",&p.leafSize,0.12f,5,"%.2f",flags);
        changed|=ImGui::SliderFloat("Densite des cartes",&p.leafDensity,0,3,"%.2f",flags);
        changed|=ImGui::SliderFloat("Taille des amas (m)",&p.clumpSize,0.2f,8,"%.2f",flags);
        changed|=ImGui::SliderFloat("Normales de volume",&p.foliageNormalBlend,0,1,"%.2f",flags);
        if(inputPath("Masque de feuilles",m_owner.maskPath)) { changed=true; m_embeddedMask={}; }
        ImGui::TextWrapped("Vide : grappe procedurale incluse. PNG avec alpha ou noir/blanc. Ratio conserve, maximum 2048 x 2048.");
        if(ImGui::Button("Relire le masque")) { changed=true; m_generateRequested=true; }
        if(EnduranceTrees::validImage(m_embeddedMask))
        {
            ImGui::TextDisabled("Masque embarque du bake actuellement utilise.");
            if(ImGui::Button("Revenir au masque procedural"))
            { m_embeddedMask={}; m_owner.maskPath.clear(); changed=true; }
        }
    }
    if(ImGui::CollapsingHeader("Racines"))
    {
        changed|=ImGui::SliderInt("Nombre de racines",&p.rootCount,0,20);
        ImGui::BeginDisabled(p.rootCount==0);
        changed|=ImGui::SliderFloat("Longueur des racines (m)",&p.rootLength,0.25f,30,"%.2f",flags);
        changed|=ImGui::SliderFloat("Rayon des racines (m)",&p.rootRadius,0.01f,
            (std::min)(3.0f,p.trunkRadius*1.2f),"%.2f",flags);
        ImGui::EndDisabled();
        ImGui::TextDisabled("0 : desactive. Les quatre types initiaux restent sans racines.");
    }
    if(ImGui::CollapsingHeader("Lianes retombantes"))
    {
        changed|=ImGui::SliderInt("Nombre de lianes",&p.vineCount,0,48);
        ImGui::BeginDisabled(p.vineCount==0);
        changed|=ImGui::SliderFloat("Longueur des lianes (m)",&p.vineLength,0.25f,60,"%.2f",flags);
        changed|=ImGui::SliderFloat("Rayon des lianes (m)",&p.vineRadius,0.005f,0.3f,"%.3f",flags);
        ImGui::EndDisabled();
    }
    if(ImGui::CollapsingHeader("Palettes"))
    {
        changed|=color("Bois sombre",p.barkLow); changed|=color("Bois clair",p.barkHigh);
        changed|=color("Feuillage bas",p.leafLow); changed|=color("Feuillage haut",p.leafHigh);
        ImGui::TextDisabled("Edition sRGB ; calculs et couleurs de sommets lineaires.");
    }
    if(changed) { p=EnduranceTrees::sanitize(p); edited(); }
    if(ImGui::CollapsingHeader("Presets"))
    {
        inputPath("Fichier preset",m_presetPath);
        if(ImGui::Button("Sauvegarder preset"))
        {
            if(EnduranceTrees::savePreset(std::filesystem::u8path(m_presetPath),p,m_error)) m_status="Preset sauvegarde.";
        }
        ImGui::SameLine();
        if(ImGui::Button("Charger preset"))
        {
            if(EnduranceTrees::loadPreset(std::filesystem::u8path(m_presetPath),p,m_error))
            { edited(); m_fitNext=true; m_generateRequested=true; }
        }
    }
    ImGui::EndDisabled();
    ImGui::Separator();
    ImGui::TextUnformatted("Bake / sauvegarde des modeles");
    ImGui::BeginDisabled(busy() || m_batchCount>0);
    inputPath("Dossier de sortie",m_owner.exportDirectory);
    ImGui::SliderInt("Variantes",&m_owner.bakeCount,1,32);
    ImGui::TextWrapped("Chaque export inclut le LOD3 : deux PNG transparents, vues a 0 et 45 degres.");
    {
        int selection=m_impostorResolution==256?1:0;
        if(ImGui::Combo("Resolution par vue",&selection,"128\0 256\0")) m_impostorResolution=selection?256:128;
    }
    ImGui::Checkbox("Ajouter les 17 vues d'imposteurs",&m_bakeImpostors);
    ImGui::BeginDisabled(!m_tree || m_dirty);
    if(ImGui::Button("Exporter cet arbre")) startBatch(1);
    ImGui::SameLine();
    if(ImGui::Button("Generer et exporter le lot")) startBatch(static_cast<unsigned>(m_owner.bakeCount));
    ImGui::EndDisabled();
    inputPath("Bake a recharger",m_loadPath);
    if(ImGui::Button("Recharger le modele sauvegarde") && !m_loadPath.empty()) startLoad();
    ImGui::EndDisabled();
    if(busy() && ImGui::Button("Arreter apres l'operation en cours"))
    {
        if(m_cancel) m_cancel->store(true);
        m_generateRequested=false;
        // A manually cancelled edit remains pending, without an automatic retry.
        if(m_jobKind==JobKind::Generate && !m_batchCount)
        { m_dirty=true; m_generationFailed=true; }
    }
    if(m_dirty) ImGui::TextDisabled("Reglages modifies : regenerez avant d'exporter.");
    if(!m_status.empty()) ImGui::TextWrapped("%s",m_status.c_str());
    if(!m_error.empty()) ImGui::TextColored(ImVec4(1,.4f,.3f,1),"%s",m_error.c_str());
    if(m_tree)
    {
        const auto& s=m_tree->statistics;
        ImGui::Separator();
        ImGui::Text("Branches %u | cartes %u",s.branches-s.roots-s.vines,s.leafCards);
        ImGui::Text("Racines %u | lianes %u",s.roots,s.vines);
        ImGui::Text("Triangles LOD0 %u | LOD1 %u",s.triangles[0],s.triangles[1]);
        ImGui::Text("Generation CPU %.1f ms",s.generationMilliseconds);
        if(!m_tree->warning.empty()) ImGui::TextWrapped("%s",m_tree->warning.c_str());
    }
    ImGui::End();
}
void TreeGeneratorLabPanel::drawPreview()
{
    if(!ImGui::Begin("Apercu 3D##TreeLab")) { ImGui::End(); return; }
    if(ImGui::Button("Cadrer")) fitCamera();
    ImGui::SameLine(); m_previewDirty|=ImGui::RadioButton("LOD0",&m_view.lod,0);
    ImGui::SameLine(); m_previewDirty|=ImGui::RadioButton("LOD1",&m_view.lod,1);
    ImGui::SameLine(); m_previewDirty|=ImGui::RadioButton("LOD3 (quad)",&m_view.lod,3);
    ImGui::SameLine(); m_previewDirty|=ImGui::Checkbox("Fil de fer",&m_view.wireframe);
    ImGui::BeginDisabled(m_view.lod==3);
    m_previewDirty|=ImGui::Checkbox("Bois",&m_view.showWood); ImGui::SameLine();
    m_previewDirty|=ImGui::Checkbox("Feuilles",&m_view.showLeaves);
    ImGui::EndDisabled(); ImGui::SameLine();
    m_previewDirty|=ImGui::Checkbox("Sol",&m_view.showGround);
    if(m_view.lod==3)
        ImGui::TextWrapped("LOD3 : 2 triangles, deux vues selon l'elevation de la camera. Eclairage neutre precalcule ; approximation pour les arbres lointains.");
    if(ImGui::CollapsingHeader("Lumiere et vent de l'apercu"))
    {
        ImGui::BeginDisabled(m_view.lod==3);
        m_previewDirty|=ImGui::SliderFloat("Azimut soleil",&m_view.sunYaw,-180,180);
        m_previewDirty|=ImGui::SliderFloat("Elevation soleil",&m_view.sunElevation,5,85);
        m_previewDirty|=ImGui::SliderFloat("Vent",&m_view.windStrength,0,1);
        ImGui::EndDisabled();
        ImGui::TextWrapped("Lumiere et vent interactifs sur les maillages. Le LOD3 utilise ses deux images precalculees ; les 17 vues optionnelles stockent albedo et normales.");
    }
    ImGui::TextDisabled("Glisser gauche : orbite | milieu : deplacer | molette : zoom");
    const ImVec2 avail=ImGui::GetContentRegionAvail();
    const int w=std::clamp(static_cast<int>(avail.x),64,1600);
    const int h=std::clamp(static_cast<int>(avail.y),64,1200);
    if(w!=m_view.width || h!=m_view.height) { m_view.width=w; m_view.height=h; m_previewDirty=true; }
    if(m_view.windStrength>0 && m_view.lod!=3) { m_view.time=static_cast<float>(ImGui::GetTime()); m_previewDirty=true; }
    if(m_gpuReady && m_tree && m_previewDirty)
    {
        if(!m_preview.render(m_view)) m_error=m_preview.error();
        m_previewDirty=false;
    }
    if(m_preview.texture())
    {
        ImGui::Image(reinterpret_cast<ImTextureID>(static_cast<std::intptr_t>(m_preview.texture())),
            ImVec2(static_cast<float>(w),static_cast<float>(h)),ImVec2(0,1),ImVec2(1,0));
        if(ImGui::IsItemHovered())
        {
            const auto& io=ImGui::GetIO();
            if(ImGui::IsMouseDragging(ImGuiMouseButton_Left))
            {
                m_view.yaw+=io.MouseDelta.x*.35f;
                m_view.pitch=std::clamp(m_view.pitch+io.MouseDelta.y*.35f,-20.0f,85.0f);
                m_previewDirty=true;
            }
            if(ImGui::IsMouseDragging(ImGuiMouseButton_Middle))
            {
                const float yaw=glm::radians(m_view.yaw);
                m_view.target+=glm::vec3(-std::cos(yaw)*io.MouseDelta.x,io.MouseDelta.y,std::sin(yaw)*io.MouseDelta.x)
                    *(m_view.distance*.0015f);
                m_previewDirty=true;
            }
            if(io.MouseWheel!=0)
            {
                m_view.distance=std::clamp(m_view.distance*std::exp(-io.MouseWheel*.12f),0.5f,1000.0f);
                m_previewDirty=true;
            }
        }
    }
    else ImGui::TextUnformatted("Preparation de l'apercu...");
    ImGui::End();
}
