// Standalone tests of the production native generator; no renderer or Python.
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

// Windows can introduce these after the STL has already been included.
#define min(a, b) (((a) < (b)) ? (a) : (b))
#define max(a, b) (((a) > (b)) ? (a) : (b))
#include "ProceduralMoon.hpp"
#if !defined(min) || !defined(max)
#error ProceduralMoon.hpp must preserve caller Windows macros
#endif
static_assert(min(2, 3) == 2 && max(2, 3) == 3, "Caller macros changed");
#undef min
#undef max

namespace
{
void require(bool condition, const std::string& message)
{
    if (!condition) throw std::runtime_error(message);
}

bool finite(float value)
{
    // std::isfinite can be folded to true by the engine's -ffast-math build.
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    return (bits & 0x7f800000u) != 0x7f800000u;
}

float special(std::uint32_t bits)
{
    float result;
    std::memcpy(&result, &bits, sizeof(result));
    return result;
}

std::vector<std::uint8_t> readFile(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    require(bool(input), "Cannot read test output " + path.string());
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

std::uint32_t readU32(const std::vector<std::uint8_t>& bytes, std::size_t offset)
{
    return std::uint32_t(bytes[offset]) | (std::uint32_t(bytes[offset + 1]) << 8) |
           (std::uint32_t(bytes[offset + 2]) << 16) | (std::uint32_t(bytes[offset + 3]) << 24);
}

void checkSurface(const EnduranceMoon::Surface& surface)
{
    require(surface.width >= 64 && surface.width <= 4096, "Surface width outside runtime limits");
    require(surface.width == 2 * surface.height, "Surface is not a 2:1 spherical atlas");
    const std::size_t count = std::size_t(surface.width) * surface.height;
    require(surface.pixels.size() == count * 4, "Surface payload dimensions disagree");
    require(finite(surface.meanAlbedo) && surface.meanAlbedo > .01f && surface.meanAlbedo < .9f,
            "Surface mean is invalid");
    std::uint64_t albedoSum = 0;
    double maximumLengthError = 0;
    for (std::size_t i = 0; i < count; ++i)
    {
        double lengthSquared = 0;
        for (std::size_t channel = 0; channel < 3; ++channel)
        {
            const double value = surface.pixels[i * 4 + channel] / 127.5 - 1;
            lengthSquared += value * value;
        }
        maximumLengthError = std::max(maximumLengthError, std::abs(std::sqrt(lengthSquared) - 1));
        albedoSum += surface.pixels[i * 4 + 3];
    }
    require(maximumLengthError < .008, "Decoded object-space normals are not unit length");
    const double quantizedMean = double(albedoSum) / (255.0 * count);
    require(std::abs(quantizedMean - surface.meanAlbedo) < 2e-7,
            "Header mean differs from the quantized albedo payload");
}

void checkSanitize()
{
    using namespace EnduranceMoon;
    Params malformed;
    malformed.seed = 0xffffffffu;
    malformed.width = (std::numeric_limits<int>::max)();
    malformed.octaves = (std::numeric_limits<int>::max)();
    malformed.fractureCount = (std::numeric_limits<int>::max)();
    auto result = sanitize(malformed);
    require(result.width == 4096 && result.octaves == 10 && result.fractureCount == 32,
            "Huge integer parameters are not bounded");
    require(result.seed == malformed.seed, "Sanitizing changed the seed");
    malformed.width = (std::numeric_limits<int>::min)();
    malformed.octaves = -100;
    malformed.fractureCount = -100;
    result = sanitize(malformed);
    require(result.width == 64 && result.octaves == 1 && result.fractureCount == 0,
            "Negative integer parameters are not bounded");
    malformed.width = 1000;
    require(sanitize(malformed).width == 512, "Width must round down to a supported power of two");

    for (float invalid : {special(0x7fc12345u), special(0x7f800000u), special(0xff800000u),
                          -1e30f, 1e30f})
    {
        malformed.macroScale = malformed.detailScale = malformed.fractureWidth = invalid;
        malformed.fractureDepth = malformed.fractureStrength = invalid;
        result = sanitize(malformed);
        require(finite(result.macroScale) && result.macroScale >= .25f && result.macroScale <= 12,
                "Macro scale is not finite and bounded");
        require(finite(result.detailScale) && result.detailScale >= 2 && result.detailScale <= 96,
                "Detail scale is not finite and bounded");
        require(finite(result.fractureWidth) && result.fractureWidth >= .001f && result.fractureWidth <= .06f,
                "Fracture width is not finite and bounded");
        require(finite(result.fractureDepth) && result.fractureDepth >= 0 && result.fractureDepth <= .05f,
                "Fracture depth is not finite and bounded");
        require(finite(result.fractureStrength) && result.fractureStrength >= 0 && result.fractureStrength <= 3,
                "Fracture strength is not finite and bounded");
        const Params again = sanitize(result);
        require(again.width == result.width && again.seed == result.seed &&
                    again.macroScale == result.macroScale && again.detailScale == result.detailScale &&
                    again.octaves == result.octaves && again.fractureCount == result.fractureCount &&
                    again.fractureWidth == result.fractureWidth && again.fractureDepth == result.fractureDepth &&
                    again.fractureStrength == result.fractureStrength,
                "Sanitizing valid parameters is not idempotent");
    }

    // Exercise generate's own boundary rather than relying on its caller to sanitize.
    malformed.width = -1;
    malformed.macroScale = special(0x7fc00001u);
    malformed.detailScale = special(0x7f800000u);
    malformed.fractureDepth = special(0xff800000u);
    malformed.fractureStrength = special(0x7fc00001u);
    checkSurface(generate(malformed));
}

void checkContinuity(const EnduranceMoon::Surface& surface)
{
    const int width = surface.width, height = surface.height;
    double seam[4]{}, interior[4]{};
    for (int y = 0; y < height; ++y)
        for (int channel = 0; channel < 4; ++channel)
        {
            const auto row = std::size_t(y) * width * 4;
            seam[channel] += std::abs(int(surface.pixels[row + channel]) -
                                      int(surface.pixels[row + (width - 1) * 4 + channel]));
            for (int x = 1; x < width; ++x)
                interior[channel] += std::abs(int(surface.pixels[row + x * 4 + channel]) -
                                              int(surface.pixels[row + (x - 1) * 4 + channel]));
        }
    for (int channel = 0; channel < 4; ++channel)
    {
        seam[channel] /= height;
        interior[channel] /= double(height) * (width - 1);
        // The first and last pixels are adjacent texel centers, not duplicate
        // endpoints. Compare their step to ordinary horizontal texture steps.
        require(seam[channel] <= 4 * interior[channel] + 2,
                "Longitude seam has an anomalous discontinuity in channel " + std::to_string(channel));
    }

    double poleNormalDelta = 0, poleAlbedoDelta = 0;
    for (int y : {0, height - 1})
        for (int x = 0; x < width / 2; ++x)
        {
            const auto a = (std::size_t(y) * width + x) * 4;
            const auto b = a + (width / 2) * 4;
            double deltaSquared = 0;
            for (int channel = 0; channel < 3; ++channel)
            {
                const double delta = (int(surface.pixels[a + channel]) -
                                      int(surface.pixels[b + channel])) / 127.5;
                deltaSquared += delta * delta;
            }
            poleNormalDelta += std::sqrt(deltaSquared);
            poleAlbedoDelta += std::abs(int(surface.pixels[a + 3]) - int(surface.pixels[b + 3])) / 255.0;
        }
    poleNormalDelta /= width;
    poleAlbedoDelta /= width;
    // Generous geometric bounds catch an unwrapped planar texture at a pole
    // without encoding the art direction or requiring identical pole pixels.
    require(poleNormalDelta < .75, "Normals diverge around the polar cap");
    require(poleAlbedoDelta < .20, "Albedo diverges around the polar cap");
    std::cout << "  seam albedo step " << seam[3] << " (interior " << interior[3]
              << "), pole normal/albedo deltas " << poleNormalDelta << '/' << poleAlbedoDelta << '\n';
}

void checkFile(const EnduranceMoon::Surface& surface, const EnduranceMoon::Surface& replacement,
               const std::filesystem::path& destination)
{
    using namespace EnduranceMoon;
    std::string error = "old error";
    require(save(surface, destination.string(), error), "Save failed: " + error);
    require(error.empty(), "Successful save retained an old error");
    const auto bytes = readFile(destination);
    require(bytes.size() == 20 + surface.pixels.size(), "EMOON001 file has the wrong byte size");
    require(std::memcmp(bytes.data(), "EMOON001", 8) == 0, "EMOON001 magic changed");
    require(readU32(bytes, 8) == std::uint32_t(surface.width) &&
                readU32(bytes, 12) == std::uint32_t(surface.height),
            "EMOON001 dimensions are not little endian");
    const auto bits = readU32(bytes, 16);
    const float decodedMean = special(bits);
    require(decodedMean == surface.meanAlbedo, "EMOON001 float mean did not round trip");
    require(std::equal(surface.pixels.begin(), surface.pixels.end(), bytes.begin() + 20),
            "EMOON001 payload did not round trip");

    const auto reject = [&](const Surface& bad, const char* reason)
    {
        error.clear();
        require(!save(bad, destination.string(), error), std::string("Accepted bad surface: ") + reason);
        require(!error.empty(), std::string("Missing error for bad surface: ") + reason);
        require(readFile(destination) == bytes, std::string("Failed save changed existing file: ") + reason);
    };
    Surface bad = surface;
    bad.pixels.pop_back(); reject(bad, "short pixel payload");
    bad = surface; bad.height += 1; reject(bad, "wrong dimensions");
    bad = surface; bad.width = -1; reject(bad, "negative width");
    bad = surface; bad.meanAlbedo = special(0x7fc00001u); reject(bad, "NaN mean");
    bad = surface; bad.meanAlbedo = special(0x7f800000u); reject(bad, "infinite mean");
    bad = surface; bad.meanAlbedo = -1; reject(bad, "negative mean");
    bad = surface; bad.meanAlbedo = 2; reject(bad, "mean outside unit range");
    reject(Surface{}, "empty surface");
    require(!save(surface, (destination / "not-a-directory.surface").string(), error),
            "Save into a regular file unexpectedly succeeded");
    require(!error.empty() && readFile(destination) == bytes,
            "I/O failure did not preserve the existing destination");

    // Force publication to fail after the complete temporary file was written.
    // Existing directories and their contents must survive, and no temporary
    // output directory should remain after the failed rename.
    const auto occupied = destination.parent_path() / "occupied.surface";
    std::filesystem::create_directories(occupied);
    const auto marker = occupied / "keep.txt";
    { std::ofstream output(marker); output << "keep existing data"; }
    const auto markerBytes = readFile(marker);
    require(!save(surface, occupied.string(), error), "Save replaced an existing directory");
    require(!error.empty() && readFile(marker) == markerBytes,
            "Failed publication damaged the existing directory");

    require(save(replacement, destination.string(), error), "Replacing a valid output failed: " + error);
    const auto replaced = readFile(destination);
    require(error.empty() && replaced != bytes && replaced.size() == 20 + replacement.pixels.size() &&
                std::equal(replacement.pixels.begin(), replacement.pixels.end(), replaced.begin() + 20),
            "Successful overwrite retained old or partial pixel data");
    for (const auto& entry : std::filesystem::directory_iterator(destination.parent_path()))
        require(entry.path().filename().string().find(".moon-bake-") != 0,
                "Saving left a temporary output directory behind");
}
}

int main(int argc, char** argv)
{
    try
    {
        require(argc == 2, "Usage: check_moon_bake TEMPORARY_OUTPUT_DIRECTORY");
        const auto directory = std::filesystem::path(argv[1]);
        std::filesystem::create_directories(directory);
        checkSanitize();

        using namespace EnduranceMoon;
        Params params;
        params.width = 256;
        params.seed = 238741u;
        const Surface first = generate(params);
        checkSurface(first);
        const Surface again = generate(params);
        require(first.pixels == again.pixels && first.meanAlbedo == again.meanAlbedo,
                "Fixed seed is not deterministic");
        Params changed = params;
        changed.seed += 1;
        const Surface other = generate(changed);
        checkSurface(other);
        std::size_t seedChanged = 0;
        for (std::size_t i = 0; i < first.pixels.size(); i += 4)
            if (!std::equal(first.pixels.begin() + i, first.pixels.begin() + i + 4, other.pixels.begin() + i))
                ++seedChanged;
        require(seedChanged > first.pixels.size() / 32, "Changing seed barely changes the surface");

        Params intact = params;
        intact.fractureStrength = 0;
        const Surface unbroken = generate(intact);
        checkSurface(unbroken);
        std::size_t fractureChanged = 0;
        double fractureAlbedoDelta = 0;
        for (std::size_t i = 0; i < first.pixels.size(); i += 4)
        {
            bool visible = false;
            for (std::size_t channel = 0; channel < 4; ++channel)
                visible |= std::abs(int(first.pixels[i + channel]) - int(unbroken.pixels[i + channel])) > 2;
            fractureChanged += visible;
            fractureAlbedoDelta += std::abs(int(first.pixels[i + 3]) - int(unbroken.pixels[i + 3]));
        }
        const std::size_t pixelCount = first.pixels.size() / 4;
        fractureAlbedoDelta /= pixelCount;
        require(fractureChanged > pixelCount / 100, "Fractures do not visibly modify enough of the surface");
        require(fractureAlbedoDelta > .15, "Fractures have no material contrast");
        std::cout << "  fractures change " << fractureChanged << '/' << pixelCount
                  << " texels; mean albedo byte delta " << fractureAlbedoDelta << '\n';
        checkContinuity(first);
        checkContinuity(unbroken);
        checkFile(first, other, directory / "native-moon.surface");

        std::atomic_bool cancel{true};
        require(generate(params, &cancel).pixels.empty(), "Pre-cancelled generation returned a surface");
        cancel.store(false);
        Params large = params;
        large.width = 2048;
        std::thread stop([&cancel]
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            cancel.store(true);
        });
        const auto start = std::chrono::steady_clock::now();
        const Surface cancelled = generate(large, &cancel);
        stop.join();
        require(cancelled.pixels.empty(), "Cancelled generation published a partial/full surface");
        require(std::chrono::steady_clock::now() - start < std::chrono::seconds(5),
                "Cancellation did not interrupt generation promptly");

        std::cout << "PASS native lunar determinism, seed variation, sanitization, unit normals, "
                     "fractures, spherical continuity, EMOON001 round trip, safe failed saves, cancellation\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL native lunar bake: " << error.what() << '\n';
        return 1;
    }
}
