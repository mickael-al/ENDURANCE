#include "glcore.hpp"
#include "PointeurClass.hpp"
#include "EnduranceSea.hpp"
#include "ShaderCompilation.hpp"
#include <iostream>
#include <limits>

namespace Ge {
unsigned buildShaderProgram(const std::vector<ShaderFile>& stages, std::string& error,
    const std::string&,const ShaderCacheOptions& cache)
{
    assert(stages.size()==2 && cache.enabled);
    if(SeaFixture::failCompile) { error="Fixture compile failure"; return 0; }
    const auto id=SeaFixture::next++; SeaFixture::programs.insert(id); return id;
}
}

int main()
{
    using namespace SeaFixture;
    Ge::RenderingEngine renderer; ptrClass engine{&renderer};
    Endurance::SeaRenderer sea;
    assert(!sea.initialize(ptrClass{}));
    assert(sea.initialize(engine));
    assert(buffers.empty() && vaos.size()==1 && programs.size()==1);
    std::vector<ProceduralTerrain::SeaTile> tiles{{0,0,0,0},{1,0,0,0}};
    error=GL_INVALID_OPERATION; // An unrelated previous pass must not hide sea.
    assert(sea.setTiles(tiles,1,0.0f,64.0f));
    assert(allocations==1 && uploads==1 && sea.stats().tiles==2 && sea.stats().uploadBytes==32);
    const auto firstCapacity=sea.stats().allocatedBytes;
    assert(firstCapacity>=32 && buffers.size()==1);
    const auto uniformsBefore=uniforms;
    assert(sea.setTiles(tiles,1,0.0f,64.0f));
    assert(allocations==1 && uploads==1 && uniforms==uniformsBefore);

    // Exercise range binding as well as the independent generic SSBO binding.
    GLuint callerBuffer=0; glCreateBuffers(1,&callerBuffer);
    glNamedBufferData(callerBuffer,1024,nullptr,GL_DYNAMIC_DRAW);
    glBindBufferRange(GL_SHADER_STORAGE_BUFFER,8,callerBuffer,256,512);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER,0);
    program=53; vao=67; active=GL_TEXTURE0+3;
    textures2D[4]=71; texturesArray[4]=79; culling=true; depthWrite=true;
    bool trackedDepth=true;
    sea.draw(&trackedDepth);
    assert(draws==1 && lastInstances==2 && atmosphereCalls==1);
    assert(program==53 && vao==67 && active==GL_TEXTURE0+3);
    assert(textures2D[4]==71 && texturesArray[4]==79 && culling && depthWrite && trackedDepth);
    assert(indexed[8].buffer==callerBuffer && indexed[8].offset==256 && indexed[8].size==512 && genericStorage==0);
    // Null depth-state pointer, empty binding, no atmosphere and culling off.
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER,8,0); genericStorage=callerBuffer;
    renderer.atmosphereEnabled=false; culling=false; depthWrite=false; textures2D[4]=0;
    sea.draw(nullptr);
    assert(draws==2 && !culling && !depthWrite && textures2D[4]==0 && texturesArray[4]==79);
    assert(indexed[8].buffer==0 && genericStorage==callerBuffer && atmosphereCalls==1);

    const auto allocationCount=allocations;
    tiles.resize(4);
    assert(sea.setTiles(tiles,2,0.0f,64.0f));
    assert(uploads==2 && allocations==allocationCount);
    tiles.resize(static_cast<std::size_t>(firstCapacity/16)+1);
    assert(sea.setTiles(tiles,3,0.0f,64.0f));
    assert(uploads==3 && allocations==allocationCount+1 && buffers.size()==2);
    assert(sea.stats().allocatedBytes>firstCapacity);
    assert(sea.setTiles({},4,0.0f,64.0f));
    sea.draw(nullptr);
    assert(draws==2 && sea.stats().tiles==0 && uploads==3);

    tiles.resize(static_cast<std::size_t>(sea.stats().allocatedBytes/16)+1);
    failAllocation=true;
    assert(!sea.setTiles(tiles,5,0.0f,64.0f));
    const auto failedAllocationCount=allocations;
    assert(!sea.error().empty() && sea.stats().tiles==0 && buffers.size()==2);
    sea.draw(nullptr);
    assert(draws==2 && !sea.setTiles(tiles,5,0.0f,64.0f) && allocations==failedAllocationCount);
    assert(sea.setTiles(tiles,6,0.0f,64.0f) && sea.error().empty());
    failUpload=true;
    assert(!sea.setTiles(tiles,7,0.0f,64.0f) && sea.stats().tiles==0);
    const auto failedUploadCount=uploads;
    assert(!sea.setTiles(tiles,7,0.0f,64.0f) && uploads==failedUploadCount);
    sea.draw(nullptr); assert(draws==2);
    assert(sea.setTiles(tiles,8,0.0f,64.0f));
    assert(!sea.setTiles(tiles,9,(std::numeric_limits<float>::infinity)(),64.0f));
    assert(sea.stats().tiles==0 && !sea.error().empty());
    assert(!sea.setTiles(tiles,10,0.0f,0.0f));
    tiles[0].lod=21;
    assert(!sea.setTiles(tiles,11,0.0f,64.0f));
    sea.shutdown(); sea.shutdown();
    assert(vaos.empty() && programs.empty() && buffers.size()==1 && buffers.count(callerBuffer));
    failCompile=true;
    assert(!sea.initialize(engine) && !sea.error().empty());
    assert(vaos.empty() && programs.empty());
    glDeleteBuffers(1,&callerBuffer);
    std::cout << "sea runtime: publication, amortized uploads, draw state, failures, lifecycle PASS\n";
}
