#include "ProceduralTerrainFieldCache.hpp"
#include <iostream>
#include <stdexcept>

using namespace ProceduralTerrain;
namespace
{
void require(bool value,const char* message)
{ if(!value) throw std::runtime_error(message); }
FieldCacheKey key(int x,int y=0,int z=0,std::uint32_t lod=0,std::uint32_t seed=1337)
{ return {{x,y,z},lod,seed}; }
}

int main()
{
    try
    {
        FieldSlotCache cache;
        cache.reset(2);
        auto a=cache.acquire(key(-8)), alias=cache.acquire(key(-8));
        auto b=cache.acquire(key(8));
        require(a.build && !alias.build && alias.slot==a.slot,"Identical key did not share its pending slot");
        require(cache.references(a.slot)==2,"Alias did not pin its owner");
        require(cache.acquire(key(16)).slot==UINT32_MAX,"Cache evicted a referenced field");
        cache.release(a.slot);
        require(cache.references(a.slot)==1,"Owner release unpinned a live alias");
        cache.release(b.slot);
        auto c=cache.acquire(key(16));
        require(c.build && c.slot==b.slot,"Cache did not evict the only unpinned slot");
        cache.release(alias.slot,false);
        auto rebuilt=cache.acquire(key(-8));
        require(rebuilt.build && rebuilt.slot==a.slot,"Unretained field survived its final release");
        cache.release(rebuilt.slot);cache.release(c.slot);

        cache.reset(3);
        a=cache.acquire(key(0));cache.release(a.slot);
        b=cache.acquire(key(8));cache.release(b.slot);
        c=cache.acquire(key(16));cache.release(c.slot);
        auto touch=cache.acquire(key(0));
        require(!touch.build,"Retained field was lost before eviction");
        cache.release(touch.slot);
        auto d=cache.acquire(key(24));
        require(d.build && d.slot==b.slot,"Eviction was not least recently used");
        require(!cache.acquire(key(16)).build,"LRU eviction removed a newer entry");

        cache.reset(8);
        auto base=cache.acquire(key(-16,-8,24,0,7));
        for(const auto& changed:{key(-8,-8,24,0,7),key(-16,0,24,0,7),key(-16,-8,32,0,7),
                                 key(-16,-8,24,1,7),key(-16,-8,24,0,8)})
        {
            const auto item=cache.acquire(changed);
            require(item.build && item.slot!=base.slot,"Coordinates, LOD or seed missing from field identity");
        }
        require(!cache.acquire(key(-16,-8,24,0,7)).build,"Full key lookup failed");

        cache.reset(1);
        a=cache.acquire(key(0));alias=cache.acquire(key(0));
        cache.release(a.slot,false);
        require(cache.references(alias.slot)==1 && cache.acquire(key(8)).slot==UINT32_MAX,
            "Disabled retention invalidated an in-flight alias");
        cache.release(alias.slot,false);
        rebuilt=cache.acquire(key(0));
        require(rebuilt.build,"Retention disabled did not rebuild on revisit");
        cache.reset(0);
        require(cache.capacity()==0 && cache.acquire(key(0)).slot==UINT32_MAX,"Empty cache accepted an allocation");
        cache.release(UINT32_MAX,false);
        std::cout<<"PASS field cache: pending aliases, pinned eviction, LRU, exact keys, disabled retention and reset\n";
    }
    catch(const std::exception& error)
    { std::cerr<<"FAIL field cache: "<<error.what()<<'\n';return 1; }
}
