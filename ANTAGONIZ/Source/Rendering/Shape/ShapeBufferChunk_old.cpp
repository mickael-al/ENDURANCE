#include "glcore.hpp"
#include "ShapeBufferChunk_old.hpp"
#include <memory.h>
#include "Debug.hpp"

namespace Ge
{
	ShapeBufferChunk_old::ShapeBufferChunk_old()
	{
		//m_iboBuffer = new DynamicGPUAllocator(1024 * 1024, GL_ELEMENT_ARRAY_BUFFER);
		//m_vboBuffer = new DynamicGPUAllocator(1024 * 1024, GL_ARRAY_BUFFER);
		m_iboBuffer = new DynamicGPUAllocator(1024 * 1024, GL_SHADER_STORAGE_BUFFER);
		m_vboBuffer = new DynamicGPUAllocator(1024 * 1024, GL_SHADER_STORAGE_BUFFER);
		m_IBO = m_iboBuffer->getBufferID();
		m_VBO = m_vboBuffer->getBufferID();
	}

	ShapeBufferChunk_old::~ShapeBufferChunk_old()
	{
		glDeleteVertexArrays(1, &m_VAO);
		delete m_iboBuffer;
		delete m_vboBuffer;
	}

	void ShapeBufferChunk_old::pushChunk(VertexBiome_old* vb, size_t vertexSize, unsigned int* index, size_t indexSize, size_t * offsetIBO, size_t * offsetVBO)
	{
		*offsetVBO = m_vboBuffer->allocate(static_cast<void*>(vb), vertexSize * sizeof(VertexBiome_old)) / sizeof(VertexBiome_old);
		*offsetIBO = m_iboBuffer->allocate(static_cast<void*>(index), indexSize * sizeof(unsigned int)) / sizeof(unsigned int);	
		if (m_previousProgram != 0)
		{	
			if (m_IBO != m_iboBuffer->getBufferID() || m_VBO != m_vboBuffer->getBufferID())
			{
				m_IBO = m_iboBuffer->getBufferID();
				m_VBO = m_vboBuffer->getBufferID();
				SetupVAO(m_previousProgram);
			}
		}
	}

	void ShapeBufferChunk_old::updateVAO()
	{
		if (m_previousProgram != 0)
		{
			if (m_IBO != m_iboBuffer->getBufferID() || m_VBO != m_vboBuffer->getBufferID())
			{
				m_IBO = m_iboBuffer->getBufferID();
				m_VBO = m_vboBuffer->getBufferID();
				SetupVAO(m_previousProgram);
			}
		}
	}

	void ShapeBufferChunk_old::SetupVAO(uint32_t program)
	{
		GLuint attrPos0 = glGetAttribLocation(program, "a_Pos0");
		GLuint attrPos1 = glGetAttribLocation(program, "a_Pos1");
		GLuint attrPos2 = glGetAttribLocation(program, "a_Pos2");

		if (glIsVertexArray(m_VAO)) 
		{
			glDeleteVertexArrays(1, &m_VAO);
			m_VAO = 0;
		}

		glGenVertexArrays(1, &m_VAO);
		glBindVertexArray(m_VAO);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_IBO);
		glBindBuffer(GL_ARRAY_BUFFER, m_VBO);

		// Attribut 0 : contient x (16b), y (16b)
		glEnableVertexAttribArray(attrPos0);
		glVertexAttribIPointer(
			attrPos0,
			1,
			GL_UNSIGNED_INT,
			sizeof(VertexBiome_old),
			(void*)0
		);

		// Attribut 1 : contient z (16b), humidity (8b), heat (8b)
		glEnableVertexAttribArray(attrPos1);
		glVertexAttribIPointer(
			attrPos1,
			1,
			GL_UNSIGNED_INT,
			sizeof(VertexBiome_old),
			(void*)4
		);

		// Attribut 2 : encoded normal
		glEnableVertexAttribArray(attrPos2);
		glVertexAttribIPointer(
			attrPos2,
			1,
			GL_UNSIGNED_INT,
			sizeof(VertexBiome_old),
			(void*)8
		);
		m_previousProgram = program;
	}
}