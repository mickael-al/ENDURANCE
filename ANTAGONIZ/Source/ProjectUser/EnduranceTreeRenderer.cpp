#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "glcore.hpp"
#include "EnduranceTreeRenderer.hpp"
#include "ProceduralGeneration.hpp"
#include "PointeurClass.hpp"
#include "Camera.hpp"
#include <algorithm>
#include <cmath>

namespace Endurance
{
TreeRenderer::TreeRenderer()=default;
TreeRenderer::~TreeRenderer() { shutdown(); }
bool TreeRenderer::initialize(const ptrClass& engine,ProceduralGeneration& terrain,
                              const EnduranceTrees::RenderSettings& settings)
{
    shutdown();
    if(!engine.graphiquePipelineManager || !engine.cameraManager) return false;
    if(!m_gpu.initialize(terrain.getConfig().objects.prototypes,settings)) return false;
    m_engine=&engine; m_terrain=&terrain;
    m_boundsToken=m_contentToken=Ge::nextShadowCasterBoundsRevision();
    engine.graphiquePipelineManager->addCustomRenderer(this); m_registered=true;
    return true;
}
void TreeRenderer::prepareRenderer()
{
    if(!m_engine || !m_terrain) return;
    auto* camera=m_engine->cameraManager->getCurrentCamera();
    if(!camera) { m_hasView=false; return; }
    const auto* uniforms=camera->getUniformCamera();
    EnduranceTrees::RenderView view;
    view.view=uniforms->view; view.clip=uniforms->proj*uniforms->view;
    view.camera=camera->getPosition(); view.projectionScale=std::abs(uniforms->proj[1][1]);
    view.orthographic=camera->getOrtho();
    float gamma=m_engine->settingManager?m_engine->settingManager->getGamma():2.2f;
    view.albedoGamma=ProceduralTerrain::finiteFieldValue(gamma)&&gamma>0?(std::max)(gamma,0.001f):2.2f;
    const bool lodChanged=!m_hasView || m_view.camera!=view.camera ||
        m_view.projectionScale!=view.projectionScale || m_view.orthographic!=view.orthographic;
    m_view=view; m_hasView=true;
    m_gpu.prepare(m_terrain->getObjectInstanceBuffer(),m_terrain->getObjectBlocks(),
        m_terrain->getObjectPlacementStats().revision,m_view);
    if(m_revision!=m_gpu.stats().revision)
    {
        m_revision=m_gpu.stats().revision;
        m_boundsToken=m_contentToken=Ge::nextShadowCasterBoundsRevision();
    }
    else if(lodChanged) m_contentToken=Ge::nextShadowCasterBoundsRevision();
}
void TreeRenderer::drawRenderer()
{ if(m_hasView) m_gpu.draw(0,m_view.clip); }
void TreeRenderer::drawShadowRenderer(unsigned shadowIndex)
{
    glm::mat4 clip;
    if(m_hasView && m_engine->lightManager && m_engine->lightManager->getShadowMatrix(shadowIndex,clip))
        m_gpu.draw(shadowIndex+1u,clip);
}
std::uint64_t TreeRenderer::shadowCasterBoundsRevision() const { return m_boundsToken; }
std::uint64_t TreeRenderer::shadowContentRevision() const { return m_contentToken; }
void TreeRenderer::appendShadowCasterBounds(std::vector<Ge::ShadowCasterBounds>& output) const
{ m_gpu.appendBounds(output); }
void TreeRenderer::shutdown() noexcept
{
    if(m_registered && m_engine && m_engine->graphiquePipelineManager)
        m_engine->graphiquePipelineManager->removeCustomRenderer(this);
    m_registered=m_hasView=false; m_engine=nullptr; m_terrain=nullptr;
    m_revision=m_boundsToken=m_contentToken=0; m_gpu.shutdown();
}
const EnduranceTrees::RenderStats& TreeRenderer::stats() const noexcept { return m_gpu.stats(); }
const std::string& TreeRenderer::error() const noexcept { return m_gpu.error(); }
}
