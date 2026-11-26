#pragma once

#include "MoonMeshData.hpp"
#include <memory>
#include <string>

namespace Ge
{
// Owning GL context only. The actual mesh is rasterized when its view/lighting
// changes; steady frames only sample the resulting premultiplied HDR image.
class MoonMeshCache final
{
public:
    MoonMeshCache();
    ~MoonMeshCache();
    MoonMeshCache(const MoonMeshCache&)=delete;
    MoonMeshCache& operator=(const MoonMeshCache&)=delete;
    bool setMesh(const MoonMeshData& data,std::string& error);
    void clear() noexcept;
    void invalidate() noexcept;
    bool valid() const noexcept;
    // Observer is relative to the body's centre, divided by physical radius.
    // Sun direction is world-space, towards the Sun, including below horizon.
    // True also means a cache hit; counters identify actual GPU redraws.
    bool update(glm::vec3 observerInBodyRadii,float rotationRadians,
        glm::vec3 sunDirection,int resolution,std::string& error);
    unsigned texture() const noexcept;
    int resolution() const noexcept;
    // uv = .5 + .5 * dot(ray, right/up) / (dot(ray,forward)*right.w).
    // Basis vectors are world-space. Forward.w is 1 for a ready image, else 0.
    glm::vec4 projectionRight() const noexcept;
    glm::vec4 projectionUp() const noexcept;
    glm::vec4 projectionForward() const noexcept;
    std::uint64_t imageUpdates() const noexcept;
    std::uint64_t shadowUpdates() const noexcept;
    std::uint64_t gpuBytes() const noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> m;
};
}
