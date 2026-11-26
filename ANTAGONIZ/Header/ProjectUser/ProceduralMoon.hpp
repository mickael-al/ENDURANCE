#ifndef ENDURANCE_PROCEDURAL_MOON_HPP
#define ENDURANCE_PROCEDURAL_MOON_HPP

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

// Offline/editor-only atlas generation. There are no rendering dependencies and
// no shared random state; independent bakes may run concurrently.
namespace EnduranceMoon
{
struct Params
{
    std::uint32_t seed = 68291;
    int width = 2048;
    float macroScale = 2.4f;
    float detailScale = 18.f;
    int octaves = 8;
    int fractureCount = 9;
    float fractureWidth = 0.018f;
    float fractureDepth = 0.025f;
    float fractureStrength = 1.25f;
};

struct Surface
{
    int width = 0;
    int height = 0;
    float meanAlbedo = 0.f;
    // South-to-north equirectangular rows, linear RGBA8. RGB is a unit
    // object-space normal mapped to [0,1]; alpha is material albedo.
    std::vector<std::uint8_t> pixels;
};

// Width is clamped to 64..4096 and rounded down to a power of two. Nonfinite
// values fall back to defaults; all other values are bounded for offline use.
Params sanitize(Params params);

// Returns an empty surface when cancelled. Cancellation is observed between
// rows/impacts. Peak working memory at 4096 is approximately 170 MiB.
Surface generate(const Params& params, const std::atomic_bool* cancel = nullptr);

// Writes EMOON001, LE uint32 width/height, LE float32 mean, then linear RGBA8.
// Publishes a complete sibling temporary file atomically. A failed save leaves
// an existing destination intact. Parent directories are created if needed.
bool save(const Surface& surface, const std::string& path, std::string& error);
}

#endif
