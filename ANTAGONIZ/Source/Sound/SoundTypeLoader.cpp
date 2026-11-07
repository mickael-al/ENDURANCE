#include "SoundTypeLoader.hpp"

#include <array>
#include <climits>
#include <cstdio>
#include <cstring>
#include <new>

namespace
{
    constexpr std::uint32_t MinimumPcmFormatSize = 16;
    constexpr std::uint32_t MaximumWaveDataSize = 512u * 1024u * 1024u;

    bool readExact(FILE* file, void* destination, std::size_t size)
    {
        return file != nullptr && destination != nullptr &&
            std::fread(destination, 1, size, file) == size;
    }

    bool skipBytes(FILE* file, std::uint64_t size)
    {
        if (file == nullptr || size > static_cast<std::uint64_t>(LONG_MAX))
        {
            return false;
        }
        return std::fseek(file, static_cast<long>(size), SEEK_CUR) == 0;
    }

    bool chunkFitsInFile(FILE* file, std::uint32_t chunkSize)
    {
        const long chunkStart = std::ftell(file);
        if (chunkStart < 0 || std::fseek(file, 0, SEEK_END) != 0)
        {
            return false;
        }
        const long fileEnd = std::ftell(file);
        const bool fits = fileEnd >= chunkStart &&
            static_cast<std::uint64_t>(fileEnd - chunkStart) >= chunkSize;
        return std::fseek(file, chunkStart, SEEK_SET) == 0 && fits;
    }
}

namespace Ge
{
    double CalculateDuration(
        std::uint64_t dataSize,
        std::uint32_t sampleRate,
        std::int32_t bitsPerSample,
        std::int32_t channelCount)
    {
        if (sampleRate == 0 || channelCount <= 0 ||
            (bitsPerSample != 8 && bitsPerSample != 16))
        {
            return 0.0;
        }
        const std::uint64_t bytesPerSecond =
            static_cast<std::uint64_t>(channelCount) *
            static_cast<std::uint64_t>(bitsPerSample / 8) *
            sampleRate;
        return bytesPerSecond == 0
            ? 0.0
            : static_cast<double>(dataSize) /
                static_cast<double>(bytesPerSecond);
    }

    std::int8_t* SoundTypeLoader::LoadWavFormat(
        const char* filePath,
        std::uint64_t* size,
        std::uint32_t* frequency,
        std::int32_t* format,
        double* time,
        bool forceMono)
    {
        if (filePath == nullptr || filePath[0] == '\0' || size == nullptr ||
            frequency == nullptr || format == nullptr || time == nullptr)
        {
            Debug::Error("Invalid arguments passed to WAV loader");
            return nullptr;
        }

        *size = 0;
        *frequency = 0;
        *format = 0;
        *time = 0.0;

        FILE* wavFile = std::fopen(filePath, "rb");
        if (wavFile == nullptr)
        {
            Debug::Error("Unable to open wave file: %s", filePath);
            return nullptr;
        }

        RIFF_Header riffHeader{};
        if (!readExact(wavFile, &riffHeader, sizeof(riffHeader)) ||
            std::memcmp(riffHeader.chunkID, "RIFF", 4) != 0 ||
            std::memcmp(riffHeader.format, "WAVE", 4) != 0)
        {
            Debug::Error("Invalid or truncated WAV header: %s", filePath);
            std::fclose(wavFile);
            return nullptr;
        }

        WAVE_Format waveFormat{};
        bool formatFound = false;
        std::uint32_t dataSize = 0;
        while (true)
        {
            std::array<char, 4> chunkId{};
            std::uint32_t chunkSize = 0;
            if (!readExact(wavFile, chunkId.data(), chunkId.size()) ||
                !readExact(wavFile, &chunkSize, sizeof(chunkSize)))
            {
                Debug::Error(
                    "Reached EOF before finding a usable data chunk: %s",
                    filePath);
                std::fclose(wavFile);
                return nullptr;
            }
            if (!chunkFitsInFile(wavFile, chunkSize))
            {
                Debug::Error("Truncated WAV chunk in %s", filePath);
                std::fclose(wavFile);
                return nullptr;
            }

            if (std::memcmp(chunkId.data(), "fmt ", 4) == 0)
            {
                if (chunkSize < MinimumPcmFormatSize)
                {
                    Debug::Error("Invalid fmt chunk size in %s", filePath);
                    std::fclose(wavFile);
                    return nullptr;
                }
                std::array<std::uint8_t, MinimumPcmFormatSize> formatData{};
                if (!readExact(wavFile, formatData.data(), formatData.size()))
                {
                    Debug::Error("Truncated fmt chunk in %s", filePath);
                    std::fclose(wavFile);
                    return nullptr;
                }
                std::memcpy(&waveFormat.audioFormat, formatData.data(), 2);
                std::memcpy(&waveFormat.numChannels, formatData.data() + 2, 2);
                std::memcpy(&waveFormat.sampleRate, formatData.data() + 4, 4);
                std::memcpy(&waveFormat.byteRate, formatData.data() + 8, 4);
                std::memcpy(&waveFormat.blockAlign, formatData.data() + 12, 2);
                std::memcpy(&waveFormat.bitsPerSample, formatData.data() + 14, 2);
                if (!skipBytes(
                        wavFile,
                        static_cast<std::uint64_t>(chunkSize) -
                            MinimumPcmFormatSize + (chunkSize & 1u)))
                {
                    Debug::Error("Unable to finish reading fmt chunk in %s", filePath);
                    std::fclose(wavFile);
                    return nullptr;
                }
                formatFound = true;
                continue;
            }

            if (std::memcmp(chunkId.data(), "data", 4) == 0)
            {
                if (!formatFound)
                {
                    Debug::Error("WAV data appears before fmt in %s", filePath);
                    std::fclose(wavFile);
                    return nullptr;
                }
                dataSize = chunkSize;
                break;
            }

            if (!skipBytes(
                    wavFile,
                    static_cast<std::uint64_t>(chunkSize) + (chunkSize & 1u)))
            {
                Debug::Error("Unable to skip WAV chunk in %s", filePath);
                std::fclose(wavFile);
                return nullptr;
            }
        }

        const std::uint32_t bytesPerSample = waveFormat.bitsPerSample / 8u;
        const std::uint32_t expectedBlockAlign =
            static_cast<std::uint32_t>(waveFormat.numChannels) * bytesPerSample;
        if (waveFormat.audioFormat != 1 ||
            (waveFormat.numChannels != 1 && waveFormat.numChannels != 2) ||
            (waveFormat.bitsPerSample != 8 && waveFormat.bitsPerSample != 16) ||
            waveFormat.sampleRate == 0 || expectedBlockAlign == 0 ||
            waveFormat.blockAlign != expectedBlockAlign || dataSize == 0 ||
            dataSize > MaximumWaveDataSize ||
            dataSize % expectedBlockAlign != 0)
        {
            Debug::Warn("Unsupported or invalid PCM WAV format in %s", filePath);
            std::fclose(wavFile);
            return nullptr;
        }

        std::int8_t* buffer = new (std::nothrow) std::int8_t[dataSize];
        if (buffer == nullptr || !readExact(wavFile, buffer, dataSize))
        {
            delete[] buffer;
            Debug::Error("Unable to read WAV sample data: %s", filePath);
            std::fclose(wavFile);
            return nullptr;
        }

        std::uint32_t outputChannels = waveFormat.numChannels;
        std::uint32_t outputSize = dataSize;
        if (forceMono && outputChannels == 2)
        {
            // Average in a wider integer type and compact in place. Preserve
            // frame count, sample rate and duration; no clipping or resampling.
            const std::uint32_t frames = dataSize / expectedBlockAlign;
            for (std::uint32_t frame = 0; frame < frames; ++frame)
            {
                if (waveFormat.bitsPerSample == 16)
                {
                    std::int16_t left, right;
                    std::memcpy(&left, buffer + frame * 4, sizeof(left));
                    std::memcpy(&right, buffer + frame * 4 + 2, sizeof(right));
                    const std::int16_t mono = static_cast<std::int16_t>(
                        (static_cast<std::int32_t>(left) + right) / 2);
                    std::memcpy(buffer + frame * 2, &mono, sizeof(mono));
                }
                else
                {
                    // 8-bit WAV PCM is unsigned, centred on 128.
                    const auto* samples = reinterpret_cast<const std::uint8_t*>(buffer);
                    const std::uint8_t mono = static_cast<std::uint8_t>(
                        (static_cast<unsigned int>(samples[frame * 2]) + samples[frame * 2 + 1]) / 2);
                    std::memcpy(buffer + frame, &mono, sizeof(mono));
                }
            }
            outputChannels = 1;
            outputSize /= 2;
        }

        *size = outputSize;
        *frequency = waveFormat.sampleRate;
        if (waveFormat.bitsPerSample == 8)
        {
            *format = outputChannels == 1
                ? AL_FORMAT_MONO8
                : AL_FORMAT_STEREO8;
        }
        else
        {
            *format = outputChannels == 1
                ? AL_FORMAT_MONO16
                : AL_FORMAT_STEREO16;
        }
        *time = CalculateDuration(
            dataSize,
            waveFormat.sampleRate,
            waveFormat.bitsPerSample,
            waveFormat.numChannels);
        std::fclose(wavFile);
        return buffer;
    }

    int SoundTypeLoader::getFileSize(FILE* inFile)
    {
        if (inFile == nullptr || std::fseek(inFile, 0, SEEK_END) != 0)
        {
            return -1;
        }
        const long fileSize = std::ftell(inFile);
        if (fileSize < 0 || fileSize > INT_MAX ||
            std::fseek(inFile, 0, SEEK_SET) != 0)
        {
            return -1;
        }
        return static_cast<int>(fileSize);
    }
}
