#ifndef ENDURANCE_TREE_RENDER_GPU_HPP
#define ENDURANCE_TREE_RENDER_GPU_HPP

#include "ProceduralTerrainObjects.hpp"
#include "ShadowCasterBounds.hpp"
#include <glm/glm.hpp>
#include <memory>

namespace EnduranceTrees
{
struct RenderSettings
{
    float lod0Coverage=0.25f, impostorCoverage=0.06f;
    float transitionFraction=0.15f, maximumDistance=2048.0f;
    std::uint32_t modelBudgetMiB=512, maximumResidentModels=64;
    std::string shaderDirectory="../ENDURANCE/Ressources/Shader/Trees";
};
struct RenderView
{
    glm::mat4 clip{1}, view{1};
    glm::vec3 camera{0}; // Main camera also controls shadow LOD/distance.
    float projectionScale=1.0f;
    float albedoGamma=2.2f;
    bool orthographic=false;
};
struct RenderStats
{
    std::uint32_t loadedModels=0, pendingModels=0, failedModels=0, budgetSkippedModels=0;
    std::uint32_t instances=0, cameraBlocks=0, shadowBlocks=0;
    std::uint64_t modelBytes=0, workingBytes=0, assetUploads=0, transformBuilds=0;
    std::uint64_t cullDispatches=0, drawCalls=0, revision=0;
    std::uint64_t transformedInstances=0, transformPagesBuilt=0, transformPagesReused=0, indirectCommands=0;
    std::uint64_t selectionUploads=0, distanceSelections=0;
    double assetLoadMilliseconds=0, assetUploadMilliseconds=0;
};

// Owning GL thread only, except the private bounded asset-loading worker.
// No engine dependency: the same implementation is exercised by EGL tests.
class TreeRenderGpu final
{
public:
    TreeRenderGpu();
    ~TreeRenderGpu();
    TreeRenderGpu(const TreeRenderGpu&)=delete;
    TreeRenderGpu& operator=(const TreeRenderGpu&)=delete;
    bool initialize(const std::vector<ProceduralTerrain::ObjectPrototype>& prototypes,
                    const RenderSettings& settings={});
    void prepare(unsigned positionBuffer,const std::vector<ProceduralTerrain::ObjectBlock>& blocks,
                 std::uint64_t revision,const RenderView& view);
    // pass 0 = G-buffer; other IDs = independent shadow layers. The framebuffer,
    // viewport, depth comparison and polygon offset are owned by the caller.
    void draw(std::uint32_t pass,const glm::mat4& clip);
    // CPU-only exact selected-content token; no GL state queries or readbacks.
    // Uses the same page selection as draw, including main-camera LOD/fade state.
    std::uint64_t shadowContentRevision(std::uint32_t pass,const glm::mat4& clip) const;
    void appendBounds(std::vector<Ge::ShadowCasterBounds>& output) const;
    void shutdown() noexcept;
    const RenderStats& stats() const noexcept;
    const std::string& error() const noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> m;
};
}
#endif
