#ifndef __ENGINE_MODEL_SHAPE__
#define __ENGINE_MODEL_SHAPE__

#include "GObject.hpp"
#include "UniformBufferObject.hpp"
#include "ShadowContentToken.hpp"

namespace Ge
{
	class Materials;
	class ShapeBuffer;
}

struct GraphicsDataMisc;

namespace Ge
{
	class Model : public GObject
	{
	public:
		Model(ShapeBuffer * buffer, unsigned int index, GraphicsDataMisc * gdm);
		~Model();		
		ShapeBuffer * getShapeBuffer() const;
		UniformBufferObject getUBO() const;
		void setMaterial(Materials * m);
		Materials * getMaterial() const;
		void setDraw(bool draw);
		bool getDraw() const;
		void mapMemory() override;
		void setIndex(unsigned int index);
		unsigned int getIndex();
		void onGUI();
        // Opt-in promise: mesh/alpha data remain immutable between explicit
        // invalidations. Managed transforms/material settings are tracked anyway.
        void setImmutableShadowContent(bool immutable) noexcept
        { if (m_immutableShadowContent != immutable) { m_immutableShadowContent = immutable; m_shadowContent.invalidate(); } }
        bool getImmutableShadowContent() const noexcept { return m_immutableShadowContent; }
        void invalidateShadowContent() noexcept { m_shadowContent.invalidate(); }
        std::uint64_t shadowContentRevision() const noexcept { return m_shadowContent.value(); }
	private:
		GraphicsDataMisc * m_gdm;
		ShapeBuffer * m_buffer;
		Materials * m_material;
		UniformBufferObject m_ubo{};
		unsigned int m_index;
		unsigned int m_ssbo;
		bool m_draw = true;
        bool m_immutableShadowContent = false;
        ShadowContentToken m_shadowContent;
	};
}

#endif //!__ENGINE_MODEL_SHAPE__
