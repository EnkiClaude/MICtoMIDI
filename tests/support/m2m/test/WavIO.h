#pragma once

#include <string>
#include <vector>

namespace m2m::test
{

/** Decoded WAV file. samples are interleaved, normalised to [-1, 1]. */
struct WavData
{
    double sampleRate = 0.0;
    int numChannels = 0;
    int bitsPerSample = 0;     // as stored in the file
    bool isFloat = false;      // as stored in the file
    std::vector<float> samples;

    std::size_t numFrames() const noexcept
    {
        return numChannels > 0 ? samples.size() / static_cast<std::size_t> (numChannels) : 0;
    }
};

/** Average of all channels per frame. */
std::vector<float> mixToMono (const WavData& wav);

/** One channel (0-based); empty if out of range. */
std::vector<float> extractChannel (const WavData& wav, int channel);

/** Reads RIFF/WAVE: PCM 8/16/24/32-bit and IEEE float 32/64-bit, including
    WAVE_FORMAT_EXTENSIBLE. Unknown chunks are skipped (odd sizes padded); a
    data chunk that claims more bytes than the file holds is truncated. */
bool readWav (const std::string& path, WavData& out, std::string& error);

/** Parse from memory (same rules as readWav). */
bool parseWav (const std::vector<unsigned char>& bytes, WavData& out, std::string& error);

enum class WavFormat { Pcm16, Float32 };

/** Write interleaved samples (PCM16 is clipped to [-1, 1] and rounded). */
bool writeWav (const std::string& path, const std::vector<float>& interleaved, int numChannels,
               double sampleRate, WavFormat format, std::string& error);

inline bool writeWavMono (const std::string& path, const std::vector<float>& mono, double sampleRate,
                          WavFormat format, std::string& error)
{
    return writeWav (path, mono, 1, sampleRate, format, error);
}

} // namespace m2m::test
