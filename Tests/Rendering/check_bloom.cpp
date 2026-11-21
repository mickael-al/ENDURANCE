#include "glcore.hpp"
#include "Bloom.hpp"
#include "GraphicsDataMisc.hpp"
#include "GraphiquePipelineManager.hpp"
#include "Engine.hpp"
#include "ShapeBuffer.hpp"
#include <array>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <vector>
namespace
{
void require(bool condition,const char* message) { if(!condition) throw std::runtime_error(message); }
GLuint texture(int width,int height,GLenum format=GL_R11F_G11F_B10F,const void* data=nullptr)
{
    GLuint result=0; glGenTextures(1,&result); glBindTexture(GL_TEXTURE_2D,result);
    glTexImage2D(GL_TEXTURE_2D,0,format,width,height,0,GL_RGB,GL_FLOAT,data);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    return result;
}
std::vector<float> read(GLuint value,int width,int height)
{
    std::vector<float> pixels(width*height*3); glBindTexture(GL_TEXTURE_2D,value);
    glGetTexImage(GL_TEXTURE_2D,0,GL_RGB,GL_FLOAT,pixels.data()); return pixels;
}
void run(const char* root)
{
    std::filesystem::current_path(std::filesystem::path(root)/"ENDURANCE");
    Ge::GraphiquePipelineManager manager; Ge::Engine::pointers.graphiquePipelineManager=&manager;
    GraphicsDataMisc graphics; Ge::Bloom bloom; require(bloom.initialize(&graphics),"Bloom initialization failed");
    require(manager.owned.size()==5,"Bloom did not build exactly five bounded programs");
    require(glGetUniformLocation(manager.owned[0]->getProgram(),"threshold")==-1 &&
            glGetUniformLocation(manager.owned[1]->getProgram(),"intensity")==-1,
            "Disabled bloom/threshold code survived specialization");
    std::string error; std::array<GLuint,3> reference{};
    const char* files[]={"downsampling.fs.glsl","upsampling.fs.glsl","tomeMapping.fs.glsl"};
    for(unsigned i=0;i<3;++i)
    {
        reference[i]=Ge::buildShaderProgram({{GL_VERTEX_SHADER,"../Asset/Shader/pp.vs.glsl",{}},
            {GL_FRAGMENT_SHADER,std::string("../Asset/Shader/")+files[i],{}}},error);
        if(!reference[i]) throw std::runtime_error(error);
    }
    Ge::ShapeBuffer quad; GLuint vertices=0;
    const float points[]={-1,-1,0,0, 1,-1,1,0, 1,1,1,1, -1,1,0,1};
    const unsigned indices[]={0,1,2,0,2,3};
    glGenVertexArrays(1,&quad.vao); glBindVertexArray(quad.vao);
    glGenBuffers(1,&vertices); glBindBuffer(GL_ARRAY_BUFFER,vertices); glBufferData(GL_ARRAY_BUFFER,sizeof(points),points,GL_STATIC_DRAW);
    const auto position=glGetAttribLocation(reference[0],"a_Position"),uv=glGetAttribLocation(reference[0],"a_TexCoords");
    require(position>=0 && uv>=0,"Postprocess vertex attributes are missing");
    for(auto* program:manager.owned)
        require(glGetAttribLocation(program->getProgram(),"a_Position")==position &&
                glGetAttribLocation(program->getProgram(),"a_TexCoords")==uv,
                "Bloom specialization changed the shared fullscreen vertex layout");
    glEnableVertexAttribArray(position); glVertexAttribPointer(position,2,GL_FLOAT,GL_FALSE,4*sizeof(float),nullptr);
    glEnableVertexAttribArray(uv); glVertexAttribPointer(uv,2,GL_FLOAT,GL_FALSE,4*sizeof(float),reinterpret_cast<void*>(2*sizeof(float)));
    glGenBuffers(1,&quad.ibo); glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,quad.ibo); glBufferData(GL_ELEMENT_ARRAY_BUFFER,sizeof(indices),indices,GL_STATIC_DRAW);
    GLuint processing=0,outputFbo=0; glGenFramebuffers(1,&processing); glGenFramebuffers(1,&outputFbo);
    bloom.setOutputFrameBuffer(outputFbo);
    unsigned cases=0;
    for(const auto size:{glm::ivec2(64,32),glm::ivec2(37,19),glm::ivec2(2,3)})
    {
        graphics.str_width=size.x; graphics.str_height=size.y; bloom.resize(size.x,size.y);
        std::vector<float> input(size.x*size.y*3);
        for(std::size_t i=0;i<input.size();++i) input[i]=float((i*137u)%59u)/17.0f;
        const auto scene=texture(size.x,size.y,GL_RGB32F,input.data());
        const auto actual=texture(size.x,size.y,GL_RGB32F),expected=texture(size.x,size.y,GL_RGB32F);
        std::array<GLuint,5> mips{}; std::array<glm::ivec2,5> sizes{}; auto mipSize=size;
        for(unsigned i=0;i<5;++i) { mipSize=glm::max(mipSize/2,glm::ivec2(1)); sizes[i]=mipSize; mips[i]=texture(mipSize.x,mipSize.y); }
        for(unsigned scenario=0;scenario<5;++scenario)
        {
            Ge::PPSetting settings; settings.bloom=scenario!=0; settings.bloom_intensity=scenario==1?0.f:1.7f;
            settings.bloom_threshold=scenario==2?-1.f:(scenario==3?0.f:1.2f);
            settings.bloom_filter=.015f; settings.exposure=.7f; float gamma=1.9f; settings.gamma=&gamma;
            glBindFramebuffer(GL_FRAMEBUFFER,outputFbo); glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,actual,0);
            glBindFramebuffer(GL_FRAMEBUFFER,processing); glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,scene,0);
            bloom.compute(processing,scene,0,&quad,&settings);
            const auto observed=read(actual,size.x,size.y);
            require(manager.owned.size()==5,"Toggling scene settings grew bloom shader variants");

            // Generic pre-specialization shader path is the numerical oracle.
            const bool enabled=settings.bloom && settings.bloom_intensity>0;
            glDisable(GL_BLEND); glBindFramebuffer(GL_FRAMEBUFFER,processing); glBindVertexArray(quad.vao);
            glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D,scene);
            if(enabled)
            {
                glUseProgram(reference[0]);
                for(unsigned i=0;i<5;++i)
                {
                    const auto src=i==0?size:sizes[i-1];
                    glUniform2f(glGetUniformLocation(reference[0],"srcResolution"),src.x,src.y);
                    glUniform1f(glGetUniformLocation(reference[0],"threshold"),i==0?settings.bloom_threshold:-1);
                    glViewport(0,0,sizes[i].x,sizes[i].y); glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,mips[i],0);
                    glDrawElements(GL_TRIANGLES,6,GL_UNSIGNED_INT,nullptr); glBindTexture(GL_TEXTURE_2D,mips[i]);
                }
                glUseProgram(reference[1]); glUniform1f(glGetUniformLocation(reference[1],"filterRadius"),settings.bloom_filter);
                glEnable(GL_BLEND); glBlendFunc(GL_ONE,GL_ONE); glBlendEquation(GL_FUNC_ADD);
                for(int i=4;i>1;--i)
                {
                    glBindTexture(GL_TEXTURE_2D,mips[i]); glViewport(0,0,sizes[i-1].x,sizes[i-1].y);
                    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,mips[i-1],0);
                    glDrawElements(GL_TRIANGLES,6,GL_UNSIGNED_INT,nullptr);
                }
            }
            glDisable(GL_BLEND); glBindFramebuffer(GL_FRAMEBUFFER,outputFbo);
            glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,expected,0); glViewport(0,0,size.x,size.y);
            glUseProgram(reference[2]); glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D,scene);
            glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D,enabled?mips[1]:0);
            glUniform1f(glGetUniformLocation(reference[2],"gamma"),gamma);
            glUniform1f(glGetUniformLocation(reference[2],"exposure"),settings.exposure);
            glUniform1f(glGetUniformLocation(reference[2],"intensity"),enabled?settings.bloom_intensity:0);
            glDrawElements(GL_TRIANGLES,6,GL_UNSIGNED_INT,nullptr); const auto wanted=read(expected,size.x,size.y);
            for(std::size_t i=0;i<wanted.size();++i)
                require(std::isfinite(observed[i]) && std::abs(observed[i]-wanted[i])<=.000002f,
                        "Compiled bloom changed pixels versus generic reference");
            require(glGetError()==GL_NO_ERROR,"Bloom generated an OpenGL error"); ++cases;
        }
        glDeleteTextures(5,mips.data()); glDeleteTextures(1,&scene); glDeleteTextures(1,&actual); glDeleteTextures(1,&expected);
    }
    bloom.release(); require(manager.owned.empty(),"Bloom leaked linked variants"); bloom.release();
    glDeleteProgram(reference[0]); glDeleteProgram(reference[1]); glDeleteProgram(reference[2]);
    glDeleteFramebuffers(1,&processing); glDeleteFramebuffers(1,&outputFbo);
    glDeleteBuffers(1,&vertices); glDeleteBuffers(1,&quad.ibo); glDeleteVertexArrays(1,&quad.vao);
    std::cout<<"PASS production Bloom: "<<cases<<" HDR image comparisons, enabled/disabled/zero intensity, threshold modes, odd/tiny resize, bounded five programs, release\n"<<std::flush;
}
}
extern "C" __attribute__((visibility("default"))) int bloom_runtime_tests(const char* root)
{
    try { glewExperimental=GL_TRUE; const auto status=glewInit();
        require(status==GLEW_OK || status==GLEW_ERROR_NO_GLX_DISPLAY,"GLEW init failed");
        while(glGetError()!=GL_NO_ERROR) {} run(root); return 0;
    } catch(const std::exception& e) { std::cerr<<"FAIL Bloom: "<<e.what()<<'\n'; return 1; }
}
