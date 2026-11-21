#ifndef GE_SHADOW_CONTENT_TOKEN_HPP
#define GE_SHADOW_CONTENT_TOKEN_HPP

#include <atomic>
#include <cstdint>

namespace Ge
{
    // Resource lifetimes and explicit external mutations cannot alias a prior
    // cached shadow, even when an allocator or OpenGL reuses its old address/ID.
    class ShadowContentToken
    {
    public:
        ShadowContentToken() noexcept : m_value(next()) {}
        ShadowContentToken(const ShadowContentToken&) noexcept : ShadowContentToken() {}
        ShadowContentToken& operator=(const ShadowContentToken&) noexcept
        { invalidate(); return *this; }
        std::uint64_t value() const noexcept { return m_value; }
        void invalidate() noexcept { m_value = next(); }
    private:
        static std::uint64_t next() noexcept
        {
            static std::atomic<std::uint64_t> serial{1};
            return serial.fetch_add(1, std::memory_order_relaxed);
        }
        std::uint64_t m_value;
    };
}
#endif
