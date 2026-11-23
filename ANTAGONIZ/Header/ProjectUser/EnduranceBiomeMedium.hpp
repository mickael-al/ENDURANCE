#pragma once

#include "EnduranceBiomeEffectsCatalog.hpp"
#include "EnduranceEnvironmentMedium.hpp"
#include <algorithm>
#include <cstring>

namespace EnduranceEnvironment
{
    // Mix the same finite fades as particles, in physical coefficient space.
    // This CPU-only operation neither resamples climate nor changes sky lighting.
    inline EnduranceEnvironmentMediumState environmentMediumState(
        const std::array<float, EffectCount>& strengths, glm::vec2 windOffset)
    {
        const auto finite = [](float value) {
            std::uint32_t bits;
            std::memcpy(&bits, &value, sizeof bits);
            return (bits & 0x7f800000u) != 0x7f800000u;
        };
        struct Accumulation
        {
            float extinction = 0.0f;
            glm::vec3 scattering{0.0f};
            float base = 0.0f, height = 0.0f, noise = 0.0f, scale = 0.0f;
        };
        std::array<Accumulation, 2> sums{};
        const auto& definitions = effectDefinitions();
        for (std::size_t i = 0; i < EffectCount; ++i)
        {
            if (!finite(strengths[i]) || strengths[i] <= 0.0f) continue;
            const auto& preset = definitions[i].medium;
            const float extinction = preset.extinction * std::clamp(strengths[i], 0.0f, 1.0f);
            if (extinction <= 0.0f) continue;
            auto& sum = sums[preset.groundLayer ? 0 : 1];
            sum.extinction += extinction;
            sum.scattering += preset.albedo * extinction;
            sum.base += preset.baseHeight * extinction;
            sum.height += preset.heightScale * extinction;
            sum.noise += preset.noiseAmount * extinction;
            sum.scale += preset.noiseScale * extinction;
        }
        EnduranceEnvironmentMediumState result;
        for (std::size_t i = 0; i < sums.size(); ++i)
        {
            const auto& sum = sums[i];
            if (sum.extinction <= 0.0f) continue;
            auto& layer = result.layers[i];
            const float inverse = 1.0f / sum.extinction;
            layer.extinction = (std::min)(sum.extinction, 0.05f);
            layer.albedo = glm::clamp(sum.scattering * inverse, 0.0f, 1.0f);
            layer.baseHeight = sum.base * inverse;
            layer.heightScale = sum.height * inverse;
            layer.noiseAmount = sum.noise * inverse;
            layer.noiseScale = sum.scale * inverse;
            layer.windOffset = {finite(windOffset.x) ? windOffset.x : 0.0f,
                                finite(windOffset.y) ? windOffset.y : 0.0f};
        }
        return result;
    }
}
