#include "glcore.hpp"
#include "ShapeBufferBase.hpp"
#include "ShadowMapCache.hpp"
#include <cassert>
#include <iostream>

int main()
{
    std::vector<Vertex> vertices(3);
    std::vector<std::uint32_t> indices{0,1,2};
    Ge::ShapeBufferBase mesh(vertices,indices,nullptr);
    mesh.SetupVAO(1);
    const auto firstName=mesh.getVAO();
    const auto firstRevision=mesh.shadowContentRevision();
    assert(FixtureVAO::positionLocation==0);
    Ge::ShadowMapCache cache;
    const std::vector<glm::mat4> matrices(4,glm::mat4(1));
    const auto render=[&]()
    {
        cache.prepare(matrices,{{&mesh,mesh.shadowContentRevision()}},false);
        unsigned draws=0;
        for(unsigned i=0;i<4;++i) if(cache.needsRender(i)) { cache.rendered(i); ++draws; }
        cache.finish(); return draws;
    };
    assert(render()==4 && render()==0);
    // Simulate another model sharing this mesh assigning a material with a
    // different attribute layout. The GL double deliberately reuses VAO IDs.
    mesh.SetupVAO(2);
    assert(mesh.getVAO()==firstName && FixtureVAO::positionLocation==3);
    assert(mesh.shadowContentRevision()!=firstRevision);
    assert(render()==4 && render()==0);
    std::cout<<"PASS production ShapeBufferBase: rebuilt shared VAO with recycled name and changed layout invalidates cached shadow maps\n";
}
