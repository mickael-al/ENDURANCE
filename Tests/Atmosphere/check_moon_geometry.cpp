// Native regression checks for the actual excavated solid, not a shaded sphere.
#include "ProceduralMoonGeometry.hpp"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <map>
#include <stdexcept>
#include <tuple>
#include <vector>

namespace {
using namespace EnduranceMoon;
void require(bool value,const char* message) { if(!value) throw std::runtime_error(message); }
bool finite(float value) {
    std::uint32_t bits; std::memcpy(&bits,&value,4);
    return (bits&0x7f800000u)!=0x7f800000u;
}
double length(MoonVec3 p) { return std::sqrt(double(p.x)*p.x+double(p.y)*p.y+double(p.z)*p.z); }
MoonVec3 sub(MoonVec3 a,MoonVec3 b) { return {a.x-b.x,a.y-b.y,a.z-b.z}; }
MoonVec3 cross(MoonVec3 a,MoonVec3 b) { return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x}; }
double dot(MoonVec3 a,MoonVec3 b) { return double(a.x)*b.x+double(a.y)*b.y+double(a.z)*b.z; }
double bodyVolume(const Geometry& mesh) {
    double sum=0;
    for(std::size_t i=0;i<mesh.parts.front().indexCount;i+=3) {
        const auto a=mesh.vertices[mesh.indices[i]].position;
        const auto b=mesh.vertices[mesh.indices[i+1]].position;
        const auto c=mesh.vertices[mesh.indices[i+2]].position;
        sum+=dot(a,cross(b,c))/6.;
    }
    return sum;
}
std::uint64_t fingerprint(const Geometry& mesh) {
    std::uint64_t hash=1469598103934665603ull;
    const auto bytes=[&](const void* ptr,std::size_t n) {
        const auto* p=static_cast<const unsigned char*>(ptr);
        for(std::size_t i=0;i<n;++i) { hash^=p[i]; hash*=1099511628211ull; }
    };
    for(const auto& v:mesh.vertices) {
        bytes(&v.position,sizeof(v.position)); bytes(&v.normal,sizeof(v.normal));
        bytes(&v.u,sizeof(v.u)); bytes(&v.v,sizeof(v.v)); bytes(v.color,sizeof(v.color));
    }
    for(auto index:mesh.indices) bytes(&index,sizeof(index));
    return hash;
}
void validate(const Geometry& mesh) {
    require(!mesh.vertices.empty() && !mesh.indices.empty() && !mesh.parts.empty(),"Geometry is empty");
    require(mesh.indices.size()%3==0,"Indices are not triangles");
    require(mesh.parts.front().firstIndex==0 && !mesh.parts.front().debris,"Main body is missing");
    std::size_t expected=0;
    for(const auto& part:mesh.parts) {
        require(part.firstIndex==expected && part.indexCount>0 && part.indexCount%3==0,"Part ranges do not cover the index buffer");
        expected+=part.indexCount;
    }
    require(expected==mesh.indices.size(),"Part ranges leave unused indices");
    for(const auto& v:mesh.vertices) {
        for(float x:{v.position.x,v.position.y,v.position.z,v.normal.x,v.normal.y,v.normal.z,v.u,v.v})
            require(finite(x),"Nonfinite mesh coordinate");
        for(float x:v.color) require(finite(x) && x>=0.f && x<=4.f,"Invalid linear material multiplier");
        require(std::abs(length(v.normal)-1.)<0.002,"Mesh normal is not unit length");
        require(v.v>=-0.0001f && v.v<=1.0001f,"Latitude UV outside range");
        require(v.position.x>=mesh.boundsMin.x-1e-5f && v.position.x<=mesh.boundsMax.x+1e-5f &&
                v.position.y>=mesh.boundsMin.y-1e-5f && v.position.y<=mesh.boundsMax.y+1e-5f &&
                v.position.z>=mesh.boundsMin.z-1e-5f && v.position.z<=mesh.boundsMax.z+1e-5f,"Vertex outside exported bounds");
    }
    std::size_t opposed=0,degenerate=0; double opposedArea=0,totalArea=0;
    for(std::size_t i=0;i<mesh.indices.size();i+=3) {
        for(unsigned j=0;j<3;++j) require(mesh.indices[i+j]<mesh.vertices.size(),"Index outside vertex buffer");
        const auto& a=mesh.vertices[mesh.indices[i]];
        const auto& b=mesh.vertices[mesh.indices[i+1]];
        const auto& c=mesh.vertices[mesh.indices[i+2]];
        const auto area=cross(sub(b.position,a.position),sub(c.position,a.position));
        if(length(area)==0) ++degenerate;
        const MoonVec3 normal{a.normal.x+b.normal.x+c.normal.x,a.normal.y+b.normal.y+c.normal.y,a.normal.z+b.normal.z+c.normal.z};
        totalArea+=length(area);
        if(dot(area,normal)<-1e-10) { ++opposed; opposedArea+=length(area); }
    }
    require(degenerate==0,"Geometry contains degenerate triangles");
    // Sharp CSG creases have explicit geometric-normal fallback vertices;
    // the exporter must not leave flipped smooth normals on those faces.
    if(opposed) std::cerr<<"Opposed triangles "<<opposed<<", area ratio "<<opposedArea/totalArea<<"\n";
    require(opposed==0,"Triangles oppose their shading normals");
}
void checkClosed(const Geometry& mesh) {
    // The longitude seam legitimately duplicates vertices. Merge identical
    // positions before counting half-edges; do not mistake UV splits for holes.
    using Key=std::tuple<float,float,float>;
    std::map<Key,std::uint32_t> weld;
    std::vector<std::uint32_t> ids(mesh.vertices.size());
    for(std::size_t i=0;i<mesh.vertices.size();++i) {
        const auto p=mesh.vertices[i].position;
        const Key key{p.x,p.y,p.z};
        auto result=weld.emplace(key,static_cast<std::uint32_t>(weld.size())); ids[i]=result.first->second;
    }
    std::map<std::pair<std::uint32_t,std::uint32_t>,unsigned> edges;
    std::map<std::pair<std::uint32_t,std::uint32_t>,int> orientation;
    for(std::size_t i=0;i<mesh.indices.size();i+=3) {
        for(unsigned k=0;k<3;++k) {
            auto a=ids[mesh.indices[i+k]],b=ids[mesh.indices[i+(k+1)%3]];
            if(a==b) continue;
            const int winding=a<b?1:-1;
            if(a>b) std::swap(a,b);
            ++edges[{a,b}]; orientation[{a,b}]+=winding;
        }
    }
    std::size_t open=0,nonmanifold=0;
    for(const auto& e:edges) { if(e.second==1) ++open; if(e.second>2) ++nonmanifold; }
    if(open||nonmanifold) std::cerr<<"Open edges "<<open<<", nonmanifold "<<nonmanifold<<" / "<<edges.size()<<"\n";
    require(open==0,"Mesh has open boundary edges after position welding");
    require(nonmanifold==0,"Mesh has nonmanifold edges after position welding");
    std::size_t inconsistent=0; for(const auto& e:orientation) if(e.second!=0) ++inconsistent;
    if(inconsistent) std::cerr<<"Inconsistent edge count: "<<inconsistent<<" / "<<edges.size()<<"\n";
    require(inconsistent==0,"Adjacent triangles have inconsistent directed edge winding");
}
}

int main() {
    try {
        GeometryParams p; p.resolution=32; p.debrisCount=5;
        const auto first=generateGeometry(p); validate(first); checkClosed(first);
        require(first.parts.size()==6,"Requested detached fragments were not generated");
        const auto same=generateGeometry(p);
        require(fingerprint(first)==fingerprint(same),"Identical seeds differ");
        ++p.seed; const auto changed=generateGeometry(p); validate(changed);
        require(fingerprint(first)!=fingerprint(changed),"Seed does not change geometry");
        --p.seed; p.debrisCount=0; p.excavationStrength=0;
        const auto intact=generateGeometry(p); validate(intact); checkClosed(intact);
        const double intactVolume=bodyVolume(intact),excavatedVolume=bodyVolume(first);
        require(intactVolume>3.0 && intactVolume<5.5,"Intact normalized body has invalid volume or winding");
        require(excavatedVolume>intactVolume*.15 && excavatedVolume<intactVolume*.94,"Default excavation does not remove a substantial real volume");
        double minimumRadius=10;
        for(std::size_t i=0;i<first.parts.front().indexCount;++i)
            minimumRadius=(std::min)(minimumRadius,length(first.vertices[first.indices[i]].position));
        require(minimumRadius<.8,"Excavation does not expose deep interior surfaces");
        std::atomic_bool cancel{true};
        require(generateGeometry(p,&cancel).vertices.empty(),"Pre-cancelled generation returned geometry");
        GeometryParams bad; bad.resolution=2000000; bad.cavityCount=-1; bad.debrisCount=2000000;
        bad.erosionStrength=std::numeric_limits<float>::quiet_NaN();
        bad.cavitySize=std::numeric_limits<float>::infinity();
        const auto safe=sanitizeGeometry(bad);
        require(safe.resolution<=256 && safe.resolution>=32 && safe.cavityCount>=0 && safe.debrisCount<=256 &&
                finite(safe.erosionStrength) && finite(safe.cavitySize),"Invalid geometry settings were not bounded");
        for(int resolution:{96,128}) {
            GeometryParams detailed; detailed.resolution=resolution; detailed.debrisCount=5;
            const auto solid=generateGeometry(detailed); validate(solid); checkClosed(solid);
            std::cout<<"PASS detailed geometry "<<resolution<<": "<<solid.vertices.size()<<" vertices, "<<solid.indices.size()/3<<" triangles\n";
        }
        for(std::uint32_t seed:{68337u,68383u}) {
            GeometryParams extreme; extreme.resolution=48; extreme.seed=seed; extreme.debrisCount=0;
            extreme.erosionStrength=.18f; extreme.erosionScale=16; extreme.cavitySize=1.1f;
            extreme.cavityDepth=1.2f; extreme.excavationStrength=1.6f;
            const auto solid=generateGeometry(extreme); checkClosed(solid);
        }
        GeometryParams side; side.resolution=96; side.excavationYaw=60;
        const auto sideSolid=generateGeometry(side); validate(sideSolid); checkClosed(sideSolid);
        std::cout<<"PASS native geometry: "<<first.vertices.size()<<" vertices, "<<first.indices.size()/3
                 <<" triangles, intact volume "<<intactVolume<<", excavated "<<excavatedVolume
                 <<", deepest radius "<<minimumRadius<<"\n";
        return 0;
    } catch(const std::exception& e) { std::cerr<<"FAIL native Moon geometry: "<<e.what()<<'\n'; return 1; }
}
