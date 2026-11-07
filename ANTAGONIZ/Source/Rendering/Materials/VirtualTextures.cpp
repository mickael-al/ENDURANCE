#include "VirtualTextures.hpp"

namespace Ge
{
	VirtualTextures::VirtualTextures(unsigned int textureId, int Width, int Height, unsigned int index) : Textures(textureId, Width, Height, index)
	{}

	VirtualTextures::~VirtualTextures()
	{
		m_textureID = 0;
	}
}