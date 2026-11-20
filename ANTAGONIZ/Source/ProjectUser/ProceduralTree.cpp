#include "ProceduralTree.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <limits>

namespace EnduranceTrees
{
namespace
{
constexpr float Pi=3.14159265358979323846f;
constexpr std::size_t MaxBranches=384, MaxCards=8192;

bool finite(float x)
{
    std::uint32_t bits=0; std::memcpy(&bits,&x,sizeof(bits));
    return (bits & 0x7f800000u)!=0x7f800000u;
}
float bounded(float x,float lo,float hi,float fallback)
{ return std::clamp(finite(x) ? x : fallback,lo,hi); }
float sat(float x) { return std::clamp(x,0.0f,1.0f); }
float smooth(float a,float b,float x)
{ const float t=sat((x-a)/(b-a)); return t*t*(3.0f-2.0f*t); }
std::uint32_t hash(std::uint32_t x)
{ x^=x>>16u; x*=0x7feb352du; x^=x>>15u; x*=0x846ca68bu; return x^(x>>16u); }
struct Random
{
    std::uint32_t state;
    explicit Random(std::uint32_t s):state(s) {}
    float unit() { state+=0x9e3779b9u; return float(hash(state)>>8u)*(1.0f/16777216.0f); }
    float range(float a,float b) { return a+(b-a)*unit(); }
    float signedUnit() { return range(-1.0f,1.0f); }
};
glm::vec3 safeNormal(glm::vec3 v,glm::vec3 fallback={0.0f,1.0f,0.0f})
{ const float q=glm::dot(v,v); return q>1.0e-12f ? v/std::sqrt(q) : fallback; }
glm::vec3 perpendicular(glm::vec3 n)
{ return safeNormal(glm::cross(n,std::abs(n.y)<0.85f ? glm::vec3(0,1,0) : glm::vec3(1,0,0)),{1,0,0}); }
bool cancelled(const std::atomic_bool* flag)
{ return flag && flag->load(std::memory_order_relaxed); }
glm::vec3 paletteColor(glm::vec3 color,glm::vec3 fallback)
{
    for(int k=0;k<3;++k) color[k]=bounded(color[k],0.0f,1.0f,fallback[k]);
    return color;
}
float valueNoise(float x,float y)
{
    const int ix=int(std::floor(x)),iy=int(std::floor(y));
    const float fx=x-float(ix),fy=y-float(iy);
    const float u=fx*fx*(3-2*fx),v=fy*fy*(3-2*fy);
    auto sample=[](int a,int b) { return float(hash(std::uint32_t(a)*0x8da6b343u ^ std::uint32_t(b)*0xd8163841u)>>8u)/16777216.0f; };
    return glm::mix(glm::mix(sample(ix,iy),sample(ix+1,iy),u),glm::mix(sample(ix,iy+1),sample(ix+1,iy+1),u),v);
}
BranchPoint sample(const Branch& b,float t)
{
    const float f=sat(t)*float(b.points.size()-1);
    const std::size_t i=(std::min)(std::size_t(f),b.points.size()-2);
    const float a=f-float(i);
    return {glm::mix(b.points[i].position,b.points[i+1].position,a),glm::mix(b.points[i].radius,b.points[i+1].radius,a)};
}
glm::vec3 tangentAt(const Branch& b,float t)
{ return safeNormal(sample(b,(std::min)(1.0f,t+0.03f)).position-sample(b,(std::max)(0.0f,t-0.03f)).position); }

Branch curve(glm::vec3 start,glm::vec3 end,glm::vec3 startDirection,glm::vec3 endDirection,
             float radius,float tipRadius,int parent,int level,float attachment,
             float bend,Random& rng)
{
    Branch b; b.parent=parent; b.level=level; b.attachment=attachment;
    const float len=glm::length(end-start);
    const glm::vec3 c1=start+safeNormal(startDirection)*(len*0.40f);
    const glm::vec3 c2=end-safeNormal(endDirection)*(len*0.32f);
    const glm::vec3 side=perpendicular(safeNormal(end-start));
    const float phase=rng.range(0,2*Pi),wobble=rng.range(0.6f,1.0f)*bend;
    const int segments=std::clamp(int(len*0.7f)+5,7,16);
    for(int i=0;i<=segments;++i)
    {
        const float t=float(i)/float(segments),s=1-t;
        glm::vec3 p=s*s*s*start+3*s*s*t*c1+3*s*t*t*c2+t*t*t*end;
        p+=side*(std::sin(t*Pi)*std::sin(t*Pi*2.0f+phase)*len*wobble*0.12f);
        // Smooth diminishing sections. A short basal collar sits inside the parent.
        float r=glm::mix(tipRadius,radius,std::pow(s,0.82f));
        r*=1.0f+0.10f*(1-smooth(0,0.18f,t));
        p.y=(std::max)(0.02f,p.y);
        b.points.push_back({p,r});
    }
    return b;
}

void buildSkeleton(Tree& tree,const std::atomic_bool* stop)
{
    const Params& p=tree.params; Random rng(p.seed);
    const float direction=rng.range(0,2*Pi);
    const glm::vec3 lean(std::cos(direction),0,std::sin(direction));
    Branch trunk; trunk.parent=-1; trunk.level=0;
    for(int i=0;i<=16;++i)
    {
        const float t=float(i)/16;
        glm::vec3 pos=lean*(p.height*p.trunkBend*(0.70f*t*t+0.18f*std::sin(t*Pi*1.7f)));
        pos+=glm::vec3(-lean.z,0,lean.x)*(p.height*p.trunkBend*0.22f*std::sin(t*Pi*2));
        pos.y=t*p.height;
        const float flare=1.0f+0.60f*(1-smooth(0,0.13f,t));
        const float r=(p.trunkRadius*std::pow(1-t,0.85f)+p.trunkRadius*0.015f)*flare;
        trunk.points.push_back({pos,r});
    }
    tree.branches.push_back(trunk);
    const bool conifer=p.family==Family::Conifer;
    const int primaryCount=conifer ? p.branchCount*3 : p.branchCount;
    for(int i=0;i<primaryCount;++i)
    {
        if(cancelled(stop)) return;
        const float tier=conifer ? float(i/3)/float((std::max)(1,p.branchCount-1)) : float(i)/float((std::max)(1,primaryCount-1));
        const float attach=glm::mix(p.branchStart,0.91f,tier)+rng.range(-0.015f,0.015f);
        const BranchPoint base=sample(trunk,attach);
        const float azimuth=direction+float(i)*2.39996323f+rng.range(-0.32f,0.32f);
        const glm::vec3 radial(std::cos(azimuth),0,std::sin(azimuth));
        float spread=p.crownWidth*0.5f*rng.range(0.76f,1.12f);
        spread*=conifer ? std::pow(1-tier*0.92f,0.70f) : (0.68f+0.32f*std::sin(tier*Pi));
        const float angle=p.branchAngle*Pi/180.0f;
        const glm::vec3 branchDirection=safeNormal(radial*std::sin(angle)+glm::vec3(0,std::cos(angle),0));
        glm::vec3 end=base.position+radial*spread;
        if(conifer) end.y=base.position.y+p.crownHeight*0.06f;
        else
        {
            const float crownBase=p.height-p.crownHeight;
            end.y=glm::mix((std::max)(base.position.y+p.height*0.06f,crownBase+p.crownHeight*0.40f),p.height*1.01f,tier);
            end.y+=rng.range(-0.07f,0.07f)*p.crownHeight;
        }
        glm::vec3 endDirection=radial*0.5f+glm::vec3(0,conifer ? 0.18f : 1.0f,0);
        const float radius=base.radius*rng.range(0.52f,0.73f);
        tree.branches.push_back(curve(base.position,end,branchDirection,endDirection,radius,radius*0.075f,0,1,attach,p.curvature,rng));
    }
    std::size_t levelBegin=1,levelEnd=tree.branches.size();
    for(int level=2;level<=p.branchingLevels;++level)
    {
        const std::size_t nextBegin=tree.branches.size();
        for(std::size_t parent=levelBegin;parent<levelEnd;++parent)
        {
            if(cancelled(stop)) return;
            // Copy because append may reallocate the branch vector.
            const Branch old=tree.branches[parent];
            const float len=glm::length(old.points.back().position-old.points.front().position);
            const int children=std::clamp(int(p.twigDensity*rng.range(1.6f,2.8f)+0.5f),1,5);
            for(int j=0;j<children;++j)
            {
                if(tree.branches.size()>=MaxBranches) { tree.statistics.budgetLimited=true; return; }
                const float attach=glm::mix(0.40f,0.86f,(float(j)+rng.range(0.1f,0.9f))/float(children));
                const BranchPoint base=sample(old,attach);
                const glm::vec3 t=tangentAt(old,attach),u=perpendicular(t),v=glm::cross(t,u);
                const float azimuth=float(j)*2.39996323f+rng.range(-0.7f,0.7f)+float(parent)*1.171f;
                const glm::vec3 side=u*std::cos(azimuth)+v*std::sin(azimuth);
                const float angle=(p.branchAngle*0.6f+rng.range(-8,8))*Pi/180.0f;
                glm::vec3 dir=safeNormal(t*std::cos(angle)+side*std::sin(angle));
                const float branchLength=len*rng.range(0.36f,0.60f);
                glm::vec3 end=base.position+dir*branchLength;
                glm::vec3 endDir=dir;
                if(p.family==Family::Weeping)
                {
                    end.y-=branchLength*0.62f;
                    endDir=safeNormal(dir*0.3f+glm::vec3(0,-1.0f,0));
                }
                else if(!conifer)
                {
                    end.y+=branchLength*0.28f;
                    endDir=safeNormal(dir+glm::vec3(0,0.55f,0));
                }
                else { end.y+=branchLength*0.10f; endDir=safeNormal(dir+glm::vec3(0,0.15f,0)); }
                // Artistic crown envelope bounds secondary spread, retaining asymmetric tips.
                const glm::vec3 center=lean*(p.height*p.trunkBend*0.6f);
                glm::vec2 xz(end.x-center.x,end.z-center.z);
                const float maxSpread=p.crownWidth*0.68f;
                const float outside=glm::length(xz);
                if(outside>maxSpread) { xz*=maxSpread/outside; end.x=center.x+xz.x; end.z=center.z+xz.y; }
                end.y=std::clamp(end.y,p.height*0.08f,p.height+p.crownHeight*0.20f);
                const float radius=(std::max)(0.006f,base.radius*p.branchTaper);
                tree.branches.push_back(curve(base.position,end,dir,endDir,radius,radius*0.15f,int(parent),level,attach,p.curvature,rng));
            }
        }
        levelBegin=nextBegin; levelEnd=tree.branches.size();
    }
}

void pushWoodVertex(Mesh& mesh,const Params& p,glm::vec3 position,glm::vec3 normal,
                    glm::vec2 uv,float shade,float flexibility,float phase)
{
    const float h=sat(position.y/p.height);
    Vertex vertex; vertex.position=position; vertex.normal=normal; vertex.uv=uv;
    const float weight=sat(0.32f+0.38f*h+shade);
    vertex.color=glm::vec4(glm::mix(p.barkLow,p.barkHigh,weight),1);
    vertex.wind={h,flexibility,phase,weight};
    mesh.vertices.push_back(vertex);
}
void meshBranch(Tree& tree,const Branch& branch,std::size_t id,int lod)
{
    Mesh& mesh=tree.lods[std::size_t(lod)].wood;
    const Params& p=tree.params;
    if(lod==1 && branch.level>=3) return;
    const int sides=branch.level==0 ? (lod==0 ? 12 : 7) : (lod==0 ? 8 : 5);
    const int segmentStride=lod==0 ? 1 : 2;
    std::vector<BranchPoint> points;
    for(std::size_t k=0;k<branch.points.size();k+=std::size_t(segmentStride)) points.push_back(branch.points[k]);
    if((branch.points.size()-1)%std::size_t(segmentStride)!=0) points.push_back(branch.points.back());
    const std::uint32_t offset=std::uint32_t(mesh.vertices.size());
    glm::vec3 tangent=safeNormal(points[1].position-points[0].position);
    glm::vec3 axis=perpendicular(tangent);
    float arclength=0;
    const float phase=float(hash(std::uint32_t(id)^p.seed)>>8u)/16777216.0f;
    for(std::size_t i=0;i<points.size();++i)
    {
        const glm::vec3 before=points[i==0 ? 0 : i-1].position;
        const glm::vec3 after=points[(std::min)(i+1,points.size()-1)].position;
        tangent=safeNormal(after-before,tangent);
        if(branch.parent<0 && i==0) tangent={0,1,0}; // Flat root footprint at ground level.
        // Projection transports the frame without Frenet-frame flips on straight sections.
        axis=safeNormal(axis-tangent*glm::dot(axis,tangent),perpendicular(tangent));
        const glm::vec3 crossAxis=safeNormal(glm::cross(tangent,axis));
        if(i) arclength+=glm::length(points[i].position-points[i-1].position);
        const float ringT=float(i)/float(points.size()-1);
        for(int j=0;j<=sides;++j)
        {
            const float angle=float(j)*2*Pi/float(sides);
            glm::vec3 radial=axis*std::cos(angle)+crossAxis*std::sin(angle);
            const float lobes=1+0.07f*std::cos(angle*3+phase*6.28f)*(1-ringT);
            const glm::vec3 position=points[i].position+radial*(points[i].radius*lobes);
            const float taper=(i+1<points.size()) ? (points[i].radius-points[i+1].radius)/(glm::length(points[i+1].position-points[i].position)+0.0001f) : 0.0f;
            glm::vec3 normal=safeNormal(radial+tangent*taper);
            // Collar normal blending hides the intersection seam, without claiming a welded mesh.
            if(branch.parent>=0 && ringT<0.22f)
            {
                const Branch& parent=tree.branches[std::size_t(branch.parent)];
                const glm::vec3 anchor=sample(parent,branch.attachment).position;
                const glm::vec3 parentT=tangentAt(parent,branch.attachment);
                const glm::vec3 relative=position-anchor;
                const glm::vec3 parentN=safeNormal(relative-parentT*glm::dot(relative,parentT),normal);
                normal=safeNormal(glm::mix(parentN,normal,smooth(0,0.22f,ringT)),normal);
            }
            const float shade=0.10f*std::sin(angle*3+phase*6.28f);
            // Uniform UV metres along each branch; pattern follows its axis.
            pushWoodVertex(mesh,p,position,normal,{float(j)/float(sides),arclength*0.28f},shade,
                           float(branch.level)*0.22f,phase);
        }
    }
    const std::uint32_t stride=std::uint32_t(sides+1);
    for(std::uint32_t i=0;i+1<points.size();++i)
    for(std::uint32_t j=0;j<std::uint32_t(sides);++j)
    {
        const std::uint32_t a=offset+i*stride+j,b=a+stride;
        // Outward winding for (axis, cross(tangent,axis)).
        mesh.indices.insert(mesh.indices.end(),{a,a+1,b,a+1,b+1,b});
    }
    auto cap=[&](bool top)
    {
        const std::size_t ring=top ? points.size()-1 : 0;
        const glm::vec3 t=top ? safeNormal(points.back().position-points[points.size()-2].position)
                             : (branch.parent<0 ? glm::vec3(0,-1,0) : -safeNormal(points[1].position-points[0].position));
        const std::uint32_t center=std::uint32_t(mesh.vertices.size());
        pushWoodVertex(mesh,p,points[ring].position,t,{0.5f,0.5f},0,float(branch.level)*0.22f,phase);
        for(std::uint32_t j=0;j<std::uint32_t(sides);++j)
        {
            const std::uint32_t a=offset+std::uint32_t(ring)*stride+j;
            if(top) mesh.indices.insert(mesh.indices.end(),{center,a,a+1});
            else mesh.indices.insert(mesh.indices.end(),{center,a+1,a});
        }
    };
    // Closed ends keep individual branch assets valid for export/inspection.
    cap(false); cap(true);
}

struct LeafCard
{
    glm::vec3 center,right,up,volumeNormal;
    float size=1,phase=0,shade=0;
    std::uint32_t key=0;
};
std::vector<LeafCard> foliageCards(Tree& tree,const std::atomic_bool* stop)
{
    std::vector<LeafCard> cards;
    const Params& p=tree.params;
    if(p.family==Family::Dead || p.leafDensity<=0) return cards;
    bool visible=false;
    for(std::size_t i=3;i<tree.leafMask.rgba.size();i+=4) if(tree.leafMask.rgba[i]>=128) { visible=true; break; }
    if(!visible) return cards;
    std::vector<bool> terminal(tree.branches.size(),true);
    for(const Branch& b:tree.branches) if(b.parent>=0) terminal[std::size_t(b.parent)]=false;
    struct FoliageSite { std::size_t branch; float t,scale; };
    std::vector<FoliageSite> sites;
    const bool conifer=p.family==Family::Conifer;
    for(std::size_t i=0;i<tree.branches.size();++i)
    {
        const Branch& branch=tree.branches[i];
        if(!conifer)
        {
            if(terminal[i] || i==0) sites.push_back({i,i==0 ? 0.98f : 0.88f,1.0f});
            continue;
        }
        if(i==0)
        {
            // The leader closes the pointed crown without a spherical top tuft.
            sites.push_back({i,0.90f,0.66f});
            sites.push_back({i,0.95f,0.43f});
            sites.push_back({i,0.99f,0.22f});
        }
        else if(branch.level==1 || terminal[i])
        {
            // Conifers carry foliage along boughs, rather than only at twig tips.
            const float length=glm::length(branch.points.back().position-branch.points.front().position);
            const int count=std::clamp(int(length/(p.clumpSize*0.85f))+1,2,6);
            for(int j=0;j<count;++j)
            {
                const float t=glm::mix(branch.level==1 ? 0.16f : 0.36f,0.95f,float(j)/float(count-1));
                const float height=sat((sample(branch,t).position.y-p.height*p.branchStart)/(p.height*(1-p.branchStart)));
                const float scale=(branch.level==1 ? 1.15f : 0.72f)*glm::mix(1.0f,0.48f,height)*glm::mix(1.08f,0.76f,t);
                sites.push_back({i,t,scale});
            }
        }
    }
    // Share a saturated budget across the complete canopy, rather than filling
    // the earliest branches and silently dropping one side of the tree.
    const int cardsPerClump=int(MaxCards/(std::max)(std::size_t(1),sites.size()));
    Random rng(p.seed^0x67a9d3e1u);
    for(std::size_t i=0;i<sites.size();++i)
    {
        if(cancelled(stop)) return cards;
        const FoliageSite& site=sites[i];
        const Branch& branch=tree.branches[site.branch];
        const float clump=p.clumpSize*rng.range(0.76f,1.16f)*site.scale;
        const glm::vec3 axis=tangentAt(branch,conifer ? site.t : 0.9f);
        const glm::vec3 center=sample(branch,site.t).position+axis*(clump*0.22f);
        glm::vec3 radii(clump,clump*0.65f,clump*0.85f);
        if(p.family==Family::Weeping) radii={clump*0.64f,clump*1.10f,clump*0.64f};
        if(conifer) radii={clump,clump*(site.branch==0 ? 0.70f : 0.28f),clump*0.72f};
        // Horizontal branch-aligned sprays form readable tapered tiers. They
        // preserve a few gaps inside the crown instead of filling a solid cone.
        const glm::vec3 forward=safeNormal(glm::vec3(axis.x,0,axis.z),{1,0,0});
        const glm::vec3 side(-forward.z,0,forward.x);
        const int desiredCount=std::clamp(int((conifer ? 18.0f : 22.0f)*p.leafDensity*clump*clump/(p.leafSize*p.leafSize)),4,256);
        const int count=(std::min)(desiredCount,cardsPerClump);
        if(count<desiredCount) tree.statistics.budgetLimited=true;
        const float clumpPhase=rng.unit();
        for(int j=0;j<count;++j)
        {
            if(cards.size()>=MaxCards) { tree.statistics.budgetLimited=true; return cards; }
            // Stratified spherical shell plus radial jitter; avoids dense invisible interiors.
            const float y=1-2*(float(j)+rng.unit())/float(count);
            const float theta=float(j)*2.39996323f+rng.range(-0.38f,0.38f);
            const float r=std::sqrt((std::max)(0.0f,1-y*y));
            const glm::vec3 direction(r*std::cos(theta),y,r*std::sin(theta));
            glm::vec3 point=direction*radii*rng.range(0.55f,1.0f);
            glm::vec3 n=safeNormal(direction/radii);
            if(conifer)
            {
                point=forward*point.x+glm::vec3(0,point.y,0)+side*point.z;
                n=forward*n.x+glm::vec3(0,n.y,0)+side*n.z;
            }
            const glm::vec3 facing=safeNormal(n*0.72f+axis*0.28f+glm::vec3(rng.signedUnit(),rng.signedUnit(),rng.signedUnit())*0.30f,n);
            const glm::vec3 right=perpendicular(facing),up=glm::cross(facing,right);
            const float roll=rng.range(-Pi,Pi);
            LeafCard c; c.center=center+point;
            c.right=right*std::cos(roll)+up*std::sin(roll);
            c.up=-right*std::sin(roll)+up*std::cos(roll);
            c.volumeNormal=n; c.size=p.leafSize*rng.range(0.72f,1.18f);
            c.phase=clumpPhase; c.shade=rng.range(-0.06f,0.06f);
            c.key=hash(std::uint32_t(conifer ? i : site.branch)*1559u+std::uint32_t(j)*31337u+p.seed);
            cards.push_back(c);
        }
    }
    return cards;
}
void meshLeaves(Tree& tree,const std::vector<LeafCard>& cards,int lod)
{
    Mesh& mesh=tree.lods[std::size_t(lod)].foliage;
    const Params& p=tree.params;
    const float aspect=float(tree.leafMask.width)/float(tree.leafMask.height);
    constexpr glm::vec2 corners[4]={{-1,-1},{1,-1},{1,1},{-1,1}};
    constexpr glm::vec2 uv[4]={{0,1},{1,1},{1,0},{0,0}};
    for(const LeafCard& c:cards)
    {
        if(lod==1 && (c.key%3u)!=0) continue;
        const float size=c.size*(lod==0 ? 1.0f : 1.42f)*0.5f;
        const float halfWidth=size*(std::min)(1.0f,aspect);
        const float halfHeight=size/(std::max)(1.0f,aspect);
        const glm::vec3 faceN=safeNormal(glm::cross(c.right,c.up));
        const std::uint32_t base=std::uint32_t(mesh.vertices.size());
        for(int k=0;k<4;++k)
        {
            Vertex vert;
            vert.position=c.center+c.right*(corners[k].x*halfWidth)+c.up*(corners[k].y*halfHeight);
            const glm::vec3 outward=safeNormal(c.volumeNormal+(c.right*corners[k].x+c.up*corners[k].y)*0.18f,c.volumeNormal);
            vert.normal=safeNormal(glm::mix(faceN,outward,p.foliageNormalBlend),outward);
            vert.uv=uv[k];
            const float height=sat((vert.position.y-(p.height-p.crownHeight-p.clumpSize))/(p.crownHeight+2*p.clumpSize));
            const float weight=sat(0.22f+height*0.58f+c.volumeNormal.y*0.12f+c.shade);
            vert.color=glm::vec4(glm::mix(p.leafLow,p.leafHigh,weight),1);
            vert.wind={sat(vert.position.y/p.height),1.0f,c.phase,weight};
            mesh.vertices.push_back(vert);
        }
        mesh.indices.insert(mesh.indices.end(),{base,base+1,base+2,base,base+2,base+3});
    }
}
Image maskImage(const Image& input)
{
    Image result=input;
    bool alpha=false;
    for(std::size_t i=3;i<input.rgba.size();i+=4) if(input.rgba[i]!=255) { alpha=true; break; }
    for(std::size_t i=0;i<input.rgba.size();i+=4)
    {
        const std::uint8_t a=alpha ? input.rgba[i+3] : std::uint8_t((std::uint32_t(input.rgba[i])*54u+std::uint32_t(input.rgba[i+1])*183u+std::uint32_t(input.rgba[i+2])*19u)/256u);
        result.rgba[i]=result.rgba[i+1]=result.rgba[i+2]=255;
        result.rgba[i+3]=a;
    }
    return result;
}
}

const char* familyName(Family family)
{
    switch(family)
    {
        case Family::Conifer:return "Conifer";
        case Family::Weeping:return "Weeping";
        case Family::Dead:return "Dead";
        default:return "Broadleaf";
    }
}
Params preset(Family family)
{
    Params p; p.family=family;
    switch(family)
    {
        case Family::Conifer:
            p.height=23; p.trunkRadius=0.48f; p.trunkBend=0.035f;
            p.crownWidth=9; p.crownHeight=17; p.branchStart=0.20f;
            p.branchCount=9; p.branchAngle=78; p.branchingLevels=2;
            p.curvature=0.16f; p.clumpSize=1.20f; p.leafSize=0.65f;
            p.leafLow={0.018f,0.045f,0.028f}; p.leafHigh={0.10f,0.19f,0.07f}; break;
        case Family::Weeping:
            p.height=16; p.trunkRadius=0.75f; p.crownWidth=16; p.crownHeight=11;
            p.branchStart=0.35f; p.branchAngle=65; p.curvature=0.42f;
            p.clumpSize=1.4f; p.leafSize=0.72f;
            p.leafLow={0.035f,0.08f,0.02f}; p.leafHigh={0.22f,0.32f,0.07f}; break;
        case Family::Dead:
            p.height=15; p.trunkRadius=0.85f; p.trunkBend=0.17f; p.crownWidth=12;
            p.branchCount=7; p.curvature=0.5f; p.leafDensity=0;
            p.barkLow={0.07f,0.06f,0.045f}; p.barkHigh={0.23f,0.20f,0.14f}; break;
        default:p.family=Family::Broadleaf;break;
    }
    return p;
}
Params sanitize(Params p)
{
    const Params defaults;
    if(int(p.family)<0 || int(p.family)>3) p.family=Family::Broadleaf;
    p.height=bounded(p.height,2,100,defaults.height);
    p.trunkRadius=bounded(p.trunkRadius,0.04f,p.height*0.14f,defaults.trunkRadius);
    p.trunkBend=bounded(p.trunkBend,0,0.45f,defaults.trunkBend);
    p.crownWidth=bounded(p.crownWidth,1,p.height*2.0f,defaults.crownWidth);
    p.crownHeight=bounded(p.crownHeight,1,p.height*1.5f,defaults.crownHeight);
    p.branchStart=bounded(p.branchStart,0.12f,0.80f,defaults.branchStart);
    p.branchCount=std::clamp(p.branchCount,3,24);
    p.branchingLevels=std::clamp(p.branchingLevels,1,4);
    p.branchAngle=bounded(p.branchAngle,15,110,defaults.branchAngle);
    p.branchTaper=bounded(p.branchTaper,0.25f,0.76f,defaults.branchTaper);
    p.curvature=bounded(p.curvature,0,1,defaults.curvature);
    p.twigDensity=bounded(p.twigDensity,0.2f,2.5f,defaults.twigDensity);
    p.leafSize=bounded(p.leafSize,0.12f,5,defaults.leafSize);
    p.leafDensity=bounded(p.leafDensity,0,3,defaults.leafDensity);
    p.clumpSize=bounded(p.clumpSize,0.2f,8,defaults.clumpSize);
    p.foliageNormalBlend=bounded(p.foliageNormalBlend,0,1,defaults.foliageNormalBlend);
    p.barkLow=paletteColor(p.barkLow,defaults.barkLow); p.barkHigh=paletteColor(p.barkHigh,defaults.barkHigh);
    p.leafLow=paletteColor(p.leafLow,defaults.leafLow); p.leafHigh=paletteColor(p.leafHigh,defaults.leafHigh);
    return p;
}
bool validImage(const Image& image)
{
    return image.width>0 && image.height>0 && image.width<=2048 && image.height<=2048
        && image.rgba.size()==std::size_t(image.width)*std::size_t(image.height)*4;
}
Image makeLeafMask(int resolution)
{
    const int n=std::clamp(resolution,16,1024);
    Image image; image.width=n; image.height=n; image.rgba.resize(std::size_t(n)*n*4);
    struct Leaf { float x,y,rx,ry,angle; };
    const Leaf leaves[]={{0.50f,0.24f,0.105f,0.205f,0},{0.30f,0.40f,0.125f,0.195f,-0.78f},
                         {0.70f,0.40f,0.125f,0.195f,0.78f},{0.31f,0.64f,0.115f,0.175f,-1.02f},
                         {0.69f,0.64f,0.115f,0.175f,1.02f}};
    for(int y=0;y<n;++y) for(int x=0;x<n;++x)
    {
        float alpha=0;
        for(int sy=0;sy<2;++sy) for(int sx=0;sx<2;++sx)
        {
            const float u=(float(x)+(float(sx)+0.5f)*0.5f)/n,v=(float(y)+(float(sy)+0.5f)*0.5f)/n;
            float a=0;
            for(const Leaf& leaf:leaves)
            {
                const float dx=u-leaf.x,dy=v-leaf.y;
                const float cs=std::cos(leaf.angle),sn=std::sin(leaf.angle);
                const float lx=(dx*cs+dy*sn)/leaf.rx,ly=(-dx*sn+dy*cs)/leaf.ry;
                // Pointed oval with broad middle; readable at the cluster scale.
                const float shape=lx*lx+ly*ly;
                a=(std::max)(a,1-smooth(0.88f,1.04f,shape));
            }
            const float stem=(1-smooth(0.008f,0.018f,std::abs(u-0.5f)))*smooth(0.10f,0.16f,v)*(1-smooth(0.86f,0.91f,v));
            a=(std::max)(a,stem); alpha+=a*0.25f;
        }
        const std::size_t k=(std::size_t(y)*n+x)*4;
        // Canonical white RGB: maskImage(makeLeafMask()) is byte-identical.
        // Palette and per-card weights carry the artistic colour variation.
        image.rgba[k]=image.rgba[k+1]=image.rgba[k+2]=255;
        image.rgba[k+3]=std::uint8_t(alpha*255+0.5f);
    }
    return image;
}
Image makeBarkTexture(int resolution)
{
    const int n=std::clamp(resolution,16,1024);
    Image image; image.width=n; image.height=n; image.rgba.resize(std::size_t(n)*n*4);
    for(int y=0;y<n;++y) for(int x=0;x<n;++x)
    {
        const float u=float(x)/n,v=float(y)/n;
        // Periodic blend avoids seams when this tiny mask repeats along branches.
        auto periodic=[&](float px,float py) {
            const float a=valueNoise(px*9,py*2),b=valueNoise((px-1)*9,py*2);
            const float c=valueNoise(px*9,(py-1)*2),d=valueNoise((px-1)*9,(py-1)*2);
            return glm::mix(glm::mix(a,b,smooth(0,1,px)),glm::mix(c,d,smooth(0,1,px)),smooth(0,1,py));
        };
        const float noise=periodic(u,v);
        const float bands=0.68f+0.16f*smooth(0.35f,0.39f,noise)+0.16f*smooth(0.61f,0.65f,noise);
        const std::size_t k=(std::size_t(y)*n+x)*4;
        image.rgba[k]=image.rgba[k+1]=image.rgba[k+2]=std::uint8_t(bands*255+0.5f); image.rgba[k+3]=255;
    }
    return image;
}
std::vector<Image> coverageMipmaps(const Image& image,float cutoff)
{
    if(!validImage(image)) return {};
    cutoff=bounded(cutoff,0.01f,0.99f,0.5f);
    const int threshold=int(std::ceil(cutoff*255));
    std::size_t covered=0;
    for(std::size_t i=3;i<image.rgba.size();i+=4) if(image.rgba[i]>=threshold) ++covered;
    const double coverage=double(covered)/double(image.width*image.height);
    std::vector<Image> mips{image};
    while(mips.back().width>1 || mips.back().height>1)
    {
        const Image& src=mips.back(); Image dst;
        dst.width=(std::max)(1,src.width/2); dst.height=(std::max)(1,src.height/2);
        dst.rgba.resize(std::size_t(dst.width)*dst.height*4);
        for(int y=0;y<dst.height;++y) for(int x=0;x<dst.width;++x)
        {
            // Area bins include odd trailing rows/columns instead of silently dropping them.
            const int x0=x*src.width/dst.width,x1=(x+1)*src.width/dst.width;
            const int y0=y*src.height/dst.height,y1=(y+1)*src.height/dst.height;
            float a=0; glm::vec3 premul(0); int count=0;
            for(int sy=y0;sy<y1;++sy) for(int sx=x0;sx<x1;++sx)
            {
                const std::size_t i=(std::size_t(sy)*src.width+sx)*4;
                const float alpha=float(src.rgba[i+3]); a+=alpha;
                premul+=glm::vec3(src.rgba[i],src.rgba[i+1],src.rgba[i+2])*alpha; ++count;
            }
            const std::size_t i=(std::size_t(y)*dst.width+x)*4;
            for(int k=0;k<3;++k) dst.rgba[i+std::size_t(k)]=std::uint8_t(a>0 ? premul[k]/a+0.5f : 255);
            dst.rgba[i+3]=std::uint8_t(a/count+0.5f);
        }
        const int pixels=dst.width*dst.height;
        const int desired=int(std::round(coverage*pixels));
        auto passing=[&](float scale) { int n=0; for(std::size_t i=3;i<dst.rgba.size();i+=4) if(float(dst.rgba[i])*scale>=threshold) ++n; return n; };
        float lo=0,hi=256;
        for(int i=0;i<24;++i) { const float mid=(lo+hi)*0.5f; if(passing(mid)<desired) lo=mid; else hi=mid; }
        const float scale=std::abs(passing(lo)-desired)<std::abs(passing(hi)-desired) ? lo : hi;
        for(std::size_t i=3;i<dst.rgba.size();i+=4) dst.rgba[i]=std::uint8_t(std::clamp(std::floor(float(dst.rgba[i])*scale+0.0001f),0.0f,255.0f));
        mips.push_back(std::move(dst));
    }
    return mips;
}
Tree generate(const Params& params,const Image* leafMask,const std::atomic_bool* stop)
{
    const auto start=std::chrono::steady_clock::now();
    Tree tree; tree.params=sanitize(params);
    if(cancelled(stop)) { tree.cancelled=true; return tree; }
    if(leafMask && validImage(*leafMask)) tree.leafMask=maskImage(*leafMask);
    else { tree.leafMask=makeLeafMask(); if(leafMask) tree.warning="Invalid leaf mask; using the built-in cluster mask."; }
    tree.barkTexture=makeBarkTexture();
    tree.branches.reserve(MaxBranches);
    buildSkeleton(tree,stop);
    if(cancelled(stop)) { tree.cancelled=true; return tree; }
    for(std::size_t i=0;i<tree.branches.size();++i)
    {
        if(cancelled(stop)) { tree.cancelled=true; return tree; }
        meshBranch(tree,tree.branches[i],i,0); meshBranch(tree,tree.branches[i],i,1);
    }
    const auto cards=foliageCards(tree,stop);
    if(cancelled(stop)) { tree.cancelled=true; return tree; }
    meshLeaves(tree,cards,0); meshLeaves(tree,cards,1);
    tree.boundsMin=glm::vec3((std::numeric_limits<float>::max)());
    tree.boundsMax=glm::vec3(-(std::numeric_limits<float>::max)());
    for(const auto& lod:tree.lods) for(const Mesh* m:{&lod.wood,&lod.foliage}) for(const Vertex& v:m->vertices)
    { tree.boundsMin=glm::min(tree.boundsMin,v.position); tree.boundsMax=glm::max(tree.boundsMax,v.position); }
    tree.statistics.branches=std::uint32_t(tree.branches.size()); tree.statistics.leafCards=std::uint32_t(cards.size());
    for(std::size_t i=0;i<2;++i) tree.statistics.triangles[i]=std::uint32_t((tree.lods[i].wood.indices.size()+tree.lods[i].foliage.indices.size())/3);
    tree.statistics.generationMilliseconds=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
    if(tree.statistics.budgetLimited) tree.warning="Generation reached the safety budget (384 branches / 8192 foliage cards). Reduce branching or foliage density.";
    return tree;
}
}
