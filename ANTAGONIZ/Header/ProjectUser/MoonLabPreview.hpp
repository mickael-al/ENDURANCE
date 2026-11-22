#ifndef ENDURANCE_MOON_LAB_PREVIEW_HPP
#define ENDURANCE_MOON_LAB_PREVIEW_HPP

#include "ProceduralMoon.hpp"
#include <atomic>
#include <cstdint>
#include <vector>

namespace EnduranceMoon
{
// Display images use top-left origin and opaque RGBA8 pixels. Albedo and lit
// sphere values are converted from linear light to sRGB for the editor.
struct PreviewImage
{
    int width=0, height=0;
    std::vector<std::uint8_t> pixels;
};

// CPU-only helpers: no context is required and no OpenGL state is touched.
// Cancellation returns an empty image. The sphere resolution is bounded to
// 64..1024; the atlas is downsampled to at most 1024 pixels wide.
PreviewImage renderSphere(const Surface& surface,float yawDegrees,float sunDegrees,
    int resolution=384,const std::atomic_bool* cancel=nullptr);
PreviewImage renderAtlas(const Surface& surface,bool normals);
}
#endif
