#ifndef __ENGINE_DEPTH_OF_FIELD__
#define __ENGINE_DEPTH_OF_FIELD__

#include "ModulePP.hpp"

namespace Ge
{
    class GraphiquePipeline;

    class DepthOfField final : public ModulePP
    {
    public:
        bool initialize(GraphicsDataMisc* gdm) override;
        void release() override;
        void resize(int width, int height) override;
        // frameBuffer is the output target; texture is a separate color input.
        void compute(unsigned int frameBuffer, unsigned int texture,
                     unsigned int depthTexture, ShapeBuffer* sb,
                     PPSetting* settings) override;
        void onGui(PPSetting* target, PPSetting* current) override;

    private:
        struct LensLocations
        {
            int inverseProjection = -1;
            int focusDistance = -1;
            int focalLength = -1;
            int fNumber = -1;
            int sensorHeight = -1;
            int renderHeight = -1;
            int maxRadius = -1;
        };

        LensLocations lensLocations(unsigned int program) const;
        void setLens(const LensLocations& locations,
                     const glm::mat4& inverseProjection, float focusDistance,
                     float focalLength, float fNumber, float sensorHeight,
                     float maxRadius) const;
        bool ensureBuffers(int width, int height);

        GraphicsDataMisc* m_gdm = nullptr;
        GraphiquePipeline* m_prefilter = nullptr;
        GraphiquePipeline* m_gather = nullptr;
        GraphiquePipeline* m_composite = nullptr;
        LensLocations m_prefilterLens;
        LensLocations m_compositeLens;
        int m_gatherRadius = -1;
        unsigned int m_frameBuffers[2] = {};
        // Prefilter (RGB + signed CoC), far blur, near blur + coverage.
        unsigned int m_textures[3] = {};
        int m_width = 0;
        int m_height = 0;
        bool m_buffersComplete = false;
    };
}

#endif
