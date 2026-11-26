#include "SoundPcm.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <limits>

namespace Ge
{
    std::uint64_t blendSoundLoopSeam(std::int8_t* data, std::uint64_t size,
        std::uint32_t sampleRate, unsigned channels, unsigned bits, float seconds)
    {
        std::uint32_t floatBits;
        std::memcpy(&floatBits, &seconds, sizeof(floatBits));
        if ((floatBits & 0x7f800000u) == 0x7f800000u || seconds <= 0.0f ||
            !data || !sampleRate || (channels != 1 && channels != 2) ||
            (bits != 8 && bits != 16) || size > (std::numeric_limits<std::size_t>::max)()) return size;
        const std::uint64_t frameBytes = channels * (bits / 8u);
        if (size % frameBytes) return size;
        const std::uint64_t frames = size / frameBytes;
        const std::uint64_t overlap = (std::min)(frames / 4,
            static_cast<std::uint64_t>((std::min)(seconds, 2.0f) * static_cast<double>(sampleRate)));
        if (overlap < 2) return size;
        auto* bytes = reinterpret_cast<std::uint8_t*>(data);
        const auto read = [&](std::uint64_t offset) -> float {
            if (bits == 8) return static_cast<float>(bytes[offset]);
            const unsigned word = bytes[offset] | (static_cast<unsigned>(bytes[offset + 1]) << 8);
            return static_cast<float>(word < 32768u ? static_cast<int>(word) : static_cast<int>(word) - 65536);
        };
        for (std::uint64_t frame = 0; frame < overlap; ++frame)
        {
            const float t = static_cast<float>(frame) / static_cast<float>(overlap - 1);
            for (unsigned channel = 0; channel < channels; ++channel)
            {
                const auto head = frame * frameBytes + channel * (bits / 8u);
                const auto tail = (frames - overlap) * frameBytes + head;
                const float mixed = read(tail) * (1.0f - t) + read(head) * t;
                if (bits == 8) bytes[head] = static_cast<std::uint8_t>(std::clamp(mixed, 0.0f, 255.0f));
                else
                {
                    const auto value = static_cast<std::uint16_t>(static_cast<std::int16_t>(
                        std::clamp(mixed, -32768.0f, 32767.0f)));
                    bytes[head] = static_cast<std::uint8_t>(value & 255u);
                    bytes[head + 1] = static_cast<std::uint8_t>(value >> 8);
                }
            }
        }
        return (frames - overlap) * frameBytes;
    }

    std::shared_ptr<const SoundAnalysisPcm> makeSoundAnalysisPcm(
        const std::int8_t* data, std::uint64_t size, std::uint32_t sampleRate,
        unsigned int channels, unsigned int bitsPerSample)
    {
        if (data == nullptr || sampleRate == 0 ||
            (channels != 1 && channels != 2) ||
            (bitsPerSample != 8 && bitsPerSample != 16)) return {};
        const std::uint64_t bytesPerFrame = channels * (bitsPerSample / 8);
        if (size == 0 || size % bytesPerFrame != 0 ||
            size > (std::numeric_limits<std::size_t>::max)()) return {};

        const std::size_t frameCount = static_cast<std::size_t>(size / bytesPerFrame);
        const std::size_t stride = 1 + (sampleRate - 1) / 24000;
        auto pcm = std::make_shared<SoundAnalysisPcm>();
        pcm->sampleRate = static_cast<double>(sampleRate) / stride;
        pcm->samples.resize(1 + (frameCount - 1) / stride);

        // Three short moving averages form a low-pass before decimation.
        // O(source frames), no trigonometry or full-rate secondary PCM array.
        // Their sub-millisecond filter delay is retained in this causal copy.
        std::array<std::vector<float>, 3> history;
        std::array<float, 3> sums{};
        if (stride > 1)
            for (auto& stage : history) stage.resize(stride, 0.0f);
        const float reciprocalStride = 1.0f / static_cast<float>(stride);
        const auto* bytes = reinterpret_cast<const std::uint8_t*>(data);
        std::size_t output = 0;
        for (std::size_t frame = 0; frame < frameCount; ++frame)
        {
            float sample = 0.0f;
            for (unsigned int channel = 0; channel < channels; ++channel)
            {
                const std::size_t index = frame * static_cast<std::size_t>(bytesPerFrame) +
                    channel * (bitsPerSample / 8);
                if (bitsPerSample == 8)
                    sample += (static_cast<int>(bytes[index]) - 128) * 256.0f;
                else
                {
                    const unsigned int word = bytes[index] |
                        (static_cast<unsigned int>(bytes[index + 1]) << 8);
                    sample += static_cast<float>(word < 32768 ?
                        static_cast<int>(word) : static_cast<int>(word) - 65536);
                }
            }
            sample /= static_cast<float>(channels);
            if (stride > 1)
            {
                const std::size_t slot = frame % stride;
                for (std::size_t stage = 0; stage < history.size(); ++stage)
                {
                    sums[stage] += sample - history[stage][slot];
                    history[stage][slot] = sample;
                    sample = sums[stage] * reciprocalStride;
                }
            }
            if (frame % stride == 0)
                pcm->samples[output++] = static_cast<std::int16_t>(
                    std::clamp(sample, -32768.0f, 32767.0f));
        }
        return pcm;
    }
}
