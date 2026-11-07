#ifndef __ENGINE_MODEL_BUFFER_CHUNK_OLD__
#define __ENGINE_MODEL_BUFFER_CHUNK_OLD__

#include <vector>
struct GraphicsDataMisc;
#include "marching_cubes_old.hpp"
#include "ShapeBuffer.hpp"
#include "DynamicGPUAllocator.hpp"

namespace Ge
{
	class ShapeBufferChunk_old : public ShapeBuffer
	{
	public:
		ShapeBufferChunk_old();
		~ShapeBufferChunk_old();
		void SetupVAO(unsigned int program);
		void pushChunk(VertexBiome_old* vb, size_t vertexSize, unsigned int* index, size_t indexSize, size_t* offsetIBO, size_t* offsetVBO);
		void updateVAO();
		inline unsigned int getIndiceSize() const { return 0; }
		inline DynamicGPUAllocator* getIboAllocator() const { return m_iboBuffer; }
		inline DynamicGPUAllocator* getVboAllocator() const { return m_vboBuffer; }
	private:
		DynamicGPUAllocator * m_iboBuffer;
		DynamicGPUAllocator * m_vboBuffer;
		unsigned int m_previousProgram = 0;
	};
}

#endif //!__ENGINE_MODEL_BUFFER_CHUNK_OLD__
