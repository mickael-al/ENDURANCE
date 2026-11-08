#include "glcore.hpp"
#include "ComputeShader.hpp"
#include "ShaderCompilation.hpp"
#include <iostream>
#include <utility>

namespace Ge
{
	ComputeShader::ComputeShader(std::string filename, std::string define, std::string traceLabel,
		const ShaderCacheOptions& cache)
	{
		m_computeProgram = buildShaderProgram({{GL_COMPUTE_SHADER, std::move(filename), std::move(define)}},
			m_error, traceLabel, cache);
		if (!isValid()) std::cerr << "[Compute shader] " << m_error << std::endl;
	}

	void ComputeShader::use()
	{
		glUseProgram(m_computeProgram);
	}

	void ComputeShader::dispatch(int num_groups_x, int num_groups_y, int num_groups_z)
	{
		if (isValid()) glDispatchCompute(num_groups_x, num_groups_y, num_groups_z);
	}

	unsigned int ComputeShader::getProgram() const
	{
		return m_computeProgram;
	}

	unsigned int ComputeShader::takeProgram() noexcept
	{
		return std::exchange(m_computeProgram, 0u);
	}

	ComputeShader::~ComputeShader()
	{
		if (m_computeProgram) glDeleteProgram(m_computeProgram);
	}
}
