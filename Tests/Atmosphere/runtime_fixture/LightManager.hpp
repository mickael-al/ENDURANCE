#pragma once
#include "Lights.hpp"
#include <vector>
namespace Ge
{
    struct LightManager
    {
        std::vector<Lights*> lights;
        std::vector<unsigned> shadowFrames;
        unsigned shadowTexture=0,shadowBuffer=0;
        const std::vector<Lights*>& getLights() const { return lights; }
        unsigned getTextureShadowArray() const { return shadowTexture; }
        unsigned getSsboShadow() const { return shadowBuffer; }
        const std::vector<unsigned>& getFrameShadowBuffer() const { return shadowFrames; }
    };
}
