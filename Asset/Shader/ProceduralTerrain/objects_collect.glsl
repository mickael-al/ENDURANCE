#include "objects_common.glsl"
uniform ivec3 uOrigin;
uniform uint uLod, uFieldMode;
uniform float uBaseSpacing, uSeaLevel;
uniform int uBiomeOverride;

vec3 objectColumnClimate(PackedFieldColumn column)
{
    uint cell=(column.detail.y>>24u)&63u;
    // Same coastal mixture as fieldSurfaceWeight, from the existing packed
    // metadata. Plane/Sphere fixtures do not have a marine classifier.
    float land=uFieldMode==0u && cell<16u?column.altitudeRatio:1.0;
    return vec3(column.humidity,column.temperature,land);
}
vec3 objectClimateAt(vec2 point)
{
    uint side=uCells+3u;
    vec2 q=clamp(point+vec2(1),vec2(0),vec2(side-1u));
    uvec2 a=uvec2(floor(q)),b=min(a+uvec2(1),uvec2(side-1u));
    vec2 f=fract(q);
    return mix(mix(objectColumnClimate(columns[a.x+a.y*side]),objectColumnClimate(columns[b.x+a.y*side]),f.x),
        mix(objectColumnClimate(columns[a.x+b.y*side]),objectColumnClimate(columns[b.x+b.y*side]),f.x),f.y);
}

// One jittered world-space site per placement cell. The cell is at least as
// large as a voxel at the maximum collecting LOD, so a voxel tests <=1 site.
bool objectCandidate(ivec3 cell,out vec2 point,out uint record,out uint profile,out uint model)
{
    if(uObjectJobBase==0u) return false;
    int voxelStep=int(1u<<uLod), siteStep=int(objectWords[2]);
    ivec3 lattice=uOrigin+cell*voxelStep;
    ivec3 sector=ivec3(objectWords[uObjectJobBase],objectWords[uObjectJobBase+1u],objectWords[uObjectJobBase+2u]);
    uvec3 small=uvec3((lattice-sector)/int(objectWords[5]));
    uint bit=small.x+objectWords[6]*(small.y+objectWords[6]*small.z);
    if((objectWords[uObjectJobBase+4u+bit/32u]&(1u<<(bit%32u)))==0u) return false;
    ivec2 site=ivec2(objectFloorDiv(lattice.x,siteStep),objectFloorDiv(lattice.z,siteStep));
    uint seed=objectWords[uObjectJobBase+3u], h=objectSiteHash(site,seed);
    vec2 jitter=objectJitter(h);
    vec2 inVoxel=vec2(site*siteStep-lattice.xz)+jitter*float(siteStep);
    if(any(lessThan(inVoxel,vec2(0))) || any(greaterThanEqual(inVoxel,vec2(voxelStep)))) return false;
    point=vec2(cell.xz)+inVoxel/float(voxelStep);
    uint biome=uint(uBiomeOverride);
    if(uBiomeOverride<0)
    {
        vec3 climate=objectClimateAt(point);
        vec2 grid=clamp(climate.xy,0.0,1.0)*4.0;
        ivec2 base=min(ivec2(floor(grid)),ivec2(3));
        vec2 f=grid-vec2(base); f=f*f*f*(f*(f*6.0-15.0)+10.0);
        ivec2 selected=base+ivec2(objectRandom(h^0xad185a31u)<f.x?1:0,objectRandom(h^0x53ba9381u)<f.y?1:0);
        biome=objectRandom(h^0x93c81d7bu)<climate.z
            ?objectWords[objectWords[14]+uint(selected.x+5*selected.y)]:25u;
    }
    profile=objectWords[12]+biome*8u;
    float density=uintBitsToFloat(objectWords[profile+2u]);
    if(density<=0.0 || objectRandom(h^0x315ef032u)>=density) return false;
    vec2 world=(vec2(site)+jitter)*float(siteStep)*uBaseSpacing;
    float period=uintBitsToFloat(objectWords[profile+4u]);
    float threshold=uintBitsToFloat(objectWords[profile+5u]);
    float softness=uintBitsToFloat(objectWords[profile+6u]);
    if(objectRandom(h^0x35ad3191u)>=objectForestMask(world,period,threshold,softness,seed)) return false;
    // Fixed priority exclusion, including across unloaded neighbouring sectors.
    // Rejecting against unplantable neighbours is conservative but avoids any
    // neighbour terrain/climate query. No iterative Poisson solver is needed.
    float minimumRatio=uintBitsToFloat(objectWords[9]);
    uint priority=objectHash(h^0x0fe93721u);
    for(int z=-1;z<=1;++z) for(int x=-1;x<=1;++x)
    {
        if(x==0 && z==0) continue;
        ivec2 delta=ivec2(x,z); uint other=objectSiteHash(site+delta,seed);
        uint rank=objectHash(other^0x0fe93721u);
        bool precedes=rank<priority || (rank==priority && (z<0 || (z==0 && x<0)));
        vec2 d=vec2(delta)+objectJitter(other)-jitter;
        if(precedes && dot(d,d)<minimumRatio*minimumRatio) return false;
    }
    uint ids=objectWords[profile], weights=objectWords[profile+1u], totalWeight=0u;
    for(uint i=0u;i<4u;++i) if(((ids>>(8u*i))&255u)!=255u) totalWeight+=(weights>>(8u*i))&255u;
    if(totalWeight==0u) return false;
    uint pick=objectHash(h^0x8dab4153u)%totalWeight, group=255u;
    for(uint i=0u;i<4u;++i)
    {
        uint id=(ids>>(8u*i))&255u, w=id==255u?0u:((weights>>(8u*i))&255u);
        if(pick<w) { group=id; break; } pick-=w;
    }
    if(group>=objectWords[1]) return false;
    uint groupBase=objectWords[13]+group*2u;
    uint variants=objectWords[groupBase+1u];
    if(variants==0u) return false;
    model=objectWords[groupBase]+objectHash(h^0x914e6d31u)%variants;
    ivec2 local=site-sector.xz/siteStep;
    record=uint(local.x)+objectWords[3]*uint(local.y)+objectWords[3]*objectWords[3]*small.y;
    return true;
}

vec3 objectTriangleVertex(uint caseIndex,uint vertex,ivec3 cell,float values[8])
{
    uint word=tables[caseIndex*REGULAR_TABLE_STRIDE+1u+vertex/4u];
    uint edge=(word>>((vertex%4u)*8u))&255u, a=(edge>>4u)&15u,b=edge&15u;
    return interpolateEdge(vec3(cell+corner(a)),vec3(cell+corner(b)),edgeFraction(values[a],values[b]));
}

void collectCellObjects(ivec3 cell,uint caseIndex,uint count,float values[8])
{
    // Positive density is solid: negative dDensity/dY identifies floor cells.
    float dy=values[2]+values[3]+values[6]+values[7]-values[0]-values[1]-values[4]-values[5];
    if(dy>=0.0) return;
    vec2 point; uint record,profile,model;
    if(!objectCandidate(cell,point,record,profile,model)) return;
    float normalY=uintBitsToFloat(objectWords[profile+3u]);
    for(uint i=0u;i<count;i+=3u)
    {
        vec3 a=objectTriangleVertex(caseIndex,i,cell,values);
        vec3 b=objectTriangleVertex(caseIndex,i+1u,cell,values);
        vec3 c=objectTriangleVertex(caseIndex,i+2u,cell,values);
        vec3 ab=b-a,ac=c-a,n=cross(ab,ac);
        float length2=dot(n,n);
        if(length2<1e-16 || n.y*n.y<normalY*normalY*length2) continue;
        vec2 q=point-a.xz;
        float determinant=ab.x*ac.z-ab.z*ac.x;
        if(abs(determinant)<1e-12) continue;
        float u=(q.x*ac.z-q.y*ac.x)/determinant;
        float v=(ab.x*q.y-ab.z*q.x)/determinant;
        if(u<0.0 || v<0.0 || u+v>1.0) continue;
        float y=a.y+u*ab.y+v*ac.y;
        // Keep exact top-face crossings: with density>0 inside, a horizontal
        // zero at an integer height is emitted by the voxel BELOW that face.
        // Duplicate triangle-edge hits share a record and atomicMax removes them.
        float worldY=(float(uOrigin.y)+y*float(1u<<uLod))*uBaseSpacing;
        if(worldY<uSeaLevel+uintBitsToFloat(objectWords[profile+7u])) continue;
        float sectorY=float(uOrigin.y-int(objectWords[uObjectJobBase+1u]))+y*float(1u<<uLod);
        uint quantized=uint(clamp(floor(sectorY/float(objectWords[4])*65535.0+0.5),0.0,65535.0));
        uint hit=((quantized+1u)<<8u)|model;
        atomicMax(objectWords[uObjectJobBase+8u+record],hit);
    }
}
