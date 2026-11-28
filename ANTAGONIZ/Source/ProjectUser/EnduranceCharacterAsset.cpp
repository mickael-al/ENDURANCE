#include "EnduranceCharacterAsset.hpp"
#include "Vertex.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>

namespace EnduranceCharacter
{
    namespace
    {
        bool finite(float value)
        {
            // Release builds use fast-math; bit tests still reject NaN/Inf.
            std::uint32_t bits;
            std::memcpy(&bits, &value, sizeof bits);
            return (bits & 0x7f800000u) != 0x7f800000u;
        }
        bool finite(const glm::vec3& value)
        { return finite(value.x) && finite(value.y) && finite(value.z); }
        bool finite(const glm::mat4& value)
        {
            for (int column = 0; column < 4; ++column)
                for (int row = 0; row < 4; ++row)
                    if (!finite(value[column][row])) return false;
            return true;
        }
        std::string lower(std::string value)
        {
            for (char& c : value) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return value;
        }
        bool contains(const std::string& value, const char* fragment)
        { return value.find(fragment) != std::string::npos; }
        std::string basename(const std::string& value)
        {
            const std::size_t end = value.find('\0');
            const std::string clean = value.substr(0, end);
            const std::size_t slash = clean.find_last_of("/\\");
            const std::string name = slash == std::string::npos ? clean : clean.substr(slash + 1);
            return name == "." || name == ".." ? std::string{} : name;
        }
        float nonnegative(float value, float fallback)
        { return finite(value) && value >= 0.0f ? value : fallback; }
        float smoothAttachment(float y, float freeHeight, float pinnedHeight)
        {
            if (!finite(y)) return 1.0f;
            if (!(pinnedHeight > freeHeight)) return y >= pinnedHeight ? 1.0f : 0.0f;
            const float t = std::clamp((y - freeHeight) / (pinnedHeight - freeHeight), 0.0f, 1.0f);
            return t * t * (3.0f - 2.0f * t);
        }
    }

    Part classify(const std::string& meshName, const std::string& materialName)
    {
        const std::string mesh = lower(meshName), material = lower(materialName);
        // Authored semantic materials survive mesh reordering and Blender's
        // numerical split suffixes. Never classify facial meshes by their index.
        if (contains(material, "hair")) return Part::Hair;
        if (contains(material, "eyeiris")) return Part::Iris;
        if (contains(material, "eyewhite")) return Part::EyeWhite;
        if (contains(material, "eyehighlight")) return Part::EyeHighlight;
        if (contains(material, "facemouth")) return Part::Mouth;
        if (contains(material, "eyelash") || contains(material, "eyeline") ||
            contains(material, "facebrow")) return Part::FaceDetail;
        if (contains(material, "_skin")) return Part::Skin;

        if (mesh == "dress" || contains(material, "dress")) return Part::Dress;
        if (mesh == "belt" || contains(material, "rope_belt")) return Part::Belt;
        if (mesh == "pants" || contains(material, "pants")) return Part::Pants;
        if (mesh == "ring" || mesh == "headband" || contains(material, "headband")) return Part::Headband;
        if (mesh == "hair" || mesh == "hairback") return Part::Hair;
        if (mesh == "body.001" || mesh == "skin") return Part::Skin;
        return Part::Unknown;
    }

    const char* partName(Part part)
    {
        switch (part)
        {
        case Part::Skin: return "skin";
        case Part::Hair: return "hair";
        case Part::Iris: return "iris";
        case Part::EyeWhite: return "eye white";
        case Part::EyeHighlight: return "eye highlight";
        case Part::FaceDetail: return "face detail";
        case Part::Mouth: return "mouth";
        case Part::Dress: return "dress";
        case Part::Belt: return "belt";
        case Part::Pants: return "pants";
        case Part::Headband: return "headband";
        default: return "unknown";
        }
    }

    bool isClothing(Part part)
    { return part == Part::Dress || part == Part::Belt || part == Part::Pants || part == Part::Headband; }

    MaterialFiles materialFiles(Part part, const std::string& diffuseSource, const std::string& normalSource)
    {
        switch (part)
        {
        case Part::Dress: return {"dress.png", "dress_normal.png"};
        case Part::Belt: return {"belt.png", "belt_normal.png"};
        case Part::Pants: return {"pants.png", "pants_normal.png"};
        case Part::Headband: return {"ring.png", "ring_normal.png"};
        default: return {basename(diffuseSource), basename(normalSource)};
        }
    }

    Normalization normalizeCharacter(const std::vector<AssetMeshView>& meshes,
        const glm::mat4& bindTransform, float desiredHeight, float sourceUnitMetres)
    {
        Normalization result;
        if (!finite(bindTransform) || !finite(desiredHeight) || desiredHeight <= 0.0f ||
            !finite(sourceUnitMetres) || sourceUnitMetres <= 0.0f)
        { result.error = "Invalid character scale or source transform."; return result; }
        float minimum = (std::numeric_limits<float>::max)();
        float maximum = -(std::numeric_limits<float>::max)();
        std::size_t count = 0;
        for (const auto& mesh : meshes)
        {
            if (mesh.part != Part::Skin || mesh.vertices == nullptr) continue;
            for (const Vertex& vertex : *mesh.vertices)
            {
                const glm::vec3 source = glm::vec3(bindTransform * glm::vec4(vertex.pos, 1.0f));
                if (!finite(source))
                { result.error = "Character skin contains a non-finite position."; return result; }
                minimum = (std::min)(minimum, source.y);
                maximum = (std::max)(maximum, source.y);
                ++count;
            }
        }
        if (count == 0 || !(maximum - minimum > 1.0e-6f))
        { result.error = "Cannot measure character height: no valid skin mesh bounds."; return result; }
        result.bodyHeight = (maximum - minimum) * sourceUnitMetres;
        result.heightMetres = desiredHeight;
        result.sourceToMetres = desiredHeight / (maximum - minimum);
        result.meshToCharacter = glm::scale(glm::mat4(1.0f), glm::vec3(result.sourceToMetres)) *
            glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -minimum, 0.0f)) * bindTransform;
        if (!finite(result.meshToCharacter) || !finite(result.bodyHeight))
        { result.error = "Character normalization exceeds the supported numeric range."; return result; }
        result.valid = true;
        return result;
    }

    AttachmentLayout attachmentLayout(const std::vector<AssetMeshView>& meshes,
        const Normalization& normalization, const AttachmentSettings& settings)
    {
        AttachmentLayout layout;
        const float height = finite(normalization.heightMetres) && normalization.heightMetres > 0.0f
            ? normalization.heightMetres : 1.70f;
        float beltLoopBottom = (std::numeric_limits<float>::max)();
        layout.waistHeight = 0.60f * height;
        if (finite(settings.waistHeightMetres) && settings.waistHeightMetres >= 0.0f)
            layout.waistHeight = settings.waistHeightMetres;
        else if (normalization.valid)
        {
            std::vector<glm::vec3> belt;
            float minimum = (std::numeric_limits<float>::max)();
            float maximum = -(std::numeric_limits<float>::max)();
            float minX = minimum, maxX = maximum;
            for (const auto& mesh : meshes)
            {
                if (mesh.part != Part::Belt || mesh.vertices == nullptr) continue;
                belt.reserve(belt.size() + mesh.vertices->size());
                for (const Vertex& vertex : *mesh.vertices)
                {
                    const glm::vec3 p = glm::vec3(normalization.meshToCharacter * glm::vec4(vertex.pos, 1.0f));
                    if (!finite(p)) continue;
                    belt.push_back(p);
                    minimum = (std::min)(minimum, p.y); maximum = (std::max)(maximum, p.y);
                    minX = (std::min)(minX, p.x); maxX = (std::max)(maxX, p.x);
                }
            }
            if (belt.size() >= 6 && maximum - minimum > 1.0e-5f && maxX - minX > 1.0e-5f)
            {
                constexpr std::size_t binCount = 64;
                struct Bin
                {
                    std::size_t count = 0;
                    double sumY = 0.0;
                    float minX = (std::numeric_limits<float>::max)();
                    float maxX = -(std::numeric_limits<float>::max)();
                };
                std::array<Bin, binCount> bins{};
                for (const auto& p : belt)
                {
                    const auto i = (std::min)(static_cast<std::size_t>(
                        std::clamp((p.y - minimum) / (maximum - minimum), 0.0f, 1.0f) * binCount), binCount - 1);
                    Bin& bin = bins[i];
                    ++bin.count; bin.sumY += p.y;
                    bin.minX = (std::min)(bin.minX, p.x); bin.maxX = (std::max)(bin.maxX, p.x);
                }
                // A densely tessellated dangling tassel is not a waist. First
                // require a broad cross-section, then select the densest loop.
                const float broadWidth = (maxX - minX) * 0.55f;
                std::size_t mode = binCount;
                for (std::size_t i = 0; i < binCount; ++i)
                    if (bins[i].count > 2 && bins[i].maxX - bins[i].minX >= broadWidth &&
                        (mode == binCount || bins[i].count > bins[mode].count)) mode = i;
                if (mode < binCount)
                {
                    const double modeY = bins[mode].sumY / bins[mode].count;
                    double sum = 0.0; std::size_t count = 0;
                    for (std::size_t index = 0; index < binCount; ++index)
                    {
                        const auto& bin = bins[index];
                        if (bin.count < 3 || bin.maxX - bin.minX < broadWidth) continue;
                        const double y = bin.sumY / bin.count;
                        if (std::abs(y - modeY) > 0.07 * height) continue;
                        sum += bin.sumY; count += bin.count;
                        beltLoopBottom = (std::min)(beltLoopBottom,
                            minimum + (maximum - minimum) * static_cast<float>(index) / binCount);
                    }
                    if (count > 0)
                    { layout.waistHeight = static_cast<float>(sum / count); layout.waistFromBelt = true; }
                }
            }
        }
        layout.dressPinnedHeight = layout.waistHeight + height * nonnegative(settings.dressPinnedAboveWaist, 0.03f);
        layout.dressFreeHeight = layout.waistHeight - height * nonnegative(settings.dressFreeBelowWaist, 0.12f);
        layout.beltPinnedHeight = layout.waistHeight - height * nonnegative(settings.beltPinnedBelowWaist, 0.025f);
        // Keep the entire lower loop fixed even if this asset has a wider belt
        // than the default proportions. Preserve the chosen tail fade length.
        if (layout.waistFromBelt)
            layout.beltPinnedHeight = (std::min)(layout.beltPinnedHeight, beltLoopBottom - 0.005f * height);
        const float beltFade = (std::max)(0.001f,
            nonnegative(settings.beltFreeBelowWaist, 0.055f) - nonnegative(settings.beltPinnedBelowWaist, 0.025f));
        layout.beltFreeHeight = layout.beltPinnedHeight - height * beltFade;
        return layout;
    }

    std::vector<float> attachmentMask(Part part, const std::vector<Vertex>& vertices,
        const glm::mat4& meshToCharacter, const AttachmentLayout& layout)
    {
        std::vector<float> pins(vertices.size(), 1.0f);
        if (part != Part::Dress && part != Part::Belt) return pins;
        const float freeHeight = part == Part::Dress ? layout.dressFreeHeight : layout.beltFreeHeight;
        const float pinnedHeight = part == Part::Dress ? layout.dressPinnedHeight : layout.beltPinnedHeight;
        if (!finite(meshToCharacter) || !finite(freeHeight) || !finite(pinnedHeight)) return pins;
        for (std::size_t i = 0; i < vertices.size(); ++i)
        {
            const float y = (meshToCharacter * glm::vec4(vertices[i].pos, 1.0f)).y;
            pins[i] = smoothAttachment(y, freeHeight, pinnedHeight);
        }
        return pins;
    }
}
