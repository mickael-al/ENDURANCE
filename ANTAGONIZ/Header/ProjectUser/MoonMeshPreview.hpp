#ifndef ENDURANCE_MOON_MESH_PREVIEW_HPP
#define ENDURANCE_MOON_MESH_PREVIEW_HPP

#include "ProceduralMoon.hpp"
#include "ProceduralMoonGeometry.hpp"
#include <array>
#include <memory>
#include <string>

namespace EnduranceMoon
{
struct MeshPreviewSettings
{
    int width=900, height=800;
    float yaw=0.f, pitch=6.f, distance=4.6f;
    float sunYaw=-40.f, sunElevation=35.f;
    float exposure=1.4f, ambient=0.055f;
    std::array<float,3> background{{0.035f,0.028f,0.055f}}; // linear RGB
    bool wireframe=false, normals=false, shadows=true;
};

// Editor only. Call all methods and destroy on the owning GL context.
// Renders the real indexed mesh, caches unchanged views, and updates the local
// solar shadow map independently of orbit/view changes. No readback or raymarch.
class MoonMeshPreview final
{
public:
    MoonMeshPreview();
    ~MoonMeshPreview();
    MoonMeshPreview(const MoonMeshPreview&)=delete;
    MoonMeshPreview& operator=(const MoonMeshPreview&)=delete;
    bool initialize();
    bool upload(const Geometry& geometry,const Surface& surface);
    bool render(const MeshPreviewSettings& settings);
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
