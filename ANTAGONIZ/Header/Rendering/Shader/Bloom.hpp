#ifndef __BLOOM__
#define __BLOOM__

#include "ModulePP.hpp"
#include "glm/glm.hpp"
#include <vector>
#include <array>

struct bloomMip
{
	glm::ivec2 intSize;
	unsigned int texture;
};

namespace Ge
{
	class GraphiquePipeline;
	class Bloom final : public ModulePP
	{
	public:
		bool initialize(GraphicsDataMisc* gdm);
		void resize(int width, int height);
		void compute(unsigned int frameBuffer, unsigned int texture, unsigned int depthtexture, ShapeBuffer* sb, PPSetting* settings);
		void onGui(PPSetting* target, PPSetting* current);
		void release();
		// Optional color target for effects that follow tone mapping.
		void setOutputFrameBuffer(unsigned int frameBuffer) { m_outputFrameBuffer = frameBuffer; }
	private:
		GraphicsDataMisc* m_gdm;
		int mipChainLength = 5;
		std::vector<bloomMip> mMipChain;
        // Only two stable modes per pass; scene changes select a ready program.
        std::array<GraphiquePipeline*,2> m_downSampleShaders{};
        std::array<GraphiquePipeline*,2> m_tomeGammaShaders{};
		GraphiquePipeline* m_upSampleShader = nullptr;
        std::array<int,2> srcResolutionLocations{{-1,-1}};
        std::array<int,2> gammaLocations{{-1,-1}};
        std::array<int,2> exposureLocations{{-1,-1}};
        int filterRadiusLocation = -1;
        int intensityLocation = -1;
        int thresholdLocation = -1;
		unsigned int m_outputFrameBuffer = 0;
	};
}

#endif // !__BLOOM__
