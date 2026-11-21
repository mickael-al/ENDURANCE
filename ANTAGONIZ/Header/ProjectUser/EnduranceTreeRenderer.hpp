#ifndef ENDURANCE_TREE_RENDERER_HPP
#define ENDURANCE_TREE_RENDERER_HPP
#include "CustomRenderer.hpp"
#include "TreeRenderGpu.hpp"

struct ptrClass;
class ProceduralGeneration;
namespace Endurance
{
class TreeRenderer final : public CustomRenderer
{
public:
    TreeRenderer();
    ~TreeRenderer() override;
    bool initialize(const ptrClass& engine,ProceduralGeneration& terrain,
                    const EnduranceTrees::RenderSettings& settings);
    void prepareRenderer() override;
    void drawRenderer() override;
    void drawShadowRenderer(unsigned shadowIndex) override;
    std::uint64_t shadowCasterBoundsRevision() const override;
    std::uint64_t shadowContentRevision() const override;
    void appendShadowCasterBounds(std::vector<Ge::ShadowCasterBounds>& output) const override;
    void shutdown() noexcept;
    const EnduranceTrees::RenderStats& stats() const noexcept;
    const std::string& error() const noexcept;
private:
    const ptrClass* m_engine=nullptr;
    ProceduralGeneration* m_terrain=nullptr;
    EnduranceTrees::TreeRenderGpu m_gpu;
    EnduranceTrees::RenderView m_view;
    std::uint64_t m_revision=0,m_boundsToken=0,m_contentToken=0;
    bool m_registered=false,m_hasView=false;
};
}
#endif
