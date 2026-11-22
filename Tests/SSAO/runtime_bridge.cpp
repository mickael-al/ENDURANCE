// Execute the production resource owner; only the editor entry point is absent.
#include "glcore.hpp"
#include "SSAOBuffer.hpp"
#include "GraphicsDataMisc.hpp"
#include "GraphiquePipelineManager.hpp"
#include "GraphiquePipeline.hpp"
#include "SettingManager.hpp"
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace Ge {
class RenderingEngine {
public:
    static bool init(SSAOBuffer& value, GraphicsDataMisc* data) { return value.initialize(data); }
    static unsigned kernel(const SSAOBuffer& value) { return value.getSSBO(); }
    static unsigned noise(const SSAOBuffer& value) { return value.getNoiseTexture(); }
    static void release(SSAOBuffer& value) { value.release(); }
    static bool init(GraphiquePipelineManager& value, GraphicsDataMisc* data) { return value.initialize(data); }
    static void release(GraphiquePipelineManager& value) { value.release(); }
};
}
namespace {
struct Runtime {
    GraphicsDataMisc data{};
    Ge::SSAOBuffer ssao;
    Ge::GraphiquePipelineManager pipelines;
    Ge::SettingManager settings;
};
Runtime& state(void* value) { return *static_cast<Runtime*>(value); }
}
#define EXPORTED extern "C" __attribute__((visibility("default")))
EXPORTED void* ssao_create(const char* root)
{
    try {
        glewExperimental=GL_TRUE;
        const auto status=glewInit();
        if(status!=GLEW_OK && status!=GLEW_ERROR_NO_GLX_DISPLAY) throw std::runtime_error("GLEW initialization");
        while(glGetError()!=GL_NO_ERROR) {}
        const auto previous=std::filesystem::current_path();
        std::filesystem::current_path(std::filesystem::path(root)/"ENDURANCE");
        auto* value=new Runtime();
        const bool initialized=Ge::RenderingEngine::init(value->pipelines,&value->data)
            && Ge::RenderingEngine::init(value->ssao,&value->data);
        std::filesystem::current_path(previous);
        if(!initialized) {
            Ge::RenderingEngine::release(value->ssao);
            Ge::RenderingEngine::release(value->pipelines);
            delete value; return nullptr;
        }
        return value;
    } catch(const std::exception& e) { std::cerr<<"SSAO runtime: "<<e.what()<<'\n'; return nullptr; }
}
EXPORTED int ssao_initialize(void* value) { return Ge::RenderingEngine::init(state(value).ssao,&state(value).data); }
EXPORTED unsigned ssao_kernel(void* value) { return Ge::RenderingEngine::kernel(state(value).ssao); }
EXPORTED unsigned ssao_noise(void* value) { return Ge::RenderingEngine::noise(state(value).ssao); }
EXPORTED void ssao_release(void* value) { Ge::RenderingEngine::release(state(value).ssao); }
EXPORTED int ssao_enabled(void* value) { return state(value).settings.getSsaoEnabled(); }
EXPORTED void ssao_set_enabled(void* value, int enabled) { state(value).settings.setSsaoEnabled(enabled!=0); }
EXPORTED unsigned ssao_program(void* value) {
    return state(value).pipelines.getDeferredLightingPipeline(state(value).settings.getSsaoEnabled())->getProgram();
}
EXPORTED void ssao_destroy(void* value)
{
    if(value) {
        ssao_release(value);
        Ge::RenderingEngine::release(state(value).pipelines);
        delete static_cast<Runtime*>(value);
    }
}
