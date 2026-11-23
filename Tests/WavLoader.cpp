#include "SoundTypeLoader.hpp"

#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <vector>

namespace
{
namespace fs = std::filesystem;
void check(bool value, const char* message)
{
    if (!value) throw std::runtime_error(message);
}

struct TemporaryFile
{
    fs::path path = fs::temp_directory_path() / ("endurance-wav-" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".wav");
    ~TemporaryFile() { std::error_code ec; fs::remove(path, ec); }
};

void writeWave(const fs::path& path, unsigned bits, unsigned channels,
               const std::vector<std::int32_t>& samples, unsigned format = 1)
{
    std::string bytes;
    const auto u16 = [&](unsigned value)
    { bytes += static_cast<char>(value); bytes += static_cast<char>(value >> 8); };
    const auto u32 = [&](unsigned value)
    { for (unsigned i = 0; i < 4; ++i) bytes += static_cast<char>(value >> (8 * i)); };
    const unsigned size = static_cast<unsigned>(samples.size()) * (bits / 8);
    bytes += "RIFF"; u32(46 + size + (size & 1u)); bytes += "WAVE";
    // Odd-sized metadata before fmt exercises the RIFF padding path.
    bytes += "JUNK"; u32(1); bytes += 'x'; bytes += '\0';
    bytes += "fmt "; u32(16); u16(format); u16(channels); u32(48000);
    u32(48000 * channels * bits / 8); u16(channels * bits / 8); u16(bits);
    bytes += "data"; u32(size);
    for (auto value : samples)
        for (unsigned i = 0; i < bits / 8; ++i)
            bytes += static_cast<char>(static_cast<std::uint32_t>(value) >> (8 * i));
    if (size & 1u) bytes += '\0';
    std::ofstream file(path, std::ios::binary);
    file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    check(bool(file), "Unable to write WAV fixture");
}

struct Decoded
{
    std::uint64_t size = 123;
    std::uint32_t frequency = 123;
    std::int32_t format = 123;
    double duration = 123;
    std::unique_ptr<std::int8_t[]> pcm;
    Decoded(const fs::path& path, bool mono = false)
        : pcm(Ge::SoundTypeLoader::LoadWavFormat(path.string().c_str(),
            &size, &frequency, &format, &duration, mono)) {}
    int sample16(std::size_t i) const
    {
        std::int16_t sample;
        std::memcpy(&sample, pcm.get() + i * sizeof(sample), sizeof(sample));
        return sample;
    }
};

void conversion24()
{
    TemporaryFile fixture;
    // Includes the sign boundary, clipping limits, sub-LSB values and enough
    // samples to cross several decoder blocks, with an incomplete last block.
    const std::vector<std::int32_t> edges{
        -8388608, 8388607, -4194304, 4194304, -256, 256, -1, 1, 0, 0};
    std::vector<std::int32_t> samples;
    for (int i = 0; i < 1639; ++i) samples.insert(samples.end(), edges.begin(), edges.end());
    writeWave(fixture.path, 24, 2, samples);
    Decoded stereo(fixture.path);
    check(stereo.pcm && stereo.size == samples.size() * 2, "24-bit output size is incorrect");
    check(stereo.frequency == 48000 && stereo.format == AL_FORMAT_STEREO16, "24-bit output format/rate changed");
    check(std::abs(stereo.duration - samples.size() / 2.0 / 48000.0) < 1e-9, "24-bit duration changed");
    for (std::size_t i = 0; i < samples.size(); ++i)
        check(std::abs(stereo.sample16(i) - samples[i] / 256.0) < 1.51,
            "24-bit sign, channel order or quantization is incorrect");

    Decoded mono(fixture.path, true);
    check(mono.pcm && mono.size == stereo.size / 2 && mono.format == AL_FORMAT_MONO16,
        "24-bit stereo-to-mono conversion failed");
    check(mono.duration == stereo.duration, "Downmix must preserve duration");
    for (std::size_t i = 0; i < samples.size() / 2; ++i)
        check(std::abs(mono.sample16(i) - (samples[i * 2] + samples[i * 2 + 1]) / 512.0) < 1.51,
            "24-bit channels must be averaged before quantization");

    samples.assign(20001, 0); // Also tests an odd-sized mono data chunk.
    writeWave(fixture.path, 24, 1, samples);
    Decoded silence(fixture.path);
    check(silence.pcm && silence.format == AL_FORMAT_MONO16 && silence.size == samples.size() * 2,
        "24-bit mono conversion failed");
    long sum = 0;
    bool hasDither = false;
    for (std::size_t i = 0; i < samples.size(); ++i)
    {
        const int value = silence.sample16(i);
        check(std::abs(value) <= 1, "Dither exceeds one output LSB on silence");
        sum += value;
        hasDither |= value != 0;
    }
    check(hasDither && std::abs(sum / double(samples.size())) < 0.02, "Dither must not introduce a DC offset");
    fs::resize_file(fixture.path, fs::file_size(fixture.path) - 4);
    Decoded truncated(fixture.path);
    check(!truncated.pcm && truncated.size == 0 && truncated.duration == 0, "Truncated data must fail safely");
}

void existingFormats()
{
    TemporaryFile fixture;
    writeWave(fixture.path, 16, 2, {-32768, 32767, -500, 500, 1000, 3000});
    Decoded pcm16(fixture.path), mono16(fixture.path, true);
    check(pcm16.pcm && pcm16.size == 12 && pcm16.sample16(0) == -32768 && pcm16.sample16(1) == 32767,
        "Existing 16-bit stereo data changed");
    check(mono16.pcm && mono16.size == 6 && mono16.sample16(0) == 0 && mono16.sample16(2) == 2000,
        "Existing 16-bit downmix changed");
    writeWave(fixture.path, 8, 2, {0, 255, 128, 128});
    Decoded pcm8(fixture.path), mono8(fixture.path, true);
    check(pcm8.pcm && pcm8.size == 4 && pcm8.format == AL_FORMAT_STEREO8 &&
        static_cast<unsigned char>(pcm8.pcm[1]) == 255, "Existing 8-bit data changed");
    check(mono8.pcm && mono8.size == 2 && mono8.format == AL_FORMAT_MONO8 &&
        static_cast<unsigned char>(mono8.pcm[0]) == 127, "Existing 8-bit downmix changed");
    writeWave(fixture.path, 32, 1, {0, 0}, 3);
    check(!Decoded(fixture.path).pcm, "Float WAV remains unsupported");
    writeWave(fixture.path, 24, 3, {0, 0, 0});
    check(!Decoded(fixture.path).pcm, "Multichannel WAV must not enter the mono/stereo decoder");
}

void installedPacks(const fs::path& root)
{
    unsigned count = 0;
    for (const auto* pack : {"Elderweald", "Otherworlds", "Ancient Horrors"})
        for (const auto& entry : fs::recursive_directory_iterator(root / pack))
        {
            if (!entry.is_regular_file() || entry.path().extension() != ".wav") continue;
            Decoded track(entry.path());
            if (!track.pcm) throw std::runtime_error("Unable to decode shipped track: " + entry.path().string());
            check(track.format == AL_FORMAT_STEREO16 && track.frequency > 0 && track.duration > 0,
                "Unexpected shipped music output format");
            check(std::abs(track.duration - track.size / 4.0 / track.frequency) < 1e-9,
                "Shipped music duration and PCM size disagree");
            ++count;
        }
    check(count > 0, "No shipped music decoded");
    std::cout << "Decoded " << count << " installed music files with the production WAV loader\n";
}
}

int main(int argc, char** argv)
{
    try
    {
        conversion24();
        existingFormats();
        if (argc == 2) installedPacks(argv[1]);
        std::cout << "WAV loader: 8/16-bit regression, 24-bit conversion, dither, mono and truncation passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
