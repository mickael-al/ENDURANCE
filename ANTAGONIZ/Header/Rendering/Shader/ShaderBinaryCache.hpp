#ifndef ENDURANCE_SHADER_BINARY_CACHE_HPP
#define ENDURANCE_SHADER_BINARY_CACHE_HPP

#include "ShaderCompilation.hpp"
#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace Ge::detail
{
    // Capture once on the owning GL context for one build. A future build (or
    // different context) gets a fresh identity and supported-format list.
    struct ShaderBinaryEnvironment
    {
        explicit ShaderBinaryEnvironment(bool enabled);
        std::array<std::string, 4> driver;
        std::vector<unsigned int> formats;
        unsigned int flags = 0, profile = 0;
        std::string unavailable;
        bool available = false;
    };

    // Internal, best-effort disk cache. Programs returned by load() are owned by
    // the caller. No program or source is retained beyond this build operation.
    class ShaderBinaryCache
    {
    public:
        ShaderBinaryCache(const std::vector<ShaderFile>& stages,
            const std::vector<ShaderSource>& sources, const ShaderCacheOptions& options,
            const std::string& label, const ShaderBinaryEnvironment* environment = nullptr);
        bool available() const noexcept { return m_available; }
        unsigned int load() const;
        void store(unsigned int program) const;

    private:
        void report(const std::string& message) const;
        bool supports(unsigned int format) const;
        std::filesystem::path m_path;
        std::array<std::uint8_t, 32> m_key{};
        std::vector<unsigned int> m_formats;
        std::string m_label;
        bool m_available = false;
    };
}

#endif
