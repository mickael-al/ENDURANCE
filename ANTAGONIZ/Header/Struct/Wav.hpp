#ifndef __WAV__
#define __WAV__

#include <cstdint>
#include <iostream>

struct RIFF_Header
{
    uint8_t chunkID[4];
    uint32_t chunkSize;
    uint8_t format[4];
};

#pragma pack(push, 1)
struct BEXT_Chunk
{
    char description[256];
    char originator[32];
    char originatorReference[32];
    char originationDate[10];
    char originationTime[8];
    uint64_t timeReferenceLow;
    uint64_t timeReferenceHigh;
    uint16_t version;
    char umid[64];
    uint8_t reserved[190];
    uint8_t codingHistory[1];
};
#pragma pack(pop)

struct WAVE_Format
{
    uint8_t subChunkID[4];
    uint32_t subChunkSize;
    uint16_t audioFormat;
    uint16_t numChannels;
    uint32_t sampleRate;
    uint32_t byteRate;
    uint16_t blockAlign;
    uint16_t bitsPerSample;
};

#endif //!__WAV__
