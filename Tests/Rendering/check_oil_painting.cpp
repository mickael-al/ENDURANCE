#include "glcore.hpp"
#include "OilPainting.hpp"
#include "PostProcess.hpp"
#include "GraphicsDataMisc.hpp"
#include "GraphiquePipelineManager.hpp"
#include "Engine.hpp"
#include "ShapeBuffer.hpp"
#include <array>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

extern "C" void GLAPIENTRY __real_glDrawElements(GLenum,GLsizei,GLenum,const void*);

// The renderer is already PostProcess's friend; expose only its lifecycle here.
namespace Ge { class RenderingEngine { public:
    static bool init(PostProcess& p, GraphicsDataMisc* g) { return p.initialize(g); }
    static void draw(PostProcess& p, GLuint f, GLuint t, GLuint d, ShapeBuffer* q) { p.compute(f,t,d,q); }
    static void resize(PostProcess& p,int w,int h) { p.resize(w,h); }
    static void release(PostProcess& p) { p.release(); }
    static GLuint target(const PostProcess& p,int i) { return p.m_postTextures[i]; }
}; }
namespace
{
void require(bool value,const char* message) { if(!value) throw std::runtime_error(message); }
using Pixel=glm::vec4;
using Image=std::vector<Pixel>;
GLuint texture(int w,int h,const Image* image=nullptr,GLenum format=GL_RGBA32F)
{
    GLuint id=0; glGenTextures(1,&id); glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D,id);
    glTexImage2D(GL_TEXTURE_2D,0,format,w,h,0,GL_RGBA,GL_FLOAT,image?image->data():nullptr);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    return id;
}
Image read(GLuint id,int w,int h)
{
    Image result(w*h); glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D,id);
    glGetTexImage(GL_TEXTURE_2D,0,GL_RGBA,GL_FLOAT,result.data()); return result;
}
void attach(GLuint fbo,GLuint color)
{
    glBindFramebuffer(GL_FRAMEBUFFER,fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,color,0);
    require(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE,"Incomplete test framebuffer");
}
void compare(const Image& actual,const Image& expected,float tolerance,bool alpha=true)
{
    for(std::size_t i=0;i<actual.size();++i) for(int c=0;c<(alpha?4:3);++c)
        if(!std::isfinite(actual[i][c]) || std::abs(actual[i][c]-expected[i][c])>tolerance)
            throw std::runtime_error("Pixel mismatch at "+std::to_string(i)+" channel "+std::to_string(c)+
                ": "+std::to_string(actual[i][c])+" vs "+std::to_string(expected[i][c]));
}
// Independent double-precision histogram reference, including ties and borders.
int bin(const Pixel& p)
{
    const glm::dvec3 color=glm::clamp(glm::dvec3(p),glm::dvec3(0),glm::dvec3(1));
    return std::clamp(int(glm::dot(color,glm::dvec3(.2126,.7152,.0722))*24),0,23);
}
Image painted(const Image& input,int w,int h,int radius,float amount)
{
    Image result=input;
    for(int y=0;y<h;++y) for(int x=0;x<w;++x)
    {
        std::array<std::vector<Pixel>,24> bins;
        for(int j=-radius;j<=radius;++j) for(int i=-radius;i<=radius;++i)
            if(i*i+j*j<=radius*radius)
            {
                const auto p=input[std::clamp(y+j,0,h-1)*w+std::clamp(x+i,0,w-1)];
                bins[bin(p)].push_back(p);
            }
        const auto centre=bin(input[y*w+x]); int best=centre;
        for(int b=0;b<24;++b)
            if(bins[b].size()>bins[best].size() ||
               (bins[b].size()==bins[best].size() && std::abs(b-centre)<std::abs(best-centre))) best=b;
        glm::dvec3 sum(0); for(const auto& p:bins[best]) sum+=glm::dvec3(p);
        result[y*w+x]=Pixel(glm::mix(glm::vec3(input[y*w+x]),glm::vec3(sum/double(bins[best].size())),amount),input[y*w+x].a);
    }
    return result;
}

PFNGLBINDFRAMEBUFFERPROC realBind=nullptr;
GLuint presentation=0;
unsigned draws=0;
bool feedback=false;
bool checking=false;
void GLAPIENTRY bindTarget(GLenum target,GLuint id) { realBind(target,id?id:presentation); }
void GLAPIENTRY drawChecked(GLenum mode,GLsizei count,GLenum type,const void* indices)
{
    GLint attachment=0,source=0,active=0;
    glGetFramebufferAttachmentParameteriv(GL_DRAW_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME,&attachment);
    glGetIntegerv(GL_ACTIVE_TEXTURE,&active); glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D,&source); glActiveTexture(active);
    feedback|=(source && source==attachment); ++draws;
    __real_glDrawElements(mode,count,type,indices);
}
struct RedirectPresentation
{
    explicit RedirectPresentation(GLuint fbo)
    {
        presentation=fbo; draws=0; feedback=false;
        realBind=__glewBindFramebuffer; __glewBindFramebuffer=bindTarget;
        checking=true;
    }
    ~RedirectPresentation() { __glewBindFramebuffer=realBind; checking=false; }
};

void run(const char* root)
{
    std::filesystem::current_path(std::filesystem::path(root)/"ENDURANCE");
    Ge::GraphiquePipelineManager manager; TestSettings engineSettings;
    Ge::Engine::pointers={&manager,&engineSettings};
    GraphicsDataMisc graphics; Ge::Camera camera; graphics.current_camera=&camera;
    Ge::PostProcess post; require(Ge::RenderingEngine::init(post,&graphics),"PostProcess initialization failed");
    Ge::OilPainting* oil=nullptr;
    for(auto* module:post.getModules()) if(auto* match=dynamic_cast<Ge::OilPainting*>(module)) oil=match;
    Ge::OilPainting archivedOil;
#ifdef GE_ENABLE_OIL_PAINTING
    constexpr bool integrated=true;
    require(oil,"Oil paint missing from the editor's module list");
#else
    constexpr bool integrated=false;
    require(!oil,"Parked oil paint is still registered in the editor");
    require(manager.owned.size()==9,"Disabled oil paint initialized extra shaders");
    // Preserve numerical coverage of the corrected standalone filter, without
    // registering it in the production chain that must ignore old scene flags.
    require(archivedOil.initialize(&graphics),"Standalone oil initialization failed");
    oil=&archivedOil;
#endif
    require(!Ge::RenderingEngine::target(post,0),"Post targets allocated while all optional effects were off");
    auto* setting=post.getPPSetting();
    setting->bloom=false; setting->fog=false; setting->dof_max_radius=0;
    setting->exposure=.7f; setting->tint_color=glm::vec3(.7f,.85f,.95f); setting->tint_strength=.6f;
    Ge::PPSetting copied; setting->oilp_strength=.37f; copied.copy(*setting);
    require(copied.oilp_strength==.37f,"Scene setting copy lost oil strength");

    Ge::ShapeBuffer quad; GLuint vertices=0;
    const float points[]={-1,-1,0,0, 1,-1,1,0, 1,1,1,1, -1,1,0,1};
    const unsigned indices[]={0,1,2,0,2,3};
    glGenVertexArrays(1,&quad.vao); glBindVertexArray(quad.vao);
    glGenBuffers(1,&vertices); glBindBuffer(GL_ARRAY_BUFFER,vertices); glBufferData(GL_ARRAY_BUFFER,sizeof(points),points,GL_STATIC_DRAW);
    const GLint position=glGetAttribLocation(manager.owned[0]->getProgram(),"a_Position");
    const GLint uv=glGetAttribLocation(manager.owned[0]->getProgram(),"a_TexCoords");
    require(position>=0 && uv>=0,"Fullscreen attributes missing");
    for(const auto* program:manager.owned)
        require(glGetAttribLocation(program->getProgram(),"a_Position")==position &&
                (glGetAttribLocation(program->getProgram(),"a_TexCoords")==uv ||
                 glGetAttribLocation(program->getProgram(),"a_TexCoords")==-1),"Mismatched fullscreen attributes");
    glEnableVertexAttribArray(position); glVertexAttribPointer(position,2,GL_FLOAT,GL_FALSE,4*sizeof(float),nullptr);
    glEnableVertexAttribArray(uv); glVertexAttribPointer(uv,2,GL_FLOAT,GL_FALSE,4*sizeof(float),reinterpret_cast<void*>(2*sizeof(float)));
    glGenBuffers(1,&quad.ibo); glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,quad.ibo); glBufferData(GL_ELEMENT_ARRAY_BUFFER,sizeof(indices),indices,GL_STATIC_DRAW);
    GLuint output=0,sourceFbo=0; glGenFramebuffers(1,&output); glGenFramebuffers(1,&sourceFbo);
    unsigned comparisons=0,chainCases=0;
    const auto programs=manager.owned.size();
    for(const auto size:{glm::ivec2(31,19),glm::ivec2(2,3),glm::ivec2(1,1)})
    {
        const int w=size.x,h=size.y; graphics.str_width=w; graphics.str_height=h;
        Ge::RenderingEngine::resize(post,w,h);
        Image input(w*h);
        for(int i=0;i<w*h;++i) input[i]=Pixel(float((i*137u)%59u)/65.f,float((i*41u)%59u)/63.f,
            float((i*31u)%59u)/61.f,.23f+float(i%7)*.1f);
        input[0]=Pixel(3.f,-.4f,12.f,.8f); // Out-of-range colors must never index beyond the histogram.
        GLuint src=texture(w,h,&input),target=texture(w,h),depth=texture(w,h);
        attach(output,target); attach(sourceFbo,src);
        setting->oil_painting=true;
        for(unsigned radius=0;radius<=5;++radius) for(float amount:{0.f,.65f,1.f})
        {
            setting->oilp_radius=radius==5?std::numeric_limits<unsigned>::max():radius;
            setting->oilp_strength=amount;
            oil->compute(output,src,depth,&quad,setting);
            compare(read(target,w,h),painted(input,w,h,std::min(radius,4u),amount),.00001f);
            require(glGetError()==GL_NO_ERROR,"Oil pass OpenGL error"); ++comparisons;
        }
        setting->oilp_radius=2; setting->oilp_strength=std::numeric_limits<float>::quiet_NaN();
        oil->compute(output,src,depth,&quad,setting);
        compare(read(target,w,h),painted(input,w,h,2,.65f),.00001f); ++comparisons;

        // Test real final presentation with all DoF/oil/tint combinations. DoF's
        // zero blur still exercises its composite and every routing branch.
        for(auto& p:input) p=Pixel(glm::clamp(glm::vec3(p),glm::vec3(.04f),glm::vec3(.95f)),1);
        glBindTexture(GL_TEXTURE_2D,src); glTexSubImage2D(GL_TEXTURE_2D,0,0,0,w,h,GL_RGBA,GL_FLOAT,input.data());
        glDeleteTextures(1,&target); target=texture(w,h,nullptr,GL_RGBA16F); attach(output,target);
        Image baseline;
        for(unsigned combination:{0u,2u,1u,3u,4u,5u,6u,7u})
        {
            setting->depth_of_field=(combination&1)!=0; setting->oil_painting=(combination&2)!=0;
            setting->color_tint=(combination&4)!=0; setting->oilp_radius=2; setting->oilp_strength=.65f;
            { RedirectPresentation redirect(output); Ge::RenderingEngine::draw(post,sourceFbo,src,depth,&quad); }
            const auto actual=read(target,w,h);
            if(!combination) baseline=actual;
            const bool useOil=integrated && setting->oil_painting;
            Image expected=useOil?painted(baseline,w,h,2,.65f):baseline;
            if(setting->color_tint) for(auto& p:expected)
                p=Pixel(glm::vec3(p)*glm::mix(glm::vec3(1),setting->tint_color,setting->tint_strength),p.a);
            compare(actual,expected,.0015f,false);
            require(draws==1u+bool(combination&1)+useOil+bool(combination&4),"Unexpected fullscreen pass count");
            if(!integrated && combination==2 && w==31)
            {
                compare(actual,baseline,0,false);
                require(!Ge::RenderingEngine::target(post,0),"Old oil scene flag allocated post targets");
            }
            require(!feedback,"Postprocess samples its own draw attachment");
            require(glGetError()==GL_NO_ERROR,"Postprocess OpenGL error"); ++chainCases;
        }
        // Both zero-strength and zero-radius skip the pass entirely, retaining
        // exactly the plain tone-mapped image and one presentation draw.
        setting->depth_of_field=false; setting->color_tint=false; setting->oil_painting=true;
        for(int bypass=0;bypass<2;++bypass)
        {
            setting->oilp_strength=bypass?.65f:0.f; setting->oilp_radius=bypass?0:2;
            { RedirectPresentation redirect(output); Ge::RenderingEngine::draw(post,sourceFbo,src,depth,&quad); }
            compare(read(target,w,h),baseline,0,false);
            require(draws==1,"Bypassed oil still issued a draw");
        }
        require(manager.owned.size()==programs,"Oil controls compiled new shaders during rendering");
        glDeleteTextures(1,&src); glDeleteTextures(1,&target); glDeleteTextures(1,&depth);
    }
    const GLuint intermediate[]={Ge::RenderingEngine::target(post,0),Ge::RenderingEngine::target(post,1)};
    Ge::RenderingEngine::release(post);
    archivedOil.release();
    require(manager.owned.empty(),"Postprocess leaked pipelines");
    for(auto id:intermediate) require(!glIsTexture(id),"Postprocess leaked color targets");
    Ge::RenderingEngine::release(post);
    glDeleteFramebuffers(1,&sourceFbo); glDeleteFramebuffers(1,&output);
    glDeleteBuffers(1,&vertices); glDeleteBuffers(1,&quad.ibo); glDeleteVertexArrays(1,&quad.vao);
    std::cout<<"PASS oil paint (integration "<<(integrated?"enabled":"disabled")<<"): "<<comparisons<<" image comparisons, "<<chainCases
        <<" postprocess combinations, HDR bounds, alpha, resize, bypass, no feedback, release\n"<<std::flush;
}
}
extern "C" void GLAPIENTRY __wrap_glDrawElements(GLenum mode,GLsizei count,GLenum type,const void* indices)
{
    if(checking) drawChecked(mode,count,type,indices);
    else __real_glDrawElements(mode,count,type,indices);
}
extern "C" __attribute__((visibility("default"))) int oil_runtime_tests(const char* root)
{
    try
    {
        glewExperimental=GL_TRUE; const auto status=glewInit();
        require(status==GLEW_OK || status==GLEW_ERROR_NO_GLX_DISPLAY,"GLEW initialization failed");
        while(glGetError()!=GL_NO_ERROR) {} run(root); return 0;
    }
    catch(const std::exception& e) { std::cerr<<"FAIL oil paint: "<<e.what()<<'\n'; return 1; }
}
