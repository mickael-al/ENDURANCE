#pragma once
// Services only: particle ownership, batching, allocation and shaders stay real.
#include "GraphiquePipelineManager.hpp"
#include <functional>
#include <unordered_map>
struct GraphicsDataMisc;
namespace Ge {
class RenderingEngine;
class Time { public: float getDeltaTime() const { return 1.f/60.f; } };
}
struct ptrClass {
    Ge::GraphiquePipelineManager* graphiquePipelineManager=nullptr;
    Ge::RenderingEngine* renderingEngine=nullptr;
    GraphicsDataMisc* gdm=nullptr;
    Ge::Time* time=nullptr;
};
