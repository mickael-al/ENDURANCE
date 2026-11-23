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

    // A deterministic dendrite bake, not a per-frame crystal simulation. Faceted
    // side branches grow at approximately 60 degrees; their accumulated travel
    // stores the arrival time, so growth reveals actual stems before their tips.
    // RG8 + mips is 2.67 MiB; the runtime shader needs just one filtered lookup.
    GLuint bakeFrostPattern()
    {
        constexpr int side = 1024;
        struct Pixel { float coverage = 0.0f, arrival = 1.0f; };
        std::vector<Pixel> pixels(side * side);
        std::uint32_t seed = 0x62f31a9du;
        auto random = [&seed]() {
            seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
            return float(seed & 0xffffffu) * (1.0f / 16777216.0f);
        };
        auto segment = [&](glm::vec2 a, glm::vec2 b, float widthA, float widthB,
                           float arrivalA, float arrivalB) {
            a *= float(side); b *= float(side);
            const glm::vec2 ab = b - a;
            const float length2 = glm::dot(ab, ab);
            if (length2 < 0.0001f) return;
            const float radius = (std::max)(widthA, widthB) + 1.0f;
            const int x0 = (std::max)(0, int(std::floor((std::min)(a.x, b.x) - radius)));
            const int x1 = (std::min)(side - 1, int(std::ceil((std::max)(a.x, b.x) + radius)));
            const int y0 = (std::max)(0, int(std::floor((std::min)(a.y, b.y) - radius)));
            const int y1 = (std::min)(side - 1, int(std::ceil((std::max)(a.y, b.y) + radius)));
            for (int y = y0; y <= y1; ++y) for (int x = x0; x <= x1; ++x)
            {
                const glm::vec2 p(float(x) + .5f, float(y) + .5f);
                const float t = std::clamp(glm::dot(p - a, ab) / length2, 0.0f, 1.0f);
                const float width = glm::mix(widthA, widthB, t);
                const float distance = glm::length(p - a - ab * t);
                const float coverage = std::clamp(width + .8f - distance, 0.0f, 1.0f);
                auto& pixel = pixels[y * side + x];
                if (coverage > pixel.coverage)
                {
                    pixel.coverage = coverage;
                    pixel.arrival = glm::mix(arrivalA, arrivalB, t);
                }
            }
        };
        auto plate = [&](glm::vec2 baseA, glm::vec2 baseB, glm::vec2 tip,
                         float arrivalBase, float arrivalTip, float opacity) {
            baseA *= float(side); baseB *= float(side); tip *= float(side);
            const glm::vec2 e = baseB - baseA, f = tip - baseA;
            const float determinant = e.x * f.y - e.y * f.x;
            if (std::abs(determinant) < .001f) return;
            const int x0 = (std::max)(0, int(std::floor((std::min)({baseA.x, baseB.x, tip.x}))));
            const int x1 = (std::min)(side - 1, int(std::ceil((std::max)({baseA.x, baseB.x, tip.x}))));
            const int y0 = (std::max)(0, int(std::floor((std::min)({baseA.y, baseB.y, tip.y}))));
            const int y1 = (std::min)(side - 1, int(std::ceil((std::max)({baseA.y, baseB.y, tip.y}))));
            for (int y = y0; y <= y1; ++y) for (int x = x0; x <= x1; ++x)
            {
                const glm::vec2 q = glm::vec2(float(x) + .5f, float(y) + .5f) - baseA;
                const float v = (q.x * f.y - q.y * f.x) / determinant;
                const float w = (e.x * q.y - e.y * q.x) / determinant;
                const float u = 1.0f - v - w;
                if (u < 0 || v < 0 || w < 0) continue;
                // Faceted translucent tissue between the veins. The seam of
                // the two triangular facets remains visible, like etched ice.
                const float edge = std::clamp((std::min)({u, v, w}) * 18.0f, 0.0f, 1.0f);
                const float facet = u > v ? .72f : 1.0f;
                const float coverage = opacity * edge * facet;
                auto& pixel = pixels[y * side + x];
                if (coverage > pixel.coverage)
                {
                    pixel.coverage = coverage;
                    pixel.arrival = glm::mix(arrivalBase, arrivalTip, w);
                }
            }
        };
        auto fern = [&](glm::vec2 origin, float angle, float length, float width, float delay) {
            constexpr int steps = 20;
            const float curve = (random() - .5f) * 1.2f;
            const float branchScale = .65f + random() * .35f;
            glm::vec2 a = origin;
            for (int step = 0; step < steps; ++step)
            {
                const float t = float(step) / steps;
                const float heading = angle + curve * t;
                const glm::vec2 axis(std::cos(heading), std::sin(heading));
                const glm::vec2 b = a + axis * (length / steps);
                const float age = delay + t * length * 1.7f;
                segment(a, b, width * (1.0f - t * .75f), width * (1.0f - (t + 1.0f / steps) * .75f),
                    age, age + length * 1.7f / steps);
                if (step > 0 && step < steps - 1)
                    for (int sign : {-1, 1})
                    {
                        const float branchAngle = heading + float(sign) * (1.04719755f + (random() - .5f) * .12f);
                        const glm::vec2 direction(std::cos(branchAngle), std::sin(branchAngle));
                        const float branchLength = length * (.26f * (1.0f - t) + .045f) * branchScale * (.7f + random() * .6f);
                        const glm::vec2 tip = b + direction * branchLength;
                        const float branchAge = age + length * 1.7f / steps;
                        // Small translucent blades grow between selected ribs;
                        // their uneven lengths keep the perimeter feathery.
                        if (step % 3 != 0)
                            plate(b - axis * (length / steps) * .72f,
                                b + axis * (length / steps) * .8f, tip,
                                branchAge, branchAge + branchLength * 2.1f,
                                (.18f + random() * .20f) * (1.0f - t * .35f));
                        segment(b, tip, width * .6f, .22f, branchAge, branchAge + branchLength * 2.1f);
                        // Small secondary teeth make a fern, rather than a set
                        // of long disconnected lines or a moving sine pattern.
                        for (int tooth = 1; tooth < 7; ++tooth)
                        {
                            const float u = (float(tooth) + (random() - .5f) * .35f) / 7.0f;
                            const glm::vec2 start = glm::mix(b, tip, u);
                            for (int branchSide : {-1, 1})
                            {
                                const float toothAngle = branchAngle + float(branchSide) * 1.04719755f;
                                const glm::vec2 toothDir(std::cos(toothAngle), std::sin(toothAngle));
                                const float toothLength = branchLength * (.22f + random() * .20f) * (1.0f - u);
                                const float toothAge = branchAge + u * branchLength * 2.1f;
                                segment(start, start + toothDir * toothLength, width * .3f, .12f,
                                    toothAge, toothAge + toothLength * 2.5f);
                            }
                        }
                    }
                a = b;
            }
        };
        for (int edge = 0; edge < 4; ++edge)
            for (int i = 0; i < 45; ++i)
            {
                const float along = (float(i) + random()) / 45.0f;
                const bool major = i % 4 == 0;
                const float length = major ? .20f + random() * .22f : .035f + random() * .13f;
                glm::vec2 origin;
                float angle;
                if (edge == 0) { origin = {along, -.006f}; angle = 1.5707963f; }
                else if (edge == 1) { origin = {along, 1.006f}; angle = -1.5707963f; }
                else if (edge == 2) { origin = {-.006f, along}; angle = 0.0f; }
                else { origin = {1.006f, along}; angle = 3.1415927f; }
                angle += (random() - .5f) * .85f;
                fern(origin, angle, length, major ? 2.4f : 1.05f, random() * .10f);
            }
        std::vector<unsigned char> encoded(side * side * 2);
        for (std::size_t i = 0; i < pixels.size(); ++i)
        {
            encoded[i * 2] = static_cast<unsigned char>(pixels[i].coverage * 255.0f + .5f);
            // Premultiply the arrival channel so mip filtering cannot mix the
            // empty background's arrival into a thin branch and delay its growth.
            encoded[i * 2 + 1] = static_cast<unsigned char>(pixels[i].coverage * std::clamp(pixels[i].arrival, 0.0f, 1.0f) * 255.0f + .5f);
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
        glTextureParameteri(texture, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTextureParameteri(texture, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
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
