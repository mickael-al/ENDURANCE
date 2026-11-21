#ifndef GE_OIL_PAINTING_HPP
#define GE_OIL_PAINTING_HPP

#include "ModulePP.hpp"
#include <array>

namespace Ge
{
    class GraphiquePipeline;
    class OilPainting final : public ModulePP
    {
    public:
        static constexpr unsigned MaxRadius = 4;
        static bool requested(const PPSetting* settings);
        bool initialize(GraphicsDataMisc* gdm) override;
        void resize(int width, int height) override;
        void compute(unsigned frameBuffer, unsigned texture, unsigned depthTexture,
            ShapeBuffer* sb, PPSetting* settings) override;
        void onGui(PPSetting* target, PPSetting* current) override;
        void release() override;
    private:
        const ptrClass* m_pc = nullptr;
        GraphicsDataMisc* m_gdm = nullptr;
        // Fixed, bounded kernels. Radius zero is an exact copy for direct users.
        std::array<GraphiquePipeline*, MaxRadius + 1> m_programs{};
        std::array<int, MaxRadius + 1> m_strengthLocations{};
    };
}
#endif
