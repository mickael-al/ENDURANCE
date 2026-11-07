#pragma once

#include "imgui-cmake/Header/imgui.h"
#include "imgui-cmake/Header/imgui_internal.h"

namespace Ge
{
    struct EditorInputCapture
    {
        bool textInput = false;
        bool lockCamera = false;
    };

    inline EditorInputCapture editorInputCapture(bool visible)
    {
        if (!visible || !ImGui::GetCurrentContext()) return {};
        // WantTextInput is computed for the next frame. Read the active text
        // owner so a click that leaves a field releases shortcuts immediately.
        const bool textInput = ImGui::GetInputTextState(ImGui::GetActiveID()) != nullptr;
        // Mouse interaction can lock the camera without disabling shortcuts
        // belonging to the interface itself, such as L + texture click.
        return {textInput, textInput || ImGui::IsAnyItemActive()};
    }
}
