#ifndef __POST_PROCCESS__
#define __POST_PROCCESS__

#include "Initializer.hpp"
#include "ModulePP.hpp"
#include <vector>

namespace Ge
{
	class ShapeBuffer;
	class Bloom;
	class DepthOfField;
	class ColorTint;
	class PostProcess final : public InitializerAPI
	{
	public:
		PPSetting* getPPSetting();
		void onGui(PPSetting*target);
		std::vector<ModulePP*> & getModules();
		void addExternalModule(ModulePP * modulePP);
		void removeExternalModule(ModulePP* modulePP);
	protected:
		friend class RenderingEngine;
		bool initialize(GraphicsDataMisc* gdm);			
		void compute(unsigned int frameBuffer, unsigned int texture, unsigned int depthTexture, ShapeBuffer * fullScreenTriangle);
		void release();
	private:
		friend class Window;
		void resize(int width, int height);
	private:
		bool ensurePostTargets(int width, int height);
		GraphicsDataMisc* m_gdm = nullptr;
		std::vector<ModulePP*> m_modules;
		std::vector<ModulePP*> m_externalModules;
		PPSetting* m_setting = nullptr;
		Bloom* m_bloom = nullptr;
		DepthOfField* m_depthOfField = nullptr;
		ColorTint* m_colorTint = nullptr;
		unsigned int m_postFrameBuffers[2] = {};
		unsigned int m_postTextures[2] = {};
		int m_postWidth = 0;
		int m_postHeight = 0;
		bool m_postTargetsComplete = false;
	};
}

#endif //!__POST_PROCCESS__