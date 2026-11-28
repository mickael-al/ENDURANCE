#pragma once

#include <glm/glm.hpp>
#include <string>
#include <vector>

struct Vertex;

namespace EnduranceCharacter
{
    // Material names take precedence over mesh names: the shipped mesh named
    // "Body" is actually the back of the hair, while "Body.001" is the body.
    enum class Part
    {
        Unknown, Skin, Hair, Iris, EyeWhite, EyeHighlight, FaceDetail, Mouth,
        Dress, Belt, Pants, Headband
    };

    Part classify(const std::string& meshName, const std::string& materialName);
    const char* partName(Part part);
    bool isClothing(Part part);

    struct MaterialFiles { std::string albedo, normal; };
    // Results are local filenames, never the author's absolute source paths.
    MaterialFiles materialFiles(Part part, const std::string& diffuseSource,
        const std::string& normalSource);

    struct AssetMeshView
    {
        Part part = Part::Unknown;
        const std::vector<Vertex>* vertices = nullptr;
    };

    struct Normalization
    {
        bool valid = false;
        std::string error;
        glm::mat4 meshToCharacter{1.0f};
        float bodyHeight = 0.0f; // Measured authoring height in metres, without hair.
        float heightMetres = 0.0f;
        float sourceToMetres = 0.0f; // Final conversion from source-global bone units.
    };

    // bindTransform maps the shared imported mesh space to FBX source-global
    // space. The shipped FBX is Y-up centimetres (sourceUnitMetres = .01).
    // Scale follows the measured skin bounds, rather than an assumed x10 factor.
    Normalization normalizeCharacter(const std::vector<AssetMeshView>& meshes,
        const glm::mat4& bindTransform, float desiredHeight = 1.70f,
        float sourceUnitMetres = 0.01f);

    struct AttachmentSettings
    {
        float waistHeightMetres = -1.0f; // Negative: infer from the broad belt loops.
        // Distances below are fractions of the normalized character height.
        float dressPinnedAboveWaist = 0.03f;
        float dressFreeBelowWaist = 0.12f;
        float beltPinnedBelowWaist = 0.025f;
        float beltFreeBelowWaist = 0.055f;
    };

    struct AttachmentLayout
    {
        float waistHeight = 0.0f;
        float dressPinnedHeight = 0.0f, dressFreeHeight = 0.0f;
        float beltPinnedHeight = 0.0f, beltFreeHeight = 0.0f;
        bool waistFromBelt = false;
    };

    AttachmentLayout attachmentLayout(const std::vector<AssetMeshView>& meshes,
        const Normalization& normalization, const AttachmentSettings& settings = {});

    // One weight per render vertex in its original order; 1 follows the body,
    // 0 is free cloth, intermediate values are compliant physical attachments.
    // Only dress and belt are released; all other parts return hard pins.
    std::vector<float> attachmentMask(Part part, const std::vector<Vertex>& vertices,
        const glm::mat4& meshToCharacter, const AttachmentLayout& layout);
}
