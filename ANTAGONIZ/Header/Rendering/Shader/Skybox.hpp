#ifndef __SKYBOX__
#define __SKYBOX__

#include "Initializer.hpp"
#include "ModulePP.hpp"
#include <vector>
#include <string>

#define SKY_SIZE 1024
#define SKY_IR_SIZE 32
#define SKY_FILTER_SIZE 256

namespace Ge
{	
	class GraphiquePipeline;
	class Skybox final : public InitializerAPI
	{
	public:
		void loadTextureSkybox(const std::string path);
		void clearTextureSkybox();
		void setNearestFiltering(bool enabled);
		void setSaturation(float saturation);
	protected:
		friend class RenderingEngine;
		bool initialize(GraphicsDataMisc* gdm);
		void render(unsigned int cameraSSBO);
		unsigned int getCubemap() const;
		unsigned int getIrradianceMap() const;
		unsigned int getPrefilterMap() const;
		unsigned int getBrdfLUTTexture() const;
		void release();
	private:
		GraphicsDataMisc* m_gdm;
		const ptrClass* m_pc;
		bool m_hasSkybox = false;
		bool m_nearestFiltering = false;
		float m_saturation = 1.0f;
		unsigned int m_nearestSampler = 0;
		int m_saturationLocation = -1;
		unsigned int m_captureFBO = 0;
		unsigned int m_captureRBO = 0;
		unsigned int m_envCubemap = 0;
		unsigned int m_prefilterMap = 0;
		unsigned int m_irradianceMap = 0;
		unsigned int m_brdfLUTTexture = 0;
		unsigned int m_cubeVAO = 0;
		unsigned int m_cubeVBO = 0;
		GraphiquePipeline* m_cubeMapCompute;
		GraphiquePipeline* m_cubeMapConvolutionCompute;
		GraphiquePipeline* m_cubeMapRender;
		GraphiquePipeline* m_cubeMapFilter;
	};
}

#endif //!__SKYBOX__
