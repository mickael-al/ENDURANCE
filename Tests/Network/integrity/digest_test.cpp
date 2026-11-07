#include "ResourceIntegrityDigest.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <string>

namespace
{
unsigned checks = 0;

void require(bool condition, const char* message)
{
    ++checks;
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

struct Vector { std::size_t size; const char* digest; };
#include "digest_vectors.inc"

float floatFromBits(std::uint32_t bits)
{
    float value = 0.0f;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}
}

int main()
{
    using integrity::sha256;
    require(sha256("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
        "SHA-256 empty input");
    require(sha256("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
        "SHA-256 abc");
    require(sha256("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") ==
        "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1",
        "SHA-256 two-block padding vector");
    require(sha256("abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmno"
                   "ijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu") ==
        "cf5b16a778af8380036ce59e7b0492370b249b11e8f07a51afac45037afee9d1",
        "SHA-256 multi-block vector");
    require(sha256(std::string(1000000, 'a')) ==
        "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0",
        "SHA-256 million-a vector");
    const std::string binary("a\0bc", 4);
    require(sha256(binary) == "40bb547d936bbd31318ee37ac8799e7ecbb22eda2651f65e3214bffb8ce97bb4",
        "std::string embedded zero is included");
    require(sha256(binary) != sha256("a"), "binary length is not strlen");

    // Expected digests are generated with Python hashlib, independently of the
    // production code. Cover both padding edges and many input chunk boundaries.
    for (const auto& vector : Vectors)
    {
        std::string input(vector.size, '\0');
        for (std::size_t index = 0; index < input.size(); ++index)
            input[index] = static_cast<char>((index * 73u + 19u) & 255u);
        require(sha256(input) == vector.digest, "whole input matches independent implementation");
        for (const std::size_t chunk : {1u, 2u, 3u, 7u, 31u, 55u, 56u, 63u, 64u, 65u, 127u, 1024u})
        {
            integrity::Sha256 hash;
            hash.update(nullptr, 0);
            for (std::size_t at = 0; at < input.size(); at += chunk)
                hash.update(input.data() + at, (std::min)(chunk, input.size() - at));
            require(hash.finish() == vector.digest, "chunked hash matches independent implementation");
            require(hash.finish() == vector.digest, "finish is repeatable");
        }
    }
    integrity::Sha256 incremental;
    incremental.update("a");
    require(incremental.finish() == sha256("a"), "intermediate finish");
    incremental.update("bc");
    require(incremental.finish() == sha256("abc"), "finish does not consume state");
    auto copied = incremental;
    incremental.update("d");
    copied.update("e");
    require(incremental.finish() == sha256("abcd") && copied.finish() == sha256("abce"),
        "stream copies evolve independently");

    integrity::ManifestHash empty;
    require(empty.valid() && empty.finish() ==
        "a0e41db7d04bd9b4dd804179281b2777125e245e986fdb11f5129724c1d77427",
        "manifest domain and protocol version");
    integrity::ManifestHash canonical;
    canonical.appendU32(0x12345678u);
    canonical.appendU64(0x0123456789abcdefULL);
    require(canonical.appendFloat(1.25f), "finite manifest float accepted");
    canonical.appendString(std::string("A\0B", 3));
    require(canonical.finish() ==
        "c28f9cc5d3a4bd4c05ac7225172f16fb85af45b96303acfe182078d778524d48",
        "canonical LE integers, IEEE float and length-prefixed binary string");

    integrity::ManifestHash positiveZero, negativeZero;
    require(positiveZero.appendFloat(floatFromBits(0x00000000u)) &&
        negativeZero.appendFloat(floatFromBits(0x80000000u)), "zero accepted");
    require(positiveZero.finish() == negativeZero.finish(), "signed zero is canonical");
    for (const std::uint32_t bits : {0x7f800000u, 0xff800000u, 0x7fc00000u,
        0x7f800001u, 0xffc12345u})
    {
        integrity::ManifestHash invalid;
        require(!invalid.appendFloat(floatFromBits(bits)) && !invalid.valid() && invalid.finish().empty(),
            "non-finite floats invalidate manifest");
        invalid.appendString("later valid data");
        require(!invalid.appendFloat(1.0f) && invalid.finish().empty(),
            "invalid manifest cannot recover silently");
    }
    integrity::ManifestHash invalidBuffer;
    invalidBuffer.appendRaw(nullptr, 1);
    require(!invalidBuffer.valid() && invalidBuffer.finish().empty(), "missing payload invalidates manifest");
    integrity::ManifestHash emptyBuffer;
    emptyBuffer.appendRaw(nullptr, 0);
    require(emptyBuffer.finish() == empty.finish(), "empty payload is valid");

    integrity::ManifestHash splitA, splitB;
    splitA.appendBytes("ab", "c"); splitB.appendBytes("a", "bc");
    require(splitA.finish() != splitB.finish(), "record boundaries cannot collide by concatenation");
    integrity::ManifestHash orderA, orderB;
    orderA.appendU32(0); orderA.appendU32(1); orderA.appendU32(2);
    orderB.appendU32(0); orderB.appendU32(2); orderB.appendU32(1);
    require(orderA.finish() != orderB.finish(), "triangle winding changes digest despite equal index sum");
    integrity::ManifestHash rawA, rawB;
    rawA.appendBytes("resource", binary);
    rawB.appendString("resource"); rawB.appendU64(binary.size());
    rawB.appendRaw(binary.data(), 1); rawB.appendRaw(binary.data() + 1, binary.size() - 1);
    require(rawA.finish() == rawB.finish(), "large framed payloads can be streamed");
    std::cout << "PASS: " << checks << " integrity digest checks\n";
}
