#include "SoundPcm.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <limits>

namespace Ge
{
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
