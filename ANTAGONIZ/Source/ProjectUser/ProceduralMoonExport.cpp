#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "ProceduralMoonExport.hpp"
#include "hv/cpputil/json.hpp"
// Private writer: unrelated engine image saves cannot alter its global state.
#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb-cmake/stb_image_write.h"
#undef STB_IMAGE_WRITE_IMPLEMENTATION
#undef STB_IMAGE_WRITE_STATIC
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>
#if defined(_WIN32)
#include <windows.h>
#endif

namespace EnduranceMoon
{
namespace
{
using Json=nlohmann::json;
using Bytes=std::vector<std::uint8_t>;
namespace fs=std::filesystem;
bool finite(float x)
{ std::uint32_t bits=0; std::memcpy(&bits,&x,4); return (bits&0x7f800000u)!=0x7f800000u; }
void require(bool condition,const char* message)
{ if(!condition) throw std::runtime_error(message); }
void append32(Bytes& bytes,std::uint32_t value)
{ for(unsigned i=0;i<4;++i) bytes.push_back(static_cast<std::uint8_t>(value>>(8*i))); }
void appendFloat(Bytes& bytes,float value)
{ std::uint32_t bits=0; std::memcpy(&bits,&value,4); append32(bytes,bits); }
void align4(Bytes& bytes)
{ while(bytes.size()%4) bytes.push_back(0); }
std::string base64(const Bytes& bytes)
{
    constexpr char table[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string result; result.reserve((bytes.size()+2)/3*4);
    for(std::size_t i=0;i<bytes.size();i+=3)
    {
        const unsigned n=(unsigned(bytes[i])<<16) | (i+1<bytes.size()?unsigned(bytes[i+1])<<8:0) |
            (i+2<bytes.size()?unsigned(bytes[i+2]):0);
        result.push_back(table[(n>>18)&63]); result.push_back(table[(n>>12)&63]);
        result.push_back(i+1<bytes.size()?table[(n>>6)&63]:'=');
        result.push_back(i+2<bytes.size()?table[n&63]:'=');
    }
    return result;
}
struct PngOutput { Bytes bytes; bool failed=false; };
void pngWrite(void* context,void* data,int length)
{
    auto& result=*static_cast<PngOutput*>(context);
    if(result.failed || length<=0) return;
    try { const auto* first=static_cast<std::uint8_t*>(data); result.bytes.insert(result.bytes.end(),first,first+length); }
    catch(...) { result.failed=true; }
}
Bytes albedoPng(const Surface& surface)
{
    std::array<std::uint8_t,256> srgb{};
    for(unsigned i=0;i<256;++i)
    {
        const float linear=float(i)/255.f;
        const float encoded=linear<=.0031308f?12.92f*linear:1.055f*std::pow(linear,1.f/2.4f)-.055f;
        srgb[i]=static_cast<std::uint8_t>(std::clamp(encoded,0.f,1.f)*255.f+.5f);
    }
    // glTF's v=0 addresses the first image row, which is also the atlas south
    // pole. Keep row order; flipping it would invert the baked material.
    Bytes pixels(std::size_t(surface.width)*surface.height*3);
    for(std::size_t i=0;i<pixels.size()/3;++i)
        pixels[i*3]=pixels[i*3+1]=pixels[i*3+2]=srgb[surface.pixels[i*4+3]];
    PngOutput result;
    const int success=stbi_write_png_to_func(pngWrite,&result,surface.width,surface.height,3,
        pixels.data(),surface.width*3);
    require(success!=0 && !result.failed && !result.bytes.empty(),"Cannot encode lunar albedo PNG.");
    return std::move(result.bytes);
}
Json recipe(const GeometryParams* geometry,const Params* surface)
{
    Json result={{"generator","ENDURANCE volumetric moon"},{"generatorVersion",1},
        {"coordinateUnits","Intact Moon radius = 1; apply physical scale at placement"}};
    if(geometry)
    {
        const auto p=sanitizeGeometry(*geometry);
        result["geometry"]={{"seed",p.seed},{"resolution",p.resolution},{"excavationStrength",p.excavationStrength},
            {"cavityCount",p.cavityCount},{"cavitySize",p.cavitySize},{"cavityDepth",p.cavityDepth},
            {"excavationYaw",p.excavationYaw},{"excavationPitch",p.excavationPitch},
            {"erosionStrength",p.erosionStrength},{"erosionScale",p.erosionScale},{"erosionOctaves",p.erosionOctaves},
            {"debrisCount",p.debrisCount},{"debrisSpread",p.debrisSpread},{"debrisSize",p.debrisSize}};
    }
    if(surface)
    {
        const auto p=sanitize(*surface);
        result["surface"]={{"seed",p.seed},{"width",p.width},{"macroScale",p.macroScale},{"detailScale",p.detailScale},
            {"octaves",p.octaves},{"fractureCount",p.fractureCount},{"fractureWidth",p.fractureWidth},
            {"fractureDepth",p.fractureDepth},{"fractureStrength",p.fractureStrength}};
    }
    return result;
}
void publish(const fs::path& destination,const Bytes& bytes)
{
    require(!destination.filename().empty(),"A model filename is required.");
    const fs::path parent=destination.has_parent_path()?destination.parent_path():fs::path(".");
    fs::create_directories(parent);
    static std::atomic<std::uint64_t> sequence{0};
    const auto clock=std::chrono::steady_clock::now().time_since_epoch().count();
    fs::path temporaryDirectory;
    for(int attempt=0;attempt<64;++attempt)
    {
        const auto candidate=parent/(".moon-model-"+std::to_string(clock)+"-"+
            std::to_string(sequence.fetch_add(1,std::memory_order_relaxed)));
        std::error_code error;
        if(fs::create_directory(candidate,error)) { temporaryDirectory=candidate; break; }
        if(error && error!=std::errc::file_exists) throw std::runtime_error(error.message());
    }
    require(!temporaryDirectory.empty(),"Cannot reserve temporary model output.");
    struct Cleanup { fs::path directory; ~Cleanup() { std::error_code ignored; fs::remove_all(directory,ignored); } } cleanup{temporaryDirectory};
    const auto temporary=temporaryDirectory/"complete.model";
    std::ofstream file(temporary,std::ios::binary|std::ios::trunc);
    require(bool(file),"Cannot open temporary model output.");
    file.write(reinterpret_cast<const char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));
    file.flush(); const bool written=file.good(); file.close();
    require(written && !file.fail(),"Cannot write complete model output.");
#if defined(_WIN32)
    if(!MoveFileExW(temporary.c_str(),destination.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))
        throw std::runtime_error("Cannot replace model: "+std::system_category().message(static_cast<int>(GetLastError())));
#else
    fs::rename(temporary,destination);
#endif
}
}

bool saveModel(const Geometry& geometry,const Surface& surface,const std::string& path,
    std::string& error,const GeometryParams* geometryRecipe,const Params* materialRecipe)
{
    error.clear();
    try
    {
        const auto destination=fs::u8path(path);
        const auto extension=destination.extension().string();
        require(extension==".glb" || extension==".gltf","Use a .glb or .gltf filename.");
        require(!geometry.vertices.empty() && geometry.vertices.size()<=4000000u &&
            !geometry.indices.empty() && geometry.indices.size()<=24000000u && geometry.indices.size()%3==0,
            "Invalid or oversized lunar mesh.");
        require(surface.width>=64 && surface.width<=4096 && surface.height==surface.width/2 &&
            surface.pixels.size()==std::size_t(surface.width)*surface.height*4,"Invalid lunar material atlas.");
        std::size_t covered=0;
        require(!geometry.parts.empty(),"The lunar model has no parts.");
        for(const auto& part:geometry.parts)
        {
            require(part.firstIndex==covered && part.indexCount>0 && part.indexCount%3==0 &&
                std::size_t(part.firstIndex)+part.indexCount<=geometry.indices.size(),"Invalid lunar part range.");
            covered+=part.indexCount;
        }
        require(covered==geometry.indices.size(),"Lunar parts do not cover the mesh.");
        Bytes binary; binary.reserve(geometry.vertices.size()*48+geometry.indices.size()*4);
        struct PackedPart
        {
            std::size_t vertexOffset=0,vertexCount=0;
            std::array<float,3> low{{(std::numeric_limits<float>::max)(),(std::numeric_limits<float>::max)(),(std::numeric_limits<float>::max)()}};
            std::array<float,3> high{{-low[0],-low[1],-low[2]}};
        };
        std::vector<PackedPart> packedParts;
        std::vector<std::uint32_t> indices; indices.reserve(geometry.indices.size());
        constexpr auto unset=(std::numeric_limits<std::uint32_t>::max)();
        std::vector<std::uint32_t> remap(geometry.vertices.size(),unset),touched;
        // Compact each fragment independently. Referencing the whole Moon's
        // accessors from every node makes ordinary importers duplicate the
        // body's entire vertex buffer for every small detached rock.
        for(const auto& part:geometry.parts)
        {
            PackedPart packed; packed.vertexOffset=binary.size();
            for(std::size_t i=part.firstIndex;i<std::size_t(part.firstIndex)+part.indexCount;++i)
            {
                const auto source=geometry.indices[i];
                require(source<geometry.vertices.size(),"Lunar triangle index outside vertex array.");
                if(remap[source]==unset)
                {
                    remap[source]=static_cast<std::uint32_t>(packed.vertexCount++); touched.push_back(source);
                    const auto& vertex=geometry.vertices[source];
                    const float values[]{vertex.position.x,vertex.position.y,vertex.position.z,
                        vertex.normal.x,vertex.normal.y,vertex.normal.z,vertex.u,vertex.v,
                        vertex.color[0],vertex.color[1],vertex.color[2],vertex.color[3]};
                    for(float value:values) { require(finite(value),"Nonfinite lunar vertex attribute."); appendFloat(binary,value); }
                    for(unsigned c=0;c<3;++c)
                    { packed.low[c]=(std::min)(packed.low[c],values[c]); packed.high[c]=(std::max)(packed.high[c],values[c]); }
                    for(float value:vertex.color) require(value>=0.f && value<=1.f,"Lunar colors must be linear values in [0,1].");
                    const float n2=values[3]*values[3]+values[4]*values[4]+values[5]*values[5];
                    require(n2>.98f && n2<1.02f,"Lunar normals must be normalized.");
                }
                indices.push_back(remap[source]);
            }
            for(auto source:touched) remap[source]=unset;
            touched.clear(); packedParts.push_back(packed);
        }
        const std::size_t vertexBytes=binary.size();
        for(auto index:indices) append32(binary,index);
        const std::size_t indexBytes=binary.size()-vertexBytes;
        const auto png=albedoPng(surface);
        const std::size_t imageOffset=binary.size(); binary.insert(binary.end(),png.begin(),png.end()); align4(binary);
        Json output;
        output["asset"]={{"version","2.0"},{"generator","ENDURANCE Moon Laboratory"}};
        output["scene"]=0;
        output["scenes"]=Json::array({{{"name","Excavated Moon"},{"nodes",Json::array()}}});
        output["bufferViews"]=Json::array({
            {{"buffer",0},{"byteOffset",0},{"byteLength",vertexBytes},{"byteStride",48},{"target",34962}},
            {{"buffer",0},{"byteOffset",vertexBytes},{"byteLength",indexBytes},{"target",34963}},
            {{"buffer",0},{"byteOffset",imageOffset},{"byteLength",png.size()}}});
        output["accessors"]=Json::array();
        output["materials"]=Json::array({{{"name","Lunar rock"},{"pbrMetallicRoughness",{
            {"baseColorTexture",{{"index",0}}},{"metallicFactor",0},{"roughnessFactor",.93}}},
            {"alphaMode","OPAQUE"},{"doubleSided",false}}});
        output["images"]=Json::array({{{"name","Unlit lunar albedo"},{"bufferView",2},{"mimeType","image/png"}}});
        output["textures"]=Json::array({{{"sampler",0},{"source",0}}});
        output["samplers"]=Json::array({{{"magFilter",9729},{"minFilter",9987},{"wrapS",10497},{"wrapT",33071}}});
        output["nodes"]=Json::array(); output["meshes"]=Json::array();
        for(std::size_t i=0;i<geometry.parts.size();++i)
        {
            const auto& part=geometry.parts[i]; const auto& packed=packedParts[i];
            const std::size_t accessor=output["accessors"].size();
            output["accessors"].push_back({{"bufferView",0},{"byteOffset",packed.vertexOffset},{"componentType",5126},
                {"count",packed.vertexCount},{"type","VEC3"},{"min",packed.low},{"max",packed.high}});
            output["accessors"].push_back({{"bufferView",0},{"byteOffset",packed.vertexOffset+12},{"componentType",5126},
                {"count",packed.vertexCount},{"type","VEC3"}});
            output["accessors"].push_back({{"bufferView",0},{"byteOffset",packed.vertexOffset+24},{"componentType",5126},
                {"count",packed.vertexCount},{"type","VEC2"}});
            output["accessors"].push_back({{"bufferView",0},{"byteOffset",packed.vertexOffset+32},{"componentType",5126},
                {"count",packed.vertexCount},{"type","VEC4"}});
            output["accessors"].push_back({{"bufferView",1},{"byteOffset",std::size_t(part.firstIndex)*4},
                {"componentType",5125},{"count",part.indexCount},{"type","SCALAR"}});
            const std::string name=part.debris?"Fragment_"+std::to_string(i):"Moon_body";
            output["meshes"].push_back({{"name",name},{"primitives",Json::array({{
                {"attributes",{{"POSITION",accessor},{"NORMAL",accessor+1},{"TEXCOORD_0",accessor+2},{"COLOR_0",accessor+3}}},
                {"indices",accessor+4},{"material",0},{"mode",4}}})}});
            output["nodes"].push_back({{"name",name},{"mesh",i},{"extras",{{"detached",part.debris}}}});
            output["scenes"][0]["nodes"].push_back(i);
        }
        output["extras"]=recipe(geometryRecipe,materialRecipe);
        output["buffers"]=Json::array({{{"byteLength",binary.size()}}});
        Bytes file;
        if(extension==".gltf")
        {
            output["buffers"][0]["uri"]="data:application/octet-stream;base64,"+base64(binary);
            const auto text=output.dump(); file.assign(text.begin(),text.end());
        }
        else
        {
            std::string json=output.dump(); while(json.size()%4) json.push_back(' ');
            const std::size_t length=12+8+json.size()+8+binary.size();
            require(length<(std::numeric_limits<std::uint32_t>::max)(),"GLB exceeds the format size limit.");
            file.reserve(length); append32(file,0x46546c67); append32(file,2); append32(file,static_cast<std::uint32_t>(length));
            append32(file,static_cast<std::uint32_t>(json.size())); append32(file,0x4e4f534a);
            file.insert(file.end(),json.begin(),json.end()); append32(file,static_cast<std::uint32_t>(binary.size()));
            append32(file,0x004e4942); file.insert(file.end(),binary.begin(),binary.end());
        }
        publish(destination,file); return true;
    }
    catch(const std::exception& exception) { error="Cannot export lunar model: "+std::string(exception.what()); return false; }
}
}
