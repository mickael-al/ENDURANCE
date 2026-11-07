#include "glcore.hpp"
#include "Lights.hpp"
#include "GraphicsDataMisc.hpp"
#include "glm/glm.hpp"
#include "glm/gtc/matrix_transform.hpp"
#include "glm/gtx/transform.hpp"
#include "glm/gtc/quaternion.hpp"
#include "glm/gtx/quaternion.hpp"
#include "glm/gtx/euler_angles.hpp"
#include "glm/common.hpp"
#include "Component.hpp"
#include "imgui-cmake/Header/imgui.h"
#include "Transform.hpp"
#include "Debug.hpp"
#include <algorithm>
#include "Camera.hpp"
#include "Engine.hpp"
#include "PointeurClass.hpp"

namespace Ge
{
	Lights::Lights(unsigned int index, GraphicsDataMisc *gdm) : GObject()
	{
		m_gdm = gdm;
		m_ssbo = m_gdm->str_ssbo.str_light;
		m_ssboShadow = m_gdm->str_ssbo.str_shadow;
		m_index = index;
		m_ubl.color = glm::vec3(1.0f);
		m_ubl.range = 10.0f;
		m_ubl.spotAngle = 45.0f;
		m_ubl.shadowId = -1;
		m_ubl.minPlaneID = -1;
		m_ubl.maxPlaneID = -1;
		m_shadowMatrix.resize(m_ubl.status == 0 ? SHADOW_MAP_CASCADE_COUNT : (m_ubl.status == 1 ? SHADOW_MAP_CUBE_COUNT : SHADOW_MAP_SPOT_COUNT));
	}

	void Lights::mapMemory()
	{
		if (!m_active)
		{
			return;
		}

		m_ubl.position = m_transform.position;
		m_ubl.direction = getDirection();
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_ssbo);
		glBufferSubData(GL_SHADER_STORAGE_BUFFER, m_index * sizeof(UniformBufferLight), sizeof(UniformBufferLight), &m_ubl);
		if (m_ubl.shadowId >= 0)
		{
			mapMemoryShadow();
		}
	}

	void Lights::setActive(bool state)
	{
		if (m_active == state)
		{
			return;
		}

		m_active = state;
		const ptrClass* pc = Engine::getPtrClassAddr();
		if (m_shadow)
		{
			pc->lightManager->updateStorageShadow();
		}
		else
		{
			pc->lightManager->updateStorage();
		}
	}

	bool Lights::isActive() const
	{
		return m_active;
	}
	void Lights::setshadow(bool state)
	{
		if (m_shadow != state)
		{
			m_shadow = state;
			const ptrClass* pc = Engine::getPtrClassAddr();
			pc->lightManager->updateStorageShadow();
		}
	}

	void Lights::mapMemoryShadow()
	{
		if (!m_active || m_ubl.shadowId < 0)
		{
			return;
		}
		Camera* currentCamera = m_gdm->current_camera;
		if (currentCamera == nullptr) return;

		if (m_ubl.status == 0 || m_ubl.status == 2)
		{
			m_ubl.direction = getDirection();
			float cascadeSplits[SHADOW_MAP_CASCADE_COUNT];

			float nearClip = currentCamera->getNear();
			float farClip = currentCamera->getFar();
			float clipRange = farClip - nearClip;

			float minZ = nearClip;
			float maxZ = nearClip + clipRange;

			float range = maxZ - minZ;
			float ratio = maxZ / minZ;

			for (uint32_t i = 0; i < SHADOW_MAP_CASCADE_COUNT; i++)
			{
				float p = (i + 1) / static_cast<float>(SHADOW_MAP_CASCADE_COUNT);
				float log = minZ * std::pow(ratio, p);
				float uniform = minZ + range * p;
				float d = cascadeSplitLambda * (log - uniform) + uniform;
				cascadeSplits[i] = (d - nearClip) / clipRange;
			}

			// Avoid a singular lookAt basis when the sun is directly overhead.
			const glm::vec3 lightDirection = glm::normalize(m_ubl.direction);
			const glm::vec3 lightUp = std::abs(lightDirection.y) > 0.99f
				? glm::vec3(0.0f, 0.0f, 1.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
			const glm::mat4 invCam = glm::inverse(currentCamera->getProjectionMatrix() * currentCamera->getViewMatrix());

			// Calculate orthographic projection matrix for each cascade
			float lastSplitDist = 0.0;
			for (uint32_t i = 0; i < SHADOW_MAP_CASCADE_COUNT; i++)
			{
				float splitDist = cascadeSplits[i];

				frustumCorners[0] = glm::vec3(-1.0f, 1.0f, -1.0f);
				frustumCorners[1] = glm::vec3(1.0f, 1.0f, -1.0f);
				frustumCorners[2] = glm::vec3(1.0f, -1.0f, -1.0f);
				frustumCorners[3] = glm::vec3(-1.0f, -1.0f, -1.0f);
				frustumCorners[4] = glm::vec3(-1.0f, 1.0f, 1.0f);
				frustumCorners[5] = glm::vec3(1.0f, 1.0f, 1.0f);
				frustumCorners[6] = glm::vec3(1.0f, -1.0f, 1.0f);
				frustumCorners[7] = glm::vec3(-1.0f, -1.0f, 1.0f);

				for (uint32_t i = 0; i < 8; i++)
				{
					glm::vec4 invCorner = invCam * glm::vec4(frustumCorners[i], 1.0f);
					frustumCorners[i] = invCorner / invCorner.w;
				}

				for (uint32_t i = 0; i < 4; i++)
				{
					glm::vec3 dist = frustumCorners[i + 4] - frustumCorners[i];
					frustumCorners[i + 4] = frustumCorners[i] + (dist * splitDist);
					frustumCorners[i] = frustumCorners[i] + (dist * lastSplitDist);
				}

				// Get frustum center
				glm::vec3 frustumCenter = glm::vec3(0.0f);
				for (uint32_t i = 0; i < 8; i++)
				{
					frustumCenter += frustumCorners[i];
				}
				frustumCenter /= 8.0f;

				float radius = 0.0f;
				for (uint32_t i = 0; i < 8; i++)
				{
					float distance = glm::length(frustumCorners[i] - frustumCenter);
					radius = glm::max(radius, distance);
				}
				radius = std::ceil(radius * 16.0f) / 16.0f;

				glm::vec3 maxExtents = glm::vec3(radius);
				glm::vec3 minExtents = -maxExtents;

				m_shadowMatrix[i].pos = frustumCenter - lightDirection * -minExtents.z;
				glm::mat4 lightViewMatrix = glm::lookAt(m_shadowMatrix[i].pos, frustumCenter, lightUp);
				glm::mat4 lightOrthoMatrix = glm::ortho(minExtents.x, maxExtents.x, maxExtents.y, minExtents.y, 0.0f, maxExtents.z - minExtents.z);

				m_shadowMatrix[i].splitDepth = (nearClip + splitDist * clipRange) * -1.0f;
				m_shadowMatrix[i].projview = lightOrthoMatrix * lightViewMatrix;				
				lastSplitDist = cascadeSplits[i];
			}
		}

		if (m_ubl.status == 1)
		{
			glm::vec3 directions[6] = 
			{
			   glm::vec3(1.0f, 0.0f, 0.0f),  // Positive X
			   glm::vec3(-1.0f, 0.0f, 0.0f), // Negative X
			   glm::vec3(0.0f, 1.0f, 0.0f),  // Positive Y
			   glm::vec3(0.0f, -1.0f, 0.0f), // Negative Y
			   glm::vec3(0.0f, 0.0f, 1.0f),  // Positive Z
			   glm::vec3(0.0f, 0.0f, -1.0f)  // Negative Z
			};
			glm::vec3 ups[6] = 
			{
				glm::vec3(0.0f, 1.0f, 0.0f), // Positive X
				glm::vec3(0.0f, 1.0f, 0.0f), // Negative X
				glm::vec3(0.0f, 0.0f, 1.0f), // Positive Y
				glm::vec3(0.0f, 0.0f, 1.0f), // Negative Y
				glm::vec3(0.0f, 1.0f, 0.0f), // Positive Z
				glm::vec3(0.0f, 1.0f, 0.0f)  // Negative Z
			};
			for (uint32_t i = 0; i < SHADOW_MAP_CUBE_COUNT; i++)
			{
				m_shadowMatrix[i].splitDepth = 0.0f;
				glm::mat4 lightPerMatrix = glm::perspective(glm::radians(90.0f), 1.0f, currentCamera->getNear(), currentCamera->getFar());
				m_shadowMatrix[i].projview = lightPerMatrix * glm::lookAt(m_ubl.position, m_ubl.position+directions[i], ups[i]);
				m_shadowMatrix[i].pos = m_ubl.position;
			}
		}

		glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_ssboShadow);
		int64_t sizeBytes = 0;
		glGetBufferParameteri64v(GL_SHADER_STORAGE_BUFFER, GL_BUFFER_SIZE, &sizeBytes);
		int64_t targetMaxSize = m_ubl.shadowId * sizeof(ShadowMatrix) + sizeof(ShadowMatrix) * m_shadowMatrix.size();
		if (targetMaxSize <= sizeBytes)
		{
			glBufferSubData(GL_SHADER_STORAGE_BUFFER, m_ubl.shadowId * sizeof(ShadowMatrix), sizeof(ShadowMatrix) * m_shadowMatrix.size(), m_shadowMatrix.data());
		}
	}

	bool Lights::getshadow() const
	{
		return m_shadow;
	}

	int Lights::addHiddenPlane(
		const glm::vec3& position,
		const glm::vec3& axisX,
		const glm::vec3& axisY,
		const glm::vec3& axisZ,
		const glm::vec2& scale)
	{
		if (glm::dot(axisX, axisX) <= 0.000001f ||
			glm::dot(axisY, axisY) <= 0.000001f ||
			glm::dot(axisZ, axisZ) <= 0.000001f ||
			scale.x <= 0.0f ||
			scale.y <= 0.0f)
		{
			return -1;
		}

		UniformBufferPlaneHidden plane{};
		plane.position = glm::vec4(position, scale.x);
		plane.axisX = glm::vec4(glm::normalize(axisX), 0.0f);
		plane.axisY = glm::vec4(glm::normalize(axisY), 0.0f);
		plane.axisZ = glm::vec4(glm::normalize(axisZ), scale.y);
		m_hiddenPlanes.push_back(plane);

		const ptrClass* pc = Engine::getPtrClassAddr();
		pc->lightManager->updateStoragePlaneHidden();
		return static_cast<int>(m_hiddenPlanes.size() - 1);
	}

	bool Lights::removeHiddenPlane(int planeIndex)
	{
		if (planeIndex < 0 || planeIndex >= static_cast<int>(m_hiddenPlanes.size()))
		{
			return false;
		}

		m_hiddenPlanes.erase(m_hiddenPlanes.begin() + planeIndex);
		const ptrClass* pc = Engine::getPtrClassAddr();
		pc->lightManager->updateStoragePlaneHidden();
		return true;
	}

	bool Lights::editHiddenPlane(
		int planeIndex,
		const glm::vec3& position,
		const glm::vec3& axisX,
		const glm::vec3& axisY,
		const glm::vec3& axisZ,
		const glm::vec2& scale)
	{
		if (planeIndex < 0 || planeIndex >= static_cast<int>(m_hiddenPlanes.size()) ||
			glm::dot(axisX, axisX) <= 0.000001f ||
			glm::dot(axisY, axisY) <= 0.000001f ||
			glm::dot(axisZ, axisZ) <= 0.000001f ||
			scale.x <= 0.0f ||
			scale.y <= 0.0f)
		{
			return false;
		}

		m_hiddenPlanes[planeIndex].position = glm::vec4(position, scale.x);
		m_hiddenPlanes[planeIndex].axisX = glm::vec4(glm::normalize(axisX), 0.0f);
		m_hiddenPlanes[planeIndex].axisY = glm::vec4(glm::normalize(axisY), 0.0f);
		m_hiddenPlanes[planeIndex].axisZ = glm::vec4(glm::normalize(axisZ), scale.y);
		const ptrClass* pc = Engine::getPtrClassAddr();
		pc->lightManager->updateStoragePlaneHidden();
		return true;
	}

	void Lights::clearHiddenPlanes()
	{
		if (m_hiddenPlanes.empty())
		{
			return;
		}

		m_hiddenPlanes.clear();
		const ptrClass* pc = Engine::getPtrClassAddr();
		pc->lightManager->updateStoragePlaneHidden();
	}

	int Lights::getHiddenPlaneCount() const
	{
		return static_cast<int>(m_hiddenPlanes.size());
	}

	const UniformBufferPlaneHidden* Lights::getHiddenPlane(int planeIndex) const
	{
		if (planeIndex < 0 || planeIndex >= static_cast<int>(m_hiddenPlanes.size()))
		{
			return nullptr;
		}
		return &m_hiddenPlanes[planeIndex];
	}

	const std::vector<ShadowMatrix>& Lights::getShadowMatrix()
	{
		return m_shadowMatrix;
	}

	void Lights::setRange(float r)
	{
		m_ubl.range = r;
		mapMemory();
	}

	void Lights::setSpotAngle(float r)
	{
		m_ubl.spotAngle = r;
		mapMemory();
	}

	float Lights::getSpotAngle() const
	{
		return m_ubl.spotAngle;
	}

	float Lights::getRange() const
	{
		return m_ubl.range;
	}

	void Lights::setColors(glm::vec3 color)
	{
		m_ubl.color = color;
		mapMemory();
	}

	glm::vec3 Lights::getColors() const
	{
		return m_ubl.color;
	}

	int Lights::getStatus() const
	{
		return m_ubl.status;
	}

	unsigned int Lights::getIndex() const
	{
		return m_index;
	}

	void Lights::setIndex(unsigned int i)
	{
		m_index = i;
	}

	void Lights::setShadowIndex(int i)
	{
		m_ubl.shadowId = i;
	}

	int Lights::getShadowIndex() const
	{
		return m_ubl.shadowId;
	}

	void Lights::onGUI()
	{
		GObject::onGUI();
		ImGui::TextColored(ImVec4(0.2f, 1, 0.2f, 1), "Light\n");
		bool active = m_active;
		if (ImGui::Checkbox("Active", &active))
		{
			setActive(active);
		}
		if (ImGui::ColorEdit3("Color", (float*)&m_ubl.color))
		{
			setColors(m_ubl.color);
		}

		if (ImGui::DragFloat("Range", &m_ubl.range, 0.2f, 0.01f))
		{
			mapMemory();
		}

		if (m_ubl.status == 2)
		{
			if (ImGui::DragFloat("Angle", &m_ubl.spotAngle, 0.2f, 0.01f))
			{
				setSpotAngle(m_ubl.spotAngle);
			}
		}
		bool s = m_shadow;
		if (ImGui::Checkbox("Shadow", &s))
		{
			setshadow(s);
		}

		if (ImGui::TreeNode("Hidden light planes"))
		{
			if (ImGui::Button("Add hidden plane"))
			{
				addHiddenPlane(
					m_transform.position,
					glm::vec3(1.0f, 0.0f, 0.0f),
					glm::vec3(0.0f, 1.0f, 0.0f),
					glm::vec3(0.0f, 0.0f, 1.0f),
					glm::vec2(1.0f));
			}

			for (int i = 0; i < static_cast<int>(m_hiddenPlanes.size()); ++i)
			{
				ImGui::PushID(i);
				glm::vec3 position = glm::vec3(m_hiddenPlanes[i].position);
				glm::vec3 axisX = glm::vec3(m_hiddenPlanes[i].axisX);
				glm::vec3 axisY = glm::vec3(m_hiddenPlanes[i].axisY);
				glm::vec3 axisZ = glm::vec3(m_hiddenPlanes[i].axisZ);
				glm::vec2 planeScale = glm::vec2(
					m_hiddenPlanes[i].position.w,
					m_hiddenPlanes[i].axisZ.w);
				bool edited = ImGui::DragFloat3("Position", &position.x, 0.1f);
				edited |= ImGui::DragFloat3("Axis X", &axisX.x, 0.02f, -1.0f, 1.0f);
				edited |= ImGui::DragFloat3("Axis Y", &axisY.x, 0.02f, -1.0f, 1.0f);
				edited |= ImGui::DragFloat3("Axis Z", &axisZ.x, 0.02f, -1.0f, 1.0f);
				edited |= ImGui::DragFloat2("Scale", &planeScale.x, 0.05f, 0.001f, 1000.0f);
				if (edited)
				{
					editHiddenPlane(i, position, axisX, axisY, axisZ, planeScale);
				}
				if (ImGui::Button("Remove"))
				{
					removeHiddenPlane(i);
					ImGui::PopID();
					break;
				}
				ImGui::Separator();
				ImGui::PopID();
			}
			ImGui::TreePop();
		}
	}

	Lights::~Lights()
	{
		if (m_shadow)
		{
			m_shadow = false;
			const ptrClass* pc = Engine::getPtrClassAddr();
			pc->lightManager->updateStorageShadow();
		}
	}
}