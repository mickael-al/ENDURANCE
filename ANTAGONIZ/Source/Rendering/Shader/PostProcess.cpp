#include "glcore.hpp"
#include "PostProcess.hpp"
#include "ShapeBuffer.hpp"
#include "Bloom.hpp"
#include "Engine.hpp"
#include "PointeurClass.hpp"
#include "Fog.hpp"
#include "VolumetricFog.hpp"
#include "DepthOfField.hpp"
#include "ColorTint.hpp"
#include "GraphicsDataMisc.hpp"
#include "Debug.hpp"
#include <algorithm>
#include <climits>
#ifdef GE_ENABLE_OIL_PAINTING
#include "OilPainting.hpp"
#endif

namespace Ge
{
	bool PostProcess::initialize(GraphicsDataMisc* gdm)
	{
		m_gdm = gdm;
		m_setting = new PPSetting();
		m_setting->gamma = Engine::getPtrClassAddr()->settingManager->getGammaAddr();
		m_modules.push_back(new Fog());
		m_bloom = new Bloom();
		m_depthOfField = new DepthOfField();
		m_colorTint = new ColorTint();
        m_environmentEffects = new EnvironmentScreenEffects();
		m_modules.push_back(m_bloom);
		m_modules.push_back(m_depthOfField);
#ifdef GE_ENABLE_OIL_PAINTING
		m_oilPainting = new OilPainting();
		m_modules.push_back(m_oilPainting);
#endif
		m_modules.push_back(m_colorTint);
        m_modules.push_back(m_environmentEffects);
		for (int i = 0; i < m_modules.size(); i++)
		{
			if (!m_modules[i]->initialize(gdm))
				return false;
		}
		m_environmentEffects->setState(m_environmentState);
		return true;
	}

	std::vector<ModulePP*>& PostProcess::getModules()
	{
		return m_modules;
	}

	void PostProcess::addExternalModule(ModulePP* modulePP)
	{
		m_externalModules.push_back(modulePP);
		modulePP->initialize(m_gdm);
	}

	void PostProcess::removeExternalModule(ModulePP* modulePP)
	{
		const auto& findIt = std::find(m_externalModules.begin(), m_externalModules.end(), modulePP);
		if (findIt != m_externalModules.end())
		{
			m_externalModules.erase(findIt);
			modulePP->release();
		}
	}

	void PostProcess::onGui(PPSetting* t)
	{
		if (ImGui::DragFloat("Gamma", m_setting->gamma))
		{
			if (t != nullptr) { t->gamma = m_setting->gamma; }
		}
		if (ImGui::DragFloat("Exposure##Bloom", &m_setting->exposure, 0.1f))
		{
			if (t != nullptr) { t->exposure = m_setting->exposure; }
		}
		for (int i = 0; i < m_modules.size(); i++)
		{
			if (m_atmosphereActive && dynamic_cast<Fog*>(m_modules[i]))
			{
				ImGui::TextUnformatted("Fog is controlled by the scene atmosphere.");
				continue;
			}
			m_modules[i]->onGui(t, m_setting);
		}
	}

	void PostProcess::compute(unsigned int frameBuffer, unsigned int texture, unsigned int depthTexture, ShapeBuffer* sb)
	{
		if (m_gdm->str_width == 0 || m_gdm->str_height == 0 ||
			m_gdm->str_width > static_cast<unsigned int>(INT_MAX) ||
			m_gdm->str_height > static_cast<unsigned int>(INT_MAX))
			return;
		bool useDof = m_setting->depth_of_field && m_gdm->current_camera;
		bool useTint = m_setting->color_tint;
		bool useEnvironment = m_environmentEffects && m_environmentEffects->requested();
        bool useOil = false;
        bool usePost = useDof || useTint || useEnvironment;
#ifdef GE_ENABLE_OIL_PAINTING
		useOil = OilPainting::requested(m_setting);
		usePost = usePost || useOil;
#endif
		if (usePost && !ensurePostTargets(
			static_cast<int>(m_gdm->str_width), static_cast<int>(m_gdm->str_height),
            (unsigned(useDof) + unsigned(useOil) + unsigned(useTint) + unsigned(useEnvironment) > 1u) ? 2u : 1u))
		{
			// A failed optional target must still leave the original presentation usable.
			useDof = useTint = useEnvironment = usePost = false;
#ifdef GE_ENABLE_OIL_PAINTING
			useOil = false;
#endif
		}
		m_bloom->setOutputFrameBuffer(usePost ? m_postFrameBuffers[0] : 0);
		glBindFramebuffer(GL_FRAMEBUFFER, 0);//fix ghost triangle bug !!!
		for (unsigned int i = 0; i < m_externalModules.size(); ++i)
		{
			if (m_atmosphereActive && (dynamic_cast<Fog*>(m_externalModules[i]) || dynamic_cast<VolumetricFog*>(m_externalModules[i]))) continue;
			m_externalModules[i]->compute(frameBuffer, texture, depthTexture, sb, m_setting);
		}
		for (unsigned int i = 0; i < m_modules.size(); ++i)
		{
			if (m_modules[i] == m_depthOfField || m_modules[i] == m_colorTint || m_modules[i] == m_environmentEffects)
				continue;
#ifdef GE_ENABLE_OIL_PAINTING
			if (m_modules[i] == m_oilPainting)
				continue;
#endif
			if (m_atmosphereActive && (dynamic_cast<Fog*>(m_modules[i]) || dynamic_cast<VolumetricFog*>(m_modules[i]))) continue;
			m_modules[i]->compute(frameBuffer, texture, depthTexture, sb, m_setting);
		}
        // Display-referred chain: tonemapping -> DoF -> optional oil -> grading
        // -> game screen effects. Reuse the same two targets without feedback.
        unsigned source = 0;
        if (useDof)
        {
            m_depthOfField->compute((useOil || useTint || useEnvironment) ? m_postFrameBuffers[1] : 0,
                m_postTextures[source], depthTexture, sb, m_setting);
            source = 1;
        }
#ifdef GE_ENABLE_OIL_PAINTING
        if (useOil)
        {
            const unsigned destination = 1 - source;
            m_oilPainting->compute((useTint || useEnvironment) ? m_postFrameBuffers[destination] : 0,
                m_postTextures[source], depthTexture, sb, m_setting);
            source = destination;
        }
#endif
        if (useTint)
        {
            const unsigned destination = 1 - source;
            m_colorTint->compute(useEnvironment ? m_postFrameBuffers[destination] : 0,
                m_postTextures[source], depthTexture, sb, m_setting);
            source = destination;
        }
        if (useEnvironment)
            m_environmentEffects->compute(0, m_postTextures[source], depthTexture, sb, m_setting);
		m_bloom->setOutputFrameBuffer(0);
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		glActiveTexture(GL_TEXTURE0);
	}

	bool PostProcess::ensurePostTargets(int width, int height, unsigned requiredCount)
	{
		if (width <= 0 || height <= 0)
			return false;
        requiredCount = (std::clamp)(requiredCount, 1u, 2u);
        const bool sameSize = width == m_postWidth && height == m_postHeight;
        if (sameSize && requiredCount <= m_postTargetCount)
            return m_postTargetsComplete;
        // A single post pass needs only the bloom output target. Allocate the
        // second texture only when two or more passes need ping-pong, then retain
        // it across toggles. Upgrading never reallocates the first target.
        const unsigned targetCount = (std::max)(requiredCount, m_postTargetCount);
        const unsigned first = sameSize && m_postTargetsComplete ? m_postTargetCount : 0u;
		if (m_postTextures[0] == 0)
		{
			glGenTextures(2, m_postTextures);
			glGenFramebuffers(2, m_postFrameBuffers);
		}
		bool complete = true;
		glActiveTexture(GL_TEXTURE0);
		for (unsigned int i = first; i < targetCount; ++i)
		{
			glBindTexture(GL_TEXTURE_2D, m_postTextures[i]);
			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA, GL_FLOAT, nullptr);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
			glBindFramebuffer(GL_FRAMEBUFFER, m_postFrameBuffers[i]);
			glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_postTextures[i], 0);
			glDrawBuffer(GL_COLOR_ATTACHMENT0);
			complete = (glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE) && complete;
		}
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		m_postWidth = width;
		m_postHeight = height;
		m_postTargetsComplete = complete;
        m_postTargetCount = targetCount;
		if (!complete)
			Debug::Error("Post processing color framebuffer is incomplete.");
		return complete;
	}

	void PostProcess::resize(int width, int height)
	{
		if (m_postTextures[0] != 0 && width > 0 && height > 0)
			ensurePostTargets(width, height, m_postTargetCount);
		for (unsigned int i = 0; i < m_externalModules.size(); ++i)
		{
			m_externalModules[i]->resize(width, height);
		}
		for (unsigned int i = 0; i < m_modules.size(); ++i)
		{
			m_modules[i]->resize(width, height);
		}
	}

	void PostProcess::release()
	{
		for (int i = 0; i < m_modules.size(); i++)
		{
			m_modules[i]->release();
			delete m_modules[i];
		}		
		m_modules.clear();
		m_bloom = nullptr;
		m_depthOfField = nullptr;
		m_colorTint = nullptr;
        m_environmentEffects = nullptr;
        m_environmentState = {};
#ifdef GE_ENABLE_OIL_PAINTING
		m_oilPainting = nullptr;
#endif
		glDeleteTextures(2, m_postTextures);
		glDeleteFramebuffers(2, m_postFrameBuffers);
		for (auto& texture : m_postTextures) texture = 0;
		for (auto& buffer : m_postFrameBuffers) buffer = 0;
		m_postWidth = m_postHeight = 0;
        m_postTargetCount = 0;
		m_postTargetsComplete = false;
		delete m_setting;
		m_setting = nullptr;
	}

	PPSetting* PostProcess::getPPSetting()
	{
		return m_setting;
	}
}