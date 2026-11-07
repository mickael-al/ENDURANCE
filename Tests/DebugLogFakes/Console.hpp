#pragma once
// Keep the actual Debug implementation and termcolor output in the test. Only
// the ImGui sink is replaced, so no window or rendering context is required.
#include "Debug.hpp"
#include <cstdarg>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

struct ImColor
{
    ImColor(float, float, float, float) {}
};

namespace Ge
{
class Console final
{
public:
    static void attach(Console* value) { Debug::console = value; }
    void setBaseColor(ImColor) {}
    void AddLog(const char* format, ...)
    {
        // Diagnostics must be passed as data, never as a second format string.
        if (std::strcmp(format, "%s") != 0)
            throw std::runtime_error("Debug did not forward a literal console format");
        va_list args;
        va_start(args, format);
        lines.emplace_back(va_arg(args, const char*));
        va_end(args);
    }
    std::vector<std::string> lines;
};
}
