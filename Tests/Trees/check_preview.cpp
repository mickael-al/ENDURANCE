#include "TreeLabPreview.hpp"
#include "glcore.hpp"
#include <algorithm>
#include <array>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace EnduranceTrees;
namespace {
void require(bool b,const std::string& text) { if(!b) throw std::runtime_error(text); }
void cleanErrors(const char* where) {
    GLenum error=glGetError(); require(error==GL_NO_ERROR,std::string(where)+": GL error "+std::to_string(error));
}
struct State {
    std::vector<GLint> values;
    std::vector<GLfloat> floats;
    State() {
        for(auto key:{GL_CURRENT_PROGRAM,GL_VERTEX_ARRAY_BINDING,GL_ARRAY_BUFFER_BINDING,
            GL_DRAW_FRAMEBUFFER_BINDING,GL_READ_FRAMEBUFFER_BINDING,GL_ACTIVE_TEXTURE,
            GL_DEPTH_FUNC,GL_CULL_FACE_MODE,GL_FRONT_FACE,GL_BLEND_SRC_RGB,GL_BLEND_DST_RGB,
            GL_BLEND_SRC_ALPHA,GL_BLEND_DST_ALPHA,GL_BLEND_EQUATION_RGB,GL_BLEND_EQUATION_ALPHA,
            GL_PIXEL_PACK_BUFFER_BINDING,GL_PIXEL_UNPACK_BUFFER_BINDING,
            GL_PACK_ALIGNMENT,GL_PACK_ROW_LENGTH,GL_PACK_SKIP_ROWS,GL_PACK_SKIP_PIXELS,
            GL_UNPACK_ALIGNMENT,GL_UNPACK_ROW_LENGTH,GL_UNPACK_SKIP_ROWS,GL_UNPACK_SKIP_PIXELS}) {
            GLint value=0; glGetIntegerv(key,&value); values.push_back(value);
        }
        for(auto key:{GL_DEPTH_TEST,GL_BLEND,GL_CULL_FACE,GL_SCISSOR_TEST,GL_FRAMEBUFFER_SRGB}) values.push_back(glIsEnabled(key));
        for(auto key:{GL_VIEWPORT,GL_SCISSOR_BOX}) {
            GLint value[4]; glGetIntegerv(key,value); values.insert(values.end(),value,value+4);
        }
        GLint polygon[2]; glGetIntegerv(GL_POLYGON_MODE,polygon); values.insert(values.end(),polygon,polygon+2);
        GLboolean mask[4]; glGetBooleanv(GL_COLOR_WRITEMASK,mask); for(auto x:mask) values.push_back(x);
        glGetBooleani_v(GL_COLOR_WRITEMASK,1,mask); for(auto x:mask) values.push_back(x);
        values.push_back(glIsEnabledi(GL_BLEND,1));
        glGetBooleanv(GL_DEPTH_WRITEMASK,mask); values.push_back(mask[0]);
        GLint active=0; glGetIntegerv(GL_ACTIVE_TEXTURE,&active);
        for(int unit:{0,1,3}) {
            glActiveTexture(GL_TEXTURE0+unit); GLint texture=0,sampler=0;
            glGetIntegerv(GL_TEXTURE_BINDING_2D,&texture); glGetIntegeri_v(GL_SAMPLER_BINDING,unit,&sampler);
            values.push_back(texture); values.push_back(sampler);
        }
        glActiveTexture(active);
        for(auto key:{GL_COLOR_CLEAR_VALUE,GL_BLEND_COLOR}) {
            GLfloat v[4]; glGetFloatv(key,v); floats.insert(floats.end(),v,v+4);
        }
    }
    void check(const char* label) const {
        State after;
        if(values!=after.values) {
            for(std::size_t i=0;i<values.size();++i) if(values[i]!=after.values[i])
                std::cerr<<label<<" state slot "<<i<<": "<<values[i]<<" -> "<<after.values[i]<<'\n';
            throw std::runtime_error(std::string(label)+" leaked OpenGL state");
        }
        require(floats==after.floats,std::string(label)+" changed clear/blend color");
        cleanErrors(label);
    }
};
struct ForeignState {
    GLuint arrays=0,buffers[3]{},framebuffers[2]{},textures[3]{},samplers[3]{},program=0;
    ForeignState() {
        glCreateVertexArrays(1,&arrays); glBindVertexArray(arrays);
        glCreateBuffers(3,buffers); for(auto b:buffers) glNamedBufferData(b,65536,nullptr,GL_DYNAMIC_COPY);
        glBindBuffer(GL_ARRAY_BUFFER,buffers[0]); glBindBuffer(GL_PIXEL_PACK_BUFFER,buffers[1]); glBindBuffer(GL_PIXEL_UNPACK_BUFFER,buffers[2]);
        glCreateFramebuffers(2,framebuffers); glBindFramebuffer(GL_DRAW_FRAMEBUFFER,framebuffers[0]); glBindFramebuffer(GL_READ_FRAMEBUFFER,framebuffers[1]);
        glCreateTextures(GL_TEXTURE_2D,3,textures); glCreateSamplers(3,samplers);
        for(int i=0;i<3;++i) {
            glTextureStorage2D(textures[i],1,GL_RGBA8,4,4); const int unit=i==2?3:i;
            glBindTextureUnit(unit,textures[i]); glSamplerParameteri(samplers[i],GL_TEXTURE_MIN_FILTER,GL_NEAREST);
            glSamplerParameteri(samplers[i],GL_TEXTURE_MAG_FILTER,GL_NEAREST); glBindSampler(unit,samplers[i]);
        }
        const char* vs="#version 450\nvoid main(){gl_Position=vec4(0,0,0,1);}";
        const char* fs="#version 450\nlayout(location=0)out vec4 c;void main(){c=vec4(1);}";
        GLuint vertex=glCreateShader(GL_VERTEX_SHADER),fragment=glCreateShader(GL_FRAGMENT_SHADER);
        glShaderSource(vertex,1,&vs,nullptr); glCompileShader(vertex); glShaderSource(fragment,1,&fs,nullptr); glCompileShader(fragment);
        program=glCreateProgram(); glAttachShader(program,vertex); glAttachShader(program,fragment); glLinkProgram(program);
        glDeleteShader(vertex); glDeleteShader(fragment); glUseProgram(program);
        glActiveTexture(GL_TEXTURE3); glViewport(11,17,123,91); glScissor(3,7,55,44);
        glEnable(GL_BLEND); glEnable(GL_SCISSOR_TEST); glEnable(GL_FRAMEBUFFER_SRGB); glEnable(GL_CULL_FACE); glDisable(GL_DEPTH_TEST);
        glDepthFunc(GL_GREATER); glDepthMask(GL_FALSE); glColorMask(GL_FALSE,GL_TRUE,GL_FALSE,GL_TRUE);
        glCullFace(GL_FRONT); glFrontFace(GL_CW); glPolygonMode(GL_FRONT_AND_BACK,GL_LINE);
        glBlendFuncSeparate(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA,GL_ONE,GL_ZERO);
        glBlendEquationSeparate(GL_FUNC_SUBTRACT,GL_FUNC_REVERSE_SUBTRACT); glClearColor(.11f,.22f,.33f,.44f); glBlendColor(.4f,.3f,.2f,.1f);
        glPixelStorei(GL_PACK_ALIGNMENT,8); glPixelStorei(GL_PACK_ROW_LENGTH,37); glPixelStorei(GL_PACK_SKIP_ROWS,2); glPixelStorei(GL_PACK_SKIP_PIXELS,3);
        glPixelStorei(GL_UNPACK_ALIGNMENT,8); glPixelStorei(GL_UNPACK_ROW_LENGTH,29); glPixelStorei(GL_UNPACK_SKIP_ROWS,1); glPixelStorei(GL_UNPACK_SKIP_PIXELS,4);
        cleanErrors("foreign state setup");
    }
    ~ForeignState() {
        glUseProgram(0); glDeleteProgram(program); glBindFramebuffer(GL_FRAMEBUFFER,0); glDeleteFramebuffers(2,framebuffers);
        glBindVertexArray(0); glDeleteVertexArrays(1,&arrays); glDeleteBuffers(3,buffers); glDeleteTextures(3,textures); glDeleteSamplers(3,samplers);
    }
};
std::vector<unsigned char> pixels(const TreeLabPreview& preview) {
    GLint pack=0,alignment=0,row=0,skipRows=0,skipPixels=0;
    glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING,&pack); glGetIntegerv(GL_PACK_ALIGNMENT,&alignment);
    glGetIntegerv(GL_PACK_ROW_LENGTH,&row); glGetIntegerv(GL_PACK_SKIP_ROWS,&skipRows); glGetIntegerv(GL_PACK_SKIP_PIXELS,&skipPixels);
    glBindBuffer(GL_PIXEL_PACK_BUFFER,0); glPixelStorei(GL_PACK_ALIGNMENT,1); glPixelStorei(GL_PACK_ROW_LENGTH,0); glPixelStorei(GL_PACK_SKIP_ROWS,0); glPixelStorei(GL_PACK_SKIP_PIXELS,0);
    std::vector<unsigned char> data(preview.width()*preview.height()*4);
    glGetTextureImage(preview.texture(),0,GL_RGBA,GL_UNSIGNED_BYTE,static_cast<GLsizei>(data.size()),data.data());
    glBindBuffer(GL_PIXEL_PACK_BUFFER,pack); glPixelStorei(GL_PACK_ALIGNMENT,alignment); glPixelStorei(GL_PACK_ROW_LENGTH,row); glPixelStorei(GL_PACK_SKIP_ROWS,skipRows); glPixelStorei(GL_PACK_SKIP_PIXELS,skipPixels);
    cleanErrors("preview readback"); return data;
}
std::size_t difference(const std::vector<unsigned char>& a,const std::vector<unsigned char>& b) {
    require(a.size()==b.size(),"Readback size mismatch"); std::size_t count=0;
    for(std::size_t i=0;i<a.size();i+=4) if(std::abs(int(a[i])-int(b[i]))+std::abs(int(a[i+1])-int(b[i+1]))+std::abs(int(a[i+2])-int(b[i+2]))>4) ++count;
    return count;
}
void writeCapture(const TreeLabPreview& preview,const std::filesystem::path& path) {
    const auto data=pixels(preview);
    std::ofstream output(path,std::ios::binary);
    output<<"P6\n"<<preview.width()<<' '<<preview.height()<<"\n255\n";
    for(int y=preview.height()-1;y>=0;--y) for(int x=0;x<preview.width();++x)
        output.write(reinterpret_cast<const char*>(data.data()+(y*preview.width()+x)*4),3);
    require(output.good(),"Could not write preview capture");
}
}

extern "C" int tree_preview_tests() {
 try {
    glewExperimental=GL_TRUE;
    const auto status=glewInit(); require(status==GLEW_OK || status==GLEW_ERROR_NO_GLX_DISPLAY,"GLEW initialization failed");
    require(GLEW_VERSION_4_5,"OpenGL 4.5 required"); while(glGetError()!=GL_NO_ERROR) {}
    ForeignState foreign; State original;
    TreeLabPreview preview;
    require(preview.initialize(),preview.error()); original.check("initialize");
    auto p=preset(Family::Broadleaf); p.branchCount=5; p.branchingLevels=2; p.leafDensity=.6f;
    const auto tree=generate(p);
    require(preview.upload(tree),preview.error()); original.check("upload");
    PreviewSettings s; s.width=224; s.height=192; s.showGround=false;
    s.target=(tree.boundsMin+tree.boundsMax)*.5f; s.distance=glm::length(tree.boundsMax-tree.boundsMin)*1.1f;
    s.showWood=s.showLeaves=false; require(preview.render(s),preview.error()); original.check("empty render");
    const auto empty=pixels(preview);
    s.showWood=s.showLeaves=true; require(preview.render(s),preview.error()); original.check("tree render");
    const auto complete=pixels(preview); require(difference(empty,complete)>100,"Tree not visible in real GL preview");
    s.showLeaves=false; require(preview.render(s),preview.error()); const auto wood=pixels(preview);
    require(difference(wood,complete)>30,"Foliage draw has no visible contribution");
    s.showLeaves=true; s.wireframe=true; require(preview.render(s),preview.error());
    require(difference(pixels(preview),complete)>30,"Wireframe control has no effect"); original.check("wireframe render");
    s.wireframe=false; s.lod=1; require(preview.render(s),preview.error());
    require(difference(pixels(preview),empty)>100,"LOD1 disappeared"); original.check("LOD render");
    // The distant level must really use a two-triangle billboard, and must not
    // recapture the high-poly tree on every frame once its GPU cache is warm.
    s.lod=3; require(preview.render(s),preview.error()); original.check("LOD3 lazy cache");
    require(difference(pixels(preview),empty)>100,"LOD3 is invisible");
    GLuint primitivesQuery=0; glGenQueries(1,&primitivesQuery);
    glBeginQuery(GL_PRIMITIVES_GENERATED,primitivesQuery);
    require(preview.render(s),preview.error());
    glEndQuery(GL_PRIMITIVES_GENERATED);
    GLuint primitiveCount=0; glGetQueryObjectuiv(primitivesQuery,GL_QUERY_RESULT,&primitiveCount);
    glDeleteQueries(1,&primitivesQuery);
    require(primitiveCount==2,"Warm LOD3 renders more than a single quad"); original.check("LOD3 warm render");
    std::vector<ImpostorView> pair;
    require(preview.captureLod3(pair,64),preview.error()); original.check("LOD3 two-view capture");
    require(pair.size()==2,"LOD3 must contain exactly two views");
    require(std::abs(pair[0].direction.y)<.001f && std::abs(pair[1].direction.y-std::sqrt(.5f))<.001f,"LOD3 capture elevations are not 0 and 45 degrees");
    require(pair[0].centre==pair[1].centre && pair[0].halfExtent==pair[1].halfExtent,"LOD3 views use incompatible frames");
    for(const auto& v:pair) {
        require(validImage(v.albedo) && v.normal.rgba.empty(),"LOD3 needs extra normal textures or has invalid color");
        require(v.albedo.width==64 && v.albedo.height==64,"LOD3 capture resolution ignored");
        std::size_t coverage=0;
        for(std::size_t i=3;i<v.albedo.rgba.size();i+=4) coverage+=v.albedo.rgba[i]>=128;
        require(coverage>10 && coverage<64*64*9/10,"LOD3 lacks a useful cutout silhouette");
    }
    require(pair[0].albedo.rgba!=pair[1].albedo.rgba,"LOD3 elevations produced the same image");
    // Wrong shader path must not destroy a working program or tree. Changing
    // the working directory is handled by the Python driver, so this fixture
    // concentrates on the stable render/capture lifecycle.
    require(preview.reloadShaders(),preview.error()); original.check("shader reload");
    require(preview.render(s),preview.error());
    std::vector<ImpostorView> views;
    require(preview.captureImpostors(views,64,8,true),preview.error()); original.check("impostor capture");
    require(views.size()==17,"Missing multi-view impostor angles");
    for(const auto& v:views) {
        require(validImage(v.albedo) && validImage(v.normal),"Capture image invalid");
        require(v.albedo.width==64 && v.albedo.height==64,"Capture extent ignored");
        require(v.albedo.rgba.size()==v.normal.rgba.size(),"Normal/albedo image mismatch");
        require(glm::length(glm::cross(v.direction,v.up))>.1f,"Capture camera up is parallel to direction");
        std::size_t visible=0;
        for(std::size_t i=0;i<v.albedo.rgba.size();i+=4) {
            require(v.albedo.rgba[i+3]==v.normal.rgba[i+3],"Capture normal opacity differs from albedo");
            if(v.albedo.rgba[i+3]) {
                ++visible; glm::vec3 n;
                for(int k=0;k<3;++k) n[k]=float(v.normal.rgba[i+k])/127.5f-1.f;
                require(std::abs(glm::length(n)-1.f)<.025f,"Invalid encoded object-space normal");
            }
        }
        require(visible>10 && visible<64*64*9/10,"Impostor is empty or lacks a transparent background");
    }
    // Captured material attributes must remain independent of preview lighting.
    s.sunYaw=-130; s.sunElevation=8; s.time=5; s.windStrength=.2f;
    require(preview.render(s),preview.error());
    std::vector<ImpostorView> relit;
    require(preview.captureImpostors(relit,64,8,true),preview.error()); original.check("relit impostor capture");
    for(std::size_t i=0;i<views.size();++i)
        require(views[i].albedo.rgba==relit[i].albedo.rgba && views[i].normal.rgba==relit[i].normal.rgba,"Impostors contain preview sun/wind instead of static material attributes");
    std::vector<ImpostorView> pairAgain;
    require(preview.captureLod3(pairAgain,64),preview.error()); original.check("LOD3 stable neutral lighting");
    for(std::size_t i=0;i<2;++i)
        require(pairAgain[i].albedo.rgba==pair[i].albedo.rgba,"LOD3 bake changes with the preview sun/wind");
    // A loaded pair must be consumed as-is, not silently overwritten by a new
    // tree capture. Distinct tinted test images make accidental recapture visible.
    s.sunYaw=40; s.sunElevation=45; s.time=s.windStrength=0;
    require(preview.render(s),preview.error()); const auto automaticLod3=pixels(preview);
    std::vector<ImpostorView> savedPair;
    require(preview.captureLod3(savedPair,256),preview.error());
    require(preview.upload(tree,&savedPair) && preview.render(s),preview.error());
    const auto reloadedLod3=pixels(preview);
    std::uint64_t colorError=0; std::size_t intersection=0,unionCount=0;
    for(std::size_t i=0;i<automaticLod3.size();i+=4) {
        int aContrast=0,bContrast=0;
        for(int c=0;c<3;++c) {
            colorError+=std::abs(int(automaticLod3[i+c])-int(reloadedLod3[i+c]));
            aContrast+=std::abs(int(automaticLod3[i+c])-int(empty[i+c]));
            bContrast+=std::abs(int(reloadedLod3[i+c])-int(empty[i+c]));
        }
        const bool a=aContrast>20,b=bContrast>20;
        intersection+=a&&b; unionCount+=a||b;
    }
    require(unionCount>100 && double(intersection)/unionCount>.85,"Reloaded LOD3 changed silhouette/orientation");
    require(double(colorError)/(automaticLod3.size()/4*3)<6,"Reloaded LOD3 colors diverge from generated cache");
    auto supplied=pair;
    for(auto& v:supplied) for(std::size_t i=0;i<v.albedo.rgba.size();i+=4) {
        v.albedo.rgba[i]=255; v.albedo.rgba[i+1]=0; v.albedo.rgba[i+2]=255;
    }
    require(preview.upload(tree,&supplied),preview.error()); original.check("LOD3 loaded texture upload");
    require(preview.render(s),preview.error());
    require(difference(automaticLod3,pixels(preview))>100,"Loaded LOD3 textures were ignored or regenerated");
    if(const char* folder=std::getenv("ENDURANCE_TREE_TEST_CAPTURE_DIR")) {
        for(int familyIndex=0;familyIndex<FamilyCount;++familyIndex) {
            const auto family=static_cast<Family>(familyIndex);
            const auto variant=generate(preset(family)); require(preview.upload(variant),preview.error());
            PreviewSettings capture; capture.width=capture.height=640; capture.showGround=true;
            capture.target=(variant.boundsMin+variant.boundsMax)*.5f;
            capture.distance=glm::length(variant.boundsMax-variant.boundsMin)*1.05f;
            require(preview.render(capture),preview.error());
            writeCapture(preview,std::filesystem::path(folder)/(std::string(familyName(family))+"_foliage.ppm"));
            capture.showLeaves=false; require(preview.render(capture),preview.error());
            writeCapture(preview,std::filesystem::path(folder)/(std::string(familyName(family))+"_wood.ppm"));
            capture.showLeaves=true; capture.showGround=false; capture.lod=3;
            for(int pitch:{0,22,45}) {
                capture.pitch=float(pitch); require(preview.render(capture),preview.error());
                writeCapture(preview,std::filesystem::path(folder)/(std::string(familyName(family))+"_lod3_"+std::to_string(pitch)+".ppm"));
            }
        }
    }
    preview.release(); original.check("release");
    require(!preview.texture(),"Release retained a preview target");
    std::cout<<"Real OpenGL tree preview, masking contribution, LOD, wireframe and foreign-state preservation: PASS\n";
    return 0;
 } catch(const std::exception& e) { std::cerr<<"Tree preview failure: "<<e.what()<<'\n'; return 1; }
}
