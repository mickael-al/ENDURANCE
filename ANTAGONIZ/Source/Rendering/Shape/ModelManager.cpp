#include "glcore.hpp"
#include "ModelManager.hpp"
#define TINYOBJLOADER_IMPLEMENTATION
#include "tinyobjloader/tiny_obj_loader.h"
#include <glm/gtx/normal.hpp>
#include "OpenFBX/src/ofbx.h"
#include "Model.hpp"
#include "ShapeBufferBase.hpp"
#include "ShapeBufferSkeleton.hpp"
#include "ShapeBufferEmpty.hpp"
#include "Debug.hpp"
#include "Vertex.hpp"
#include <algorithm>
#include <cmath>
#include <unordered_map>
#include "GraphicsDataMisc.hpp"
#include "GraphiquePipeline.hpp"
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace
{
	std::string getFBXObjectPath(const ofbx::Object* object)
	{
		std::vector<std::string> elements;
		while (object != nullptr)
		{
			if (object->name[0] != '\0')
			{
				elements.emplace_back(object->name);
			}
			object = object->getParent();
		}
		std::reverse(elements.begin(), elements.end());

		std::string path;
		for (const std::string& element : elements)
		{
			if (!path.empty())
			{
				path += "/";
			}
			path += element;
		}
		return path;
	}
}

namespace Ge
{
	bool ModelManager::initialize(GraphicsDataMisc *gdm)
	{
		m_gdm = gdm;
		glGenBuffers(1, &m_ssbo);
		m_gdm->str_ssbo.str_model = m_ssbo;
		float pos[] = { -1.0f, 1.0f,0.0f, -1.0f, -3.0f,0.0f , 3.0f, 1.0f,0.0f };
		float texCord[] = {0.0f,0.0f,0.0f,-2.0f,2.0f,0.0f};
		float normal[] = { 0.0f,0.0f,1.0f,0.0f,0.0f,1.0f, 0.0f,0.0f,1.0f };
		unsigned int indice[] = { 0,1,2};

		m_fullScreenTriangle = allocateBuffer(pos, texCord, normal, indice, 3, 3);
		m_fullScreenTriangle->SetupVAO(gdm->str_default_pipeline_forward->getProgram());

		float posQ[] = { -1.0f, 1.0f,0.0f, -1.0f, -1.0f,0.0f , 1.0f, 1.0f,0.0f, 1.0f, -1.0f,0.0f };
		float texCordQ[] = { 0.0f,1.0f,0.0f,0.0f,1.0f,1.0f,1.0f,0.0f };
		float normalQ[] = { 0.0f,0.0f,1.0f,0.0f,0.0f,1.0f, 0.0f,0.0f,1.0f, 0.0f,0.0f,1.0f };
		unsigned int indiceQ[] = { 0,1,2,3,2,1 };

		m_defferedQuad = allocateBuffer(posQ, texCordQ, normalQ, indiceQ, 4, 6);
		m_defferedQuad->SetupVAO(gdm->str_default_pipeline_forward->getProgram());
		Debug::INITSUCCESS("ModelManager");
		return true;
	}

	void ModelManager::release()
	{
		for (int i = 0; i < m_shapeBuffers.size(); i++)
		{			
			m_poolBuffer.deleteObject((ShapeBufferBase*)m_shapeBuffers[i]);
		}
		m_shapeBuffers.clear();
		for (int i = 0; i < m_models.size(); i++)
		{			
			m_pool.deleteObject(m_models[i]);
		}
		m_models.clear();
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
		glDeleteBuffers(1, &m_ssbo);
		Debug::RELEASESUCCESS("ModelManager");
	}

	Model * ModelManager::createModel(ShapeBuffer *buffer, std::string nom)
	{
		if (buffer == nullptr)
		{
			Debug::Warn("Le buffer n'existe pas");
			return nullptr;
		}
		Model * Mesh = m_pool.newObject(buffer, m_models.size(), m_gdm);
		m_models.push_back(Mesh);
		m_gdm->str_dataMisc.modelCount = m_models.size();
		updateStorage();
		Mesh->setName(nom);
		Mesh->setMaterial(m_gdm->str_default_material);		
		m_gdm->str_dataMisc.recreateCommandBuffer = true;
		return Mesh;
	}

	void ModelManager::destroyModel(Model *model)
	{
		model->setParent(nullptr);
		m_models.erase(std::remove(m_models.begin(), m_models.end(), model), m_models.end());
		Materials * mat = model->getMaterial();
		ShapeBuffer * sb = model->getShapeBuffer();
		std::vector<Model*> &vecModel = m_instanced[mat][sb];
		vecModel.erase(std::remove(vecModel.begin(), vecModel.end(), model), vecModel.end());
		if (vecModel.size() == 0)
		{
			auto bufferIterator = std::find_if(
				m_instanced[mat].begin(), m_instanced[mat].end(),
				[sb](const std::pair<ShapeBuffer * , std::vector<Model*>>& shapeBufferPair) {
				return shapeBufferPair.first == sb;
			}
			);

			if (bufferIterator != m_instanced[mat].end())
			{
				m_instanced[mat].erase(bufferIterator);
				if (m_instanced[mat].empty())
				{
					m_instanced.erase(mat);
				}
			}
		}		
		m_pool.deleteObject(model);
		m_gdm->str_dataMisc.modelCount = m_models.size();
		m_gdm->str_dataMisc.recreateCommandBuffer = true;
		updateStorage();
	}

	void ModelManager::destroyBuffer(ShapeBuffer *buffer)
	{
		m_shapeBuffers.erase(std::remove(m_shapeBuffers.begin(), m_shapeBuffers.end(), buffer), m_shapeBuffers.end());
		for (auto& materialPair : m_instanced) 
		{
			Materials* currentMaterials = materialPair.first;

			auto bufferIterator = std::find_if(
				materialPair.second.begin(), materialPair.second.end(),
				[buffer](const std::pair<ShapeBuffer *, std::vector<Model*>>& shapeBufferPair) {
				return shapeBufferPair.first == buffer;
			}
			);

			if (bufferIterator != materialPair.second.end()) 
			{
				materialPair.second.erase(bufferIterator);			
				if (materialPair.second.empty())
				{
					m_instanced.erase(materialPair.first);
					break;
				}
			}
		}		
		if (ShapeBufferBase* sbb = dynamic_cast<ShapeBufferBase*>(buffer))
		{
			m_poolBuffer.deleteObject(sbb);
		}
		else
		{
			delete buffer;
		}
		m_gdm->str_dataMisc.recreateCommandBuffer = true;	
	}

	std::vector<Model*> & ModelManager::getModels()
	{
		return m_models;
	}

	std::unordered_map<Materials*, std::unordered_map<ShapeBuffer*, std::vector<Model*>>> & ModelManager::getInstancedModels()
	{
		return m_instanced;
	}

	void ModelManager::buildInstancedModels(Model * target,Materials * switchMat)
	{
		Materials * mat = target->getMaterial();
		ShapeBuffer * sb = target->getShapeBuffer();
		std::vector<Model*> &vecModel = m_instanced[mat][sb];
		vecModel.erase(std::remove(vecModel.begin(), vecModel.end(), target), vecModel.end());
		m_instanced[switchMat][sb].push_back(target);
		int count = 0;
		for (auto& material : m_instanced)
		{
			for (auto& buffer : material.second)
			{
				for (auto& model : buffer.second)
				{
					if (model->getIndex() != count)
					{
						model->setIndex(count);
					}
					count++;
				}
			}
		}
	}

	void ModelManager::clearInstancedMaterial(Materials * mat)
	{
		std::unordered_map<Materials*, std::unordered_map<ShapeBuffer*, std::vector<Model*>>>::iterator materialIterator = std::find_if(
			m_instanced.begin(), m_instanced.end(), 
			[mat](const std::pair<Materials*, std::unordered_map<ShapeBuffer*, std::vector<Model*>>>& pair) { return pair.first == mat; }
		);
		if (materialIterator != m_instanced.end()) 
		{
			m_instanced.erase(materialIterator);
		}
	}

	void ModelManager::printModelInfo(const char *path)
	{
		tinyobj::attrib_t attrib;
		std::vector<tinyobj::shape_t> shapes;
		std::vector<tinyobj::material_t> materials;
		std::string warn, err;
		std::vector<Vertex> vertices;
		std::vector<uint32_t> indices;
		glm::vec3 normalResult;

		if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, path))
		{
			Debug::Warn("%s  %s", nullptr, warn.c_str(), err.c_str());
		}

		std::unordered_map<Vertex, uint32_t> uniqueVertices{};

		for (const auto &shape : shapes)
		{
			Debug::Log("%s", path);
			for (const auto &index : shape.mesh.indices)
			{
				Vertex vertex{};

				vertex.pos = {
					attrib.vertices[3 * index.vertex_index + 0],
					attrib.vertices[3 * index.vertex_index + 1],
					attrib.vertices[3 * index.vertex_index + 2] };

				vertex.texCoord = {
					attrib.texcoords[2 * index.texcoord_index + 0],
					1.0f - attrib.texcoords[2 * index.texcoord_index + 1] };

				vertex.normal = {
					attrib.normals[3 * index.normal_index + 0],
					attrib.normals[3 * index.normal_index + 1],
					attrib.normals[3 * index.normal_index + 2] };

				if (uniqueVertices.count(vertex) == 0)
				{
					uniqueVertices[vertex] = static_cast<uint32_t>(vertices.size());
					vertices.push_back(vertex);
				}

				indices.push_back(uniqueVertices[vertex]);
			}
		}
		Debug::Info("POS: %d", vertices.size());
		for (int i = 0; i < vertices.size(); i++)
		{
			std::cout << vertices[i].pos.x << "," << vertices[i].pos.y << "," << vertices[i].pos.z << ",";
		}
		std::cout << std::endl;
		Debug::Info("TEXCORD: %d", vertices.size());
		for (int i = 0; i < vertices.size(); i++)
		{
			std::cout << vertices[i].texCoord.x << "," << vertices[i].texCoord.y << ",";
		}
		std::cout << std::endl;
		Debug::Info("NORMAL: %d", vertices.size());
		for (int i = 0; i < vertices.size(); i++)
		{
			std::cout << vertices[i].normal.x << "," << vertices[i].normal.y << "," << vertices[i].normal.z << ",";
		}
		std::cout << std::endl;
		Debug::Info("INDICE: %d", indices.size());
		for (int i = 0; i < indices.size(); i++)
		{
			std::cout << indices[i] << ",";
		}
		std::cout << std::endl;
	}

	ShapeBuffer * ModelManager::allocateBuffer(float * pos, float * texCord, float * normal, unsigned int * indice, unsigned vertexSize, unsigned indiceSize)
	{
		std::vector<Vertex> vertices;
		std::vector<uint32_t> indices(indice, indice + indiceSize);

		vertices.reserve(vertexSize);
		for (int i = 0; i < vertexSize; i++)
		{
			Vertex vertex{};

			vertex.pos = {
				pos[3 * i + 0],
				pos[3 * i + 1],
				pos[3 * i + 2] };

			vertex.texCoord = {
				texCord[2 * i + 0],
				texCord[2 * i + 1] };

			vertex.normal = {
				normal[3 * i + 0],
				normal[3 * i + 1],
				normal[3 * i + 2] };

			vertex.color.x = 1;
			vertex.color.y = 1;
			vertex.color.z = 1;
			vertices.push_back(vertex);
		}

		uint32_t index0, index1, index2;
		glm::vec3 edge1, edge2, tangent;
		glm::vec2 deltaUV1, deltaUV2;
		float r;
		for (size_t i = 0; i < indices.size(); i += 3)
		{
			index0 = indices[i];
			index1 = indices[i + 1];
			index2 = indices[i + 2];

			Vertex& vertex0 = vertices[index0];
			Vertex& vertex1 = vertices[index1];
			Vertex& vertex2 = vertices[index2];

			edge1 = vertex1.pos - vertex0.pos;
			edge2 = vertex2.pos - vertex0.pos;

			deltaUV1 = vertex1.texCoord - vertex0.texCoord;
			deltaUV2 = vertex2.texCoord - vertex0.texCoord;

			r = 1.0f / (deltaUV1.x * deltaUV2.y - deltaUV2.x * deltaUV1.y);

			tangent = glm::normalize(r * (deltaUV2.y * edge1 - deltaUV1.y * edge2));
	
			vertex0.tangents = tangent;
			vertex1.tangents = tangent;
			vertex2.tangents = tangent;
		}				
		ShapeBuffer* buffer = (ShapeBuffer*)m_poolBuffer.newObject(vertices, indices, m_gdm);
		m_shapeBuffers.push_back(buffer);
		return buffer;
	}

	ShapeBuffer* ModelManager::getDefferedQuad() const
	{
		return m_defferedQuad;
	}

	ShapeBuffer* ModelManager::getFullScreenTriangle() const
	{
		return m_fullScreenTriangle;
	}

	void ModelManager::ComputationTangent(std::vector<Vertex> &vertices)
	{
		glm::vec3 edge1;
		glm::vec3 edge2;
		glm::vec2 deltaUV1;
		glm::vec2 deltaUV2;
		glm::vec3 tangents;

		float r;
		for (int i = 0; i + 2 < vertices.size(); i += 3)
		{
			edge1 = vertices[i + 1].pos - vertices[i].pos;
			edge2 = vertices[i + 2].pos - vertices[i].pos;

			deltaUV1 = vertices[i + 1].texCoord - vertices[i].texCoord;
			deltaUV2 = vertices[i + 2].texCoord - vertices[i].texCoord;

			r = 1.0f / (deltaUV1.x * deltaUV2.y - deltaUV2.x * deltaUV1.y);

			tangents.x = r * (deltaUV2.y * edge1.x - deltaUV1.y * edge2.x);
			tangents.y = r * (deltaUV2.y * edge1.y - deltaUV1.y * edge2.y);
			tangents.z = r * (deltaUV2.y * edge1.z - deltaUV1.y * edge2.z);

			tangents = glm::normalize(tangents);

			vertices[i + 0].tangents = tangents;
			vertices[i + 1].tangents = tangents;
			vertices[i + 2].tangents = tangents;
		}
	}

	std::vector<ShapeBuffer*> ModelManager::allocateBuffers(const char* path, bool normal_recalculate)
	{
		tinyobj::attrib_t attrib;
		std::vector<tinyobj::shape_t> shapes;
		std::vector<tinyobj::material_t> materials;
		std::vector<ShapeBuffer*> sbvec;
		std::string warn, err;

		if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, path))
		{
			Debug::Warn("%s  %s", warn.c_str(), err.c_str());
			return sbvec;
		}

		std::vector<Vertex> vertices;
		std::vector<uint32_t> indices;
		std::unordered_map<Vertex, uint32_t> uniqueVertices;

		vertices.reserve(attrib.vertices.size() / 3);
		indices.reserve(attrib.vertices.size());

		for (const auto& shape : shapes)
		{
			vertices.clear();
			indices.clear();
			uniqueVertices.clear();
			for (const auto& index : shape.mesh.indices)
			{
				Vertex vertex{};

				vertex.pos = {
					attrib.vertices[3 * index.vertex_index + 0],
					attrib.vertices[3 * index.vertex_index + 1],
					attrib.vertices[3 * index.vertex_index + 2]
				};

				vertex.texCoord = {
					attrib.texcoords[2 * index.texcoord_index + 0],
					1.0f - attrib.texcoords[2 * index.texcoord_index + 1]
				};

				vertex.normal = {
					attrib.normals[3 * index.normal_index + 0],
					attrib.normals[3 * index.normal_index + 1],
					attrib.normals[3 * index.normal_index + 2]
				};

				vertex.color = { 1, 1, 1 };
				vertex.tangents = { 0, 0, 0 };

				if (uniqueVertices.count(vertex) == 0)
				{
					uniqueVertices[vertex] = static_cast<uint32_t>(vertices.size());
					vertices.emplace_back(vertex);
				}

				indices.emplace_back(uniqueVertices[vertex]);
			}

			uint32_t index0, index1, index2;
			glm::vec3 edge1, edge2, tangent;
			glm::vec2 deltaUV1, deltaUV2;
			float r;
			for (size_t i = 0; i < indices.size(); i += 3)
			{
				index0 = indices[i];
				index1 = indices[i + 1];
				index2 = indices[i + 2];

				Vertex& vertex0 = vertices[index0];
				Vertex& vertex1 = vertices[index1];
				Vertex& vertex2 = vertices[index2];

				edge1 = vertex1.pos - vertex0.pos;
				edge2 = vertex2.pos - vertex0.pos;

				deltaUV1 = vertex1.texCoord - vertex0.texCoord;
				deltaUV2 = vertex2.texCoord - vertex0.texCoord;

				r = 1.0f / (deltaUV1.x * deltaUV2.y - deltaUV2.x * deltaUV1.y);

				tangent = glm::normalize(r * (deltaUV2.y * edge1 - deltaUV1.y * edge2));

				vertex0.tangents = tangent;
				vertex1.tangents = tangent;
				vertex2.tangents = tangent;
				if (normal_recalculate)
				{
					glm::vec3 normal = glm::normalize(glm::cross(vertex1.pos - vertex0.pos, vertex2.pos - vertex0.pos));
					vertex0.normal = normal;
					vertex1.normal = normal;
					vertex2.normal = normal;
				}
			}
			ShapeBuffer* buffer = (ShapeBuffer*)m_poolBuffer.newObject(vertices, indices, m_gdm);
			m_shapeBuffers.push_back(buffer);
			sbvec.push_back(buffer);
		}

		return sbvec;
	}

	void ModelManager::updateStorage()
	{
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_ssbo);
		glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(UniformBufferObject)*m_models.size(), nullptr, GL_DYNAMIC_DRAW);
		int count = 0;
		for (auto& material : m_instanced)
		{
			for (auto& buffer : material.second)
			{
				for (auto& model : buffer.second)
				{
					model->setIndex(count++);
				}
			}
		}
	}

	inline void addClusterData(uint8_t c,float w, uint8_t* td,float* wd)
	{		
		for (unsigned int k = 0; k < 4; ++k)
		{			
			if (wd[k] == 0.0f)
			{
				wd[k] = w;
				td[k] = c;
				return;
			}
			if (w > wd[k])
			{
				for (unsigned int i = 3; i > k; --i)
				{
					wd[i] = wd[i - 1];
					td[i] = td[i - 1];
				}

				wd[k] = w;
				td[k] = c;
				return;
			}
		}
	}

	ShapeBuffer* ModelManager::allocateEmptyBuffer(uint32_t indiceSize)
	{
		ShapeBufferEmpty* buffer = new ShapeBufferEmpty(indiceSize);
		m_shapeBuffers.push_back(buffer);
		return buffer;
	}

	std::function<ShapeBufferSkeleton* ()> ModelManager::prepareFBXBufferBones(const char* path,bool normal_recalculate)
	{
		Skeleton skeleton;
		FBXSkinnedData data = loadFBXSkinnedData(skeleton,path,normal_recalculate);

		return [this, data = std::move(data), skeleton = std::move(skeleton)]() mutable -> ShapeBufferSkeleton*
		{
			ShapeBufferSkeleton* buffer = new ShapeBufferSkeleton(data.meshVertexList,data.meshIndicesList,skeleton,m_gdm);
			m_shapeBuffers.push_back(buffer);

			return buffer;
		};
	}

	ShapeBufferSkeleton* ModelManager::allocateFBXBufferBones(const char* path, bool normal_recalculate)
	{
		Skeleton skeleton;
		FBXSkinnedData data = loadFBXSkinnedData(skeleton, path, normal_recalculate);
		ShapeBufferSkeleton* buffer = new ShapeBufferSkeleton(data.meshVertexList,data.meshIndicesList,skeleton,m_gdm);
		m_shapeBuffers.push_back(buffer);

		return buffer;
	}

	ShapeBufferSkeleton* ModelManager::allocateFBXBufferBones(
		const char* path,
		const Skeleton& referenceSkeleton,
		bool normal_recalculate)
	{
		Skeleton skeleton = referenceSkeleton;
		FBXSkinnedData data = loadFBXSkinnedData(
			skeleton,
			path,
			normal_recalculate,
			&referenceSkeleton);
		ShapeBufferSkeleton* buffer = new ShapeBufferSkeleton(
			data.meshVertexList,
			data.meshIndicesList,
			skeleton,
			m_gdm);
		m_shapeBuffers.push_back(buffer);
		return buffer;
	}

	std::function<std::vector<ShapeBuffer*>()> ModelManager::prepareFBXBuffer(const char* path,bool normal_recalculate,std::vector<int> m_loadIdMesh)
	{
		std::vector<FBXMeshData> meshes = loadFBXData(path,normal_recalculate,m_loadIdMesh);

		return [this,meshes = std::move(meshes)]() mutable -> std::vector<ShapeBuffer*>
		{
			std::vector<ShapeBuffer*> sbvec;

			for (auto& mesh : meshes)
			{
				ShapeBuffer* buffer = (ShapeBuffer*)m_poolBuffer.newObject(mesh.vertices,mesh.indices,m_gdm);
				m_shapeBuffers.push_back(buffer);
				sbvec.push_back(buffer);
			}

			return sbvec;
		};
	}

	std::vector<ShapeBuffer*> ModelManager::allocateFBXBuffer(const char* path, bool normal_recalculate, std::vector<int> m_loadIdMesh)
	{
		std::vector<ShapeBuffer*> sbvec;
		std::vector<FBXMeshData> meshes = loadFBXData(path,normal_recalculate,m_loadIdMesh);

		for (auto& mesh : meshes)
		{
			ShapeBuffer* buffer = (ShapeBuffer*)m_poolBuffer.newObject(mesh.vertices,mesh.indices,m_gdm);
			m_shapeBuffers.push_back(buffer);
			sbvec.push_back(buffer);
		}

		return sbvec;
	}

	std::function<ShapeBuffer* ()> ModelManager::prepareBuffer(const char* path,bool normal_recalculate)
	{
		std::vector<Vertex> vertices;
		std::vector<uint32_t> indices;

		loadObjData(vertices, indices, path, normal_recalculate);

		return [this,vertices = std::move(vertices),indices = std::move(indices)]() mutable -> ShapeBuffer*
		{
			ShapeBuffer* buffer = (ShapeBuffer*)m_poolBuffer.newObject(vertices, indices, m_gdm);
			m_shapeBuffers.push_back(buffer);
			return buffer;
		};
	}

	ShapeBuffer* ModelManager::allocateBuffer(const char* path, bool normal_recalculate)
	{		
		std::vector<Vertex> vertices;
		std::vector<uint32_t> indices;
		loadObjData(vertices,indices,path,normal_recalculate);
		ShapeBuffer* buffer = (ShapeBuffer*)m_poolBuffer.newObject(vertices, indices, m_gdm);
		m_shapeBuffers.push_back(buffer);
		return buffer;
	}

	void ModelManager::loadObjData(std::vector<Vertex>& vertices, std::vector<uint32_t>& indices, const char* path, bool normal_recalculate)
	{
		tinyobj::attrib_t attrib;
		std::vector<tinyobj::shape_t> shapes;
		std::vector<tinyobj::material_t> materials;
		std::string warn, err;

		if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, path))
		{
			Debug::Warn("%s  %s", warn.c_str(), err.c_str());
			return;
		}

		std::unordered_map<Vertex, uint32_t> uniqueVertices;

		vertices.reserve(attrib.vertices.size() / 3);
		indices.reserve(attrib.vertices.size());

		for (const auto& shape : shapes)
		{
			for (const auto& index : shape.mesh.indices)
			{
				Vertex vertex{};

				vertex.pos = {
					attrib.vertices[3 * index.vertex_index + 0],
					attrib.vertices[3 * index.vertex_index + 1],
					attrib.vertices[3 * index.vertex_index + 2]
				};

				vertex.texCoord = {
					attrib.texcoords[2 * index.texcoord_index + 0],
					1.0f - attrib.texcoords[2 * index.texcoord_index + 1]
				};

				vertex.normal = {
					attrib.normals[3 * index.normal_index + 0],
					attrib.normals[3 * index.normal_index + 1],
					attrib.normals[3 * index.normal_index + 2]
				};

				vertex.color = { 1, 1, 1 };
				vertex.tangents = { 0, 0, 0 };

				if (uniqueVertices.count(vertex) == 0)
				{
					uniqueVertices[vertex] = static_cast<uint32_t>(vertices.size());
					vertices.emplace_back(vertex);
				}

				indices.emplace_back(uniqueVertices[vertex]);
			}
		}

		uint32_t index0, index1, index2;
		glm::vec3 edge1, edge2, tangent;
		glm::vec2 deltaUV1, deltaUV2;
		float r;
		for (size_t i = 0; i < indices.size(); i += 3)
		{
			index0 = indices[i];
			index1 = indices[i + 1];
			index2 = indices[i + 2];

			Vertex& vertex0 = vertices[index0];
			Vertex& vertex1 = vertices[index1];
			Vertex& vertex2 = vertices[index2];

			edge1 = vertex1.pos - vertex0.pos;
			edge2 = vertex2.pos - vertex0.pos;

			deltaUV1 = vertex1.texCoord - vertex0.texCoord;
			deltaUV2 = vertex2.texCoord - vertex0.texCoord;

			r = 1.0f / (deltaUV1.x * deltaUV2.y - deltaUV2.x * deltaUV1.y);

			tangent = glm::normalize(r * (deltaUV2.y * edge1 - deltaUV1.y * edge2));

			vertex0.tangents = tangent;
			vertex1.tangents = tangent;
			vertex2.tangents = tangent;
			if (normal_recalculate)
			{
				glm::vec3 normal = glm::normalize(glm::cross(vertex1.pos - vertex0.pos, vertex2.pos - vertex0.pos));
				vertex0.normal = normal;
				vertex1.normal = normal;
				vertex2.normal = normal;
			}
		}
	}

	std::vector<FBXMeshData> ModelManager::loadFBXData(const char* path,bool normal_recalculate,std::vector<int> m_loadIdMesh)
	{
		std::vector<FBXMeshData> meshes;
		FILE* fp = fopen(path, "rb");

		if (!fp)
		{
			Debug::Warn("cannot load : %s ", path);
			return meshes;
		}

		fseek(fp, 0, SEEK_END);
		long file_size = ftell(fp);
		fseek(fp, 0, SEEK_SET);

		auto* content = new ofbx::u8[file_size];
		fread(content, 1, file_size, fp);

		ofbx::LoadFlags flags =
			ofbx::LoadFlags::TRIANGULATE |
			ofbx::LoadFlags::IGNORE_BLEND_SHAPES |
			ofbx::LoadFlags::IGNORE_CAMERAS |
			ofbx::LoadFlags::IGNORE_LIGHTS |
			ofbx::LoadFlags::IGNORE_SKIN |
			ofbx::LoadFlags::IGNORE_BONES |
			ofbx::LoadFlags::IGNORE_PIVOTS |
			ofbx::LoadFlags::IGNORE_POSES |
			ofbx::LoadFlags::IGNORE_VIDEOS |
			ofbx::LoadFlags::IGNORE_LIMBS |
			ofbx::LoadFlags::IGNORE_ANIMATIONS;

		ofbx::IScene* scene = ofbx::load((ofbx::u8*)content, file_size, (ofbx::u16)flags);
		if (!scene)
		{
			delete[] content;
			fclose(fp);
			Debug::Warn("failed to parse FBX : %s", path);
			return meshes;
		}
		int size = (m_loadIdMesh.size() > 0) ? m_loadIdMesh.size() : scene->getMeshCount();

		for (int i = 0; i < size; ++i)
		{
			FBXMeshData meshData;

			auto& vertices = meshData.vertices;
			auto& indices = meshData.indices;

			std::unordered_map<Vertex, uint32_t> uniqueVertices;

			const ofbx::Geometry* geom = nullptr;

			if (m_loadIdMesh.size() > 0)
			{
				geom = scene->getMesh(m_loadIdMesh[i] % scene->getMeshCount())->getGeometry();
			}
			else
			{
				geom = scene->getMesh(i)->getGeometry();
			}

			vertices.reserve(geom->getVertexCount());
			indices.reserve(geom->getIndexCount());

			const ofbx::Vec3* position = geom->getVertices();
			const ofbx::Vec2* uv = geom->getUVs();
			const ofbx::Vec2* lightMapUV = geom->getUVs(1);
			const ofbx::Vec3* normals = geom->getNormals();
			const ofbx::Vec3* tangents = geom->getTangents();
			const ofbx::Vec4* colors = geom->getColors();

			for (int j = 0; j < geom->getVertexCount(); ++j)
			{
				Vertex vertex{};

				vertex.pos = { position[j].x,position[j].y,position[j].z };

				vertex.texCoord = { uv[j].x, 1.0f - uv[j].y };

				if (normals && !normal_recalculate)
				{
					vertex.normal = { normals[j].x,normals[j].y,normals[j].z};
				}

				if (tangents)
				{
					vertex.tangents ={tangents[j].x,tangents[j].y,tangents[j].z};
				}
				else
				{
					vertex.tangents = { 0,0,0 };
				}

				if (lightMapUV)
				{
					// Static FBX meshes store UV2 in the otherwise unused color attribute.
					vertex.color = { lightMapUV[j].x, 1.0f - lightMapUV[j].y, 1.0f };
				}
				else if (colors)
				{
					vertex.color ={colors[j].x,colors[j].y,colors[j].z};
				}
				else
				{
					vertex.color = { 1,1,1 };
				}

				if (uniqueVertices.count(vertex) == 0)
				{
					uniqueVertices[vertex] =(uint32_t)vertices.size();
					vertices.emplace_back(vertex);
				}

				indices.emplace_back(uniqueVertices[vertex]);
			}

			if (tangents == nullptr || normal_recalculate || normals == nullptr)
			{
				for (size_t k = 0; k < indices.size(); k += 3)
				{
					uint32_t index0 = indices[k];
					uint32_t index1 = indices[k + 1];
					uint32_t index2 = indices[k + 2];

					Vertex& vertex0 = vertices[index0];
					Vertex& vertex1 = vertices[index1];
					Vertex& vertex2 = vertices[index2];

					if (tangents == nullptr)
					{
						glm::vec3 edge1 = vertex1.pos - vertex0.pos;
						glm::vec3 edge2 = vertex2.pos - vertex0.pos;
						glm::vec2 deltaUV1 = vertex1.texCoord - vertex0.texCoord;
						glm::vec2 deltaUV2 = vertex2.texCoord - vertex0.texCoord;

						float r = 1.0f / (deltaUV1.x * deltaUV2.y - deltaUV2.x * deltaUV1.y);

						glm::vec3 tangent =glm::normalize(r * (deltaUV2.y * edge1 - deltaUV1.y * edge2));

						vertex0.tangents = tangent;
						vertex1.tangents = tangent;
						vertex2.tangents = tangent;
					}

					if (normal_recalculate || normals == nullptr)
					{
						glm::vec3 normal = glm::normalize(glm::cross(vertex1.pos - vertex0.pos,vertex2.pos - vertex0.pos));

						vertex0.normal = normal;
						vertex1.normal = normal;
						vertex2.normal = normal;
					}
				}
			}

			meshes.push_back(std::move(meshData));
		}
		scene->destroy();
		delete[] content;
		fclose(fp);

		return meshes;
	}

	FBXSkinnedData ModelManager::loadFBXSkinnedData(
		Skeleton& skeleton,
		const char* path,
		bool normal_recalculate,
		const Skeleton* referenceSkeleton)
	{
		FBXSkinnedData data;
		FILE* fp = fopen(path, "rb");
		if (!fp)
		{
			Debug::Warn("Cannot load : %s ", path);
			return data;
		}
		fseek(fp, 0, SEEK_END);
		long file_size = ftell(fp);
		fseek(fp, 0, SEEK_SET);
		auto* content = new ofbx::u8[file_size];
		fread(content, 1, file_size, fp);
		ofbx::LoadFlags flags =
			ofbx::LoadFlags::TRIANGULATE |
			ofbx::LoadFlags::IGNORE_CAMERAS |
			ofbx::LoadFlags::IGNORE_LIGHTS |
			ofbx::LoadFlags::IGNORE_TEXTURES |
			ofbx::LoadFlags::IGNORE_MATERIALS |
			ofbx::LoadFlags::IGNORE_VIDEOS;

		ofbx::IScene* scene = ofbx::load((ofbx::u8*)content, file_size, (ofbx::u16)flags);
		if (!scene)
		{
			delete[] content;
			fclose(fp);
			Debug::Warn("failed to parse FBX : %s", path);
			return data;
		}
		if (referenceSkeleton != nullptr)
		{
			if (referenceSkeleton->bones.size() > 255u)
			{
				Debug::Warn(
					"Reference skeleton exceeds the 8-bit bone index limit: %s",
					path);
				scene->destroy();
				delete[] content;
				fclose(fp);
				return data;
			}
			skeleton = *referenceSkeleton;
		}
		else
		{
			skeleton = Skeleton{};
		}

		std::vector<const ofbx::Mesh*> targetMesh;

		unsigned int mc = scene->getMeshCount();
		for (unsigned int i = 0; i < mc; ++i)
		{
			const ofbx::Mesh* mesh = scene->getMesh(i);
			const ofbx::Geometry* geom = mesh->getGeometry();
			if (geom)
			{
				const ofbx::Skin* skin = geom->getSkin();
				if (skin != nullptr)
				{
					targetMesh.push_back(mesh);
				}
			}
		}

		if (targetMesh.size() == 0)
		{
			Debug::Warn("No skinned mesh found in FBX file");
			scene->destroy();
			delete[] content;
			fclose(fp);
			return data;
		}

		if (referenceSkeleton == nullptr)
		{
			const ofbx::Mesh* bindSpaceMesh = targetMesh.front();
			skeleton.bindTransform =
				glm::mat4(glm::make_mat4(
					bindSpaceMesh->getGlobalTransform().m)) *
				glm::mat4(glm::make_mat4(
					bindSpaceMesh->getGeometricMatrix().m));
			const float bindDeterminant =
				glm::determinant(glm::mat3(skeleton.bindTransform));
			if (!std::isfinite(bindDeterminant) ||
				std::abs(bindDeterminant) <= 0.0000001f)
			{
				Debug::Warn(
					"FBX skinned mesh has a singular bind transform: %s",
					path);
				skeleton.bindTransform = glm::mat4(1.0f);
				skeleton.inverseBindTransform = glm::mat4(1.0f);
			}
			else
			{
				skeleton.inverseBindTransform =
					glm::inverse(skeleton.bindTransform);
			}
		}

		std::vector<const ofbx::Object*> parentsBonesID;
		std::unordered_map<const ofbx::Object*, uint16_t> objectToID;

		std::vector<uint8_t> vertexInfluencesID;
		std::vector<float> vertexInfluencesW;

		for (unsigned int m = 0; m < targetMesh.size(); m++)
		{
			const ofbx::Geometry* geom = targetMesh[m]->getGeometry();
			const ofbx::Vec3* position = geom->getVertices();
			const ofbx::Vec2* uv = geom->getUVs();
			const ofbx::Vec3* normals = geom->getNormals();
			const ofbx::Vec3* tangents = geom->getTangents();
			const ofbx::Skin* skin = geom->getSkin();
			const glm::mat4 meshBindTransform =
				glm::mat4(glm::make_mat4(
					targetMesh[m]->getGlobalTransform().m)) *
				glm::mat4(glm::make_mat4(
					targetMesh[m]->getGeometricMatrix().m));
			const glm::mat4 meshToSkeleton =
				skeleton.inverseBindTransform * meshBindTransform;
			const glm::mat3 normalToSkeleton =
				glm::transpose(glm::inverse(glm::mat3(meshToSkeleton)));
			const glm::mat3 tangentToSkeleton =
				glm::mat3(meshToSkeleton);
			const uint32_t* indice_ptr = (uint32_t*)geom->getFaceIndices();
			uint32_t indexCount = geom->getIndexCount();
			std::vector<uint32_t> indice(indice_ptr, indice_ptr + indexCount);
			std::vector<Vertex> vertices;
			vertices.resize(geom->getVertexCount());

			vertexInfluencesID.resize(geom->getVertexCount() * 4);
			vertexInfluencesW.resize(geom->getVertexCount() * 4);
			std::fill(
				vertexInfluencesID.begin(),
				vertexInfluencesID.end(),
				static_cast<uint8_t>(0));
			std::fill(
				vertexInfluencesW.begin(),
				vertexInfluencesW.end(),
				0.0f);
			unsigned int clusterCount = skin->getClusterCount();

			if (clusterCount > 255)
			{
				Debug::Warn("The number of clusters exceeds the maximum number of bones. %d > 255", clusterCount);
				delete[] content;
				fclose(fp);
				return data;
			}
			uint8_t* vidData = vertexInfluencesID.data();
			float* vwData = vertexInfluencesW.data();
			for (unsigned int c = 0; c < clusterCount; ++c)
			{
				const ofbx::Cluster* cluster = skin->getCluster(c);
				const ofbx::Object* obj = cluster != nullptr ? cluster->getLink() : nullptr;
				if (cluster == nullptr || obj == nullptr)
				{
					Debug::Warn("FBX contains a skin cluster without a linked bone: %s", path);
					continue;
				}

				const unsigned int indicesCountSize = cluster->getIndicesCount();
				auto findObj = objectToID.find(obj);
				unsigned int cid = 0;
				if (findObj == objectToID.end())
				{
					uint16_t id = 0;
					if (referenceSkeleton != nullptr)
					{
						bool mapped = false;
						const auto pathIt = referenceSkeleton->pathToID.find(
							getFBXObjectPath(obj));
						if (pathIt != referenceSkeleton->pathToID.end())
						{
							id = pathIt->second;
							mapped = true;
						}
						else
						{
							const auto nameIt =
								referenceSkeleton->nameToID.find(obj->name);
							if (nameIt != referenceSkeleton->nameToID.end())
							{
								id = nameIt->second;
								mapped = true;
							}
						}
						if (!mapped)
						{
							Debug::Warn(
								"Cannot map skinned part bone %s to the reference skeleton in %s",
								obj->name,
								path);
							continue;
						}
					}
					else
					{
						id = static_cast<uint16_t>(skeleton.bones.size());
						Bone bone{};
						bone.idGlobal = id;
						bone.name = obj->name;
						bone.parent = -1;
						const ofbx::Matrix linkMatrix =
							cluster->getTransformLinkMatrix();
						const glm::mat4 link = glm::make_mat4(linkMatrix.m);
						bone.bindPose = link;
						bone.inverseBindPose = glm::inverse(link);
						bone.worldPoseCluster =
							glm::make_mat4(cluster->getGlobalTransform().m);
						bone.worldPoseObj =
							glm::make_mat4(obj->getGlobalTransform().m);

						skeleton.bones.push_back(bone);
						parentsBonesID.push_back(obj);
					}
					objectToID[obj] = id;
					cid = id;
				}
				else
				{
					cid = findObj->second;
				}

				if (indicesCountSize == 0)
				{
					continue;
				}
				const int* vtxIndices = cluster->getIndices();
				const double* weights = cluster->getWeights();
				for (unsigned int j = 0; j < indicesCountSize; ++j)
				{
					const int vertexIndex = vtxIndices[j];
					if (vertexIndex < 0 ||
						static_cast<size_t>(vertexIndex) >= vertices.size())
					{
						continue;
					}
					const float weight = static_cast<float>(weights[j]);
					uint8_t* vertexIDs = vidData + (vertexIndex * 4);
					float* vertexWeights = vwData + (vertexIndex * 4);
					addClusterData(
						static_cast<uint8_t>(cid),
						weight,
						vertexIDs,
						vertexWeights);
				}
			}
			glm::uvec3 packedData;
			for (unsigned int j = 0; j < vertices.size(); ++j)
			{
				Vertex& vertex = vertices[j];
				vertex.pos = glm::vec3(
					meshToSkeleton *
					glm::vec4(
						position[j].x,
						position[j].y,
						position[j].z,
						1.0f));

				vertex.texCoord.x = uv[j].x;
				vertex.texCoord.y = 1.0f - uv[j].y;

				if (normals != nullptr && !normal_recalculate)
				{
					vertex.normal = glm::normalize(
						normalToSkeleton *
						glm::vec3(
							normals[j].x,
							normals[j].y,
							normals[j].z));
				}

				if (tangents != nullptr)
				{
					vertex.tangents = glm::normalize(
						tangentToSkeleton *
						glm::vec3(
							tangents[j].x,
							tangents[j].y,
							tangents[j].z));
				}
				else
				{
					vertex.tangents = { 0, 0, 0 };
				}

				packedData.x =
					vertexInfluencesID[j * 4] |
					(vertexInfluencesID[j * 4 + 1] << 8) |
					(vertexInfluencesID[j * 4 + 2] << 16) |
					(vertexInfluencesID[j * 4 + 3] << 24);


				glm::vec4 weight = glm::vec4(vertexInfluencesW[j * 4], vertexInfluencesW[j * 4 + 1], vertexInfluencesW[j * 4 + 2], vertexInfluencesW[j * 4 + 3]);
				float sum = glm::max(0.0001f, weight.x + weight.y + weight.z + weight.w);
				packedData.y =
					((unsigned int)((weight.x / sum) * 65535) & 0xFFFF) |
					(((unsigned int)((weight.y / sum) * 65535) & 0xFFFF) << 16);

				packedData.z =
					((unsigned int)((weight.z / sum) * 65535) & 0xFFFF) |
					(((unsigned int)((weight.w / sum) * 65535) & 0xFFFF) << 16);


				vertex.color.x = *(float*)&packedData.x;
				vertex.color.y = *(float*)&packedData.y;
				vertex.color.z = *(float*)&packedData.z;

				//on utilise les bit de color pour ajouté les infos compresser pour optimiser
				//on normalise les poids à l'avance sur 16 bit [0.0-1.0] * (2^16-1)
				//vertex.color.x -> (8 bit bonesID) * 4 
				//vertex.color.y + vertex.color.z -> (16 bit weight) * 4
				indice[j] = j;
			}

			if (tangents == nullptr || normal_recalculate || normals == nullptr)
			{
				uint32_t index0, index1, index2;
				glm::vec3 edge1, edge2, tangent;
				glm::vec2 deltaUV1, deltaUV2;
				float r;
				for (size_t i = 0; i < indexCount; i += 3)
				{
					index0 = indice[i];
					index1 = indice[i + 1];
					index2 = indice[i + 2];

					Vertex& vertex0 = vertices[index0];
					Vertex& vertex1 = vertices[index1];
					Vertex& vertex2 = vertices[index2];

					if (tangents == nullptr)
					{
						edge1 = vertex1.pos - vertex0.pos;
						edge2 = vertex2.pos - vertex0.pos;

						deltaUV1 = vertex1.texCoord - vertex0.texCoord;
						deltaUV2 = vertex2.texCoord - vertex0.texCoord;

						r = 1.0f / (deltaUV1.x * deltaUV2.y - deltaUV2.x * deltaUV1.y);

						tangent = glm::normalize(r * (deltaUV2.y * edge1 - deltaUV1.y * edge2));

						vertex0.tangents = tangent;
						vertex1.tangents = tangent;
						vertex2.tangents = tangent;
					}
					if (normal_recalculate || normals == nullptr)
					{
						glm::vec3 normal = glm::normalize(glm::cross(vertex1.pos - vertex0.pos, vertex2.pos - vertex0.pos));
						vertex0.normal = normal;
						vertex1.normal = normal;
						vertex2.normal = normal;
					}
				}
			}
			data.meshVertexList.push_back(std::move(vertices));
			data.meshIndicesList.push_back(std::move(indice));
		}
		if (referenceSkeleton == nullptr)
		{
			for (size_t i = 0; i < skeleton.bones.size(); ++i)
			{
				const ofbx::Object* p = parentsBonesID[i]->getParent();
				while (p != nullptr && objectToID.count(p) == 0)
				{
					p = p->getParent();
				}
				if (p != nullptr)
				{
					skeleton.bones[i].parent = objectToID[p];
				}

				const std::string fullPath =
					getFBXObjectPath(parentsBonesID[i]);
				skeleton.pathToID[fullPath] =
					static_cast<uint16_t>(i);
				skeleton.nameToID[skeleton.bones[i].name] =
					static_cast<uint16_t>(i);
			}
		}
		vertexInfluencesID.clear();
		vertexInfluencesW.clear();
		scene->destroy();
		delete[] content;
		fclose(fp);
		return data;
	}
}