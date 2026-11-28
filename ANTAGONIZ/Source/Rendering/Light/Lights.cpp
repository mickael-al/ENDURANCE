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
#include <cstdint>
#include <cstring>
#include "Camera.hpp"
#include "Engine.hpp"
#include "PointeurClass.hpp"
#include "ShadowCasterBounds.hpp"
#include "ShadowCascadeSplits.hpp"

namespace Ge
{
    namespace
    {
        float clampedDirectionalShadowBias(float value, float fallback)
        {
            // Remain valid under the engine's -ffast-math compilation flags.
            std::uint32_t bits;
            std::memcpy(&bits, &value, sizeof(bits));
            if ((bits & 0x7f800000u) == 0x7f800000u) return fallback;
            return glm::clamp(value, 0.0f, 8.0f);
        }
    }

    void Lights::setShadowCasterMargin(float metres)
    {
        const float next=finiteShadowCoordinate(metres)?glm::clamp(metres,0.0f,200000.0f):0.0f;
        if(next==m_shadowCasterMargin) return;
        m_shadowCasterMargin=next;
        if(m_active && m_shadow && m_ubl.status==0) mapMemoryShadow();
    }

    void Lights::setDirectionalShadowBias(float depthTexels, float normalTexels)
    {
        const float depth = clampedDirectionalShadowBias(depthTexels, 0.5f);
        const float normal = clampedDirectionalShadowBias(normalTexels, 1.5f);
        if (m_ubl.shadowDepthBias == depth && m_ubl.shadowNormalBias == normal) return;
        m_ubl.shadowDepthBias = depth;
        m_ubl.shadowNormalBias = normal;
        mapMemory();
    }

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

		if (m_ubl.status == 0)
		{
			m_ubl.direction = getDirection();
			float nearClip = currentCamera->getNear();
			float farClip = currentCamera->getFar();
			float clipRange = farClip - nearClip;
			const auto cascadeSplits = directionalCascadeSplits<SHADOW_MAP_CASCADE_COUNT>(
				nearClip, farClip, cascadeSplitLambda, currentCamera->getFirstShadowCascadeDistance());

			// Avoid a singular lookAt basis when the sun is directly overhead.
			const glm::vec3 lightDirection = glm::normalize(m_ubl.direction);
			const glm::vec3 lightUp = std::abs(lightDirection.y) > 0.99f
				? glm::vec3(0.0f, 0.0f, 1.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
			// Fit in camera space: translation or rotation must not resize the
			// sphere through cancellation in a world-space float inverse.
			const glm::dmat4 invProjection = glm::inverse(glm::dmat4(currentCamera->getProjectionMatrix()));
			const glm::dmat4 invView = glm::inverse(glm::dmat4(currentCamera->getViewMatrix()));
			const glm::dvec3 direction = glm::normalize(glm::dvec3(lightDirection));
			const glm::dvec3 right = glm::normalize(glm::cross(direction, glm::dvec3(lightUp)));
			const glm::dvec3 up = glm::cross(right, direction);
			const auto* pc = Engine::getPtrClassAddr();
			const auto* casterBounds = m_ubl.status == 0 && pc && pc->lightManager
				? &pc->lightManager->getShadowCasterBounds() : nullptr;
			glm::dvec3 rayOrigins[4], raysPerDepth[4];
			for (unsigned corner = 0; corner < 4; ++corner)
			{
				const double x = (corner & 1) ? 1.0 : -1.0;
				const double y = (corner & 2) ? 1.0 : -1.0;
				const auto nearH = invProjection * glm::dvec4(x, y, -1.0, 1.0);
				// A mid-depth point avoids the poorly conditioned far-plane
				// inverse of a perspective camera spanning many kilometres.
				const auto middleH = invProjection * glm::dvec4(x, y, 0.0, 1.0);
				rayOrigins[corner] = glm::dvec3(nearH) / nearH.w;
				const glm::dvec3 middle = glm::dvec3(middleH) / middleH.w;
				raysPerDepth[corner] = (middle - rayOrigins[corner]) / (rayOrigins[corner].z - middle.z);
			}

			// Calculate orthographic projection matrix for each cascade
			float lastSplitDist = 0.0;
			for (uint32_t i = 0; i < SHADOW_MAP_CASCADE_COUNT; i++)
			{
				float splitDist = cascadeSplits[i];

				// The next cascade includes the last 10% of the preceding
				// interval. Keep this fraction in sync with Shadow/shadow_filter.glsl;
				// splitDepth still denotes the nominal (unexpanded) boundary.
				constexpr double cascadeBlendFraction = 0.10;
				double nearSplit = lastSplitDist;
				if (i > 0)
				{
					const double previousEnd = nearClip + lastSplitDist * clipRange;
					const double previousStart = i > 1 ? nearClip + cascadeSplits[i - 2] * clipRange : 0.0;
					nearSplit = (std::max)(0.0, nearSplit - cascadeBlendFraction * (previousEnd - previousStart) / clipRange);
				}
				glm::dvec3 corners[8];
				for (unsigned corner = 0; corner < 4; ++corner)
				{
					const auto& nearPoint = rayOrigins[corner];
					const auto& rayPerDepth = raysPerDepth[corner];
					corners[corner] = nearPoint + rayPerDepth * (nearClip + nearSplit * clipRange + nearPoint.z);
					corners[corner + 4] = nearPoint + rayPerDepth * (nearClip + double(splitDist) * clipRange + nearPoint.z);
				}
				glm::dvec3 viewCenter(0.0);
				for (const auto& corner : corners) viewCenter += corner;
				viewCenter /= 8.0;
				double radius = 0.0;
				for (const auto& corner : corners) radius = (std::max)(radius, glm::length(corner - viewCenter));
				// Reserve three texels for centre snapping, the PCF footprint
				// and the default geometric-normal offset. Solve using the final
				// texel size, rather than padding with the unexpanded radius.
				radius = std::ceil((radius / (1.0 - 6.0 / TEXTURE_DIM)) * 16.0) / 16.0;

				DirectionalShadowVolume volume;
				volume.center = glm::dvec3(invView * glm::dvec4(viewCenter, 1.0));
				volume.right = right;
				volume.up = up;
				volume.direction = direction;
				volume.radius = radius;
				const double worldTexel = 2.0 * radius / TEXTURE_DIM;
				const double centerX = glm::dot(right, volume.center);
				const double centerY = glm::dot(up, volume.center);
				volume.center += right * (std::round(centerX / worldTexel) * worldTexel - centerX)
					+ up * (std::round(centerY / worldTexel) * worldTexel - centerY);
				const float margin = casterBounds
					? fitDirectionalShadowCasterMargin(volume, m_shadowCasterMargin, *casterBounds)
					: m_shadowCasterMargin;
				if (m_ubl.status == 0) m_directionalShadowCasterMargins[i] = margin;

				const double distance = volume.radius + static_cast<double>(margin);
				m_shadowMatrix[i].pos = glm::vec3(volume.center - direction * distance);
				// Build the basis independently of the eye position. Moving only
				// the near plane must not rotate XY through float cancellation.
				glm::dmat4 lightViewMatrix(1.0);
				lightViewMatrix[0] = glm::dvec4(right.x, up.x, -direction.x, 0.0);
				lightViewMatrix[1] = glm::dvec4(right.y, up.y, -direction.y, 0.0);
				lightViewMatrix[2] = glm::dvec4(right.z, up.z, -direction.z, 0.0);
				lightViewMatrix[3] = glm::dvec4(-glm::dot(right, volume.center),
					-glm::dot(up, volume.center), glm::dot(direction, volume.center) - distance, 1.0);
				const glm::dmat4 lightOrthoMatrix = glm::ortho(-volume.radius, volume.radius,
					volume.radius, -volume.radius, 0.0, 2.0 * volume.radius + margin);

				m_shadowMatrix[i].splitDepth = (nearClip + splitDist * clipRange) * -1.0f;
				m_shadowMatrix[i].projview = glm::mat4(lightOrthoMatrix * lightViewMatrix);
				lastSplitDist = cascadeSplits[i];
			}
		}

		if (m_ubl.status == 2)
		{
			// One perspective map covers a spotlight cone; directional
			// camera cascades do not represent its point of emission.
			m_ubl.direction = getDirection();
			const glm::vec3 direction = glm::normalize(m_ubl.direction);
			const glm::vec3 up = std::abs(direction.y) > 0.99f
				? glm::vec3(0, 0, 1) : glm::vec3(0, 1, 0);
			const float nearPlane = glm::clamp(currentCamera->getNear(), 0.01f, 0.1f);
			const float farPlane = (std::max)(currentCamera->getFar(), nearPlane + 1.0f);
			const auto projection = glm::perspective(glm::radians(double(m_ubl.spotAngle)), 1.0, double(nearPlane), double(farPlane));
			const auto position = glm::dvec3(m_ubl.position);
			m_shadowMatrix[0].projview = glm::mat4(projection * glm::lookAt(position, position + glm::dvec3(direction), glm::dvec3(up)));
			m_shadowMatrix[0].pos = m_ubl.position;
			m_shadowMatrix[0].splitDepth = 0.0f;
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
		m_ubl.spotAngle = finiteShadowCoordinate(r) ? glm::clamp(r, 1.0f, 179.0f) : 45.0f;
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
		if (m_ubl.status == 0)
		{
			glm::vec2 bias = getDirectionalShadowBias();
			bool changed = ImGui::DragFloat("Shadow depth bias (texels)", &bias.x, 0.01f, 0.0f, 8.0f);
			changed |= ImGui::DragFloat("Shadow normal bias (texels)", &bias.y, 0.01f, 0.0f, 8.0f);
			if (changed) setDirectionalShadowBias(bias.x, bias.y);
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
