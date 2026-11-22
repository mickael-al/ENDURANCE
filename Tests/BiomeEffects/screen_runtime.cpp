// The actual screen pass is compiled; only window/editor services are replaced.
#include "glcore.hpp"
#include "EnvironmentScreenEffects.hpp"
#include "GraphicsDataMisc.hpp"
#include "GraphiquePipelineManager.hpp"
#include "Engine.hpp"
#include "ShapeBuffer.hpp"
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace {
struct Runtime {
    GraphicsDataMisc graphics;
    Ge::Camera camera;
    Ge::GraphiquePipelineManager manager;
    Ge::EnvironmentScreenEffects effects;
    Ge::ShapeBuffer quad;
    GLuint vertices=0;
};
Runtime& state(void* value){return *static_cast<Runtime*>(value);}
}
#define EXPORTED extern "C" __attribute__((visibility("default")))
EXPORTED void* screen_create(const char* root)
{
    try {
        glewExperimental=GL_TRUE;auto status=glewInit();
        if(status!=GLEW_OK && status!=GLEW_ERROR_NO_GLX_DISPLAY)throw std::runtime_error("GLEW initialization failed");
        while(glGetError()!=GL_NO_ERROR){}
        std::filesystem::current_path(std::filesystem::path(root)/"ENDURANCE");
        auto* value=new Runtime();
        Ge::Engine::pointers.graphiquePipelineManager=&value->manager;
        value->graphics.current_camera=&value->camera;
        value->camera.projection[2][2]=-1.000002f;
        value->camera.projection[3][2]=-.2000002f;
        value->camera.projection[2][3]=-1.f;
        if(!value->effects.initialize(&value->graphics)){delete value;return nullptr;}
        const float points[]={-1,-1,0,0, 1,-1,1,0, 1,1,1,1, -1,1,0,1};
        const unsigned indices[]={0,1,2,0,2,3};
        glGenVertexArrays(1,&value->quad.vao);glBindVertexArray(value->quad.vao);
        glGenBuffers(1,&value->vertices);glBindBuffer(GL_ARRAY_BUFFER,value->vertices);
        glBufferData(GL_ARRAY_BUFFER,sizeof(points),points,GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,4*sizeof(float),nullptr);
        glEnableVertexAttribArray(1);glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,4*sizeof(float),reinterpret_cast<void*>(2*sizeof(float)));
        glGenBuffers(1,&value->quad.ibo);glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,value->quad.ibo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER,sizeof(indices),indices,GL_STATIC_DRAW);
        return value;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return nullptr;}
}
EXPORTED unsigned screen_program(void* value,unsigned index){return state(value).manager.owned.at(index)->getProgram();}
EXPORTED unsigned screen_program_count(void* value){return static_cast<unsigned>(state(value).manager.owned.size());}
EXPORTED int screen_requested(void* value){return state(value).effects.requested();}
EXPORTED void screen_state(void* value,const float* input)
{
    Ge::EnvironmentScreenState params;
    params.heat=input[0];params.frost=input[1];params.wet=input[2];params.condensation=input[3];
    params.dust=input[4];params.flash=input[5];params.tint={input[6],input[7],input[8]};
    params.scale=input[9];params.speed=input[10];params.time=input[11];state(value).effects.setState(params);
}
EXPORTED void screen_draw(void* value,unsigned width,unsigned height,unsigned fbo,unsigned color,unsigned depth)
{
    auto& data=state(value);data.graphics.str_width=width;data.graphics.str_height=height;
    data.effects.compute(fbo,color,depth,&data.quad,nullptr);
}
EXPORTED unsigned screen_destroy(void* value)
{
    if(!value)return 0;auto& data=state(value);data.effects.release();
    const unsigned remaining=static_cast<unsigned>(data.manager.owned.size());
    glDeleteBuffers(1,&data.vertices);glDeleteBuffers(1,&data.quad.ibo);glDeleteVertexArrays(1,&data.quad.vao);
    delete static_cast<Runtime*>(value);
    return remaining;
}
