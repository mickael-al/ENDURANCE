#include "EnduranceMoonMesh.hpp"
#include "hv/cpputil/json.hpp"
// Public decoder deliberately shares the process with the loader's private
// decoder, reproducing the engine setup and its global vertical-flip state.
#define STB_IMAGE_IMPLEMENTATION
#include "stb-cmake/stb_image.h"
#include <glm/geometric.hpp>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

namespace
{
using Bytes = std::vector<std::uint8_t>;
using Json = nlohmann::json;
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
std::uint32_t word(const std::uint8_t* p)
{ return std::uint32_t(p[0]) | (std::uint32_t(p[1]) << 8) | (std::uint32_t(p[2]) << 16) | (std::uint32_t(p[3]) << 24); }
void append(Bytes& bytes, std::uint32_t value)
{ for (unsigned i = 0; i < 4; ++i) bytes.push_back(static_cast<std::uint8_t>(value >> (i * 8))); }
Bytes read(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}
void write(const std::filesystem::path& path, const Bytes& bytes)
{
    std::ofstream output(path, std::ios::binary);
    output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    require(bool(output), "Cannot write loader fixture");
}
Bytes glb(const Json& json, const Bytes& binary)
{
    std::string text = json.dump();
    while (text.size() % 4) text.push_back(' ');
    Bytes bytes;
    append(bytes, 0x46546c67); append(bytes, 2);
    append(bytes, static_cast<std::uint32_t>(28 + text.size() + binary.size()));
    append(bytes, static_cast<std::uint32_t>(text.size())); append(bytes, 0x4e4f534a);
    bytes.insert(bytes.end(), text.begin(), text.end());
    append(bytes, static_cast<std::uint32_t>(binary.size())); append(bytes, 0x004e4942);
    bytes.insert(bytes.end(), binary.begin(), binary.end());
    return bytes;
}
}
int main(int argc, char** argv)
{
    try
    {
        require(argc == 3, "Pass the committed Moon GLB and a temporary directory");
        const auto source = read(argv[1]);
        require(source.size() > 28, "Missing committed Moon asset");
        const auto jsonSize = word(source.data() + 12);
        const auto json = Json::parse(source.begin() + 20, source.begin() + 20 + jsonSize);
        const Bytes binary(source.begin() + 28 + jsonSize, source.end());
        Ge::MoonMeshData loaded;
        std::string error;
        // Leave the public decoder flipped as an unrelated texture importer can.
        stbi_set_flip_vertically_on_load(1);
        if (!EnduranceMoon::loadMoonMesh(argv[1], loaded, error)) throw std::runtime_error(error);
        require(loaded.vertices.size() == 49412 && loaded.indices.size() == 292800,
            "Lost body or detached fragment geometry");
        require(loaded.width == 2048 && loaded.height == 1024 && loaded.albedoRgba.size() == 2048u * 1024u * 4u,
            "Moon atlas dimensions changed");
        require(loaded.meanAlbedo > .01f && loaded.meanAlbedo < .8f, "Invalid mean lunar reflectance");
        float radius = 0;
        std::uint32_t maximumIndex = 0;
        for (const auto& v : loaded.vertices)
        {
            radius = (std::max)(radius, glm::length(v.position));
            require(std::abs(glm::length(v.normal) - 1.f) < .0001f, "Invalid imported normal");
            require(v.color.a == 1.f, "Invalid linear vertex opacity");
        }
        for (auto index : loaded.indices)
        {
            require(index < loaded.vertices.size(), "Unrebased part index");
            maximumIndex = (std::max)(maximumIndex, index);
        }
        require(maximumIndex > 49000 && radius > 1.5f && radius < 4.f,
            "Detached fragments lost or original body coordinates rescaled");
        const auto& imageView = json.at("bufferViews").at(json.at("images").at(0).at("bufferView").get<std::size_t>());
        const auto pngOffset = imageView.at("byteOffset").get<std::size_t>();
        const auto pngSize = imageView.at("byteLength").get<int>();
        int width = 0, height = 0, channels = 0;
        stbi_set_flip_vertically_on_load(0);
        auto* pixels = stbi_load_from_memory(binary.data() + pngOffset, pngSize, &width, &height, &channels, 4);
        require(pixels && width == 2048 && height == 1024, "Reference atlas decode failed");
        const bool identical = std::equal(loaded.albedoRgba.begin(), loaded.albedoRgba.end(), pixels);
        stbi_image_free(pixels);
        require(identical, "Private decoder used the public flip flag or changed atlas pixels");

        unsigned rejected = 0;
        const auto badPath = std::filesystem::path(argv[2]) / "invalid.glb";
        const auto originalPosition = loaded.vertices[0].position;
        auto reject = [&](const Bytes& bytes)
        {
            write(badPath, bytes);
            require(!EnduranceMoon::loadMoonMesh(badPath.string(), loaded, error) && !error.empty(),
                "Malformed or unsupported Moon asset accepted");
            require(loaded.vertices.size() == 49412 && loaded.indices.size() == 292800 &&
                loaded.vertices[0].position == originalPosition && loaded.albedoRgba.size() == 2048u * 1024u * 4u,
                "Failed asset load replaced a valid mesh");
            ++rejected;
        };
        reject(Bytes(source.begin(), source.begin() + source.size() / 2));
        auto changed = json; changed["nodes"][0]["translation"] = Json::array({1, 0, 0}); reject(glb(changed, binary));
        changed = json; changed["accessors"][0]["count"] = 0xffffffffu; reject(glb(changed, binary));
        changed = json; changed["bufferViews"][0]["byteStride"] = 4; reject(glb(changed, binary));
        changed = json; changed["bufferViews"][0]["byteOffset"] = binary.size() + 4; reject(glb(changed, binary));
        changed = json; changed["buffers"][0]["uri"] = "external.bin"; reject(glb(changed, binary));
        changed = json; changed["materials"][0]["alphaMode"] = "BLEND"; reject(glb(changed, binary));
        changed = json; changed["accessors"][0]["count"] = -1; reject(glb(changed, binary));
        auto badBinary = binary;
        const auto indexOffset = json.at("bufferViews").at(1).at("byteOffset").get<std::size_t>();
        std::fill_n(badBinary.begin() + indexOffset, 4, 255); reject(glb(json, badBinary));
        badBinary = binary; badBinary[0] = 0; badBinary[1] = 0; badBinary[2] = 0xc0; badBinary[3] = 0x7f;
        reject(glb(json, badBinary)); // NaN: must also reject under -ffast-math.
        badBinary = binary;
        badBinary[pngOffset + 16] = 0; badBinary[pngOffset + 17] = 1;
        badBinary[pngOffset + 18] = 0; badBinary[pngOffset + 19] = 0;
        reject(glb(json, badBinary)); // Reject image size before allocation/decode.
        std::filesystem::remove(badPath);
        std::cout << "Moon GLB: 47 parts, " << loaded.vertices.size() << " vertices, " << loaded.indices.size() / 3
            << " triangles; mean albedo " << loaded.meanAlbedo << "; " << rejected
            << " invalid assets rejected; public/private decoder linkage and flip isolation passed\n";
        return 0;
    }
    catch (const std::exception& exception)
    { std::cerr << exception.what() << '\n'; return 1; }
}
