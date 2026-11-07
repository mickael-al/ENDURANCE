#include "NoiseGeneration.hpp"

void NoiseGeneration::generatePerlinNoise(float* field, int size, float scale) const
{
    glm::vec3 pos;
    for (int z = 0; z < size; z++)
    {
        for (int y = 0; y < size; y++)
        {
            for (int x = 0; x < size; x++)
            {
                pos.x = x * m_scale + m_offset.x;
                pos.y = y * m_scale + m_offset.y;
                pos.z = z * m_scale + m_offset.z;
                field[x + y * size + z * size * size] = defaultCurve(glm::perlin(pos*scale));
            }
        }
    }
}

void NoiseGeneration::generateSimplexNoise(float* field, int size, float scale) const
{
    glm::vec3 pos;
    for (int z = 0; z < size; z++)
    {
        for (int y = 0; y < size; y++)
        {
            for (int x = 0; x < size; x++)
            {
                pos.x = x * m_scale + m_offset.x;
                pos.y = y * m_scale + m_offset.y;
                pos.z = z * m_scale + m_offset.z;
                field[x + y * size + z * size * size] = defaultCurve(glm::simplex(pos * scale));
            }
        }
    }
}

/*void NoiseGeneration::generateWorleyNoise(float* field, int size, float scale) const
{
    for (int z = 0; z < size; z++)
    {
        for (int y = 0; y < size; y++)
        {
            for (int x = 0; x < size; x++)
            {
                
            }
        }
    }
}

void NoiseGeneration::generateFractalNoise(float* field, int size, float scale) const
{

}

void NoiseGeneration::generateValueNoise(float* field, int size, float scale) const
{

}

void NoiseGeneration::generateGradientNoise(float* field, int size, float scale) const
{

}*/

inline float mod_float(float x, float y) {
    return x - y * std::floor(x / y);
}

inline glm::vec2 mod_vec2(const glm::vec2& p, float y) {
    return glm::vec2(mod_float(p.x, y), mod_float(p.y, y));
}

inline float fract(float x) 
{
    return x - std::floor(x);
}

glm::vec2 gradientNoise_dir(const glm::vec2& p_in) 
{
    glm::vec2 p = mod_vec2(p_in, 289.0f);

    float x = mod_float((34.0f * p.x + 1.0f) * p.x, 289.0f) + p.y;
    x = mod_float((34.0f * x + 1.0f) * x, 289.0f);
    x = fract(x / 41.0f) * 2.0f - 1.0f;
    x = fract(x / 41.0f) * 2.0f - 1.0f;

    return glm::normalize(glm::vec2(x - std::floor(x + 0.5f), std::abs(x) - 0.5f));
}

float NoiseGeneration::gradientNoise(const glm::vec2& p)
{
    glm::vec2 ip = glm::floor(p);
    glm::vec2 fp = glm::fract(p);

    float d00 = glm::dot(gradientNoise_dir(ip), fp);
    float d01 = glm::dot(gradientNoise_dir(ip + glm::vec2(0.0f, 1.0f)), fp - glm::vec2(0.0f, 1.0f));
    float d10 = glm::dot(gradientNoise_dir(ip + glm::vec2(1.0f, 0.0f)), fp - glm::vec2(1.0f, 0.0f));
    float d11 = glm::dot(gradientNoise_dir(ip + glm::vec2(1.0f, 1.0f)), fp - glm::vec2(1.0f, 1.0f));

    fp = fp * fp * fp * (fp * (fp * 6.0f - 15.0f) + 10.0f);

    return glm::mix(glm::mix(d00, d01, fp.y), glm::mix(d10, d11, fp.y), fp.x) + 0.5f;
}

/*void NoiseGeneration::generateBillowNoise(float* field, int size, float scale) const
{

}

void NoiseGeneration::generateRidgedMultifractalNoise(float* field, int size, float scale) const
{
}

void NoiseGeneration::generateMandelbrotNoise(float* field, int size, float scale) const
{

}*/