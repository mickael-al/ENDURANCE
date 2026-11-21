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

    void GraphiquePipeline::updateDrawLocations() const
    {
        const auto revision = shadowContentRevision();
        if (m_drawLocationsRevision == revision) return;
        m_objectOffsetLocation = m_program ? glGetUniformLocation(m_program, "offsetUbo") : -1;
        m_shadowOffsetLocation = m_program ? glGetUniformLocation(m_program, "offsetShadow") : -1;
        m_drawLocationsRevision = revision;
    }

    int GraphiquePipeline::getObjectOffsetLocation() const
    {
        updateDrawLocations();
        return m_objectOffsetLocation;
    }

    int GraphiquePipeline::getShadowOffsetLocation() const
    {
        updateDrawLocations();
        return m_shadowOffsetLocation;
    }

    ShaderPair* GraphiquePipeline::getShaderPair() const
    {
        return m_shaderPair;
    }
}
