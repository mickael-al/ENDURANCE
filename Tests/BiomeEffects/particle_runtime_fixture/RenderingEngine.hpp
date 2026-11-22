#pragma once
#include "GraphiquePipelineManager.hpp"
#include "FrameBuffer.hpp"
#include "Atmosphere.hpp"
#include <functional>
#include <map>
namespace Ge {
class RenderingEngine {
public:
    std::map<void*,std::function<void(bool*)>> callbacks;
    FrameBuffer framebuffer;
    Atmosphere atmosphere;
    unsigned calls=0;
    void addTransparencyCallback(void* owner,std::function<void(bool*)> callback) { callbacks[owner]=std::move(callback); }
    void removeTransparencyCallback(void* owner) { callbacks.erase(owner); }
    Atmosphere* getAtmosphere() { return &atmosphere; }
    const FrameBuffer* getFrameBuffer() const { return &framebuffer; }
    void render(bool* depth) { for(auto& entry:callbacks) { ++calls; entry.second(depth); } }
    static void attach(GraphiquePipelineManager& manager,GraphicsDataMisc* data) { manager.m_gdm=data; }
};
}
