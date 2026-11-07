#include "DepthOfField.hpp"
#include "glcore.hpp"
#include "GraphicsDataMisc.hpp"
#include "Camera.hpp"
#include "Debug.hpp"
#include "GraphiquePipeline.hpp"
#include "GraphiquePipelineManager.hpp"
#include "Engine.hpp"
#include "PointeurClass.hpp"
#include "ShapeBuffer.hpp"
#include "imgui-cmake/Header/imgui.h"

#include <algorithm>
#include <cmath>
#include <climits>

namespace
{
    float bounded(float value, float minimum, float maximum, float fallback)
    {
        return std::isfinite(value)
            ? (std::max)(minimum, (std::min)(value, maximum)) : fallback;
    }
}

namespace Ge
{
    bool DepthOfField::initialize(GraphicsDataMisc* gdm)
    {
        m_gdm = gdm;
        auto* manager = Engine::getPtrClassAddr()->graphiquePipelineManager;
        m_prefilter = manager->createPipeline("../Asset/Shader/dof_prefilter.fs.glsl", "../Asset/Shader/pp.vs.glsl");
        m_gather = manager->createPipeline("../Asset/Shader/dof_gather.fs.glsl", "../Asset/Shader/pp.vs.glsl");
        m_composite = manager->createPipeline("../Asset/Shader/dof_composite.fs.glsl", "../Asset/Shader/pp.vs.glsl");
        if (!m_prefilter || !m_gather || !m_composite)
            return false;
        m_prefilterLens = lensLocations(m_prefilter->getProgram());
        m_compositeLens = lensLocations(m_composite->getProgram());
        m_gatherRadius = glGetUniformLocation(m_gather->getProgram(), "u_maxRadius");
        // Allocate the half-resolution targets only when the effect is used.
        return true;
    }

    DepthOfField::LensLocations DepthOfField::lensLocations(unsigned int program) const
    {
        LensLocations result;
        result.inverseProjection = glGetUniformLocation(program, "u_inverseProjection");
        result.focusDistance = glGetUniformLocation(program, "u_focusDistance");
        result.focalLength = glGetUniformLocation(program, "u_focalLength");
        result.fNumber = glGetUniformLocation(program, "u_fNumber");
        result.sensorHeight = glGetUniformLocation(program, "u_sensorHeight");
        result.renderHeight = glGetUniformLocation(program, "u_renderHeight");
        result.maxRadius = glGetUniformLocation(program, "u_maxRadius");
        return result;
    }

    void DepthOfField::setLens(const LensLocations& locations,
                              const glm::mat4& inverseProjection,
                              float focusDistance, float focalLength,
                              float fNumber, float sensorHeight,
                              float maxRadius) const
    {
        glUniformMatrix4fv(locations.inverseProjection, 1, GL_FALSE, &inverseProjection[0][0]);
        glUniform1f(locations.focusDistance, focusDistance);
        glUniform1f(locations.focalLength, focalLength);
        glUniform1f(locations.fNumber, fNumber);
        glUniform1f(locations.sensorHeight, sensorHeight);
        glUniform1f(locations.renderHeight, static_cast<float>(m_gdm->str_height));
        glUniform1f(locations.maxRadius, maxRadius);
    }

    bool DepthOfField::ensureBuffers(int width, int height)
    {
        if (width <= 0 || height <= 0)
            return false;
        if (width == m_width && height == m_height)
            return m_buffersComplete;

        if (m_textures[0] == 0)
        {
            glGenTextures(3, m_textures);
            glGenFramebuffers(2, m_frameBuffers);
        }
        const int halfWidth = width / 2 + width % 2;
        const int halfHeight = height / 2 + height % 2;
        glActiveTexture(GL_TEXTURE0);
        for (unsigned int texture : m_textures)
        {
            glBindTexture(GL_TEXTURE_2D, texture);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, halfWidth, halfHeight,
                         0, GL_RGBA, GL_FLOAT, nullptr);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        }
        glBindFramebuffer(GL_FRAMEBUFFER, m_frameBuffers[0]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_textures[0], 0);
        glDrawBuffer(GL_COLOR_ATTACHMENT0);
        bool complete = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
        glBindFramebuffer(GL_FRAMEBUFFER, m_frameBuffers[1]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_textures[1], 0);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, m_textures[2], 0);
        const GLenum attachments[] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1};
        glDrawBuffers(2, attachments);
        complete = complete && glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        m_width = width;
        m_height = height;
        m_buffersComplete = complete;
        if (!complete)
            Debug::Error("Depth of field framebuffer is incomplete.");
        return complete;
    }

    void DepthOfField::resize(int width, int height)
    {
        if (m_textures[0] != 0 && width > 0 && height > 0)
            ensureBuffers(width, height);
    }

    void DepthOfField::compute(unsigned int frameBuffer, unsigned int texture,
                               unsigned int depthTexture, ShapeBuffer* sb,
                               PPSetting* settings)
    {
        if (!settings->depth_of_field || !m_gdm->current_camera ||
            m_gdm->str_width == 0 || m_gdm->str_height == 0 ||
            m_gdm->str_width > static_cast<unsigned int>(INT_MAX) ||
            m_gdm->str_height > static_cast<unsigned int>(INT_MAX))
            return;

        const int width = static_cast<int>(m_gdm->str_width);
        const int height = static_cast<int>(m_gdm->str_height);
        const bool buffersReady = ensureBuffers(width, height);
        const auto* camera = m_gdm->current_camera;
        const glm::mat4 inverseProjection = glm::inverse(camera->getProjectionMatrix());
        const float sensorHeight = bounded(settings->dof_sensor_height, 1.0f, 100.0f, 24.0f) * 0.001f;
        float focalLength = bounded(settings->dof_focal_length, 1.0f, 300.0f, 50.0f) * 0.001f;
        if (settings->dof_match_camera_fov && !camera->getOrtho())
        {
            const float fov = bounded(m_gdm->current_camera->getFieldOfView(), 1.0f, 179.0f, 70.0f);
            focalLength = sensorHeight / (2.0f * std::tan(glm::radians(fov) * 0.5f));
        }
        const float focusDistance = (std::max)(
            bounded(settings->dof_focus_distance, 0.01f, 10000.0f, 12.0f), focalLength + 0.001f);
        const float fNumber = bounded(settings->dof_f_number, 0.1f, 32.0f, 1.8f);
        // A reference-pixel radius keeps the apparent blur stable on resize.
        const float maxRadius = buffersReady
            ? (std::min)(bounded(settings->dof_max_radius, 0.0f, 32.0f, 12.0f) * height / 1080.0f, 96.0f)
            : 0.0f;

        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        glDisable(GL_BLEND);
        glBindVertexArray(sb->getVAO());
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, sb->getIBO());

        if (buffersReady && maxRadius > 0.0f)
        {
            glBindFramebuffer(GL_FRAMEBUFFER, m_frameBuffers[0]);
            glViewport(0, 0, width / 2 + width % 2, height / 2 + height % 2);
            glUseProgram(m_prefilter->getProgram());
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, texture);
            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, depthTexture);
            setLens(m_prefilterLens, inverseProjection, focusDistance, focalLength, fNumber, sensorHeight, maxRadius);
            glDrawElements(GL_TRIANGLES, sb->getIndiceSize(), GL_UNSIGNED_INT, nullptr);

            glBindFramebuffer(GL_FRAMEBUFFER, m_frameBuffers[1]);
            glUseProgram(m_gather->getProgram());
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, m_textures[0]);
            glUniform1f(m_gatherRadius, maxRadius * 0.5f);
            glDrawElements(GL_TRIANGLES, sb->getIndiceSize(), GL_UNSIGNED_INT, nullptr);
        }

        glBindFramebuffer(GL_FRAMEBUFFER, frameBuffer);
        glViewport(0, 0, width, height);
        glUseProgram(m_composite->getProgram());
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, texture);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, depthTexture);
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, m_textures[1]);
        glActiveTexture(GL_TEXTURE3);
        glBindTexture(GL_TEXTURE_2D, m_textures[2]);
        setLens(m_compositeLens, inverseProjection, focusDistance, focalLength, fNumber, sensorHeight, maxRadius);
        // With a zero radius the composite is a sharp copy, including on FBO failure.
        glDrawElements(GL_TRIANGLES, sb->getIndiceSize(), GL_UNSIGNED_INT, nullptr);
        glActiveTexture(GL_TEXTURE0);
    }

    void DepthOfField::onGui(PPSetting* target, PPSetting* current)
    {
        if (!ImGui::TreeNodeEx("Depth of Field"))
            return;
        if (ImGui::Checkbox("Active##DoF", &current->depth_of_field) && target)
            target->depth_of_field = current->depth_of_field;
        if (ImGui::DragFloat("Focus distance (m)##DoF", &current->dof_focus_distance, 0.1f, 0.05f, 10000.0f) && target)
            target->dof_focus_distance = current->dof_focus_distance;
        if (ImGui::DragFloat("Aperture (f-number)##DoF", &current->dof_f_number, 0.05f, 0.1f, 32.0f) && target)
            target->dof_f_number = current->dof_f_number;
        if (ImGui::Checkbox("Match camera FoV##DoF", &current->dof_match_camera_fov) && target)
            target->dof_match_camera_fov = current->dof_match_camera_fov;
        const bool useCameraFov = current->dof_match_camera_fov && m_gdm->current_camera && !m_gdm->current_camera->getOrtho();
        if (!useCameraFov)
        {
            if (ImGui::DragFloat("Focal length (mm)##DoF", &current->dof_focal_length, 0.5f, 1.0f, 300.0f) && target)
                target->dof_focal_length = current->dof_focal_length;
            ImGui::TextWrapped("Manual focal length uses a virtual lens. Orthographic cameras use this setting.");
        }
        if (ImGui::DragFloat("Sensor height (mm)##DoF", &current->dof_sensor_height, 0.1f, 1.0f, 100.0f) && target)
            target->dof_sensor_height = current->dof_sensor_height;
        if (ImGui::SliderFloat("Max radius (px at 1080p)##DoF", &current->dof_max_radius, 0.0f, 32.0f) && target)
            target->dof_max_radius = current->dof_max_radius;
        ImGui::TextWrapped("Lower f-numbers increase blur. HUD is drawn afterwards and stays sharp.");
        ImGui::TreePop();
    }

    void DepthOfField::release()
    {
        auto* manager = Engine::getPtrClassAddr()->graphiquePipelineManager;
        if (m_prefilter) manager->destroyPipeline(m_prefilter);
        if (m_gather) manager->destroyPipeline(m_gather);
        if (m_composite) manager->destroyPipeline(m_composite);
        m_prefilter = m_gather = m_composite = nullptr;
        glDeleteTextures(3, m_textures);
        glDeleteFramebuffers(2, m_frameBuffers);
        for (auto& texture : m_textures) texture = 0;
        for (auto& buffer : m_frameBuffers) buffer = 0;
        m_width = m_height = 0;
        m_buffersComplete = false;
    }
}
