#ifndef __COLOR_TINT__
#define __COLOR_TINT__

#include "ModulePP.hpp"

namespace Ge
{
    class GraphiquePipeline;
    class ColorTint final : public ModulePP
    {
    public:
        bool initialize(GraphicsDataMisc* gdm) override;
        void resize(int width, int height) override;
        void compute(unsigned int frameBuffer, unsigned int texture, unsigned int depthTexture,
            ShapeBuffer* sb, PPSetting* settings) override;
        void onGui(PPSetting* target, PPSetting* current) override;
        void release() override;

    private:
        const ptrClass* m_pc = nullptr;
        GraphicsDataMisc* m_gdm = nullptr;
        GraphiquePipeline* m_pipeline = nullptr;
        int m_tintLocation = -1;
        int m_strengthLocation = -1;
        int m_saturationLocation = -1;
        int m_contrastLocation = -1;
    };
}

#endif // !__COLOR_TINT__
