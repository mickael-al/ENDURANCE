#include "glcore.hpp"
#include "LightManager.hpp"
#include "Lights.hpp"
#include "PointLight.hpp"
#include "DirectionalLight.hpp"
#include "SpotLight.hpp"
#include "Debug.hpp"
#include "GraphicsDataMisc.hpp"
#include <algorithm>
#include "ShadowMatrix.hpp"
#include "UniformBufferPlaneHidden.hpp"

bool LightManager::initialize(GraphicsDataMisc * gdm)
{
	m_gdm = gdm;
	glGenBuffers(1, &m_ssbo);
	glGenBuffers(1, &m_ssboShadow);
	glGenBuffers(1, &m_ssboPlaneHidden);
	m_gdm->str_ssbo.str_light = m_ssbo;
	m_gdm->str_ssbo.str_shadow = m_ssboShadow;
	m_gdm->str_ssbo.str_planeHidden = m_ssboPlaneHidden;

	glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_ssboPlaneHidden);
	glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(UniformBufferPlaneHidden), nullptr, GL_DYNAMIC_DRAW);
	m_gdm->str_dataMisc.lightCount = 0;
	glGenTextures(1, &m_textureShadowArray);
	glBindTexture(GL_TEXTURE_2D_ARRAY, m_textureShadowArray);	
	glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_DEPTH_COMPONENT32, TEXTURE_DIM, TEXTURE_DIM, 1, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
	// PCF compares individual depth samples before averaging visibility.
	glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	
	Debug::INITSUCCESS("LightManager");
	return true;
}

unsigned int LightManager::getSsboShadow() const
{
	return m_ssboShadow;
}

unsigned int LightManager::getSsboPlaneHidden() const
{
	return m_ssboPlaneHidden;
}

unsigned int LightManager::getTextureShadowArray() const
{
	return m_textureShadowArray;
}

const std::vector<unsigned int>& LightManager::getFrameShadowBuffer() const
{
	return m_frameBufferDepthShadow;
}

void LightManager::release()
{
	while (!m_lights.empty())
	{
		Lights* light = m_lights.back();
		// Destruction may rebuild shadow storage; only live lights may remain.
		m_lights.pop_back();
		delete light;
	}
	m_gdm->str_dataMisc.lightCount = 0;
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
	glDeleteBuffers(1, &m_ssbo);
	glDeleteBuffers(1, &m_ssboShadow);
	glDeleteBuffers(1, &m_ssboPlaneHidden);
	
	glDeleteFramebuffers(m_frameBufferDepthShadow.size(), m_frameBufferDepthShadow.data());
	glDeleteTextures(1, &m_textureShadowArray);
	Debug::RELEASESUCCESS("LightManager");
}

void LightManager::updateShadowCascadeMatrix()
{
	for (int i = 0; i < m_lights.size(); i++)
	{
		if (m_lights[i]->isActive() && m_lights[i]->getshadow() && m_lights[i]->getStatus() == 0)
		{			
			m_lights[i]->mapMemoryShadow();			
		}
	}
}

void LightManager::updateStorageShadow()
{	
	int countTotal = 0;
	for (int i = 0; i < m_lights.size(); i++)
	{
		m_lights[i]->setShadowIndex(-1);
		if (m_lights[i]->isActive() && m_lights[i]->getshadow())
		{			
			m_lights[i]->setShadowIndex(countTotal);
			countTotal += m_lights[i]->getStatus() == 0 ? SHADOW_MAP_CASCADE_COUNT : (m_lights[i]->getStatus() == 1 ? SHADOW_MAP_CUBE_COUNT : SHADOW_MAP_SPOT_COUNT);
		}
	}
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_ssboShadow);
	glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(ShadowMatrix) * countTotal, nullptr, GL_DYNAMIC_DRAW);
	// Mapping active lights refreshes their transform and then their shadow matrices.
	updateStorage();
	glDeleteTextures(1, &m_textureShadowArray);
	glGenTextures(1, &m_textureShadowArray);
	glBindTexture(GL_TEXTURE_2D_ARRAY, m_textureShadowArray);

	glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_DEPTH_COMPONENT32, TEXTURE_DIM, TEXTURE_DIM, std::max(1, countTotal), 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
	// PCF compares individual depth samples before averaging visibility.
	glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

	if (countTotal < m_frameBufferDepthShadow.size())
	{
		int reste = m_frameBufferDepthShadow.size() - countTotal;
		for (int i = 0; i < reste; i++)
		{			
			glDeleteFramebuffers(1, &m_frameBufferDepthShadow[m_frameBufferDepthShadow.size() - 1]);
			m_frameBufferDepthShadow.pop_back();
		}
	}

	unsigned int frameBufferID = 0;
	
	for (int i = 0; i < countTotal; ++i)
	{
		if (i >= m_frameBufferDepthShadow.size())
		{
			glGenFramebuffers(1, &frameBufferID);
			m_frameBufferDepthShadow.push_back(frameBufferID);
		}
		// The depth texture was replaced above. Reattach every existing FBO too,
		// otherwise changing shadow-casting lights leaves it on the old texture.
		glBindFramebuffer(GL_FRAMEBUFFER, m_frameBufferDepthShadow[i]);
		glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, m_textureShadowArray, 0, i);
		glDrawBuffer(GL_NONE);
		glReadBuffer(GL_NONE);
		if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
		{
			Debug::Error("Erreur lors de la creation du framebuffer d'ombre.");
		}
	}
	glBindFramebuffer(GL_FRAMEBUFFER, 0);

	glBindTexture(GL_TEXTURE_2D_ARRAY, m_textureShadowArray);
}

void LightManager::updateStoragePlaneHidden()
{
	std::vector<UniformBufferPlaneHidden> packedPlanes;
	std::vector<Lights*> activeLights;

	for (Lights* light : m_lights)
	{
		if (!light->isActive())
		{
			continue;
		}

		activeLights.push_back(light);
		if (light->m_hiddenPlanes.empty())
		{
			light->m_ubl.minPlaneID = -1;
			light->m_ubl.maxPlaneID = -1;
			continue;
		}

		light->m_ubl.minPlaneID = static_cast<int>(packedPlanes.size());
		packedPlanes.insert(packedPlanes.end(), light->m_hiddenPlanes.begin(), light->m_hiddenPlanes.end());
		light->m_ubl.maxPlaneID = static_cast<int>(packedPlanes.size());
	}

	glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_ssboPlaneHidden);
	if (packedPlanes.empty())
	{
		glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(UniformBufferPlaneHidden), nullptr, GL_DYNAMIC_DRAW);
	}
	else
	{
		glBufferData(
			GL_SHADER_STORAGE_BUFFER,
			sizeof(UniformBufferPlaneHidden) * packedPlanes.size(),
			packedPlanes.data(),
			GL_DYNAMIC_DRAW);
	}

	for (Lights* light : activeLights)
	{
		light->mapMemory();
	}
}

void LightManager::updateStorage()
{
	unsigned int activeCount = 0;
	for (Lights* light : m_lights)
	{
		if (light->isActive())
		{
			++activeCount;
		}
	}

	glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_ssbo);
	glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(UniformBufferLight) * activeCount, nullptr, GL_DYNAMIC_DRAW);
	unsigned int packedIndex = 0;
	for (Lights* light : m_lights)
	{
		if (!light->isActive())
		{
			continue;
		}
		light->setIndex(packedIndex++);
	}
	m_gdm->str_dataMisc.lightCount = activeCount;
	updateStoragePlaneHidden();
}
const std::vector<glm::mat4> LightManager::getShadowMatrix()
{
	std::vector<glm::mat4> allsm;
	for (int i = 0; i < m_lights.size(); i++)
	{
		if (!m_lights[i]->isActive() || !m_lights[i]->getshadow())
		{
			continue;
		}
		const std::vector<ShadowMatrix>& sm = m_lights[i]->getShadowMatrix();
		for (int j = 0; j < sm.size(); j++)
		{
			allsm.push_back(sm[j].projview);
		}
	}
	return allsm;
}

void LightManager::getShadowMatrix(std::vector<glm::mat4>* mats)
{
	for (int i = 0; i < m_lights.size(); i++)
	{
		if (m_lights[i]->isActive() && m_lights[i]->getshadow())
		{
			const std::vector<ShadowMatrix>& sm = m_lights[i]->getShadowMatrix();
			for (int j = 0; j < sm.size(); j++)
			{
				mats->push_back(sm[j].projview);
			}
		}
	}
}

bool LightManager::getShadowMatrix(unsigned int shadowIndex, glm::mat4& matrix) const
{
	for (Lights* light : m_lights)
	{
		if (!light->isActive() || !light->getshadow() || light->getShadowIndex() < 0)
			continue;
		const unsigned int first = static_cast<unsigned int>(light->getShadowIndex());
		const auto& matrices = light->getShadowMatrix();
		if (shadowIndex >= first && shadowIndex - first < matrices.size())
		{
			matrix = matrices[shadowIndex - first].projview;
			return true;
		}
	}
	return false;
}

SpotLight * LightManager::createSpotLight(glm::vec3 position, glm::vec3 color, glm::vec3 euler, float angle, std::string name)
{
	SpotLight * light = new SpotLight(static_cast<int>(m_lights.size()), m_gdm);
	m_lights.push_back(light);
	updateStorage();
	light->setPosition(position);
	light->setEulerAngles(euler);
	light->setColors(color);
	light->setSpotAngle(angle);
	light->setName(name);
	return (SpotLight *)light;
}

DirectionalLight * LightManager::createDirectionalLight(glm::vec3 euler, glm::vec3 color, std::string name)
{	
	DirectionalLight * light = new DirectionalLight(static_cast<int>(m_lights.size()), m_gdm);
	m_lights.push_back(light);
	updateStorage();
	light->setColors(color);
	light->setEulerAngles(euler);
	light->setName(name);
	return (DirectionalLight *)light;
}

PointLight * LightManager::createPointLight(glm::vec3 position, glm::vec3 color, std::string name)
{	
	PointLight * light = new PointLight(static_cast<int>(m_lights.size()), m_gdm);
	m_lights.push_back(light);
	updateStorage();
	light->setPosition(position);
	light->setColors(color);
	light->setName(name);	
	return (PointLight *)light;
}

void LightManager::destroyLight(Lights *light)
{
	const auto found = std::find(m_lights.begin(), m_lights.end(), light);
	if (found == m_lights.end()) return;
	m_lights.erase(found);
	delete light;
	updateStorage();
}