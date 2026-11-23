#pragma once

#include "ModulePP.hpp"
#include <array>

namespace Ge
{
    // Game-controlled strengths. No biome interpretation belongs to the engine.
    // All-zero state bypasses the pass, including intermediate target allocation.
    struct EnvironmentScreenState
    {
        float heat = 0.0f, frost = 0.0f, wet = 0.0f, condensation = 0.0f;
        float dust = 0.0f, flash = 0.0f;
        glm::vec3 tint{0.72f, 0.64f, 0.50f};
        float scale = 1.0f, speed = 1.0f, time = 0.0f;
    };

    class GraphiquePipeline;
    class EnvironmentScreenEffects final : public ModulePP
    {
    public:
        bool initialize(GraphicsDataMisc* gdm) override;
        void release() override;
        void resize(int, int) override {} // PostProcess owns the two shared targets.
        void onGui(PPSetting*, PPSetting*) override {} // Controlled by the game component.
        void compute(unsigned int frameBuffer, unsigned int texture, unsigned int depthTexture,
            ShapeBuffer* sb, PPSetting* settings) override;
        void setState(const EnvironmentScreenState& state);
        bool requested() const noexcept;

    private:
        struct Variant
        {
            GraphiquePipeline* pipeline = nullptr;
            int strengths = -1, veil = -1, tint = -1, geometry = -1, clock = -1;
        };
        const ptrClass* m_pc = nullptr;
        GraphicsDataMisc* m_gdm = nullptr;
        // Four expensive procedural features => sixteen bounded permutations.
        // Dust and lightning use only inexpensive compositing arithmetic.
        std::array<Variant, 16> m_variants{};
        EnvironmentScreenState m_state{};
        unsigned int m_frostPattern = 0;
    };
}
