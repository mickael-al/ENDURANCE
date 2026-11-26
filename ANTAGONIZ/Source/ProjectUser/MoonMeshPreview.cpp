#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "MoonMeshPreview.hpp"
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
#include <vector>

namespace EnduranceMoon
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
glm::vec3 vector(MoonVec3 v) { return {v.x,v.y,v.z}; }
glm::vec3 direction(float yaw,float pitch)
{
    const float y=glm::radians(yaw),p=glm::radians(pitch);
    return {std::cos(p)*std::sin(y),std::sin(p),std::cos(p)*std::cos(y)};
}
MeshPreviewSettings sanitize(MeshPreviewSettings s)
{
    s.width=std::clamp(s.width,64,2048); s.height=std::clamp(s.height,64,2048);
    s.yaw=std::fmod(safe(s.yaw,0,-1e6f,1e6f),360.f);
    s.pitch=safe(s.pitch,6,-85,85); s.distance=safe(s.distance,4.6f,1.15f,20.f);
    s.sunYaw=std::fmod(safe(s.sunYaw,-40,-1e6f,1e6f),360.f);
    s.sunElevation=safe(s.sunElevation,35,-89,89);
    s.exposure=safe(s.exposure,1.4f,.05f,8.f); s.ambient=safe(s.ambient,.055f,0,1);
    for(int i=0;i<3;++i) s.background[i]=safe(s.background[i],0,0,1);
    return s;
}
bool equal(const MeshPreviewSettings& a,const MeshPreviewSettings& b)
{
    return a.width==b.width && a.height==b.height && a.yaw==b.yaw && a.pitch==b.pitch &&
        a.distance==b.distance && a.sunYaw==b.sunYaw && a.sunElevation==b.sunElevation &&
        a.exposure==b.exposure && a.ambient==b.ambient && a.background==b.background &&
        a.wireframe==b.wireframe && a.normals==b.normals && a.shadows==b.shadows;
}
float srgb(float v)
{ return v<=.0031308f ? v*12.92f : 1.055f*std::pow(v,1.f/2.4f)-.055f; }

// The lab is invoked inside the HUD. Preserve all state this helper changes,
// including nondefault pixel-store buffers, depth conventions and clip planes.
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
    const std::array<GLenum,15> caps{{GL_DEPTH_TEST,GL_CULL_FACE,GL_SCISSOR_TEST,
        GL_STENCIL_TEST,GL_RASTERIZER_DISCARD,GL_FRAMEBUFFER_SRGB,GL_POLYGON_OFFSET_FILL,
        GL_POLYGON_OFFSET_LINE,GL_POLYGON_OFFSET_POINT,GL_SAMPLE_ALPHA_TO_COVERAGE,
        GL_SAMPLE_COVERAGE,GL_COLOR_LOGIC_OP,GL_SAMPLE_MASK,GL_SAMPLE_ALPHA_TO_ONE,GL_DITHER}};
    std::array<GLboolean,15> enabled{};
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
layout(binding=0) uniform sampler2D uSurface;
#ifdef MOON_PREVIEW_SHADOWS
layout(binding=1) uniform sampler2DShadow uShadow;
uniform float uShadowBias;
#endif
uniform vec3 uLight;
uniform float uExposure;
uniform float uAmbient;
vec3 toSrgb(vec3 v)
{
    v=max(v,vec3(0.0));
    return mix(12.92*v,1.055*pow(v,vec3(1.0/2.4))-0.055,step(vec3(0.0031308),v));
}
void main()
{
    vec3 n=normalize(vNormal);
#ifdef MOON_PREVIEW_NORMALS
    outColor=vec4(n*0.5+0.5,1.0);
#else
    float diffuse=max(dot(n,uLight),0.0);
    float visibility=1.0;
#ifdef MOON_PREVIEW_SHADOWS
    vec3 shadow=vShadow.xyz/vShadow.w*0.5+0.5;
    // PCF taps must compare against the receiver's depth at the tap location,
    // not the central pixel's depth. Otherwise even a flat tilted triangle
    // shadows itself across the filter footprint. Derivatives follow the real
    // triangle plane, independently of the interpolated shading normal.
    vec2 texel=1.0/vec2(textureSize(uShadow,0));
    vec3 dx=dFdx(shadow),dy=dFdy(shadow);
    float determinant=dx.x*dy.y-dx.y*dy.x;
    vec2 gradient=vec2(0.0);
    if(abs(determinant)>1e-12)
        gradient=vec2(dx.z*dy.y-dy.z*dx.y,dx.x*dy.z-dy.x*dx.z)/determinant;
    // Each hardware bilinear comparison covers another half texel. Account
    // for that residual footprint with a bounded bias, not a wide soft blur.
    float footprint=min(dot(abs(gradient),texel)*0.75,0.012);
    float bias=uShadowBias+footprint;
    visibility=0.0;
    for(int y=-1;y<=1;++y) for(int x=-1;x<=1;++x)
    {
        vec2 offset=vec2(x,y)*texel;
        float receiver=shadow.z+clamp(dot(gradient,offset),-0.012,0.012);
        visibility+=texture(uShadow,vec3(shadow.xy+offset,receiver-bias));
    }
    visibility*=1.0/9.0;
#endif
    float albedo=texture(uSurface,vUv).a;
    float sky=mix(0.55,1.0,n.y*0.5+0.5);
    vec3 radiance=albedo*vColor*(uAmbient*sky+2.0*diffuse*visibility)*uExposure;
    outColor=vec4(toSrgb(radiance),1.0);
#endif
}
)GLSL";
const char* depthVertex=R"GLSL(
layout(location=0) in vec3 aPosition;
uniform mat4 uViewProjection;
void main() { gl_Position=uViewProjection*vec4(aPosition,1.0); }
)GLSL";
const char* depthFragment="void main() {}\n";

GLuint compile(GLenum type,const char* source,const char* defines,std::string& error)
{
    const GLuint shader=glCreateShader(type);
    if(!shader) { error="Impossible de creer un shader lunaire."; return 0; }
    const char* parts[]{"#version 450 core\n",defines,source};
    glShaderSource(shader,3,parts,nullptr); glCompileShader(shader);
    GLint success=0; glGetShaderiv(shader,GL_COMPILE_STATUS,&success);
    if(!success)
    {
        GLint length=0; glGetShaderiv(shader,GL_INFO_LOG_LENGTH,&length);
        std::vector<char> log(static_cast<std::size_t>((std::max)(length,1)),0);
        glGetShaderInfoLog(shader,length,nullptr,log.data());
        error="Shader de l'apercu lunaire : "+std::string(log.data());
        glDeleteShader(shader); return 0;
    }
    return shader;
}
GLuint program(const char* vs,const char* fs,const char* defines,std::string& error)
{
    const GLuint v=compile(GL_VERTEX_SHADER,vs,defines,error);
    if(!v) return 0;
    const GLuint f=compile(GL_FRAGMENT_SHADER,fs,defines,error);
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
            error="Edition des liens de l'apercu lunaire : "+std::string(log.data());
            glDeleteProgram(p); p=0;
        }
    }
    else error="Impossible de creer le programme lunaire.";
    glDeleteShader(v); glDeleteShader(f); return p;
}

struct Mesh
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
    void draw() const
    { glBindVertexArray(vao); glDrawElements(GL_TRIANGLES,count,GL_UNSIGNED_INT,nullptr); }
};
struct Target
{
    GLuint fbo=0,color=0,depth=0;
    int width=0,height=0;
    void clear()
    {
        if(fbo) glDeleteFramebuffers(1,&fbo);
        if(color) glDeleteTextures(1,&color);
        if(depth) glDeleteTextures(1,&depth);
        fbo=color=depth=0; width=height=0;
    }
    bool resize(int w,int h)
    {
        if(width==w && height==h && fbo) return true;
        if(!fbo) glCreateFramebuffers(1,&fbo);
        if(!color) glCreateTextures(GL_TEXTURE_2D,1,&color);
        if(!depth) glCreateTextures(GL_TEXTURE_2D,1,&depth);
        // Keep the color handle stable across resizes: ImGui may already have
        // queued an image command referring to this texture in the same frame.
        for(GLuint id:{color,depth})
        {
            glBindTexture(GL_TEXTURE_2D,id);
            const bool z=id==depth;
            glTexImage2D(GL_TEXTURE_2D,0,z?GL_DEPTH_COMPONENT24:GL_RGBA8,w,h,0,
                z?GL_DEPTH_COMPONENT:GL_RGBA,z?GL_UNSIGNED_INT:GL_UNSIGNED_BYTE,nullptr);
            glTextureParameteri(id,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
            glTextureParameteri(id,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
            glTextureParameteri(id,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
            glTextureParameteri(id,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        }
        glNamedFramebufferTexture(fbo,GL_COLOR_ATTACHMENT0,color,0);
        glNamedFramebufferTexture(fbo,GL_DEPTH_ATTACHMENT,depth,0);
        glNamedFramebufferDrawBuffer(fbo,GL_COLOR_ATTACHMENT0);
        glNamedFramebufferReadBuffer(fbo,GL_COLOR_ATTACHMENT0);
        if(!fbo || !color || !depth || glCheckNamedFramebufferStatus(fbo,GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE)
            return false;
        width=w; height=h; return true;
    }
};
}

struct MoonMeshPreview::Impl
{
    GLuint lit=0,shadowLit=0,normals=0,depthProgram=0;
    GLuint atlas=0,shadow=0,shadowFbo=0;
    Mesh mesh;
    Target target;
    glm::vec3 center{0};
    float radius=1;
    glm::mat4 lightProjection{1};
    MeshPreviewSettings previous;
    bool hasFrame=false,shadowValid=false;
    float shadowYaw=0,shadowElevation=0;
    std::string error;
    static constexpr int shadowSize=2048;
    void release()
    {
        mesh.clear(); target.clear();
        for(GLuint p:{lit,shadowLit,normals,depthProgram}) if(p) glDeleteProgram(p);
        for(GLuint t:{atlas,shadow}) if(t) glDeleteTextures(1,&t);
        if(shadowFbo) glDeleteFramebuffers(1,&shadowFbo);
        lit=shadowLit=normals=depthProgram=atlas=shadow=shadowFbo=0;
        hasFrame=shadowValid=false;
    }
    bool createShadow()
    {
        if(shadowFbo) return true;
        glCreateTextures(GL_TEXTURE_2D,1,&shadow);
        glTextureStorage2D(shadow,1,GL_DEPTH_COMPONENT24,shadowSize,shadowSize);
        glTextureParameteri(shadow,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
        glTextureParameteri(shadow,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        glTextureParameteri(shadow,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_BORDER);
        glTextureParameteri(shadow,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_BORDER);
        glTextureParameteri(shadow,GL_TEXTURE_COMPARE_MODE,GL_COMPARE_REF_TO_TEXTURE);
        glTextureParameteri(shadow,GL_TEXTURE_COMPARE_FUNC,GL_LEQUAL);
        const float border[]{1,1,1,1}; glTextureParameterfv(shadow,GL_TEXTURE_BORDER_COLOR,border);
        glCreateFramebuffers(1,&shadowFbo);
        glNamedFramebufferTexture(shadowFbo,GL_DEPTH_ATTACHMENT,shadow,0);
        glNamedFramebufferDrawBuffer(shadowFbo,GL_NONE);
        glNamedFramebufferReadBuffer(shadowFbo,GL_NONE);
        if(shadow && shadowFbo && glCheckNamedFramebufferStatus(shadowFbo,GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE)
            return true;
        if(shadowFbo) glDeleteFramebuffers(1,&shadowFbo);
        if(shadow) glDeleteTextures(1,&shadow);
        shadow=shadowFbo=0; return false;
    }
};

MoonMeshPreview::MoonMeshPreview():m(std::make_unique<Impl>()) {}
MoonMeshPreview::~MoonMeshPreview() { release(); }
void MoonMeshPreview::release() noexcept { m->release(); }
unsigned int MoonMeshPreview::texture() const noexcept { return m->target.color; }
int MoonMeshPreview::width() const noexcept { return m->target.width; }
int MoonMeshPreview::height() const noexcept { return m->target.height; }
const std::string& MoonMeshPreview::error() const noexcept { return m->error; }

bool MoonMeshPreview::initialize()
{
    if(m->lit && m->shadowLit && m->normals && m->depthProgram) return true;
    State state;
    m->error.clear();
    GLuint lit=program(vertexSource,fragmentSource,"",m->error);
    GLuint shadowLit=lit?program(vertexSource,fragmentSource,"#define MOON_PREVIEW_SHADOWS\n",m->error):0;
    GLuint normals=shadowLit?program(vertexSource,fragmentSource,"#define MOON_PREVIEW_NORMALS\n",m->error):0;
    GLuint depth=normals?program(depthVertex,depthFragment,"",m->error):0;
    if(!lit || !shadowLit || !normals || !depth)
    {
        for(GLuint p:{lit,shadowLit,normals,depth}) if(p) glDeleteProgram(p);
        return false;
    }
    m->lit=lit; m->shadowLit=shadowLit; m->normals=normals; m->depthProgram=depth;
    return true;
}

bool MoonMeshPreview::upload(const Geometry& geometry,const Surface& surface)
{
    m->error.clear();
    if(geometry.vertices.empty() || geometry.indices.empty() || geometry.indices.size()%3 ||
       geometry.vertices.size()>8000000 || geometry.indices.size()>24000000 ||
       surface.width<2 || surface.width>4096 || surface.height!=surface.width/2 ||
       surface.pixels.size()!=static_cast<std::size_t>(surface.width)*surface.height*4)
    { m->error="Geometrie ou atlas lunaire invalide."; return false; }
    for(auto index:geometry.indices) if(index>=geometry.vertices.size())
    { m->error="Indice lunaire hors du maillage."; return false; }
    glm::vec3 low{(std::numeric_limits<float>::max)()},high{-(std::numeric_limits<float>::max)()};
    for(const auto& vertex:geometry.vertices)
    {
        const glm::vec3 p=vector(vertex.position),n=vector(vertex.normal);
        for(int i=0;i<3;++i)
        {
            if(!finite(p[i]) || std::abs(p[i])>32 || !finite(n[i]) || !finite(vertex.color[i]))
            { m->error="Sommet lunaire invalide."; return false; }
            low[i]=(std::min)(low[i],p[i]); high[i]=(std::max)(high[i],p[i]);
        }
        if(!finite(vertex.u) || !finite(vertex.v) || glm::dot(n,n)<1e-8f)
        { m->error="Normale ou UV lunaire invalide."; return false; }
    }
    if(!initialize()) return false;
    State state; state.transfer();
    Mesh next;
    GLuint atlas=0;
    glCreateVertexArrays(1,&next.vao); glCreateBuffers(1,&next.vbo); glCreateBuffers(1,&next.ibo);
    const auto vb=static_cast<GLsizeiptr>(geometry.vertices.size()*sizeof(MeshVertex));
    const auto ib=static_cast<GLsizeiptr>(geometry.indices.size()*sizeof(std::uint32_t));
    glNamedBufferData(next.vbo,vb,geometry.vertices.data(),GL_STATIC_DRAW);
    glNamedBufferData(next.ibo,ib,geometry.indices.data(),GL_STATIC_DRAW);
    glVertexArrayVertexBuffer(next.vao,0,next.vbo,0,sizeof(MeshVertex));
    glVertexArrayElementBuffer(next.vao,next.ibo);
    const std::array<GLuint,4> offsets{{offsetof(MeshVertex,position),offsetof(MeshVertex,normal),
        offsetof(MeshVertex,u),offsetof(MeshVertex,color)}};
    const std::array<GLint,4> components{{3,3,2,4}};
    for(GLuint i=0;i<4;++i)
    {
        glEnableVertexArrayAttrib(next.vao,i);
        glVertexArrayAttribFormat(next.vao,i,components[i],GL_FLOAT,GL_FALSE,offsets[i]);
        glVertexArrayAttribBinding(next.vao,i,0);
    }
    next.count=static_cast<GLsizei>(geometry.indices.size());
    glCreateTextures(GL_TEXTURE_2D,1,&atlas);
    int levels=1;
    for(int size=surface.width;size>1;size/=2) ++levels;
    glTextureStorage2D(atlas,levels,GL_RGBA8,surface.width,surface.height);
    glTextureSubImage2D(atlas,0,0,0,surface.width,surface.height,GL_RGBA,GL_UNSIGNED_BYTE,surface.pixels.data());
    glGenerateTextureMipmap(atlas);
    glTextureParameteri(atlas,GL_TEXTURE_MIN_FILTER,GL_LINEAR_MIPMAP_LINEAR);
    glTextureParameteri(atlas,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glTextureParameteri(atlas,GL_TEXTURE_WRAP_S,GL_REPEAT);
    glTextureParameteri(atlas,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    GLint64 allocatedVb=0,allocatedIb=0;
    glGetNamedBufferParameteri64v(next.vbo,GL_BUFFER_SIZE,&allocatedVb);
    glGetNamedBufferParameteri64v(next.ibo,GL_BUFFER_SIZE,&allocatedIb);
    GLint allocatedWidth=0;
    glGetTextureLevelParameteriv(atlas,0,GL_TEXTURE_WIDTH,&allocatedWidth);
    if(!next.vao || !next.vbo || !next.ibo || !atlas || allocatedVb!=vb || allocatedIb!=ib || allocatedWidth!=surface.width)
    {
        next.clear(); if(atlas) glDeleteTextures(1,&atlas);
        m->error="Allocation GPU de la lune impossible. L'ancien apercu est conserve.";
        return false;
    }
    m->mesh.clear(); m->mesh=next;
    if(m->atlas) glDeleteTextures(1,&m->atlas);
    m->atlas=atlas;
    m->center=(low+high)*.5f;
    m->radius=(std::max)(glm::length(high-low)*.5f,0.1f)*1.02f;
    m->hasFrame=false; m->shadowValid=false;
    return true;
}

bool MoonMeshPreview::render(const MeshPreviewSettings& input)
{
    if(!m->mesh.count || !m->atlas)
    { m->error="Generez d'abord la geometrie lunaire."; return false; }
    const auto settings=sanitize(input);
    if(m->hasFrame && equal(settings,m->previous)) return true;
    State state; state.transfer(); state.draw();
    m->error.clear();
    if(!m->target.resize(settings.width,settings.height))
    { m->error="Creation du framebuffer lunaire impossible."; m->hasFrame=false; return false; }
    const glm::vec3 light=direction(settings.sunYaw,settings.sunElevation);
    const bool useShadow=settings.shadows && !settings.normals;
    if(useShadow && (!m->shadowValid || settings.sunYaw!=m->shadowYaw || settings.sunElevation!=m->shadowElevation))
    {
        if(!m->createShadow()) { m->error="Creation de la carte d'ombres lunaire impossible."; return false; }
        const float r=m->radius;
        const glm::vec3 up=std::abs(light.y)>.97f?glm::vec3(0,0,1):glm::vec3(0,1,0);
        m->lightProjection=glm::ortho(-r,r,-r,r,.1f*r,6.f*r)*
            glm::lookAt(m->center+light*(3.f*r),m->center,up);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER,m->shadowFbo);
        glViewport(0,0,Impl::shadowSize,Impl::shadowSize);
        float clearDepth=1;
        glClearNamedFramebufferfv(m->shadowFbo,GL_DEPTH,0,&clearDepth);
        glUseProgram(m->depthProgram);
        glUniformMatrix4fv(glGetUniformLocation(m->depthProgram,"uViewProjection"),1,GL_FALSE,glm::value_ptr(m->lightProjection));
        m->mesh.draw();
        m->shadowYaw=settings.sunYaw; m->shadowElevation=settings.sunElevation;
        m->shadowValid=true;
    }
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER,m->target.fbo);
    glViewport(0,0,settings.width,settings.height);
    float clear[]{srgb(settings.background[0]),srgb(settings.background[1]),srgb(settings.background[2]),1};
    float clearDepth=1;
    glClearNamedFramebufferfv(m->target.fbo,GL_COLOR,0,clear);
    glClearNamedFramebufferfv(m->target.fbo,GL_DEPTH,0,&clearDepth);
    // The authored body stays centered at the origin; detached fragments do
    // not move the orbit pivot as their seed or distribution changes.
    const glm::vec3 eye=direction(settings.yaw,settings.pitch)*settings.distance;
    const float farPlane=(std::max)(32.f,settings.distance+2.f*m->radius+glm::length(m->center));
    const glm::mat4 viewProjection=glm::perspective(glm::radians(40.f),float(settings.width)/settings.height,.02f,farPlane)*
        glm::lookAt(eye,glm::vec3(0),glm::vec3(0,1,0));
    const GLuint p=settings.normals?m->normals:(useShadow?m->shadowLit:m->lit);
    glUseProgram(p);
    glUniformMatrix4fv(glGetUniformLocation(p,"uViewProjection"),1,GL_FALSE,glm::value_ptr(viewProjection));
    glUniformMatrix4fv(glGetUniformLocation(p,"uLightProjection"),1,GL_FALSE,glm::value_ptr(m->lightProjection));
    glUniform3fv(glGetUniformLocation(p,"uLight"),1,glm::value_ptr(light));
    glUniform1f(glGetUniformLocation(p,"uExposure"),settings.exposure);
    glUniform1f(glGetUniformLocation(p,"uAmbient"),settings.ambient);
    // Bias is expressed in normalized shadow depth, independent of scene metres.
    glUniform1f(glGetUniformLocation(p,"uShadowBias"),.00004f);
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D,m->atlas);
    glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D,useShadow?m->shadow:0);
    if(settings.wireframe) glPolygonMode(GL_FRONT_AND_BACK,GL_LINE);
    m->mesh.draw();
    m->previous=settings; m->hasFrame=true;
    return true;
}
}
