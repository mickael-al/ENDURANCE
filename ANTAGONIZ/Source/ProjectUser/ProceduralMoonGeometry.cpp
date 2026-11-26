#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "ProceduralMoonGeometry.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <queue>
#include <iterator>
#include <unordered_map>

namespace EnduranceMoon
{
namespace
{
using V=MoonVec3;
constexpr float Pi=3.14159265358979323846f;
V operator+(V a,V b) { return {a.x+b.x,a.y+b.y,a.z+b.z}; }
V operator-(V a,V b) { return {a.x-b.x,a.y-b.y,a.z-b.z}; }
V operator*(V a,float b) { return {a.x*b,a.y*b,a.z*b}; }
float dot(V a,V b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
V cross(V a,V b) { return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x}; }
float length(V a) { return std::sqrt(dot(a,a)); }
V unit(V a) { return a*(1.0f/std::sqrt((std::max)(dot(a,a),1.e-30f))); }
float smooth(float a,float b,float v)
{ const float t=std::clamp((v-a)/(b-a),0.f,1.f); return t*t*(3.f-2.f*t); }
bool finite(float f)
{ std::uint32_t bits; std::memcpy(&bits,&f,4); return (bits&0x7f800000u)!=0x7f800000u; }
float safe(float v,float fallback,float lo,float hi)
{ return std::clamp(finite(v)?v:fallback,lo,hi); }
bool cancelled(const std::atomic_bool* c) { return c && c->load(std::memory_order_relaxed); }
struct Random
{
    std::uint32_t state;
    std::uint32_t next() { state+=0x9e3779b9u; auto v=state; v=(v^(v>>16))*0x85ebca6bu; v=(v^(v>>13))*0xc2b2ae35u; return v^(v>>16); }
    float value() { return float(next()>>8)*(1.f/16777216.f); }
    float range(float a,float b) { return a+(b-a)*value(); }
    V sphere() { const float y=range(-1,1),a=range(-Pi,Pi),r=std::sqrt((std::max)(0.f,1-y*y)); return {r*std::cos(a),y,r*std::sin(a)}; }
};
struct Noise
{
    std::array<int,512> perm{};
    explicit Noise(std::uint32_t seed)
    {
        Random r{seed^0x5343554cu};
        for(int i=0;i<256;++i) perm[i]=i;
        for(int i=255;i>0;--i) std::swap(perm[i],perm[r.next()%unsigned(i+1)]);
        for(int i=0;i<256;++i) perm[i+256]=perm[i];
    }
    float sample(V p) const
    {
        static constexpr V g[12]={{1,1,0},{-1,1,0},{1,-1,0},{-1,-1,0},
            {1,0,1},{-1,0,1},{1,0,-1},{-1,0,-1},{0,1,1},{0,-1,1},{0,1,-1},{0,-1,-1}};
        const float skew=(p.x+p.y+p.z)/3.f;
        const int i=int(std::floor(p.x+skew)),j=int(std::floor(p.y+skew)),k=int(std::floor(p.z+skew));
        const float unskew=float(i+j+k)/6.f;
        const V q{p.x-i+unskew,p.y-j+unskew,p.z-k+unskew};
        const int rx=(q.x>=q.y)+(q.x>=q.z),ry=(q.y>q.x)+(q.y>=q.z),rz=(q.z>q.x)+(q.z>q.y);
        float sum=0;
        for(int c=0;c<4;++c)
        {
            const int x=rx>=3-c,y=ry>=3-c,z=rz>=3-c;
            const float f=float(c)/6.f;
            const V d{q.x-x+f,q.y-y+f,q.z-z+f};
            float t=.6f-dot(d,d); if(t<=0) continue;
            const int h=perm[((i+x)&255)+perm[((j+y)&255)+perm[(k+z)&255]]];
            t*=t; sum+=t*t*dot(g[h%12],d);
        }
        return sum*32.f;
    }
};
struct Cutter { V center,axisX,axisY,axisZ,radius; float minRadius; };
struct Field
{
    GeometryParams params;
    Noise noise;
    std::vector<Cutter> cutters;
    V direction,tangent,up;
    std::array<float,8> frequencies{},weights{};
    int octaveCount=0;
    float step=0;
    explicit Field(GeometryParams p):params(p),noise(p.seed)
    {
        step=2.16f/float(p.resolution);
        const float y=p.excavationYaw*Pi/180.f,t=p.excavationPitch*Pi/180.f;
        direction={std::sin(y)*std::cos(t),std::sin(t),std::cos(y)*std::cos(t)};
        tangent=unit(cross({0,1,0},direction)); up=unit(cross(direction,tangent));
        float amplitude=1,frequency=p.erosionScale,total=0;
        for(int i=0;i<p.erosionOctaves;++i)
        {
            const float weight=amplitude*(1-smooth(.22f,.48f,frequency*step));
            if(weight<=0) break;
            frequencies[octaveCount]=frequency; weights[octaveCount]=weight;
            total+=weight; ++octaveCount; frequency*=2.03f; amplitude*=.48f;
        }
        for(int i=0;i<octaveCount;++i) weights[i]/=total;
        if(p.excavationStrength<=0 || p.cavityCount==0) return;
        Random rng{p.seed^0x43415645u};
        for(int i=0;i<p.cavityCount;++i)
        {
            // One dominant bite and connected secondary breakouts. They form
            // a broad asymmetric missing sector, not uniformly scattered pits.
            const float scale=i==0?1.f:rng.range(.36f,.7f);
            const float strength=std::sqrt(p.excavationStrength);
            const float size=p.cavitySize*scale*strength;
            const float offsetX=i==0?0.f:rng.range(-.58f,.6f);
            const float offsetY=i==0?-.08f:rng.range(-.8f,.75f);
            const float radial=1.20f-p.cavityDepth*.48f+(i==0?0.f:rng.range(-.03f,.25f));
            const V normal=unit(direction+tangent*(offsetX*.35f)+up*(offsetY*.22f));
            const V tx=unit(cross(up,normal)),ty=unit(cross(normal,tx));
            Cutter c;
            c.center=direction*radial+tangent*offsetX+up*offsetY;
            c.axisX=tx; c.axisY=ty; c.axisZ=normal;
            c.radius={size*rng.range(.9f,1.15f),size*rng.range(.95f,1.25f),size*rng.range(.75f,1.05f)};
            c.minRadius=(std::min)({c.radius.x,c.radius.y,c.radius.z});
            cutters.push_back(c);
        }
    }
    float fbm(V p) const
    {
        float s=0; for(int i=0;i<octaveCount;++i) s+=weights[i]*noise.sample(p*frequencies[i]+V{9.2f,17.8f,-3.7f});
        return s;
    }
    float base(V p) const
    {
        const float r=length(p);
        // Small broad surface relief preserves the recognisable lunar outline.
        // The compact band avoids noise evaluation in known interior/exterior.
        if(std::abs(r-1.f)>.04f) return r-1.f;
        return r-1.f-.009f*noise.sample(p*3.7f+V{1.3f,4.7f,2.1f});
    }
    float cutDistance(V p) const
    {
        float d=10;
        for(const auto& c:cutters)
        {
            const V q=p-c.center;
            const V e{dot(q,c.axisX)/c.radius.x,dot(q,c.axisY)/c.radius.y,dot(q,c.axisZ)/c.radius.z};
            d=(std::min)(d,(length(e)-1.f)*c.minRadius);
        }
        if(std::abs(d)<params.erosionStrength*1.6f+.025f)
        {
            // Shared world-space erosion joins neighboring excavations into
            // continuous rock instead of giving each cavity a separate pattern.
            const float rough=fbm(p);
            const float ridges=std::abs(noise.sample(p*(params.erosionScale*.57f)+V{-2,5,9}))-.35f;
            d+=params.erosionStrength*(rough*.85f+ridges*.45f);
        }
        return d;
    }
    float operator()(V p) const
    {
        const float b=base(p);
        if(b>.015f || cutters.empty()) return b;
        return (std::max)(b,-cutDistance(p));
    }
    V normal(V p) const
    {
        const float h=step*.16f;
        return unit({(*this)(p+V{h,0,0})-(*this)(p-V{h,0,0}),
            (*this)(p+V{0,h,0})-(*this)(p-V{0,h,0}),
            (*this)(p+V{0,0,h})-(*this)(p-V{0,0,h})});
    }
};

void uv(MeshVertex& v,V source)
{
    source=unit(source);
    v.u=std::atan2(source.z,source.x)/(2.f*Pi)+.5f;
    v.v=std::asin(std::clamp(source.y,-1.f,1.f))/Pi+.5f;
}
void bodyUv(MeshVertex& vertex,const Field& field)
{
    uv(vertex,vertex.position);
    const float interior=1-smooth(-.045f,-.006f,field.base(vertex.position));
    if(interior>0)
    {
        const float u=.5f+dot(vertex.position,field.tangent)*.34f;
        const float v=.5f+dot(vertex.position,field.up)*.34f;
        // Unwrap first so interpolation uses the shortest longitude interval.
        float wrapped=vertex.u;
        if(wrapped-u>.5f) wrapped-=1.f;
        if(wrapped-u<-.5f) wrapped+=1.f;
        vertex.u=wrapped*(1-interior)+u*interior;
        vertex.v=vertex.v*(1-interior)+v*interior;
    }
}
void color(MeshVertex& vertex,float interior)
{
    // An interior material distinction, without directional lighting or AO
    // baked into base color. Actual cavity shadows belong to the renderer.
    vertex.color[0]=1.f-interior*.12f;
    vertex.color[1]=1.f-interior*.16f;
    vertex.color[2]=1.f-interior*.20f;
    vertex.color[3]=1.f;
}
void appendTriangle(Geometry& mesh,std::uint32_t a,std::uint32_t b,std::uint32_t c,V outward)
{
    if(a==b || b==c || a==c) return;
    const auto& va=mesh.vertices[a]; const auto& vb=mesh.vertices[b]; const auto& vc=mesh.vertices[c];
    const V n=cross(vb.position-va.position,vc.position-va.position);
    if(dot(n,n)<=0.f) return;
    if(dot(n,outward)<0) std::swap(b,c);
    mesh.indices.push_back(a); mesh.indices.push_back(b); mesh.indices.push_back(c);
}

void makeBody(Geometry& mesh,const Field& field,const std::atomic_bool* cancel)
{
    const int n=field.params.resolution,side=n+1;
    constexpr float bound=1.08f;
    const float step=2*bound/n;
    const auto id=[&](int x,int y,int z) { return std::uint32_t((z*side+y)*side+x); };
    const auto position=[&](int x,int y,int z) { return V{x*step-bound,y*step-bound,z*step-bound}; };
    std::vector<float> values(std::size_t(side)*side*side);
    for(int z=0;z<=n;++z)
    {
        if(cancelled(cancel)) return;
        for(int y=0;y<=n;++y) for(int x=0;x<=n;++x)
        {
            float f=field(position(x,y,z));
            // Near-zero samples otherwise round several edge intersections onto
            // one endpoint in float32. Preserve the sign while keeping this
            // sub-voxel regularization far below the geometric detail scale.
            const float minimum=step*.001f;
            if(std::abs(f)<minimum) f=f<0?-minimum:minimum;
            values[id(x,y,z)]=f;
        }
    }
    // Store only edges touching the surface. Every cube shares the same
    // Freudenthal tetrahedralization, including all face diagonals.
    std::unordered_map<std::uint64_t,std::uint32_t> edgeVertices;
    edgeVertices.reserve(std::size_t(n)*n*12);
    mesh.vertices.reserve(std::size_t(n)*n*12); mesh.indices.reserve(std::size_t(n)*n*60);
    constexpr int off[8][3]={{0,0,0},{1,0,0},{1,1,0},{0,1,0},{0,0,1},{1,0,1},{1,1,1},{0,1,1}};
    constexpr int tets[6][4]={{0,5,1,6},{0,1,2,6},{0,2,3,6},{0,3,7,6},{0,7,4,6},{0,4,5,6}};
    for(int z=0;z<n;++z)
    {
        if(cancelled(cancel)) return;
        for(int y=0;y<n;++y) for(int x=0;x<n;++x)
        {
            std::uint32_t ids[8]; float f[8]; V p[8]; unsigned mask=0;
            for(int k=0;k<8;++k)
            {
                ids[k]=id(x+off[k][0],y+off[k][1],z+off[k][2]); f[k]=values[ids[k]];
                p[k]=position(x+off[k][0],y+off[k][1],z+off[k][2]); if(f[k]<0) mask|=1u<<k;
            }
            if(mask==0 || mask==255) continue;
            const auto edge=[&](int a,int b)
            {
                if(ids[a]>ids[b]) std::swap(a,b);
                const std::uint64_t key=(std::uint64_t(ids[a])<<32)|ids[b];
                const auto found=edgeVertices.find(key); if(found!=edgeVertices.end()) return found->second;
                const float t=f[a]/(f[a]-f[b]);
                MeshVertex v; v.position=p[a]+(p[b]-p[a])*t; v.normal=field.normal(v.position); bodyUv(v,field);
                const float interior=1-smooth(-.025f,-.002f,field.base(v.position)); color(v,interior);
                const auto index=std::uint32_t(mesh.vertices.size()); mesh.vertices.push_back(v); edgeVertices.emplace(key,index); return index;
            };
            for(const auto& tetra:tets)
            {
                int inside[4],outside[4],ni=0,no=0;
                for(int k:tetra) { if(f[k]<0) inside[ni++]=k; else outside[no++]=k; }
                if(ni==0 || ni==4) continue;
                V inCenter{},outCenter{};
                for(int k=0;k<ni;++k) inCenter=inCenter+p[inside[k]];
                for(int k=0;k<no;++k) outCenter=outCenter+p[outside[k]];
                const V outward=outCenter*(1.f/no)-inCenter*(1.f/ni);
                if(ni==1) appendTriangle(mesh,edge(inside[0],outside[0]),edge(inside[0],outside[1]),edge(inside[0],outside[2]),outward);
                else if(ni==3) appendTriangle(mesh,edge(outside[0],inside[0]),edge(outside[0],inside[1]),edge(outside[0],inside[2]),outward);
                else
                {
                    const auto a=edge(inside[0],outside[0]),b=edge(inside[0],outside[1]);
                    const auto c=edge(inside[1],outside[0]),d=edge(inside[1],outside[1]);
                    appendTriangle(mesh,a,b,c,outward); appendTriangle(mesh,b,d,c,outward);
                }
            }
        }
    }
    mesh.parts.push_back({0,std::uint32_t(mesh.indices.size()),false,{0,0,0}});
}

struct Quadric
{
    // Symmetric homogeneous 4x4 matrix, stored in its upper triangle.
    double q[10]{};
    void plane(V normal,double d)
    {
        const double x=normal.x,y=normal.y,z=normal.z;
        const double v[10]{x*x,x*y,x*z,x*d,y*y,y*z,y*d,z*z,z*d,d*d};
        for(int i=0;i<10;++i) q[i]+=v[i];
    }
    Quadric operator+(const Quadric& other) const
    { Quadric r; for(int i=0;i<10;++i) r.q[i]=q[i]+other.q[i]; return r; }
    double cost(V p) const
    {
        const double x=p.x,y=p.y,z=p.z;
        return (std::max)(0.0,q[0]*x*x+2*q[1]*x*y+2*q[2]*x*z+2*q[3]*x+
            q[4]*y*y+2*q[5]*y*z+2*q[6]*y+q[7]*z*z+2*q[8]*z+q[9]);
    }
    V best(V a,V b) const
    {
        V result=(a+b)*.5f;
        double score=cost(result);
        for(V p:{a,b}) { const double c=cost(p); if(c<score) { score=c; result=p; } }
        const double A=q[0],B=q[1],C=q[2],D=q[4],E=q[5],F=q[7];
        const double det=A*(D*F-E*E)-B*(B*F-C*E)+C*(B*E-C*D);
        if(std::abs(det)>1.e-12)
        {
            const double x=-q[3],y=-q[6],z=-q[8];
            const V p{float(((D*F-E*E)*x+(C*E-B*F)*y+(B*E-C*D)*z)/det),
                float(((C*E-B*F)*x+(A*F-C*C)*y+(B*C-A*E)*z)/det),
                float(((B*E-C*D)*x+(B*C-A*E)*y+(A*D-B*B)*z)/det)};
            if(dot(p-(a+b)*.5f,p-(a+b)*.5f)<dot(a-b,a-b)*4.f && cost(p)<score) result=p;
        }
        return result;
    }
};
void simplifyBody(Geometry& mesh,const Field& field,const std::atomic_bool* cancel)
{
    // Field extraction oversamples smooth faces because every cube contains six
    // tetrahedra. Collapse only manifold edges whose local triangles keep their
    // orientation. QEM retains the silhouette and sharp cavity boundaries.
    struct Face { std::array<std::uint32_t,3> v; bool live=true; };
    struct Candidate
    {
        double cost; std::uint32_t a,b,versionA,versionB; V position;
        bool operator<(const Candidate& r) const
        { if(cost!=r.cost) return cost>r.cost; if(a!=r.a) return a>r.a; return b>r.b; }
    };
    const auto vertexCount=mesh.vertices.size(),faceCount=mesh.indices.size()/3;
    if(faceCount<100) return;
    std::vector<Face> faces; faces.reserve(faceCount);
    std::vector<Quadric> quadrics(vertexCount);
    std::vector<std::vector<std::uint32_t>> adjacent(vertexCount);
    std::vector<std::uint32_t> versions(vertexCount,0);
    std::vector<bool> alive(vertexCount,true);
    for(std::size_t i=0;i<faceCount;++i)
    {
        if((i&4095u)==0 && cancelled(cancel)) return;
        const std::array<std::uint32_t,3> tri{mesh.indices[i*3],mesh.indices[i*3+1],mesh.indices[i*3+2]};
        faces.push_back({tri,true});
        const V a=mesh.vertices[tri[0]].position,b=mesh.vertices[tri[1]].position,c=mesh.vertices[tri[2]].position;
        const V normal=unit(cross(b-a,c-a)); const double d=-dot(normal,a);
        for(auto id:tri) { quadrics[id].plane(normal,d); adjacent[id].push_back(std::uint32_t(i)); }
    }
    std::priority_queue<Candidate> queue;
    const auto enqueue=[&](std::uint32_t a,std::uint32_t b)
    {
        if(a==b || !alive[a] || !alive[b]) return;
        if(a>b) std::swap(a,b);
        const Quadric q=quadrics[a]+quadrics[b];
        const V p=q.best(mesh.vertices[a].position,mesh.vertices[b].position);
        queue.push({q.cost(p),a,b,versions[a],versions[b],p});
    };
    std::vector<std::uint64_t> edges; edges.reserve(faceCount*3);
    for(const auto& f:faces) for(int k=0;k<3;++k)
    {
        auto a=f.v[k],b=f.v[(k+1)%3]; if(a>b) std::swap(a,b);
        edges.push_back((std::uint64_t(a)<<32)|b);
    }
    std::sort(edges.begin(),edges.end()); edges.erase(std::unique(edges.begin(),edges.end()),edges.end());
    for(std::size_t i=0;i<edges.size();++i)
    {
        if((i&4095u)==0 && cancelled(cancel)) return;
        const auto edge=edges[i]; enqueue(std::uint32_t(edge>>32),std::uint32_t(edge));
    }
    edges.clear(); edges.shrink_to_fit();
    const std::size_t target=(std::min)(std::size_t(80000),std::size_t(faceCount*.34));
    std::size_t remaining=faceCount,iterations=0;
    std::vector<std::uint32_t> local,neighborsA,neighborsB,intersection;
    std::vector<std::array<std::uint32_t,3>> newFaces;
    while(remaining>target && !queue.empty())
    {
        if((++iterations&4095u)==0 && cancelled(cancel)) return;
        const Candidate candidate=queue.top(); queue.pop();
        const auto a=candidate.a,b=candidate.b;
        if(!alive[a] || !alive[b] || versions[a]!=candidate.versionA || versions[b]!=candidate.versionB) continue;
        // Stop before high-frequency rock details are lost. This threshold is
        // in radius-one object coordinates, independent of exported scale.
        if(candidate.cost>0.00003) break;
        local=adjacent[a]; local.insert(local.end(),adjacent[b].begin(),adjacent[b].end());
        std::sort(local.begin(),local.end()); local.erase(std::unique(local.begin(),local.end()),local.end());
        neighborsA.clear(); neighborsB.clear(); newFaces.clear(); int shared=0; bool valid=true;
        for(auto fi:local)
        {
            const auto& f=faces[fi]; if(!f.live) continue;
            const bool hasA=f.v[0]==a||f.v[1]==a||f.v[2]==a;
            const bool hasB=f.v[0]==b||f.v[1]==b||f.v[2]==b;
            if(!hasA && !hasB) continue;
            for(auto v:f.v) if(v!=a && v!=b) { if(hasA) neighborsA.push_back(v); if(hasB) neighborsB.push_back(v); }
            if(hasA && hasB) { ++shared; continue; }
            auto canonical=f.v;
            for(auto& vertex:canonical) if(vertex==b) vertex=a;
            std::sort(canonical.begin(),canonical.end()); newFaces.push_back(canonical);
            V old[3],next[3];
            for(int k=0;k<3;++k) { old[k]=mesh.vertices[f.v[k]].position; next[k]=(f.v[k]==a||f.v[k]==b)?candidate.position:old[k]; }
            const V before=cross(old[1]-old[0],old[2]-old[0]),after=cross(next[1]-next[0],next[2]-next[0]);
            const float beforeSq=dot(before,before),afterSq=dot(after,after);
            if(afterSq<1.e-20f || dot(before,after)<.35f*std::sqrt(beforeSq*afterSq)) { valid=false; break; }
        }
        if(!valid || shared!=2) continue;
        std::sort(newFaces.begin(),newFaces.end());
        if(std::adjacent_find(newFaces.begin(),newFaces.end())!=newFaces.end()) continue;
        const auto unique=[](std::vector<std::uint32_t>& v) { std::sort(v.begin(),v.end()); v.erase(std::unique(v.begin(),v.end()),v.end()); };
        unique(neighborsA); unique(neighborsB); intersection.clear();
        std::set_intersection(neighborsA.begin(),neighborsA.end(),neighborsB.begin(),neighborsB.end(),std::back_inserter(intersection));
        if(intersection.size()!=2) continue; // Link condition: preserve topology.
        mesh.vertices[a].position=candidate.position;
        quadrics[a]=quadrics[a]+quadrics[b]; alive[b]=false; ++versions[a]; ++versions[b];
        adjacent[a].clear(); adjacent[b].clear();
        for(auto fi:local)
        {
            auto& f=faces[fi]; if(!f.live) continue;
            for(auto& v:f.v) if(v==b) v=a;
            if(f.v[0]==f.v[1] || f.v[1]==f.v[2] || f.v[0]==f.v[2]) { f.live=false; --remaining; continue; }
            if(f.v[0]==a||f.v[1]==a||f.v[2]==a) adjacent[a].push_back(fi);
        }
        for(auto fi:adjacent[a]) for(auto v:faces[fi].v) if(v!=a) enqueue(a,v);
    }
    std::vector<std::uint32_t> remap(vertexCount,(std::numeric_limits<std::uint32_t>::max)());
    std::vector<MeshVertex> vertices; vertices.reserve(remaining/2+2);
    mesh.indices.clear(); mesh.indices.reserve(remaining*3);
    for(const auto& f:faces) if(f.live) for(auto id:f.v)
    {
        if(remap[id]==(std::numeric_limits<std::uint32_t>::max)())
        {
            auto v=mesh.vertices[id]; v.normal=field.normal(v.position); bodyUv(v,field);
            color(v,1-smooth(-.025f,-.002f,field.base(v.position)));
            remap[id]=std::uint32_t(vertices.size()); vertices.push_back(v);
        }
        mesh.indices.push_back(remap[id]);
    }
    mesh.vertices.swap(vertices);
    mesh.parts[0].indexCount=std::uint32_t(mesh.indices.size());
}

struct FragmentShape
{
    const Noise& noise;
    V axes,offset;
    std::array<V,5> planes;
    std::array<float,5> cuts;
    float radius(V direction) const
    {
        // Low-frequency volume noise, anisotropy, and cleavage planes make
        // rocks with chipped facets rather than small spherical moon copies.
        const V d{direction.x/axes.x,direction.y/axes.y,direction.z/axes.z};
        float r=(.88f+.14f*noise.sample(direction*2.6f+offset)+.06f*noise.sample(direction*6.1f+offset)) / length(d);
        for(int i=0;i<5;++i)
        {
            const float denominator=dot(direction,planes[i]);
            if(denominator>0) r=(std::min)(r,cuts[i]/denominator);
        }
        return r;
    }
    float field(V p) const { const float r=length(p); return r-radius(p*(1.f/(std::max)(r,1e-12f))); }
    V normal(V p) const
    {
        constexpr float h=.001f;
        return unit({field(p+V{h,0,0})-field(p-V{h,0,0}),field(p+V{0,h,0})-field(p-V{0,h,0}),field(p+V{0,0,h})-field(p-V{0,0,h})});
    }
};
void makeFragments(Geometry& mesh,const Field& field,const std::atomic_bool* cancel)
{
    if(field.cutters.empty() || field.params.debrisSize<=0) return;
    Random rng{field.params.seed^0x44454252u};
    std::vector<V> placed; std::vector<float> radii;
    for(int fragment=0;fragment<field.params.debrisCount;++fragment)
    {
        if(cancelled(cancel)) return;
        const float size=field.params.debrisSize*(fragment<3?rng.range(1.15f,1.8f):rng.range(.24f,.85f));
        FragmentShape shape{field.noise,{rng.range(.7f,1.3f),rng.range(.7f,1.3f),rng.range(.7f,1.3f)},rng.sphere()*23.f,{},{}};
        for(int i=0;i<5;++i) { shape.planes[i]=rng.sphere(); shape.cuts[i]=rng.range(.48f,.83f); }
        V center{};
        for(int attempt=0;attempt<24;++attempt)
        {
            const V radial=unit(field.direction*rng.range(.3f,.9f)+rng.sphere()*rng.range(.7f,1.3f));
            center=radial*(1.10f+size*1.5f+field.params.debrisSpread*rng.range(.05f,1.f));
            bool separated=true;
            for(std::size_t i=0;i<placed.size();++i) if(length(center-placed[i])<(radii[i]+size)*1.35f) { separated=false; break; }
            if(separated) break;
        }
        placed.push_back(center); radii.push_back(size);
        // Object-local UVs are derived from the source region, before pieces
        // move away. They do not slide as separation parameters are adjusted.
        const V sourceCenter=field.direction*.85f+field.tangent*rng.range(-.3f,.3f)+field.up*rng.range(-.4f,.4f);
        const V axisZ=rng.sphere(),axisX=unit(cross(axisZ,std::abs(axisZ.y)<.9f?V{0,1,0}:V{1,0,0})),axisY=cross(axisZ,axisX);
        const auto rotate=[&](V p) { return axisX*p.x+axisY*p.y+axisZ*p.z; };
        // Icosphere subdivision has no degenerate polar triangles. Shared
        // midpoint indices keep every fragment topologically closed.
        const float t=(1.f+std::sqrt(5.f))*.5f;
        std::vector<V> points{{-1,t,0},{1,t,0},{-1,-t,0},{1,-t,0},{0,-1,t},{0,1,t},{0,-1,-t},{0,1,-t},{t,0,-1},{t,0,1},{-t,0,-1},{-t,0,1}};
        for(auto& p:points) p=unit(p);
        std::vector<std::array<std::uint32_t,3>> triangles{{0,11,5},{0,5,1},{0,1,7},{0,7,10},{0,10,11},{1,5,9},{5,11,4},{11,10,2},{10,7,6},{7,1,8},{3,9,4},{3,4,2},{3,2,6},{3,6,8},{3,8,9},{4,9,5},{2,4,11},{6,2,10},{8,6,7},{9,8,1}};
        const int subdivisions=fragment<3 && field.params.resolution>=96?3:2;
        for(int level=0;level<subdivisions;++level)
        {
            std::unordered_map<std::uint64_t,std::uint32_t> edges;
            const auto mid=[&](std::uint32_t a,std::uint32_t b)
            {
                if(a>b) std::swap(a,b);
                const std::uint64_t key=(std::uint64_t(a)<<32)|b;
                auto f=edges.find(key); if(f!=edges.end()) return f->second;
                const auto i=std::uint32_t(points.size()); points.push_back(unit(points[a]+points[b])); edges.emplace(key,i); return i;
            };
            std::vector<std::array<std::uint32_t,3>> next; next.reserve(triangles.size()*4);
            for(auto tri:triangles) { const auto a=mid(tri[0],tri[1]),b=mid(tri[1],tri[2]),c=mid(tri[2],tri[0]); next.push_back({tri[0],a,c}); next.push_back({tri[1],b,a}); next.push_back({tri[2],c,b}); next.push_back({a,b,c}); }
            triangles.swap(next);
        }
        const auto firstVertex=std::uint32_t(mesh.vertices.size()),firstIndex=std::uint32_t(mesh.indices.size());
        for(V d:points)
        {
            const V local=d*shape.radius(d); MeshVertex vertex;
            vertex.position=center+rotate(local)*size; vertex.normal=rotate(shape.normal(local));
            uv(vertex,sourceCenter+rotate(local)*size); color(vertex,.5f); mesh.vertices.push_back(vertex);
        }
        for(auto tri:triangles)
        {
            const V outward=mesh.vertices[firstVertex+tri[0]].position+mesh.vertices[firstVertex+tri[1]].position+mesh.vertices[firstVertex+tri[2]].position-center*3.f;
            appendTriangle(mesh,firstVertex+tri[0],firstVertex+tri[1],firstVertex+tri[2],outward);
        }
        mesh.parts.push_back({firstIndex,std::uint32_t(mesh.indices.size())-firstIndex,true,center});
    }
}
void finish(Geometry& mesh)
{
    if(mesh.vertices.empty()) return;
    // The analytic field gradient is a good smooth normal except where a
    // low-resolution triangle bridges a sharp subtraction crease. Such a face
    // can oppose the interpolated gradients even though its winding is correct.
    // Give only those faces independent geometric normals; do not corrupt
    // winding to accommodate shading. Position-welded topology is unchanged.
    for(std::size_t i=0;i<mesh.indices.size();i+=3)
    {
        const auto& a=mesh.vertices[mesh.indices[i]];
        const auto& b=mesh.vertices[mesh.indices[i+1]];
        const auto& c=mesh.vertices[mesh.indices[i+2]];
        const V geometric=cross(b.position-a.position,c.position-a.position);
        if(dot(geometric,a.normal+b.normal+c.normal)>=0) continue;
        const V normal=unit(geometric);
        for(int k=0;k<3;++k)
        {
            auto vertex=mesh.vertices[mesh.indices[i+k]]; vertex.normal=normal;
            mesh.indices[i+k]=std::uint32_t(mesh.vertices.size()); mesh.vertices.push_back(vertex);
        }
    }
    // Repeat longitude across the seam. Duplicate only seam vertices, keeping
    // all interior grid edges indexed and the positions themselves watertight.
    std::unordered_map<std::uint32_t,std::uint32_t> wrapped;
    for(std::size_t i=0;i<mesh.indices.size();i+=3)
    {
        float lo=2,hi=-1;
        for(int k=0;k<3;++k) { const float u=mesh.vertices[mesh.indices[i+k]].u; lo=(std::min)(lo,u); hi=(std::max)(hi,u); }
        if(hi-lo<=.5f) continue;
        for(int k=0;k<3;++k)
        {
            auto& index=mesh.indices[i+k]; if(mesh.vertices[index].u>=.5f) continue;
            const auto found=wrapped.find(index);
            if(found!=wrapped.end()) index=found->second;
            else { auto v=mesh.vertices[index]; v.u+=1; const auto next=std::uint32_t(mesh.vertices.size()); mesh.vertices.push_back(v); wrapped.emplace(index,next); index=next; }
        }
    }
    mesh.boundsMin=mesh.boundsMax=mesh.vertices.front().position;
    for(const auto& v:mesh.vertices)
    {
        mesh.boundsMin.x=(std::min)(mesh.boundsMin.x,v.position.x); mesh.boundsMin.y=(std::min)(mesh.boundsMin.y,v.position.y); mesh.boundsMin.z=(std::min)(mesh.boundsMin.z,v.position.z);
        mesh.boundsMax.x=(std::max)(mesh.boundsMax.x,v.position.x); mesh.boundsMax.y=(std::max)(mesh.boundsMax.y,v.position.y); mesh.boundsMax.z=(std::max)(mesh.boundsMax.z,v.position.z);
    }
}
}
GeometryParams sanitizeGeometry(GeometryParams p)
{
    const GeometryParams d;
    p.resolution=std::clamp(p.resolution,32,256);
    p.excavationStrength=safe(p.excavationStrength,d.excavationStrength,0,1.6f);
    p.cavityCount=std::clamp(p.cavityCount,0,12);
    p.cavitySize=safe(p.cavitySize,d.cavitySize,.2f,1.1f);
    p.cavityDepth=safe(p.cavityDepth,d.cavityDepth,.1f,1.2f);
    p.excavationYaw=safe(p.excavationYaw,d.excavationYaw,-360,360);
    p.excavationPitch=safe(p.excavationPitch,d.excavationPitch,-85,85);
    p.erosionStrength=safe(p.erosionStrength,d.erosionStrength,0,.18f);
    p.erosionScale=safe(p.erosionScale,d.erosionScale,1,16);
    p.erosionOctaves=std::clamp(p.erosionOctaves,1,8);
    p.debrisCount=std::clamp(p.debrisCount,0,96);
    p.debrisSpread=safe(p.debrisSpread,d.debrisSpread,0,1.8f);
    p.debrisSize=safe(p.debrisSize,d.debrisSize,0,.25f);
    return p;
}
Geometry generateGeometry(const GeometryParams& parameters,const std::atomic_bool* cancel)
{
    if(cancelled(cancel)) return {};
    const Field field(sanitizeGeometry(parameters));
    Geometry mesh; makeBody(mesh,field,cancel);
    if(cancelled(cancel)) return {};
    simplifyBody(mesh,field,cancel);
    if(cancelled(cancel)) return {};
    makeFragments(mesh,field,cancel);
    if(cancelled(cancel)) return {};
    finish(mesh);
    return mesh;
}
}
