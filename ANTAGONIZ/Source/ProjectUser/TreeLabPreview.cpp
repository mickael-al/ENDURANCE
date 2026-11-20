#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "TreeLabPreview.hpp"
#include "ShaderCompilation.hpp"
#include "glcore.hpp"
#include "glm/gtc/matrix_transform.hpp"
#include "glm/gtc/type_ptr.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <limits>
#include <utility>

namespace EnduranceTrees
{
namespace
{
bool finite(float value)
{
    std::uint32_t bits=0;
    std::memcpy(&bits,&value,sizeof(bits));
    return (bits&0x7f800000u)!=0x7f800000u;
}
float safe(float value,float fallback,float low,float high)
{ return std::clamp(finite(value)?value:fallback,low,high); }
glm::vec3 safeVector(glm::vec3 value,glm::vec3 fallback,float low,float high)
{
    for(int i=0;i<3;++i) value[i]=safe(value[i],fallback[i],low,high);
    return value;
}
glm::vec3 direction(float yaw,float pitch)
{
    const float y=glm::radians(yaw),p=glm::radians(pitch);
    return {std::cos(p)*std::sin(y),std::sin(p),std::cos(p)*std::cos(y)};
}
float srgb(float value)
{ return value<=0.0031308f?value*12.92f:1.055f*std::pow(value,1.0f/2.4f)-0.055f; }
float linear(float value)
{ return value<=0.04045f?value/12.92f:std::pow((value+0.055f)/1.055f,2.4f); }
std::uint8_t byte(float value)
{ return static_cast<std::uint8_t>(std::clamp(value,0.0f,1.0f)*255.0f+0.5f); }

// Preview runs inside HUD rendering; never assume that its GL state is default.
// DSA keeps object edits from changing VAO, buffer and texture bindings.
class State final
{
public:
    State()
    {
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&drawFbo);
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&readFbo);
        glGetIntegerv(GL_CURRENT_PROGRAM,&program);
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING,&vao);
        glGetIntegerv(GL_VIEWPORT,viewport.data());
        glGetIntegerv(GL_ACTIVE_TEXTURE,&activeTexture);
        glActiveTexture(GL_TEXTURE0);
        glGetIntegerv(GL_TEXTURE_BINDING_2D,&texture);
        glGetIntegeri_v(GL_SAMPLER_BINDING,0,&sampler);
        glGetIntegerv(GL_DEPTH_FUNC,&depthFunc);
        glGetBooleanv(GL_DEPTH_WRITEMASK,&depthMask);
        glGetBooleani_v(GL_COLOR_WRITEMASK,0,colorMask.data());
        glGetIntegerv(GL_POLYGON_MODE,polygonMode.data());
        glGetDoublev(GL_DEPTH_RANGE,depthRange.data());
        glGetIntegerv(GL_CLIP_ORIGIN,&clipOrigin);
        glGetIntegerv(GL_CLIP_DEPTH_MODE,&clipDepth);
        for(std::size_t i=0;i<capabilities.size();++i) enabled[i]=glIsEnabled(capabilities[i]);
        blend=glIsEnabledi(GL_BLEND,0);
        for(unsigned i=0;i<clipEnabled.size();++i) clipEnabled[i]=glIsEnabled(GL_CLIP_DISTANCE0+i);
        glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING,&packBuffer);
        glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING,&unpackBuffer);
        for(std::size_t i=0;i<pixelNames.size();++i) glGetIntegerv(pixelNames[i],&pixelValues[i]);
    }
    ~State()
    {
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER,static_cast<GLuint>(drawFbo));
        glBindFramebuffer(GL_READ_FRAMEBUFFER,static_cast<GLuint>(readFbo));
        glUseProgram(static_cast<GLuint>(program));
        glBindVertexArray(static_cast<GLuint>(vao));
        glViewport(viewport[0],viewport[1],viewport[2],viewport[3]);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D,static_cast<GLuint>(texture));
        glBindSampler(0,static_cast<GLuint>(sampler));
        glActiveTexture(static_cast<GLenum>(activeTexture));
        glDepthFunc(static_cast<GLenum>(depthFunc));
        glDepthMask(depthMask);
        glColorMaski(0,colorMask[0],colorMask[1],colorMask[2],colorMask[3]);
        glPolygonMode(GL_FRONT_AND_BACK,static_cast<GLenum>(polygonMode[0]));
        glDepthRange(depthRange[0],depthRange[1]);
        glClipControl(static_cast<GLenum>(clipOrigin),static_cast<GLenum>(clipDepth));
        for(std::size_t i=0;i<capabilities.size();++i)
            if(enabled[i]) glEnable(capabilities[i]); else glDisable(capabilities[i]);
        if(blend) glEnablei(GL_BLEND,0); else glDisablei(GL_BLEND,0);
        for(unsigned i=0;i<clipEnabled.size();++i)
            if(clipEnabled[i]) glEnable(GL_CLIP_DISTANCE0+i); else glDisable(GL_CLIP_DISTANCE0+i);
        glBindBuffer(GL_PIXEL_PACK_BUFFER,static_cast<GLuint>(packBuffer));
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER,static_cast<GLuint>(unpackBuffer));
        for(std::size_t i=0;i<pixelNames.size();++i) glPixelStorei(pixelNames[i],pixelValues[i]);
    }
    void prepareTransfer()
    {
        glBindBuffer(GL_PIXEL_PACK_BUFFER,0);
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER,0);
        for(GLenum name:pixelNames)
            glPixelStorei(name,(name==GL_PACK_ALIGNMENT||name==GL_UNPACK_ALIGNMENT)?1:0);
    }
    void prepareDraw()
    {
        glDisablei(GL_BLEND,0);
        for(GLenum cap:capabilities) glDisable(cap);
        for(unsigned i=0;i<clipEnabled.size();++i) glDisable(GL_CLIP_DISTANCE0+i);
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);
        glDepthMask(GL_TRUE);
        glColorMaski(0,GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
        glDepthRange(0.0,1.0);
        glClipControl(GL_LOWER_LEFT,GL_NEGATIVE_ONE_TO_ONE);
        glBindSampler(0,0);
        glPolygonMode(GL_FRONT_AND_BACK,GL_FILL);
    }
private:
    GLint drawFbo=0,readFbo=0,program=0,vao=0,activeTexture=0,texture=0,sampler=0;
    GLint depthFunc=0,clipOrigin=0,clipDepth=0,packBuffer=0,unpackBuffer=0;
    std::array<GLint,4> viewport{};
    std::array<GLint,2> polygonMode{};
    std::array<GLdouble,2> depthRange{};
    GLboolean depthMask=GL_TRUE,blend=GL_FALSE;
    std::array<GLboolean,4> colorMask{};
    const std::array<GLenum,15> capabilities{{GL_DEPTH_TEST,GL_CULL_FACE,GL_SCISSOR_TEST,
        GL_STENCIL_TEST,GL_RASTERIZER_DISCARD,GL_FRAMEBUFFER_SRGB,GL_POLYGON_OFFSET_FILL,
        GL_POLYGON_OFFSET_LINE,GL_POLYGON_OFFSET_POINT,GL_SAMPLE_ALPHA_TO_COVERAGE,
        GL_SAMPLE_COVERAGE,GL_COLOR_LOGIC_OP,GL_SAMPLE_MASK,GL_SAMPLE_ALPHA_TO_ONE,GL_DITHER}};
    std::array<GLboolean,15> enabled{};
    std::array<GLboolean,8> clipEnabled{};
    const std::array<GLenum,12> pixelNames{{GL_PACK_ALIGNMENT,GL_PACK_ROW_LENGTH,
        GL_PACK_SKIP_ROWS,GL_PACK_SKIP_PIXELS,GL_PACK_IMAGE_HEIGHT,GL_PACK_SKIP_IMAGES,
        GL_UNPACK_ALIGNMENT,GL_UNPACK_ROW_LENGTH,GL_UNPACK_SKIP_ROWS,GL_UNPACK_SKIP_PIXELS,
        GL_UNPACK_IMAGE_HEIGHT,GL_UNPACK_SKIP_IMAGES}};
    std::array<GLint,12> pixelValues{};
};

struct GpuMesh
{
    GLuint vao=0,vbo=0,ibo=0;
    GLsizei count=0;
    void clear()
    {
        if(vao) glDeleteVertexArrays(1,&vao);
        if(vbo) glDeleteBuffers(1,&vbo);
        if(ibo) glDeleteBuffers(1,&ibo);
        vao=vbo=ibo=0; count=0;
    }
    bool upload(const Mesh& mesh)
    {
        if(mesh.indices.empty()) return true;
        if(mesh.vertices.empty() || mesh.indices.size()>static_cast<std::size_t>((std::numeric_limits<GLsizei>::max)())) return false;
        for(std::uint32_t index:mesh.indices) if(index>=mesh.vertices.size()) return false;
        glCreateVertexArrays(1,&vao);
        glCreateBuffers(1,&vbo);
        glCreateBuffers(1,&ibo);
        if(!vao||!vbo||!ibo) return false;
        const auto vb=static_cast<GLsizeiptr>(mesh.vertices.size()*sizeof(Vertex));
        const auto ib=static_cast<GLsizeiptr>(mesh.indices.size()*sizeof(std::uint32_t));
        glNamedBufferData(vbo,vb,mesh.vertices.data(),GL_STATIC_DRAW);
        glNamedBufferData(ibo,ib,mesh.indices.data(),GL_STATIC_DRAW);
        GLint64 size=0;
        glGetNamedBufferParameteri64v(vbo,GL_BUFFER_SIZE,&size); if(size!=vb) return false;
        glGetNamedBufferParameteri64v(ibo,GL_BUFFER_SIZE,&size); if(size!=ib) return false;
        glVertexArrayVertexBuffer(vao,0,vbo,0,sizeof(Vertex));
        glVertexArrayElementBuffer(vao,ibo);
        const std::array<GLuint,5> offsets{{offsetof(Vertex,position),offsetof(Vertex,normal),
            offsetof(Vertex,uv),offsetof(Vertex,color),offsetof(Vertex,wind)}};
        const std::array<GLint,5> components{{3,3,2,4,4}};
        for(GLuint i=0;i<5;++i)
        {
            glEnableVertexArrayAttrib(vao,i);
            glVertexArrayAttribFormat(vao,i,components[i],GL_FLOAT,GL_FALSE,offsets[i]);
            glVertexArrayAttribBinding(vao,i,0);
        }
        count=static_cast<GLsizei>(mesh.indices.size());
        return true;
    }
    void draw() const
    {
        if(!count) return;
        glBindVertexArray(vao);
        glDrawElements(GL_TRIANGLES,count,GL_UNSIGNED_INT,nullptr);
    }
};

struct Target
{
    GLuint fbo=0,color=0,normal=0,depth=0;
    int width=0,height=0;
    void clear()
    {
        if(fbo) glDeleteFramebuffers(1,&fbo);
        for(GLuint id:{color,normal,depth}) if(id) glDeleteTextures(1,&id);
        fbo=color=normal=depth=0; width=height=0;
    }
    bool create(int w,int h,bool capture)
    {
        if(!fbo) glCreateFramebuffers(1,&fbo);
        const auto allocate=[&](GLuint& id,GLenum format)
        {
            if(!id) glCreateTextures(GL_TEXTURE_2D,1,&id);
            // Mutable storage keeps the preview texture handle stable across
            // dock/viewport resizes, including ImGui commands already queued.
            glBindTexture(GL_TEXTURE_2D,id);
            glTexImage2D(GL_TEXTURE_2D,0,static_cast<GLint>(format),w,h,0,
                format==GL_DEPTH_COMPONENT24?GL_DEPTH_COMPONENT:GL_RGBA,
                format==GL_DEPTH_COMPONENT24?GL_UNSIGNED_INT:GL_UNSIGNED_BYTE,nullptr);
            glTextureParameteri(id,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
            glTextureParameteri(id,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
            glTextureParameteri(id,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
            glTextureParameteri(id,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        };
        allocate(color,GL_RGBA8);
        allocate(depth,GL_DEPTH_COMPONENT24);
        glNamedFramebufferTexture(fbo,GL_COLOR_ATTACHMENT0,color,0);
        glNamedFramebufferTexture(fbo,GL_DEPTH_ATTACHMENT,depth,0);
        const std::array<GLenum,2> attachments{{GL_COLOR_ATTACHMENT0,GL_COLOR_ATTACHMENT1}};
        if(capture)
        {
            allocate(normal,GL_RGBA8);
            glNamedFramebufferTexture(fbo,GL_COLOR_ATTACHMENT1,normal,0);
        }
        glNamedFramebufferDrawBuffers(fbo,capture?2:1,attachments.data());
        glNamedFramebufferReadBuffer(fbo,GL_COLOR_ATTACHMENT0);
        GLint allocatedWidth=0,allocatedHeight=0;
        glGetTextureLevelParameteriv(color,0,GL_TEXTURE_WIDTH,&allocatedWidth);
        glGetTextureLevelParameteriv(color,0,GL_TEXTURE_HEIGHT,&allocatedHeight);
        const bool complete=fbo&&color&&depth&&allocatedWidth==w&&allocatedHeight==h&&
            glCheckNamedFramebufferStatus(fbo,GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE;
        if(complete) { width=w; height=h; }
        return complete;
    }
};

GLuint makeTexture(const Image& image,bool cutout)
{
    if(!validImage(image)) return 0;
    GLuint id=0;
    glCreateTextures(GL_TEXTURE_2D,1,&id);
    if(!id) return 0;
    int levels=1;
    for(int extent=(std::max)(image.width,image.height);extent>1;extent/=2) ++levels;
    glTextureStorage2D(id,levels,GL_RGBA8,image.width,image.height);
    if(cutout)
    {
        const auto mips=coverageMipmaps(image);
        for(std::size_t i=0;i<mips.size();++i)
            glTextureSubImage2D(id,static_cast<GLint>(i),0,0,mips[i].width,mips[i].height,
                GL_RGBA,GL_UNSIGNED_BYTE,mips[i].rgba.data());
    }
    else
    {
        glTextureSubImage2D(id,0,0,0,image.width,image.height,GL_RGBA,GL_UNSIGNED_BYTE,image.rgba.data());
        glGenerateTextureMipmap(id);
    }
    glTextureParameteri(id,GL_TEXTURE_MIN_FILTER,GL_LINEAR_MIPMAP_LINEAR);
    glTextureParameteri(id,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glTextureParameteri(id,GL_TEXTURE_WRAP_S,cutout?GL_CLAMP_TO_EDGE:GL_REPEAT);
    glTextureParameteri(id,GL_TEXTURE_WRAP_T,cutout?GL_CLAMP_TO_EDGE:GL_REPEAT);
    GLint allocated=0;
    glGetTextureLevelParameteriv(id,0,GL_TEXTURE_WIDTH,&allocated);
    if(allocated!=image.width) { glDeleteTextures(1,&id); return 0; }
    return id;
}

void readImage(GLuint fbo,GLenum attachment,int w,int h,Image& image)
{
    image.width=w; image.height=h;
    image.rgba.resize(static_cast<std::size_t>(w)*h*4);
    glBindFramebuffer(GL_READ_FRAMEBUFFER,fbo);
    glReadBuffer(attachment);
    glReadPixels(0,0,w,h,GL_RGBA,GL_UNSIGNED_BYTE,image.rgba.data());
    const std::size_t row=static_cast<std::size_t>(w)*4;
    for(int y=0;y<h/2;++y)
        std::swap_ranges(image.rgba.begin()+y*row,image.rgba.begin()+(y+1)*row,
            image.rgba.begin()+(h-1-y)*row);
}

// Developer-only supersampled resolve. Colors are alpha-weighted in linear
// space; normals are alpha-weighted then renormalized. The resulting alpha is
// coverage, not transparency of the leaf material.
void resolveCapture(ImpostorView& view)
{
    const int w=view.albedo.width/2,h=view.albedo.height/2;
    Image albedo,normal;
    albedo.width=normal.width=w; albedo.height=normal.height=h;
    albedo.rgba.resize(static_cast<std::size_t>(w)*h*4);
    normal.rgba.resize(albedo.rgba.size());
    for(int y=0;y<h;++y) for(int x=0;x<w;++x)
    {
        glm::vec3 c(0.0f),n(0.0f);
        float sum=0.0f;
        for(int dy=0;dy<2;++dy) for(int dx=0;dx<2;++dx)
        {
            const std::size_t index=(static_cast<std::size_t>(y*2+dy)*view.albedo.width+x*2+dx)*4;
            const float a=view.albedo.rgba[index+3]/255.0f;
            sum+=a;
            for(int channel=0;channel<3;++channel)
            {
                c[channel]+=linear(view.albedo.rgba[index+channel]/255.0f)*a;
                n[channel]+=(view.normal.rgba[index+channel]/255.0f*2.0f-1.0f)*a;
            }
        }
        const std::size_t out=(static_cast<std::size_t>(y)*w+x)*4;
        if(sum>0.0f) c/=sum;
        const float length2=glm::dot(n,n);
        n=length2>0.000001f?n/std::sqrt(length2):glm::vec3(0,0,1);
        for(int channel=0;channel<3;++channel)
        {
            albedo.rgba[out+channel]=byte(srgb(c[channel]));
            normal.rgba[out+channel]=byte(n[channel]*0.5f+0.5f);
        }
        albedo.rgba[out+3]=normal.rgba[out+3]=byte(sum*0.25f);
    }
    // Propagate edge RGB into the alpha-zero padding, without changing alpha.
    // This prevents dark halos when GPU filtering samples across the silhouette.
    std::vector<int> frontier;
    const std::size_t pixels=static_cast<std::size_t>(w)*h;
    frontier.reserve(pixels);
    std::vector<std::uint8_t> reached(pixels,0);
    for(std::size_t i=0;i<pixels;++i)
        if(albedo.rgba[i*4+3]) { reached[i]=1; frontier.push_back(static_cast<int>(i)); }
    for(std::size_t head=0;head<frontier.size();++head)
    {
        const int index=frontier[head],x=index%w,y=index/w;
        const std::array<int,4> neighbours{{x>0?index-1:-1,x+1<w?index+1:-1,
            y>0?index-w:-1,y+1<h?index+w:-1}};
        for(int next:neighbours)
        {
            if(next<0||reached[static_cast<std::size_t>(next)]) continue;
            reached[static_cast<std::size_t>(next)]=1;
            for(int channel=0;channel<3;++channel)
            {
                albedo.rgba[static_cast<std::size_t>(next)*4+channel]=albedo.rgba[static_cast<std::size_t>(index)*4+channel];
                normal.rgba[static_cast<std::size_t>(next)*4+channel]=normal.rgba[static_cast<std::size_t>(index)*4+channel];
            }
            frontier.push_back(next);
        }
    }
    view.albedo=std::move(albedo); view.normal=std::move(normal);
}
}

struct TreeLabPreview::Impl
{
    GLuint program=0,leaf=0,bark=0;
    struct Uniforms
    {
        GLint vp=-1,camera=-1,sun=-1,kind=-1,capture=-1,groundExtent=-1,time=-1,wind=-1;
    } u;
    std::array<GpuMesh,4> meshes;
    GpuMesh ground;
    Target preview;
    glm::vec3 centre{0.0f,9.0f,0.0f};
    float radius=15.0f;
    bool uploaded=false;
    std::string error;
    void clear()
    {
        for(auto& mesh:meshes) mesh.clear();
        ground.clear(); preview.clear();
        if(program) glDeleteProgram(program);
        if(leaf) glDeleteTextures(1,&leaf);
        if(bark) glDeleteTextures(1,&bark);
        program=leaf=bark=0; uploaded=false;
    }
    void draw(Target& target,const PreviewSettings& settings,const glm::mat4& vp,
              const glm::vec3& camera,bool capture)
    {
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER,target.fbo);
        glViewport(0,0,target.width,target.height);
        const glm::vec3 bg=safeVector(settings.background,{0.075f,0.095f,0.12f},0.0f,1.0f);
        const std::array<GLfloat,4> clear{{capture?0.0f:srgb(bg.x),capture?0.0f:srgb(bg.y),
            capture?0.0f:srgb(bg.z),capture?0.0f:1.0f}};
        const std::array<GLfloat,4> neutral{{0.5f,0.5f,1.0f,0.0f}};
        const GLfloat depth=1.0f;
        glClearBufferfv(GL_COLOR,0,clear.data());
        if(capture) glClearBufferfv(GL_COLOR,1,neutral.data());
        glClearBufferfv(GL_DEPTH,0,&depth);
        glUseProgram(program);
        glUniformMatrix4fv(u.vp,1,GL_FALSE,glm::value_ptr(vp));
        glUniform3fv(u.camera,1,glm::value_ptr(camera));
        const glm::vec3 sun=direction(safe(settings.sunYaw,40.0f,-3600.0f,3600.0f),
            safe(settings.sunElevation,45.0f,-89.0f,89.0f));
        glUniform3fv(u.sun,1,glm::value_ptr(sun));
        glUniform1i(u.capture,capture?1:0);
        glUniform1f(u.time,safe(settings.time,0.0f,-1.0e6f,1.0e6f));
        glUniform1f(u.wind,capture?0.0f:safe(settings.windStrength,0.0f,0.0f,3.0f));
        glUniform1f(u.groundExtent,(std::max)(radius*3.0f,10.0f));
        if(!capture&&settings.showGround)
        {
            glUniform1i(u.kind,2);
            ground.draw();
        }
        glPolygonMode(GL_FRONT_AND_BACK,settings.wireframe&&!capture?GL_LINE:GL_FILL);
        const auto base=static_cast<std::size_t>(std::clamp(settings.lod,0,1)*2);
        if(settings.showWood)
        {
            glUniform1i(u.kind,0);
            glBindTextureUnit(0,bark);
            meshes[base].draw();
        }
        if(settings.showLeaves)
        {
            glUniform1i(u.kind,1);
            glBindTextureUnit(0,leaf);
            meshes[base+1].draw();
        }
    }
};

TreeLabPreview::TreeLabPreview():m(std::make_unique<Impl>()) {}
TreeLabPreview::~TreeLabPreview() { release(); }

bool TreeLabPreview::initialize()
{
    if(!m->program&&!reloadShaders()) return false;
    if(!m->ground.vao)
    {
        Mesh floor;
        for(glm::vec3 p:{glm::vec3(-1,0,-1),glm::vec3(1,0,-1),glm::vec3(1,0,1),glm::vec3(-1,0,1)})
        {
            Vertex vertex; vertex.position=p; vertex.normal={0,1,0}; floor.vertices.push_back(vertex);
        }
        floor.indices={0,2,1,0,3,2};
        if(!m->ground.upload(floor)) { m->ground.clear(); m->error="Cannot allocate tree preview floor."; return false; }
    }
    return true;
}

bool TreeLabPreview::reloadShaders()
{
    Ge::ShaderCacheOptions cache; cache.enabled=true;
    std::string error;
    const GLuint replacement=Ge::buildShaderProgram({
        {GL_VERTEX_SHADER,"../ENDURANCE/Ressources/Shader/TreeLab/preview.vs.glsl",{}},
        {GL_FRAGMENT_SHADER,"../ENDURANCE/Ressources/Shader/TreeLab/preview.fs.glsl",{}}
    },error,"Tree Lab preview",cache);
    if(!replacement) { m->error=error; return false; }
    if(m->program) glDeleteProgram(m->program);
    m->program=replacement;
    const auto get=[&](const char* name) { return glGetUniformLocation(replacement,name); };
    m->u.vp=get("uViewProjection"); m->u.camera=get("uCamera"); m->u.sun=get("uSun");
    m->u.kind=get("uKind"); m->u.capture=get("uCapture"); m->u.groundExtent=get("uGroundExtent");
    m->u.time=get("uTime"); m->u.wind=get("uWindStrength");
    m->error.clear(); return true;
}

bool TreeLabPreview::upload(const Tree& tree)
{
    if(tree.cancelled || !validImage(tree.leafMask) || !validImage(tree.barkTexture))
    { m->error="Tree preview requires a completed tree and valid textures."; return false; }
    State state; state.prepareTransfer();
    std::array<GpuMesh,4> replacement;
    GLuint leaf=0,bark=0;
    const auto rollback=[&]()
    {
        for(auto& mesh:replacement) mesh.clear();
        if(leaf) glDeleteTextures(1,&leaf);
        if(bark) glDeleteTextures(1,&bark);
        m->error="Cannot allocate the tree preview geometry or textures.";
        return false;
    };
    for(std::size_t i=0;i<2;++i)
        if(!replacement[i*2].upload(tree.lods[i].wood)||!replacement[i*2+1].upload(tree.lods[i].foliage)) return rollback();
    leaf=makeTexture(tree.leafMask,true);
    bark=makeTexture(tree.barkTexture,false);
    if(!leaf||!bark) return rollback();
    for(auto& mesh:m->meshes) mesh.clear();
    if(m->leaf) glDeleteTextures(1,&m->leaf);
    if(m->bark) glDeleteTextures(1,&m->bark);
    m->meshes=replacement; m->leaf=leaf; m->bark=bark;
    m->centre=safeVector((tree.boundsMin+tree.boundsMax)*0.5f,{0.0f,9.0f,0.0f},-1.0e5f,1.0e5f);
    m->radius=safe(glm::length(tree.boundsMax-tree.boundsMin)*0.5f,15.0f,0.1f,1.0e5f);
    m->uploaded=true; m->error.clear(); return true;
}

bool TreeLabPreview::render(const PreviewSettings& settings)
{
    if(!initialize()) return false;
    State state; state.prepareDraw(); state.prepareTransfer();
    const int w=std::clamp(settings.width,16,4096),h=std::clamp(settings.height,16,4096);
    if(m->preview.width!=w||m->preview.height!=h)
    {
        if(!m->preview.create(w,h,false))
        { m->error="Tree preview framebuffer is incomplete."; return false; }
    }
    const glm::vec3 target=safeVector(settings.target,m->centre,-1.0e5f,1.0e5f);
    const float distance=safe(settings.distance,32.0f,0.1f,1.0e6f);
    const glm::vec3 camera=target+direction(safe(settings.yaw,35.0f,-3600.0f,3600.0f),
        safe(settings.pitch,15.0f,-89.0f,89.0f))*distance;
    const float farPlane=(std::max)(distance+m->radius*5.0f,100.0f);
    const glm::mat4 projection=glm::perspectiveRH_NO(glm::radians(45.0f),static_cast<float>(w)/static_cast<float>(h),
        (std::max)(0.01f,distance*0.0005f),farPlane);
    m->draw(m->preview,settings,projection*glm::lookAtRH(camera,target,glm::vec3(0,1,0)),camera,false);
    m->error.clear(); return true;
}

bool TreeLabPreview::captureImpostors(std::vector<ImpostorView>& views,int resolution,int azimuthViews,bool elevatedViews)
{
    if(!m->uploaded) { m->error="Generate and upload a tree before baking impostors."; return false; }
    if(!initialize()) return false;
    State state; state.prepareDraw(); state.prepareTransfer();
    // Attachment one may have its own color write mask and blend enable.
    std::array<GLboolean,4> mask1{};
    glGetBooleani_v(GL_COLOR_WRITEMASK,1,mask1.data());
    const GLboolean blend1=glIsEnabledi(GL_BLEND,1);
    glColorMaski(1,GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE); glDisablei(GL_BLEND,1);
    struct RestoreAttachment
    {
        std::array<GLboolean,4> mask; GLboolean blend;
        ~RestoreAttachment() { glColorMaski(1,mask[0],mask[1],mask[2],mask[3]);
            if(blend) glEnablei(GL_BLEND,1); else glDisablei(GL_BLEND,1); }
    } restoreAttachment{mask1,blend1};
    Target target;
    struct ReleaseTarget { Target& t; ~ReleaseTarget() { t.clear(); } } releaseTarget{target};
    const int size=std::clamp(resolution,32,1024),count=std::clamp(azimuthViews,1,16);
    const int renderSize=size*2;
    if(!target.create(renderSize,renderSize,true)) { m->error="Impostor framebuffer is incomplete."; return false; }
    std::vector<ImpostorView> result;
    const float extent=m->radius*1.06f;
    PreviewSettings settings; settings.showGround=false;
    const auto capture=[&](float yaw,float pitch)
    {
        ImpostorView view;
        view.direction=direction(yaw,pitch); view.centre=m->centre; view.halfExtent=extent;
        const glm::vec3 eye=view.centre+view.direction*(m->radius*3.0f);
        const glm::vec3 up=pitch>89.0f?glm::vec3(0,0,-1):glm::vec3(0,1,0);
        view.up=up;
        const glm::mat4 projection=glm::orthoRH_NO(-extent,extent,-extent,extent,m->radius*0.01f,m->radius*6.0f);
        m->draw(target,settings,projection*glm::lookAtRH(eye,view.centre,up),eye,true);
        readImage(target.fbo,GL_COLOR_ATTACHMENT0,renderSize,renderSize,view.albedo);
        readImage(target.fbo,GL_COLOR_ATTACHMENT1,renderSize,renderSize,view.normal);
        resolveCapture(view);
        result.push_back(std::move(view));
    };
    for(int i=0;i<count;++i) capture(static_cast<float>(i)*360.0f/static_cast<float>(count),15.0f);
    if(elevatedViews)
    {
        for(int i=0;i<count;++i) capture(static_cast<float>(i)*360.0f/static_cast<float>(count),60.0f);
        capture(0.0f,90.0f);
    }
    views=std::move(result); m->error.clear(); return true;
}

unsigned int TreeLabPreview::texture() const noexcept { return m->preview.color; }
int TreeLabPreview::width() const noexcept { return m->preview.width; }
int TreeLabPreview::height() const noexcept { return m->preview.height; }
const std::string& TreeLabPreview::error() const noexcept { return m->error; }
void TreeLabPreview::release() noexcept { if(m) m->clear(); }
}
