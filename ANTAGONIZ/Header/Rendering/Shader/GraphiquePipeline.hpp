#ifndef __ENGINE_GRAPHIQUE_PIPELINE__
#define __ENGINE_GRAPHIQUE_PIPELINE__

#include <string>
#include "ShaderCompilation.hpp"
#include "ShadowContentToken.hpp"

struct GraphicsDataMisc;
namespace Ge
{
	class ShaderPair;
}

namespace Ge
{
	enum ShaderType
	{
		VertexShader,
		GeometryShader,
		FragmentShader,
		ComputeShaderType,
	};

	class GraphiquePipeline
	{
	public:
		GraphiquePipeline(GraphicsDataMisc * gdm, ShaderPair * sp, std::string define,
			const ShaderCacheOptions& cache = {});
		~GraphiquePipeline();
		GraphiquePipeline(const GraphiquePipeline&) = delete;
		GraphiquePipeline& operator=(const GraphiquePipeline&) = delete;
		unsigned int getProgram() const noexcept { return m_program; }
		bool isValid() const noexcept { return m_program != 0; }
		const std::string& getLastError() const noexcept { return m_error; }
		ShaderPair * getShaderPair() const;
        std::uint64_t shadowContentRevision() const noexcept { return m_shadowContent.value(); }
        void invalidateShadowContent() noexcept { m_shadowContent.invalidate(); }
	private:
		ShaderPair * m_shaderPair;
		GraphicsDataMisc * m_gdm;
		unsigned int m_program = 0;
		std::string m_error;
        ShadowContentToken m_shadowContent;
	};
}

#endif //!__ENGINE_GRAPHIQUE_PIPELINE__
