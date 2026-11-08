#ifndef __ENGINE_LIGHT__
#define __ENGINE_LIGHT__

#include "GObject.hpp"
#include "UniformBufferLight.hpp"
#include "UniformBufferPlaneHidden.hpp"
#include "ShadowMatrix.hpp"

#define TEXTURE_DIM 2048
#define SHADOW_MAP_CASCADE_COUNT 4
#define SHADOW_MAP_CUBE_COUNT 6
#define SHADOW_MAP_SPOT_COUNT 4

struct GraphicsDataMisc;

namespace Ge
{
	class Lights : public GObject
	{
	public:
		Lights(unsigned int index, GraphicsDataMisc *gdm);
		glm::vec3 getColors() const;
		float getRange() const;
		float getSpotAngle() const;
		int getStatus() const; //Statut directional spotlight pointlight
		unsigned int getIndex() const;
		int getShadowIndex() const;
		void setColors(glm::vec3 color);
		void setRange(float r);
		void setSpotAngle(float r);
		void setIndex(unsigned int i);
		void setShadowIndex(int i);
		void setActive(bool state);
		bool isActive() const;
		void mapMemory();
		void mapMemoryShadow();
		void setshadow(bool state);
		bool getshadow() const;
		int addHiddenPlane(
			const glm::vec3& position,
			const glm::vec3& axisX,
			const glm::vec3& axisY,
			const glm::vec3& axisZ,
			const glm::vec2& scale);
		bool removeHiddenPlane(int planeIndex);
		bool editHiddenPlane(
			int planeIndex,
			const glm::vec3& position,
			const glm::vec3& axisX,
			const glm::vec3& axisY,
			const glm::vec3& axisZ,
			const glm::vec2& scale);
		void clearHiddenPlanes();
		int getHiddenPlaneCount() const;
		const UniformBufferPlaneHidden* getHiddenPlane(int planeIndex) const;
		const std::vector<ShadowMatrix>& getShadowMatrix();
		void onGUI() override;
		virtual ~Lights();
	protected:
		friend class LightManager;
		UniformBufferLight m_ubl{};
		std::vector<UniformBufferPlaneHidden> m_hiddenPlanes;
		std::vector<ShadowMatrix> m_shadowMatrix;
		GraphicsDataMisc * m_gdm;
		unsigned int m_index = 0;
		unsigned int m_ssbo;
		unsigned int m_ssboShadow;
		bool m_shadow = false;
		bool m_active = true;
		//shadow Dir
		float cascadeSplitLambda = 0.96f;
		glm::vec3 frustumCorners[8];
	};
}

#endif //__ENGINE_LIGHT__
