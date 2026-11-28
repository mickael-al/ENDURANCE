#include "MemoryManager.hpp"
#include <cassert>
#include <cstdint>
#include <iostream>
#include <vector>

struct alignas(16) MaterialLike
{
    float values[16]{};
    explicit MaterialLike(float value = 1.0f)
    { for (float& component : values) component = value; }
};

struct Base
{
    static int destroyed;
    virtual ~Base() { ++destroyed; }
    std::uint64_t sentinel = 0xfedcba9876543210ull;
};
int Base::destroyed = 0;

struct alignas(64) Derived : Base
{
    unsigned char payload[512]{};
    Derived() { for (auto& byte : payload) byte = 0x7f; }
};

int main()
{
    MemoryPool<MaterialLike> materials;
    // Odd-sized raw allocations must not misalign the next typed allocation.
    materials.allocate(1, 1);
    auto* first = materials.newObject(3.0f);
    assert(reinterpret_cast<std::uintptr_t>(first) % alignof(MaterialLike) == 0);
    for (float value : first->values) assert(value == 3.0f);
    materials.deleteObject(first);
    auto* recycled = materials.newObject(7.0f);
    assert(first == recycled);
    for (float value : recycled->values) assert(value == 7.0f);
    materials.deleteObject(recycled);

    // Exercise multiple chunks and the same 16-byte alignment as real materials.
    std::vector<MaterialLike*> live;
    for (int i = 0; i < 40000; ++i)
    {
        auto* value = materials.newObject(static_cast<float>(i));
        assert(reinterpret_cast<std::uintptr_t>(value) % alignof(MaterialLike) == 0);
        live.push_back(value);
    }
    for (int i = 0; i < 40000; ++i)
    {
        assert(live[i]->values[15] == static_cast<float>(i));
        materials.deleteObject(live[i]);
    }

    MemoryPool<Base> polymorphic;
    auto* small = polymorphic.newObject();
    auto* neighbour = polymorphic.newObject();
    polymorphic.deleteObject(small);
    auto* large = polymorphic.newDerivedObject<Derived>();
    assert(reinterpret_cast<std::uintptr_t>(large) % alignof(Derived) == 0);
    assert(neighbour->sentinel == 0xfedcba9876543210ull);
    for (auto byte : large->payload) assert(byte == 0x7f);
    polymorphic.deleteObject(large);
    polymorphic.deleteObject(neighbour);
    assert(Base::destroyed == 3);

    bool rejected = false;
    try { materials.allocate(2 * 1024 * 1024); }
    catch (const std::bad_alloc&) { rejected = true; }
    assert(rejected);
    std::cout << "Memory pool alignment, chunks, recycling and derived slots passed\n";
}
