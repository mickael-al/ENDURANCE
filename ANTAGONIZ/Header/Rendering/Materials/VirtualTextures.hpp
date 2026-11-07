#ifndef __VIRTUAL_TEXTURE__
#define __VIRTUAL_TEXTURE__

#include "Textures.hpp"

namespace Ge
{
	class VirtualTextures : public Textures
	{
	public:
		VirtualTextures(unsigned int textureId, int Width, int Height, unsigned int index = -1);
		~VirtualTextures();
	};
}

#endif //!__VIRTUAL_TEXTURE__