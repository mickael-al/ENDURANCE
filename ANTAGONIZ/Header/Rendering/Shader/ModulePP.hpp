#ifndef __MODULE_POST_PROCESS__
#define __MODULE_POST_PROCESS__

#include "Initializer.hpp"
#include <glm/glm.hpp>
struct GraphicsDataMisc;

namespace Ge
{
	struct PPSetting
	{
		bool fog;
		float fog_min_distance;
		float fog_max_distance;
		int fog_mode;
		glm::vec3 fog_color;
		bool bloom;
		float bloom_filter;
		float bloom_threshold;
		float bloom_intensity;
		float exposure;
		float* gamma;	
		bool depth_of_field;
		float dof_focus_distance;
		float dof_focal_length;
		float dof_f_number;
		float dof_sensor_height;
		float dof_max_radius;
		bool dof_match_camera_fov;
		bool color_tint;
		glm::vec3 tint_color;
		float tint_strength;
		float tint_saturation;
		float tint_contrast;
		bool oil_painting;
		unsigned int oilp_radius;		
		PPSetting()
		{
			bloom = true;
			bloom_filter = 0.001f;
			bloom_threshold = 0.01f;
			bloom_intensity = 1.0f;
			exposure = 1.0;
			gamma = nullptr;

			depth_of_field = false;
			dof_focus_distance = 12.0f; // metres
			dof_focal_length = 50.0f; // millimetres
			dof_f_number = 1.8f;
			dof_sensor_height = 24.0f; // millimetres
			dof_max_radius = 12.0f; // full-resolution pixels at 1080p
			dof_match_camera_fov = true;

			color_tint = false;
			tint_color = glm::vec3(1.0f);
			tint_strength = 0.5f;
			tint_saturation = 1.0f;
			tint_contrast = 1.0f;

			fog = false;
			fog_min_distance = 100.0f;
			fog_max_distance = 800.0f;
			fog_mode = 0;
			fog_color = glm::vec3(1,1,1);

			oil_painting = false;
			oilp_radius = 1;
		}
		void copy(PPSetting pp)
		{
			bloom = pp.bloom;
			bloom_filter = pp.bloom_filter;
			bloom_threshold = pp.bloom_threshold;
			bloom_intensity = pp.bloom_intensity;
			exposure = pp.exposure;			

			depth_of_field = pp.depth_of_field;
			dof_focus_distance = pp.dof_focus_distance;
			dof_focal_length = pp.dof_focal_length;
			dof_f_number = pp.dof_f_number;
			dof_sensor_height = pp.dof_sensor_height;
			dof_max_radius = pp.dof_max_radius;
			dof_match_camera_fov = pp.dof_match_camera_fov;

			color_tint = pp.color_tint;
			tint_color = pp.tint_color;
			tint_strength = pp.tint_strength;
			tint_saturation = pp.tint_saturation;
			tint_contrast = pp.tint_contrast;

			fog = pp.fog;
			fog_min_distance = pp.fog_min_distance;
			fog_max_distance = pp.fog_max_distance;
			fog_mode = pp.fog_mode;
			fog_color = pp.fog_color;

			oil_painting = pp.oil_painting;
			oilp_radius = pp.oilp_radius;
		}		
	};
	class ShapeBuffer;
	class ModulePP : public InitializerAPI
	{
	public:		
		virtual ~ModulePP() = default;
		virtual bool initialize(GraphicsDataMisc* gdm) = 0;
		virtual void release() = 0;
		virtual void resize(int width, int height) = 0;
		virtual void compute(unsigned int frameBuffer, unsigned int texture, unsigned int depthTexture, ShapeBuffer * sb, PPSetting * settings) = 0;
		virtual void onGui(PPSetting* target, PPSetting* current) = 0;
	};
}

#endif //!__MODULE_POST_PROCESS__