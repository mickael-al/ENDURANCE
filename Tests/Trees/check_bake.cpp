#include "ProceduralTreeBake.hpp"
#include "stb-cmake/stb_image.h"
#include "stb-cmake/stb_image_write.h"
#include "hv/cpputil/json.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace EnduranceTrees;
namespace {
void require(bool b,const std::string& message) { if(!b) throw std::runtime_error(message); }
void equalMesh(const Mesh& a,const Mesh& b) {
    require(a.indices==b.indices,"Baked indices changed");
    require(a.vertices.size()==b.vertices.size(),"Baked vertex count changed");
    for(std::size_t i=0;i<a.vertices.size();++i) {
        const auto& x=a.vertices[i]; const auto& y=b.vertices[i];
        require(x.position==y.position && x.normal==y.normal && x.uv==y.uv && x.color==y.color && x.wind==y.wind,"Baked vertex attributes changed");
    }
}
void equalImage(const Image& a,const Image& b) {
    require(a.width==b.width && a.height==b.height && a.rgba.size()==b.rgba.size(),"Baked image extent changed");
    for(std::size_t i=0;i<a.rgba.size();++i)
        require(std::abs(int(a.rgba[i])-int(b.rgba[i]))<=(i%4==3?0:2),"Baked texture changed beyond byte color-space rounding");
}
std::string contents(const std::filesystem::path& p) {
    std::ifstream stream(p,std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(stream),{});
}
void writeJson(const std::filesystem::path& path,const nlohmann::json& value) {
    std::ofstream file(path); file<<value.dump(2); require(file.good(),"JSON fixture write failed");
}
}
int main(int argc,char** argv) {
 try {
    require(argc==2,"Provide a disposable output directory");
    const std::filesystem::path root=argv[1]; std::filesystem::create_directories(root);
    Params p=preset(Family::Weeping); p.seed=94732; p.height=14.25f; p.branchCount=5;
    p.branchingLevels=2; p.leafDensity=0.53f; p.barkHigh=glm::vec3(0.33f,0.16f,0.09f);
    p.rootCount=5; p.rootLength=4.25f; p.rootRadius=.35f;
    p.vineCount=7; p.vineLength=6.75f; p.vineRadius=.065f;
    const auto canonical=paramsToJson(p);
    std::string error; Params parsed;
    require(paramsFromJson(canonical,parsed,error),"Preset JSON roundtrip failed: "+error);
    require(paramsToJson(parsed)==canonical,"Preset JSON lost parameters");
    require(savePreset(root/"preset.json",p,error),"Preset save failed: "+error);
    require(loadPreset(root/"preset.json",parsed,error),"Preset load failed: "+error);
    require(paramsToJson(parsed)==canonical,"Preset file lost parameters");
    require(!paramsFromJson("{not valid json",parsed,error),"Malformed preset JSON accepted");
    require(paramsToJson(parsed)==canonical,"Rejected preset damaged active settings");
    require(!loadPreset(root/"missing.json",parsed,error),"Missing preset accepted");
    require(paramsToJson(parsed)==canonical,"Missing preset damaged active settings");
    // Global stb orientation must not flip the private mask import. RGBA is
    // retained unchanged so generation can choose alpha or opaque luminance.
    Image fixture; fixture.width=7; fixture.height=11; fixture.rgba.resize(7*11*4);
    for(int y=0;y<11;++y) for(int x=0;x<7;++x) {
        const auto i=(y*7+x)*4;
        fixture.rgba[i]=std::uint8_t(x*37); fixture.rgba[i+1]=std::uint8_t(y*23);
        fixture.rgba[i+2]=std::uint8_t((x+y)*11); fixture.rgba[i+3]=std::uint8_t((x+2*y)*9);
    }
    require(stbi_write_png((root/"source-mask.png").string().c_str(),7,11,4,fixture.rgba.data(),7*4)!=0,"Fixture PNG failed");
    stbi_set_flip_vertically_on_load(true);
    Image imported;
    require(loadLeafMask(root/"source-mask.png",imported,error),"Mask load failed: "+error);
    require(imported.width==7 && imported.height==11 && imported.rgba==fixture.rgba,"Mask import inherited global flip or altered RGBA");
    std::vector<std::uint8_t> hugeMask(2049*4,255);
    require(stbi_write_png((root/"oversized.png").string().c_str(),2049,1,4,hugeMask.data(),2049*4)!=0,"Oversized fixture failed");
    require(!loadLeafMask(root/"oversized.png",imported,error) && imported.rgba==fixture.rgba,"Oversized mask accepted or damaged prior mask");
    require(!loadLeafMask(root/"missing.png",imported,error) && imported.rgba==fixture.rgba,"Missing mask damaged prior mask");
    const auto tree=generate(p);
    require(!tree.cancelled,"Unexpected cancelled generation");
    const auto destination=root/"asset";
    require(bake(tree,destination,error),"Asset bake failed: "+error);
    require(std::filesystem::is_regular_file(destination/"manifest.json"),"Bake manifest missing");
    require(std::filesystem::is_regular_file(destination/"tree.bin"),"Bake binary payload missing");
    const auto manifest=contents(destination/"manifest.json"),bin=contents(destination/"tree.bin");
    require(!bake(tree,destination,error),"Existing export silently overwritten");
    require(contents(destination/"manifest.json")==manifest && contents(destination/"tree.bin")==bin,"Failed export altered existing asset");
    Tree loaded; require(loadBakedTree(destination,loaded,error),"Asset reload failed: "+error);
    require(paramsToJson(loaded.params)==canonical,"Baked asset lost generation recipe");
    require(loaded.boundsMin==tree.boundsMin && loaded.boundsMax==tree.boundsMax,"Baked bounds changed");
    for(int i=0;i<2;++i) { equalMesh(tree.lods[i].wood,loaded.lods[i].wood); equalMesh(tree.lods[i].foliage,loaded.lods[i].foliage); }
    require(tree.branches.size()==loaded.branches.size(),"Baked skeleton lost branches");
    for(std::size_t i=0;i<tree.branches.size();++i) {
        const auto& a=tree.branches[i]; const auto& b=loaded.branches[i];
        require(a.parent==b.parent && a.level==b.level && a.kind==b.kind && a.attachment==b.attachment && a.points.size()==b.points.size(),"Baked skeleton hierarchy changed");
        for(std::size_t j=0;j<a.points.size();++j) require(a.points[j].position==b.points[j].position && a.points[j].radius==b.points[j].radius,"Baked branch sample changed");
    }
    equalImage(tree.leafMask,loaded.leafMask); equalImage(tree.barkTexture,loaded.barkTexture);
    require(loaded.statistics.roots==tree.statistics.roots && loaded.statistics.vines==tree.statistics.vines,"Bake lost root/vine counts");
    // V1 recipes and caches remain readable: no new roots/vines are injected.
    auto legacyParams=p; legacyParams.rootCount=legacyParams.vineCount=0;
    const auto legacyTree=generate(legacyParams);
    require(bake(legacyTree,root/"legacy",error),"Legacy fixture bake failed: "+error);
    auto oldPreset=nlohmann::json::parse(paramsToJson(legacyParams));
    oldPreset["generatorVersion"]=1; oldPreset["version"]=1;
    auto oldManifest=nlohmann::json::parse(contents(root/"legacy/manifest.json"));
    oldManifest["generatorVersion"]=1; oldManifest["version"]=1; oldManifest.erase("lod3");
    for(const char* field:{"rootCount","rootLength","rootRadius","vineCount","vineLength","vineRadius"}) {
        oldPreset["params"].erase(field); oldManifest["params"].erase(field);
    }
    for(auto& branch:oldManifest["branches"]) branch.erase("kind");
    oldManifest["statistics"].erase("roots"); oldManifest["statistics"].erase("vines");
    auto oldGltf=nlohmann::json::parse(contents(root/"legacy/tree.gltf")); oldGltf["extras"]["version"]=1;
    writeJson(root/"legacy/manifest.json",oldManifest); writeJson(root/"legacy/tree.gltf",oldGltf);
    require(paramsFromJson(oldPreset.dump(),parsed,error),"V1 recipe no longer loads: "+error);
    require(parsed.rootCount==0 && parsed.vineCount==0,"V1 recipe acquired optional structures");
    std::vector<ImpostorView> legacyViews(2);
    require(loadBakedTree(root/"legacy",loaded,error,&legacyViews),"V1 baked model no longer loads: "+error);
    require(legacyViews.empty() && loaded.statistics.roots==0 && loaded.statistics.vines==0,"V1 cache acquired new data");
    equalMesh(legacyTree.lods[0].wood,loaded.lods[0].wood);
    // Impostor albedo is already sRGB and normals are linear. Publication must
    // preserve their bytes without another gamma conversion.
    ImpostorView view; view.albedo=fixture; view.normal=fixture;
    for(std::size_t i=0;i<view.normal.rgba.size();i+=4) {
        view.normal.rgba[i]=128; view.normal.rgba[i+1]=255; view.normal.rgba[i+2]=128;
    }
    view.centre=(tree.boundsMin+tree.boundsMax)*.5f; view.halfExtent=20;
    std::vector<ImpostorView> views{view};
    require(bake(tree,root/"impostor",error,&views),"Impostor bake failed: "+error);
    require(loadLeafMask(root/"impostor/impostor_0_albedo.png",imported,error) && imported.rgba==fixture.rgba,"Impostor albedo was gamma converted twice or flipped");
    require(loadLeafMask(root/"impostor/impostor_0_normal.png",imported,error) && imported.rgba==view.normal.rgba,"Impostor normal was gamma converted or flipped");
    views[0].up=views[0].direction;
    require(!bake(tree,root/"bad-impostor",error,&views),"Degenerate impostor basis accepted");
    require(!std::filesystem::exists(root/"bad-impostor"),"Failed impostor bake published partial output");
    // The lightweight LOD3 uses precisely two color textures, with byte-exact
    // sRGB roundtrip and a shared frame. No normal-map dependency is introduced.
    Image lodColor; lodColor.width=lodColor.height=32; lodColor.rgba.resize(32*32*4);
    for(int y=0;y<32;++y) for(int x=0;x<32;++x) {
        const auto i=(y*32+x)*4;
        lodColor.rgba[i]=std::uint8_t(x*7); lodColor.rgba[i+1]=std::uint8_t(y*7);
        lodColor.rgba[i+2]=71; lodColor.rgba[i+3]=x>6 && x<25 && y>3 && y<28 ? 255 : 0;
    }
    std::vector<ImpostorView> lod3(2);
    for(auto& v:lod3) { v.albedo=lodColor; v.centre=view.centre; v.halfExtent=20; }
    lod3[1].direction=glm::normalize(glm::vec3(0,1,1));
    lod3[1].up=glm::normalize(glm::vec3(0,1,-1));
    require(bake(tree,root/"lod3",error,nullptr,&lod3),"LOD3 bake failed: "+error);
    require(loadLeafMask(root/"lod3/lod3_0.png",imported,error) && imported.rgba==lodColor.rgba,"LOD3 horizontal PNG changed colors or alpha");
    require(loadLeafMask(root/"lod3/lod3_45.png",imported,error) && imported.rgba==lodColor.rgba,"LOD3 elevated PNG changed colors or alpha");
    std::vector<ImpostorView> restoredViews;
    require(loadBakedTree(root/"lod3",loaded,error,&restoredViews),"LOD3 reload failed: "+error);
    require(restoredViews.size()==2,"LOD3 reload lost one of the angles");
    for(std::size_t i=0;i<2;++i) {
        require(restoredViews[i].albedo.rgba==lod3[i].albedo.rgba && restoredViews[i].normal.rgba.empty(),"LOD3 image payload changed");
        require(restoredViews[i].direction==lod3[i].direction && restoredViews[i].centre==lod3[i].centre && restoredViews[i].halfExtent==lod3[i].halfExtent,"LOD3 framing changed");
    }
    lod3.pop_back(); require(!bake(tree,root/"incomplete-lod3",error,nullptr,&lod3),"One-view LOD3 accepted");
    require(!std::filesystem::exists(root/"incomplete-lod3"),"Invalid LOD3 published partial output");
    auto cancelled=tree; cancelled.cancelled=true;
    require(!bake(cancelled,root/"cancelled",error),"Cancelled tree published");
    require(!std::filesystem::exists(root/"cancelled"),"Cancelled tree left partial output");
    std::ofstream(root/"blocked")<<"file";
    require(!bake(tree,root/"blocked/asset",error),"Bake succeeded through file-as-directory");
    // A copied asset is damaged deliberately; originals remain available for
    // the independent Python glTF/buffer/PNG inspector after this process.
    const auto corrupt=root/"corrupt";
    std::filesystem::copy(destination,corrupt,std::filesystem::copy_options::recursive);
    std::filesystem::resize_file(corrupt/"tree.bin",17);
    require(!loadBakedTree(corrupt,loaded,error),"Truncated mesh buffer accepted");
    std::cout<<"Preset and baked tree roundtrip, refusal of replacement, IO failures and truncated buffer: PASS\n";
    return 0;
 } catch(const std::exception& e) { std::cerr<<"Tree bake test failure: "<<e.what()<<'\n'; return 1; }
}
