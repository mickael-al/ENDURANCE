#ifndef GE_IMMUTABLE_SHADOW_CACHE_HPP
#define GE_IMMUTABLE_SHADOW_CACHE_HPP

#include "ShadowContentToken.hpp"
#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <algorithm>
#include <cstring>
#include <vector>

namespace Ge
{
    // CPU-only snapshot of the built-in depth shader's actual inputs. Geometry
    // and alpha pixels are immutable only by explicit model opt-in; transforms,
    // material UVs, visibility, bindings and resource lifetimes are still tracked.
    // No hashes, texture reads or OpenGL queries are needed in the frame loop.
    class ImmutableShadowCache
    {
        struct Caster
        {
            std::uint64_t model, mesh, material, texture, pipeline;
            unsigned vao, vbo, ibo, indices, textureId, program, instanceIndex;
            int materialIndex;
            glm::mat4 transform;
            glm::vec2 offset, tiling;
            float alpha;
            bool operator==(const Caster& b) const noexcept
            {
                return model == b.model && mesh == b.mesh && material == b.material &&
                    texture == b.texture && pipeline == b.pipeline && vao == b.vao &&
                    vbo == b.vbo && ibo == b.ibo && indices == b.indices &&
                    textureId == b.textureId && program == b.program &&
                    instanceIndex == b.instanceIndex && materialIndex == b.materialIndex &&
                    std::memcmp(&transform, &b.transform, sizeof(transform)) == 0 &&
                    std::memcmp(&offset, &b.offset, sizeof(offset)) == 0 &&
                    std::memcmp(&tiling, &b.tiling, sizeof(tiling)) == 0 &&
                    std::memcmp(&alpha, &b.alpha, sizeof(alpha)) == 0;
            }
        };
        static bool finite(float value) noexcept
        {
            std::uint32_t bits;
            std::memcpy(&bits, &value, sizeof(bits));
            return (bits & UINT32_C(0x7f800000)) != UINT32_C(0x7f800000);
        }
    public:
        // Zero means unknown/mutable: retain the conservative redraw-every-frame
        // path. Default-constructed models never opt themselves into this cache.
        template<class Materials, class Groups, class Pipeline>
        std::uint64_t update(const Materials& materials, const Groups& groups,
                             const Pipeline* defaultPipeline, bool callbacks)
        {
            m_pending.clear();
            if (callbacks || !defaultPipeline) return 0;
            for (auto* material : materials)
            {
                if (!material || !material->getDraw() || !material->getCastShadow() ||
                    !material->getDepthTest() || material->getColor().a == 0.0f) continue;
                const auto found = groups.find(material);
                if (found == groups.end()) continue;
                for (const auto& group : found->second)
                {
                    if (!std::any_of(group.second.begin(), group.second.end(),
                        [](const auto* model) { return model && model->getDraw(); })) continue;
                    auto* mesh = group.first;
                    auto* texture = material->getAlbedoTexture();
                    // Animated/custom buffers, external render-target textures,
                    // shader animation and additional GPU instances stay dynamic.
                    if (!mesh || !mesh->supportsImmutableShadowContent() || !texture ||
                        !texture->supportsImmutableShadowContent() ||
                        material->getShadowPipeline() != defaultPipeline ||
                        !material->getAditionalSSBO().empty() || material->getAditionalInstanced() != 0)
                        return 0;
                    for (auto* model : group.second)
                    {
                        // The draw submits the complete instance group, including
                        // hidden members whose uploaded transforms are displaced.
                        if (!model || !model->getImmutableShadowContent() ||
                            model->getShapeBuffer() != mesh || model->getMaterial() != material) return 0;
                        const auto ubo = model->getUBO();
                        Caster caster{model->shadowContentRevision(), mesh->shadowContentRevision(),
                            material->shadowContentRevision(), texture->shadowContentRevision(),
                            defaultPipeline->shadowContentRevision(), mesh->getVAO(), mesh->getVBO(),
                            mesh->getIBO(), mesh->getIndiceSize(), texture->getTextureID(),
                            defaultPipeline->getProgram(), model->getIndex(), ubo.mat_index,
                            ubo.model, material->getOffset(), material->getTilling(), material->getColor().a};
                        for (unsigned c = 0; c < 4; ++c) for (unsigned r = 0; r < 4; ++r)
                            if (!finite(caster.transform[c][r])) return 0;
                        if (!finite(caster.alpha) || !finite(caster.offset.x) || !finite(caster.offset.y) ||
                            !finite(caster.tiling.x) || !finite(caster.tiling.y)) return 0;
                        m_pending.push_back(caster);
                    }
                }
            }
            if (m_pending != m_casters)
            {
                m_casters.swap(m_pending);
                m_revision.invalidate();
            }
            return m_revision.value();
        }
    private:
        std::vector<Caster> m_casters, m_pending;
        ShadowContentToken m_revision;
    };
}
#endif
