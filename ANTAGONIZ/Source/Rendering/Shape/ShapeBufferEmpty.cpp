#include "glcore.hpp"
#include "ShapeBufferEmpty.hpp"
#include <memory.h>
#include "Debug.hpp"

namespace Ge
{
	ShapeBufferEmpty::ShapeBufferEmpty(uint32_t indices)
	{
		m_indiceSize = indices;
		std::vector<uint32_t> index;
		index.resize(m_indiceSize);
		for (unsigned int i = 0; i < index.size(); ++i)
		{
			index[i] = i;
		}
		glGenBuffers(1, &m_IBO);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_IBO);
		glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(uint32_t) * index.size(), index.data(), GL_STATIC_DRAW);
	}

	ShapeBufferEmpty::~ShapeBufferEmpty()
	{		
		glDeleteBuffers(1, &m_IBO);
		glDeleteVertexArrays(1, &m_VAO);
	}

	void ShapeBufferEmpty::SetupVAO(unsigned int program)
	{		
		if (glIsVertexArray(m_VAO) == GL_TRUE)
		{
			glDeleteVertexArrays(1, &m_VAO);
			m_VAO = 0;
		}

		glGenVertexArrays(1, &m_VAO);
		glBindVertexArray(m_VAO);		
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_IBO);
	}

	unsigned int ShapeBufferEmpty::getIndiceSize() const
	{
		return m_indiceSize;
	}
}