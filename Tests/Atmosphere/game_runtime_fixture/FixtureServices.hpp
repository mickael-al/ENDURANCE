#pragma once
// Ownership and input fixtures only. The component, atmosphere implementation,
// shader compiler, climate evaluation, GPU resources and JSON paths are real.
#include "Camera.hpp"
#include "LightManager.hpp"
#include "CustomRenderer.hpp"
#include <algorithm>
#include <vector>

struct SceneData { std::vector<Ge::Lights*> dlight; };
namespace Ge
{
    class Atmosphere;
    struct Time
    {
        float delta=0;
        float getDeltaTime() const { return delta; }
    };
    struct CameraManager
    {
        Camera* current=nullptr;
        Camera* getCurrentCamera() const { return current; }
    };
    struct GraphiquePipelineManager
    {
        std::vector<CustomRenderer*> renderers;
        void addCustomRenderer(CustomRenderer* value) { renderers.push_back(value); }
        void removeCustomRenderer(CustomRenderer* value)
        { renderers.erase(std::remove(renderers.begin(),renderers.end(),value),renderers.end()); }
        const std::vector<CustomRenderer*>& getCustomRenderer() const { return renderers; }
    };
    struct RenderingEngine
    {
        Atmosphere* atmosphere=nullptr;
        Atmosphere* getAtmosphere() const { return atmosphere; }
    };
    struct Editor
    {
        SceneData* scene=nullptr;
        inline static Editor* current=nullptr;
        static Editor* instance() { return current; }
        SceneData* getCurrentSceneData() const { return scene; }
    };
}
struct ptrClass
{
    Ge::LightManager* lightManager=nullptr;
    Ge::CameraManager* cameraManager=nullptr;
    Ge::GraphiquePipelineManager* graphiquePipelineManager=nullptr;
    Ge::RenderingEngine* renderingEngine=nullptr;
    Ge::Time* time=nullptr;
};
namespace Ge
{
    struct Engine
    {
        inline static const ptrClass* services=nullptr;
        static const ptrClass* getPtrClassAddr() { return services; }
    };
}
