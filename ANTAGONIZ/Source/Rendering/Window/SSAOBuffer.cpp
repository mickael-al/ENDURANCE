#include "glcore.hpp"
#include "SSAOBuffer.hpp"
#include "Debug.hpp"
#include <glm/glm.hpp>
#include <array>
#include <cmath>
#include <random>

namespace Ge
{
    bool SSAOBuffer::initialize(GraphicsDataMisc*)
    {
        // No retained vectors: restart cannot append another kernel/noise set.
        release();
        constexpr unsigned sampleCount = 64;
        static_assert(sizeof(glm::vec4) == 16, "SSAO std430 sample stride must be 16 bytes");
        std::array<glm::vec4, sampleCount> kernel{};
        std::array<float, 16 * 2> noise{};
        std::mt19937 generator(0x5353414fu);
        // Explicit conversion avoids library-dependent distribution algorithms.
        auto random = [&generator]() { return static_cast<float>(generator() >> 8) * (1.0f / 16777216.0f); };
        for (unsigned i = 0; i < sampleCount; ++i)
        {
            glm::vec3 sample;
            float length2;
            do
            {
                // Evaluation order of function arguments differs across compilers.
                const float x = random() * 2.0f - 1.0f;
                const float y = random() * 2.0f - 1.0f;
                const float z = random();
                sample = glm::vec3(x, y, z);
                length2 = glm::dot(sample, sample);
            } while (!(length2 > 1e-8f));
            sample *= 1.0f / std::sqrt(length2);
            const float fraction = static_cast<float>(i) / static_cast<float>(sampleCount);
            const float scale = 0.1f + 0.9f * fraction * fraction;
            kernel[i] = glm::vec4(sample * (random() * scale), 0.0f);
        }
        for (unsigned i = 0; i < 16; ++i)
        {
            const float angle = random() * 6.2831853071795864769f;
            noise[2 * i] = std::cos(angle);
            noise[2 * i + 1] = std::sin(angle);
        }

        glCreateBuffers(1, &m_ssbo);
        if (!m_ssbo) return false;
        // std430 vec3[] ALSO has a 16-byte stride. Explicit vec4 storage keeps
        // every sample aligned and avoids reading past a packed glm::vec3 array.
        glNamedBufferStorage(m_ssbo, sizeof(kernel), kernel.data(), 0);
        GLint bufferBytes = 0;
        glGetNamedBufferParameteriv(m_ssbo, GL_BUFFER_SIZE, &bufferBytes);
        glCreateTextures(GL_TEXTURE_2D, 1, &m_noiseTexture);
        if (!m_noiseTexture)
        {
            release();
            return false;
        }
        // Rotation uses only XY. Keep both FP32 values, without unused Z/alpha.
        glTextureStorage2D(m_noiseTexture, 1, GL_RG32F, 4, 4);
        glTextureSubImage2D(m_noiseTexture, 0, 0, 0, 4, 4, GL_RG, GL_FLOAT, noise.data());
        glTextureParameteri(m_noiseTexture, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTextureParameteri(m_noiseTexture, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTextureParameteri(m_noiseTexture, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTextureParameteri(m_noiseTexture, GL_TEXTURE_WRAP_T, GL_REPEAT);
        GLint noiseWidth = 0;
        glGetTextureLevelParameteriv(m_noiseTexture, 0, GL_TEXTURE_WIDTH, &noiseWidth);
        if (bufferBytes != static_cast<GLint>(sizeof(kernel)) || noiseWidth != 4)
        {
            release();
            return false;
        }
        // DSA initialization leaves the camera SSBO binding and active texture
        // untouched. RenderingEngine binds these resources explicitly per draw.
        Debug::INITSUCCESS("SSAOBuffer");
        return true;
    }

    unsigned int SSAOBuffer::getSSBO() const { return m_ssbo; }
    unsigned int SSAOBuffer::getNoiseTexture() const { return m_noiseTexture; }

    void SSAOBuffer::release()
    {
        if (m_noiseTexture) glDeleteTextures(1, &m_noiseTexture);
        if (m_ssbo) glDeleteBuffers(1, &m_ssbo);
        m_noiseTexture = m_ssbo = 0;
    }
}
