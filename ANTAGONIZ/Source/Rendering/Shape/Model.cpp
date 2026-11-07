#include "glcore.hpp"
#include "Model.hpp"
#include "GraphicsDataMisc.hpp"
#include "glm/gtc/matrix_transform.hpp"
#include "glm/gtx/transform.hpp"
#include "glm/gtc/quaternion.hpp" 
#include "glm/gtx/quaternion.hpp"
#include "glm/gtx/euler_angles.hpp"
#include "glm/common.hpp"
#include "Debug.hpp"
#include "ShapeBuffer.hpp"
#include "UniformBufferObject.hpp"
#include "Materials.hpp"
#include "ModelManager.hpp"
#include "Engine.hpp"
#include "EngineHeader.hpp"
#include "GraphiquePipeline.hpp"
#include "imgui-cmake/Header/imgui.h"

namespace Ge
{
	Model::Model(ShapeBuffer * buffer, unsigned int index, GraphicsDataMisc * gdm) : GObject()
	{
		m_gdm = gdm;
		m_buffer = buffer;
		m_material = gdm->str_default_material;
		m_index = index;
		m_ssbo = m_gdm->str_ssbo.str_model;
		m_ubo.mat_index = 0;
	}

	Model::~Model()
	{
		if (m_material != nullptr)
		{
			removeComponent((Component *)m_material);
			m_material->removeModel(this);
		}
	}

	void Model::mapMemory()
	{
		m_ubo.model = getModelMatrix();
		if (!m_draw)
		{
			// Keep the matrix invertible for shaders which build a normal matrix,
			// but place the model well outside every camera and shadow frustum.
			m_ubo.model[3] = glm::vec4(1000000.0f, 1000000.0f, 1000000.0f, 1.0f);
		}
		m_ubo.mat_index = m_material != nullptr ? m_material->getIndex() : 0;
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_ssbo);
		glBufferSubData(GL_SHADER_STORAGE_BUFFER, m_index * sizeof(UniformBufferObject), sizeof(UniformBufferObject), &m_ubo);
	}

	void Model::setIndex(unsigned int index)
	{
		m_index = index;
		mapMemory();
	}

	unsigned int Model::getIndex()
	{
		return m_index;
	}

	void Model::onGUI()
	{
		GObject::onGUI();		
	}

	void Model::setMaterial(Materials * m)
	{			
		if (m_material != nullptr)
		{		
			removeComponent((Component *)m_material);
			m_material->removeModel(this);
		}
		if (m == nullptr)
		{
			m = m_gdm->str_default_material;
		}
		addComponent((Component *)m);
		Engine::getPtrClass().modelManager->buildInstancedModels(this, m);
		m_material = m;
		m_material->addModel(this);		
		mapMemory();
		m_buffer->SetupVAO(m_material->getPipeline()->getProgram());
	}

	Materials * Model::getMaterial() const
	{
		return m_material;
	}

	void Model::setDraw(bool draw)
	{
		if (m_draw == draw)
		{
			return;
		}
		m_draw = draw;
		mapMemory();
	}

	bool Model::getDraw() const
	{
		return m_draw;
	}

	ShapeBuffer * Model::getShapeBuffer() const
	{
		return m_buffer;
	}

	UniformBufferObject Model::getUBO() const
	{
		return m_ubo;
	}
}
