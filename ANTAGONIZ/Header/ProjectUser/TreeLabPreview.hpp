#ifndef ENDURANCE_TREE_LAB_PREVIEW_HPP
#define ENDURANCE_TREE_LAB_PREVIEW_HPP

#include "ProceduralTree.hpp"
#include <memory>
#include <string>
#include <vector>

namespace EnduranceTrees
{
struct PreviewSettings
{
    int width=900, height=700, lod=0;
    float yaw=35.0f, pitch=15.0f, distance=32.0f; // Degrees and metres.
    glm::vec3 target{0.0f,9.0f,0.0f};
    glm::vec3 background{0.075f,0.095f,0.12f}; // Linear RGB.
    float sunYaw=40.0f, sunElevation=45.0f;
    float time=0.0f, windStrength=0.0f;
    bool showWood=true, showLeaves=true, wireframe=false, showGround=true;
};

// Developer-only preview/bake helper. Every operation, including destruction,
// must run on the owning OpenGL context. Does not register with the game renderer.
class TreeLabPreview final
{
public:
    TreeLabPreview();
    ~TreeLabPreview();
    TreeLabPreview(const TreeLabPreview&)=delete;
    TreeLabPreview& operator=(const TreeLabPreview&)=delete;
    bool initialize();
    bool reloadShaders();
    bool upload(const Tree& tree, const std::vector<ImpostorView>* lod3=nullptr);
    bool render(const PreviewSettings& settings);
    // Explicit offline operation; blocking readback is never used by preview.
    // Exactly 17 unlit albedo/object-normal views. lod==3 selects up to three
    // cached views on one quad and relights the reconstructed normal.
    bool captureImpostors(std::vector<ImpostorView>& views, int resolution=256);
    unsigned int texture() const noexcept;
    int width() const noexcept;
    int height() const noexcept;
    const std::string& error() const noexcept;
    void release() noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> m;
};
}
#endif
