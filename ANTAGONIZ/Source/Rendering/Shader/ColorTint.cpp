#include "ColorTint.hpp"
#include "glcore.hpp"
#include "GraphicsDataMisc.hpp"
#include "GraphiquePipelineManager.hpp"
#include "GraphiquePipeline.hpp"
#include "Engine.hpp"
#include "PointeurClass.hpp"
#include "ShapeBuffer.hpp"
#include "imgui-cmake/Header/imgui.h"
#include <algorithm>
#include <climits>
#include <cmath>

namespace
{
    float finiteClamped(float value, float fallback, float minimum, float maximum)
    {
        return std::isfinite(value) ? (std::clamp)(value, minimum, maximum) : fallback;
    }
}

namespace Ge
{
    bool ColorTint::initialize(GraphicsDataMisc* gdm)
    {
        if (gdm == nullptr)
        {
            return false;
        }
        m_pc = Engine::getPtrClassAddr();
        m_gdm = gdm;
        if (m_pc == nullptr || m_pc->graphiquePipelineManager == nullptr)
        {
            return false;
        }
        m_pipeline = m_pc->graphiquePipelineManager->createPipeline(
            "../Asset/Shader/color_tint.fs.glsl", "../Asset/Shader/pp.vs.glsl");
        if (m_pipeline == nullptr)
        {
            return false;
        }
        const unsigned int program = m_pipeline->getProgram();
        m_tintLocation = glGetUniformLocation(program, "u_tint_color");
        m_strengthLocation = glGetUniformLocation(program, "u_strength");
        m_saturationLocation = glGetUniformLocation(program, "u_saturation");
        m_contrastLocation = glGetUniformLocation(program, "u_contrast");
        return true;
    }

    void ColorTint::resize(int, int)
    {
        // PostProcess owns the input and output textures for this pass.
    }

    void ColorTint::release()
    {
        if (m_pipeline != nullptr && m_pc != nullptr)
        {
            m_pc->graphiquePipelineManager->destroyPipeline(m_pipeline);
        }
        m_pipeline = nullptr;
        m_pc = nullptr;
        m_gdm = nullptr;
    }

    void ColorTint::compute(unsigned int frameBuffer, unsigned int texture, unsigned int,
        ShapeBuffer* sb, PPSetting* settings)
    {
        if (settings == nullptr || !settings->color_tint || m_pipeline == nullptr ||
            m_gdm == nullptr || sb == nullptr || texture == 0 ||
            m_gdm->str_width == 0 || m_gdm->str_height == 0 ||
            m_gdm->str_width > static_cast<unsigned int>(INT_MAX) ||
            m_gdm->str_height > static_cast<unsigned int>(INT_MAX))
        {
            return;
        }

        const glm::vec3 tint(
            finiteClamped(settings->tint_color.r, 1.0f, 0.0f, 1.0f),
            finiteClamped(settings->tint_color.g, 1.0f, 0.0f, 1.0f),
            finiteClamped(settings->tint_color.b, 1.0f, 0.0f, 1.0f));
        const float strength = finiteClamped(settings->tint_strength, 0.5f, 0.0f, 1.0f);
        const float saturation = finiteClamped(settings->tint_saturation, 1.0f, 0.0f, 2.0f);
        const float contrast = finiteClamped(settings->tint_contrast, 1.0f, 0.0f, 2.0f);

        // The caller supplies a separate source texture after bloom/tonemapping.
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        glDisable(GL_BLEND);
        glBindFramebuffer(GL_FRAMEBUFFER, frameBuffer);
        glViewport(0, 0, static_cast<int>(m_gdm->str_width), static_cast<int>(m_gdm->str_height));
        glUseProgram(m_pipeline->getProgram());
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, texture);
        glUniform3f(m_tintLocation, tint.r, tint.g, tint.b);
        glUniform1f(m_strengthLocation, strength);
        glUniform1f(m_saturationLocation, saturation);
        glUniform1f(m_contrastLocation, contrast);
        glBindVertexArray(sb->getVAO());
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, sb->getIBO());
        glDrawElements(GL_TRIANGLES, sb->getIndiceSize(), GL_UNSIGNED_INT, nullptr);
    }

    void ColorTint::onGui(PPSetting* target, PPSetting* current)
    {
        if (current == nullptr || !ImGui::TreeNodeEx("Color Tint"))
        {
            return;
        }
        if (ImGui::Checkbox("Active##ColorTint", &current->color_tint) && target != nullptr)
        {
            target->color_tint = current->color_tint;
        }
        if (ImGui::ColorEdit3("Tint##ColorTint", &current->tint_color.x) && target != nullptr)
        {
            target->tint_color = current->tint_color;
        }
        if (ImGui::SliderFloat("Strength##ColorTint", &current->tint_strength, 0.0f, 1.0f) && target != nullptr)
        {
            target->tint_strength = current->tint_strength;
        }
        if (ImGui::SliderFloat("Saturation##ColorTint", &current->tint_saturation, 0.0f, 2.0f) && target != nullptr)
        {
            target->tint_saturation = current->tint_saturation;
        }
        if (ImGui::SliderFloat("Contrast##ColorTint", &current->tint_contrast, 0.0f, 2.0f) && target != nullptr)
        {
            target->tint_contrast = current->tint_contrast;
        }
        ImGui::TreePop();
    }
}
