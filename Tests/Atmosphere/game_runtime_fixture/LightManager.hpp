#pragma once
#include "Lights.hpp"
#include <vector>
#include <cstdint>
namespace Ge
{
    struct LightManager
    {
        std::vector<Lights*> lights;
        std::vector<unsigned> shadowFrames;
        unsigned shadowTexture=0,shadowBuffer=0;
        std::uint64_t shadowContentRevision=1;
        std::uint64_t getShadowContentRevision() const { return shadowContentRevision; }
        const std::vector<Lights*>& getLights() const { return lights; }
        unsigned getTextureShadowArray() const { return shadowTexture; }
        unsigned getSsboShadow() const { return shadowBuffer; }
        const std::vector<unsigned>& getFrameShadowBuffer() const { return shadowFrames; }
    };
}
