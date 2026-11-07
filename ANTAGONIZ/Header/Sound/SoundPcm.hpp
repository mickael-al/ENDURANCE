#ifndef GE_SOUND_PCM_HPP
#define GE_SOUND_PCM_HPP

#include <cstdint>
#include <memory>
#include <vector>

namespace Ge
{
    // Optional analysis data, independent of the OpenAL playback buffer.
    // Mono signed PCM, at most 24 kHz: at most 2.88 MB per minute of music.
    // The source rate is divided by an integer, preserving its playback clock.
    struct SoundAnalysisPcm
    {
        std::vector<std::int16_t> samples;
        double sampleRate = 0.0;
    };

    // Accepts the same little-endian mono/stereo 8/16-bit PCM as the WAV loader.
    // Called only when loading a buffer explicitly requesting analysis data.
    std::shared_ptr<const SoundAnalysisPcm> makeSoundAnalysisPcm(
        const std::int8_t* data, std::uint64_t size, std::uint32_t sampleRate,
        unsigned int channels, unsigned int bitsPerSample);
}

#endif
