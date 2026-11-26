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
#include <vector>

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

    // Cache a periodic Voronoi displacement field once, in C++. RG8 stores the
    // vector to each facet's source pixel, not a white frost overlay. Computing
    // the 25 candidate sites here avoids any cellular search during rendering.
    // 1024 squared RG8 + mips stays at 2.67 MiB, like the previous pattern cache.
    GLuint bakeFrostPattern()
    {
        constexpr int side = 1024, cells = 32;
        constexpr float encodingRange = 3.0f; // signed +/-1.5 cells
        auto hash = [](std::uint32_t value) {
            value ^= value >> 16; value *= 0x7feb352du;
            value ^= value >> 15; value *= 0x846ca68bu;
            return value ^ (value >> 16);
        };
        std::vector<glm::vec2> sites(cells * cells);
        for (int y = 0; y < cells; ++y) for (int x = 0; x < cells; ++x)
        {
            const auto h = hash(static_cast<std::uint32_t>(y * cells + x) ^ 0x62f31a9du);
            const auto k = hash(h ^ 0x97b2148du);
            sites[y * cells + x] = glm::vec2(.08f) + .84f * glm::vec2(
                float(h & 0xffffffu), float(k & 0xffffffu)) * (1.0f / 16777216.0f);
        }
        std::vector<unsigned char> encoded(side * side * 2);
        for (int y = 0; y < side; ++y) for (int x = 0; x < side; ++x)
        {
            const glm::vec2 q = (glm::vec2(x, y) + .5f) * (float(cells) / side);
            const int cx = int(q.x), cy = int(q.y);
            float nearest = 100.0f;
            glm::vec2 offset(0.0f);
            for (int dy = -2; dy <= 2; ++dy) for (int dx = -2; dx <= 2; ++dx)
            {
                const int sx = cx + dx, sy = cy + dy;
                const glm::vec2 site = glm::vec2(sx, sy) + sites[
                    ((sy + cells) % cells) * cells + (sx + cells) % cells];
                const glm::vec2 delta = site - q;
                const float distance = glm::dot(delta, delta);
                if (distance < nearest) { nearest = distance; offset = delta; }
            }
            const glm::vec2 packed = glm::clamp(offset / encodingRange + .5f, 0.0f, 1.0f);
            const auto index = static_cast<std::size_t>(y * side + x) * 2;
            encoded[index] = static_cast<unsigned char>(packed.x * 255.0f + .5f);
            encoded[index + 1] = static_cast<unsigned char>(packed.y * 255.0f + .5f);
        }
        GLuint texture = 0;
        glCreateTextures(GL_TEXTURE_2D, 1, &texture);
        glTextureStorage2D(texture, 11, GL_RG8, side, side);
        GLint unpackBuffer = 0, unpackAlignment = 0, unpackRow = 0, unpackSkipRows = 0, unpackSkipPixels = 0;
        glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, &unpackBuffer);
        glGetIntegerv(GL_UNPACK_ALIGNMENT, &unpackAlignment);
        glGetIntegerv(GL_UNPACK_ROW_LENGTH, &unpackRow);
        glGetIntegerv(GL_UNPACK_SKIP_ROWS, &unpackSkipRows);
        glGetIntegerv(GL_UNPACK_SKIP_PIXELS, &unpackSkipPixels);
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
        glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
        glTextureSubImage2D(texture, 0, 0, 0, side, side, GL_RG, GL_UNSIGNED_BYTE, encoded.data());
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, static_cast<GLuint>(unpackBuffer));
        glPixelStorei(GL_UNPACK_ALIGNMENT, unpackAlignment);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, unpackRow);
        glPixelStorei(GL_UNPACK_SKIP_ROWS, unpackSkipRows);
        glPixelStorei(GL_UNPACK_SKIP_PIXELS, unpackSkipPixels);
        glTextureParameteri(texture, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTextureParameteri(texture, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTextureParameteri(texture, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTextureParameteri(texture, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glGenerateTextureMipmap(texture);
        return texture;
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
        m_frostPattern = bakeFrostPattern();
        return true;
    }

    void EnvironmentScreenEffects::release()
    {
        if (m_frostPattern) glDeleteTextures(1, &m_frostPattern);
        m_frostPattern = 0;
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
        if (m_state.frost > 0.0f)
        {
            glBindTextureUnit(2, m_frostPattern);
            glBindSampler(2, 0);
        }
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
