#ifndef __MEMORY_MANAGER__
#define __MEMORY_MANAGER__

#include <iostream>
#include <cstring>
#include <vector>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>

template<typename T>
class MemoryPool 
{
public:
    MemoryPool() : head(nullptr), freePtr(nullptr), remainingSize(0) 
    {
        dealocPtr.reserve(CHUNK_SIZE);
    }

    ~MemoryPool() 
    {
        while (head) 
        {
            MemoryChunk* next = head->nextChunk;
            delete head;
            head = next;
        }
        dealocPtr.clear();
    }

    void* allocate(size_t size, size_t alignment = alignof(T))
    {
        if (size > CHUNK_SIZE || alignment == 0 || (alignment & (alignment - 1)) != 0)
            throw std::bad_alloc();
        void* ptr = freePtr;
        size_t available = remainingSize;
        if (!ptr || !std::align(alignment, size, ptr, available))
        {
            MemoryChunk* newChunk = new MemoryChunk;
            newChunk->nextChunk = head;
            head = newChunk;
            freePtr = head->data;
            remainingSize = CHUNK_SIZE;
            ptr = freePtr;
            available = remainingSize;
            if (!std::align(alignment, size, ptr, available)) throw std::bad_alloc();
        }
        freePtr = static_cast<char*>(ptr) + size;
        remainingSize = available - size;
        return ptr;
    }

    template<typename... Args>
    T* newObject(Args&&... args) 
    {
        if (!dealocPtr.empty())
        {
            T* recycledObj = reinterpret_cast<T*>(dealocPtr.back());
            dealocPtr.pop_back();
            memset(recycledObj, 0, sizeof(T));
            new (recycledObj) T(std::forward<Args>(args)...);
            return recycledObj;
        }
        else
        {
            return new (allocate(sizeof(T))) T(std::forward<Args>(args)...);
        }
    }

    template<typename Derived, typename... Args>
    Derived* newDerivedObject(Args&&... args)
    {
        static_assert(std::is_base_of<T, Derived>::value, "Derived must inherit the pool's object type");
        // deleteObject only guarantees sizeof(T) bytes for a recycled slot.
        // Never construct a larger derived object over a smaller base slot.
        if constexpr (sizeof(Derived) <= sizeof(T))
        {
            for (auto it = dealocPtr.begin(); it != dealocPtr.end(); ++it)
            {
                if (reinterpret_cast<std::uintptr_t>(*it) % alignof(Derived) != 0) continue;
                Derived* recycledObj = reinterpret_cast<Derived*>(*it);
                dealocPtr.erase(it);
                memset(recycledObj, 0, sizeof(Derived));
                return new (recycledObj) Derived(std::forward<Args>(args)...);
            }
        }
        return new (allocate(sizeof(Derived), alignof(Derived))) Derived(std::forward<Args>(args)...);
    }

    
    void deleteObject(T* obj)
    {
        obj->~T();
        dealocPtr.push_back((char*)obj);
    }
private:
    static const int CHUNK_SIZE = 1024 * 1024;

    struct MemoryChunk
    {
        MemoryChunk* nextChunk;
        // The pointer preceding data previously shifted the first Materials
        // allocation by eight bytes, violating its alignas(16) UBO on 64-bit.
        alignas(std::max_align_t) char data[CHUNK_SIZE];
    };

    MemoryChunk* head;
    char* freePtr;
    size_t remainingSize;
    std::vector<char*> dealocPtr;
};


#endif //!__MEMORY_MANAGER__
