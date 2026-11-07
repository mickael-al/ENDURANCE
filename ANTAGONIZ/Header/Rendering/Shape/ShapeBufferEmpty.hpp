#ifndef __ENGINE_MODEL_BUFFER_EMPTY__
#define __ENGINE_MODEL_BUFFER_EMPTY__

#include <vector>
#include "ShapeBuffer.hpp"

namespace Ge
{
	class ShapeBufferEmpty : public ShapeBuffer
	{
	public:
		ShapeBufferEmpty(uint32_t indices);
		~ShapeBufferEmpty();
		void SetupVAO(unsigned int program);
		unsigned int getIndiceSize() const;
	private:
		uint32_t m_indiceSize;
	};
}

#endif //!__ENGINE_MODEL_BUFFER_EMPTY__
