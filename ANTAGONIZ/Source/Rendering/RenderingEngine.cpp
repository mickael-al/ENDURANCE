#include "glcore.hpp"
#include "RenderingEngine.hpp"
#include "Debug.hpp"
#include "PointeurClass.hpp"
#include "GraphicsDataMisc.hpp"
#include "Window.hpp"
#include "TextureManager.hpp"
#include "MaterialManager.hpp"
#include "ModelManager.hpp"
#include "CameraManager.hpp"
#include "GraphiquePipelineManager.hpp"
#include "Hud.hpp"
#include "Model.hpp"
#include "Materials.hpp"
#include "GraphiquePipeline.hpp"
#include "ShapeBufferBase.hpp"
#include "LightManager.hpp"
#include "ShaderDataMisc.hpp"
#include "ShaderPair.hpp"
#include "CustomRenderer.hpp"
#include "FrameBuffer.hpp"
#include "Textures.hpp"
#include "Lights.hpp"
#include "SSAOBuffer.hpp"
#include "Camera.hpp"
#include "PostProcess.hpp"
#include <algorithm>
#include <stack>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include "Skybox.hpp"
#include "SkinnedTransformManager.hpp"

namespace
{
	bool hasDrawableModel(const std::vector<Ge::Model*>& models)
	{
		return std::any_of(
			models.begin(),
			models.end(),
			[](const Ge::Model* model)
			{
				return model != nullptr && model->getDraw();
			});
	}
}

namespace Ge
{
#ifdef OPENGL_DEBUG

	static void APIENTRY OpenGLDebugCallback(
		GLenum source,
		GLenum type,
		GLuint id,
		GLenum severity,
		GLsizei length,
		const GLchar* message,
		const void* userParam)
	{
		const char* sourceStr = "UNKNOWN";
		switch (source)
		{
		case GL_DEBUG_SOURCE_API:             sourceStr = "API"; break;
		case GL_DEBUG_SOURCE_WINDOW_SYSTEM:   sourceStr = "WINDOW"; break;
		case GL_DEBUG_SOURCE_SHADER_COMPILER: sourceStr = "SHADER"; break;
		case GL_DEBUG_SOURCE_THIRD_PARTY:     sourceStr = "THIRD PARTY"; break;
		case GL_DEBUG_SOURCE_APPLICATION:     sourceStr = "APPLICATION"; break;
		case GL_DEBUG_SOURCE_OTHER:           sourceStr = "OTHER"; break;
		}

		const char* typeStr = "UNKNOWN";
		switch (type)
		{
		case GL_DEBUG_TYPE_ERROR:               typeStr = "ERROR"; break;
		case GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR: typeStr = "DEPRECATED"; break;
		case GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR:  typeStr = "UNDEFINED"; break;
		case GL_DEBUG_TYPE_PORTABILITY:         typeStr = "PORTABILITY"; break;
		case GL_DEBUG_TYPE_PERFORMANCE:         typeStr = "PERFORMANCE"; return;//break;
		case GL_DEBUG_TYPE_MARKER:              typeStr = "MARKER"; break;
		case GL_DEBUG_TYPE_PUSH_GROUP:          typeStr = "PUSH GROUP"; break;
		case GL_DEBUG_TYPE_POP_GROUP:           typeStr = "POP GROUP"; break;
		case GL_DEBUG_TYPE_OTHER:               typeStr = "OTHER"; break;
		}

		const char* severityStr = "UNKNOWN";
		switch (severity)
		{
		case GL_DEBUG_SEVERITY_HIGH:         severityStr = "HIGH"; break;
		case GL_DEBUG_SEVERITY_MEDIUM:       severityStr = "MEDIUM"; break;
		case GL_DEBUG_SEVERITY_LOW:          severityStr = "LOW"; break;
		case GL_DEBUG_SEVERITY_NOTIFICATION: severityStr = "NOTIFICATION"; break;
		}

		std::cout
			<< "[OpenGL][" << severityStr << "][" << typeStr << "] "
			<< message
			<< " (Source: " << sourceStr << ", ID: " << id << ")"
			<< std::endl;
	}

	#endif

    RenderingEngine::RenderingEngine(GraphicsDataMisc * graphicsDataMisc)
    {
		m_graphicsDataMisc = graphicsDataMisc;
		m_frameBuffer = new FrameBuffer();
		m_ssaoBuffer = new SSAOBuffer();
		m_postProcess = new PostProcess();
		m_window = new Window(m_frameBuffer, m_postProcess);
		m_textureManager = new TextureManager();
		m_materialManager = new MaterialManager();
		m_modelManager = new ModelManager();
		m_cameraManager = new CameraManager();
		m_graphiquePipelineManager = new GraphiquePipelineManager();
		m_hud = new Hud();
		m_lightManager = new LightManager();
		m_shaderDataMisc = new ShaderDataMisc();		
		m_skybox = new Skybox();
		m_skinnedTransformManager = new SkinnedTransformManager();
    }

	RenderingEngine::~RenderingEngine()
	{
		delete m_skinnedTransformManager;
		delete m_skybox;
		delete m_postProcess;
		delete m_frameBuffer;
		delete m_ssaoBuffer;
		delete m_shaderDataMisc;
		delete m_lightManager;
		delete m_cameraManager;
		delete m_modelManager;
		delete m_materialManager;
		delete m_textureManager;
		delete m_window;			
		m_graphicsDataMisc = nullptr;		
	}

	glm::vec3 RenderingEngine::getWorldCoordinates(int mouseX, int mouseY)
	{
		int screenWidth = m_ptrClass->settingManager->getWindowWidth();
		int screenHeight = m_ptrClass->settingManager->getWindowHeight();
		float depth;
		glBindFramebuffer(GL_FRAMEBUFFER, m_frameBuffer->getFrameBuffer());
		glReadPixels(mouseX, screenHeight - mouseY, 1, 1, GL_DEPTH_COMPONENT, GL_FLOAT, &depth);
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
	
		float ndcX = (2.0f * mouseX) / screenWidth - 1.0f;
		float ndcY = 1.0f - (2.0f * mouseY) / screenHeight;
		float ndcZ = 2.0f * depth - 1.0f;

		glm::vec4 clipSpacePos(ndcX, ndcY, ndcZ, 1.0f);

		Camera* cam = m_cameraManager->getCurrentCamera();
		glm::mat4 invProjectionMatrix = glm::inverse(cam->getProjectionMatrix());
		glm::vec4 viewSpacePos = invProjectionMatrix * clipSpacePos;
		viewSpacePos /= viewSpacePos.w;

		glm::mat4 invViewMatrix = glm::inverse(cam->getViewMatrix());
		glm::vec4 worldSpacePos = invViewMatrix * viewSpacePos;

		return glm::vec3(worldSpacePos);
	}

	Ray RenderingEngine::screenPointToRay(int mouseX, int mouseY)
	{
		int w = m_ptrClass->settingManager->getWindowWidth();
		int h = m_ptrClass->settingManager->getWindowHeight();

		float x = (2.0f * mouseX) / w - 1.0f;
		float y = 1.0f - (2.0f * mouseY) / h;

		glm::vec4 rayClip(x, y, -1.0f, 1.0f);

		Camera* cam = m_cameraManager->getCurrentCamera();

		glm::vec4 rayView =
			glm::inverse(cam->getProjectionMatrix()) * rayClip;
		rayView = glm::vec4(rayView.x, rayView.y, -1.0f, 0.0f);

		glm::vec3 rayWorld =
			glm::normalize(glm::vec3(
				glm::inverse(cam->getViewMatrix()) * rayView
			));

		Ray ray;
		ray.origin = cam->getPosition();
		ray.direction = rayWorld;
		return ray;
	}


    bool RenderingEngine::initialize(ptrClass *p_ptrClass)
    {
        if (p_ptrClass == nullptr)
        {
            Debug::Error("ptrClass nullptr RenderingEngine");
            return false;
        }
		m_ptrClass = p_ptrClass;
		m_ptrClass->hud = m_hud;
		m_ptrClass->textureManager = m_textureManager;
		m_ptrClass->modelManager = m_modelManager;
		m_ptrClass->cameraManager = m_cameraManager;
		m_ptrClass->materialManager = m_materialManager;
		m_ptrClass->graphiquePipelineManager = m_graphiquePipelineManager;
		m_ptrClass->lightManager = m_lightManager;
		m_ptrClass->postProcess = m_postProcess;
		m_ptrClass->skybox = m_skybox;
		m_ptrClass->skinnedTransformManager = m_skinnedTransformManager;
		m_ptrClass->renderingEngine = this;		

        Debug::Info("Initialisation du moteur de rendu");

		if (!m_window->initialize(p_ptrClass->settingManager->getWindowWidth(), p_ptrClass->settingManager->getWindowHeight(), p_ptrClass->settingManager->getName(), p_ptrClass->settingManager->getIconPath(), p_ptrClass->settingManager->getVsync(), m_graphicsDataMisc))
		{
			Debug::INITFAILED("Window");
			return false;
		}

		glewExperimental = GL_TRUE;
		GLenum error = glewInit();
		if (error != GLEW_OK)
		{
			Debug::Error("glewInit error code : %d", error);
			return false;
		}
		Debug::INITSUCCESS("OpenGL");    
#ifdef OPENGL_DEBUG
		int flags;
		glGetIntegerv(GL_CONTEXT_FLAGS, &flags);
		if (flags & GL_CONTEXT_FLAG_DEBUG_BIT)
		{
			Debug::Warn("Context Dbg active");
		}

		glEnable(GL_DEBUG_OUTPUT);
		glDebugMessageCallback(OpenGLDebugCallback, nullptr);
		glDebugMessageControl(
			GL_DONT_CARE,
			GL_DONT_CARE,
			GL_DEBUG_SEVERITY_NOTIFICATION,
			0,
			nullptr,
			GL_FALSE);
		glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
#endif

		if (!m_frameBuffer->initialize(m_graphicsDataMisc))
		{
			Debug::INITFAILED("FrameBuffer");
			return false;
		}

		if (!m_textureManager->initialize(m_graphicsDataMisc))
		{
			Debug::INITFAILED("TextureManager");
			return false;
		}

		if (!m_graphiquePipelineManager->initialize(m_graphicsDataMisc))
		{
			Debug::INITFAILED("GraphiquePipelineManager");
			return false;
		}

		if (!m_materialManager->initialize(m_graphicsDataMisc))
		{
			Debug::INITFAILED("MaterialManager");
			return false;
		}

		if (!m_modelManager->initialize(m_graphicsDataMisc))
		{
			Debug::INITFAILED("ModelManager");
			return false;
		}

		if(!m_cameraManager->initialize(m_graphicsDataMisc))
		{
			Debug::INITFAILED("CameraManager");
			return false;
		}	

		if(!m_hud->initialize(m_graphicsDataMisc))
		{
			Debug::INITFAILED("ImGUI");
			return false;
		}

		if (!m_lightManager->initialize(m_graphicsDataMisc))
		{
			Debug::INITFAILED("LightManager");
			return false;
		}

		if (!m_shaderDataMisc->initialize(m_graphicsDataMisc))
		{
			Debug::INITFAILED("LightManager");
			return false;
		}
		if (!m_ssaoBuffer->initialize(m_graphicsDataMisc))
		{
			Debug::INITFAILED("SSAOManager");
			return false;
		}
		if (!m_postProcess->initialize(m_graphicsDataMisc))
		{
			Debug::INITFAILED("PostProcess");
			return false;
		}
		if (!m_skybox->initialize(m_graphicsDataMisc))
		{
			Debug::INITFAILED("Skybox");
			return false;
		}
		if (!m_skinnedTransformManager->initialize(m_ptrClass))
		{
			Debug::INITFAILED("SkinnedTransformManager");
			return false;
		}
        Debug::INITSUCCESS("RenderingEngine");

        return true;
    }
	
    void RenderingEngine::release()
    {        	
		RenderingEngine::m_skinnedTransformManager->release();
		RenderingEngine::m_skybox->release();
		RenderingEngine::m_postProcess->release();
		RenderingEngine::m_shaderDataMisc->release();
		RenderingEngine::m_ssaoBuffer->release();
		RenderingEngine::m_lightManager->release();
		RenderingEngine::m_hud->release();
		RenderingEngine::m_cameraManager->release();
		RenderingEngine::m_modelManager->release();
		RenderingEngine::m_materialManager->release();
		RenderingEngine::m_graphiquePipelineManager->release();
		RenderingEngine::m_textureManager->release();
		RenderingEngine::m_frameBuffer->release();
		RenderingEngine::m_window->release();
		m_ptrClass = nullptr;
        Debug::RELEASESUCCESS("RenderingEngine");
    }

	FrameBuffer* RenderingEngine::getFrameBuffer() const
	{
		return m_frameBuffer;
	}

	Window* RenderingEngine::getWindow() const
	{
		return m_window;
	}

	void RenderingEngine::addCallback(void* owner, std::function<void(int)> func)
	{
		callbacks[owner] = std::move(func);
	}

	void RenderingEngine::addTransparencyCallback(void* owner, std::function<void(bool*)> func)
	{
		callbacksTransparency[owner] = std::move(func);
	}

	void RenderingEngine::removeCallback(void* owner)
	{
		callbacks.erase(owner);
	}

	void RenderingEngine::removeTransparencyCallback(void* owner)
	{
		callbacksTransparency.erase(owner);
	}

    void RenderingEngine::drawFrame()
    {
		if (m_skinnedTransformManager != nullptr && m_ptrClass != nullptr && m_ptrClass->time != nullptr)
		{
			m_skinnedTransformManager->updateAnimations(m_ptrClass->time->getDeltaTime());
		}
		m_cameraManager->updateFlyCam();
		const auto& customRenderers = m_graphiquePipelineManager->getCustomRenderer();
		for (CustomRenderer* renderer : customRenderers)
		{
			if (renderer != nullptr) renderer->prepareRenderer();
		}
		m_shaderDataMisc->update(m_cameraManager->getCurrentCamera());
		glBindFramebuffer(GL_FRAMEBUFFER, m_frameBuffer->getFrameBuffer());
		glViewport(0, 0, m_graphicsDataMisc->str_width, m_graphicsDataMisc->str_height);
		const glm::vec4 & clear = m_ptrClass->settingManager->getClearColor();
		glClearColor(clear.x, clear.y, clear.z, clear.w);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		m_skybox->render(m_cameraManager->getSSBO());
		glEnable(GL_CULL_FACE);
		glEnable(GL_DEPTH_TEST);//!!! DO NOT DELETE THIS LINE !!! : total destruction of the application and end of the universe and the parallel universe
		glDepthMask(GL_TRUE);
		glDisable(GL_BLEND);

		std::unordered_map<Materials*, std::unordered_map<ShapeBuffer*, std::vector<Model*>>> & instanced_models = m_modelManager->getInstancedModels();		
		const std::vector<Materials *> & matlist = m_materialManager->getMaterials();
		bool transparency = !callbacksTransparency.empty();
		ShapeBuffer * currentShape = nullptr;
		GraphiquePipeline * currentPipeline = nullptr;
		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, m_cameraManager->getSSBO());
		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, m_modelManager->getSSBO());		
		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, m_materialManager->getSSBO());
		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, m_shaderDataMisc->getSSBO());
		for (const auto & call : callbacks)
		{
			call.second(-1);
		}
		for (CustomRenderer* renderer : customRenderers)
		{
			if (renderer != nullptr)
			{
				glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, m_cameraManager->getSSBO());
				renderer->drawRenderer();
			}
		}
		if (!callbacks.empty() || !customRenderers.empty())
		{
			glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, m_cameraManager->getSSBO());
			glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, m_modelManager->getSSBO());
			glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, m_materialManager->getSSBO());
			glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, m_shaderDataMisc->getSSBO());
		}
		bool currentDepthTest = true;
		bool currentDepthWrite = true;
		for (auto& material : matlist)
		{
			if (material == nullptr || !material->getDraw() || material->getColor().a == 0.0f)
			{
				continue;
			}
			currentPipeline = material->getPipeline();
			unsigned int program = currentPipeline->getProgram();
			glUseProgram(program);
			ShaderPair* sp = currentPipeline->getShaderPair();
			if (sp->transparency)
			{
				transparency = true;
				break;
			}
			auto& mku = instanced_models[material];
			glActiveTexture(GL_TEXTURE0);
			glBindTexture(GL_TEXTURE_2D, material->getAlbedoTexture()->getTextureID());
			glActiveTexture(GL_TEXTURE1);
			glBindTexture(GL_TEXTURE_2D, material->getNormalTexture()->getTextureID());
			glActiveTexture(GL_TEXTURE2);
			glBindTexture(GL_TEXTURE_2D, material->getMetallicTexture()->getTextureID());
			glActiveTexture(GL_TEXTURE3);
			glBindTexture(GL_TEXTURE_2D, material->getRoughnessTexture()->getTextureID());
			glActiveTexture(GL_TEXTURE4);
			glBindTexture(GL_TEXTURE_2D, material->getOclusionTexture()->getTextureID());
			if (material->getDepthTest() != currentDepthTest)
			{
				currentDepthTest = material->getDepthTest();
				if (currentDepthTest)
				{
					glEnable(GL_DEPTH_TEST);
				}
				else
				{
					glDisable(GL_DEPTH_TEST);
				}
			}

			if (material->getDepthWrite() != currentDepthWrite)
			{
				currentDepthWrite = material->getDepthWrite();
				glDepthMask(currentDepthWrite ? GL_TRUE : GL_FALSE);
			}

			if (sp->cullMode == 2)
			{
				glDisable(GL_CULL_FACE);
			}

			std::vector<unsigned int> & assbo = material->getAditionalSSBO();
			for (int j = 0; j < assbo.size(); j++)
			{
				glBindBufferBase(GL_SHADER_STORAGE_BUFFER, j+4, assbo[j]);				
			}
			for (auto& buffer : mku)
			{
				if (!buffer.second.empty() && hasDrawableModel(buffer.second))
				{
					currentShape = buffer.first;
					glBindVertexArray(currentShape->getVAO());
					glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, currentShape->getIBO());
					glUniform1i(glGetUniformLocation(program, "offsetUbo"), buffer.second[0]->getIndex());
					glDrawElementsInstanced(GL_TRIANGLES, currentShape->getIndiceSize(), GL_UNSIGNED_INT, nullptr, buffer.second.size() + material->getAditionalInstanced());
				}
			}
			if (sp->cullMode == 2)
			{
				glEnable(GL_CULL_FACE);
			}
		}		
		if (!currentDepthWrite)
		{
			currentDepthWrite = true;
			glDepthMask(GL_TRUE);
		}
		if (transparency)
		{
			glMemoryBarrier(GL_FRAMEBUFFER_BARRIER_BIT);
			glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, 0, 0);
		}
		/*  Shadow  */
		
		glDisable(GL_CULL_FACE);
		glEnable(GL_DEPTH_TEST);
		glDepthMask(GL_TRUE);
		glDepthFunc(GL_LESS);
		glClearDepth(1.0);
		currentDepthTest = true;
		glDisable(GL_BLEND);		
		const std::vector<unsigned int>& frameBufferDepthShadow = m_lightManager->getFrameShadowBuffer();
		for (int i = 0; i < frameBufferDepthShadow.size(); i++)
		{
			glBindFramebuffer(GL_FRAMEBUFFER, frameBufferDepthShadow[i]);
			glClear(GL_DEPTH_BUFFER_BIT);
			glViewport(0, 0, TEXTURE_DIM, TEXTURE_DIM);
			glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, m_lightManager->getSsboShadow());
			glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, m_modelManager->getSSBO());
			glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, m_materialManager->getSSBO());
			currentPipeline = m_graphicsDataMisc->str_default_pipeline_shadow;
			unsigned int program = currentPipeline->getProgram();
			glUseProgram(program);
			int uboU = glGetUniformLocation(program, "offsetUbo");
			int shadowU = glGetUniformLocation(program, "offsetShadow");

			for (auto& material : matlist)
			{
				if (material == nullptr || !material->getDraw() || !material->getCastShadow() || material->getColor().a == 0.0f)
				{
					continue;
				}
				if (!material->getDepthTest()) continue;
				GraphiquePipeline* sp = material->getShadowPipeline();
				if (sp != currentPipeline && sp != nullptr)
				{
					currentPipeline = sp;
					program = currentPipeline->getProgram();
					glUseProgram(program);
					uboU = glGetUniformLocation(program, "offsetUbo");
					shadowU = glGetUniformLocation(program, "offsetShadow");
					glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, m_shaderDataMisc->getSSBO());
				}
				// Buffers belong to the material/instance, not to the pipeline.
				// Consecutive skinned or cloth actors can share the same shader.
				std::vector<unsigned int>& assbo = material->getAditionalSSBO();
				for (int j = 0; j < assbo.size(); j++)
				{
					glBindBufferBase(GL_SHADER_STORAGE_BUFFER, j + 4, assbo[j]);
				}
				auto& mku = instanced_models[material];
				glActiveTexture(GL_TEXTURE0);
				glBindTexture(GL_TEXTURE_2D, material->getAlbedoTexture()->getTextureID());
				for (auto& buffer : mku)
				{
					if (!buffer.second.empty() && hasDrawableModel(buffer.second))
					{
						currentShape = buffer.first;
						glBindVertexArray(currentShape->getVAO());
						glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, currentShape->getIBO());
						glUniform1i(uboU, buffer.second[0]->getIndex());
						glUniform1i(shadowU, i);
						glDrawElementsInstanced(GL_TRIANGLES, currentShape->getIndiceSize(), GL_UNSIGNED_INT, nullptr, buffer.second.size() + material->getAditionalInstanced());
					}
				}
			}
			for (const auto & call : callbacks)
			{
				call.second(i);
			}
			for (CustomRenderer* renderer : customRenderers)
			{
				if (renderer != nullptr)
				{
					glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, m_lightManager->getSsboShadow());
					renderer->drawShadowRenderer(static_cast<unsigned int>(i));
				}
			}
		}
		glCullFace(GL_BACK);
		/*  Shadow  */
		
		glBindFramebuffer(GL_FRAMEBUFFER, m_frameBuffer->getFowardFrameBuffer());		
		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, m_cameraManager->getSSBO());
		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, m_lightManager->getSSBO());
		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, m_shaderDataMisc->getSSBO());
		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, m_lightManager->getSsboShadow());
		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, m_ssaoBuffer->getSSBO());
		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 5, m_lightManager->getSsboPlaneHidden());
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, m_frameBuffer->getPosition());
		glActiveTexture(GL_TEXTURE1);
		glBindTexture(GL_TEXTURE_2D, m_frameBuffer->getNormal());
		glActiveTexture(GL_TEXTURE2);
		glBindTexture(GL_TEXTURE_2D, m_frameBuffer->getColor());
		glActiveTexture(GL_TEXTURE3);
		glBindTexture(GL_TEXTURE_2D, m_frameBuffer->getOther());
		glActiveTexture(GL_TEXTURE4);
		glBindTexture(GL_TEXTURE_2D_ARRAY, m_lightManager->getTextureShadowArray());
		glActiveTexture(GL_TEXTURE5);
		glBindTexture(GL_TEXTURE_2D, m_ssaoBuffer->getNoiseTexture());
		glActiveTexture(GL_TEXTURE6);
		glBindTexture(GL_TEXTURE_CUBE_MAP, m_skybox->getIrradianceMap());
		glActiveTexture(GL_TEXTURE7);
		glBindTexture(GL_TEXTURE_CUBE_MAP, m_skybox->getPrefilterMap());
		glActiveTexture(GL_TEXTURE8);
		glBindTexture(GL_TEXTURE_2D, m_skybox->getBrdfLUTTexture());
		glUseProgram(m_graphicsDataMisc->str_default_pipeline_forward->getProgram());
		glViewport(0, 0, m_graphicsDataMisc->str_width, m_graphicsDataMisc->str_height);

		ShapeBuffer* sb = m_modelManager->getFullScreenTriangle();
		glBindVertexArray(sb->getVAO());
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, sb->getIBO());
		glDrawElements(GL_TRIANGLES, sb->getIndiceSize(), GL_UNSIGNED_INT, 0);
		
		if (transparency)
		{			
			glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, m_frameBuffer->getDepth(), 0);
			glEnable(GL_DEPTH_TEST);
			glEnable(GL_CULL_FACE);
			glEnable(GL_BLEND);
			glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
			glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, m_modelManager->getSSBO());
			glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, m_materialManager->getSSBO());
			glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, m_shaderDataMisc->getSSBO());
			glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, m_lightManager->getSSBO());
			glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 5, m_lightManager->getSsboShadow());		
			glActiveTexture(GL_TEXTURE5);
			glBindTexture(GL_TEXTURE_2D_ARRAY, m_lightManager->getTextureShadowArray());

			glm::vec2 invRes(1.0f / m_graphicsDataMisc->str_width, 1.0f / m_graphicsDataMisc->str_height);
			
			for (int i = matlist.size() - 1; i > 0; --i)
			{
				Materials* mat = matlist[i];
				if (mat == nullptr || !mat->getDraw() || mat->getColor().a == 0.0f)
				{
					continue;
				}
				currentPipeline = mat->getPipeline();
				unsigned int program = currentPipeline->getProgram();
				glUseProgram(program);
				ShaderPair* sp = currentPipeline->getShaderPair();
				if (!sp->transparency)
				{
					break;
				}
				if (mat->getDepthWrite() != currentDepthWrite)
				{
					currentDepthWrite = mat->getDepthWrite();
					glDepthMask(currentDepthWrite ? GL_TRUE : GL_FALSE);
				}
				auto& mku = instanced_models[mat];
				glActiveTexture(GL_TEXTURE0);
				glBindTexture(GL_TEXTURE_2D, mat->getAlbedoTexture()->getTextureID());
				glActiveTexture(GL_TEXTURE1);
				glBindTexture(GL_TEXTURE_2D, mat->getNormalTexture()->getTextureID());
				glActiveTexture(GL_TEXTURE2);
				glBindTexture(GL_TEXTURE_2D, mat->getMetallicTexture()->getTextureID());
				glActiveTexture(GL_TEXTURE3);
				glBindTexture(GL_TEXTURE_2D, mat->getRoughnessTexture()->getTextureID());
				glActiveTexture(GL_TEXTURE4);
				glBindTexture(GL_TEXTURE_2D, mat->getOclusionTexture()->getTextureID());				

				if (sp->cullMode == 2)
				{
					glDisable(GL_CULL_FACE);
				}

				std::vector<unsigned int>& assbo = mat->getAditionalSSBO();
				for (int j = 0; j < assbo.size(); j++)
				{
					glBindBufferBase(GL_SHADER_STORAGE_BUFFER, j + 6, assbo[j]);
				}
				for (auto& buffer : mku)
				{
					if (!buffer.second.empty() && hasDrawableModel(buffer.second))
					{
						currentShape = buffer.first;
						glBindVertexArray(currentShape->getVAO());
						glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, currentShape->getIBO());
						glUniform1i(glGetUniformLocation(program, "offsetUbo"), buffer.second[0]->getIndex());
						glDrawElementsInstanced(GL_TRIANGLES, currentShape->getIndiceSize(), GL_UNSIGNED_INT, nullptr, buffer.second.size() + mat->getAditionalInstanced());
						glTextureBarrier();
					}
				}
				if (sp->cullMode == 2)
				{
					glEnable(GL_CULL_FACE);
				}
			}
			for (const auto & call : callbacksTransparency)
			{
				call.second(&currentDepthWrite);
			}
			glMemoryBarrier(GL_FRAMEBUFFER_BARRIER_BIT);
			glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, 0, 0);
			if (!currentDepthWrite)
			{
				currentDepthWrite = true;
				glDepthMask(GL_TRUE);
			}
			glDisable(GL_BLEND);
		}

		// Tone mapping/bloom, then optional depth of field and tint. HUD stays sharp.
		m_postProcess->compute(m_frameBuffer->getFowardFrameBuffer(), m_frameBuffer->getColorFoward(), m_frameBuffer->getDepth(), m_modelManager->getDefferedQuad());
		
		m_hud->render();		
		if (transparency)
		{
			glBindFramebuffer(GL_FRAMEBUFFER, m_frameBuffer->getFrameBuffer());
			glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, m_frameBuffer->getDepth(), 0);
		}
		glfwSwapBuffers(m_graphicsDataMisc->str_window);
    }
}
