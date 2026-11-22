#include "EnvironmentScreenEffects.hpp"
#include "glcore.hpp"
#include "GraphicsDataMisc.hpp"
#include "GraphiquePipelineManager.hpp"
#include "GraphiquePipeline.hpp"
#include "Engine.hpp"
#include "PointeurClass.hpp"
#include "ShapeBuffer.hpp"
#include "Camera.hpp"
#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>

namespace
{
    float bounded(float value, float low, float high, float fallback = 0.0f)
    {
        // Release enables fast-math, which permits removing std::isfinite.
        // Inspect the IEEE-754 exponent without aliasing or floating arithmetic.
        std::uint32_t bits;
        static_assert(sizeof(bits) == sizeof(value), "Expected a 32-bit float");
        std::memcpy(&bits, &value, sizeof(bits));
        return (bits & 0x7f800000u) != 0x7f800000u
            ? (std::clamp)(value, low, high) : fallback;
    }
}

namespace Ge
{
    void EnvironmentScreenEffects::setState(const EnvironmentScreenState& state)
    {
        m_state = state;
        m_state.heat = bounded(state.heat, 0.0f, 1.0f);
        m_state.frost = bounded(state.frost, 0.0f, 1.0f);
        m_state.wet = bounded(state.wet, 0.0f, 1.0f);
        m_state.condensation = bounded(state.condensation, 0.0f, 1.0f);
        m_state.dust = bounded(state.dust, 0.0f, 1.0f);
        m_state.flash = bounded(state.flash, 0.0f, 1.0f);
        for (int i = 0; i < 3; ++i) m_state.tint[i] = bounded(state.tint[i], 0.0f, 1.0f, 0.5f);
        m_state.scale = bounded(state.scale, 0.1f, 8.0f, 1.0f);
        m_state.speed = bounded(state.speed, 0.0f, 8.0f, 1.0f);
        m_state.time = bounded(state.time, -1.0e6f, 1.0e6f);
    }

    bool EnvironmentScreenEffects::requested() const noexcept
    {
        return m_state.heat > 0.0f || m_state.frost > 0.0f || m_state.wet > 0.0f ||
            m_state.condensation > 0.0f || m_state.dust > 0.0f || m_state.flash > 0.0f;
    }

    bool EnvironmentScreenEffects::initialize(GraphicsDataMisc* gdm)
    {
        release();
        m_gdm = gdm;
        m_pc = Engine::getPtrClassAddr();
        if (!gdm || !m_pc || !m_pc->graphiquePipelineManager) return false;
        const char* names[] = {"GE_ENV_HEAT", "GE_ENV_FROST", "GE_ENV_WET", "GE_ENV_CONDENSATION"};
        ShaderCacheOptions cache;
        cache.enabled = true;
        for (unsigned mask = 0; mask < m_variants.size(); ++mask)
        {
            std::string defines;
            for (unsigned bit = 0; bit < 4; ++bit)
                if (mask & (1u << bit)) defines += std::string("#define ") + names[bit] + " 1\n";
            auto& variant = m_variants[mask];
            variant.pipeline = m_pc->graphiquePipelineManager->createPipeline(
                "../Asset/Shader/Environment/screen.fs.glsl", "../Asset/Shader/pp.vs.glsl",
                false, true, false, 1, defines, cache);
            if (!variant.pipeline || !variant.pipeline->isValid()) { release(); return false; }
            const auto program = variant.pipeline->getProgram();
            variant.strengths = glGetUniformLocation(program, "uStrengths");
            variant.veil = glGetUniformLocation(program, "uVeil");
            variant.tint = glGetUniformLocation(program, "uTint");
            variant.geometry = glGetUniformLocation(program, "uProjectionZ");
            variant.clock = glGetUniformLocation(program, "uClock");
        }
        return true;
    }

    void EnvironmentScreenEffects::release()
    {
        for (auto& variant : m_variants)
        {
            if (variant.pipeline && m_pc && m_pc->graphiquePipelineManager)
                m_pc->graphiquePipelineManager->destroyPipeline(variant.pipeline);
            variant = {};
        }
        m_gdm = nullptr;
        m_pc = nullptr;
        m_state = {};
    }

    void EnvironmentScreenEffects::compute(unsigned int frameBuffer, unsigned int texture,
        unsigned int depthTexture, ShapeBuffer* sb, PPSetting*)
    {
        if (!requested() || !m_gdm || !sb || !texture || !m_gdm->str_width || !m_gdm->str_height ||
            m_gdm->str_width > static_cast<unsigned>(INT_MAX) ||
            m_gdm->str_height > static_cast<unsigned>(INT_MAX)) return;
        // A camera/depth absence only suppresses atmospheric distortion, not lens effects.
        const bool heat = m_state.heat > 0.0f && depthTexture && m_gdm->current_camera;
        const unsigned mask = (heat ? 1u : 0u) | (m_state.frost > 0.0f ? 2u : 0u) |
            (m_state.wet > 0.0f ? 4u : 0u) | (m_state.condensation > 0.0f ? 8u : 0u);
        const auto& variant = m_variants[mask];
        if (!variant.pipeline) return;
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        glDisable(GL_BLEND);
        glBindFramebuffer(GL_FRAMEBUFFER, frameBuffer);
        glViewport(0, 0, static_cast<int>(m_gdm->str_width), static_cast<int>(m_gdm->str_height));
        glUseProgram(variant.pipeline->getProgram());
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, texture);
        if (heat)
        {
            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, depthTexture);
            const auto projection = m_gdm->current_camera->getProjectionMatrix();
            glUniform4f(variant.geometry, projection[2][2], projection[3][2],
                projection[2][3], projection[3][3]);
        }
        glUniform4f(variant.strengths, heat ? m_state.heat : 0.0f,
            m_state.frost, m_state.wet, m_state.condensation);
        // Lightning is spatially uniform. Evaluate its two short pulses once
        // per frame rather than running exponentials for every screen pixel.
        float flash = 0.0f;
        if (m_state.flash > 0.0f)
        {
            const double time = static_cast<double>(m_state.time) * m_state.speed * 0.19;
            const double cycle = time - std::floor(time);
            const double first = cycle - 0.08, second = cycle - 0.13;
            flash = m_state.flash * static_cast<float>(
                std::exp(-2500.0 * first * first) + 0.45 * std::exp(-5000.0 * second * second));
        }
        glUniform2f(variant.veil, m_state.dust, flash);
        glUniform3fv(variant.tint, 1, &m_state.tint.x);
        glUniform4f(variant.clock, m_state.time, m_state.scale, m_state.speed,
            static_cast<float>(m_gdm->str_width) / static_cast<float>(m_gdm->str_height));
        glBindVertexArray(sb->getVAO());
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, sb->getIBO());
        glDrawElements(GL_TRIANGLES, sb->getIndiceSize(), GL_UNSIGNED_INT, nullptr);
    }
}
