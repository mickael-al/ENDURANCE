#include "Bloom.hpp"
#include "glcore.hpp"
#include "GraphicsDataMisc.hpp"
#include "Debug.hpp"
#include "GraphiquePipelineManager.hpp"
#include "GraphiquePipeline.hpp"
#include "Engine.hpp"
#include "PointeurClass.hpp"
#include "ShapeBuffer.hpp"
#include <algorithm>
#include <climits>

namespace Ge
{	
	bool Bloom::initialize(GraphicsDataMisc* gdm)
	{
        const ptrClass& pc = Engine::getPtrClass();
		m_gdm = gdm;
                
        if (gdm->str_width > (unsigned int)INT_MAX || gdm->str_height > (unsigned int)INT_MAX) 
        {
            Debug::Error("Window size conversion overflow - cannot build bloom FBO!");
            return false;
        }
        glm::ivec2 mipIntSize((int)gdm->str_width, (int)gdm->str_height);
        mMipChain.reserve(mipChainLength);

        for (unsigned int i = 0; i < mipChainLength; i++)
        {
            bloomMip mip;

            mipIntSize = glm::max(mipIntSize / 2, glm::ivec2(1));
            mip.intSize = mipIntSize;

            glGenTextures(1, &mip.texture);
            glBindTexture(GL_TEXTURE_2D, mip.texture);
            // we are downscaling an HDR color buffer, so we need a float texture format
            glTexImage2D(GL_TEXTURE_2D, 0, GL_R11F_G11F_B10F,
                mipIntSize.x, mipIntSize.y,
                0, GL_RGB, GL_FLOAT, nullptr);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

            mMipChain.emplace_back(mip);
        }

        m_downSampleShader = pc.graphiquePipelineManager->createPipeline("../Asset/Shader/downsampling.fs.glsl", "../Asset/Shader/pp.vs.glsl");
        m_upSampleShader = pc.graphiquePipelineManager->createPipeline("../Asset/Shader/upsampling.fs.glsl", "../Asset/Shader/pp.vs.glsl");
        m_tomeGammaShader = pc.graphiquePipelineManager->createPipeline("../Asset/Shader/tomeMapping.fs.glsl", "../Asset/Shader/pp.vs.glsl");
        srcResolutionLocation = glGetUniformLocation(m_downSampleShader->getProgram(), "srcResolution");
        thresholdLocation = glGetUniformLocation(m_downSampleShader->getProgram(), "threshold");
        filterRadiusLocation = glGetUniformLocation(m_upSampleShader->getProgram(), "filterRadius");
        gammaLocation = glGetUniformLocation(m_tomeGammaShader->getProgram(), "gamma");
        exposureLocation = glGetUniformLocation(m_tomeGammaShader->getProgram(), "exposure");        
        intensityLocation = glGetUniformLocation(m_tomeGammaShader->getProgram(), "intensity");
        //threshold;

		return true;
	}
	
	void Bloom::release()
	{
        const ptrClass& pc = Engine::getPtrClass();
        for (unsigned int i = 0; i < mMipChain.size(); i++)
        {
            glDeleteTextures(1, &mMipChain[i].texture);
        }
        mMipChain.clear();
        
        pc.graphiquePipelineManager->destroyPipeline(m_downSampleShader);
        pc.graphiquePipelineManager->destroyPipeline(m_upSampleShader);
        pc.graphiquePipelineManager->destroyPipeline(m_tomeGammaShader);
	}

	void Bloom::resize(int width, int height)
	{
        glm::ivec2 mipIntSize(width, height);
        for (unsigned int i = 0; i < mMipChain.size(); i++)
        {
            mipIntSize = glm::max(mipIntSize / 2, glm::ivec2(1));
            if (mMipChain[i].intSize == mipIntSize)
            {
                continue;
            }
            mMipChain[i].intSize = mipIntSize;
            glBindTexture(GL_TEXTURE_2D, mMipChain[i].texture);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_R11F_G11F_B10F, mipIntSize.x, mipIntSize.y, 0, GL_RGB, GL_FLOAT, nullptr);
        }
	}

	void Bloom::compute(unsigned int frameBuffer, unsigned int texture,unsigned int depthtexture, ShapeBuffer* sb, PPSetting* settings)
	{
        if (m_gdm->str_width <= 0 || m_gdm->str_height <= 0)
        {
            return;
        }
        const glm::ivec2 size(m_gdm->str_width, m_gdm->str_height);
        const bool useBloom = settings->bloom && settings->bloom_intensity > 0.0f;
        // Keep the existing quarter-resolution bloom appearance. The last
        // upsample into mip 0 was never sampled by the composition shader.
        const int compositeMip = 1;
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        glDisable(GL_BLEND);
        glBindVertexArray(sb->getVAO());
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, sb->getIBO());
        float filterRadius = settings->bloom_filter;
        if (useBloom)
        {
            glBindFramebuffer(GL_FRAMEBUFFER, frameBuffer);

            glUseProgram(m_downSampleShader->getProgram());
            glUniform2f(srcResolutionLocation, size.x, size.y);

            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, texture);
            for (int i = 0; i < mMipChain.size(); i++)
            {
                const bloomMip& mip = mMipChain[i];
                glViewport(0, 0, mip.intSize.x, mip.intSize.y);
                glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, mip.texture, 0);

                glUniform1f(thresholdLocation, i == 0 ? settings->bloom_threshold : -1.0f);
                glDrawElements(GL_TRIANGLES, sb->getIndiceSize(), GL_UNSIGNED_INT, 0);

                // Set current mip resolution as srcResolution for next iteration            
                glUniform2f(srcResolutionLocation, (float)mip.intSize.x, (float)mip.intSize.y);
                // Set current mip as texture input for next iteration
                glBindTexture(GL_TEXTURE_2D, mip.texture);
            }

            glUseProgram(m_upSampleShader->getProgram());
            glUniform1f(filterRadiusLocation, filterRadius);

            glEnable(GL_BLEND);
            glBlendFunc(GL_ONE, GL_ONE);
            glBlendEquation(GL_FUNC_ADD);

            for (int i = (int)mMipChain.size() - 1; i > compositeMip; i--)
            {
                const bloomMip& mip = mMipChain[i];
                const bloomMip& nextMip = mMipChain[i - 1];

                // Bind viewport and texture from where to read
                glBindTexture(GL_TEXTURE_2D, mip.texture);

                // Set framebuffer render target (we write to this texture)
                glViewport(0, 0, nextMip.intSize.x, nextMip.intSize.y);
                glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                    GL_TEXTURE_2D, nextMip.texture, 0);

                // Render screen-filled quad of resolution of current mip
                glDrawElements(GL_TRIANGLES, sb->getIndiceSize(), GL_UNSIGNED_INT, 0);
            }
            // Restore the engine's HDR attachment for the next frame/modules.
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
        }
        // Disable additive blending
        glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA); // Restore if this was default
        glDisable(GL_BLEND);
        // With no later effects this still presents directly to the screen.
        // Otherwise tone mapping writes a separate color target for the next pass.
        glBindFramebuffer(GL_FRAMEBUFFER, m_outputFrameBuffer);
        glViewport(0, 0, size.x, size.y);
        glUseProgram(m_tomeGammaShader->getProgram());
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, texture);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, useBloom ? mMipChain[compositeMip].texture : 0);
        const float gamma = settings->gamma == nullptr ? 2.2f : *settings->gamma;
        glUniform1f(gammaLocation, (std::max)(gamma, 0.001f));
        glUniform1f(exposureLocation, settings->exposure);
        glUniform1f(intensityLocation, useBloom ? settings->bloom_intensity : 0.0f);
        
        glDrawElements(GL_TRIANGLES, sb->getIndiceSize(), GL_UNSIGNED_INT, 0);
        glActiveTexture(GL_TEXTURE0);
	}

    void Bloom::onGui(PPSetting* t, PPSetting* s)
    {
        if (ImGui::TreeNodeEx("Bloom"))
        {
            if (ImGui::Checkbox("Active##Bloom", &s->bloom))
            {
                if (t != nullptr) { t->bloom = s->bloom; }
            }
            if (ImGui::DragFloat("Filter##Bloom", &s->bloom_filter, 0.01f))
            {
                if (t != nullptr) { t->bloom_filter = s->bloom_filter; }
            }
            if (ImGui::DragFloat("Threshold##Bloom", &s->bloom_threshold, 0.01f))
            {
                if (t != nullptr) { t->bloom_threshold = s->bloom_threshold; }
            }
            if (ImGui::DragFloat("Intensity##Bloom", &s->bloom_intensity, 0.01f))
            {
                if (t != nullptr) { t->bloom_intensity = s->bloom_intensity; }
            }
            ImGui::TreePop();
        }
    }
}
