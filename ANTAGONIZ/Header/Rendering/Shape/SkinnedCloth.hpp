#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "ClothSimulation.hpp"
#include "PreparedClothContacts.hpp"
#include "Vertex.hpp"

namespace Ge
{
    class ShapeBufferSkeleton;
    class SkinnedTransform;

    // Shared immutable rest mesh. Simulation welding does not change the
    // original render indices, UVs, normal seams or packed skin influences.
    class SkinnedClothMesh final
    {
    public:
        static std::shared_ptr<const SkinnedClothMesh> build(
            ShapeBufferSkeleton* buffer,
            const std::vector<float>& renderPinWeights,
            const ClothBuildSettings& settings = {},
            const std::vector<std::size_t>& includedShapes = {},
            const std::vector<std::uint16_t>& attachmentBoneRemap = {});

        std::size_t shapeCount() const { return m_shapes.size(); }
        std::size_t vertexCount() const { return m_vertices.size(); }
        std::size_t skinSampleCount() const { return m_skinSamples.size(); }
        std::size_t contactSampleCount() const { return m_contactSamples.size(); }
        const std::vector<float>& renderPinWeights() const { return m_renderPins; }
        std::size_t shapeIndex(std::size_t drawIndex) const;
        const std::shared_ptr<const ClothTopology>& topology() const { return m_topology; }

    private:
        friend class SkinnedCloth;
        struct ShapeRange { std::size_t shapeIndex, first, count; };
        struct TriangleUV
        {
            glm::vec2 first, second;
            float determinant;
        };
        struct Influence
        {
            std::array<std::uint16_t, 4> bone{};
            std::array<float, 4> weight{};
        };
        struct SurfaceFrame
        {
            std::array<std::uint32_t, 3> particle{};
            glm::mat3 rest{1.0f};
        };
        struct SkinSample { glm::vec3 rest; Influence influence; };
        struct TargetContribution
        {
            std::uint32_t sample, particle;
            float weight;
        };
        struct ContactSample
        {
            ClothRenderEmbedding embedding;
            std::size_t frame;
            bool active = false;
            glm::vec3 rest{0.0f};
            std::uint32_t hardSkinSample = UINT32_MAX;
        };
        struct NodeNeighbour { std::uint32_t particle; glm::vec3 restEdge; float weight; };
        struct NodeFrame
        {
            std::vector<NodeNeighbour> neighbours;
            std::array<std::uint32_t, 2> seed{{UINT32_MAX, UINT32_MAX}};
        };
        ShapeBufferSkeleton* m_buffer = nullptr;
        std::shared_ptr<const ClothTopology> m_topology;
        std::vector<Vertex> m_vertices;
        std::vector<std::uint32_t> m_triangles;
        std::vector<TriangleUV> m_triangleUV;
        std::vector<SkinSample> m_skinSamples;
        std::vector<std::uint32_t> m_renderToSkinSample;
        std::vector<TargetContribution> m_targetContributions;
        std::vector<unsigned char> m_activeSkinSamples;
        std::vector<std::uint32_t> m_hardSkinSamples;
        std::vector<ShapeRange> m_shapes;
        std::vector<float> m_renderPins;
        std::vector<SurfaceFrame> m_surfaceFrames;
        std::vector<std::size_t> m_embeddingFrames;
        std::vector<ContactSample> m_contactSamples;
        std::vector<std::uint32_t> m_renderToContactSample;
        std::vector<NodeFrame> m_nodeFrames;
        std::vector<std::uint32_t> m_renderToNormalGroup, m_renderToTangentGroup;
        std::vector<glm::vec3> m_normalFallbacks;
        std::vector<std::uint32_t> m_tangentToNormalGroup;
        std::size_t m_normalGroupCount = 0, m_tangentGroupCount = 0;
        float m_restDiagonal = 0.0f;
    };

    struct ClothRenderVertex
    {
        glm::vec4 position{0.0f};
        glm::vec4 normal{0.0f, 1.0f, 0.0f, 0.0f};
        glm::vec4 tangent{1.0f, 0.0f, 0.0f, 1.0f};
    };

    // Copied with a completed geometry snapshot; reading diagnostics never
    // reaches into the running worker's mutable solver or scratch storage.
    struct ClothAsyncDiagnostics
    {
        std::uint64_t epoch = 0, inputSequence = 0;
        double gameSeconds = 0.0, workerSeconds = 0.0;
        glm::mat4 sourceWorld{1.0f};
        unsigned substeps = 0;
        float fixedSeconds = 1.0f / 120.0f;
    };

    // Per-instance CPU cloth and geometry SSBOs. Models, materials and the
    // immutable skinned asset remain owned by the caller. Public methods and
    // returned presentation/diagnostic views belong to the rendering thread.
    // The optional worker accesses only private CPU state and copied inputs.
    class SkinnedCloth final
    {
    public:
        SkinnedCloth();
        ~SkinnedCloth();
        SkinnedCloth(const SkinnedCloth&) = delete;
        SkinnedCloth& operator=(const SkinnedCloth&) = delete;

        // The part must be imported into this actor's reference bind space.
        // Names, hierarchy and bind matrices are checked before GPU allocation.
        bool bind(std::shared_ptr<const SkinnedClothMesh> mesh,
            SkinnedTransform* transform, const ClothSettings& settings = {});
        bool update(float deltaSeconds, const std::vector<ClothCapsule>& capsules,
            bool forceReset = false, const std::vector<ClothPlane>& planes = {});
        void reset();
        void release();
        // Start once at appearance. Frames publish inputs and consume the
        // latest complete result without waiting. Stop/join is lifecycle only.
        bool startAsync();
        void stopAsync();
        void setActive(bool active);
        // Render-thread request; the immutable input snapshot carries the
        // requested cadence to the worker without touching its live solver.
        void setFixedSeconds(float seconds);
        bool asyncEnabled() const;
        bool outputReady() const { return m_outputReady; }
        std::uint64_t currentEpoch() const { return m_epoch; }
        const glm::mat4& snapshotWorld() const { return m_diagnostics.sourceWorld; }
        bool workerFailed() const;
        const ClothAsyncDiagnostics& asyncDiagnostics() const { return m_diagnostics; }
        bool valid() const;
        std::size_t getShapeCount() const { return m_buffers.size(); }
        unsigned int getBufferID(std::size_t shape) const;
        const ClothSimulation* simulation() const;
        const std::vector<ClothRenderVertex>& renderVertices() const;

    private:
        struct Frame;
        struct AsyncState;
        bool captureFrame(Frame& frame, float deltaSeconds,
            const std::vector<ClothCapsule>& capsules, bool forceReset,
            const std::vector<ClothPlane>& planes);
        bool computeFrame(const Frame& frame, float deltaSeconds, bool needsReset);
        bool skinTargets(const glm::mat4& world, bool resetAllTargets);
        void rebuildDeformationCache(const glm::mat4& world, float scale);
        void deform(const glm::mat4& world, const glm::mat4& inverseWorld,
            const std::vector<ClothCapsule>& capsules, const std::vector<ClothPlane>& planes);
        void uploadBuffers(const std::vector<ClothRenderVertex>& vertices);
        void workerLoop() noexcept;
        std::shared_ptr<const SkinnedClothMesh> m_mesh;
        SkinnedTransform* m_transform = nullptr;
        std::unique_ptr<ClothSimulation> m_simulation;
        std::vector<unsigned int> m_buffers;
        std::vector<glm::vec3> m_targets;
        std::vector<glm::vec3> m_skinSampleTargets;
        std::vector<glm::mat4> m_palette;
        std::vector<ClothRenderVertex> m_renderVertices;
        std::vector<glm::vec3> m_normals, m_tangents, m_bitangents;
        std::vector<float> m_tangentHandedness;
        std::vector<glm::vec3> m_surfaceNormals;
        std::vector<unsigned char> m_surfaceFrameValid;
        std::vector<glm::vec3> m_contactPositions;
        std::vector<glm::vec4> m_localContactPositions, m_localSkinTargets;
        std::vector<glm::quat> m_nodeRotations;
        std::vector<glm::mat3> m_nodeMatrices;
        std::vector<glm::vec3> m_rotationInputPositions;
        // Per-instance rest data transformed once at reset; immutable for
        // every reconstruction until the actor's bind scale/basis changes.
        std::vector<std::array<glm::vec3, 3>> m_contactOffsets;
        std::vector<std::size_t> m_nodeReferenceOffsets;
        std::vector<glm::vec3> m_nodeReferenceEdges;
        std::vector<glm::mat3> m_nodeReferenceFrames;
        std::vector<unsigned char> m_nodeReferenceFrameValid;
        PreparedClothContacts m_preparedContacts;
        glm::mat3 m_resetWorldBasis{1.0f};
        float m_collisionThickness = 0.018f;
        glm::mat4 m_previousWorld{1.0f};
        bool m_previousWorldValid = false;
        bool m_resetPending = true;
        std::unique_ptr<Frame> m_syncFrame;
        std::unique_ptr<AsyncState> m_async;
        // Render-thread-owned presentation data, separate from CPU scratch.
        std::vector<ClothRenderVertex> m_presentedVertices;
        ClothAsyncDiagnostics m_diagnostics;
        std::uint64_t m_epoch = 1, m_inputSequence = 0;
        double m_gameSeconds = 0.0;
        float m_submittedScale = 1.0f;
        float m_requestedFixedSeconds = 1.0f / 120.0f;
        bool m_outputReady = false;
        bool m_active = true;
    };
}
