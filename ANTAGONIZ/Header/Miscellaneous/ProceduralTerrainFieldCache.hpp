#ifndef ENDURANCE_PROCEDURAL_TERRAIN_FIELD_CACHE_HPP
#define ENDURANCE_PROCEDURAL_TERRAIN_FIELD_CACHE_HPP

#include <array>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace ProceduralTerrain
{
    // The containing mesher owns one immutable set of field parameters/programs.
    // Transition masks deliberately do not identify a procedural field.
    struct FieldCacheKey
    {
        std::array<std::int32_t, 3> origin{};
        std::uint32_t lod=0, seed=0;
        bool operator==(const FieldCacheKey& other) const noexcept
        { return origin==other.origin && lod==other.lod && seed==other.seed; }
    };
    struct FieldCacheHash
    {
        std::size_t operator()(const FieldCacheKey& key) const noexcept
        {
            std::size_t hash=2166136261u;
            for (auto coordinate:key.origin)
                hash=(hash^static_cast<std::uint32_t>(coordinate))*16777619u;
            return ((hash^key.lod)*16777619u^key.seed)*16777619u;
        }
    };

    // CPU ownership of GPU slots. A referenced slot is never evicted, including
    // while generation or mesh emission is in flight. New entries can be shared
    // by a later job in the same ordered GPU batch; exactly one job builds them.
    class FieldSlotCache
    {
    public:
        struct Acquisition { std::uint32_t slot=UINT32_MAX; bool build=false; };
        void reset(std::uint32_t capacity)
        { m_entries.assign(capacity,{}); m_lookup.clear(); if (capacity) m_lookup.reserve(capacity); m_clock=0; }
        Acquisition acquire(const FieldCacheKey& key)
        {
            const auto found=m_lookup.find(key);
            if (found!=m_lookup.end())
            {
                auto& entry=m_entries[found->second];
                ++entry.references; entry.used=++m_clock;
                return {found->second,false};
            }
            std::uint32_t selected=UINT32_MAX;
            for (std::uint32_t i=0;i<m_entries.size();++i)
            {
                const auto& entry=m_entries[i];
                if (entry.references) continue;
                if (!entry.occupied) { selected=i; break; }
                if (selected==UINT32_MAX || entry.used<m_entries[selected].used) selected=i;
            }
            if (selected==UINT32_MAX) return {};
            auto& entry=m_entries[selected];
            if (entry.occupied) m_lookup.erase(entry.key);
            entry={key,1,++m_clock,true};
            m_lookup.emplace(key,selected);
            return {selected,true};
        }
        void release(std::uint32_t slot,bool retain=true) noexcept
        {
            if (slot>=m_entries.size() || !m_entries[slot].references) return;
            auto& entry=m_entries[slot];
            --entry.references; entry.used=++m_clock;
            if (!entry.references && !retain)
            { m_lookup.erase(entry.key); entry.occupied=false; }
        }
        std::uint32_t capacity() const noexcept { return static_cast<std::uint32_t>(m_entries.size()); }
        std::uint32_t references(std::uint32_t slot) const noexcept
        { return slot<m_entries.size() ? m_entries[slot].references : 0; }
    private:
        struct Entry
        {
            FieldCacheKey key;
            std::uint32_t references=0;
            std::uint64_t used=0;
            bool occupied=false;
        };
        std::vector<Entry> m_entries;
        std::unordered_map<FieldCacheKey,std::uint32_t,FieldCacheHash> m_lookup;
        std::uint64_t m_clock=0;
    };
}
#endif
