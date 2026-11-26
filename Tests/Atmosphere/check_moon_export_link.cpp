// Link the real Moon exporter's private stb writer with the public writer used
// by TextureManager.cpp. Standalone exporter tests cannot catch this collision.
#include "ProceduralMoonExport.hpp"
#include "hv/cpputil/json.hpp"
#define STB_IMAGE_IMPLEMENTATION
#include "stb-cmake/stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb-cmake/stb_image_write.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <vector>

namespace
{
using Bytes = std::vector<unsigned char>;
void require(bool ok, const char* message)
{ if (!ok) throw std::runtime_error(message); }
void append(void* context, void* data, int size)
{
    const auto* first = static_cast<unsigned char*>(data);
    auto& bytes = *static_cast<Bytes*>(context);
    bytes.insert(bytes.end(), first, first + size);
}
void checkPublicWriter()
{
    constexpr int width = 16, height = 2;
    Bytes rgb(width * height * 3);
    for (std::size_t i = 0; i < rgb.size(); ++i)
        rgb[i] = static_cast<unsigned char>((i * 29 + 7) % 256);
    Bytes png;
    require(stbi_write_png_to_func(append, &png, width, height, 3,
        rgb.data(), width * 3) != 0, "Public PNG encoding failed");
    int w = 0, h = 0, channels = 0;
    std::unique_ptr<unsigned char, decltype(&stbi_image_free)> decoded(
        stbi_load_from_memory(png.data(), static_cast<int>(png.size()), &w, &h, &channels, 3), stbi_image_free);
    require(decoded && w == width && h == height, "Public PNG dimensions failed");
    require(std::equal(rgb.begin(), rgb.end(), decoded.get()), "Public PNG pixels changed");

    // One constant row and one varying row exercise both HDR RLE helpers.
    std::vector<float> linear(width * height * 3);
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
            for (int c = 0; c < 3; ++c)
                linear[(y * width + x) * 3 + c] = (c + 1) * (y ? (x + 1) * .25f : 2.f);
    Bytes hdr;
    require(stbi_write_hdr_to_func(append, &hdr, width, height, 3, linear.data()) != 0,
        "Public HDR encoding failed");
    std::unique_ptr<float, decltype(&stbi_image_free)> floats(
        stbi_loadf_from_memory(hdr.data(), static_cast<int>(hdr.size()), &w, &h, &channels, 3), stbi_image_free);
    require(floats && w == width && h == height, "Public HDR dimensions failed");
    for (std::size_t i = 0; i < linear.size(); ++i)
        require(std::isfinite(floats.get()[i]) && std::abs(floats.get()[i] - linear[i]) <= .01f * linear[i],
            "Public HDR pixels changed");
}
std::uint32_t read32(const Bytes& bytes, std::size_t offset)
{
    require(offset <= bytes.size() && bytes.size() - offset >= 4, "Truncated GLB");
    return std::uint32_t(bytes[offset]) | (std::uint32_t(bytes[offset + 1]) << 8) |
        (std::uint32_t(bytes[offset + 2]) << 16) | (std::uint32_t(bytes[offset + 3]) << 24);
}
void checkMoonExport(const std::filesystem::path& output)
{
    EnduranceMoon::Geometry mesh;
    mesh.vertices.resize(3);
    mesh.vertices[0].position = {0, 0, 0};
    mesh.vertices[1].position = {1, 0, 0};
    mesh.vertices[2].position = {0, 1, 0};
    for (auto& vertex : mesh.vertices) vertex.normal = {0, 0, 1};
    mesh.indices = {0, 1, 2};
    mesh.parts.push_back({0, 3, false, {}});
    EnduranceMoon::Surface surface;
    surface.width = 64; surface.height = 32;
    surface.pixels.resize(surface.width * surface.height * 4);
    for (int y = 0; y < surface.height; ++y)
        for (int x = 0; x < surface.width; ++x)
            surface.pixels[(y * surface.width + x) * 4 + 3] = static_cast<unsigned char>((x * 5 + y * 7) % 256);
    std::string error;
    if (!EnduranceMoon::saveModel(mesh, surface, output.string(), error))
        throw std::runtime_error(error);
    std::ifstream file(output, std::ios::binary);
    Bytes glb{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    require(read32(glb, 0) == 0x46546c67 && read32(glb, 4) == 2 && read32(glb, 8) == glb.size(),
        "Invalid GLB header");
    const auto jsonSize = read32(glb, 12);
    require(read32(glb, 16) == 0x4e4f534a && jsonSize <= glb.size() - 20, "Invalid GLB JSON chunk");
    const auto json = nlohmann::json::parse(glb.begin() + 20, glb.begin() + 20 + jsonSize);
    const std::size_t binaryHeader = 20 + jsonSize, binaryStart = binaryHeader + 8;
    const auto binarySize = read32(glb, binaryHeader);
    require(read32(glb, binaryHeader + 4) == 0x004e4942 && binaryStart <= glb.size() &&
        binarySize == glb.size() - binaryStart, "Invalid GLB binary chunk");
    const auto viewIndex = json.at("images").at(0).at("bufferView").get<std::size_t>();
    const auto& view = json.at("bufferViews").at(viewIndex);
    const auto offset = view.at("byteOffset").get<std::size_t>();
    const auto length = view.at("byteLength").get<std::size_t>();
    require(offset <= binarySize && length <= binarySize - offset, "PNG outside GLB buffer");
    int width = 0, height = 0, channels = 0;
    std::unique_ptr<unsigned char, decltype(&stbi_image_free)> pixels(
        stbi_load_from_memory(glb.data() + binaryStart + offset, static_cast<int>(length),
            &width, &height, &channels, 3), stbi_image_free);
    require(pixels && width == surface.width && height == surface.height, "Embedded Moon PNG invalid");
    for (int i = 0; i < width * height; ++i)
    {
        const float linear = surface.pixels[i * 4 + 3] / 255.f;
        const float srgb = linear <= .0031308f ? 12.92f * linear : 1.055f * std::pow(linear, 1.f / 2.4f) - .055f;
        const int expected = static_cast<int>(srgb * 255.f + .5f);
        for (int c = 0; c < 3; ++c)
            require(std::abs(int(pixels.get()[i * 3 + c]) - expected) <= 1,
                "Embedded Moon albedo or row order changed");
    }
    std::filesystem::remove(output);
}
}
int main(int argc, char** argv)
{
    try
    {
        require(argc == 2, "Pass a temporary GLB output path");
        checkPublicWriter();
        checkMoonExport(argv[1]);
        std::cout << "Moon/private + engine/public stb linkage, PNG/HDR and GLB round trips passed\n";
        return 0;
    }
    catch (const std::exception& error)
    { std::cerr << error.what() << '\n'; return 1; }
}
