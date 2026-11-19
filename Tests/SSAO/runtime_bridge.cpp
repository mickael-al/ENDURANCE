// Execute the production resource owner; only the editor entry point is absent.
#include "glcore.hpp"
#include "SSAOBuffer.hpp"
#include "GraphicsDataMisc.hpp"
#include <iostream>
#include <stdexcept>

namespace Ge {
class RenderingEngine {
public:
    static bool init(SSAOBuffer& value, GraphicsDataMisc* data) { return value.initialize(data); }
    static unsigned kernel(const SSAOBuffer& value) { return value.getSSBO(); }
    static unsigned noise(const SSAOBuffer& value) { return value.getNoiseTexture(); }
    static void release(SSAOBuffer& value) { value.release(); }
};
}
namespace { GraphicsDataMisc data{}; }
#define EXPORTED extern "C" __attribute__((visibility("default")))
EXPORTED void* ssao_create(const char* root)
{
    try {
        glewExperimental=GL_TRUE;
        const auto status=glewInit();
        if(status!=GLEW_OK && status!=GLEW_ERROR_NO_GLX_DISPLAY) throw std::runtime_error("GLEW initialization");
        while(glGetError()!=GL_NO_ERROR) {}
        (void)root;
        auto* value=new Ge::SSAOBuffer();
        if(!Ge::RenderingEngine::init(*value,&data)) { delete value; return nullptr; }
        return value;
    } catch(const std::exception& e) { std::cerr<<"SSAO runtime: "<<e.what()<<'\n'; return nullptr; }
}
EXPORTED int ssao_initialize(void* value) { return Ge::RenderingEngine::init(*static_cast<Ge::SSAOBuffer*>(value),&data); }
EXPORTED unsigned ssao_kernel(void* value) { return Ge::RenderingEngine::kernel(*static_cast<Ge::SSAOBuffer*>(value)); }
EXPORTED unsigned ssao_noise(void* value) { return Ge::RenderingEngine::noise(*static_cast<Ge::SSAOBuffer*>(value)); }
EXPORTED void ssao_release(void* value) { Ge::RenderingEngine::release(*static_cast<Ge::SSAOBuffer*>(value)); }
EXPORTED void ssao_destroy(void* value)
{ if(value) { ssao_release(value); delete static_cast<Ge::SSAOBuffer*>(value); } }
