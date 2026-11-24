#include "SoundPcm.hpp"
#include <cassert>
#include <cstdint>
#include <iostream>
#include <limits>
#include <vector>

int main()
{
    for (unsigned channels : {1u, 2u})
    {
        std::vector<std::int16_t> pcm(100 * channels);
        for (int f = 0; f < 100; ++f)
            for (unsigned c = 0; c < channels; ++c) pcm[f * channels + c] = (f - 50) * (c ? -400 : 400);
        const auto original = pcm;
        const auto bytes = pcm.size() * 2;
        auto* data = reinterpret_cast<std::int8_t*>(pcm.data());
        assert(Ge::blendSoundLoopSeam(data, bytes, 100, channels, 16, 0) == bytes && pcm == original);
        assert(Ge::blendSoundLoopSeam(data, bytes, 100, channels, 16,
            (std::numeric_limits<float>::quiet_NaN)()) == bytes && pcm == original);
        const auto result = Ge::blendSoundLoopSeam(data, bytes, 100, channels, 16, .2f);
        assert(result == 80 * channels * 2);
        for (unsigned c = 0; c < channels; ++c)
        {
            assert(pcm[c] == original[80 * channels + c]); // Continues after last retained frame.
            assert(pcm[19 * channels + c] == original[19 * channels + c]);
        }
        for (unsigned i = 20 * channels; i < pcm.size(); ++i) assert(pcm[i] == original[i]);
    }
    std::vector<std::uint8_t> pcm8(100);
    for (unsigned i = 0; i < pcm8.size(); ++i) pcm8[i] = static_cast<std::uint8_t>(i);
    assert(Ge::blendSoundLoopSeam(reinterpret_cast<std::int8_t*>(pcm8.data()), 100, 100, 1, 8, .1f) == 90);
    assert(pcm8[0] == 90 && pcm8[9] == 9 && pcm8[10] == 10);
    assert(Ge::blendSoundLoopSeam(nullptr, 123, 100, 1, 16, 1) == 123);
    std::cout << "Loop seams: PCM8/PCM16 mono/stereo, boundary continuity, opt-out and invalid input passed\n";
}
