#ifndef __ENGINE_MODEL_BUFFER_INTERFACE__
#define __ENGINE_MODEL_BUFFER_INTERFACE__

#include "ShadowContentToken.hpp"

namespace Ge
{
	class ShapeBuffer
	{
	public:
		virtual ~ShapeBuffer() = default;
		unsigned int getVAO() const
		{
			return m_VAO;
		}

		unsigned int getVBO() const
		{
			return m_VBO;
		}

		unsigned int getIBO() const
		{
			return m_IBO;
		}
		virtual void SetupVAO(unsigned int program) = 0;
		virtual unsigned int getIndiceSize() const = 0;
        virtual bool supportsImmutableShadowContent() const noexcept { return false; }
        std::uint64_t shadowContentRevision() const noexcept { return m_shadowContent.value(); }
        // Call after externally modifying the raw VBO/IBO or vertex layout.
        // Continuous deformation should never opt into immutable shadow caching.
        void invalidateShadowContent() noexcept { m_shadowContent.invalidate(); }
	protected:
		unsigned int m_VAO;
		unsigned int m_VBO;
		unsigned int m_IBO;
        ShadowContentToken m_shadowContent;
	};
}
#endif //!__ENGINE_MODEL_BUFFER_INTERFACE__