#ifndef __ENGINE_MODEL_MANAGER___
#define __ENGINE_MODEL_MANAGER___

#include "Initializer.hpp"
#include "Manager.hpp"
#include <map>
#include <unordered_map>
#include <vector>
#include <glm/glm.hpp>
#include <string>
#include <functional>
#include "MemoryManager.hpp"

namespace Ge
{
	class ShapeBuffer;
	class ShapeBufferBase;
	class ShapeBufferSkeleton;
	class Model;
	class Materials;
}

struct Vertex;
struct Skeleton;

struct FBXMeshData
{
	std::vector<Vertex> vertices;
	std::vector<uint32_t> indices;
};

struct FBXSkinnedData
{
	std::vector<std::vector<Vertex>> meshVertexList;
	std::vector<std::vector<uint32_t>> meshIndicesList;
};

namespace Ge
{
    class ModelManager final : public InitializerAPI, public Manager
    {
	protected:
		friend class RenderingEngine;
		bool initialize(GraphicsDataMisc * gdm);		
		void release();
		void updateStorage();
	public:
        ShapeBuffer * allocateBuffer(const char *path, bool normal_recalculate = false);
		std::function<ShapeBuffer*()> prepareBuffer(const char* path, bool normal_recalculate = false);
        std::vector<ShapeBuffer*> allocateBuffers(const char* path, bool normal_recalculate = false);
		ShapeBuffer * allocateBuffer(float * pos, float * texCord, float * normal, unsigned int * indice, unsigned vertexSize, unsigned indiceSize);
		ShapeBufferSkeleton* allocateFBXBufferBones(const char* path, bool normal_recalculate = false);
		ShapeBufferSkeleton* allocateFBXBufferBones(
			const char* path,
			const Skeleton& referenceSkeleton,
			bool normal_recalculate = false);
		std::function<ShapeBufferSkeleton* ()> prepareFBXBufferBones(const char* path, bool normal_recalculate = false);
		std::vector<ShapeBuffer*> allocateFBXBuffer(const char* path, bool normal_recalculate, std::vector<int> m_loadIdMesh);
		std::function<std::vector<ShapeBuffer*>()> prepareFBXBuffer(const char* path, bool normal_recalculate, std::vector<int> m_loadIdMesh);
		void printModelInfo(const char *path);
        Model * createModel(ShapeBuffer *buffer, std::string nom = "Model");
        void destroyModel(Model *model);
        void destroyBuffer(ShapeBuffer *buffer);
        std::vector<Model*> & getModels();	
		std::unordered_map<Materials*, std::unordered_map<ShapeBuffer*,std::vector<Model*>>> & getInstancedModels();		
        void ComputationTangent(std::vector<Vertex> & vertices);
		ShapeBuffer* getDefferedQuad() const;
		ShapeBuffer* getFullScreenTriangle() const;
		ShapeBuffer* allocateEmptyBuffer(uint32_t indiceSize);
		void loadObjData(std::vector<Vertex>& vertices, std::vector<uint32_t>& indices, const char* path, bool normal_recalculate);
		std::vector<FBXMeshData> loadFBXData(const char* path, bool normal_recalculate, std::vector<int> m_loadIdMesh);
		FBXSkinnedData loadFBXSkinnedData(
			Skeleton& skeleton,
			const char* path,
			bool normal_recalculate,
			const Skeleton* referenceSkeleton = nullptr);
	private:
		friend class Model;		
		void buildInstancedModels(Model * target, Materials * mat);
		friend class MaterialManager;
		void clearInstancedMaterial(Materials * mat);
    private:		
		MemoryPool<Model> m_pool;
		MemoryPool<ShapeBufferBase> m_poolBuffer;		
        std::vector<Model*> m_models;
		std::unordered_map<Materials*, std::unordered_map<ShapeBuffer*, std::vector<Model*>>> m_instanced;		
        std::vector<ShapeBuffer *> m_shapeBuffers;        
		GraphicsDataMisc * m_gdm;
		ShapeBuffer* m_defferedQuad;
		ShapeBuffer* m_fullScreenTriangle;
    };
}

#endif //!__ENGINE_MODEL_MANAGER___