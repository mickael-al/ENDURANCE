#include "ResourceIntegrityDigest.hpp"

#include <algorithm>
#include <cassert>
#include <cstring>
#include <limits>

namespace integrity
{
namespace
{
    // SHA-256 constants and round function: FIPS 180-4, sections 4.2.2 and 6.2.
    constexpr std::array<std::uint32_t, 64> RoundConstants = {{
        0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u,
        0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
        0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
        0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
        0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
        0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
        0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
        0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
        0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
        0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
        0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u,
        0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
        0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u,
        0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
        0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
        0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u
    }};

    constexpr std::uint32_t rotateRight(std::uint32_t value, unsigned amount)
    {
        return (value >> amount) | (value << (32u - amount));
    }
}

Sha256::Sha256()
    : m_state{{0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
               0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u}}
{
}

void Sha256::transform(const std::uint8_t* block)
{
    std::array<std::uint32_t, 64> words{};
    for (std::size_t index = 0; index < 16; ++index)
    {
        const auto* p = block + index * 4;
        words[index] = (std::uint32_t{p[0]} << 24) | (std::uint32_t{p[1]} << 16) |
                       (std::uint32_t{p[2]} << 8) | std::uint32_t{p[3]};
    }
    for (std::size_t index = 16; index < words.size(); ++index)
    {
        const auto a = words[index - 15];
        const auto b = words[index - 2];
        const auto small0 = rotateRight(a, 7) ^ rotateRight(a, 18) ^ (a >> 3);
        const auto small1 = rotateRight(b, 17) ^ rotateRight(b, 19) ^ (b >> 10);
        words[index] = words[index - 16] + small0 + words[index - 7] + small1;
    }
    auto a = m_state[0]; auto b = m_state[1];
    auto c = m_state[2]; auto d = m_state[3];
    auto e = m_state[4]; auto f = m_state[5];
    auto g = m_state[6]; auto h = m_state[7];
    for (std::size_t index = 0; index < words.size(); ++index)
    {
        const auto big1 = rotateRight(e, 6) ^ rotateRight(e, 11) ^ rotateRight(e, 25);
        const auto choose = (e & f) ^ (~e & g);
        const auto first = h + big1 + choose + RoundConstants[index] + words[index];
        const auto big0 = rotateRight(a, 2) ^ rotateRight(a, 13) ^ rotateRight(a, 22);
        const auto majority = (a & b) ^ (a & c) ^ (b & c);
        const auto second = big0 + majority;
        h = g; g = f; f = e; e = d + first;
        d = c; c = b; b = a; a = first + second;
    }
    m_state[0] += a; m_state[1] += b; m_state[2] += c; m_state[3] += d;
    m_state[4] += e; m_state[5] += f; m_state[6] += g; m_state[7] += h;
}

void Sha256::update(const void* bytes, std::size_t size)
{
    if (size == 0) return;
    assert(bytes != nullptr);
    const auto* data = static_cast<const std::uint8_t*>(bytes);
    m_size += static_cast<std::uint64_t>(size);
    if (m_buffered != 0)
    {
        const auto count = (std::min)(size, m_buffer.size() - m_buffered);
        std::memcpy(m_buffer.data() + m_buffered, data, count);
        m_buffered += count;
        data += count;
        size -= count;
        if (m_buffered == m_buffer.size())
        {
            transform(m_buffer.data());
            m_buffered = 0;
        }
    }
    while (size >= m_buffer.size())
    {
        transform(data);
        data += m_buffer.size();
        size -= m_buffer.size();
    }
    if (size != 0)
    {
        std::memcpy(m_buffer.data(), data, size);
        m_buffered = size;
    }
}

std::string Sha256::finish() const
{
    Sha256 final = *this;
    const auto bits = m_size * std::uint64_t{8};
    std::array<std::uint8_t, 128> tail{};
    tail[0] = 0x80;
    const std::size_t padding = m_buffered < 56 ? 56 - m_buffered : 120 - m_buffered;
    for (unsigned index = 0; index < 8; ++index)
        tail[padding + index] = static_cast<std::uint8_t>(bits >> (56u - index * 8u));
    final.update(tail.data(), padding + 8);
    constexpr char Hex[] = "0123456789abcdef";
    std::string result(64, '0');
    for (std::size_t index = 0; index < final.m_state.size(); ++index)
    {
        for (unsigned nibble = 0; nibble < 8; ++nibble)
            result[index * 8 + nibble] = Hex[(final.m_state[index] >> (28u - nibble * 4u)) & 0xfu];
    }
    return result;
}

std::string sha256(std::string_view bytes)
{
    Sha256 result;
    result.update(bytes);
    return result.finish();
}

ManifestHash::ManifestHash()
{
    appendString("ENDURANCE.ResourceIntegrity");
    appendU32(ProtocolVersion);
}

void ManifestHash::appendU32(std::uint32_t value)
{
    std::array<std::uint8_t, 4> bytes{};
    for (unsigned index = 0; index < bytes.size(); ++index)
        bytes[index] = static_cast<std::uint8_t>(value >> (index * 8u));
    appendRaw(bytes.data(), bytes.size());
}

void ManifestHash::appendU64(std::uint64_t value)
{
    std::array<std::uint8_t, 8> bytes{};
    for (unsigned index = 0; index < bytes.size(); ++index)
        bytes[index] = static_cast<std::uint8_t>(value >> (index * 8u));
    appendRaw(bytes.data(), bytes.size());
}

void ManifestHash::appendString(std::string_view value)
{
    appendU64(static_cast<std::uint64_t>(value.size()));
    appendRaw(value.data(), value.size());
}

void ManifestHash::appendBytes(std::string_view key, std::string_view bytes)
{
    appendString(key);
    appendString(bytes);
}

void ManifestHash::appendRaw(const void* bytes, std::size_t size)
{
    if (!m_valid) return;
    if (bytes == nullptr && size != 0)
    {
        m_valid = false;
        return;
    }
    m_hash.update(bytes, size);
}

bool ManifestHash::appendFloat(float value)
{
    static_assert(sizeof(float) == sizeof(std::uint32_t) &&
        std::numeric_limits<float>::is_iec559, "Integrity floats require 32-bit IEEE-754");
    if (!m_valid) return false;
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    // The game enables -ffast-math in Release. Floating-point classification and
    // comparisons may then assume away NaN, infinity or signed zero. Inspect
    // the representation exclusively with integer operations instead.
    if ((bits & 0x7f800000u) == 0x7f800000u)
    {
        m_valid = false;
        return false;
    }
    if ((bits & 0x7fffffffu) == 0) bits = 0;
    appendU32(bits);
    return true;
}

std::string ManifestHash::finish() const
{
    return m_valid ? m_hash.finish() : std::string();
}
}
