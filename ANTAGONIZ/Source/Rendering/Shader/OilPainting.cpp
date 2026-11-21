#include "OilPainting.hpp"
#include "glcore.hpp"
#include "GraphicsDataMisc.hpp"
#include "Debug.hpp"
#include "GraphiquePipelineManager.hpp"
#include "GraphiquePipeline.hpp"
#include "Engine.hpp"
#include "PointeurClass.hpp"
#include "ShapeBuffer.hpp"
#include "imgui-cmake/Header/imgui.h"
#include <algorithm>
#include <climits>
#include <cmath>
#include <string>

namespace
{
    float strength(const Ge::PPSetting* settings)
    {
        return std::isfinite(settings->oilp_strength)
            ? (std::clamp)(settings->oilp_strength, 0.0f, 1.0f) : 0.65f;
    }
}
namespace Ge
{
    bool OilPainting::requested(const PPSetting* settings)
    {
        return settings && settings->oil_painting && settings->oilp_radius > 0 && strength(settings) > 0;
    }

    bool OilPainting::initialize(GraphicsDataMisc* gdm)
    {
        release();
        m_pc = Engine::getPtrClassAddr(); m_gdm = gdm;
        if (!m_pc || !m_pc->graphiquePipelineManager || !gdm) return false;
        for (unsigned radius = 0; radius <= MaxRadius; ++radius)
        {
            auto*& pipeline = m_programs[radius];
            pipeline = m_pc->graphiquePipelineManager->createPipeline(
                "../Asset/Shader/oilpainting.fs.glsl", "../Asset/Shader/pp.vs.glsl",
                false, true, false, 1, "#define GE_OIL_RADIUS " + std::to_string(radius) + "\n", {true,{}});
            if (!pipeline || !pipeline->getProgram())
            {
                Debug::Error("Could not compile oil painting kernels."); release(); return false;
            }
            m_strengthLocations[radius] = glGetUniformLocation(pipeline->getProgram(), "u_strength");
        }
        // PostProcess supplies separate input/output targets after tone mapping.
        return true;
    }

    void OilPainting::resize(int, int) {}

    void OilPainting::release()
    {
        for (auto*& pipeline : m_programs)
        {
            if (pipeline && m_pc && m_pc->graphiquePipelineManager)
                m_pc->graphiquePipelineManager->destroyPipeline(pipeline);
            pipeline = nullptr;
        }
        m_strengthLocations.fill(-1); m_pc = nullptr; m_gdm = nullptr;
    }

    void OilPainting::compute(unsigned frameBuffer, unsigned texture, unsigned, ShapeBuffer* sb, PPSetting* settings)
    {
        if (!settings || !settings->oil_painting || !m_gdm || !sb || !texture ||
            !m_gdm->str_width || !m_gdm->str_height ||
            m_gdm->str_width > static_cast<unsigned>(INT_MAX) || m_gdm->str_height > static_cast<unsigned>(INT_MAX)) return;
        const float amount = strength(settings);
        const unsigned radius = amount > 0 ? (std::min)(settings->oilp_radius, MaxRadius) : 0;
        const auto* pipeline = m_programs[radius];
        if (!pipeline) return;
        glDisable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE); glDisable(GL_BLEND);
        glBindFramebuffer(GL_FRAMEBUFFER, frameBuffer);
        glViewport(0, 0, static_cast<int>(m_gdm->str_width), static_cast<int>(m_gdm->str_height));
        glUseProgram(pipeline->getProgram());
        glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, texture);
        glUniform1f(m_strengthLocations[radius], amount);
        glBindVertexArray(sb->getVAO()); glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, sb->getIBO());
        glDrawElements(GL_TRIANGLES, sb->getIndiceSize(), GL_UNSIGNED_INT, nullptr);
    }

    void OilPainting::onGui(PPSetting* target, PPSetting* current)
    {
        if (!current || !ImGui::TreeNodeEx("Oil Painting")) return;
        bool changed = ImGui::Checkbox("Active##OP", &current->oil_painting);
        int radius = static_cast<int>((std::min)(current->oilp_radius, MaxRadius));
        if (ImGui::SliderInt("Brush radius (pixels)##OP", &radius, 0, static_cast<int>(MaxRadius)))
        { current->oilp_radius = static_cast<unsigned>(radius); changed = true; }
        changed |= ImGui::SliderFloat("Strength##OP", &current->oilp_strength, 0.0f, 1.0f);
        ImGui::TextWrapped("Start with radius 2 and strength 0.65. Larger brushes cost more. Radius or strength 0 bypasses the pass.");
        if (changed && target)
        {
            target->oil_painting = current->oil_painting;
            target->oilp_radius = current->oilp_radius;
            target->oilp_strength = current->oilp_strength;
        }
        ImGui::TreePop();
    }
}
