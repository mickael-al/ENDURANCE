#if defined(_WIN32) && !defined(NOMINMAX)
#define NOMINMAX
#endif
#include "glcore.hpp"
#include "SkinnedCloth.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <map>
#include <atomic>
#include <chrono>
#include <thread>

#include "Miscellaneous/LatestSnapshot.hpp"

#include "ShapeBufferBase.hpp"
#include "ShapeBufferSkeleton.hpp"
#include "SkinnedTransform.hpp"

namespace
{
    bool finite(const glm::vec3& value)
    {
        return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
    }

    bool finite(const glm::mat4& value)
    {
        for (unsigned column = 0; column < 4; ++column)
            for (unsigned row = 0; row < 4; ++row)
                if (!std::isfinite(value[column][row])) return false;
        return true;
    }

    bool compatibleBind(const glm::mat4& first, const glm::mat4& second)
    {
        for (unsigned column = 0; column < 4; ++column)
            for (unsigned row = 0; row < 4; ++row)
            {
                const float a = first[column][row], b = second[column][row];
                if (!std::isfinite(a) || !std::isfinite(b) ||
                    std::abs(a - b) > 1.0e-5f * (std::max)({1.0f, std::abs(a), std::abs(b)})) return false;
            }
        return true;
    }

    glm::vec3 unitOr(const glm::vec3& value, const glm::vec3& fallback)
    {
        const float squared = glm::dot(value, value);
        return finite(value) && squared > 1.0e-12f
            ? value / std::sqrt(squared) : fallback;
    }

    float rotationScore(const glm::mat3& rotation, const glm::mat3& covariance)
    {
        return glm::dot(rotation[0], covariance[0]) + glm::dot(rotation[1], covariance[1]) +
            glm::dot(rotation[2], covariance[2]);
    }

    void fitRotation(const glm::mat3& covariance, glm::quat& rotation, glm::mat3& basis)
    {
        // Bounded warm-started polar rotation extraction. Rank-two cloth
        // neighbourhoods need no matrix inverse or SVD to fit their rotation.
        for (unsigned iteration = 0; iteration < 12; ++iteration)
        {
            const float score = rotationScore(basis, covariance);
            glm::vec3 omega = glm::cross(basis[0], covariance[0]) +
                glm::cross(basis[1], covariance[1]) + glm::cross(basis[2], covariance[2]);
            omega /= std::abs(score) + 1.0e-9f;
            float angle = glm::length(omega);
            if (!finite(omega) || angle < 1.0e-5f) break;
            const glm::vec3 axis = omega / angle;
            angle = (std::min)(angle, 1.0f);
            bool accepted = false;
            for (unsigned attempt = 0; attempt < 5; ++attempt)
            {
                const glm::quat candidate = glm::normalize(glm::angleAxis(angle, axis) * rotation);
                const glm::mat3 candidateBasis = glm::mat3_cast(candidate);
                if (rotationScore(candidateBasis, covariance) >= score - 1.0e-6f)
                { rotation = candidate; basis = candidateBasis; accepted = true; break; }
                angle *= 0.5f;
            }
            if (!accepted) break;
        }
    }

    bool edgeFrame(const glm::vec3& first, const glm::vec3& second, glm::mat3& frame)
    {
        const glm::vec3 cross = glm::cross(first, second);
        if (!finite(first) || !finite(second) || glm::dot(first, first) < 1.0e-12f ||
            glm::dot(cross, cross) < 1.0e-16f) return false;
        const glm::vec3 tangent = glm::normalize(first), normal = glm::normalize(cross);
        frame = glm::mat3(tangent, glm::cross(normal, tangent), normal);
        return true;
    }
}

namespace Ge
{
    static_assert(sizeof(ClothRenderVertex) == 3 * sizeof(glm::vec4),
        "Cloth vertex SSBO must use three tightly packed vec4 fields");
    std::shared_ptr<const SkinnedClothMesh> SkinnedClothMesh::build(
        ShapeBufferSkeleton* buffer, const std::vector<float>& renderPinWeights,
        const ClothBuildSettings& settings, const std::vector<std::size_t>& includedShapes,
        const std::vector<std::uint16_t>& attachmentBoneRemap)
    {
        if (buffer == nullptr || buffer->getSkeleton().bones.empty()) return {};
        auto mesh = std::shared_ptr<SkinnedClothMesh>(new SkinnedClothMesh());
        mesh->m_buffer = buffer;
        const std::size_t boneCount = buffer->getSkeleton().bones.size();
        if (!attachmentBoneRemap.empty())
        {
            if (attachmentBoneRemap.size() != boneCount) return {};
            for (std::uint16_t bone : attachmentBoneRemap)
                if (bone >= boneCount) return {};
        }
        std::vector<glm::vec3> positions;
        std::map<std::array<std::uint32_t, 11>, std::uint32_t> skinSampleIndex;
        std::vector<std::size_t> selection = includedShapes;
        if (selection.empty())
            for (std::size_t i = 0; i < buffer->getShapes().size(); ++i) selection.push_back(i);
        std::vector<bool> selected(buffer->getShapes().size(), false);
        for (std::size_t shapeIndex : selection)
        {
            if (shapeIndex >= selected.size() || selected[shapeIndex]) return {};
            selected[shapeIndex] = true;
            const ShapeBufferBase* shape = buffer->getShapes()[shapeIndex];
            if (shape == nullptr || shape->getVertices().empty() ||
                shape->getIndices().size() % 3 != 0) return {};
            const auto& vertices = shape->getVertices();
            const std::size_t first = mesh->m_vertices.size();
            if (first + vertices.size() > std::numeric_limits<std::uint32_t>::max()) return {};
            mesh->m_shapes.push_back({shapeIndex, first, vertices.size()});
            for (const Vertex& vertex : vertices)
            {
                if (!finite(vertex.pos) || !std::isfinite(vertex.texCoord.x) ||
                    !std::isfinite(vertex.texCoord.y)) return {};
                std::uint32_t packed[3]{};
                // Packed influence bits can represent NaNs as floats; memcpy
                // preserves them without numeric conversion or aliasing UB.
                std::memcpy(&packed[0], &vertex.color.x, sizeof(std::uint32_t));
                std::memcpy(&packed[1], &vertex.color.y, sizeof(std::uint32_t));
                std::memcpy(&packed[2], &vertex.color.z, sizeof(std::uint32_t));
                Influence influence;
                float sum = 0.0f;
                for (unsigned i = 0; i < 4; ++i)
                {
                    influence.bone[i] = static_cast<std::uint16_t>((packed[0] >> (8u * i)) & 0xffu);
                    influence.weight[i] = static_cast<float>((packed[1 + i / 2] >> (16u * (i % 2))) & 0xffffu) / 65535.0f;
                    if (influence.weight[i] > 0.0f && influence.bone[i] >= boneCount) return {};
                    sum += influence.weight[i];
                }
                if (sum <= 0.0f) return {};
                if (!attachmentBoneRemap.empty())
                {
                    for (unsigned i = 0; i < 4; ++i)
                    {
                        if (influence.weight[i] <= 0.0f) continue;
                        influence.bone[i] = attachmentBoneRemap[influence.bone[i]];
                        for (unsigned j = 0; j < i; ++j)
                        {
                            if (influence.bone[j] == influence.bone[i])
                            {
                                influence.weight[j] += influence.weight[i];
                                influence.weight[i] = 0.0f;
                                break;
                            }
                        }
                    }
                }
                for (float& weight : influence.weight) weight /= sum;
                // UV/normal seams and triangle expansion often duplicate the
                // exact same skin point. Cache that immutable work once.
                std::array<std::uint32_t, 11> key{};
                std::memcpy(&key[0], &vertex.pos.x, sizeof(std::uint32_t));
                std::memcpy(&key[1], &vertex.pos.y, sizeof(std::uint32_t));
                std::memcpy(&key[2], &vertex.pos.z, sizeof(std::uint32_t));
                for (unsigned i = 0; i < 4; ++i)
                {
                    key[3 + i] = influence.weight[i] > 0.0f ? influence.bone[i] : 0;
                    std::memcpy(&key[7 + i], &influence.weight[i], sizeof(std::uint32_t));
                }
                auto sample = skinSampleIndex.find(key);
                if (sample == skinSampleIndex.end())
                {
                    const auto index = static_cast<std::uint32_t>(mesh->m_skinSamples.size());
                    mesh->m_skinSamples.push_back({vertex.pos, influence});
                    sample = skinSampleIndex.emplace(key, index).first;
                }
                mesh->m_renderToSkinSample.push_back(sample->second);
                mesh->m_vertices.push_back(vertex);
                positions.push_back(vertex.pos);
            }
            for (std::uint32_t index : shape->getIndices())
            {
                if (index >= vertices.size()) return {};
                mesh->m_triangles.push_back(static_cast<std::uint32_t>(first + index));
            }
        }
        if (positions.empty() || renderPinWeights.size() != positions.size()) return {};
        std::vector<float> restHandedness(positions.size(), 1.0f);
        mesh->m_triangleUV.reserve(mesh->m_triangles.size() / 3);
        for (std::size_t triangle = 0; triangle < mesh->m_triangles.size(); triangle += 3)
        {
            const auto a = mesh->m_triangles[triangle], b = mesh->m_triangles[triangle + 1],
                c = mesh->m_triangles[triangle + 2];
            const glm::vec3 normal = glm::cross(positions[b] - positions[a], positions[c] - positions[a]);
            const glm::vec2 first = mesh->m_vertices[b].texCoord - mesh->m_vertices[a].texCoord;
            const glm::vec2 second = mesh->m_vertices[c].texCoord - mesh->m_vertices[a].texCoord;
            const float uvDeterminant = first.x * second.y - first.y * second.x;
            mesh->m_triangleUV.push_back({first, second, uvDeterminant});
            for (auto vertex : {a, b, c})
                restHandedness[vertex] = uvDeterminant * glm::dot(mesh->m_vertices[vertex].normal, normal) < 0.0f ? -1.0f : 1.0f;
        }
        std::map<std::array<std::uint32_t, 6>, std::uint32_t> normalGroups;
        std::map<std::array<std::uint32_t, 9>, std::uint32_t> tangentGroups;
        mesh->m_renderToNormalGroup.reserve(positions.size());
        mesh->m_renderToTangentGroup.reserve(positions.size());
        for (std::size_t i = 0; i < positions.size(); ++i)
        {
            const Vertex& vertex = mesh->m_vertices[i];
            std::array<std::uint32_t, 6> normalKey{};
            for (unsigned axis = 0; axis < 3; ++axis)
            {
                std::memcpy(&normalKey[axis], &vertex.pos[axis], sizeof(std::uint32_t));
                std::memcpy(&normalKey[3 + axis], &vertex.normal[axis], sizeof(std::uint32_t));
            }
            const auto normal = normalGroups.emplace(normalKey, static_cast<std::uint32_t>(normalGroups.size()));
            mesh->m_renderToNormalGroup.push_back(normal.first->second);
            if (normal.second)
                mesh->m_normalFallbacks.push_back(unitOr(vertex.normal, glm::vec3(0, 1, 0)));
            std::array<std::uint32_t, 9> tangentKey{};
            std::copy(normalKey.begin(), normalKey.end(), tangentKey.begin());
            std::memcpy(&tangentKey[6], &vertex.texCoord.x, sizeof(std::uint32_t));
            std::memcpy(&tangentKey[7], &vertex.texCoord.y, sizeof(std::uint32_t));
            tangentKey[8] = restHandedness[i] < 0.0f ? 0 : 1;
            const auto tangent = tangentGroups.emplace(tangentKey, static_cast<std::uint32_t>(tangentGroups.size()));
            mesh->m_renderToTangentGroup.push_back(tangent.first->second);
            if (tangent.second) mesh->m_tangentToNormalGroup.push_back(normal.first->second);
        }
        mesh->m_normalGroupCount = normalGroups.size();
        mesh->m_tangentGroupCount = tangentGroups.size();
        glm::vec3 minimum = positions.front(), maximum = minimum;
        for (const glm::vec3& position : positions)
        {
            minimum = glm::min(minimum, position);
            maximum = glm::max(maximum, position);
        }
        mesh->m_restDiagonal = glm::length(maximum - minimum);
        mesh->m_renderPins.reserve(renderPinWeights.size());
        for (float weight : renderPinWeights)
        {
            if (!std::isfinite(weight)) return {};
            mesh->m_renderPins.push_back(std::clamp(weight, 0.0f, 1.0f));
        }
        std::vector<unsigned char> hardSkinSamples(mesh->m_skinSamples.size(), 0);
        for (std::size_t i = 0; i < mesh->m_renderPins.size(); ++i)
            if (mesh->m_renderPins[i] >= ClothHardPinWeight)
            {
                const auto sample = mesh->m_renderToSkinSample[i];
                if (hardSkinSamples[sample] == 0)
                {
                    mesh->m_hardSkinSamples.push_back(sample);
                    hardSkinSamples[sample] = 1;
                }
            }
        mesh->m_topology = ClothTopology::build(positions, mesh->m_triangles, renderPinWeights, settings);
        if (!mesh->m_topology || !mesh->m_topology->valid()) return {};
        const auto& embeddings = mesh->m_topology->renderEmbedding();
        if (embeddings.size() != positions.size()) return {};
        const auto& mapping = mesh->m_topology->renderToParticle();
        std::vector<std::size_t> targetCounts(mesh->m_topology->particleCount(), 0);
        std::map<std::pair<std::uint32_t, std::uint32_t>, std::size_t> contributionCounts;
        for (std::size_t i = 0; i < mapping.size(); ++i)
        {
            ++targetCounts[mapping[i]];
            ++contributionCounts[{mesh->m_renderToSkinSample[i], mapping[i]}];
        }
        mesh->m_activeSkinSamples.resize(mesh->m_skinSamples.size(), 0);
        for (const auto& contribution : contributionCounts)
        {
            const auto sample = contribution.first.first, particle = contribution.first.second;
            mesh->m_targetContributions.push_back({sample, particle,
                static_cast<float>(contribution.second) / static_cast<float>(targetCounts[particle])});
            if (mesh->m_topology->pinWeights()[particle] > 0.0f) mesh->m_activeSkinSamples[sample] = 1;
        }
        const auto& rest = mesh->m_topology->restPositions();
        mesh->m_nodeFrames.resize(rest.size());
        std::map<std::pair<std::uint32_t, std::uint32_t>, bool> nodeEdges;
        const auto& proxyTriangles = mesh->m_topology->triangles();
        for (std::size_t triangle = 0; triangle < proxyTriangles.size(); triangle += 3)
            for (unsigned edge = 0; edge < 3; ++edge)
            {
                auto a = proxyTriangles[triangle + edge], b = proxyTriangles[triangle + (edge + 1) % 3];
                if (a > b) std::swap(a, b);
                if (a == b || !nodeEdges.emplace(std::make_pair(a, b), true).second) continue;
                const glm::vec3 restEdge = rest[b] - rest[a];
                const float squared = glm::dot(restEdge, restEdge);
                if (squared <= 1.0e-12f) continue;
                const float weight = 1.0f / squared;
                mesh->m_nodeFrames[a].neighbours.push_back({b, restEdge, weight});
                mesh->m_nodeFrames[b].neighbours.push_back({a, -restEdge, weight});
            }
        for (auto& frame : mesh->m_nodeFrames)
        {
            float largestArea = 0.0f;
            for (std::size_t first = 0; first < frame.neighbours.size(); ++first)
                for (std::size_t second = first + 1; second < frame.neighbours.size(); ++second)
                {
                    const glm::vec3 cross = glm::cross(frame.neighbours[first].restEdge, frame.neighbours[second].restEdge);
                    const float area = glm::dot(cross, cross);
                    if (area > largestArea)
                    { largestArea = area; frame.seed = {{static_cast<std::uint32_t>(first), static_cast<std::uint32_t>(second)}}; }
                }
        }
        std::map<std::array<std::uint32_t, 3>, std::size_t> frameIndex;
        std::map<std::array<std::uint32_t, 9>, std::uint32_t> contactIndex;
        mesh->m_embeddingFrames.reserve(embeddings.size());
        mesh->m_renderToContactSample.reserve(embeddings.size());
        for (std::size_t render = 0; render < embeddings.size(); ++render)
        {
            const ClothRenderEmbedding& embedding = embeddings[render];
            if (!finite(embedding.barycentric) || !finite(embedding.frameOffset) ||
                embedding.particles[0] >= rest.size() || embedding.particles[1] >= rest.size() ||
                embedding.particles[2] >= rest.size()) return {};
            auto found = frameIndex.find(embedding.particles);
            if (found == frameIndex.end())
            {
                const glm::vec3 edge = rest[embedding.particles[1]] - rest[embedding.particles[0]];
                const glm::vec3 other = rest[embedding.particles[2]] - rest[embedding.particles[0]];
                const glm::vec3 tangent = unitOr(edge, glm::vec3(1, 0, 0));
                const glm::vec3 normal = unitOr(glm::cross(edge, other), glm::vec3(0, 0, 1));
                SurfaceFrame frame;
                frame.particle = embedding.particles;
                frame.rest = glm::mat3(tangent, glm::cross(normal, tangent), normal);
                const std::size_t index = mesh->m_surfaceFrames.size();
                mesh->m_surfaceFrames.push_back(frame);
                found = frameIndex.emplace(embedding.particles, index).first;
            }
            mesh->m_embeddingFrames.push_back(found->second);
            std::array<std::uint32_t, 9> key{};
            for (unsigned i = 0; i < 3; ++i)
            {
                key[i] = embedding.particles[i];
                std::memcpy(&key[3 + i], &embedding.barycentric[i], sizeof(std::uint32_t));
                std::memcpy(&key[6 + i], &embedding.frameOffset[i], sizeof(std::uint32_t));
            }
            auto contact = contactIndex.find(key);
            if (contact == contactIndex.end())
            {
                const auto index = static_cast<std::uint32_t>(mesh->m_contactSamples.size());
                mesh->m_contactSamples.push_back({embedding, found->second, false, positions[render], UINT32_MAX});
                contact = contactIndex.emplace(key, index).first;
            }
            mesh->m_contactSamples[contact->second].active = mesh->m_contactSamples[contact->second].active ||
                mesh->m_renderPins[render] < ClothHardPinWeight;
            if (mesh->m_renderPins[render] >= ClothHardPinWeight)
                mesh->m_contactSamples[contact->second].hardSkinSample = mesh->m_renderToSkinSample[render];
            mesh->m_renderToContactSample.push_back(contact->second);
        }
        return mesh;
    }

    std::size_t SkinnedClothMesh::shapeIndex(std::size_t drawIndex) const
    {
        return drawIndex < m_shapes.size() ? m_shapes[drawIndex].shapeIndex :
            std::numeric_limits<std::size_t>::max();
    }

    struct SkinnedCloth::Frame
    {
        glm::mat4 world{1.0f}, inverseWorld{1.0f};
        std::vector<glm::mat4> palette;
        std::vector<ClothCapsule> capsules;
        std::vector<ClothPlane> planes;
        float scale = 1.0f, deltaSeconds = 0.0f, fixedSeconds = 1.0f / 120.0f;
        bool mirrored = false, needsReset = false;
        std::uint64_t epoch = 1, inputSequence = 0;
        double gameSeconds = 0.0;
    };

    struct SkinnedCloth::AsyncState
    {
        struct Output
        {
            std::vector<ClothRenderVertex> vertices;
            ClothAsyncDiagnostics diagnostics;
        };
        LatestSnapshot<Frame> input;
        LatestSnapshot<Output> output;
        std::atomic<bool> stop{false}, failed{false};
        std::atomic<bool> active{true};
        std::thread worker;
        // Initialized before launching, subsequently worker-owned.
        std::uint64_t processedEpoch = 1;
        double processedGameSeconds = 0.0;
        glm::mat4 processedWorld{1.0f};
    };

    SkinnedCloth::SkinnedCloth() = default;
    SkinnedCloth::~SkinnedCloth() { release(); }

    bool SkinnedCloth::bind(std::shared_ptr<const SkinnedClothMesh> mesh,
        SkinnedTransform* transform, const ClothSettings& settings)
    {
        release();
        if (!mesh || !mesh->m_topology || !mesh->m_topology->valid() ||
            transform == nullptr || transform->getBuffer() == nullptr) return false;
        const auto& referenceSkeleton = transform->getBuffer()->getSkeleton();
        const auto& partSkeleton = mesh->m_buffer->getSkeleton();
        if (!compatibleBind(referenceSkeleton.bindTransform, partSkeleton.bindTransform) ||
            !compatibleBind(referenceSkeleton.inverseBindTransform, partSkeleton.inverseBindTransform)) return false;
        const auto& reference = referenceSkeleton.bones;
        const auto& part = partSkeleton.bones;
        if (reference.size() != part.size()) return false;
        for (std::size_t i = 0; i < reference.size(); ++i)
            if (reference[i].name != part[i].name || reference[i].parent != part[i].parent ||
                !compatibleBind(reference[i].inverseBindPose, part[i].inverseBindPose)) return false;
        m_mesh = std::move(mesh);
        m_transform = transform;
        m_syncFrame.reset(new Frame);
        m_simulation.reset(new ClothSimulation(m_mesh->m_topology, settings));
        m_requestedFixedSeconds = m_simulation->fixedSeconds();
        m_collisionThickness = std::isfinite(settings.collisionThickness)
            ? (std::max)(0.0f, settings.collisionThickness) : ClothSettings{}.collisionThickness;
        m_targets.resize(m_mesh->m_topology->particleCount());
        m_skinSampleTargets.resize(m_mesh->m_skinSamples.size());
        m_palette.resize(reference.size());
        m_renderVertices.resize(m_mesh->m_vertices.size());
        m_normals.resize(m_mesh->m_normalGroupCount);
        m_tangents.resize(m_mesh->m_tangentGroupCount);
        m_bitangents.resize(m_mesh->m_tangentGroupCount);
        m_tangentHandedness.resize(m_mesh->m_tangentGroupCount);
        m_surfaceNormals.resize(m_mesh->m_surfaceFrames.size(), glm::vec3(0, 0, 1));
        m_surfaceFrameValid.resize(m_surfaceNormals.size(), 0);
        m_contactPositions.resize(m_mesh->m_contactSamples.size());
        m_localContactPositions.resize(m_contactPositions.size());
        m_localSkinTargets.resize(m_skinSampleTargets.size());
        m_nodeRotations.resize(m_mesh->m_nodeFrames.size(), glm::quat(1, 0, 0, 0));
        m_nodeMatrices.resize(m_nodeRotations.size(), glm::mat3(1.0f));
        m_contactOffsets.resize(m_mesh->m_contactSamples.size());
        m_nodeReferenceOffsets.resize(m_mesh->m_nodeFrames.size() + 1, 0);
        for (std::size_t particle = 0; particle < m_mesh->m_nodeFrames.size(); ++particle)
            m_nodeReferenceOffsets[particle + 1] = m_nodeReferenceOffsets[particle] +
                m_mesh->m_nodeFrames[particle].neighbours.size();
        m_nodeReferenceEdges.resize(m_nodeReferenceOffsets.back());
        m_nodeReferenceFrames.resize(m_nodeRotations.size(), glm::mat3(1.0f));
        m_nodeReferenceFrameValid.resize(m_nodeRotations.size(), 0);
        m_buffers.resize(m_mesh->m_shapes.size(), 0);
        glGenBuffers(static_cast<GLsizei>(m_buffers.size()), m_buffers.data());
        for (std::size_t i = 0; i < m_buffers.size(); ++i)
        {
            if (m_buffers[i] == 0) { release(); return false; }
            const auto& range = m_mesh->m_shapes[i];
            glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_buffers[i]);
            glBufferData(GL_SHADER_STORAGE_BUFFER,
                static_cast<GLsizeiptr>(range.count * sizeof(ClothRenderVertex)), nullptr, GL_DYNAMIC_DRAW);
        }
        if (!update(0.0f, {}, true)) { release(); return false; }
        m_presentedVertices = m_renderVertices;
        m_outputReady = true;
        return true;
    }

    bool SkinnedCloth::skinTargets(const glm::mat4& world, bool resetAllTargets)
    {
        for (std::size_t i = 0; i < m_targets.size(); ++i)
            if (resetAllTargets || m_mesh->m_topology->pinWeights()[i] > 0.0f) m_targets[i] = glm::vec3(0.0f);
        for (std::size_t i = 0; i < m_mesh->m_skinSamples.size(); ++i)
        {
            if (!resetAllTargets && m_mesh->m_activeSkinSamples[i] == 0) continue;
            const auto& sample = m_mesh->m_skinSamples[i];
            const auto& influence = sample.influence;
            const glm::vec4 rest(sample.rest, 1.0f);
            glm::vec4 skinned(0.0f);
            for (unsigned j = 0; j < 4; ++j)
                if (influence.weight[j] > 0.0f)
                    skinned += (m_palette[influence.bone[j]] * rest) * influence.weight[j];
            const glm::vec3 target(world * skinned);
            if (!finite(target)) return false;
            m_skinSampleTargets[i] = target;
        }
        for (const auto& contribution : m_mesh->m_targetContributions)
        {
            if (resetAllTargets || m_mesh->m_topology->pinWeights()[contribution.particle] > 0.0f)
                m_targets[contribution.particle] += m_skinSampleTargets[contribution.sample] * contribution.weight;
        }
        return true;
    }

    bool SkinnedCloth::captureFrame(Frame& frame, float deltaSeconds,
        const std::vector<ClothCapsule>& capsules, bool forceReset,
        const std::vector<ClothPlane>& planes)
    {
        const glm::mat4 world = m_transform->getModelMatrix();
        const float determinant = glm::determinant(world);
        if (!finite(world) || std::abs(determinant) < 1.0e-12f) return false;
        const glm::vec3 scales(glm::length(glm::vec3(world[0])),
            glm::length(glm::vec3(world[1])), glm::length(glm::vec3(world[2])));
        const float scale = (scales.x + scales.y + scales.z) / 3.0f;
        if (!finite(scales) || scale < 1.0e-6f ||
            (std::max)({scales.x, scales.y, scales.z}) -
            (std::min)({scales.x, scales.y, scales.z}) > scale * 0.02f) return false;
        bool transformDiscontinuity = false;
        if (m_previousWorldValid)
        {
            const float translation = glm::length(glm::vec3(world[3] - m_previousWorld[3]));
            float trace = 0.0f;
            for (unsigned axis = 0; axis < 3; ++axis)
                trace += glm::dot(glm::normalize(glm::vec3(world[axis])),
                    glm::normalize(glm::vec3(m_previousWorld[axis])));
            const float rotationCosine = std::clamp((trace - 1.0f) * 0.5f, -1.0f, 1.0f);
            const float teleportDistance = (std::max)(0.5f, m_mesh->m_restDiagonal * scale * 0.75f);
            transformDiscontinuity = translation > teleportDistance || rotationCosine < -0.5f ||
                determinant * glm::determinant(m_previousWorld) < 0.0f;
            if (!std::isfinite(deltaSeconds) || deltaSeconds <= 0.0f)
                transformDiscontinuity = transformDiscontinuity || translation > 1.0e-5f ||
                    rotationCosine < 1.0f - 1.0e-6f;
        }
        // The asynchronous producer never reads mutable solver state.
        const float previousScale = m_async ? m_submittedScale : m_simulation->uniformScale();
        const bool needsReset = forceReset || m_resetPending || transformDiscontinuity ||
            std::abs(scale - previousScale) > scale * 0.001f;
        const Skeleton& skeleton = m_transform->getBuffer()->getSkeleton();
        const auto& pose = m_transform->getGlobalPose();
        if (!pose.empty() && pose.size() != skeleton.bones.size()) return false;
        frame.palette.resize(skeleton.bones.size());
        for (std::size_t bone = 0; bone < frame.palette.size(); ++bone)
        {
            frame.palette[bone] = pose.empty() ? glm::mat4(1.0f) :
                skeleton.inverseBindTransform * pose[bone] *
                skeleton.bones[bone].inverseBindPose * skeleton.bindTransform;
            if (!finite(frame.palette[bone])) return false;
        }
        const float dt = std::isfinite(deltaSeconds) && deltaSeconds > 0.0f ? deltaSeconds : 0.0f;
        if (needsReset)
        {
            ++m_epoch;
            if (m_async) m_outputReady = false;
        }
        m_gameSeconds += double(dt);
        frame.world = world;
        frame.inverseWorld = glm::inverse(world);
        frame.scale = scale;
        frame.deltaSeconds = dt;
        frame.fixedSeconds = m_requestedFixedSeconds;
        frame.mirrored = determinant < 0.0f;
        frame.needsReset = needsReset;
        frame.epoch = m_epoch;
        frame.inputSequence = ++m_inputSequence;
        frame.gameSeconds = m_gameSeconds;
        frame.capsules = capsules;
        frame.planes = planes;
        m_previousWorld = world;
        m_previousWorldValid = true;
        // Compare future frames with the last requested reset scale, so many
        // small scale changes still accumulate into a material-space reset.
        if (needsReset) m_submittedScale = scale;
        m_resetPending = false;
        return true;
    }

    bool SkinnedCloth::computeFrame(const Frame& frame, float deltaSeconds, bool needsReset)
    {
        m_simulation->setFixedSeconds(frame.fixedSeconds);
        m_palette = frame.palette;
        if (!skinTargets(frame.world, needsReset)) return false;
        if (needsReset)
        {
            m_simulation->reset(m_targets, frame.scale, frame.mirrored);
            std::fill(m_surfaceFrameValid.begin(), m_surfaceFrameValid.end(), 0);
            rebuildDeformationCache(frame.world, frame.scale);
            std::fill(m_nodeRotations.begin(), m_nodeRotations.end(), glm::quat(1, 0, 0, 0));
            std::fill(m_nodeMatrices.begin(), m_nodeMatrices.end(), glm::mat3(1.0f));
            m_rotationInputPositions.clear();
        }
        m_simulation->advance(deltaSeconds, m_targets, frame.capsules, frame.planes);
        deform(frame.world, frame.inverseWorld, frame.capsules, frame.planes);
        return true;
    }

    bool SkinnedCloth::update(float deltaSeconds, const std::vector<ClothCapsule>& capsules,
        bool forceReset, const std::vector<ClothPlane>& planes)
    {
        if (!valid()) return false;
        if (workerFailed()) { m_outputReady = false; return false; }
        if (!m_active) return true;
        if (m_async)
        {
            Frame& frame = m_async->input.writer();
            if (!captureFrame(frame, deltaSeconds, capsules, forceReset, planes))
            {
                m_resetPending = true;
                m_outputReady = false;
                return false;
            }
            m_async->input.publish();
            // Pause freezes the last displayed mesh. A relocation/reset may
            // still install its new epoch, without integrating game time.
            const bool paused = !(std::isfinite(deltaSeconds) && deltaSeconds > 0.0f);
            if (m_async->output.tryConsume())
            {
                const auto& output = m_async->output.reader();
                if (output.diagnostics.epoch == m_epoch && (!paused || !m_outputReady))
                {
                    m_presentedVertices = output.vertices;
                    m_diagnostics = output.diagnostics;
                    uploadBuffers(m_presentedVertices);
                    m_outputReady = true;
                }
            }
            return true;
        }
        if (!captureFrame(*m_syncFrame, deltaSeconds, capsules, forceReset, planes)) return false;
        if (!computeFrame(*m_syncFrame, m_syncFrame->deltaSeconds, m_syncFrame->needsReset))
        {
            m_resetPending = true;
            return false;
        }
        uploadBuffers(m_renderVertices);
        m_diagnostics.epoch = m_syncFrame->epoch;
        m_diagnostics.inputSequence = m_syncFrame->inputSequence;
        m_diagnostics.gameSeconds = m_syncFrame->gameSeconds;
        m_diagnostics.sourceWorld = m_syncFrame->world;
        m_diagnostics.substeps = m_simulation->lastSubsteps();
        m_diagnostics.fixedSeconds = m_simulation->fixedSeconds();
        m_outputReady = true;
        return true;
    }

    void SkinnedCloth::rebuildDeformationCache(const glm::mat4& world, float scale)
    {
        m_resetWorldBasis = glm::mat3(world) / scale;
        const auto& rest = m_mesh->m_topology->restPositions();
        for (std::size_t sample = 0; sample < m_mesh->m_contactSamples.size(); ++sample)
        {
            const auto& contact = m_mesh->m_contactSamples[sample];
            for (unsigned vertex = 0; vertex < 3; ++vertex)
                m_contactOffsets[sample][vertex] = m_resetWorldBasis *
                    (contact.rest - rest[contact.embedding.particles[vertex]]) * scale;
        }
        for (std::size_t particle = 0; particle < m_mesh->m_nodeFrames.size(); ++particle)
        {
            const auto& node = m_mesh->m_nodeFrames[particle];
            const std::size_t first = m_nodeReferenceOffsets[particle];
            for (std::size_t neighbour = 0; neighbour < node.neighbours.size(); ++neighbour)
                m_nodeReferenceEdges[first + neighbour] = m_resetWorldBasis * node.neighbours[neighbour].restEdge;
            glm::mat3 reference;
            m_nodeReferenceFrameValid[particle] = node.seed[0] != UINT32_MAX &&
                edgeFrame(m_nodeReferenceEdges[first + node.seed[0]],
                    m_nodeReferenceEdges[first + node.seed[1]], reference);
            if (m_nodeReferenceFrameValid[particle])
                m_nodeReferenceFrames[particle] = glm::transpose(reference);
        }
    }

    void SkinnedCloth::deform(const glm::mat4& world, const glm::mat4& inverseWorld,
        const std::vector<ClothCapsule>& capsules, const std::vector<ClothPlane>& planes)
    {
        const auto& positions = m_simulation->renderPositions(m_targets);
        const float scale = m_simulation->uniformScale();
        const bool updateRotations = positions != m_rotationInputPositions;
        for (std::size_t particle = 0; particle < m_nodeRotations.size(); ++particle)
        {
            if (!updateRotations) break;
            const auto& node = m_mesh->m_nodeFrames[particle];
            glm::mat3 covariance(0.0f);
            for (std::size_t edge = 0; edge < node.neighbours.size(); ++edge)
            {
                const auto& neighbour = node.neighbours[edge];
                const glm::vec3& reference = m_nodeReferenceEdges[m_nodeReferenceOffsets[particle] + edge];
                const glm::vec3 current = (positions[neighbour.particle] - positions[particle]) / scale;
                covariance += glm::outerProduct(current, reference) * neighbour.weight;
            }
            float squared = 0.0f;
            for (unsigned axis = 0; axis < 3; ++axis) squared += glm::dot(covariance[axis], covariance[axis]);
            glm::quat rotation = m_nodeRotations[particle];
            glm::mat3 rotationBasis = m_nodeMatrices[particle];
            if (std::isfinite(squared) && squared > 1.0e-12f)
            {
                if (m_nodeReferenceFrameValid[particle])
                {
                    const auto& first = node.neighbours[node.seed[0]];
                    const auto& second = node.neighbours[node.seed[1]];
                    glm::mat3 current;
                    if (edgeFrame(positions[first.particle] - positions[particle],
                            positions[second.particle] - positions[particle], current))
                    {
                        const glm::quat seed = glm::normalize(glm::quat_cast(current * m_nodeReferenceFrames[particle]));
                        const glm::mat3 seedBasis = glm::mat3_cast(seed);
                        if (rotationScore(seedBasis, covariance) >
                            rotationScore(rotationBasis, covariance) + 1.0e-6f)
                        { rotation = seed; rotationBasis = seedBasis; }
                    }
                }
                fitRotation(covariance, rotation, rotationBasis);
            }
            m_nodeRotations[particle] = rotation;
            m_nodeMatrices[particle] = rotationBasis;
        }
        if (updateRotations) m_rotationInputPositions = positions;
        const glm::mat3 worldBasis(world);
        for (std::size_t i = 0; i < m_surfaceNormals.size(); ++i)
        {
            const auto& frame = m_mesh->m_surfaceFrames[i];
            const glm::vec3 edge = positions[frame.particle[1]] - positions[frame.particle[0]];
            const glm::vec3 other = positions[frame.particle[2]] - positions[frame.particle[0]];
            const glm::vec3 cross = glm::cross(edge, other);
            if (glm::dot(edge, edge) > 1.0e-12f && glm::dot(cross, cross) > 1.0e-16f)
            {
                m_surfaceNormals[i] = glm::normalize(cross);
                m_surfaceFrameValid[i] = 1;
            }
            else if (m_surfaceFrameValid[i] == 0)
            {
                const glm::vec3 tangent = unitOr(worldBasis * frame.rest[0], glm::vec3(1, 0, 0));
                const glm::vec3 normal = unitOr(glm::cross(tangent, worldBasis * frame.rest[1]), glm::vec3(0, 0, 1));
                m_surfaceNormals[i] = normal;
                m_surfaceFrameValid[i] = 1;
            }
        }
        const float thickness = m_collisionThickness * m_simulation->uniformScale();
        m_preparedContacts.prepare(capsules, thickness, planes);
        for (std::size_t i = 0; i < m_mesh->m_contactSamples.size(); ++i)
        {
            const auto& sample = m_mesh->m_contactSamples[i];
            if (sample.hardSkinSample != UINT32_MAX)
            {
                m_contactPositions[i] = m_skinSampleTargets[sample.hardSkinSample];
                continue;
            }
            const auto& embedding = sample.embedding;
            glm::vec3 position(0.0f);
            for (unsigned vertex = 0; vertex < 3; ++vertex)
            {
                const auto particle = embedding.particles[vertex];
                const glm::vec3& offset = m_contactOffsets[i][vertex];
                position += (positions[particle] + m_nodeMatrices[particle] * offset) * embedding.barycentric[vertex];
            }
            m_preparedContacts.projectSurface(position, m_surfaceNormals[sample.frame]);
            m_contactPositions[i] = position;
        }
        std::fill(m_normals.begin(), m_normals.end(), glm::vec3(0.0f));
        std::fill(m_tangents.begin(), m_tangents.end(), glm::vec3(0.0f));
        std::fill(m_bitangents.begin(), m_bitangents.end(), glm::vec3(0.0f));
        // UV/normal seams expand the same physical point into many vertices.
        // Convert each unique world point once, preserving the full vec4.
        for (std::size_t i = 0; i < m_contactPositions.size(); ++i)
            m_localContactPositions[i] = inverseWorld * glm::vec4(m_contactPositions[i], 1.0f);
        for (const auto sample : m_mesh->m_hardSkinSamples)
            m_localSkinTargets[sample] = inverseWorld * glm::vec4(m_skinSampleTargets[sample], 1.0f);
        for (std::size_t i = 0; i < m_renderVertices.size(); ++i)
        {
            // Fully attached render points use their own precise skin target,
            // not a cluster average. The free hem never mixes with skinning.
            m_renderVertices[i].position = m_mesh->m_renderPins[i] >= ClothHardPinWeight
                ? m_localSkinTargets[m_mesh->m_renderToSkinSample[i]] :
                m_localContactPositions[m_mesh->m_renderToContactSample[i]];
        }
        for (std::size_t triangle = 0; triangle < m_mesh->m_triangles.size(); triangle += 3)
        {
            const auto a = m_mesh->m_triangles[triangle], b = m_mesh->m_triangles[triangle + 1],
                c = m_mesh->m_triangles[triangle + 2];
            const glm::vec3 first = glm::vec3(m_renderVertices[b].position - m_renderVertices[a].position);
            const glm::vec3 second = glm::vec3(m_renderVertices[c].position - m_renderVertices[a].position);
            const glm::vec3 normal = glm::cross(first, second);
            const auto& uv = m_mesh->m_triangleUV[triangle / 3];
            const auto& uvFirst = uv.first;
            const auto& uvSecond = uv.second;
            const float determinant = uv.determinant;
            const glm::vec3 tangent = std::abs(determinant) > 1.0e-9f
                ? (first * uvSecond.y - second * uvFirst.y) / determinant : glm::vec3(0.0f);
            const glm::vec3 bitangent = std::abs(determinant) > 1.0e-9f
                ? (second * uvFirst.x - first * uvSecond.x) / determinant : glm::vec3(0.0f);
            for (auto index : {a, b, c})
            {
                m_normals[m_mesh->m_renderToNormalGroup[index]] += normal;
                m_tangents[m_mesh->m_renderToTangentGroup[index]] += tangent;
                m_bitangents[m_mesh->m_renderToTangentGroup[index]] += bitangent;
            }
        }
        // A group's authored normal/UV/handedness keys are identical, so its
        // normalised basis is shared without averaging across authored seams.
        for (std::size_t group = 0; group < m_normals.size(); ++group)
            m_normals[group] = unitOr(m_normals[group], m_mesh->m_normalFallbacks[group]);
        for (std::size_t group = 0; group < m_tangents.size(); ++group)
        {
            const auto& normal = m_normals[m_mesh->m_tangentToNormalGroup[group]];
            glm::vec3 tangent = m_tangents[group] - glm::dot(m_tangents[group], normal) * normal;
            const float squared = glm::dot(tangent, tangent);
            if (finite(tangent) && squared > 1.0e-12f)
                tangent /= std::sqrt(squared);
            else
            {
                const glm::vec3 axis = std::abs(normal.y) < 0.9f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
                tangent = unitOr(glm::cross(axis, normal), glm::vec3(1, 0, 0));
            }
            m_tangents[group] = tangent;
            m_tangentHandedness[group] = glm::dot(glm::cross(normal, tangent), m_bitangents[group]) < 0.0f ? -1.0f : 1.0f;
        }
        for (std::size_t i = 0; i < m_renderVertices.size(); ++i)
        {
            m_renderVertices[i].normal = glm::vec4(m_normals[m_mesh->m_renderToNormalGroup[i]], 0.0f);
            const auto group = m_mesh->m_renderToTangentGroup[i];
            m_renderVertices[i].tangent = glm::vec4(m_tangents[group], m_tangentHandedness[group]);
        }
    }

    void SkinnedCloth::uploadBuffers(const std::vector<ClothRenderVertex>& vertices)
    {
        for (std::size_t i = 0; i < m_buffers.size(); ++i)
        {
            const auto& range = m_mesh->m_shapes[i];
            glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_buffers[i]);
            glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0,
                static_cast<GLsizeiptr>(range.count * sizeof(ClothRenderVertex)),
                vertices.data() + range.first);
        }
    }

    bool SkinnedCloth::startAsync()
    {
        if (m_async) return !workerFailed();
        if (!valid()) return false;
        try
        {
            std::unique_ptr<AsyncState> state(new AsyncState);
            state->input.initializeAll([this](Frame& frame)
            {
                frame.palette.resize(m_palette.size());
                frame.capsules.reserve(32);
                frame.planes.reserve(4);
            });
            state->output.initializeAll([this](AsyncState::Output& output)
            {
                output.vertices = m_renderVertices;
                output.diagnostics = m_diagnostics;
            });
            state->processedEpoch = m_epoch;
            state->processedGameSeconds = m_gameSeconds;
            state->processedWorld = m_previousWorld;
            state->active.store(m_active, std::memory_order_relaxed);
            m_presentedVertices = m_renderVertices;
            m_async = std::move(state);
            m_async->worker = std::thread(&SkinnedCloth::workerLoop, this);
            return true;
        }
        catch (...)
        {
            // Thread creation may fail; the already-bound synchronous cape
            // remains valid and the caller can choose that fallback.
            m_async.reset();
            return false;
        }
    }

    void SkinnedCloth::workerLoop() noexcept
    {
        AsyncState& state = *m_async;
        try
        {
            while (!state.stop.load(std::memory_order_acquire))
            {
                if (!state.active.load(std::memory_order_acquire) || !state.input.tryConsume())
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    continue;
                }
                const Frame& frame = state.input.reader();
                bool needsReset = frame.epoch != state.processedEpoch;
                // Coalesced ordinary submissions can cover a large transform
                // jump even when no individual rendered frame teleported.
                const float translation = glm::length(glm::vec3(frame.world[3] - state.processedWorld[3]));
                float trace = 0.0f;
                for (unsigned axis = 0; axis < 3; ++axis)
                    trace += glm::dot(glm::normalize(glm::vec3(frame.world[axis])),
                        glm::normalize(glm::vec3(state.processedWorld[axis])));
                const float rotationCosine = std::clamp((trace - 1.0f) * 0.5f, -1.0f, 1.0f);
                needsReset = needsReset || translation > (std::max)(0.5f,
                    m_mesh->m_restDiagonal * frame.scale * 0.75f) || rotationCosine < -0.5f;
                const double elapsed = (std::max)(0.0, frame.gameSeconds - state.processedGameSeconds);
                const float dt = needsReset ? frame.deltaSeconds :
                    static_cast<float>((std::min)(elapsed, double((std::numeric_limits<float>::max)())));
                state.processedGameSeconds = frame.gameSeconds;
                state.processedEpoch = frame.epoch;
                state.processedWorld = frame.world;
                if (!(dt > 0.0f) && !needsReset)
                {
                    // Cadence is an input setting, not a geometry reset. A
                    // paused request can refresh its coefficients without
                    // integrating or replacing the displayed snapshot.
                    m_simulation->setFixedSeconds(frame.fixedSeconds);
                    continue;
                }
                const auto started = std::chrono::steady_clock::now();
                if (!computeFrame(frame, dt, needsReset))
                {
                    state.failed.store(true, std::memory_order_release);
                    return;
                }
                if (state.stop.load(std::memory_order_acquire)) return;
                auto& output = state.output.writer();
                // The producer owns both vectors exclusively. Each output
                // slot was preallocated before the worker was launched; swap
                // transfers the completed mesh without a 1.45 MB copy.
                output.vertices.swap(m_renderVertices);
                output.diagnostics.epoch = frame.epoch;
                output.diagnostics.inputSequence = frame.inputSequence;
                output.diagnostics.gameSeconds = frame.gameSeconds;
                output.diagnostics.sourceWorld = frame.world;
                output.diagnostics.substeps = m_simulation->lastSubsteps();
                output.diagnostics.fixedSeconds = m_simulation->fixedSeconds();
                output.diagnostics.workerSeconds = std::chrono::duration<double>(
                    std::chrono::steady_clock::now() - started).count();
                state.output.publish();
            }
        }
        catch (...)
        {
            // No exceptions escape a std::thread entry point. Failure is
            // reported atomically; only the render thread decides GL cleanup.
            state.failed.store(true, std::memory_order_release);
        }
    }

    void SkinnedCloth::stopAsync()
    {
        if (!m_async) return;
        m_async->stop.store(true, std::memory_order_release);
        if (m_async->worker.joinable()) m_async->worker.join();
        m_async.reset();
        m_renderVertices = m_presentedVertices;
        m_resetPending = true;
    }

    void SkinnedCloth::setActive(bool active)
    {
        if (active == m_active) return;
        m_active = active;
        if (m_async) m_async->active.store(active, std::memory_order_release);
        if (active)
        {
            m_resetPending = true;
            m_outputReady = false;
        }
    }

    void SkinnedCloth::setFixedSeconds(float seconds)
    {
        if (std::isfinite(seconds) && seconds > 0.0f)
            m_requestedFixedSeconds = std::clamp(seconds, 1.0f / 480.0f, 1.0f / 30.0f);
    }

    bool SkinnedCloth::asyncEnabled() const { return bool(m_async); }

    bool SkinnedCloth::workerFailed() const
    {
        return m_async && m_async->failed.load(std::memory_order_acquire);
    }

    const ClothSimulation* SkinnedCloth::simulation() const
    {
        return m_async ? nullptr : m_simulation.get();
    }

    const std::vector<ClothRenderVertex>& SkinnedCloth::renderVertices() const
    {
        return m_async ? m_presentedVertices : m_renderVertices;
    }

    void SkinnedCloth::reset()
    {
        m_resetPending = true;
        if (m_async) m_outputReady = false;
    }

    void SkinnedCloth::release()
    {
        stopAsync();
        if (!m_buffers.empty())
            glDeleteBuffers(static_cast<GLsizei>(m_buffers.size()), m_buffers.data());
        m_buffers.clear();
        m_transform = nullptr;
        m_simulation.reset();
        m_syncFrame.reset();
        m_mesh.reset();
        m_targets.clear(); m_skinSampleTargets.clear(); m_palette.clear();
        m_renderVertices.clear(); m_normals.clear(); m_tangents.clear(); m_bitangents.clear();
        m_tangentHandedness.clear();
        m_surfaceNormals.clear(); m_surfaceFrameValid.clear();
        m_contactPositions.clear();
        m_localContactPositions.clear(); m_localSkinTargets.clear();
        m_nodeRotations.clear(); m_nodeMatrices.clear();
        m_rotationInputPositions.clear();
        m_contactOffsets.clear(); m_nodeReferenceOffsets.clear(); m_nodeReferenceEdges.clear();
        m_nodeReferenceFrames.clear(); m_nodeReferenceFrameValid.clear();
        m_resetWorldBasis = glm::mat3(1.0f);
        m_previousWorld = glm::mat4(1.0f);
        m_previousWorldValid = false;
        m_resetPending = true;
        m_presentedVertices.clear();
        m_diagnostics = {};
        m_epoch = 1;
        m_inputSequence = 0;
        m_gameSeconds = 0.0;
        m_submittedScale = 1.0f;
        m_requestedFixedSeconds = 1.0f / 120.0f;
        m_outputReady = false;
        m_active = true;
    }

    bool SkinnedCloth::valid() const
    {
        return m_mesh && m_transform && m_simulation && !m_buffers.empty();
    }

    unsigned int SkinnedCloth::getBufferID(std::size_t shape) const
    {
        return shape < m_buffers.size() ? m_buffers[shape] : 0;
    }
}
