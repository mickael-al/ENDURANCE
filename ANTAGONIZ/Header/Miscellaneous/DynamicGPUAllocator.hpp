#ifndef __DYNAMIC_GPU_ALLOCATOR__
#define __DYNAMIC_GPU_ALLOCATOR__

#include <glcore.hpp>
#include <vector>
#include <cstdint>
#include <map>

#ifndef max_gpu
#define max_gpu(a,b)            (((a) > (b)) ? (a) : (b))
#endif

#ifndef min_gpu
#define min_gpu(a,b)            (((a) < (b)) ? (a) : (b))
#endif

class DynamicGPUAllocator 
{
public:
    DynamicGPUAllocator(size_t initialSize = 1024 * 1024,GLenum target = GL_SHADER_STORAGE_BUFFER)
    {
        capacity = initialSize;
        m_target = target;
        glGenBuffers(1, &ssbo);
        glBindBuffer(m_target, ssbo);
        glBufferData(m_target, capacity, nullptr, GL_DYNAMIC_DRAW);
        freeList[0] = capacity;        
    }

    ~DynamicGPUAllocator() 
    {
        if (ssbo != 0)
        {
            glDeleteBuffers(1, &ssbo);
        }
    }
    
    size_t allocate(const void* data, size_t size) // Alloue un bloc et retourne son offset dans le SSBO
    {
        // Trouve un bloc libre suffisamment grand
        for (auto it = freeList.begin(); it != freeList.end(); ++it) 
        {
            size_t offset = it->first;
            size_t blockSize = it->second;
            if (blockSize >= size) 
            {
                glBindBuffer(m_target, ssbo);
                glBufferSubData(m_target, offset, size, data);

                usedBlocks[offset] = size;

                // Met à jour la freeList
                if (blockSize > size) 
                {
                    freeList[offset + size] = blockSize - size;
                }
                freeList.erase(it);
                return offset;
            }
        }

        // Pas assez de place : resize
        resize(capacity * 2);
        return allocate(data, size); // relance après redimensionnement
    }

    bool getUsedRange(size_t& minOffset, size_t& maxOffset) const
    {
        if (usedBlocks.empty())
        {
            return false;
        }

        minOffset = SIZE_MAX;
        maxOffset = 0;

        for (const auto& [offset, size] : usedBlocks)
        {
            minOffset = min_gpu(minOffset, offset);
            maxOffset = max_gpu(maxOffset, offset + size);
        }

        return true;
    }

    std::vector<uint8_t> readUsedData()
    {
        size_t minOffset, maxOffset;

        if (!getUsedRange(minOffset, maxOffset))
            return {};

        size_t size = maxOffset - minOffset;

        std::vector<uint8_t> cpuData(size);

        glBindBuffer(m_target, ssbo);
        glGetBufferSubData(m_target, minOffset, size, cpuData.data());

        return cpuData;
    }

    // Supprime un bloc
    void free(size_t offset) 
    {
        auto it = usedBlocks.find(offset);
        if (it != usedBlocks.end()) 
        {
            size_t size = it->second;
            usedBlocks.erase(it);
            freeList[offset] = size;
            coalesce();
        }
    }

    // Édite un bloc existant (même taille)
    void update(size_t offset, const void* data, size_t size) 
    {
        auto it = usedBlocks.find(offset);
        if (it != usedBlocks.end() && it->second >= size) 
        {
            glBindBuffer(m_target, ssbo);
            glBufferSubData(m_target, offset, size, data);
        }
    }

    void update(size_t offset, size_t offset_mem, const void* data, size_t size)
    {
        auto it = usedBlocks.find(offset);
        if (it != usedBlocks.end() && it->second >= size)
        {
            glBindBuffer(m_target, ssbo);
            glBufferSubData(m_target, offset_mem, size, data);
        }
    }

    bool setCapacity(size_t size)
    {
        if (size < capacity)
        {
            return false;
        }
        resize(size);
        return true;
    }

    void clear()
    {
        freeList.clear();
        usedBlocks.clear();
        freeList[0] = capacity;
    }

    unsigned int getBufferID() const { return ssbo; }

private:
    unsigned int ssbo = 0;
    size_t capacity = 0;
    GLenum m_target;

    std::map<size_t, size_t> freeList;      
    std::map<size_t, size_t> usedBlocks;  

    // Fusionne les blocs libres adjacents
    void coalesce() 
    {
        std::map<size_t, size_t> newFree;
        size_t prevOffset = SIZE_MAX, prevSize = 0;

        for (auto& [offset, size] : freeList) 
        {
            if (prevOffset + prevSize == offset) 
            {
                prevSize += size;
            }
            else 
            {
                if (prevOffset != SIZE_MAX)
                {
                    newFree[prevOffset] = prevSize;
                }
                prevOffset = offset;
                prevSize = size;
            }
        }
        if (prevOffset != SIZE_MAX)
        {
            newFree[prevOffset] = prevSize;
        }

        freeList = std::move(newFree);
    }

    void resize(size_t newCapacity) 
    {
        unsigned int newBuffer;
        glGenBuffers(1, &newBuffer);
        glBindBuffer(GL_COPY_WRITE_BUFFER, newBuffer);
        glBufferData(GL_COPY_WRITE_BUFFER, newCapacity, nullptr, GL_DYNAMIC_DRAW);

        glBindBuffer(GL_COPY_READ_BUFFER, ssbo);        
        glCopyBufferSubData(GL_COPY_READ_BUFFER, GL_COPY_WRITE_BUFFER, 0, 0, capacity);

        glDeleteBuffers(1, &ssbo);
        ssbo = newBuffer;        
        if (freeList.empty())
        {
            freeList[capacity] = newCapacity - capacity;
        }
        else
        {
            bool found = false;
            for (auto& val : freeList)
            {
                size_t offs = val.first + val.second;
                if (offs == capacity)
                {
                    val.second += (newCapacity - capacity);
                    found = true;
                    break;
                }
            }
            if (!found)
            {
                freeList[capacity] = newCapacity - capacity;
            }
        }
        capacity = newCapacity;
    }
};

#endif //!__DYNAMIC_GPU_ALLOCATOR__