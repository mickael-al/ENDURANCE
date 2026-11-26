// Production mesh rasterization and complete atmosphere run on an actual GL
// context. Only camera/light/window ownership uses existing runtime fixtures.
#include "glcore.hpp"
#include "Atmosphere.hpp"
#include "MoonMeshCache.hpp"
#include "EnduranceMoonMesh.hpp"
#include "Camera.hpp"
#include "LightManager.hpp"
#include "PointeurClass.hpp"
#include "GraphicsDataMisc.hpp"
#include "ShaderCompilation.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace
{
void require(bool value,const std::string& message)
{ if(!value) throw std::runtime_error(message); }
void checkGL(const char* label)
{ const auto e=glGetError(); require(e==GL_NO_ERROR,std::string(label)+": GL error "+std::to_string(e)); }
std::vector<float> read(GLuint texture,bool cube=false)
{
    GLint w=0,h=0; glGetTextureLevelParameteriv(texture,0,GL_TEXTURE_WIDTH,&w);
    glGetTextureLevelParameteriv(texture,0,GL_TEXTURE_HEIGHT,&h);
    require(w>0 && h>0,"Missing allocated lunar texture");
    std::vector<float> data(std::size_t(w)*h*4*(cube?6:1));
    glGetTextureImage(texture,0,GL_RGBA,GL_FLOAT,GLsizei(data.size()*sizeof(float)),data.data());
    for(float value:data) require(std::isfinite(value),"Nonfinite lunar texel");
    checkGL("lunar readback"); return data;
}
float difference(const std::vector<float>& a,const std::vector<float>& b)
{
    require(a.size()==b.size(),"Image size changed"); float result=0;
    for(std::size_t i=0;i<a.size();++i) result=std::max(result,std::abs(a[i]-b[i]));
    return result;
}
void quad(Ge::MoonMeshData& mesh,float x0,float y0,float x1,float y1,float z=0)
{
    const auto base=std::uint32_t(mesh.vertices.size());
    for(const auto& p:{glm::vec3(x0,y0,z),glm::vec3(x1,y0,z),glm::vec3(x1,y1,z),glm::vec3(x0,y1,z)})
        mesh.vertices.push_back({p,{0,0,1},{.5f,.5f},{1,1,1,1}});
    for(auto i:{0u,1u,2u,0u,2u,3u}) mesh.indices.push_back(base+i);
}
Ge::MoonMeshData piercedMesh()
{
    Ge::MoonMeshData mesh; mesh.width=mesh.height=2;
    mesh.albedoRgba.assign(16,255); mesh.meanAlbedo=1;
    quad(mesh,-1,-1,-.3f,1); quad(mesh,.3f,-1,1,1);
    quad(mesh,-.3f,-1,.3f,-.3f); quad(mesh,-.3f,.3f,.3f,1);
    quad(mesh,1.5f,.1f,1.9f,.5f,.1f); // separated fragment outside unit body
    return mesh;
}
glm::vec4 sample(const Ge::MoonMeshCache& cache,const std::vector<float>& pixels,
    glm::vec3 observer,glm::vec3 point)
{
    const auto ray=glm::normalize(point-observer);
    const auto r=cache.projectionRight(),u=cache.projectionUp(),f=cache.projectionForward();
    const float divisor=glm::dot(ray,glm::vec3(f))*r.w;
    const glm::vec2 uv=.5f+.5f*glm::vec2(glm::dot(ray,glm::vec3(r)),glm::dot(ray,glm::vec3(u)))/divisor;
    const int n=cache.resolution();
    require(uv.x>0 && uv.x<1 && uv.y>0 && uv.y<1,"Expected point excluded from cache projection");
    const auto index=4*(std::size_t(int(uv.y*n))*n+int(uv.x*n));
    return {pixels[index],pixels[index+1],pixels[index+2],pixels[index+3]};
}
void saveCapture(const Ge::MoonMeshCache& cache,const std::vector<float>& pixels)
{
    const char* directory=std::getenv("ENDURANCE_MOON_CAPTURE_DIR"); if(!directory) return;
    const auto path=std::filesystem::path(directory)/"moon-runtime.ppm";
    std::ofstream output(path,std::ios::binary); const int n=cache.resolution();
    output<<"P6\n"<<n<<' '<<n<<"\n255\n";
    for(int y=n-1;y>=0;--y) for(int x=0;x<n;++x)
    {
        const auto i=4*(std::size_t(y)*n+x);
        for(int c=0;c<3;++c)
        {
            const float value=std::clamp(pixels[i+c]*3.14159265f+.025f*(1-pixels[i+3]),0.f,1.f);
            const auto byte=static_cast<unsigned char>(std::pow(value,1.f/2.2f)*255+.5f);
            output.write(reinterpret_cast<const char*>(&byte),1);
        }
    }
    require(bool(output),"Cannot save actual lunar runtime capture");
}
void cacheTests(const Ge::MoonMeshData& baked)
{
    Ge::MoonMeshCache cache; std::string error;
    require(cache.setMesh(piercedMesh(),error),error);
    const glm::vec3 observer{0,0,6},light{0,0,1};
    glViewport(3,7,31,23); glEnable(GL_SCISSOR_TEST); glScissor(2,5,7,9);
    glEnable(GL_BLEND); glDepthMask(GL_FALSE); glDepthFunc(GL_GEQUAL);
    glFrontFace(GL_CW); glCullFace(GL_FRONT); glEnable(GL_CULL_FACE);
    glColorMask(GL_FALSE,GL_TRUE,GL_FALSE,GL_TRUE); glEnable(GL_RASTERIZER_DISCARD);
    glActiveTexture(GL_TEXTURE5); glPixelStorei(GL_UNPACK_ALIGNMENT,8);
    require(cache.update(observer,0,light,512,error),error);
    GLint viewport[4],depth=0,front=0,cull=0,active=0,unpack=0; GLboolean mask[4],depthMask=1;
    glGetIntegerv(GL_VIEWPORT,viewport); glGetIntegerv(GL_DEPTH_FUNC,&depth);
    glGetIntegerv(GL_FRONT_FACE,&front); glGetIntegerv(GL_CULL_FACE_MODE,&cull);
    glGetIntegerv(GL_ACTIVE_TEXTURE,&active); glGetIntegerv(GL_UNPACK_ALIGNMENT,&unpack);
    glGetBooleanv(GL_COLOR_WRITEMASK,mask); glGetBooleanv(GL_DEPTH_WRITEMASK,&depthMask);
    require(viewport[0]==3 && viewport[1]==7 && viewport[2]==31 && viewport[3]==23 &&
        depth==GL_GEQUAL && front==GL_CW && cull==GL_FRONT && active==GL_TEXTURE5 && unpack==8 &&
        !depthMask && !mask[0] && mask[1] && !mask[2] && mask[3] && glIsEnabled(GL_SCISSOR_TEST) &&
        glIsEnabled(GL_BLEND) && glIsEnabled(GL_RASTERIZER_DISCARD),"Moon cache leaked GL state");
    glDisable(GL_RASTERIZER_DISCARD); glDisable(GL_SCISSOR_TEST); glDisable(GL_BLEND);
    glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE); glDepthMask(GL_TRUE); glDepthFunc(GL_LESS);
    glFrontFace(GL_CCW); glCullFace(GL_BACK); glDisable(GL_CULL_FACE); glActiveTexture(GL_TEXTURE0);
    glPixelStorei(GL_UNPACK_ALIGNMENT,4);
    auto lit=read(cache.texture());
    require(sample(cache,lit,observer,{0,0,0}).w<.01f,"Geometric hole filled by spherical coverage");
    require(sample(cache,lit,observer,{-.6f,0,0}).w>.99f,"Moon body coverage missing");
    require(sample(cache,lit,observer,{1.7f,.3f,.1f}).w>.99f,"Detached fragment clipped to original sphere");
    require(sample(cache,lit,observer,{-.6f,0,0}).x>.25f,"Unit-light Lambertian surface is too dark");
    auto image=cache.imageUpdates(),shadow=cache.shadowUpdates();
    for(int i=0;i<100;++i) require(cache.update(observer,0,light,512,error),error);
    require(cache.imageUpdates()==image && cache.shadowUpdates()==shadow,"Stationary cache redrew geometry");
    require(cache.update(observer+glm::vec3(.000001f,0,0),0,light,512,error),error);
    require(cache.imageUpdates()==image,"Subpixel observer change redrew cache");
    int cumulativeSteps=0;
    while(cache.imageUpdates()==image && cumulativeSteps<10000)
    {
        ++cumulativeSteps;
        require(cache.update(observer+glm::vec3(.00001f*cumulativeSteps,0,0),0,light,512,error),error);
    }
    require(cumulativeSteps>1 && cache.imageUpdates()==image+1 && cache.shadowUpdates()==shadow,
        "Small observer movements did not accumulate against last rendered view");
    image=cache.imageUpdates();
    require(cache.update(observer+glm::vec3(.2f,0,0),0,light,512,error),error);
    require(cache.imageUpdates()==image+1 && cache.shadowUpdates()==shadow,"Observer move rebuilt solar shadow or missed view update");
    require(cache.update(observer,0,-light,512,error),error);
    auto dark=read(cache.texture());
    require(sample(cache,dark,observer,{-.6f,0,0}).w>.99f && sample(cache,dark,observer,{-.6f,0,0}).x<1e-5f,
        "Dark moon lost opaque silhouette or gained artificial emission");
    require(cache.shadowUpdates()==shadow+1,"Solar direction did not refresh lunar shadows");
    require(cache.setMesh(baked,error),error);
    const auto start=std::chrono::steady_clock::now();
    require(cache.update({0,0,6},0,glm::normalize(glm::vec3(-.5f,.6f,1)),1024,error),error);
    glFinish();
    const auto milliseconds=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
    const auto real=read(cache.texture()); saveCapture(cache,real);
    float coverage=0; for(std::size_t i=3;i<real.size();i+=4) coverage+=real[i];
    require(coverage>1000 && coverage<1024.f*1024.f*.9f,"Actual baked lunar mesh empty or fills entire cache");
    require(cache.gpuBytes()>0 && cache.gpuBytes()<128ull*1024*1024,"Lunar GPU allocation exceeds bounded 128 MiB budget");
    image=cache.imageUpdates(); shadow=cache.shadowUpdates();
    for(int i=0;i<1000;++i) require(cache.update({0,0,6},0,glm::normalize(glm::vec3(-.5f,.6f,1)),1024,error),error);
    require(cache.imageUpdates()==image && cache.shadowUpdates()==shadow,"Real model performs work on cache hits");
    const auto owned=cache.texture(); cache.clear();
    require(!cache.valid() && cache.gpuBytes()==0 && !glIsTexture(owned),"Lunar cache release leaked resources");
    checkGL("mesh cache tests");
    std::cout<<"PASS mesh cache: real holes/fragments, dark coverage, GL state, stationary hits, independent view/shadow updates; actual "
        <<baked.indices.size()/3<<" triangles cold update "<<milliseconds<<" ms on this test renderer\n"<<std::flush;
}
struct Targets
{
    GLuint fbo=0,textures[4]{};
    Targets()
    {
        glCreateFramebuffers(1,&fbo); glCreateTextures(GL_TEXTURE_2D,4,textures);
        const GLenum attachments[]{GL_COLOR_ATTACHMENT0,GL_COLOR_ATTACHMENT1,GL_COLOR_ATTACHMENT2,GL_COLOR_ATTACHMENT3};
        for(int i=0;i<4;++i)
        { glTextureStorage2D(textures[i],1,GL_RGBA32F,64,36); glNamedFramebufferTexture(fbo,attachments[i],textures[i],0); }
        glNamedFramebufferDrawBuffers(fbo,4,attachments); glBindFramebuffer(GL_FRAMEBUFFER,fbo); glViewport(0,0,64,36);
        require(glCheckNamedFramebufferStatus(fbo,GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE,"Test sky targets incomplete");
    }
    ~Targets() { glBindFramebuffer(GL_FRAMEBUFFER,0); glDeleteFramebuffers(1,&fbo); glDeleteTextures(4,textures); }
};
Ge::AtmosphereSettings settings()
{
    Ge::AtmosphereSettings s; s.enabled=true; s.quality=Ge::AtmosphereQuality::Low;
    s.physicalAtmosphere=false; s.clouds=false; s.localFog=false; s.godRays=false;
    s.cubemapSize=32; s.prefilterSize=32; s.irradianceSize=4; s.iblSamples=8;
    s.volumeWidth=16; s.volumeHeight=9; s.volumeDepth=16; s.skySteps=8;
    s.moonSurface=true; s.moonAngularRadius=10; s.moonDiskIntensity=1; s.moonIlluminanceScale=1;
    s.moonMeshResolution=512; s.sunDiskIntensity=0; s.wind={0,0}; s.measureGpu=false;
    s.environmentInterval=0; s.environmentBlendSeconds=0; s.iblMaxWorkUnitsPerFrame=64;
    s.targetGpuMilliseconds=1000; return s;
}
GLuint textureAt(unsigned unit,GLenum target)
{
    glActiveTexture(GL_TEXTURE0+unit); GLint id=0; glGetIntegerv(target,&id); glActiveTexture(GL_TEXTURE0); return GLuint(id);
}
struct Runtime
{
    Ge::LightManager lights; Ge::Lights sun,moon; ptrClass pc;
    GraphicsDataMisc graphics; Ge::Camera camera; Ge::Atmosphere atmosphere; Targets target;
    Runtime()
    {
        sun.direction={0,.2f,-1}; sun.shadow=0;
        moon.direction={0,-.2f,1}; moon.index=1; moon.shadow=4;
        lights.lights={&sun,&moon}; lights.shadowFrames.resize(8); pc.lightManager=&lights;
        graphics.str_width=64; graphics.str_height=36; camera.position={0,0,0}; camera.forward={0,.2f,-1};
        atmosphere.initialize(&pc,&graphics); atmosphere.settings()=settings(); atmosphere.setSun(&sun); atmosphere.setMoon(&moon);
        Ge::AtmospherePalette palette; palette.strength=0; palette.exposure=1; atmosphere.setPaletteTarget(palette,true);
        const auto mesh=piercedMesh(); require(atmosphere.setMoonMesh(&mesh),"Atmosphere rejected mesh");
    }
    void frame(double seconds=0)
    {
        atmosphere.prepare(&camera,seconds); atmosphere.renderSky(0);
        require(atmosphere.active(),"Atmosphere inactive: "+atmosphere.error()); checkGL("mesh atmosphere frame");
    }
    void settle()
    {
        for(int i=0;i<300;++i)
        {
            frame(); const auto& s=atmosphere.stats();
            if(s.environmentReady && !s.environmentCaptureActive && s.publishedEnvironmentRevision==s.requestedEnvironmentRevision) return;
        }
        throw std::runtime_error("Lunar IBL did not settle");
    }
};
void atmosphereTests()
{
    Runtime r; r.settle(); auto before=r.atmosphere.stats();
    r.camera.forward={.3f,.1f,-1}; r.camera.fov=55; r.frame(7);
    require(r.atmosphere.stats().moonImageUpdates==before.moonImageUpdates &&
        r.atmosphere.stats().moonShadowUpdates==before.moonShadowUpdates,"Pan/FOV/time rebuilt Moon mesh");
    r.sun.range=17; r.atmosphere.settings().moonDiskColor={.7f,.8f,.9f}; r.frame();
    require(r.atmosphere.stats().moonImageUpdates==before.moonImageUpdates,"Solar intensity or tint rebuilt geometry cache");
    r.sun.direction={.6f,.4f,-1}; r.frame();
    require(r.atmosphere.stats().moonImageUpdates==before.moonImageUpdates+1 &&
        r.atmosphere.stats().moonShadowUpdates==before.moonShadowUpdates+1,"Sun direction did not relight model");
    before=r.atmosphere.stats(); r.camera.position.x+=10000; r.frame();
    require(r.atmosphere.stats().moonImageUpdates==before.moonImageUpdates+1 &&
        r.atmosphere.stats().moonShadowUpdates==before.moonShadowUpdates,"Parallax redrew lunar solar shadows");
    r.atmosphere.computeVolumes(&r.camera);
    require(!r.atmosphere.shadowLayerActive(0) && r.atmosphere.shadowLayerActive(4),"Night should select Moon shadow cascades only");
    r.sun.direction={.6f,-.4f,-1}; r.frame(); r.atmosphere.computeVolumes(&r.camera);
    require(r.atmosphere.shadowLayerActive(0) && !r.atmosphere.shadowLayerActive(4),"Day should select solar shadow cascades only");
    r.atmosphere.release();
    require(r.atmosphere.stats().moonMeshGpuBytes==0,"Atmosphere release retained lunar mesh resources");
    std::cout<<"PASS atmosphere mesh integration: pan/FOV/time/intensity cache hits, relighting/parallax and single day/night shadow source\n"<<std::flush;
}
void immutableCaptureTest()
{
    std::string error;
    const GLuint probe=Ge::buildShaderProgram({{GL_COMPUTE_SHADER,"../Tests/Atmosphere/runtime_probe.C.glsl",{}}},error);
    require(probe!=0,error);
    std::vector<float> reference;
    {
        Runtime r; r.settle(); r.atmosphere.bindLighting(probe);
        reference=read(textureAt(7,GL_TEXTURE_BINDING_CUBE_MAP),true);
    }
    {
        Runtime r; r.atmosphere.settings().iblMaxWorkUnitsPerFrame=1; r.frame();
        require(r.atmosphere.stats().environmentCaptureActive && r.atmosphere.stats().captureWorkCompleted==1,
            "Expected frozen lunar snapshot before cube tiles");
        const auto frozen=r.atmosphere.stats().captureRevision;
        r.sun.direction={1,.1f,0}; r.frame();
        require(r.atmosphere.stats().moonImageUpdates==2,"Live lunar image did not relight during capture");
        for(int i=0;i<300 && !r.atmosphere.stats().environmentReady;++i) r.frame();
        require(r.atmosphere.stats().environmentReady && r.atmosphere.stats().publishedEnvironmentRevision==frozen,
            "Sun edit cancelled or replaced an immutable capture");
        r.atmosphere.bindLighting(probe);
        const auto first=read(textureAt(7,GL_TEXTURE_BINDING_CUBE_MAP),true);
        require(difference(reference,first)<.003f,"IBL cube mixed old projection/sun with live lunar image");
        r.settle(); r.atmosphere.bindLighting(probe);
        require(difference(first,read(textureAt(7,GL_TEXTURE_BINDING_CUBE_MAP),true))>.001f,
            "Latest lunar relight never reached reflection IBL");
    }
    glDeleteProgram(probe); checkGL("frozen moon capture");
    std::cout<<"PASS immutable lunar IBL: first capture matches frozen reference during live relight, following capture publishes new lighting\n"<<std::flush;
}
}
extern "C" __attribute__((visibility("default"))) int moon_mesh_runtime_tests(const char* root)
{
    try
    {
        glewExperimental=GL_TRUE; const auto status=glewInit();
        require(status==GLEW_OK || status==GLEW_ERROR_NO_GLX_DISPLAY,"GLEW initialization failed");
        while(glGetError()!=GL_NO_ERROR) {}
        std::filesystem::current_path(std::filesystem::path(root)/"ENDURANCE");
        Ge::MoonMeshData baked; std::string error;
        require(EnduranceMoon::loadMoonMesh("../ENDURANCE/Ressources/Model/Moon/Baked/moon.glb",baked,error),error);
        cacheTests(baked); atmosphereTests(); immutableCaptureTest(); return 0;
    }
    catch(const std::exception& e) { std::cerr<<"FAIL moon mesh runtime: "<<e.what()<<'\n'; return 1; }
}
