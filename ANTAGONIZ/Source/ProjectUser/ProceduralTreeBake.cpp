#include "ProceduralTreeBake.hpp"
#include "hv/cpputil/json.hpp"
// A private decoder avoids the engine's process-global stb vertical-flip flag.
// Cache loading is allowed on the authoring worker while skyboxes load on GL.
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#include "stb-cmake/stb_image.h"
#undef STBI_NO_STDIO
#undef STBI_ONLY_PNG
#undef STB_IMAGE_IMPLEMENTATION
#undef STB_IMAGE_STATIC
#include "stb-cmake/stb_image_write.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <system_error>

namespace EnduranceTrees
{
namespace
{
using Json = nlohmann::json;
namespace fs = std::filesystem;
constexpr std::uint32_t AssetVersion = 2;
constexpr std::size_t MaxFileBytes = 256u * 1024u * 1024u;
constexpr std::size_t MaxVertices = 1000000u;
constexpr std::size_t MaxIndices = 6000000u;
constexpr std::size_t MaxBranches = 20000u;
constexpr std::size_t VertexBytes = 16u * sizeof(float);
constexpr const char* AssetTag = "ENDURANCE.TreeAsset";
constexpr const char* PresetTag = "ENDURANCE.TreePreset";

#define TREE_FLOAT_FIELDS(V) \
    V(height) V(trunkRadius) V(trunkBend) V(crownWidth) V(crownHeight) \
    V(branchStart) V(branchAngle) V(branchTaper) V(curvature) V(twigDensity) \
    V(leafSize) V(leafDensity) V(clumpSize) V(foliageNormalBlend)
#define TREE_INT_FIELDS(V) V(branchCount) V(branchingLevels)
#define TREE_OPTIONAL_FLOAT_FIELDS(V) V(rootLength) V(rootRadius) V(vineLength) V(vineRadius)
#define TREE_OPTIONAL_INT_FIELDS(V) V(rootCount) V(vineCount)
#define TREE_COLOR_FIELDS(V) V(barkLow) V(barkHigh) V(leafLow) V(leafHigh)

bool finite(float value)
{
    static_assert(sizeof(float) == sizeof(std::uint32_t), "IEEE32 floats required");
    std::uint32_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    return (bits & 0x7f800000u) != 0x7f800000u;
}
void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}
Json vectorJson(const glm::vec3& value) { return Json::array({value.x, value.y, value.z}); }
glm::vec3 readVector(const Json& value)
{
    require(value.is_array() && value.size() == 3, "Expected a three-component vector");
    glm::vec3 result;
    for (int i = 0; i < 3; ++i)
    {
        require(value[i].is_number(), "Vector components must be numbers");
        result[i] = value[i].get<float>();
        require(finite(result[i]), "Non-finite vector component");
    }
    return result;
}
std::int64_t integer(const Json& value, std::int64_t low, std::int64_t high)
{
    require(value.is_number_integer(), "Expected an integer");
    if (value.is_number_unsigned())
        require(value.get<std::uint64_t>() <= static_cast<std::uint64_t>(high), "Integer out of range");
    const auto v = value.get<std::int64_t>();
    require(v >= low && v <= high, "Integer out of range");
    return v;
}
Json encodeParams(const Params& params)
{
    const Params p = sanitize(params);
    Json j;
    j["seed"] = p.seed;
    j["family"] = static_cast<std::int32_t>(p.family);
#define ENCODE(name) j[#name] = p.name;
    TREE_FLOAT_FIELDS(ENCODE)
    TREE_INT_FIELDS(ENCODE)
    TREE_OPTIONAL_FLOAT_FIELDS(ENCODE)
    TREE_OPTIONAL_INT_FIELDS(ENCODE)
#undef ENCODE
#define ENCODE_COLOR(name) j[#name] = vectorJson(p.name);
    TREE_COLOR_FIELDS(ENCODE_COLOR)
#undef ENCODE_COLOR
    return j;
}
Params decodeParams(const Json& j)
{
    require(j.is_object(), "Expected tree parameters object");
    Params p;
    p.seed = static_cast<std::uint32_t>(integer(j.at("seed"), 0, 0xffffffffll));
    p.family = static_cast<Family>(integer(j.at("family"), 0, FamilyCount - 1));
#define DECODE_FLOAT(name) require(j.at(#name).is_number(), "Expected numeric parameter " #name); \
    p.name = j.at(#name).get<float>(); require(finite(p.name), "Non-finite parameter " #name);
    TREE_FLOAT_FIELDS(DECODE_FLOAT)
#undef DECODE_FLOAT
#define DECODE_INT(name) p.name = static_cast<std::int32_t>(integer(j.at(#name), 0, 100000));
    TREE_INT_FIELDS(DECODE_INT)
#undef DECODE_INT
#define DECODE_COLOR(name) p.name = readVector(j.at(#name));
    TREE_COLOR_FIELDS(DECODE_COLOR)
#undef DECODE_COLOR
    // Version 1 did not have roots or vines. Sanitize only their fallback
    // defaults against this tree's size, then validate all provided values.
    const Params defaults = sanitize(p);
#define DECODE_OPTIONAL_FLOAT(name) \
    if(j.contains(#name)) { require(j.at(#name).is_number(), "Expected numeric parameter " #name); \
        p.name=j.at(#name).get<float>(); require(finite(p.name), "Non-finite parameter " #name); } \
    else p.name=defaults.name;
    TREE_OPTIONAL_FLOAT_FIELDS(DECODE_OPTIONAL_FLOAT)
#undef DECODE_OPTIONAL_FLOAT
#define DECODE_OPTIONAL_INT(name) \
    if(j.contains(#name)) p.name=static_cast<std::int32_t>(integer(j.at(#name),0,100000));
    TREE_OPTIONAL_INT_FIELDS(DECODE_OPTIONAL_INT)
#undef DECODE_OPTIONAL_INT
    // Require legal ranges rather than silently replacing damaged settings.
    const Params checked = sanitize(p);
#define CHECK(name) require(checked.name == p.name, "Parameter outside generator limits: " #name);
    TREE_FLOAT_FIELDS(CHECK)
    TREE_INT_FIELDS(CHECK)
    TREE_OPTIONAL_FLOAT_FIELDS(CHECK)
    TREE_OPTIONAL_INT_FIELDS(CHECK)
#undef CHECK
#define CHECK_COLOR(name) require(glm::all(glm::equal(checked.name, p.name)), "Color outside generator limits: " #name);
    TREE_COLOR_FIELDS(CHECK_COLOR)
#undef CHECK_COLOR
    return p;
}
Json presetJson(const Params& p)
{
    return {{"format", PresetTag}, {"version", AssetVersion},
            {"generatorVersion", GeneratorVersion}, {"params", encodeParams(p)}};
}
std::string readText(const fs::path& path, std::size_t limit = 4u * 1024u * 1024u)
{
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    require(stream.good(), "Cannot open input file");
    const auto length = stream.tellg();
    require(length >= 0 && static_cast<std::uint64_t>(length) <= limit, "Input file is too large");
    std::string text(static_cast<std::size_t>(length), '\0');
    stream.seekg(0);
    if (!text.empty()) stream.read(&text[0], static_cast<std::streamsize>(text.size()));
    require(stream.good(), "Cannot read complete input file");
    return text;
}
void writeBytes(const fs::path& path, const void* data, std::size_t bytes)
{
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    require(stream.good(), "Cannot create output file");
    if (bytes) stream.write(static_cast<const char*>(data), static_cast<std::streamsize>(bytes));
    stream.flush();
    require(stream.good(), "Cannot write complete output file");
    stream.close();
    require(!stream.fail(), "Cannot close output file");
}
void writeJson(const fs::path& path, const Json& j)
{
    const std::string text = j.dump(2) + '\n';
    writeBytes(path, text.data(), text.size());
}
struct TemporaryPath
{
    fs::path path;
    ~TemporaryPath() { if (!path.empty()) { std::error_code ec; fs::remove_all(path, ec); } }
};
fs::path reserveDirectory(const fs::path& parent, const std::string& stem)
{
    static std::atomic<std::uint64_t> serial{0};
    for (unsigned attempt = 0; attempt < 32; ++attempt)
    {
        const auto clock = std::chrono::steady_clock::now().time_since_epoch().count();
        const fs::path candidate = parent / (stem + ".building-" + std::to_string(clock) + "-" + std::to_string(serial++));
        std::error_code ec;
        if (fs::create_directory(candidate, ec)) return candidate;
        if (ec) throw fs::filesystem_error("Cannot create export staging directory", candidate, ec);
    }
    throw std::runtime_error("Cannot reserve an export staging directory");
}
std::uint8_t linearToSrgb(std::uint8_t value)
{
    const float x = value / 255.0f;
    const float s = x <= 0.0031308f ? x * 12.92f : 1.055f * std::pow(x, 1.0f / 2.4f) - 0.055f;
    return static_cast<std::uint8_t>(std::clamp(std::lround(s * 255.0f), 0l, 255l));
}
std::uint8_t srgbToLinear(std::uint8_t value)
{
    const float x = value / 255.0f;
    const float s = x <= 0.04045f ? x / 12.92f : std::pow((x + 0.055f) / 1.055f, 2.4f);
    return static_cast<std::uint8_t>(std::clamp(std::lround(s * 255.0f), 0l, 255l));
}
void pngWrite(void* context, void* data, int bytes)
{
    auto& stream = *static_cast<std::ofstream*>(context);
    stream.write(static_cast<const char*>(data), bytes);
}
void writeImage(const fs::path& path, const Image& image, bool encodeSrgb = true)
{
    require(validImage(image) && image.width <= 8192 && image.height <= 8192, "Invalid export image");
    auto pixels = image.rgba;
    if (encodeSrgb)
        for (std::size_t i = 0; i < pixels.size(); i += 4)
            for (int channel = 0; channel < 3; ++channel) pixels[i + channel] = linearToSrgb(pixels[i + channel]);
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    require(stream.good(), "Cannot create PNG");
    // No global STB flip settings: several tree exports may run concurrently.
    const int success = stbi_write_png_to_func(pngWrite, &stream, image.width, image.height, 4, pixels.data(), image.width * 4);
    stream.flush();
    require(success != 0 && stream.good(), "Cannot write PNG");
    stream.close();
    require(!stream.fail(), "Cannot close PNG");
}
Image readImage(const fs::path& path, int maxDimension = 8192, bool decodeSrgb = true,
                std::size_t maxFileBytes = 64u * 1024u * 1024u)
{
    const std::string content = readText(path, maxFileBytes);
    int width = 0, height = 0, channels = 0;
    require(stbi_info_from_memory(reinterpret_cast<const stbi_uc*>(content.data()), static_cast<int>(content.size()),
                                  &width, &height, &channels) != 0,
            "Invalid PNG image");
    require(width > 0 && height > 0 && width <= maxDimension && height <= maxDimension, "PNG dimensions out of bounds");
    stbi_uc* raw = stbi_load_from_memory(reinterpret_cast<const stbi_uc*>(content.data()), static_cast<int>(content.size()),
                                        &width, &height, &channels, 4);
    require(raw != nullptr, "Cannot decode PNG");
    Image result;
    result.width = width; result.height = height;
    try { result.rgba.assign(raw, raw + static_cast<std::size_t>(width) * height * 4); }
    catch (...) { stbi_image_free(raw); throw; }
    stbi_image_free(raw);
    if (decodeSrgb)
        for (std::size_t i = 0; i < result.rgba.size(); i += 4)
            for (int c = 0; c < 3; ++c) result.rgba[i + c] = srgbToLinear(result.rgba[i + c]);
    return result;
}
void appendWord(std::vector<std::uint8_t>& bytes, std::uint32_t value)
{
    for (int i = 0; i < 4; ++i) bytes.push_back(static_cast<std::uint8_t>(value >> (8 * i)));
}
void appendFloat(std::vector<std::uint8_t>& bytes, float value)
{
    require(finite(value), "Non-finite exported vertex");
    std::uint32_t bits; std::memcpy(&bits, &value, sizeof(bits)); appendWord(bytes, bits);
}
std::uint32_t readWord(const std::vector<std::uint8_t>& bytes, std::size_t offset)
{
    require(offset <= bytes.size() && bytes.size() - offset >= 4, "Truncated mesh buffer");
    return std::uint32_t(bytes[offset]) | (std::uint32_t(bytes[offset+1]) << 8)
        | (std::uint32_t(bytes[offset+2]) << 16) | (std::uint32_t(bytes[offset+3]) << 24);
}
float readFloat(const std::vector<std::uint8_t>& bytes, std::size_t offset)
{
    const std::uint32_t bits = readWord(bytes, offset);
    float value; std::memcpy(&value, &bits, sizeof(value));
    require(finite(value), "Non-finite cached vertex"); return value;
}
void validateMesh(const Mesh& mesh)
{
    require(mesh.vertices.size() <= MaxVertices && mesh.indices.size() <= MaxIndices, "Mesh exceeds export budget");
    require(mesh.indices.size() % 3 == 0, "Incomplete triangle");
    require(mesh.vertices.empty() == mesh.indices.empty(), "Mesh vertices and triangles are inconsistent");
    for (const auto& v : mesh.vertices)
    {
        for (int c = 0; c < 3; ++c)
            require(finite(v.position[c]) && finite(v.normal[c]), "Invalid vertex position or normal");
        const float normalLength = glm::dot(v.normal, v.normal);
        require(normalLength > 0.25f && normalLength < 2.25f, "Invalid vertex normal length");
        for (int c = 0; c < 2; ++c) require(finite(v.uv[c]), "Invalid vertex UV");
        for (int c = 0; c < 4; ++c)
        {
            require(finite(v.color[c]) && v.color[c] >= 0.0f && v.color[c] <= 1.0f, "Invalid vertex color");
            require(finite(v.wind[c]), "Invalid wind attribute");
        }
    }
    for (const auto index : mesh.indices) require(index < mesh.vertices.size(), "Triangle index out of bounds");
}
struct GltfWriter
{
    Json gltf;
    std::vector<std::uint8_t> bytes;
    GltfWriter()
    {
        gltf = {{"asset", {{"version", "2.0"}, {"generator", "ENDURANCE Tree Lab"}}},
                {"bufferViews", Json::array()}, {"accessors", Json::array()},
                {"meshes", Json::array()}, {"nodes", Json::array()}, {"scenes", Json::array()}, {"scene", 0}};
    }
    int view(std::size_t offset, std::size_t size, int target, int stride = 0)
    {
        Json result = {{"buffer", 0}, {"byteOffset", offset}, {"byteLength", size}, {"target", target}};
        if (stride) result["byteStride"] = stride;
        const int index = static_cast<int>(gltf["bufferViews"].size()); gltf["bufferViews"].push_back(result); return index;
    }
    int accessor(int bufferView, std::size_t count, const char* type, int componentType, int byteOffset = 0)
    {
        const int index = static_cast<int>(gltf["accessors"].size());
        gltf["accessors"].push_back({{"bufferView", bufferView}, {"byteOffset", byteOffset},
            {"componentType", componentType}, {"count", count}, {"type", type}}); return index;
    }
    Json mesh(const Mesh& mesh, int material)
    {
        validateMesh(mesh);
        require(!mesh.vertices.empty(), "Cannot export an empty primitive");
        const std::size_t offset = bytes.size();
        glm::vec3 minimum = mesh.vertices.front().position, maximum = minimum;
        for (const auto& v : mesh.vertices)
        {
            minimum = glm::min(minimum, v.position); maximum = glm::max(maximum, v.position);
            for (int i = 0; i < 3; ++i) appendFloat(bytes, v.position[i]);
            for (int i = 0; i < 3; ++i) appendFloat(bytes, v.normal[i]);
            for (int i = 0; i < 2; ++i) appendFloat(bytes, v.uv[i]);
            for (int i = 0; i < 4; ++i) appendFloat(bytes, v.color[i]);
            for (int i = 0; i < 4; ++i) appendFloat(bytes, v.wind[i]);
        }
        const int vertexView = view(offset, bytes.size() - offset, 34962, static_cast<int>(VertexBytes));
        const int position = accessor(vertexView, mesh.vertices.size(), "VEC3", 5126);
        gltf["accessors"][position]["min"] = vectorJson(minimum);
        gltf["accessors"][position]["max"] = vectorJson(maximum);
        const int normal = accessor(vertexView, mesh.vertices.size(), "VEC3", 5126, 12);
        const int uv = accessor(vertexView, mesh.vertices.size(), "VEC2", 5126, 24);
        const int color = accessor(vertexView, mesh.vertices.size(), "VEC4", 5126, 32);
        const int wind = accessor(vertexView, mesh.vertices.size(), "VEC4", 5126, 48);
        const std::size_t indexOffset = bytes.size();
        for (const auto index : mesh.indices) appendWord(bytes, index);
        const int indexView = view(indexOffset, bytes.size() - indexOffset, 34963);
        const int indices = accessor(indexView, mesh.indices.size(), "SCALAR", 5125);
        return {{"attributes", {{"POSITION", position}, {"NORMAL", normal}, {"TEXCOORD_0", uv},
                                  {"COLOR_0", color}, {"_WIND", wind}}},
                {"indices", indices}, {"mode", 4}, {"material", material}};
    }
};

Json encodeBranches(const Tree& tree)
{
    require(tree.branches.size() <= MaxBranches, "Skeleton exceeds export budget");
    Json branches = Json::array();
    for (std::size_t index = 0; index < tree.branches.size(); ++index)
    {
        const Branch& branch = tree.branches[index];
        require(branch.parent >= -1 && branch.parent < static_cast<std::int32_t>(index), "Invalid branch hierarchy");
        require(branch.level >= 0 && branch.level <= 32, "Invalid branch level");
        require(static_cast<int>(branch.kind) >= 0 && static_cast<int>(branch.kind) <= 2, "Invalid branch kind");
        require(finite(branch.attachment) && branch.attachment >= 0 && branch.attachment <= 1, "Invalid attachment");
        require(branch.points.size() >= 2 && branch.points.size() <= 4096, "Invalid branch polyline");
        Json points = Json::array();
        for (const auto& point : branch.points)
        {
            for (int c = 0; c < 3; ++c) require(finite(point.position[c]), "Invalid branch position");
            require(finite(point.radius) && point.radius > 0 && point.radius < 1000, "Invalid branch radius");
            points.push_back(Json::array({point.position.x, point.position.y, point.position.z, point.radius}));
        }
        branches.push_back({{"parent", branch.parent}, {"level", branch.level}, {"kind", static_cast<int>(branch.kind)},
                             {"attachment", branch.attachment}, {"points", points}});
    }
    return branches;
}
Json collisionCapsules(const Tree& tree)
{
    Json capsules = Json::array();
    for (const auto& branch : tree.branches)
    {
        if (branch.level > 1 || branch.kind == BranchKind::Vine) continue;
        // Coarse segments for later runtime collision import; foliage is excluded.
        for (std::size_t p = 1; p < branch.points.size(); p += 3)
        {
            const auto& a = branch.points[p-1];
            const auto& b = branch.points[(std::min)(p+2, branch.points.size()-1)];
            if (a.radius < 0.08f) continue;
            capsules.push_back({{"a", vectorJson(a.position)}, {"b", vectorJson(b.position)},
                                {"radius", (std::max)(a.radius, b.radius)}});
        }
    }
    return capsules;
}
void validateLod3(const std::vector<ImpostorView>& views)
{
    require(views.size() == 2, "LOD3 requires exactly two views (0 and 45 degrees)");
    for(std::size_t i=0;i<views.size();++i)
    {
        const auto& v=views[i];
        require(validImage(v.albedo) && v.albedo.width <= 2048 && v.albedo.height == v.albedo.width,
                "Invalid square LOD3 color image");
        require(finite(v.halfExtent) && v.halfExtent > 0.0f, "Invalid LOD3 extent");
        for(int c=0;c<3;++c)
            require(finite(v.direction[c]) && finite(v.up[c]) && finite(v.centre[c]), "Invalid LOD3 camera");
        const float directionLength=glm::dot(v.direction,v.direction),upLength=glm::dot(v.up,v.up);
        require(directionLength > 0.99f && directionLength < 1.01f && upLength > 0.99f && upLength < 1.01f
                && std::abs(glm::dot(v.direction,v.up)) < 0.999f, "Invalid LOD3 camera basis");
        const float expectedY=i == 0 ? 0.0f : 0.7071067812f;
        require(std::abs(v.direction.y-expectedY) < 0.005f, "LOD3 view elevations must be 0 and 45 degrees");
    }
    const auto& a=views[0]; const auto& b=views[1];
    require(a.albedo.width == b.albedo.width && a.albedo.height == b.albedo.height, "LOD3 image sizes differ");
    require(glm::length(a.centre-b.centre) < 0.001f
            && std::abs(a.halfExtent-b.halfExtent) <= (std::max)(0.001f,a.halfExtent*0.0001f),
            "LOD3 views must share centre and extent");
    const glm::vec2 h0=glm::normalize(glm::vec2(a.direction.x,a.direction.z));
    const glm::vec2 h1=glm::normalize(glm::vec2(b.direction.x,b.direction.z));
    require(glm::dot(h0,h1) > 0.999f, "LOD3 views must share azimuth");
}
void buildAsset(const Tree& tree, const fs::path& directory, const std::vector<ImpostorView>* impostors,
                const std::vector<ImpostorView>* lod3)
{
    require(!tree.cancelled, "Cannot bake a cancelled generation");
    require(!tree.lods[0].wood.vertices.empty() && !tree.lods[1].wood.vertices.empty(), "Tree has no wood geometry");
    const Json branches = encodeBranches(tree);
    for (int c = 0; c < 3; ++c)
        require(finite(tree.boundsMin[c]) && finite(tree.boundsMax[c]) && tree.boundsMin[c] <= tree.boundsMax[c], "Invalid tree bounds");
    GltfWriter writer;
    for (int lod = 0; lod < 2; ++lod)
    {
        Json primitives = Json::array();
        primitives.push_back(writer.mesh(tree.lods[lod].wood, 0));
        if (!tree.lods[lod].foliage.vertices.empty()) primitives.push_back(writer.mesh(tree.lods[lod].foliage, 1));
        writer.gltf["meshes"].push_back({{"name", "Tree_LOD" + std::to_string(lod)}, {"primitives", primitives}});
        writer.gltf["nodes"].push_back({{"name", "Tree_LOD" + std::to_string(lod)}, {"mesh", lod}, {"extras", {{"lod", lod}}}});
        writer.gltf["scenes"].push_back({{"name", "LOD" + std::to_string(lod)}, {"nodes", Json::array({lod})}});
    }
    require(writer.bytes.size() <= MaxFileBytes, "Tree geometry exceeds file budget");
    writer.gltf["buffers"] = Json::array({{{"uri", "tree.bin"}, {"byteLength", writer.bytes.size()}}});
    writer.gltf["images"] = Json::array({{{"uri", "bark.png"}}, {{"uri", "leaves.png"}}});
    writer.gltf["samplers"] = Json::array({
        {{"magFilter", 9729}, {"minFilter", 9987}, {"wrapS", 10497}, {"wrapT", 10497}},
        {{"magFilter", 9729}, {"minFilter", 9987}, {"wrapS", 33071}, {"wrapT", 33071}}});
    writer.gltf["textures"] = Json::array({{{"source", 0}, {"sampler", 0}}, {{"source", 1}, {"sampler", 1}}});
    writer.gltf["materials"] = Json::array({
        {{"name", "Bark"}, {"pbrMetallicRoughness", {{"baseColorTexture", {{"index", 0}}},
            {"metallicFactor", 0.0}, {"roughnessFactor", 0.9}}}},
        {{"name", "Foliage"}, {"pbrMetallicRoughness", {{"baseColorTexture", {{"index", 1}}},
            {"metallicFactor", 0.0}, {"roughnessFactor", 0.82}}},
            {"alphaMode", "MASK"}, {"alphaCutoff", 0.5}, {"doubleSided", true}}});
    writer.gltf["extras"] = {{"format", AssetTag}, {"version", AssetVersion},
        {"windAttributes", "_WIND: normalized height, flexibility, phase, palette weight"},
        {"lodSelection", "Two independent scenes. Default scene is LOD0; select LOD1 explicitly."}};
    writeBytes(directory / "tree.bin", writer.bytes.data(), writer.bytes.size());
    writeJson(directory / "tree.gltf", writer.gltf);
    writeJson(directory / "preset.json", presetJson(tree.params));
    writeImage(directory / "bark.png", tree.barkTexture);
    writeImage(directory / "leaves.png", tree.leafMask);
    const auto mips = coverageMipmaps(tree.leafMask, 0.5f);
    Json mipFiles = Json::array();
    for (std::size_t level = 0; level < mips.size(); ++level)
    {
        const std::string name = level == 0 ? "leaves.png" : "leaves_mip" + std::to_string(level) + ".png";
        if (level) writeImage(directory / name, mips[level]);
        mipFiles.push_back({{"uri", name}, {"width", mips[level].width}, {"height", mips[level].height}});
    }
    Json manifest = {{"format", AssetTag}, {"version", AssetVersion}, {"generatorVersion", GeneratorVersion},
        {"params", encodeParams(tree.params)}, {"boundsMin", vectorJson(tree.boundsMin)}, {"boundsMax", vectorJson(tree.boundsMax)},
        {"branches", branches}, {"collisionCapsules", collisionCapsules(tree)},
        {"leafMipmaps", mipFiles}, {"alphaCutoff", 0.5}, {"textureColorSpace", "sRGB; alpha linear"},
        {"meshColorSpace", "linear"}, {"wind", "Vertex attribute _WIND: height, flexibility, phase, palette"},
        {"statistics", {{"branches", tree.statistics.branches}, {"leafCards", tree.statistics.leafCards},
             {"roots", tree.statistics.roots}, {"vines", tree.statistics.vines},
             {"triangles", tree.statistics.triangles}, {"budgetLimited", tree.statistics.budgetLimited}}}};
    manifest["impostors"] = Json::array();
    if (impostors)
    {
        require(impostors->size() <= 64, "Too many impostor views");
        for (std::size_t i = 0; i < impostors->size(); ++i)
        {
            const auto& view = (*impostors)[i];
            require(finite(view.halfExtent) && view.halfExtent > 0.0f, "Invalid impostor extent");
            for (int c=0; c<3; ++c)
                require(finite(view.direction[c]) && finite(view.up[c]) && finite(view.centre[c]), "Invalid impostor camera");
            const float viewLength = glm::dot(view.direction, view.direction);
            const float upLength = glm::dot(view.up, view.up);
            require(viewLength > 0.99f && viewLength < 1.01f && upLength > 0.99f && upLength < 1.01f
                    && std::abs(glm::dot(view.direction, view.up)) < 0.999f, "Invalid impostor camera basis");
            require(view.albedo.width == view.normal.width && view.albedo.height == view.normal.height,
                    "Impostor image dimensions differ");
            const std::string base = "impostor_" + std::to_string(i);
            writeImage(directory / (base + "_albedo.png"), view.albedo, false);
            writeImage(directory / (base + "_normal.png"), view.normal, false);
            manifest["impostors"].push_back({{"albedo", base + "_albedo.png"}, {"normal", base + "_normal.png"},
                {"direction", vectorJson(view.direction)}, {"up", vectorJson(view.up)}, {"centre", vectorJson(view.centre)},
                {"cameraConvention", "Camera = centre + direction * distance; lookAt up vector; orthographic square"},
                {"halfExtent", view.halfExtent}, {"width", view.albedo.width}, {"height", view.albedo.height},
                {"albedoEncoding", "sRGB, straight alpha"}, {"normalEncoding", "object space, xyz * 0.5 + 0.5, linear"}});
        }
    }
    if(lod3)
    {
        validateLod3(*lod3);
        Json views=Json::array();
        for(std::size_t i=0;i<lod3->size();++i)
        {
            const auto& v=(*lod3)[i];
            const std::string file=i == 0 ? "lod3_0.png" : "lod3_45.png";
            // Capture output already contains sRGB color; never gamma-encode twice.
            writeImage(directory/file,v.albedo,false);
            views.push_back({{"uri",file},{"elevationDegrees",i == 0 ? 0 : 45},
                {"direction",vectorJson(v.direction)},{"up",vectorJson(v.up)},
                {"centre",vectorJson(v.centre)},{"halfExtent",v.halfExtent},
                {"width",v.albedo.width},{"height",v.albedo.height}});
        }
        manifest["lod3"]={{"type","twoViewBillboard"},{"views",views},
            {"colorEncoding","sRGB, straight alpha"},{"lighting","neutral baked lighting"},
            {"alphaCutoff",0.5},{"triangles",2},
            {"cameraConvention","Elevation from horizon; same azimuth; camera = centre + direction * distance; orthographic square"}};
    }
    writeJson(directory / "manifest.json", manifest);
}
std::size_t sizeNumber(const Json& j, std::size_t limit)
{
    return static_cast<std::size_t>(integer(j, 0, static_cast<std::int64_t>(limit)));
}
struct Reader
{
    const Json& gltf;
    const std::vector<std::uint8_t>& bytes;
    struct Access { std::size_t offset=0, count=0, stride=0; };
    Access access(int index, const char* type, int components, int componentType, std::size_t maximum) const
    {
        const auto& accessors = gltf.at("accessors");
        require(accessors.is_array() && index >= 0 && static_cast<std::size_t>(index) < accessors.size(), "Invalid accessor index");
        const auto& a = accessors[index];
        require(a.at("type") == type && a.at("componentType") == componentType && !a.contains("sparse"), "Unsupported cached accessor");
        require(!a.value("normalized", false), "Unexpected normalized accessor");
        const auto& views = gltf.at("bufferViews");
        const std::size_t viewIndex = sizeNumber(a.at("bufferView"), 1000);
        require(views.is_array() && viewIndex < views.size(), "Invalid buffer view");
        const auto& view = views[viewIndex];
        require(view.at("buffer") == 0, "Unexpected buffer index");
        const std::size_t start = sizeNumber(view.value("byteOffset", Json(0)), bytes.size());
        const std::size_t length = sizeNumber(view.at("byteLength"), bytes.size());
        const std::size_t local = sizeNumber(a.value("byteOffset", Json(0)), length);
        const std::size_t count = sizeNumber(a.at("count"), maximum);
        const std::size_t element = static_cast<std::size_t>(components) * 4;
        const std::size_t stride = sizeNumber(view.value("byteStride", Json(element)), 252);
        require(count > 0 && stride >= element && stride % 4 == 0 && start % 4 == 0 && local % 4 == 0,
                "Invalid cached buffer alignment or stride");
        require(start <= bytes.size() && length <= bytes.size() - start && local <= length, "Buffer view out of bounds");
        require(element <= length - local && count - 1 <= (length - local - element) / stride, "Accessor out of bounds");
        return {start + local, count, stride};
    }
    Mesh mesh(const Json& primitive) const
    {
        require(primitive.value("mode", 4) == 4, "Only triangles are supported");
        const auto& attrs = primitive.at("attributes");
        const Access pos = access(static_cast<int>(integer(attrs.at("POSITION"), 0, 1000)), "VEC3", 3, 5126, MaxVertices);
        const Access norm = access(static_cast<int>(integer(attrs.at("NORMAL"), 0, 1000)), "VEC3", 3, 5126, MaxVertices);
        const Access uv = access(static_cast<int>(integer(attrs.at("TEXCOORD_0"), 0, 1000)), "VEC2", 2, 5126, MaxVertices);
        const Access color = access(static_cast<int>(integer(attrs.at("COLOR_0"), 0, 1000)), "VEC4", 4, 5126, MaxVertices);
        const Access wind = access(static_cast<int>(integer(attrs.at("_WIND"), 0, 1000)), "VEC4", 4, 5126, MaxVertices);
        const Access indices = access(static_cast<int>(integer(primitive.at("indices"), 0, 1000)), "SCALAR", 1, 5125, MaxIndices);
        require(pos.count == norm.count && pos.count == uv.count && pos.count == color.count && pos.count == wind.count,
                "Inconsistent vertex attribute counts");
        Mesh result; result.vertices.resize(pos.count); result.indices.resize(indices.count);
        for (std::size_t i = 0; i < pos.count; ++i)
        {
            auto& v = result.vertices[i];
            for (int c=0; c<3; ++c) { v.position[c]=readFloat(bytes,pos.offset+i*pos.stride+c*4); v.normal[c]=readFloat(bytes,norm.offset+i*norm.stride+c*4); }
            for (int c=0; c<2; ++c) v.uv[c]=readFloat(bytes,uv.offset+i*uv.stride+c*4);
            for (int c=0; c<4; ++c) { v.color[c]=readFloat(bytes,color.offset+i*color.stride+c*4); v.wind[c]=readFloat(bytes,wind.offset+i*wind.stride+c*4); }
        }
        for (std::size_t i=0; i<indices.count; ++i) result.indices[i]=readWord(bytes,indices.offset+i*indices.stride);
        validateMesh(result); return result;
    }
};
}

std::string paramsToJson(const Params& params) { return presetJson(params).dump(2); }
bool paramsFromJson(const std::string& text, Params& params, std::string& error)
{
    try
    {
        require(text.size() <= 4u*1024u*1024u, "Preset exceeds size limit");
        const Json j = Json::parse(text);
        require(j.at("format") == PresetTag, "Not an ENDURANCE tree preset");
        integer(j.at("version"),1,AssetVersion);
        integer(j.at("generatorVersion"),1,GeneratorVersion);
        Params result = decodeParams(j.at("params"));
        params = result; error.clear(); return true;
    }
    catch (const std::exception& e) { error = e.what(); return false; }
}
bool savePreset(const fs::path& path, const Params& params, std::string& error)
{
    try
    {
        require(!path.empty() && !path.filename().empty(), "Choose a preset filename");
        const fs::path parent = path.has_parent_path() ? path.parent_path() : fs::path(".");
        fs::create_directories(parent);
        TemporaryPath staging{reserveDirectory(parent, path.filename().string())};
        const fs::path replacement = staging.path / "preset.json";
        writeJson(replacement, presetJson(params));
        // Windows rename cannot replace an existing file. Keep the previous file
        // inside our temporary directory until replacement has succeeded.
        const bool exists = fs::exists(path);
        require(!exists || fs::is_regular_file(path), "Preset target is not a regular file");
        const fs::path backup = staging.path / "previous.json";
        if (exists) fs::rename(path, backup);
        try { fs::rename(replacement, path); }
        catch (...) { if (exists) { std::error_code ec; fs::rename(backup, path, ec); if (ec) staging.path.clear(); } throw; }
        error.clear(); return true;
    }
    catch (const std::exception& e) { error=e.what(); return false; }
}
bool loadPreset(const fs::path& path, Params& params, std::string& error)
{
    try { return paramsFromJson(readText(path), params, error); }
    catch (const std::exception& e) { error=e.what(); return false; }
}
bool loadLeafMask(const fs::path& path, Image& image, std::string& error)
{
    try
    {
        Image result = readImage(path, 2048, false, 32u * 1024u * 1024u);
        image = std::move(result); error.clear(); return true;
    }
    catch (const std::exception& e) { error=e.what(); return false; }
}
bool bake(const Tree& tree, const fs::path& directory, std::string& error,
          const std::vector<ImpostorView>* impostors, const std::vector<ImpostorView>* lod3)
{
    try
    {
        require(!directory.empty() && !directory.filename().empty() && directory.filename() != "." && directory.filename() != "..",
                "Choose a named export directory");
        require(!fs::exists(directory), "Export already exists; choose a new name or seed");
        const fs::path parent = directory.has_parent_path() ? directory.parent_path() : fs::path(".");
        fs::create_directories(parent);
        TemporaryPath staging{reserveDirectory(parent, directory.filename().string())};
        buildAsset(tree, staging.path, impostors, lod3);
        require(!fs::exists(directory), "Export directory appeared during bake; it was not replaced");
        fs::rename(staging.path, directory);
        staging.path.clear(); error.clear(); return true;
    }
    catch (const std::exception& e) { error=e.what(); return false; }
}
bool loadBakedTree(const fs::path& directory, Tree& tree, std::string& error, std::vector<ImpostorView>* lod3)
{
    try
    {
        const Json manifest = Json::parse(readText(directory / "manifest.json", 32u*1024u*1024u));
        require(manifest.at("format") == AssetTag, "Not an ENDURANCE baked tree");
        integer(manifest.at("version"),1,AssetVersion);
        integer(manifest.at("generatorVersion"),1,GeneratorVersion);
        Tree result;
        result.params = decodeParams(manifest.at("params"));
        result.boundsMin = readVector(manifest.at("boundsMin")); result.boundsMax = readVector(manifest.at("boundsMax"));
        require(glm::all(glm::lessThanEqual(result.boundsMin,result.boundsMax)), "Invalid baked tree bounds");
        const Json gltf = Json::parse(readText(directory / "tree.gltf"));
        require(gltf.at("asset").at("version") == "2.0" && gltf.at("extras").at("format") == AssetTag,
                "Not an ENDURANCE generated mesh");
        integer(gltf.at("extras").at("version"),1,AssetVersion);
        require(gltf.at("buffers").size() == 1 && gltf.at("buffers")[0].at("uri") == "tree.bin", "Unsupported mesh buffer path");
        const std::string buffer = readText(directory / "tree.bin", MaxFileBytes);
        require(sizeNumber(gltf.at("buffers")[0].at("byteLength"), MaxFileBytes) == buffer.size(), "Mesh buffer length mismatch");
        const std::vector<std::uint8_t> bytes(buffer.begin(),buffer.end());
        const Reader reader{gltf,bytes};
        const auto& meshes = gltf.at("meshes"); require(meshes.is_array() && meshes.size()==2, "Expected two mesh LODs");
        for (int lod=0; lod<2; ++lod)
        {
            const auto& primitives=meshes[lod].at("primitives");
            require(primitives.is_array() && primitives.size()>=1 && primitives.size()<=2, "Invalid mesh primitives");
            require(primitives[0].at("material")==0, "Expected wood primitive");
            result.lods[lod].wood=reader.mesh(primitives[0]);
            if (primitives.size()==2) { require(primitives[1].at("material")==1,"Expected foliage primitive"); result.lods[lod].foliage=reader.mesh(primitives[1]); }
        }
        const auto& branches=manifest.at("branches");
        require(branches.is_array() && branches.size()<=MaxBranches, "Invalid skeleton size");
        result.branches.reserve(branches.size());
        std::size_t totalPoints=0;
        for (std::size_t i=0;i<branches.size();++i)
        {
            const auto& b=branches[i]; Branch branch;
            branch.parent=static_cast<std::int32_t>(integer(b.at("parent"),-1,static_cast<std::int64_t>(i)-1));
            branch.level=static_cast<std::int32_t>(integer(b.at("level"),0,32));
            branch.kind=static_cast<BranchKind>(integer(b.value("kind",Json(0)),0,2));
            branch.attachment=b.at("attachment").get<float>();
            require(finite(branch.attachment) && branch.attachment>=0 && branch.attachment<=1, "Invalid branch attachment");
            const auto& points=b.at("points");
            require(points.is_array() && points.size()>=2 && points.size()<=4096, "Invalid branch points");
            totalPoints+=points.size(); require(totalPoints<=1000000, "Skeleton exceeds point budget");
            for (const auto& p:points)
            {
                require(p.is_array() && p.size()==4, "Invalid branch point");
                BranchPoint point;
                for (int c=0;c<3;++c) { point.position[c]=p[c].get<float>(); require(finite(point.position[c]),"Invalid branch position"); }
                point.radius=p[3].get<float>(); require(finite(point.radius) && point.radius>0 && point.radius<1000,"Invalid branch radius");
                branch.points.push_back(point);
            }
            result.branches.push_back(std::move(branch));
        }
        result.leafMask=readImage(directory / "leaves.png"); result.barkTexture=readImage(directory / "bark.png");
        result.statistics.branches=static_cast<std::uint32_t>(result.branches.size());
        for(const auto& b:result.branches)
        {
            if(b.kind == BranchKind::Root) ++result.statistics.roots;
            if(b.kind == BranchKind::Vine) ++result.statistics.vines;
        }
        result.statistics.leafCards=static_cast<std::uint32_t>(integer(manifest.at("statistics").at("leafCards"),0,MaxVertices));
        result.statistics.budgetLimited=manifest.at("statistics").at("budgetLimited").get<bool>();
        for (int lod=0;lod<2;++lod)
            result.statistics.triangles[lod]=static_cast<std::uint32_t>((result.lods[lod].wood.indices.size()+result.lods[lod].foliage.indices.size())/3);
        // Bounds are used for preview framing/culling: do not trust edited metadata.
        for (const auto& lod:result.lods) for (const Mesh* mesh:{&lod.wood,&lod.foliage}) for (const auto& v:mesh->vertices)
            require(glm::all(glm::greaterThanEqual(v.position,result.boundsMin-glm::vec3(0.001f)))
                    && glm::all(glm::lessThanEqual(v.position,result.boundsMax+glm::vec3(0.001f))), "Mesh lies outside cached bounds");
        std::vector<ImpostorView> billboard;
        if(lod3 && manifest.contains("lod3"))
        {
            const auto& description=manifest.at("lod3");
            require(description.at("type") == "twoViewBillboard", "Unsupported LOD3 representation");
            const auto& views=description.at("views");
            require(views.is_array() && views.size() == 2, "Expected two cached LOD3 views");
            for(std::size_t i=0;i<views.size();++i)
            {
                const auto& j=views[i]; ImpostorView v;
                const std::string file=i == 0 ? "lod3_0.png" : "lod3_45.png";
                require(j.at("uri") == file && j.at("elevationDegrees") == (i == 0 ? 0 : 45), "Invalid LOD3 image path or angle");
                v.albedo=readImage(directory/file,2048,false,32u*1024u*1024u);
                require(j.at("width") == v.albedo.width && j.at("height") == v.albedo.height, "LOD3 image extent differs from metadata");
                v.direction=readVector(j.at("direction")); v.up=readVector(j.at("up"));
                v.centre=readVector(j.at("centre")); v.halfExtent=j.at("halfExtent").get<float>();
                billboard.push_back(std::move(v));
            }
            validateLod3(billboard);
        }
        tree=std::move(result);
        if(lod3) *lod3=std::move(billboard);
        error.clear(); return true;
    }
    catch (const std::exception& e) { error=e.what(); return false; }
}
}
