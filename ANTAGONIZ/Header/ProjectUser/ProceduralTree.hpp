#ifndef ENDURANCE_PROCEDURAL_TREE_HPP
#define ENDURANCE_PROCEDURAL_TREE_HPP

#include "glm/glm.hpp"
#include <array>
#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

// Offline, engine-independent tree authoring. Units are metres, Y is up.
namespace EnduranceTrees
{
constexpr std::uint32_t GeneratorVersion = 2;
// Keep the original identifiers stable for saved authoring recipes.
enum class Family : std::int32_t
{
    Broadleaf = 0, Conifer, Weeping, Dead,
    Oak, Birch, Acacia, Baobab, Mangrove, Cypress, Palm, Aspen, Juniper, Banyan
};
constexpr std::int32_t FamilyCount = 14;
enum class BranchKind : std::int32_t { Wood = 0, Root, Vine };

struct Params
{
    std::uint32_t seed = 1337;
    Family family = Family::Broadleaf;
    float height = 18.0f;
    float trunkRadius = 0.65f;
    float trunkBend = 0.12f;
    float crownWidth = 13.0f;
    float crownHeight = 9.0f;
    float branchStart = 0.36f; // Fraction of trunk height.
    std::int32_t branchCount = 10;
    std::int32_t branchingLevels = 3;
    float branchAngle = 55.0f; // Degrees from the upward direction.
    float branchTaper = 0.52f;
    float curvature = 0.28f;
    float twigDensity = 1.0f;
    float leafSize = 0.75f; // Longest side of a small leaf-cluster card.
    float leafDensity = 1.0f;
    float clumpSize = 1.75f;
    float foliageNormalBlend = 0.72f;
    std::int32_t rootCount = 0; // The four original families keep roots disabled.
    float rootLength = 3.5f; // Horizontal spread from the trunk, metres.
    float rootRadius = 0.25f; // Basal root radius, metres.
    std::int32_t vineCount = 0;
    float vineLength = 4.0f; // Maximum drop, clipped above the ground.
    float vineRadius = 0.045f; // Hanging stem radius, metres.
    glm::vec3 barkLow{0.105f, 0.065f, 0.040f}; // Linear RGB.
    glm::vec3 barkHigh{0.29f, 0.19f, 0.10f};
    glm::vec3 leafLow{0.025f, 0.065f, 0.018f};
    glm::vec3 leafHigh{0.18f, 0.30f, 0.055f};
};

struct Image
{
    int width = 0, height = 0;
    std::vector<std::uint8_t> rgba; // Top row first, straight-alpha, linear mask.
};

struct ImpostorView
{
    Image albedo; // sRGB, top row first, straight alpha.
    Image normal; // Object-space normals encoded in [0,1], same alpha.
    glm::vec3 direction{0.0f,0.0f,1.0f};
    glm::vec3 up{0.0f,1.0f,0.0f};
    glm::vec3 centre{0.0f};
    float halfExtent=1.0f;
};

// One shared layout for capture, cache validation and view selection:
// eight azimuths at 15 degrees, eight at 60 degrees, and the zenith.
constexpr unsigned ImpostorViewCount=17;
glm::vec3 impostorDirection(unsigned index);
glm::vec3 impostorUp(unsigned index);
bool validImpostors(const std::vector<ImpostorView>& views);

struct Vertex
{
    glm::vec3 position{0.0f};
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
    glm::vec2 uv{0.0f}; // Top-origin V, matching glTF and Image rows.
    glm::vec4 color{1.0f}; // Linear RGB, alpha=1; texture supplies cutout alpha.
    // x=normalized height, y=branch/leaf flexibility, z=seeded phase, w=palette weight.
    glm::vec4 wind{0.0f};
};

struct Mesh { std::vector<Vertex> vertices; std::vector<std::uint32_t> indices; };
struct Lod { Mesh wood, foliage; };
struct BranchPoint { glm::vec3 position{0.0f}; float radius=0.01f; };
struct Branch
{
    BranchKind kind=BranchKind::Wood;
    std::int32_t parent=-1;
    std::int32_t level=0;
    float attachment=0.0f; // Fraction of the parent's polyline length.
    std::vector<BranchPoint> points;
};
struct Statistics
{
    std::uint32_t branches=0, leafCards=0;
    std::uint32_t roots=0, vines=0;
    std::array<std::uint32_t,2> triangles{};
    bool budgetLimited=false;
    double generationMilliseconds=0.0;
};
struct Tree
{
    Params params;
    std::vector<Branch> branches;
    std::array<Lod,2> lods;
    Image leafMask, barkTexture;
    glm::vec3 boundsMin{0.0f}, boundsMax{0.0f};
    Statistics statistics;
    bool cancelled=false;
    std::string warning;
};

Params preset(Family family);
Params sanitize(Params params);
const char* familyName(Family family);
bool validImage(const Image& image);
Image makeLeafMask(int resolution=128);
Image makeBarkTexture(int resolution=128);
// Mips include the original image; fixed cutoff is also used by the shaders.
std::vector<Image> coverageMipmaps(const Image& image, float cutoff=0.5f);
// A supplied mask's aspect ratio is respected. No GL calls or file IO occur here.
Tree generate(const Params& params, const Image* leafMask=nullptr,
              const std::atomic_bool* cancel=nullptr);
}

#endif
