#include "SharedShaderContext.hpp"
#define GLFW_INCLUDE_NONE
#include "GLFW/glfw3.h"

namespace Ge
{
    std::unique_ptr<SharedShaderContext> SharedShaderContext::create(std::string& error)
    {
        auto* owner=glfwGetCurrentContext();
        if (!owner) { error="No current GLFW context for background shader compilation"; return {}; }
        auto result=std::unique_ptr<SharedShaderContext>(new SharedShaderContext(nullptr));
        // Window::initialize keeps the parent's context/pixel-format hints
        // unchanged after creation. Reuse them so WGL sharing does not request
        // a different MSAA pixel format or a newer core-profile context.
        // Only hide this auxiliary window; ordinary engine windows are visible.
        glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE);
        auto* window=glfwCreateWindow(1,1,"Shader compiler",nullptr,owner);
        glfwWindowHint(GLFW_VISIBLE,GLFW_TRUE);
        if (!window)
        {
            const char* detail=nullptr;
            glfwGetError(&detail);
            error="Cannot create shared shader compiler context";
            if (detail) error+=std::string(": ")+detail;
            return {};
        }
        result->m_window=window;
        error.clear();
        return result;
    }

    SharedShaderContext::~SharedShaderContext()
    {
        if (m_window) glfwDestroyWindow(m_window);
    }

    BackgroundShaderCompiler::ContextHooks SharedShaderContext::hooks() const
    {
        auto* window=m_window;
        return {
            [window]() { glfwMakeContextCurrent(window); return glfwGetCurrentContext()==window; },
            []() { glfwMakeContextCurrent(nullptr); }
        };
    }
}
