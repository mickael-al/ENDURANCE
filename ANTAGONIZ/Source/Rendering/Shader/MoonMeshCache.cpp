#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "MoonMeshCache.hpp"
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

namespace Ge
{
namespace
{
bool finite(float value)
{
    std::uint32_t bits=0;
    std::memcpy(&bits,&value,sizeof(bits));
    return (bits&0x7f800000u)!=0x7f800000u;
}
bool finite(glm::vec3 value) { return finite(value.x)&&finite(value.y)&&finite(value.z); }
int mipCount(int size) { int count=1; while(size>1) { size/=2; ++count; } return count; }
std::uint64_t texels(int size)
{ std::uint64_t count=0; do { count+=std::uint64_t(size)*size; size/=2; } while(size); return count; }
void dropTexture(GLuint& texture) { if(texture) glDeleteTextures(1,&texture); texture=0; }

// Runs only on uploads/cache misses. Hits do no GL queries or draws.
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
        for(unsigned i=0;i<2;++i)
        {
            glActiveTexture(GL_TEXTURE0+i);
            glGetIntegerv(GL_TEXTURE_BINDING_2D,&textures[i]);
            glGetIntegeri_v(GL_SAMPLER_BINDING,i,&samplers[i]);
        }
        glGetIntegerv(GL_DEPTH_FUNC,&depthFunc);
        glGetIntegerv(GL_CULL_FACE_MODE,&cullFace);
        glGetIntegerv(GL_FRONT_FACE,&frontFace);
        glGetBooleanv(GL_DEPTH_WRITEMASK,&depthMask);
        glGetBooleani_v(GL_COLOR_WRITEMASK,0,colorMask.data());
        blend=glIsEnabledi(GL_BLEND,0);
        glGetIntegerv(GL_POLYGON_MODE,polygonMode.data());
        glGetDoublev(GL_DEPTH_RANGE,depthRange.data());
        glGetIntegerv(GL_CLIP_ORIGIN,&clipOrigin);
        glGetIntegerv(GL_CLIP_DEPTH_MODE,&clipDepth);
        for(std::size_t i=0;i<caps.size();++i) enabled[i]=glIsEnabled(caps[i]);
        for(unsigned i=0;i<clipEnabled.size();++i) clipEnabled[i]=glIsEnabled(GL_CLIP_DISTANCE0+i);
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
        for(unsigned i=0;i<2;++i)
        {
            glActiveTexture(GL_TEXTURE0+i);
            glBindTexture(GL_TEXTURE_2D,static_cast<GLuint>(textures[i]));
            glBindSampler(i,static_cast<GLuint>(samplers[i]));
        }
        glActiveTexture(static_cast<GLenum>(activeTexture));
        glDepthFunc(static_cast<GLenum>(depthFunc)); glDepthMask(depthMask);
        glCullFace(static_cast<GLenum>(cullFace)); glFrontFace(static_cast<GLenum>(frontFace));
        glColorMaski(0,colorMask[0],colorMask[1],colorMask[2],colorMask[3]);
        if(blend) glEnablei(GL_BLEND,0); else glDisablei(GL_BLEND,0);
        glPolygonMode(GL_FRONT_AND_BACK,static_cast<GLenum>(polygonMode[0]));
        glDepthRange(depthRange[0],depthRange[1]);
        glClipControl(static_cast<GLenum>(clipOrigin),static_cast<GLenum>(clipDepth));
        for(std::size_t i=0;i<caps.size();++i)
            if(enabled[i]) glEnable(caps[i]); else glDisable(caps[i]);
        for(unsigned i=0;i<clipEnabled.size();++i)
            if(clipEnabled[i]) glEnable(GL_CLIP_DISTANCE0+i); else glDisable(GL_CLIP_DISTANCE0+i);
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER,static_cast<GLuint>(unpackBuffer));
        for(std::size_t i=0;i<pixelNames.size();++i) glPixelStorei(pixelNames[i],pixelValues[i]);
    }
    void transfer()
    {
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER,0);
        for(GLenum name:pixelNames) glPixelStorei(name,name==GL_UNPACK_ALIGNMENT ? 1 : 0);
    }
    void draw()
    {
        glDisablei(GL_BLEND,0); glColorMaski(0,GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
        for(GLenum cap:caps) glDisable(cap);
        for(unsigned i=0;i<clipEnabled.size();++i) glDisable(GL_CLIP_DISTANCE0+i);
        glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LESS); glDepthMask(GL_TRUE);
        glEnable(GL_CULL_FACE); glCullFace(GL_BACK); glFrontFace(GL_CCW);
        glDepthRange(0,1); glClipControl(GL_LOWER_LEFT,GL_NEGATIVE_ONE_TO_ONE);
        glBindSampler(0,0); glBindSampler(1,0);
        glActiveTexture(GL_TEXTURE0); glPolygonMode(GL_FRONT_AND_BACK,GL_FILL);
    }
private:
    GLint drawFbo=0,readFbo=0,program=0,vao=0,activeTexture=0;
    std::array<GLint,2> textures{},samplers{},polygonMode{};
    GLint depthFunc=0,clipOrigin=0,clipDepth=0,unpackBuffer=0,cullFace=0,frontFace=0;
    std::array<GLint,4> viewport{};
    std::array<GLdouble,2> depthRange{};
    GLboolean depthMask=GL_TRUE,blend=GL_FALSE;
    std::array<GLboolean,4> colorMask{};
    const std::array<GLenum,18> caps{{GL_DEPTH_TEST,GL_CULL_FACE,GL_SCISSOR_TEST,
        GL_STENCIL_TEST,GL_RASTERIZER_DISCARD,GL_FRAMEBUFFER_SRGB,GL_POLYGON_OFFSET_FILL,
        GL_POLYGON_OFFSET_LINE,GL_POLYGON_OFFSET_POINT,GL_SAMPLE_ALPHA_TO_COVERAGE,
        GL_SAMPLE_COVERAGE,GL_COLOR_LOGIC_OP,GL_SAMPLE_MASK,GL_SAMPLE_ALPHA_TO_ONE,GL_DITHER,GL_PRIMITIVE_RESTART,GL_PRIMITIVE_RESTART_FIXED_INDEX,GL_POLYGON_SMOOTH}};
    std::array<GLboolean,18> enabled{};
    std::array<GLboolean,8> clipEnabled{};
    const std::array<GLenum,8> pixelNames{{GL_UNPACK_ALIGNMENT,GL_UNPACK_ROW_LENGTH,
        GL_UNPACK_IMAGE_HEIGHT,GL_UNPACK_SKIP_PIXELS,GL_UNPACK_SKIP_ROWS,
        GL_UNPACK_SKIP_IMAGES,GL_UNPACK_SWAP_BYTES,GL_UNPACK_LSB_FIRST}};
    std::array<GLint,8> pixelValues{};
};

const char* vertexSource=R"GLSL(
layout(location=0) in vec3 aPosition;
layout(location=1) in vec3 aNormal;
layout(location=2) in vec2 aUv;
layout(location=3) in vec4 aColor;
uniform mat4 uViewProjection;
uniform mat4 uLightProjection;
out vec3 vNormal;
out vec2 vUv;
out vec3 vColor;
out vec4 vShadow;
void main()
{
    gl_Position=uViewProjection*vec4(aPosition,1.0);
    vNormal=aNormal; vUv=aUv; vColor=aColor.rgb;
    vShadow=uLightProjection*vec4(aPosition,1.0);
}
)GLSL";
const char* fragmentSource=R"GLSL(
in vec3 vNormal;
in vec2 vUv;
in vec3 vColor;
in vec4 vShadow;
layout(location=0) out vec4 outColor;
layout(binding=0) uniform sampler2D uAlbedo;
layout(binding=1) uniform sampler2DShadow uShadow;
uniform vec3 uLight;
void main()
{
    vec3 n=normalize(vNormal);
    float diffuse=max(dot(n,uLight),0.0);
    vec3 shadow=vShadow.xyz/vShadow.w*0.5+0.5;
    // Receiver-plane correction follows each real triangle, not its smoothed
    // normal. It prevents the wide self-shadow bands a constant PCF bias causes.
    vec2 texel=1.0/vec2(textureSize(uShadow,0));
    vec3 dx=dFdx(shadow),dy=dFdy(shadow);
    float determinant=dx.x*dy.y-dx.y*dy.x;
    vec2 gradient=vec2(0.0);
    if(abs(determinant)>1e-12)
        gradient=vec2(dx.z*dy.y-dy.z*dx.y,dx.x*dy.z-dy.x*dx.z)/determinant;
    float bias=0.00002+min(dot(abs(gradient),texel)*0.75,0.012);
    float visibility=0.0;
    for(int y=-1;y<=1;++y) for(int x=-1;x<=1;++x)
    {
        vec2 offset=vec2(x,y)*texel;
        float receiver=shadow.z+clamp(dot(gradient,offset),-0.012,0.012);
        visibility+=texture(uShadow,vec3(shadow.xy+offset,receiver-bias));
    }
    // The image stores reflected radiance per unit solar irradiance. Tint,
    // solar intensity, clouds and atmosphere remain in the main sky shader.
    vec3 albedo=texture(uAlbedo,vUv).rgb*vColor;
    outColor=vec4(albedo*(diffuse*visibility/(9.0*3.14159265359)),1.0);
}
)GLSL";
const char* depthVertex=R"GLSL(
layout(location=0) in vec3 aPosition;
uniform mat4 uViewProjection;
void main() { gl_Position=uViewProjection*vec4(aPosition,1.0); }
)GLSL";
const char* depthFragment="void main() {}\n";

GLuint compile(GLenum type,const char* source,std::string& error)
{
    const GLuint shader=glCreateShader(type);
    if(!shader) { error="Cannot create Moon cache shader."; return 0; }
    const char* parts[]{"#version 450 core\n",source};
    glShaderSource(shader,2,parts,nullptr); glCompileShader(shader);
    GLint success=0; glGetShaderiv(shader,GL_COMPILE_STATUS,&success);
    if(!success)
    {
        GLint length=0; glGetShaderiv(shader,GL_INFO_LOG_LENGTH,&length);
        std::vector<char> log(static_cast<std::size_t>((std::max)(length,1)),0);
        glGetShaderInfoLog(shader,length,nullptr,log.data());
        error="Moon cache shader: "+std::string(log.data());
        glDeleteShader(shader); return 0;
    }
    return shader;
}
GLuint makeProgram(const char* vs,const char* fs,std::string& error)
{
    const GLuint v=compile(GL_VERTEX_SHADER,vs,error);
    if(!v) return 0;
    const GLuint f=compile(GL_FRAGMENT_SHADER,fs,error);
    if(!f) { glDeleteShader(v); return 0; }
    GLuint p=glCreateProgram();
    if(p)
    {
        glAttachShader(p,v); glAttachShader(p,f); glLinkProgram(p);
        GLint success=0; glGetProgramiv(p,GL_LINK_STATUS,&success);
        if(!success)
        {
            GLint length=0; glGetProgramiv(p,GL_INFO_LOG_LENGTH,&length);
            std::vector<char> log(static_cast<std::size_t>((std::max)(length,1)),0);
            glGetProgramInfoLog(p,length,nullptr,log.data());
            error="Moon cache link: "+std::string(log.data());
            glDeleteProgram(p); p=0;
        }
    }
    else error="Cannot create Moon cache program.";
    glDeleteShader(v); glDeleteShader(f); return p;
}
struct Mesh
{
    GLuint vao=0,vbo=0,ibo=0,albedo=0;
    GLsizei count=0;
    std::uint64_t bytes=0;
    float bound=0.f;
    void clear()
    {
        if(vao) glDeleteVertexArrays(1,&vao);
        if(vbo) glDeleteBuffers(1,&vbo);
        if(ibo) glDeleteBuffers(1,&ibo);
        dropTexture(albedo);
        vao=vbo=ibo=0; count=0; bytes=0; bound=0;
    }
    void draw() const
    { glBindVertexArray(vao); glDrawElements(GL_TRIANGLES,count,GL_UNSIGNED_INT,nullptr); }
};
struct Target
{
    GLuint fbo=0,color=0,depth=0;
    int size=0;
    void clear()
    {
        if(fbo) glDeleteFramebuffers(1,&fbo);
        dropTexture(color); dropTexture(depth); fbo=0; size=0;
    }
    bool create(int requested,bool shadow,std::string& error)
    {
        if(size==requested) return true;
        Target next;
        glCreateFramebuffers(1,&next.fbo);
        glCreateTextures(GL_TEXTURE_2D,1,&next.depth);
        glTextureStorage2D(next.depth,1,GL_DEPTH_COMPONENT24,requested,requested);
        glTextureParameteri(next.depth,GL_TEXTURE_MIN_FILTER,shadow?GL_LINEAR:GL_NEAREST);
        glTextureParameteri(next.depth,GL_TEXTURE_MAG_FILTER,shadow?GL_LINEAR:GL_NEAREST);
        glTextureParameteri(next.depth,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
        glTextureParameteri(next.depth,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        glNamedFramebufferTexture(next.fbo,GL_DEPTH_ATTACHMENT,next.depth,0);
        if(shadow)
        {
            glTextureParameteri(next.depth,GL_TEXTURE_COMPARE_MODE,GL_COMPARE_REF_TO_TEXTURE);
            glTextureParameteri(next.depth,GL_TEXTURE_COMPARE_FUNC,GL_LEQUAL);
            glNamedFramebufferDrawBuffer(next.fbo,GL_NONE);
            glNamedFramebufferReadBuffer(next.fbo,GL_NONE);
        }
        else
        {
            glCreateTextures(GL_TEXTURE_2D,1,&next.color);
            glTextureStorage2D(next.color,mipCount(requested),GL_RGBA16F,requested,requested);
            glTextureParameteri(next.color,GL_TEXTURE_MIN_FILTER,GL_LINEAR_MIPMAP_LINEAR);
            glTextureParameteri(next.color,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
            glTextureParameteri(next.color,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
            glTextureParameteri(next.color,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
            glNamedFramebufferTexture(next.fbo,GL_COLOR_ATTACHMENT0,next.color,0);
            glNamedFramebufferDrawBuffer(next.fbo,GL_COLOR_ATTACHMENT0);
            glNamedFramebufferReadBuffer(next.fbo,GL_COLOR_ATTACHMENT0);
        }
        if(glCheckNamedFramebufferStatus(next.fbo,GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE)
        { next.clear(); error="Incomplete Moon cache framebuffer."; return false; }
        next.size=requested;
        clear(); *this=next;
        return true;
    }
};
glm::vec3 upFor(glm::vec3 forward)
{ return std::abs(forward.y)>.95f ? glm::vec3(0,0,1) : glm::vec3(0,1,0); }
}

struct MoonMeshCache::Impl
{
    Mesh mesh;
    Target image,shadow;
    GLuint colorProgram=0,depthProgram=0;
    GLint colorView=-1,colorLightProjection=-1,colorLight=-1,depthView=-1;
    bool imageValid=false,shadowValid=false;
    glm::vec3 observer{},light{};
    float rotation=0.f;
    glm::mat4 lightProjection{1.f};
    glm::vec4 right{},up{},forward{};
    std::uint64_t images=0,shadows=0;
    bool programs(std::string& error)
    {
        if(colorProgram && depthProgram) return true;
        const GLuint color=makeProgram(vertexSource,fragmentSource,error);
        if(!color) return false;
        const GLuint depth=makeProgram(depthVertex,depthFragment,error);
        if(!depth) { glDeleteProgram(color); return false; }
        colorProgram=color; depthProgram=depth;
        colorView=glGetUniformLocation(color,"uViewProjection");
        colorLightProjection=glGetUniformLocation(color,"uLightProjection");
        colorLight=glGetUniformLocation(color,"uLight");
        depthView=glGetUniformLocation(depth,"uViewProjection");
        return true;
    }
    void clear()
    {
        mesh.clear(); image.clear(); shadow.clear();
        if(colorProgram) glDeleteProgram(colorProgram);
        if(depthProgram) glDeleteProgram(depthProgram);
        colorProgram=depthProgram=0; imageValid=shadowValid=false;
        right=up=forward=glm::vec4(0); images=shadows=0;
    }
};

MoonMeshCache::MoonMeshCache():m(new Impl) {}
MoonMeshCache::~MoonMeshCache() { clear(); }
bool MoonMeshCache::valid() const noexcept { return m->mesh.count>0; }
void MoonMeshCache::clear() noexcept { m->clear(); }
void MoonMeshCache::invalidate() noexcept { m->imageValid=m->shadowValid=false; }
unsigned MoonMeshCache::texture() const noexcept { return m->imageValid?m->image.color:0; }
int MoonMeshCache::resolution() const noexcept { return m->image.size; }
glm::vec4 MoonMeshCache::projectionRight() const noexcept { return m->right; }
glm::vec4 MoonMeshCache::projectionUp() const noexcept { return m->up; }
glm::vec4 MoonMeshCache::projectionForward() const noexcept
{ return m->imageValid ? m->forward : glm::vec4(0); }
std::uint64_t MoonMeshCache::imageUpdates() const noexcept { return m->images; }
std::uint64_t MoonMeshCache::shadowUpdates() const noexcept { return m->shadows; }
std::uint64_t MoonMeshCache::gpuBytes() const noexcept
{
    return m->mesh.bytes+texels(m->image.size)*8+
        (std::uint64_t(m->image.size)*m->image.size+std::uint64_t(m->shadow.size)*m->shadow.size)*4;
}

bool MoonMeshCache::setMesh(const MoonMeshData& data,std::string& error)
{
    error.clear();
    if(data.vertices.empty() || data.vertices.size()>4000000 || data.indices.empty() ||
       data.indices.size()>24000000 || data.indices.size()%3 || data.width==0 || data.height==0 ||
       data.width>8192 || data.height>8192 ||
       data.albedoRgba.size()!=std::size_t(data.width)*data.height*4 ||
       !finite(data.meanAlbedo)||data.meanAlbedo<0.f||data.meanAlbedo>1.f)
    { error="Invalid Moon mesh or albedo dimensions."; return false; }
    float bound=0.f;
    for(const auto& vertex:data.vertices)
    {
        if(!finite(vertex.position)||!finite(vertex.normal)||!finite(vertex.uv.x)||!finite(vertex.uv.y))
        { error="Nonfinite Moon vertex."; return false; }
        const float n2=glm::dot(vertex.normal,vertex.normal);
        const float r2=glm::dot(vertex.position,vertex.position);
        if(!finite(n2)||n2<.5f||n2>1.5f||!finite(r2)||r2>4096.f*4096.f)
        { error="Invalid Moon vertex position/normal."; return false; }
        bound=(std::max)(bound,std::sqrt(r2));
        for(int c=0;c<4;++c) if(!finite(vertex.color[c])||vertex.color[c]<0.f||vertex.color[c]>1.f)
        { error="Invalid linear Moon vertex color."; return false; }
    }
    if(bound<=1e-6f) { error="Degenerate Moon mesh."; return false; }
    for(auto i:data.indices) if(i>=data.vertices.size())
    { error="Moon triangle index out of range."; return false; }
    State state; state.transfer();
    Mesh next;
    glCreateVertexArrays(1,&next.vao);
    glCreateBuffers(1,&next.vbo); glCreateBuffers(1,&next.ibo);
    if(!next.vao||!next.vbo||!next.ibo)
    { next.clear(); error="Cannot allocate Moon geometry objects."; return false; }
    glNamedBufferData(next.vbo,static_cast<GLsizeiptr>(data.vertices.size()*sizeof(MoonMeshData::Vertex)),data.vertices.data(),GL_STATIC_DRAW);
    glNamedBufferData(next.ibo,static_cast<GLsizeiptr>(data.indices.size()*sizeof(std::uint32_t)),data.indices.data(),GL_STATIC_DRAW);
    glVertexArrayVertexBuffer(next.vao,0,next.vbo,0,sizeof(MoonMeshData::Vertex));
    glVertexArrayElementBuffer(next.vao,next.ibo);
    const GLint sizes[]{3,3,2,4};
    const GLuint offsets[]{offsetof(MoonMeshData::Vertex,position),offsetof(MoonMeshData::Vertex,normal),
        offsetof(MoonMeshData::Vertex,uv),offsetof(MoonMeshData::Vertex,color)};
    for(GLuint i=0;i<4;++i)
    {
        glEnableVertexArrayAttrib(next.vao,i);
        glVertexArrayAttribFormat(next.vao,i,sizes[i],GL_FLOAT,GL_FALSE,offsets[i]);
        glVertexArrayAttribBinding(next.vao,i,0);
    }
    glCreateTextures(GL_TEXTURE_2D,1,&next.albedo);
    if(!next.albedo) { next.clear(); error="Cannot allocate Moon albedo object."; return false; }
    const int levels=mipCount(static_cast<int>((std::max)(data.width,data.height)));
    glTextureStorage2D(next.albedo,levels,GL_SRGB8_ALPHA8,data.width,data.height);
    GLint atlasWidth=0;
    GLint64 vertexBytes=0,indexBytes=0;
    glGetTextureLevelParameteriv(next.albedo,0,GL_TEXTURE_WIDTH,&atlasWidth);
    glGetNamedBufferParameteri64v(next.vbo,GL_BUFFER_SIZE,&vertexBytes);
    glGetNamedBufferParameteri64v(next.ibo,GL_BUFFER_SIZE,&indexBytes);
    if(atlasWidth!=static_cast<GLint>(data.width)||
       vertexBytes!=static_cast<GLint64>(data.vertices.size()*sizeof(MoonMeshData::Vertex))||
       indexBytes!=static_cast<GLint64>(data.indices.size()*sizeof(std::uint32_t)))
    { next.clear(); error="Cannot allocate Moon geometry/albedo storage."; return false; }
    glTextureSubImage2D(next.albedo,0,0,0,data.width,data.height,GL_RGBA,GL_UNSIGNED_BYTE,data.albedoRgba.data());
    glTextureParameteri(next.albedo,GL_TEXTURE_MIN_FILTER,GL_LINEAR_MIPMAP_LINEAR);
    glTextureParameteri(next.albedo,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glTextureParameteri(next.albedo,GL_TEXTURE_WRAP_S,GL_REPEAT);
    glTextureParameteri(next.albedo,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    glGenerateTextureMipmap(next.albedo);
    next.count=static_cast<GLsizei>(data.indices.size()); next.bound=bound;
    next.bytes=data.vertices.size()*sizeof(MoonMeshData::Vertex)+data.indices.size()*sizeof(std::uint32_t);
    unsigned w=data.width,h=data.height;
    for(int l=0;l<levels;++l)
    { next.bytes+=std::uint64_t(w)*h*4; w=(std::max)(w/2,1u); h=(std::max)(h/2,1u); }
    m->mesh.clear(); m->mesh=next;
    invalidate();
    return true;
}

bool MoonMeshCache::update(glm::vec3 observer,float rotation,glm::vec3 sunDirection,int resolution,std::string& error)
{
    error.clear();
    if(!valid()) { error="Moon mesh is not loaded."; return false; }
    const float distance=glm::length(observer),sunLength=glm::length(sunDirection);
    if(!finite(observer)||!finite(rotation)||!finite(sunDirection)||!finite(distance)||
        !finite(sunLength)||sunLength<1e-6f||distance<=m->mesh.bound*1.01f)
    {
        m->imageValid=false;
        error="Moon observer must remain outside the body and fragment bounds, with a valid light direction.";
        return false;
    }
    resolution=resolution<=512?512:(resolution<=1024?1024:2048);
    sunDirection/=sunLength;
    rotation=std::remainder(rotation,6.2831853071795864769f);
    // Cumulative thresholds are always measured from the last rendered state.
    // At 1024px this bounds ordinary travel to less than one quarter cache texel.
    const float tanHalf=m->mesh.bound/std::sqrt(distance*distance-m->mesh.bound*m->mesh.bound)*1.025f;
    const float pixelsPerUnit=float(resolution)/(2.f*distance*tanHalf);
    const bool newLight=!m->shadowValid || glm::length(sunDirection-m->light)>0.0001f || rotation!=m->rotation;
    const bool newImage=!m->imageValid || resolution!=m->image.size || newLight ||
        glm::length(observer-m->observer)*pixelsPerUnit>.25f;
    if(!newImage) return true;
    State state; state.draw();
    if(!m->programs(error)||!m->image.create(resolution,false,error)||!m->shadow.create(2048,true,error)) return false;
    const glm::mat4 model=glm::rotate(glm::mat4(1.f),rotation,glm::vec3(0,1,0));
    // Lighting and depth both operate in object-space; a rigid Y rotation
    // changes the light vector, while the shared vertex buffers remain static.
    const glm::vec3 localLight=glm::vec3(glm::transpose(model)*glm::vec4(sunDirection,0));
    if(newLight)
    {
        const float extent=m->mesh.bound*1.03f;
        m->lightProjection=glm::ortho(-extent,extent,-extent,extent,extent*.25f,extent*4.25f)*
            glm::lookAt(localLight*extent*2.25f,glm::vec3(0),upFor(localLight));
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER,m->shadow.fbo);
        glViewport(0,0,m->shadow.size,m->shadow.size);
        float depth=1.f; glClearNamedFramebufferfv(m->shadow.fbo,GL_DEPTH,0,&depth);
        glUseProgram(m->depthProgram);
        glUniformMatrix4fv(m->depthView,1,GL_FALSE,glm::value_ptr(m->lightProjection));
        m->mesh.draw();
        m->shadowValid=true; m->light=sunDirection; m->rotation=rotation; ++m->shadows;
    }
    const glm::vec3 forward=-observer/distance;
    const glm::vec3 right=glm::normalize(glm::cross(forward,upFor(forward)));
    const glm::vec3 up=glm::cross(right,forward);
    const float nearPlane=(std::max)(distance-m->mesh.bound*1.02f,.0001f);
    const float farPlane=distance+m->mesh.bound*1.02f;
    const glm::mat4 viewProjection=glm::perspective(2.f*std::atan(tanHalf),1.f,nearPlane,farPlane)*
        glm::lookAt(observer,glm::vec3(0),up)*model;
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER,m->image.fbo);
    glViewport(0,0,resolution,resolution);
    float clear[]{0,0,0,0},depth=1.f;
    glClearNamedFramebufferfv(m->image.fbo,GL_COLOR,0,clear);
    glClearNamedFramebufferfv(m->image.fbo,GL_DEPTH,0,&depth);
    glUseProgram(m->colorProgram);
    glUniformMatrix4fv(m->colorView,1,GL_FALSE,glm::value_ptr(viewProjection));
    glUniformMatrix4fv(m->colorLightProjection,1,GL_FALSE,glm::value_ptr(m->lightProjection));
    glUniform3fv(m->colorLight,1,glm::value_ptr(localLight));
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D,m->mesh.albedo);
    glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D,m->shadow.depth);
    m->mesh.draw();
    glMemoryBarrier(GL_FRAMEBUFFER_BARRIER_BIT|GL_TEXTURE_FETCH_BARRIER_BIT);
    glGenerateTextureMipmap(m->image.color);
    m->observer=observer;
    m->right=glm::vec4(right,tanHalf); m->up=glm::vec4(up,tanHalf); m->forward=glm::vec4(forward,1.f);
    m->imageValid=true; ++m->images;
    return true;
}
}
