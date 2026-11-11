#ifndef ENDURANCE_CLIMATE_NOISE_LAB_PANEL_HPP
#define ENDURANCE_CLIMATE_NOISE_LAB_PANEL_HPP

#include "ImguiBlock.hpp"
#include "imgui-cmake/Header/imgui.h"
#include "glcore.hpp"
#include <array>
#include <cstddef>

class ClimateNoiseLabComponent;

class ClimateNoiseLabPanel final : public Ge::ImguiBlock
{
public:
    explicit ClimateNoiseLabPanel(ClimateNoiseLabComponent& owner);
    ~ClimateNoiseLabPanel() override;
    void init(GraphicsDataMisc*) override;
    void render(GraphicsDataMisc*) override;

private:
    static constexpr std::size_t TextureCount = 5;
    struct Uniforms
    {
        GLint resolution=-1, seed=-1, worldOrigin=-1, worldExtent=-1;
        GLint climateFrequency=-1, humidityFrequencyMultiplier=-1;
        GLint temperatureFrequencyMultiplier=-1, humidityNoiseType=-1;
        GLint temperatureNoiseType=-1, octaves=-1, lacunarity=-1, gain=-1;
        GLint remapMinimum=-1, remapMaximum=-1, attractionMode=-1;
        GLint attractionTarget=-1, attractionMaxShift=-1, matrixBlend=-1;
        GLint biomeMatrix=-1;
    } m_uniforms;

    ClimateNoiseLabComponent& m_owner;
    GLuint m_program=0;
    std::array<GLuint,TextureCount> m_textures{};
    int m_allocatedResolution=0;
    bool m_dirty=true, m_generateRequested=false, m_resetDock=false;

    void release();
    bool reloadShader();
    void discoverUniforms();
    void allocateTextures();
    void generate();
    void buildDockLayout(ImGuiID dockspace, ImVec2 size);
    void drawSettings();
    void drawTextureWindow(const char* name, GLuint texture, const char* description);
};

#endif
