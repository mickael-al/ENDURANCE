#include <glcore.hpp>
#include "ParticleSystem.hpp"
#include "ShaderUtil.hpp"
#include "Engine.hpp"
#include "Camera.hpp"
#include "RenderingEngine.hpp"
#include "ShaderPair.hpp"
#include "GraphicsDataMisc.hpp"

namespace Ge
{
    ParticleSystem::ParticleSystem()
    {
        m_pc = Engine::getPtrClassAddr();
        m_allocator = new DynamicGPUAllocator(1024 * 128);
        m_instancingGroup = new DynamicGPUAllocator(1024 * 4);
        glGenVertexArrays(1, &m_vao);
        glBindVertexArray(m_vao);
        glBindVertexArray(0);
        m_baseRender = m_pc->graphiquePipelineManager->createPipeline("../Asset/Shader/particle.fs.glsl", "../Asset/Shader/particle.vs.glsl", false,true,true,2);
        m_baseCs = new ComputeShader("../Asset/Shader/particle_base.C.glsl");
        m_init = new ComputeShader("../Asset/Shader/init_particle.C.glsl");
        m_ufill = new ComputeShader("../Asset/Shader/UClear.C.glsl");
        m_pc->renderingEngine->addTransparencyCallback(this, std::bind(&ParticleSystem::render, this, std::placeholders::_1));
    }

    ParticleSystem::~ParticleSystem()
    {        
        m_pc->graphiquePipelineManager->destroyPipeline(m_baseRender);
        delete m_ufill;
        delete m_init;
        delete m_baseCs;
        glDeleteVertexArrays(1, &m_vao);
        delete m_instancingGroup;
        delete m_allocator;
        m_pc->renderingEngine->removeTransparencyCallback(this);
    }

    size_t ParticleSystem::createGroup(void* settings, size_t sizeSettings, size_t nbParticule, Textures* tex, ComputeShader* cs, GraphiquePipeline* gp, GraphiquePipeline* dgp)
    {        
        ParticleGroup group;
        group.offsetSettings = m_allocator->allocate(settings, sizeSettings) / sizeof(float);
        group.countSettings = size_t(double(sizeSettings + (sizeof(float)-1))/sizeof(float));
        group.offsetData = m_allocator->allocate(nullptr, nbParticule * sizeof(Particle)) / sizeof(float);
        group.countData = nbParticule;
        if (cs != nullptr)
        {
            group.cs = cs;
        }
        else
        {
            group.cs = m_baseCs;
        }
        if (gp != nullptr)
        {
            group.pipeline = gp;
        }
        else
        {
            group.pipeline = m_baseRender;
        }
        group.depth_pipeline = dgp;
        group.texture = tex;    

        int x, y, z;
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, m_allocator->getBufferID());        
        m_init->use();
        glUniform1ui(glGetUniformLocation(m_init->getProgram(), "u_offset"), (unsigned int)group.offsetData);
        glUniform1ui(glGetUniformLocation(m_init->getProgram(), "u_offsetSettings"), (unsigned int)group.offsetSettings);
        glUniform1ui(glGetUniformLocation(m_init->getProgram(), "u_count"), (unsigned int)group.countData);        
        ShaderUtil::CalcWorkSize(group.countData, &x, &y, &z);
        m_init->dispatch(x, y, z);
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

        m_groups[++m_counterNewID] = group;
        updateInstanced();
        return m_counterNewID;
    }

    void ParticleSystem::updateGroup(size_t groupID, void* settings, size_t sizeSettings)
    {
        auto it = m_groups.find(groupID);
        if (it == m_groups.end())
        {
            return;
        }
        auto& group = it->second;
        m_allocator->update(group.offsetSettings*sizeof(float), settings, sizeSettings);
    }

    void ParticleSystem::restartGroup(size_t groupID)
    {
        const auto it = m_groups.find(groupID);
        if (it == m_groups.end())
        {
            return;
        }

        const ParticleGroup& group = it->second;
        int x, y, z;
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, m_allocator->getBufferID());
        m_init->use();
        glUniform1ui(
            glGetUniformLocation(m_init->getProgram(), "u_offset"),
            static_cast<unsigned int>(group.offsetData));
        glUniform1ui(
            glGetUniformLocation(m_init->getProgram(), "u_offsetSettings"),
            static_cast<unsigned int>(group.offsetSettings));
        glUniform1ui(
            glGetUniformLocation(m_init->getProgram(), "u_count"),
            static_cast<unsigned int>(group.countData));
        ShaderUtil::CalcWorkSize(group.countData, &x, &y, &z);
        m_init->dispatch(x, y, z);
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
    }

    void ParticleSystem::removeGroup(size_t groupID)
    {
        auto it = m_groups.find(groupID);
        if (it == m_groups.end())
        {
            return;
        }
        auto& group = it->second;
        m_allocator->free(group.offsetSettings * sizeof(float));
        m_allocator->free(group.offsetData * sizeof(float));
        m_groups.erase(it);
        updateInstanced();
    }

    void ParticleSystem::updateInstanced()
    {
        m_renderBatches.clear();
        std::vector<GpuGroupData> packedData;
        std::vector<unsigned int> packedInstance;
        packedData.reserve(m_groups.size());
        packedInstance.reserve(m_groups.size());
        std::vector<std::pair<const size_t,Ge::ParticleGroup>*> stack;
        stack.reserve(m_groups.size());
        for (auto& data : m_groups) { stack.push_back(&data); }
        while(stack.size() > 0)
        {
            auto* currentStack = stack.back();
            stack.pop_back();
            ParticleGroup& pgm = currentStack->second;
            size_t instanceCount = 1;
            packedInstance.push_back(packedData.size() * 4);
            packedData.push_back({ (uint32_t)pgm.offsetSettings ,(uint32_t)pgm.countSettings ,(uint32_t)pgm.offsetData ,(uint32_t)pgm.countData });
            for (int j = (int)stack.size()-1; j >= 0; --j)
            {
                if (currentStack->second.Same(&stack[j]->second))
                {
                    ParticleGroup & pg = stack[j]->second;
                    packedInstance.push_back(packedData.size() * 4);
                    packedData.push_back({ (uint32_t)pg.offsetSettings ,(uint32_t)pg.countSettings ,(uint32_t)pg.offsetData ,(uint32_t)pg.countData });                   
                    ++instanceCount;
                    stack.erase(stack.begin() + j);
                }
            }
            m_renderBatches.push_back({ currentStack->first, instanceCount });
        }
        for (unsigned int i = 0; i < packedInstance.size(); ++i)
        {
            packedInstance[i] += packedInstance.size();
        }       

        m_instancingGroup->clear();
        if (packedData.empty())
        {
            return;
        }
        m_instancingGroup->allocate(nullptr, packedData.size() * sizeof(GpuGroupData) + packedInstance.size() * sizeof(uint32_t));
        m_instancingGroup->update(0, (void*)packedInstance.data(), packedInstance.size() * sizeof(uint32_t));
        m_instancingGroup->update(0,packedInstance.size() * sizeof(uint32_t), (void*)packedData.data(), packedData.size() * sizeof(GpuGroupData));
    }

    inline uint16_t PackScale(float scale)
    {
        scale = std::clamp(scale, 0.0f, 64.0f);
        return uint16_t((scale / 64.0f) * 65535.0f);
    }

    inline uint16_t PackVelocity(float v)
    {
        v /= 256.0f;
        v = std::clamp(v, -1.0f, 1.0f);

        float norm = (v + 1.0f) * 0.5f;
        return uint16_t(norm * 65535.0f);
    }

    void ParticleSystem::render(bool * currentDepthWrite)
    {
        if (m_renderBatches.empty())
        {
            return;
        }
        glBindVertexArray(m_vao);
        if (*currentDepthWrite)
        {
            *currentDepthWrite = false;
            glDepthMask(GL_FALSE);
        }
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 8, m_pc->gdm->str_work_id);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 6, m_allocator->getBufferID());
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 7, m_instancingGroup->getBufferID());
        unsigned int total_offset = 0;
        for (const ParticleRenderBatch& batch : m_renderBatches)
        {
            auto groupIt = m_groups.find(batch.representativeGroupID);
            if (groupIt == m_groups.end())
            {
                return;
            }
            auto& group = groupIt->second;
            ShaderPair* sp = group.pipeline->getShaderPair();
            unsigned int total_instance = (unsigned int)batch.instanceCount;
            if (sp->cullMode == 2)
            {
                glDisable(GL_CULL_FACE);
            }
            if (group.depth_pipeline != nullptr)
            {
                // The next depth-aware texture needs its own coverage.
                int clearX, clearY, clearZ;
                const size_t pixelCount = static_cast<size_t>(m_pc->gdm->str_width) *
                                          static_cast<size_t>(m_pc->gdm->str_height);
                ShaderUtil::CalcWorkSize(static_cast<int>((pixelCount + 1) / 2), &clearX, &clearY, &clearZ);
                glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
                m_ufill->use();
                m_ufill->dispatch(clearX, clearY, clearZ);
                glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
            }
            glUseProgram(group.pipeline->getProgram());
            glActiveTexture(GL_TEXTURE10);
            glBindTexture(GL_TEXTURE_2D, group.texture->getTextureID());
            if (group.depth_pipeline != nullptr)
            {
                glUniform2ui(glGetUniformLocation(group.pipeline->getProgram(), "u_count"), (unsigned int)group.countData, total_offset);
                glUniform1ui(glGetUniformLocation(group.pipeline->getProgram(), "u_width"), (unsigned int)m_pc->gdm->str_width);
                glDrawArraysInstanced(GL_TRIANGLES, 0, group.countData * 3, total_instance);
                glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

                glUseProgram(group.depth_pipeline->getProgram());
                glUniform2ui(glGetUniformLocation(group.depth_pipeline->getProgram(), "u_count"), (unsigned int)group.countData, total_offset);
                glUniform1ui(glGetUniformLocation(group.depth_pipeline->getProgram(), "u_width"), (unsigned int)m_pc->gdm->str_width);
                glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
                glDepthMask(GL_TRUE);
                glDrawArraysInstanced(GL_TRIANGLES, 0, group.countData * 3, total_instance);
                glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
                glDepthMask(GL_FALSE);
            }
            else
            {
                glUniform2ui(glGetUniformLocation(group.pipeline->getProgram(), "u_count"), (unsigned int)group.countData, total_offset);
                glDrawArraysInstanced(GL_TRIANGLES, 0, group.countData * 3, total_instance);
            }
            
            if (sp->cullMode == 2)
            {
                glEnable(GL_CULL_FACE);
            }
            int x, y, z;
            
            group.cs->use();

            glUniform3ui(glGetUniformLocation(group.cs->getProgram(), "u_instance"), (unsigned int)group.countData, total_offset, total_instance);
            glUniform1f(glGetUniformLocation(group.cs->getProgram(), "u_dt"), glm::clamp(m_pc->time->getDeltaTime(), 0.0f, 1.0f / 32.0f));
            ShaderUtil::CalcWorkSize(group.countData* total_instance, &x, &y, &z);
            group.cs->dispatch(x, y, z);
            glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT);
            total_offset += total_instance;
        }
        glBindVertexArray(0);
        glUseProgram(0);
        /*glBindVertexArray(m_vao);
        if (*currentDepthWrite)
        {
            *currentDepthWrite = false;
            glDepthMask(GL_FALSE);
        }     //TODO pack draw per instance  
        m_ufill->use();
        int x, y, z;
        ShaderUtil::CalcWorkSize(m_pc->gdm->str_width * m_pc->gdm->str_height, &x, &y, &z);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 7, m_pc->gdm->str_work_id);
        m_ufill->dispatch(x, y, z);
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
        for (auto& it : m_groups)
        {
            auto& group = it.second;
            ShaderPair* sp = group.pipeline->getShaderPair();
            if (sp->cullMode == 2)
            {
                glDisable(GL_CULL_FACE);
            }
            glUseProgram(group.pipeline->getProgram());
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 6, m_allocator->getBufferID());
            glUniform1ui(glGetUniformLocation(group.pipeline->getProgram(), "u_offset"), (unsigned int)group.offsetData);
            glUniform1ui(glGetUniformLocation(group.pipeline->getProgram(), "u_offsetSettings"), (unsigned int)group.offsetSettings);
            glUniform1ui(glGetUniformLocation(group.pipeline->getProgram(), "u_count"), (unsigned int)group.countData);
            glActiveTexture(GL_TEXTURE10);
            glBindTexture(GL_TEXTURE_2D, group.texture->getTextureID());            
            if(group.depth_pipeline != nullptr)
            {
                glUniform1ui(glGetUniformLocation(group.pipeline->getProgram(), "u_width"), (unsigned int)m_pc->gdm->str_width);
                glDrawArrays(GL_TRIANGLES, 0, group.countData * 3);

                glUseProgram(group.depth_pipeline->getProgram());
                glUniform1ui(glGetUniformLocation(group.depth_pipeline->getProgram(), "u_offset"), (unsigned int)group.offsetData);
                glUniform1ui(glGetUniformLocation(group.depth_pipeline->getProgram(), "u_offsetSettings"), (unsigned int)group.offsetSettings);
                glUniform1ui(glGetUniformLocation(group.depth_pipeline->getProgram(), "u_count"), (unsigned int)group.countData);
                glUniform1ui(glGetUniformLocation(group.depth_pipeline->getProgram(), "u_width"), (unsigned int)m_pc->gdm->str_width);
                glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
                glDepthMask(GL_TRUE);
                glDrawArrays(GL_TRIANGLES, 0, group.countData * 3);
                glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
                glDepthMask(GL_FALSE);
            }
            else
            {
                glDrawArrays(GL_TRIANGLES, 0, group.countData * 3);
            }
            if (sp->cullMode == 2)
            {
                glEnable(GL_CULL_FACE);
            }
            int x, y, z;
            group.cs->use();
            glUniform1ui(glGetUniformLocation(group.cs->getProgram(), "u_offset"), (unsigned int)group.offsetData);
            glUniform1ui(glGetUniformLocation(group.cs->getProgram(), "u_offsetSettings"), (unsigned int)group.offsetSettings);
            glUniform1ui(glGetUniformLocation(group.cs->getProgram(), "u_count"), (unsigned int)group.countData);
            glUniform1f(glGetUniformLocation(group.cs->getProgram(), "u_dt"), glm::clamp(m_pc->time->getDeltaTime(),0.0f,1.0f/32.0f));
            ShaderUtil::CalcWorkSize(group.countData, &x, &y, &z);
            group.cs->dispatch(x, y, z);
            glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT);
        }

        glBindVertexArray(0);
        glUseProgram(0);*/
    }
}
