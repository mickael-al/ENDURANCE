#ifndef __PARTICLE_SYSTEM__
#define __PARTICLE_SYSTEM__

#include <vector>
#include <glm/glm.hpp>
#include "ParticleSettings.hpp"
#include "DynamicGPUAllocator.hpp"
#include "ComputeShader.hpp"
#include "GraphiquePipeline.hpp"
#include "Textures.hpp"
#include "PointeurClass.hpp"

namespace Ge
{
    struct Particle
    {
        glm::vec3 position;
        uint16_t cr;
        uint16_t cg;
        uint16_t cb;
        uint16_t ca;
        float time;
        uint16_t scale;
        uint16_t vx;
        uint16_t vy;
        uint16_t vz;
    };

    struct GpuGroupData
    {
        uint32_t offsetSettings;
        uint32_t countSettings;
        uint32_t offsetData;
        uint32_t countData;
    };

    struct ParticleGroup
    {        
        size_t offsetSettings;
        size_t countSettings;
        size_t offsetData;
        size_t countData;        
        ComputeShader* cs = nullptr;
        GraphiquePipeline* pipeline = nullptr;
        GraphiquePipeline* depth_pipeline = nullptr;
        Textures* texture = nullptr;

        bool Same(ParticleGroup * other)
        {
            return pipeline == other->pipeline &&
                depth_pipeline == other->depth_pipeline &&
                texture == other->texture && 
                countData == other->countData &&
                cs == other->cs;
        }
    };

    struct ParticleRenderBatch
    {
        size_t representativeGroupID;
        size_t instanceCount;
    };

    class ParticleSystem
    {
    public:
        ParticleSystem();
        ~ParticleSystem();

        size_t createGroup(void * settings,size_t sizeSettings, size_t nbParticule, Textures* tex, ComputeShader* cs = nullptr, GraphiquePipeline* gp = nullptr, GraphiquePipeline* dgp = nullptr);
        void updateGroup(size_t groupID, void* settings, size_t sizeSettings);
        void restartGroup(size_t groupID);
        void removeGroup(size_t groupID);
        void render(bool* currentDepthWrite);
        void updateInstanced();
    private:
        const ptrClass* m_pc = nullptr;
        DynamicGPUAllocator * m_allocator;
        DynamicGPUAllocator * m_instancingGroup;
        std::unordered_map<size_t,ParticleGroup> m_groups;
        std::vector<ParticleRenderBatch> m_renderBatches;
        unsigned int m_vao = 0;
        GraphiquePipeline* m_baseRender = nullptr;
        ComputeShader * m_baseCs = nullptr;
        ComputeShader * m_init = nullptr;        
        ComputeShader * m_ufill = nullptr;
        size_t m_counterNewID = 0;
    };
}

#endif //!__PARTICLE_SYSTEM__
