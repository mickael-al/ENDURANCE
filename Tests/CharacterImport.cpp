#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <unordered_map>

#include "ModelManager.hpp"
#include "ShapeBufferSkeleton.hpp"
#include "Debug.hpp"

#ifdef CHARACTER_IMPORT_STANDALONE
namespace Ge { void Debug::Warn(const char* format, ...)
{
    va_list arguments; va_start(arguments, format);
    std::vfprintf(stderr, format, arguments); va_end(arguments);
    std::fputc('\n', stderr);
} }
#endif

namespace
{
void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

std::uint32_t bits(float value)
{
    std::uint32_t result; std::memcpy(&result, &value, sizeof(result)); return result;
}

bool finite(glm::vec3 value)
{
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

void checkSkinning(const FBXSkinnedData& data, const Skeleton& skeleton)
{
    glm::vec3 minimum(1.e20f), maximum(-1.e20f);
    for (std::size_t meshIndex = 0; meshIndex < data.meshVertexList.size(); ++meshIndex)
        for (const Vertex& vertex : data.meshVertexList[meshIndex])
        {
            if (!finite(vertex.pos) || !finite(vertex.normal) || !finite(vertex.tangents))
                throw std::runtime_error("Non-finite geometry in " + data.meshMetadata[meshIndex].meshName +
                    " (position=" + std::to_string(finite(vertex.pos)) +
                    ", normal=" + std::to_string(finite(vertex.normal)) +
                    ", tangent=" + std::to_string(finite(vertex.tangents)) + ")");
            const auto ids = bits(vertex.color.x);
            const auto w01 = bits(vertex.color.y), w23 = bits(vertex.color.z);
            const unsigned weights[] = {w01 & 65535u, w01 >> 16u, w23 & 65535u, w23 >> 16u};
            unsigned total = 0;
            for (unsigned i = 0; i < 4; ++i)
            {
                total += weights[i];
                if (weights[i]) require(((ids >> (8u * i)) & 255u) < skeleton.bones.size(),
                    "Skinning bone index is outside the shared skeleton");
            }
            require(total >= 65531u && total <= 65535u,
                "A vertex is unweighted or its four influences are not normalized");
            const glm::vec3 source(skeleton.bindTransform * glm::vec4(vertex.pos, 1.f));
            minimum = glm::min(minimum, source); maximum = glm::max(maximum, source);
        }
    // The exported mesh objects have different local scales and origins. All
    // parts must reconstruct into the same centimeter, Y-up source space.
    require(std::abs(minimum.y) < .01f && maximum.y > 166.f && maximum.y < 168.f,
        "FBX mesh-to-skeleton conversion changed the character's height/origin");
}
}

int main(int argc, char** argv)
{
    try
    {
        const char* path = argc > 1 ? argv[1] : "ENDURANCE/Ressources/Model/Character/Endurance.fbx";
        Skeleton skeleton;
        const auto data = Ge::ModelManager::loadFBXSkinnedData(skeleton, path, false);
        require(data.meshVertexList.size() == 15 && data.meshIndicesList.size() == 15 &&
            data.meshMetadata.size() == 15, "The character's fifteen separate meshes were not preserved");
        require(skeleton.bones.size() == 128, "The character's shared skeleton is incomplete");
        std::unordered_map<std::string, const FBXMeshMetadata*> byName;
        for (const auto& metadata : data.meshMetadata)
        {
            require(metadata.sourceMaterialCount == 1 && !metadata.materialName.empty(),
                "Per-mesh material identity was lost");
            byName[metadata.meshName] = &metadata;
        }
        require(byName.at("Body")->materialName.find("HairBack") != std::string::npos,
            "Back hair must not be classified as skin from its mesh name");
        require(byName.at("Body.001")->diffuseTexture.find("_10.png") != std::string::npos,
            "Body texture path was lost");
        require(byName.at("Face.002")->diffuseTexture.find("_02.png") != std::string::npos,
            "Iris texture path was lost");
        require(byName.at("Hair")->normalTexture.find("N00_000_Hair_00_nml.png") != std::string::npos,
            "Hair normal-map path was lost");
        require(byName.at("dress")->normalTexture.find("normal.png") != std::string::npos &&
            byName.at("belt")->roughnessTexture.find("roughness.png") != std::string::npos,
            "Clothing source texture paths were lost");
        checkSkinning(data, skeleton);

        // Separate parts can be loaded against a differently ordered existing
        // skeleton; weights must map by identity rather than FBX encounter order.
        Skeleton reference = skeleton;
        const std::size_t count = reference.bones.size();
        std::reverse(reference.bones.begin(), reference.bones.end());
        for (auto& entry : reference.nameToID) entry.second = static_cast<uint16_t>(count - 1u - entry.second);
        for (auto& entry : reference.pathToID) entry.second = static_cast<uint16_t>(count - 1u - entry.second);
        for (std::size_t i = 0; i < count; ++i)
        {
            auto& bone = reference.bones[i]; bone.idGlobal = static_cast<uint16_t>(i);
            if (bone.parent >= 0) bone.parent = static_cast<int16_t>(count - 1u - bone.parent);
        }
        Skeleton mappedSkeleton;
        const auto mapped = Ge::ModelManager::loadFBXSkinnedData(mappedSkeleton, path, false, &reference);
        checkSkinning(mapped, mappedSkeleton);
        require(mapped.meshMetadata.size() == data.meshMetadata.size(), "Metadata was lost for a shared skeleton");
        for (std::size_t mesh = 0; mesh < data.meshVertexList.size(); ++mesh)
        {
            require(mapped.meshVertexList[mesh].size() == data.meshVertexList[mesh].size(),
                "Shared-skeleton import changed mesh topology");
            for (std::size_t vertex = 0; vertex < data.meshVertexList[mesh].size(); ++vertex)
            {
                const auto& original = data.meshVertexList[mesh][vertex];
                const auto& result = mapped.meshVertexList[mesh][vertex];
                const auto oldIds = bits(original.color.x), newIds = bits(result.color.x);
                require(bits(original.color.y) == bits(result.color.y) && bits(original.color.z) == bits(result.color.z),
                    "Shared-skeleton import changed weights");
                const unsigned packedWeights[] = {bits(original.color.y), bits(original.color.z)};
                for (unsigned slot = 0; slot < 4; ++slot)
                    if (((packedWeights[slot / 2u] >> (16u * (slot % 2u))) & 65535u) != 0u)
                        require(((newIds >> (8u * slot)) & 255u) == count - 1u - ((oldIds >> (8u * slot)) & 255u),
                            "Shared-skeleton import did not remap a bone by identity");
            }
        }
        std::puts("Character import: 15 meshes, 128 shared bones, material paths, finite skinning and remapping passed.");
        return 0;
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "Character import FAILED: %s\n", error.what()); return 1;
    }
}
