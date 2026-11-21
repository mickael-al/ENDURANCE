#ifndef __ENGINE_TEXTURES__
#define __ENGINE_TEXTURES__

#include "ShadowContentToken.hpp"

struct GraphicsDataMisc;
typedef unsigned char stbi_uc;

namespace Ge
{
	class Textures
	{
	public:
		Textures(stbi_uc* pc, int Width, int Height, unsigned int index, bool filter,bool mipmaps, GraphicsDataMisc * gdm);
		Textures(unsigned int textureId, int Width, int Height, unsigned int index, bool filter, bool mipmaps, GraphicsDataMisc* gdm);
		Textures(unsigned int textureId, int Width, int Height, unsigned int index = -1);
		~Textures();		
		unsigned int getIndex() const;
		void setIndex(unsigned int index);
		unsigned int getTextureID() const;
		const bool getFilter() const;
		int getWidth() const;
		int getHeight() const;
        bool supportsImmutableShadowContent() const noexcept { return m_ownedPixelData; }
        std::uint64_t shadowContentRevision() const noexcept { return m_shadowContent.value(); }
        // External writes to image alpha/filter state must invalidate every
        // referencing immutable caster through this shared resource token.
        void invalidateShadowContent() noexcept { m_shadowContent.invalidate(); }
	protected:
		unsigned int m_index;
		unsigned int m_textureID;
		int m_width;
		int m_height;
		bool m_filter;
        bool m_ownedPixelData = false;
        ShadowContentToken m_shadowContent;
	};
}

#endif //!__ENGINE_TEXTURES__