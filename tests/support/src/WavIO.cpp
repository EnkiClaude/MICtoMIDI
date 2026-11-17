#include "m2m/test/WavIO.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>

namespace m2m::test
{

namespace
{
    std::uint32_t le32 (const unsigned char* p) noexcept
    {
        return static_cast<std::uint32_t> (p[0]) | (static_cast<std::uint32_t> (p[1]) << 8)
             | (static_cast<std::uint32_t> (p[2]) << 16) | (static_cast<std::uint32_t> (p[3]) << 24);
    }

    std::uint16_t le16 (const unsigned char* p) noexcept
    {
        return static_cast<std::uint16_t> (p[0] | (p[1] << 8));
    }

    void put16 (std::vector<unsigned char>& b, std::uint32_t v)
    {
        b.push_back (static_cast<unsigned char> (v & 0xff));
        b.push_back (static_cast<unsigned char> ((v >> 8) & 0xff));
    }

    void put32 (std::vector<unsigned char>& b, std::uint32_t v)
    {
        put16 (b, v & 0xffff);
        put16 (b, v >> 16);
    }

    void putTag (std::vector<unsigned char>& b, const char* tag)
    {
        b.insert (b.end(), tag, tag + 4);
    }

    constexpr std::uint16_t formatPcm = 1, formatFloat = 3, formatExtensible = 0xFFFE;
}

std::vector<float> mixToMono (const WavData& wav)
{
    const auto frames = wav.numFrames();
    const auto ch = static_cast<std::size_t> (std::max (1, wav.numChannels));
    std::vector<float> mono (frames);
    for (std::size_t f = 0; f < frames; ++f)
    {
        double acc = 0.0;
        for (std::size_t c = 0; c < ch; ++c)
            acc += static_cast<double> (wav.samples[f * ch + c]);
        mono[f] = static_cast<float> (acc / static_cast<double> (ch));
    }
    return mono;
}

std::vector<float> extractChannel (const WavData& wav, int channel)
{
    if (channel < 0 || channel >= wav.numChannels)
        return {};
    const auto frames = wav.numFrames();
    const auto ch = static_cast<std::size_t> (wav.numChannels);
    std::vector<float> out (frames);
    for (std::size_t f = 0; f < frames; ++f)
        out[f] = wav.samples[f * ch + static_cast<std::size_t> (channel)];
    return out;
}

bool parseWav (const std::vector<unsigned char>& bytes, WavData& out, std::string& error)
{
    out = {};
    const std::size_t size = bytes.size();
    const unsigned char* d = bytes.data();

    if (size < 12 || std::memcmp (d, "RIFF", 4) != 0 || std::memcmp (d + 8, "WAVE", 4) != 0)
    {
        error = "not a RIFF/WAVE file";
        return false;
    }

    bool haveFmt = false;
    std::uint16_t format = 0, channels = 0, bits = 0, blockAlign = 0;
    std::uint32_t rate = 0;
    const unsigned char* data = nullptr;
    std::size_t dataBytes = 0;

    std::size_t pos = 12;
    while (pos + 8 <= size)
    {
        const unsigned char* hdr = d + pos;
        const std::size_t chunkSize = le32 (hdr + 4);
        const std::size_t bodyStart = pos + 8;
        const std::size_t available = size - bodyStart;
        const std::size_t body = std::min (chunkSize, available);

        if (std::memcmp (hdr, "fmt ", 4) == 0)
        {
            if (body < 16)
            {
                error = "fmt chunk too short";
                return false;
            }
            const unsigned char* f = d + bodyStart;
            format     = le16 (f);
            channels   = le16 (f + 2);
            rate       = le32 (f + 4);
            blockAlign = le16 (f + 12);
            bits       = le16 (f + 14);
            if (format == formatExtensible)
            {
                if (body < 40)
                {
                    error = "WAVE_FORMAT_EXTENSIBLE fmt chunk too short";
                    return false;
                }
                format = le16 (f + 24); // first two bytes of the sub-format GUID
            }
            haveFmt = true;
        }
        else if (std::memcmp (hdr, "data", 4) == 0)
        {
            data = d + bodyStart;
            dataBytes = body;
            if (haveFmt)
                break; // everything needed; ignore trailing chunks
        }

        // Advance, with RIFF pad byte for odd sizes; stop on overflow.
        const std::size_t next = bodyStart + chunkSize + (chunkSize & 1u);
        if (next <= pos || chunkSize > available)
            break;
        pos = next;
    }

    if (! haveFmt)
    {
        error = "missing fmt chunk";
        return false;
    }
    if (data == nullptr)
    {
        error = "missing data chunk";
        return false;
    }
    if (channels == 0 || rate == 0)
    {
        error = "invalid channel count or sample rate";
        return false;
    }

    const bool isFloat = format == formatFloat;
    if (! (format == formatPcm || isFloat))
    {
        error = "unsupported WAV format tag " + std::to_string (format);
        return false;
    }
    if (isFloat ? ! (bits == 32 || bits == 64) : ! (bits == 8 || bits == 16 || bits == 24 || bits == 32))
    {
        error = "unsupported bit depth " + std::to_string (bits);
        return false;
    }

    const std::size_t bytesPerSample = bits / 8u;
    const std::size_t frameBytes = std::max<std::size_t> (blockAlign, bytesPerSample * channels);
    const std::size_t frames = dataBytes / frameBytes;

    out.sampleRate = rate;
    out.numChannels = channels;
    out.bitsPerSample = bits;
    out.isFloat = isFloat;
    out.samples.resize (frames * channels);

    for (std::size_t f = 0; f < frames; ++f)
    {
        for (std::size_t c = 0; c < channels; ++c)
        {
            const unsigned char* p = data + f * frameBytes + c * bytesPerSample;
            double v = 0.0;
            if (isFloat && bits == 32)
            {
                const std::uint32_t u = le32 (p);
                float x;
                std::memcpy (&x, &u, 4);
                v = static_cast<double> (x);
            }
            else if (isFloat)
            {
                const std::uint64_t u = static_cast<std::uint64_t> (le32 (p)) | (static_cast<std::uint64_t> (le32 (p + 4)) << 32);
                double x;
                std::memcpy (&x, &u, 8);
                v = x;
            }
            else if (bits == 8)
            {
                v = (static_cast<double> (p[0]) - 128.0) / 128.0;
            }
            else if (bits == 16)
            {
                v = static_cast<double> (static_cast<std::int16_t> (le16 (p))) / 32768.0;
            }
            else if (bits == 24)
            {
                std::int32_t s = static_cast<std::int32_t> (static_cast<std::uint32_t> (p[0])
                                                            | (static_cast<std::uint32_t> (p[1]) << 8)
                                                            | (static_cast<std::uint32_t> (p[2]) << 16));
                if (s & 0x800000)
                    s -= 0x1000000;
                v = static_cast<double> (s) / 8388608.0;
            }
            else
            {
                v = static_cast<double> (static_cast<std::int32_t> (le32 (p))) / 2147483648.0;
            }
            out.samples[f * channels + c] = static_cast<float> (v);
        }
    }
    return true;
}

bool readWav (const std::string& path, WavData& out, std::string& error)
{
    std::ifstream in (path, std::ios::binary);
    if (! in)
    {
        error = "cannot open '" + path + "' for reading";
        return false;
    }
    std::vector<unsigned char> bytes ((std::istreambuf_iterator<char> (in)), std::istreambuf_iterator<char>());
    if (! parseWav (bytes, out, error))
    {
        error = path + ": " + error;
        return false;
    }
    return true;
}

bool writeWav (const std::string& path, const std::vector<float>& interleaved, int numChannels,
               double sampleRate, WavFormat format, std::string& error)
{
    if (numChannels <= 0 || sampleRate <= 0.0)
    {
        error = "invalid channel count or sample rate";
        return false;
    }

    const bool isFloat = format == WavFormat::Float32;
    const std::uint32_t bits = isFloat ? 32 : 16;
    const std::uint32_t ch = static_cast<std::uint32_t> (numChannels);
    const std::uint32_t rate = static_cast<std::uint32_t> (std::lround (sampleRate));
    const std::uint32_t blockAlign = ch * bits / 8;
    const std::size_t frames = interleaved.size() / ch;
    const std::size_t dataBytes64 = frames * blockAlign;
    if (dataBytes64 > 0xFFFFFFF0u)
    {
        error = "audio too long for a WAV file";
        return false;
    }
    const auto dataBytes = static_cast<std::uint32_t> (dataBytes64);

    std::vector<unsigned char> b;
    b.reserve (44 + dataBytes + 1);
    putTag (b, "RIFF");
    put32 (b, 36 + dataBytes + (dataBytes & 1u));
    putTag (b, "WAVE");
    putTag (b, "fmt ");
    put32 (b, 16);
    put16 (b, isFloat ? formatFloat : formatPcm);
    put16 (b, ch);
    put32 (b, rate);
    put32 (b, rate * blockAlign);
    put16 (b, blockAlign);
    put16 (b, bits);
    putTag (b, "data");
    put32 (b, dataBytes);

    for (std::size_t i = 0; i < frames * ch; ++i)
    {
        const float x = interleaved[i];
        if (isFloat)
        {
            std::uint32_t u;
            std::memcpy (&u, &x, 4);
            put32 (b, u);
        }
        else
        {
            const double clipped = std::clamp (static_cast<double> (x), -1.0, 1.0);
            const auto s = static_cast<std::int32_t> (std::clamp (std::lround (clipped * 32768.0), -32768L, 32767L));
            put16 (b, static_cast<std::uint32_t> (static_cast<std::uint16_t> (static_cast<std::int16_t> (s))));
        }
    }
    if (dataBytes & 1u)
        b.push_back (0);

    std::ofstream o (path, std::ios::binary | std::ios::trunc);
    if (! o)
    {
        error = "cannot open '" + path + "' for writing";
        return false;
    }
    o.write (reinterpret_cast<const char*> (b.data()), static_cast<std::streamsize> (b.size()));
    if (! o)
    {
        error = "error writing '" + path + "'";
        return false;
    }
    return true;
}

} // namespace m2m::test
