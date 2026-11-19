#ifndef __SSAO_BUFFER__
#define __SSAO_BUFFER__

#include "Initializer.hpp"

namespace Ge
{
    class SSAOBuffer final : public InitializerAPI
    {
    protected:
        friend class RenderingEngine;
        bool initialize(GraphicsDataMisc* gdm) override;
        unsigned int getNoiseTexture() const;
        unsigned int getSSBO() const;
        void release() override;
    private:
        unsigned int m_noiseTexture = 0;
        unsigned int m_ssbo = 0;
    };
}
#endif
