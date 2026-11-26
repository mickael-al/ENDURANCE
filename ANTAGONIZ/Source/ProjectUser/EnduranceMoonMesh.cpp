#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "EnduranceMoonMesh.hpp"
#include "hv/cpputil/json.hpp"
// Like tree loading, use a private PNG decoder: texture/skybox imports can
// change the engine decoder's process-global vertical-flip flag concurrently.
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#include "stb-cmake/stb_image.h"
#undef STBI_NO_STDIO
#undef STBI_ONLY_PNG
#undef STB_IMAGE_IMPLEMENTATION
#undef STB_IMAGE_STATIC
#include <glm/geometric.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>

namespace EnduranceMoon
{
namespace
{
using Json = nlohmann::json;
using Bytes = std::vector<std::uint8_t>;
constexpr std::size_t MaxFileBytes = 128u * 1024u * 1024u;
constexpr std::size_t MaxVertices = 1000000u, MaxIndices = 6000000u;
void require(bool condition, const char* message)
{ if (!condition) throw std::runtime_error(message); }
bool finite(float value)
{
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    return (bits & 0x7f800000u) != 0x7f800000u;
}
std::size_t sizeNumber(const Json& value, std::size_t maximum)
{
    require(value.is_number_integer() && value >= 0, "Expected nonnegative integer");
    const auto result = value.get<std::uint64_t>();
    require(result <= maximum, "Moon asset exceeds a size or index limit");
    return static_cast<std::size_t>(result);
}
const Json& item(const Json& list, const Json& index)
{
    require(list.is_array() && !list.empty(), "Expected a nonempty glTF array");
    return list.at(sizeNumber(index, list.size() - 1));
}
std::uint32_t read32(const std::uint8_t* p)
{ return std::uint32_t(p[0]) | (std::uint32_t(p[1]) << 8) | (std::uint32_t(p[2]) << 16) | (std::uint32_t(p[3]) << 24); }
float readFloat(const std::uint8_t* p)
{
    const auto bits = read32(p);
    float value = 0;
    std::memcpy(&value, &bits, sizeof(value));
    require(finite(value), "Nonfinite Moon vertex attribute");
    return value;
}
Bytes readFile(const std::string& path)
{
    std::ifstream stream(std::filesystem::u8path(path), std::ios::binary | std::ios::ate);
    require(bool(stream), "Cannot open baked Moon GLB");
    const auto length = stream.tellg();
    require(length >= 28 && length <= static_cast<std::streamoff>(MaxFileBytes), "Invalid or oversized Moon GLB");
    Bytes bytes(static_cast<std::size_t>(length));
    stream.seekg(0);
    stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    require(bool(stream), "Cannot read complete Moon GLB");
    return bytes;
}
struct Reader
{
    const Json& document;
    const std::uint8_t* binary;
    std::size_t size;
    struct View { const std::uint8_t* bytes; std::size_t size; const Json* json; };
    struct Access { const std::uint8_t* bytes; std::size_t count, stride; };
    View view(const Json& index) const
    {
        const auto& v = item(document.at("bufferViews"), index);
        require(v.at("buffer") == 0 && !v.contains("extensions"), "Unsupported Moon buffer view");
        const auto offset = sizeNumber(v.value("byteOffset", Json(0)), size);
        const auto length = sizeNumber(v.at("byteLength"), size - offset);
        return {binary + offset, length, &v};
    }
    Access access(const Json& index, const char* type, unsigned components,
                  unsigned componentType, std::size_t maximum) const
    {
        const auto& a = item(document.at("accessors"), index);
        require(a.at("type") == type && a.at("componentType") == componentType &&
            !a.contains("sparse") && !a.value("normalized", false), "Unsupported Moon accessor");
        const auto v = view(a.at("bufferView"));
        const auto offset = sizeNumber(a.value("byteOffset", Json(0)), v.size);
        const auto count = sizeNumber(a.at("count"), maximum);
        const auto element = std::size_t(components) * 4;
        const auto stride = sizeNumber(v.json->value("byteStride", Json(element)), 252);
        require(count > 0 && stride >= element && stride % 4 == 0 && offset % 4 == 0,
            "Invalid Moon attribute count, stride or alignment");
        require(element <= v.size - offset && count - 1 <= (v.size - offset - element) / stride,
            "Moon accessor outside binary buffer");
        return {v.bytes + offset, count, stride};
    }
    void appendMesh(const Json& mesh, Ge::MoonMeshData& output) const
    {
        require(!mesh.contains("weights"), "Animated Moon meshes are unsupported");
        const auto& primitives = mesh.at("primitives");
        require(primitives.is_array() && !primitives.empty() && primitives.size() <= 16, "Invalid Moon primitive count");
        for (const auto& primitive : primitives)
        {
            require(primitive.value("mode", 4) == 4 && primitive.at("material") == 0 &&
                !primitive.contains("targets") && !primitive.contains("extensions"), "Unsupported Moon primitive");
            const auto& attrs = primitive.at("attributes");
            const auto position = access(attrs.at("POSITION"), "VEC3", 3, 5126, MaxVertices);
            const auto normal = access(attrs.at("NORMAL"), "VEC3", 3, 5126, MaxVertices);
            const auto uv = access(attrs.at("TEXCOORD_0"), "VEC2", 2, 5126, MaxVertices);
            const auto color = access(attrs.at("COLOR_0"), "VEC4", 4, 5126, MaxVertices);
            const auto indices = access(primitive.at("indices"), "SCALAR", 1, 5125, MaxIndices);
            require(position.count == normal.count && position.count == uv.count && position.count == color.count &&
                indices.count % 3 == 0, "Inconsistent Moon vertex or triangle counts");
            require(position.count <= MaxVertices - output.vertices.size() && indices.count <= MaxIndices - output.indices.size(),
                "Combined Moon geometry exceeds runtime limits");
            const auto base = static_cast<std::uint32_t>(output.vertices.size());
            output.vertices.resize(output.vertices.size() + position.count);
            for (std::size_t i = 0; i < position.count; ++i)
            {
                auto& v = output.vertices[base + i];
                for (unsigned c = 0; c < 3; ++c)
                {
                    v.position[c] = readFloat(position.bytes + i * position.stride + c * 4);
                    v.normal[c] = readFloat(normal.bytes + i * normal.stride + c * 4);
                    require(std::abs(v.position[c]) <= 100.f, "Moon coordinates must retain radius-one units");
                }
                const float normalLength = glm::dot(v.normal, v.normal);
                require(normalLength > .98f && normalLength < 1.02f, "Moon normal is not normalized");
                v.normal /= std::sqrt(normalLength);
                for (unsigned c = 0; c < 2; ++c)
                {
                    v.uv[c] = readFloat(uv.bytes + i * uv.stride + c * 4);
                    require(std::abs(v.uv[c]) <= 10000.f, "Moon UV outside supported range");
                }
                for (unsigned c = 0; c < 4; ++c)
                {
                    v.color[c] = readFloat(color.bytes + i * color.stride + c * 4);
                    require(v.color[c] >= 0.f && v.color[c] <= 1.f, "Moon vertex color outside [0,1]");
                }
                require(v.color.a == 1.f, "Moon runtime requires opaque vertex colors");
            }
            output.indices.reserve(output.indices.size() + indices.count);
            for (std::size_t i = 0; i < indices.count; ++i)
            {
                const auto index = read32(indices.bytes + i * indices.stride);
                require(index < position.count, "Moon triangle index outside vertex buffer");
                output.indices.push_back(base + index);
            }
        }
    }
};
void decodeAtlas(const Reader& reader, Ge::MoonMeshData& output)
{
    const auto& j = reader.document;
    const auto& materials = j.at("materials");
    require(materials.is_array() && materials.size() == 1, "Moon requires one shared material");
    const auto& material = materials.at(0);
    require(material.value("alphaMode", "OPAQUE") == "OPAQUE" && !material.value("doubleSided", false) &&
        !material.contains("normalTexture") && !material.contains("emissiveTexture") && !material.contains("extensions"),
        "Unsupported baked Moon material");
    const auto& pbr = material.at("pbrMetallicRoughness");
    require(pbr.value("metallicFactor", 1.0) == 0.0 && !pbr.contains("baseColorFactor"), "Unsupported Moon base material");
    const auto& binding = pbr.at("baseColorTexture");
    require(binding.value("texCoord", 0) == 0 && !binding.contains("extensions"), "Unsupported Moon UV mapping");
    const auto& texture = item(j.at("textures"), binding.at("index"));
    require(!texture.contains("extensions"), "Unsupported Moon texture extension");
    const auto& sampler = item(j.at("samplers"), texture.at("sampler"));
    require(sampler.value("wrapS", 10497) == 10497 && sampler.value("wrapT", 10497) == 33071,
        "Moon atlas requires repeat U and clamped V");
    const auto& image = item(j.at("images"), texture.at("source"));
    require(image.at("mimeType") == "image/png" && !image.contains("uri"), "Expected embedded Moon PNG");
    const auto v = reader.view(image.at("bufferView"));
    require(v.size >= 8 && v.size <= MaxFileBytes, "Invalid Moon PNG length");
    int width = 0, height = 0, channels = 0;
    require(stbi_info_from_memory(v.bytes, static_cast<int>(v.size), &width, &height, &channels) != 0 &&
        width >= 1 && height >= 1 && width <= 4096 && height <= 4096, "Invalid or oversized Moon PNG dimensions");
    std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> decoded(stbi_load_from_memory(v.bytes,
        static_cast<int>(v.size), &width, &height, &channels, 4), stbi_image_free);
    require(bool(decoded), "Cannot decode embedded Moon PNG");
    output.width = static_cast<unsigned>(width); output.height = static_cast<unsigned>(height);
    output.albedoRgba.assign(decoded.get(), decoded.get() + std::size_t(width) * height * 4);
    for (std::size_t i = 3; i < output.albedoRgba.size(); i += 4)
        require(output.albedoRgba[i] == 255, "Moon runtime requires an opaque atlas");
}
float meanAlbedo(const Ge::MoonMeshData& mesh)
{
    std::array<float, 256> linear{};
    for (unsigned i = 0; i < linear.size(); ++i)
    {
        const float v = i / 255.f;
        linear[i] = v <= .04045f ? v / 12.92f : std::pow((v + .055f) / 1.055f, 2.4f);
    }
    std::vector<float> vertexAlbedo(mesh.vertices.size());
    for (std::size_t i = 0; i < mesh.vertices.size(); ++i)
    {
        const auto& v = mesh.vertices[i];
        const float u = v.uv.x - std::floor(v.uv.x), t = std::clamp(v.uv.y, 0.f, 1.f);
        const auto x = (std::min)(static_cast<unsigned>(u * mesh.width), mesh.width - 1);
        const auto y = (std::min)(static_cast<unsigned>(t * mesh.height), mesh.height - 1);
        const auto offset = (std::size_t(y) * mesh.width + x) * 4;
        vertexAlbedo[i] = glm::dot(glm::vec3(linear[mesh.albedoRgba[offset]],
            linear[mesh.albedoRgba[offset + 1]], linear[mesh.albedoRgba[offset + 2]]) * glm::vec3(v.color),
            glm::vec3(.2126f, .7152f, .0722f));
    }
    double weighted = 0, area = 0;
    for (std::size_t i = 0; i < mesh.indices.size(); i += 3)
    {
        const auto a = mesh.indices[i], b = mesh.indices[i + 1], c = mesh.indices[i + 2];
        const auto cross = glm::cross(mesh.vertices[b].position - mesh.vertices[a].position,
                                      mesh.vertices[c].position - mesh.vertices[a].position);
        const double weight = std::sqrt(double(glm::dot(cross, cross)));
        weighted += weight * (vertexAlbedo[a] + vertexAlbedo[b] + vertexAlbedo[c]) / 3.0;
        area += weight;
    }
    require(area > 0, "Moon mesh contains no nondegenerate surface");
    return static_cast<float>(std::clamp(weighted / area, 0.0, 1.0));
}
}

bool loadMoonMesh(const std::string& path, Ge::MoonMeshData& output, std::string& error)
{
    try
    {
        const auto file = readFile(path);
        require(read32(file.data()) == 0x46546c67 && read32(file.data() + 4) == 2 &&
            read32(file.data() + 8) == file.size(), "Invalid Moon GLB header");
        const auto jsonSize = std::size_t(read32(file.data() + 12));
        require(read32(file.data() + 16) == 0x4e4f534a && jsonSize <= 4u * 1024u * 1024u &&
            jsonSize % 4 == 0 && jsonSize <= file.size() - 28, "Invalid Moon JSON chunk");
        const auto j = Json::parse(file.begin() + 20, file.begin() + 20 + jsonSize);
        const auto binaryHeader = 20 + jsonSize, binaryStart = binaryHeader + 8;
        const auto binarySize = std::size_t(read32(file.data() + binaryHeader));
        require(read32(file.data() + binaryHeader + 4) == 0x004e4942 && binarySize % 4 == 0 &&
            binarySize == file.size() - binaryStart, "Invalid Moon binary chunk");
        require(j.at("asset").at("version") == "2.0" && j.value("extensionsRequired", Json::array()).empty() &&
            !j.contains("animations") && !j.contains("skins"), "Unsupported Moon glTF version or features");
        const auto& buffers = j.at("buffers");
        require(buffers.is_array() && buffers.size() == 1 && !buffers.at(0).contains("uri"), "Expected one embedded Moon buffer");
        const auto declared = sizeNumber(buffers.at(0).at("byteLength"), binarySize);
        require(binarySize - declared <= 3, "Moon binary buffer length mismatch");
        const Reader reader{j, file.data() + binaryStart, declared};
        Ge::MoonMeshData loaded;
        const auto sceneIndex = j.value("scene", Json(0));
        const auto& scene = item(j.at("scenes"), sceneIndex);
        const auto& nodes = scene.at("nodes");
        require(nodes.is_array() && !nodes.empty() && nodes.size() <= 1024, "Invalid Moon scene node count");
        for (const auto& nodeIndex : nodes)
        {
            const auto& node = item(j.at("nodes"), nodeIndex);
            require(!node.contains("children") && !node.contains("matrix") && !node.contains("translation") &&
                !node.contains("rotation") && !node.contains("scale") && !node.contains("skin") && !node.contains("extensions"),
                "Moon GLB must have baked node transforms; re-export from the Moon laboratory");
            reader.appendMesh(item(j.at("meshes"), node.at("mesh")), loaded);
        }
        decodeAtlas(reader, loaded);
        loaded.meanAlbedo = meanAlbedo(loaded);
        output = std::move(loaded);
        error.clear();
        return true;
    }
    catch (const std::exception& exception)
    {
        error = "Cannot load lunar mesh: " + std::string(exception.what()) + " (" + path + ")";
        return false;
    }
}
}
