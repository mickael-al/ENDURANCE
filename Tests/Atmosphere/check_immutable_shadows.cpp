#include "ImmutableShadowCache.hpp"
#include "ShadowMapCache.hpp"
#include <glm/vec4.hpp>
#include <cassert>
#include <iostream>
#include <limits>
#include <unordered_map>

using Ge::ShadowContentToken;
struct Resource
{
    ShadowContentToken token;
    std::uint64_t shadowContentRevision() const { return token.value(); }
};
struct Pipeline : Resource { unsigned program=1; unsigned getProgram() const { return program; } };
struct Mesh : Resource
{
    bool immutable=true;
    unsigned vao=1,vbo=2,ibo=3,indices=36;
    bool supportsImmutableShadowContent() const { return immutable; }
    unsigned getVAO() const { return vao; } unsigned getVBO() const { return vbo; }
    unsigned getIBO() const { return ibo; } unsigned getIndiceSize() const { return indices; }
};
struct Texture : Resource
{
    bool immutable=true; unsigned id=1;
    bool supportsImmutableShadowContent() const { return immutable; }
    unsigned getTextureID() const { return id; }
};
struct Material : Resource
{
    bool draw=true,casts=true,depth=true;
    glm::vec4 color{1}; glm::vec2 offset{0},tiling{1};
    Pipeline* pipeline; Texture* texture;
    std::vector<unsigned> extras; unsigned instances=0;
    bool getDraw() const { return draw; } bool getCastShadow() const { return casts; }
    bool getDepthTest() const { return depth; } glm::vec4 getColor() const { return color; }
    glm::vec2 getOffset() const { return offset; } glm::vec2 getTilling() const { return tiling; }
    Pipeline* getShadowPipeline() const { return pipeline; }
    Texture* getAlbedoTexture() const { return texture; }
    const std::vector<unsigned>& getAditionalSSBO() const { return extras; }
    unsigned getAditionalInstanced() const { return instances; }
};
struct Ubo { glm::mat4 model{1}; int mat_index=0; };
struct Model : Resource
{
    bool draw=true,immutable=false; unsigned index=0;
    Mesh* mesh; Material* material; Ubo ubo;
    bool getDraw() const { return draw; } bool getImmutableShadowContent() const { return immutable; }
    Mesh* getShapeBuffer() const { return mesh; } Material* getMaterial() const { return material; }
    Ubo getUBO() const { return ubo; } unsigned getIndex() const { return index; }
};

int main()
{
    Pipeline shader; Mesh mesh; Texture alpha;
    Material material; material.pipeline=&shader; material.texture=&alpha;
    Model cube; cube.mesh=&mesh; cube.material=&material;
    std::vector<Material*> materials{&material};
    std::unordered_map<Material*,std::unordered_map<Mesh*,std::vector<Model*>>> groups;
    groups[&material][&mesh]={&cube};
    Ge::ImmutableShadowCache cache;
    Ge::ShadowMapCache maps;
    std::vector<glm::mat4> matrices(4,glm::mat4(1));
    const auto revision=[&](bool callback=false) { return cache.update(materials,groups,&shader,callback); };
    const auto frame=[&]()
    {
        auto token=revision();
        maps.prepare(matrices,{{&cache,token}},token==0);
        unsigned draws=0;
        for (unsigned i=0;i<4;++i) if(maps.needsRender(i)) { ++draws; maps.rendered(i); }
        maps.finish(); return draws;
    };
    assert(revision()==0); // Legacy/default scenes remain conservative.
    assert(frame()==4 && frame()==4);
    cube.immutable=true;
    assert(frame()==4);
    const auto settled=maps.contentRevision();
    for(unsigned i=0;i<64;++i) assert(frame()==0 && maps.contentRevision()==settled);
    const auto changed=[&](auto edit)
    {
        const auto before=revision(); edit();
        assert(revision()!=before && revision()!=0);
        assert(frame()==4 && frame()==0);
    };
    changed([&]{cube.ubo.model[3].x+=2;}); // Includes inherited/parent transforms.
    changed([&]{cube.ubo.model[0].x=2;});
    changed([&]{cube.index=3;});
    changed([&]{cube.ubo.mat_index=2;});
    changed([&]{material.offset.x=.4f;});
    changed([&]{material.tiling.y=2;});
    changed([&]{material.color.a=.7f;});
    changed([&]{material.token.invalidate();});
    changed([&]{mesh.token.invalidate();}); // External write, same handles.
    changed([&]{alpha.token.invalidate();});
    changed([&]{cube.token.invalidate();});
    changed([&]{shader.token.invalidate();});
    changed([&]{shader.program=8;});
    changed([&]{mesh.vbo=6;});
    changed([&]{mesh.ibo=7;});
    changed([&]{mesh.vao=9;});
    changed([&]{mesh.indices=72;});
    changed([&]{alpha.id=15;});
    changed([&]{material.draw=false;}); changed([&]{material.draw=true;});
    changed([&]{material.casts=false;}); changed([&]{material.casts=true;});
    changed([&]{material.depth=false;}); changed([&]{material.depth=true;});
    changed([&]{material.color.a=0;}); changed([&]{material.color.a=1;});
    changed([&]{cube.draw=false;}); changed([&]{cube.draw=true;});

    // Unknown callbacks, vertex animation, procedural instance inputs and
    // externally maintained render-target alpha must never acquire a token.
    assert(revision(true)==0);
    cube.immutable=false; assert(revision()==0); cube.immutable=true;
    mesh.immutable=false; assert(revision()==0); mesh.immutable=true;
    alpha.immutable=false; assert(revision()==0); alpha.immutable=true;
    material.extras={2}; assert(revision()==0); material.extras.clear();
    material.instances=1; assert(revision()==0); material.instances=0;
    Pipeline custom; material.pipeline=&custom; assert(revision()==0);
    material.pipeline=nullptr; assert(revision()==0); material.pipeline=&shader;
    material.texture=nullptr; assert(revision()==0); material.texture=&alpha;
    cube.ubo.model[0].x=(std::numeric_limits<float>::quiet_NaN)(); assert(revision()==0);
    cube.ubo.model[0].x=1;
    material.offset.x=(std::numeric_limits<float>::infinity)(); assert(revision()==0);
    material.offset.x=0;

    // Same memory address and GL names after resource replacement are different
    // lifetimes. No hash collision or buffer readback can hide the publication.
    changed([&]{mesh.~Mesh(); new(&mesh) Mesh();});
    changed([&]{alpha.~Texture(); new(&alpha) Texture();});
    const auto emptyBefore=revision(); groups.clear(); assert(revision()!=emptyBefore);
    assert(frame()==4 && frame()==0);
    std::cout<<"PASS immutable caster cache: 64 idle frames reuse all four layers; transforms, material/alpha/mesh edits, flags, lifetimes, removals invalidate; unknown/animated inputs stay uncached\n";
}
