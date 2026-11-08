#include "glcore.hpp"
#include "GraphiquePipeline.hpp"
#include "ShaderCompilation.hpp"
#include "ShaderPair.hpp"
#include <iostream>

namespace Ge
{
    GraphiquePipeline::GraphiquePipeline(GraphicsDataMisc* gdm, ShaderPair* sp, std::string define,
        const ShaderCacheOptions& cache)
        : m_shaderPair(sp), m_gdm(gdm)
    {
        if (!sp)
            m_error = "Cannot build a graphics pipeline without shader files";
        else
            m_program = buildShaderProgram({{GL_VERTEX_SHADER, sp->Vert, define},
                {GL_FRAGMENT_SHADER, sp->Frag, define}}, m_error, {}, cache);
        if (!isValid()) std::cerr << "[Graphics shader] " << m_error << std::endl;
    }

    GraphiquePipeline::~GraphiquePipeline()
    {
        if (m_program) glDeleteProgram(m_program);
    }

    ShaderPair* GraphiquePipeline::getShaderPair() const
    {
        return m_shaderPair;
    }
}
