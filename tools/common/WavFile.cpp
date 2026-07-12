/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Ceciliae is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#include "common/WavFile.h"

#include <cstdint>
#include <cstring>
#include <fstream>
#include <ios>

namespace ceciliae::tools::WavFile
{

namespace
{
constexpr std::uint16_t kFormatPcm        = 0x0001;
constexpr std::uint16_t kFormatFloat      = 0x0003;
constexpr std::uint16_t kFormatExtensible = 0xFFFE;

void setError(std::string* error, const char* message)
{
    if (error != nullptr)
        *error = message;
}

// --- little-endian byte assembly (portable regardless of host endianness) ---

std::uint16_t readU16(const unsigned char* p) noexcept
{
    return static_cast<std::uint16_t>(p[0] | (p[1] << 8));
}

std::uint32_t readU32(const unsigned char* p) noexcept
{
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8)
         | (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}

bool tagEquals(const unsigned char* p, const char* tag) noexcept
{
    return p[0] == tag[0] && p[1] == tag[1] && p[2] == tag[2] && p[3] == tag[3];
}

// Convert one little-endian PCM sample of @p bytesPerSample width to float.
float pcmToFloat(const unsigned char* p, unsigned bytesPerSample) noexcept
{
    switch (bytesPerSample)
    {
        case 2:
        {
            const std::int16_t v = static_cast<std::int16_t>(readU16(p));
            return static_cast<float>(v) / 32768.0f;
        }
        case 3:
        {
            std::int32_t v = p[0] | (p[1] << 8) | (p[2] << 16);
            if (v & 0x800000) // sign-extend 24 -> 32
                v |= ~0xFFFFFF;
            return static_cast<float>(v) / 8388608.0f;
        }
        case 4:
        {
            const std::int32_t v = static_cast<std::int32_t>(readU32(p));
            return static_cast<float>(v) / 2147483648.0f;
        }
        default:
            return 0.0f;
    }
}

void writeU16(std::ofstream& os, std::uint16_t v)
{
    const unsigned char b[2] = { static_cast<unsigned char>(v & 0xFF),
                                 static_cast<unsigned char>((v >> 8) & 0xFF) };
    os.write(reinterpret_cast<const char*>(b), 2);
}

void writeU32(std::ofstream& os, std::uint32_t v)
{
    const unsigned char b[4] = { static_cast<unsigned char>(v & 0xFF),
                                 static_cast<unsigned char>((v >> 8) & 0xFF),
                                 static_cast<unsigned char>((v >> 16) & 0xFF),
                                 static_cast<unsigned char>((v >> 24) & 0xFF) };
    os.write(reinterpret_cast<const char*>(b), 4);
}

void writeTag(std::ofstream& os, const char* tag)
{
    os.write(tag, 4);
}
} // namespace

bool read(const std::string& path, WavData& out, std::string* error)
{
    out = WavData{};

    std::ifstream in(path, std::ios::binary);
    if (!in)
    {
        setError(error, "cannot open input file");
        return false;
    }

    std::vector<unsigned char> buf((std::istreambuf_iterator<char>(in)),
                                   std::istreambuf_iterator<char>());
    if (buf.size() < 12 || !tagEquals(buf.data(), "RIFF") || !tagEquals(buf.data() + 8, "WAVE"))
    {
        setError(error, "not a RIFF/WAVE file");
        return false;
    }

    std::uint16_t formatTag       = 0;
    std::uint16_t channels        = 0;
    std::uint32_t sampleRate      = 0;
    std::uint16_t bitsPerSample   = 0;
    bool          haveFmt         = false;

    const unsigned char* dataPtr  = nullptr;
    std::uint32_t        dataSize  = 0;

    // Walk the chunk list starting just past "WAVE".
    std::size_t pos = 12;
    while (pos + 8 <= buf.size())
    {
        const unsigned char* id = buf.data() + pos;
        const std::uint32_t chunkSize = readU32(buf.data() + pos + 4);
        const std::size_t   body      = pos + 8;
        if (body > buf.size())
            break;
        const std::size_t avail = buf.size() - body;
        const std::size_t take  = chunkSize <= avail ? chunkSize : avail;

        if (tagEquals(id, "fmt ") && take >= 16)
        {
            const unsigned char* f = buf.data() + body;
            formatTag     = readU16(f + 0);
            channels      = readU16(f + 2);
            sampleRate    = readU32(f + 4);
            bitsPerSample = readU16(f + 14);
            if (formatTag == kFormatExtensible && take >= 26)
                formatTag = readU16(f + 24); // real format lives in the sub-format GUID head
            haveFmt = true;
        }
        else if (tagEquals(id, "data"))
        {
            dataPtr  = buf.data() + body;
            dataSize = static_cast<std::uint32_t>(take);
        }

        // Chunks are word-aligned: an odd size carries a single pad byte.
        pos = body + take + (take & 1u);
    }

    if (!haveFmt)
    {
        setError(error, "missing fmt chunk");
        return false;
    }
    if (dataPtr == nullptr || channels == 0 || bitsPerSample == 0)
    {
        setError(error, "missing or empty data chunk");
        return false;
    }
    if (formatTag != kFormatPcm && formatTag != kFormatFloat)
    {
        setError(error, "unsupported sample format (need PCM or IEEE float)");
        return false;
    }

    const unsigned bytesPerSample = bitsPerSample / 8u;
    const unsigned frameBytes     = bytesPerSample * channels;
    if (bytesPerSample == 0 || frameBytes == 0)
    {
        setError(error, "invalid bit depth");
        return false;
    }

    const std::size_t frames = dataSize / frameBytes;

    out.sampleRate  = static_cast<core::SampleRate>(sampleRate);
    out.numChannels = channels;
    out.numFrames   = frames;
    out.interleaved.resize(frames * channels);

    const bool isFloat = (formatTag == kFormatFloat);
    for (std::size_t f = 0; f < frames; ++f)
    {
        for (unsigned c = 0; c < channels; ++c)
        {
            const unsigned char* s = dataPtr + (f * frameBytes) + (c * bytesPerSample);
            float sample = 0.0f;
            if (isFloat && bytesPerSample == 4)
                std::memcpy(&sample, s, sizeof(float));
            else
                sample = pcmToFloat(s, bytesPerSample);
            out.interleaved[f * channels + c] = sample;
        }
    }

    return true;
}

bool write(const std::string& path, const WavData& data, std::string* error)
{
    if (data.numChannels == 0 || data.sampleRate <= 0.0)
    {
        setError(error, "refusing to write empty / unspecified audio");
        return false;
    }

    std::ofstream os(path, std::ios::binary | std::ios::trunc);
    if (!os)
    {
        setError(error, "cannot open output file for writing");
        return false;
    }

    const std::uint16_t channels      = static_cast<std::uint16_t>(data.numChannels);
    const std::uint32_t sampleRate    = static_cast<std::uint32_t>(data.sampleRate);
    const std::uint16_t bitsPerSample = 32;
    const std::uint16_t blockAlign    = static_cast<std::uint16_t>(channels * (bitsPerSample / 8));
    const std::uint32_t byteRate      = sampleRate * blockAlign;
    const std::uint32_t dataBytes =
        static_cast<std::uint32_t>(data.numFrames * channels * sizeof(float));
    const std::uint32_t riffSize = 4 + (8 + 16) + (8 + dataBytes);

    writeTag(os, "RIFF");
    writeU32(os, riffSize);
    writeTag(os, "WAVE");

    writeTag(os, "fmt ");
    writeU32(os, 16);
    writeU16(os, kFormatFloat);
    writeU16(os, channels);
    writeU32(os, sampleRate);
    writeU32(os, byteRate);
    writeU16(os, blockAlign);
    writeU16(os, bitsPerSample);

    writeTag(os, "data");
    writeU32(os, dataBytes);
    os.write(reinterpret_cast<const char*>(data.interleaved.data()),
             static_cast<std::streamsize>(dataBytes));

    if (!os)
    {
        setError(error, "write failed (disk full or path not writable?)");
        return false;
    }
    return true;
}

} // namespace ceciliae::tools::WavFile
