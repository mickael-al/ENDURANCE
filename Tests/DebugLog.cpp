// Exercise the production logger with diagnostics larger than driver shader
// logs. ASan catches the original static-buffer overflow in Debug.cpp.
#include "Debug.hpp"
#include "Console.hpp"
#include <array>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace
{
void require(bool value, const char* message)
{
    if (!value) throw std::runtime_error(message);
}

struct CapturedStreams
{
    std::ostringstream output, error;
    std::istringstream input{"\n\n\n\n\n\n\n\n\n\n\n\n"};
    std::streambuf* originalOutput;
    std::streambuf* originalError;
    std::streambuf* originalInput;
    CapturedStreams()
        : originalOutput(std::cout.rdbuf(output.rdbuf())),
          originalError(std::cerr.rdbuf(error.rdbuf())),
          originalInput(std::cin.rdbuf(input.rdbuf())) {}
    ~CapturedStreams()
    {
        std::cout.rdbuf(originalOutput);
        std::cerr.rdbuf(originalError);
        std::cin.rdbuf(originalInput);
    }
    void clear()
    {
        output.str(""); output.clear();
        error.str(""); error.clear();
    }
};

void check()
{
    Ge::Console console;
    CapturedStreams streams;
    struct Detach { ~Detach() { Ge::Console::attach(nullptr); } } detach;
    Ge::Console::attach(&console);
    using LogFunction = void (*)(const char*, ...);
    struct Severity { const char* prefix; LogFunction log; bool error; };
    const std::array<Severity,5> severities{{
        {"USER", Ge::Debug::Log, false}, {"INFO", Ge::Debug::Info, false},
        {"WARN", Ge::Debug::Warn, false}, {"VLAYER", Ge::Debug::VLayer, false},
        {"ERROR", Ge::Debug::Error, true}
    }};
    // Includes both sides of the former 1024-byte boundary and large compiler
    // messages. Embedded format tokens must survive unchanged inside %s data.
    const std::string diagnostic = "shader.glsl:123: error: %s %n %% invalid expression\n";
    for (std::size_t length : {0u, 1000u, 1023u, 1024u, 2048u, 65537u, 131073u})
    {
        std::string payload;
        while (payload.size() < length) payload += diagnostic;
        payload.resize(length);
        for (const auto& severity : severities)
        {
            streams.clear(); console.lines.clear();
            severity.log("begin=%d hex=%x percent=%% <%s> end=%u", -17, 0x2a,
                         payload.c_str(), 42u);
            const std::string expected = std::string("[ENGINE][") + severity.prefix +
                "] : begin=-17 hex=2a percent=% <" + payload + "> end=42\n";
            require((severity.error ? streams.error.str() : streams.output.str()) == expected,
                    "Terminal diagnostic was truncated or incorrectly formatted");
            require((severity.error ? streams.output.str() : streams.error.str()).empty(),
                    "Diagnostic used the wrong terminal stream");
            require(console.lines.size() == 1 && console.lines.front() == expected,
                    "ImGui diagnostic differs from the complete terminal diagnostic");
        }
    }
    streams.clear(); console.lines.clear();
    Ge::Debug::Log("");
    require(streams.output.str() == "[ENGINE][USER] : \n" &&
            console.lines.size() == 1 && console.lines.front() == streams.output.str(),
            "Empty formatted message lost its prefix or newline");
    // The normal no-console path must also retain the entire message.
    Ge::Console::attach(nullptr);
    streams.clear();
    const std::string payload(131073, 'x');
    Ge::Debug::Info("%s:tail", payload.c_str());
    require(streams.output.str() == "[ENGINE][INFO] : " + payload + ":tail\n",
            "Large diagnostic without an ImGui console was truncated");
}
}

int main()
{
    try
    {
        check();
        std::cout << "PASS Debug logger: all five severities, complete 128 KiB diagnostics, "
                     "1024-byte boundaries, printf formatting, terminal and ImGui sinks\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL Debug logger: " << error.what() << '\n';
        return 1;
    }
}
