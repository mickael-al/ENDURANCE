#pragma once

#include <glm/glm.hpp>
#include "Physics/PreparedClothContacts.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

inline constexpr float ClothHardPinWeight = 0.9999f;

struct ClothBuildSettings
{
    // Zero chooses a small tolerance relative to the mesh bounds. Negative
    // disables seam welding. UV and normal seams remain in the render mesh.
    float seamWeldDistance = 0.0f;
    // Zero retains the full simulation mesh. A proxy preserves fine render
    // detail through triangle embedding instead of simulating every UV vertex.
    std::size_t maxParticles = 1200;
};

struct ClothRenderEmbedding
{
    std::array<std::uint32_t, 3> particles{{0, 0, 0}};
    glm::vec3 barycentric{1.0f, 0.0f, 0.0f};
    // Material-space offset along triangle tangent, bitangent, normal. At draw
    // time use the deformed world triangle frame and multiply by uniformScale.
    glm::vec3 frameOffset{0.0f};
};

// Immutable material-space connectivity, shared by every instance of a mesh.
// No renderer, skeleton, animation or game knowledge belongs in this class.
class ClothTopology
{
public:
    static std::shared_ptr<const ClothTopology> build(
        const std::vector<glm::vec3>& restRenderPositions,
        const std::vector<std::uint32_t>& renderTriangles,
        const std::vector<float>& renderPinWeights,
        const ClothBuildSettings& settings = {});

    bool valid() const { return !m_triangles.empty(); }
    std::size_t particleCount() const { return m_restPositions.size(); }
    const std::vector<glm::vec3>& restPositions() const { return m_restPositions; }
    const std::vector<std::uint32_t>& triangles() const { return m_triangles; }
    const std::vector<std::uint32_t>& renderToParticle() const { return m_renderToParticle; }
    const std::vector<float>& pinWeights() const { return m_pinWeights; }
    const std::vector<ClothRenderEmbedding>& renderEmbedding() const { return m_renderEmbedding; }

private:
    friend class ClothSimulation;
    struct Edge { std::uint32_t a, b; float length; };
    struct Bend { std::uint32_t a, b, c, d; float angle, length; };
    struct Tether { std::uint32_t particle, anchor; float length; };
    std::vector<glm::vec3> m_restPositions;
    std::vector<std::uint32_t> m_triangles, m_renderToParticle;
    std::vector<float> m_pinWeights, m_areas;
    std::vector<Edge> m_edges;
    std::vector<Bend> m_bends;
    std::vector<Tether> m_tethers;
    std::vector<std::vector<std::uint32_t>> m_collisionNeighbours;
    std::vector<ClothRenderEmbedding> m_renderEmbedding;
};

enum class ClothContactResponse : unsigned char
{
    Physical,
    // A moving exclusion volume separates cloth without launching it. Retain
    // the pre-projection integrated velocity, removing only its inward normal
    // component. No contact impulse/friction is added by this animated helper.
    SeparationOnly
};

struct ClothCapsule
{
    glm::vec3 a{0.0f}, b{0.0f};
    float radius = 0.0f;
    float friction = 0.25f;
    // Equal endpoints form a sphere, including continuous swept contacts.
    ClothContactResponse response = ClothContactResponse::Physical;
};

struct ClothPlane
{
    // dot(normal, position) >= offset. Normal need not be normalized.
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
    float offset = 0.0f;
    float friction = 0.25f;
};

// Stateless collision polish shared with fine render-surface reconstruction.
// Returns a nearby exterior point even when overlapping capsules make ordinary
// sequential projections cycle. Plane halfspaces remain satisfied as well.
glm::vec3 projectOutsideClothCapsules(const glm::vec3& position,
    const glm::vec3& preferredDirection, const std::vector<ClothCapsule>& capsules,
    float thickness, const std::vector<ClothPlane>& planes = {});

struct ClothSettings
{
    glm::vec3 gravity{0.0f, -9.81f, 0.0f};
    float fixedSeconds = 1.0f / 120.0f;
    unsigned maxSubsteps = 8;
    unsigned iterations = 8;
    float density = 0.20f; // mass per world-space area
    float damping = 2.0f; // continuous velocity damping, per second
    float stretchCompliance = 0.0000002f;
    // Mass/edge-length-normalized angular spring compliance (seconds squared).
    // Cloth keeps stiff edge lengths while its authored folds can flex.
    float bendCompliance = 0.002f;
    // Mass-normalized spring compliance (seconds squared). Multiplying by
    // inverse mass in XPBD gives the same pin strength after mesh refinement
    // or actor rescaling. The weight then changes its natural frequency.
    float attachmentCompliance = 0.0002f;
    // Lengths below are in topology units and follow reset's uniform scale.
    float collisionThickness = 0.018f;
    float maxSpeed = 60.0f;
    bool selfCollision = true;
    // Bounds pathological folded-cloth work per dynamic point and substep.
    unsigned selfCollisionCandidates = 64;
    // Material geodesic distance to an actual hard pin, never a skin target.
    // This cheap long-range upper bound avoids stretched hanging fabric.
    bool tethers = true;
    float tetherSlack = 1.03f;
    bool triangleContacts = true;
};

class ClothSimulation
{
public:
    explicit ClothSimulation(std::shared_ptr<const ClothTopology> topology,
        const ClothSettings& settings = {});

    // Targets are per particle, in world space, after animation and IK.
    // Reset is explicit for teleports, skeleton changes or uniform rescaling.
    // It is the only operation that gives unpinned particles a skin pose.
    void reset(const std::vector<glm::vec3>& worldParticleTargets,
        float uniformScale = 1.0f, bool mirrored = false);

    // Zero/invalid delta freezes all simulation state. Long frames have bounded
    // catch-up; moving capsules are swept between simulation samples.
    void advance(float deltaSeconds,
        const std::vector<glm::vec3>& worldParticleTargets,
        const std::vector<ClothCapsule>& capsules,
        const std::vector<ClothPlane>& planes = {});

    // Call only from the simulation's owner thread. A frequency change updates
    // coefficients, preserving positions, velocities and elapsed fractional time.
    // Non-finite/non-positive inputs are ignored; supported range is 30-480 Hz.
    void setFixedSeconds(float seconds);
    float fixedSeconds() const { return m_settings.fixedSeconds; }

    const std::vector<glm::vec3>& positions() const { return m_positions; }
    const std::vector<glm::vec3>& velocities() const { return m_velocities; }
    const std::shared_ptr<const ClothTopology>& topology() const { return m_topology; }
    float uniformScale() const { return m_scale; }
    unsigned lastSubsteps() const { return m_lastSubsteps; }

    // Drawing starts from the current physical sample and predicts only the
    // remaining time, bounded to one fixed substep. Fully pinned points follow
    // the current animated pose exactly; partial/zero pins use simulation only.
    const std::vector<glm::vec3>& renderPositions(
        const std::vector<glm::vec3>& currentHardPinTargets);

private:
    void step(float dt, const std::vector<glm::vec3>& targets,
        const std::vector<ClothCapsule>& capsules,
        const std::vector<ClothPlane>& planes);
    void prepareFixedCoefficients();
    void prepareConstraints();
    void preparePlanes(const std::vector<ClothPlane>& planes);
    void prepareSelfCollision();
    void solveSelfCollision();
    void solveTriangleContacts(float dt, const std::vector<ClothCapsule>& capsules);
    std::shared_ptr<const ClothTopology> m_topology;
    ClothSettings m_settings;
    std::vector<glm::vec3> m_positions, m_previous, m_velocities, m_rendered;
    std::vector<glm::vec3> m_frameTargets, m_stepTargets, m_pinLambda;
    std::vector<glm::vec3> m_contactNormal, m_surfaceVelocity;
    std::vector<float> m_contactFriction;
    std::vector<ClothContactResponse> m_contactResponse;
    std::vector<ClothCapsule> m_frameCapsules, m_simCapsules;
    std::vector<ClothCapsule> m_cleanedCapsules, m_sampledCapsules;
    std::vector<std::array<glm::vec3, 2>> m_capsuleBounds;
    std::vector<std::array<glm::vec4, 6>> m_capsulePackedBounds;
    // Axes/lengths and motion/radius invariants of one fixed substep. The last
    // two entries hold sphere motion/flag and velocity/radius delta, avoiding
    // repeated collider preparation for every particle/structural iteration.
    std::vector<std::array<glm::vec4, 5>> m_capsuleStepCache;
    // The t=0 CCD sample uses the same previous point/capsule in every
    // structural iteration. Fill only pairs that survive swept-AABB rejection.
    std::vector<std::array<glm::vec4, 4>> m_capsuleStartCache;
    std::vector<unsigned char> m_capsuleStartValid;
    // Original/unit normals and plane constants, preserving expression order.
    std::vector<std::array<glm::vec4, 3>> m_planeStepCache;
    std::vector<unsigned char> m_triangleCapsuleFeasibility;
    std::vector<float> m_inverseMass, m_edgeLambda, m_bendLambda, m_bendRestWeight;
    // Active constraints keep authored order in both directions. Their static
    // coefficients only depend on settings, masses, reset scale and mirror parity.
    struct PreparedEdge
    {
        std::uint32_t a, b, constraint;
        float length, denominator;
    };
    struct PreparedBend
    {
        std::array<std::uint32_t, 4> particles;
        std::uint32_t constraint;
        float alpha, angle, maximumMove;
        double safeMaximumMoveSquared;
    };
    struct PreparedPin
    {
        std::uint32_t particle;
        float alpha, denominator;
    };
    struct PreparedTether
    {
        std::uint32_t particle, anchor;
        float maximum;
    };
    std::vector<PreparedEdge> m_preparedEdges;
    std::vector<PreparedBend> m_preparedBends;
    std::vector<PreparedPin> m_preparedPins;
    std::vector<PreparedTether> m_preparedTethers;
    std::vector<std::uint32_t> m_sampledPins, m_hardPins, m_dynamicParticles;
    glm::vec3 m_gravityStep{0.0f};
    float m_dampingStep = 1.0f, m_inverseDt2 = 1.0f, m_stretchAlpha = 0.0f;
    float m_thickness = 0.0f, m_maximumSpeed = 0.0f;
    float m_contactTolerance = 0.0f;
    struct SelfCollisionCell
    {
        std::int64_t x = 0, y = 0, z = 0;
        std::uint32_t first = UINT32_MAX, last = UINT32_MAX;
    };
    std::vector<SelfCollisionCell> m_selfCollisionCells;
    std::vector<std::uint32_t> m_selfCollisionNext;
    // Bounded pair tables for ordinary proxies; large full-resolution meshes
    // retain the original on-demand path instead of allocating quadratically.
    std::vector<std::uint64_t> m_selfCollisionExclusions;
    std::vector<std::size_t> m_selfPairOffsets;
    std::vector<float> m_selfSeparation;
    std::size_t m_selfExclusionRowWords = 0;
    PreparedClothContacts m_preparedContacts;
    float m_scale = 1.0f, m_accumulator = 0.0f, m_orientationParity = 1.0f;
    unsigned m_lastSubsteps = 0;
    bool m_ready = false;
};
