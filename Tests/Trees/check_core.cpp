#include "ProceduralTree.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <set>
#include <stdexcept>

using namespace EnduranceTrees;
namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
bool finite(float value) { std::uint32_t bits; std::memcpy(&bits,&value,4); return (bits&0x7f800000u)!=0x7f800000u; }
template<int N> void finiteVector(const glm::vec<N,float,glm::defaultp>& v) {
    for(int i=0;i<N;++i) require(finite(v[i]),"Non-finite component in generated asset");
}
std::uint64_t hashWord(std::uint64_t h,std::uint32_t word) { return (h^word)*1099511628211ull; }
std::uint64_t hashFloat(std::uint64_t h,float value) { std::uint32_t word; std::memcpy(&word,&value,4); return hashWord(h,word); }
template<int N> std::uint64_t hashVector(std::uint64_t h,const glm::vec<N,float,glm::defaultp>& v) {
    for(int i=0;i<N;++i) h=hashFloat(h,v[i]); return h;
}
std::uint64_t meshHash(const Mesh& m) {
    auto h=1469598103934665603ull;
    for(const auto& v:m.vertices) {
        h=hashVector(h,v.position); h=hashVector(h,v.normal); h=hashVector(h,v.uv);
        h=hashVector(h,v.color); h=hashVector(h,v.wind);
    }
    for(auto i:m.indices) h=hashWord(h,i);
    return h;
}
void checkMesh(const Mesh& m,const Tree& tree) {
    require(m.indices.size()%3==0,"Index count is not triangular");
    require(m.vertices.empty()==m.indices.empty(),"Unreferenced or missing mesh vertices");
    for(const auto& v:m.vertices) {
        finiteVector(v.position); finiteVector(v.normal); finiteVector(v.uv); finiteVector(v.color); finiteVector(v.wind);
        require(std::abs(glm::length(v.normal)-1.f)<0.003f,"Generated normal is not normalized");
        for(int k=0;k<3;++k) {
            require(v.position[k]>=tree.boundsMin[k]-0.001f && v.position[k]<=tree.boundsMax[k]+0.001f,"Vertex outside published tree bounds");
            require(v.color[k]>=0.f && v.color[k]<=1.f,"Palette color outside linear unit range");
        }
    }
    for(auto i:m.indices) require(i<m.vertices.size(),"Mesh index outside vertex buffer");
    for(std::size_t i=0;i<m.indices.size();i+=3) {
        const auto a=m.indices[i],b=m.indices[i+1],c=m.indices[i+2];
        require(a!=b && a!=c && b!=c,"Repeated index in triangle");
        const auto e1=m.vertices[b].position-m.vertices[a].position;
        const auto e2=m.vertices[c].position-m.vertices[a].position;
        require(glm::dot(glm::cross(e1,e2),glm::cross(e1,e2))>1e-16f,"Zero-area triangle");
    }
}
void checkTree(const Tree& t) {
    require(!t.cancelled && !t.branches.empty(),"Generation did not return a tree");
    finiteVector(t.boundsMin); finiteVector(t.boundsMax);
    for(int k=0;k<3;++k) require(t.boundsMax[k]>t.boundsMin[k],"Invalid bounds extent");
    require(t.statistics.branches==t.branches.size(),"Incorrect reported branch count");
    for(std::size_t i=0;i<t.branches.size();++i) {
        const auto& b=t.branches[i];
        require(b.parent>=-1 && b.parent<static_cast<std::int32_t>(i),"Branch hierarchy is cyclic or invalid");
        require(b.points.size()>=2,"Branch has no length");
        require(b.level>=0 && b.attachment>=0.f && b.attachment<=1.f,"Invalid branch metadata");
        for(const auto& point:b.points) {
            finiteVector(point.position);
            require(finite(point.radius) && point.radius>0.f,"Invalid branch radius");
        }
    }
    for(std::size_t i=0;i<t.lods.size();++i) {
        checkMesh(t.lods[i].wood,t); checkMesh(t.lods[i].foliage,t);
        require(!t.lods[i].wood.indices.empty(),"LOD lost the trunk");
        require(t.statistics.triangles[i]==(t.lods[i].wood.indices.size()+t.lods[i].foliage.indices.size())/3,"Incorrect reported triangle count");
    }
    require(t.statistics.triangles[1]<t.statistics.triangles[0],"Distant LOD did not reduce geometry");
    require(validImage(t.leafMask) && validImage(t.barkTexture),"Invalid generated texture");
}
float coverage(const Image& image) {
    std::size_t passed=0;
    for(std::size_t i=3;i<image.rgba.size();i+=4) passed+=image.rgba[i]>=128;
    return static_cast<float>(passed)/(image.width*image.height);
}
void checkMips(const Image& source) {
    auto mips=coverageMipmaps(source);
    require(!mips.empty() && mips.front().rgba==source.rgba,"Mipmap generation changed the source");
    require(mips.front().width==source.width && mips.front().height==source.height,"Mipmap source extent changed");
    const float original=coverage(source);
    for(std::size_t i=0;i<mips.size();++i) {
        const auto& level=mips[i]; require(validImage(level),"Invalid mip extent or payload");
        if(i) {
            require(level.width==std::max(1,mips[i-1].width/2),"Incorrect mip width");
            require(level.height==std::max(1,mips[i-1].height/2),"Incorrect mip height");
        }
        const auto quantization=1.f/(level.width*level.height);
        require(std::abs(coverage(level)-original)<=0.13f+quantization,"Mip loses excessive alpha-test coverage");
    }
    require(mips.back().width==1 && mips.back().height==1,"Incomplete mip chain");
}
}

int main() {
 try {
    require(!validImage({}),"Empty image accepted");
    Image invalid; invalid.width=-1; invalid.height=10; invalid.rgba.resize(40);
    require(!validImage(invalid),"Negative image extent accepted");
    invalid.width=(std::numeric_limits<int>::max)(); invalid.height=(std::numeric_limits<int>::max)();
    require(!validImage(invalid),"Overflowing image extent accepted");
    Image mask=makeLeafMask(); require(validImage(mask),"Default mask invalid"); checkMips(mask);
    Image wide; wide.width=48; wide.height=16; wide.rgba.assign(48*16*4,255);
    Image tall; tall.width=16; tall.height=48; tall.rgba=wide.rgba;
    Image empty=wide; for(std::size_t i=3;i<empty.rgba.size();i+=4) empty.rgba[i]=0;
    checkMips(wide); checkMips(tall); checkMips(empty);
    std::set<std::uint64_t> silhouettes;
    for(auto family:{Family::Broadleaf,Family::Conifer,Family::Weeping,Family::Dead}) {
        auto p=preset(family); p.branchCount=6; p.twigDensity=0.6f; p.leafDensity=0.6f;
        for(std::uint32_t seed:{1u,1337u,0xffffffffu}) {
            p.seed=seed;
            const auto a=generate(p),b=generate(p); checkTree(a); checkTree(b);
            for(int i=0;i<2;++i) {
                require(meshHash(a.lods[i].wood)==meshHash(b.lods[i].wood),"Wood is not deterministic for identical seed");
                require(meshHash(a.lods[i].foliage)==meshHash(b.lods[i].foliage),"Foliage is not deterministic for identical seed");
            }
            silhouettes.insert(meshHash(a.lods[0].wood));
            if(family==Family::Dead) require(a.lods[0].foliage.indices.empty(),"Dead preset has leaves");
            else require(!a.lods[0].foliage.indices.empty(),"Living preset has no foliage");
            std::cout<<familyName(family)<<" seed="<<seed<<" branches="<<a.statistics.branches<<" triangles="<<a.statistics.triangles[0]<<"/"<<a.statistics.triangles[1]<<'\n';
        }
    }
    require(silhouettes.size()==12,"Seeds or family presets fail to vary wood geometry");
    auto p=preset(Family::Broadleaf); p.branchCount=4; p.branchingLevels=2;
    const auto a=generate(p,&wide),b=generate(p,&tall),c=generate(p,&empty);
    checkTree(a); checkTree(b); checkTree(c);
    require(meshHash(a.lods[0].wood)==meshHash(b.lods[0].wood),"Leaf mask unexpectedly changes trunk");
    require(meshHash(a.lods[0].foliage)!=meshHash(b.lods[0].foliage),"Leaf mask aspect ratio ignored");
    require(a.leafMask.width==wide.width && b.leafMask.height==tall.height,"Imported mask dimensions lost");
    std::atomic_bool cancel{true}; require(generate(p,nullptr,&cancel).cancelled,"Cancellation ignored");
    p.height=(std::numeric_limits<float>::quiet_NaN)(); p.trunkRadius=-(std::numeric_limits<float>::infinity)();
    p.crownWidth=(std::numeric_limits<float>::infinity)(); p.barkLow.x=(std::numeric_limits<float>::quiet_NaN)();
    p.branchCount=-100; p.branchingLevels=-99; p.family=static_cast<Family>(999);
    checkTree(generate(p));
    std::cout<<"Tree CPU invariants, deterministic seeds, mask aspect, mip coverage and invalid-input handling: PASS\n";
    return 0;
 } catch(const std::exception& e) { std::cerr<<"Tree test failure: "<<e.what()<<'\n'; return 1; }
}
