#ifndef ENDURANCE_SHARED_SHADER_CONTEXT_HPP
#define ENDURANCE_SHARED_SHADER_CONTEXT_HPP

#include "BackgroundShaderCompiler.hpp"
#include <memory>
#include <string>

struct GLFWwindow;

namespace Ge
{
    // GLFW window creation/destruction belongs to the main thread. The hidden
    // shared context is made current only by BackgroundShaderCompiler's worker.
    // Stop/join that compiler before destroying this provider.
    class SharedShaderContext
    {
    public:
        static std::unique_ptr<SharedShaderContext> create(std::string& error);
        ~SharedShaderContext();
        BackgroundShaderCompiler::ContextHooks hooks() const;
        SharedShaderContext(const SharedShaderContext&)=delete;
        SharedShaderContext& operator=(const SharedShaderContext&)=delete;
    private:
        explicit SharedShaderContext(GLFWwindow* window) : m_window(window) {}
        GLFWwindow* m_window=nullptr;
    };
}
#endif
