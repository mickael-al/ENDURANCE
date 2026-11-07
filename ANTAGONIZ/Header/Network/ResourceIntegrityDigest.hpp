#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace integrity
{
// Bump this when the manifest's canonical encoding or protected-resource schema
// changes. The SHA-256 algorithm itself is fixed by FIPS 180-4.
inline constexpr std::uint32_t ProtocolVersion = 1;

class Sha256
{
public:
    Sha256();
    void update(const void* bytes, std::size_t size);
    void update(std::string_view bytes) { update(bytes.data(), bytes.size()); }
    // Non-destructive: callers may inspect a digest and then continue streaming.
    std::string finish() const;

private:
    void transform(const std::uint8_t* block);
    std::array<std::uint32_t, 8> m_state;
    std::array<std::uint8_t, 64> m_buffer{};
    std::uint64_t m_size = 0;
    std::size_t m_buffered = 0;
};

std::string sha256(std::string_view bytes);

// Canonical, streaming encoding for resource manifests. The collector supplies
// stable resource keys, sorted records and an explicit schema. No raw C++ struct,
// pointer, OpenGL object name or unordered-container traversal belongs here.
class ManifestHash
{
public:
    ManifestHash();
    void appendU32(std::uint32_t value);
    void appendU64(std::uint64_t value);
    void appendString(std::string_view value);
    void appendBytes(std::string_view key, std::string_view bytes);
    // Use only after encoding the payload's length/count and type in the schema.
    void appendRaw(const void* bytes, std::size_t size);
    // Finite IEEE-754 values only. Negative zero and positive zero are identical.
    // A rejected value invalidates the entire manifest, even if ignored by caller.
    bool appendFloat(float value);
    bool valid() const { return m_valid; }
    std::string finish() const;

private:
    Sha256 m_hash;
    bool m_valid = true;
};
}
