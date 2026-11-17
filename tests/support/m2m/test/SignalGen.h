#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace m2m::test
{

/** Deterministic PRNG (SplitMix64 seeding + xoshiro256**). Integer-only, so
    the sequence is identical on every platform and compiler. */
class Rng
{
public:
    explicit Rng (std::uint64_t seed = 1) noexcept;

    std::uint64_t nextU64() noexcept;

    /** Uniform in [0, 1), 53-bit resolution. */
    double uniform() noexcept;

    /** Uniform in [-1, 1). */
    double uniformSigned() noexcept { return 2.0 * uniform() - 1.0; }

    /** Uniform in [lo, hi). */
    double range (double lo, double hi) noexcept { return lo + (hi - lo) * uniform(); }

private:
    std::uint64_t s[4];
};

/** Second-order IIR section (RBJ cookbook), direct form I, double state. */
class Biquad
{
public:
    static Biquad lowPass  (double sr, double hz, double q = 0.7071);
    static Biquad highPass (double sr, double hz, double q = 0.7071);
    static Biquad bandPass (double sr, double centreHz, double q); // 0 dB peak gain

    /** Raw coefficients; normalised by a0. */
    static Biquad fromCoefficients (double b0, double b1, double b2, double a0, double a1, double a2) noexcept;

    double process (double x) noexcept;
    void reset() noexcept { x1 = x2 = y1 = y2 = 0.0; }

    void processInPlace (std::vector<float>& buf) noexcept;

private:
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
};

//==============================================================================
// Level helpers. Levels are dBFS: "peak" means max |x|, "rms" means RMS.

float dbToGain (double db) noexcept;
double gainToDb (double gain) noexcept;  // <= 1e-12 -> -240

double rms (const std::vector<float>& buf) noexcept;
double rms (const float* data, std::size_t n) noexcept;
double peak (const std::vector<float>& buf) noexcept;

/** Scale so the peak equals levelDb (no-op on silence). */
void normalisePeak (std::vector<float>& buf, double levelDb) noexcept;

/** Scale so the RMS equals levelDb (no-op on silence). */
void normaliseRms (std::vector<float>& buf, double levelDb) noexcept;

void scale (std::vector<float>& buf, double gain) noexcept;

std::size_t secondsToSamples (double seconds, double sr) noexcept;

//==============================================================================
// Tones. All tones use phase accumulation, so frequency changes are
// continuous (no phase jumps).

/** Harmonic partial amplitudes, index 0 = fundamental. Partials at or above
    Nyquist are skipped. Amplitudes are relative; the rendered tone is
    normalised to the requested level. */
namespace partials
{
    std::vector<float> pure();            // { 1 }
    std::vector<float> voiceLike();       // strong fundamental, falling harmonics
    std::vector<float> weakFundamental(); // fundamental 20 dB below the 2nd harmonic
    std::vector<float> missingFundamental(); // no fundamental at all
}

/** Linear ADSR. attack/decay from the start, release ends at the buffer end;
    sustainLevel is relative (0..1). Segments are clamped to the length. */
struct Adsr
{
    double attackSec    = 0.01;
    double decaySec     = 0.0;
    double sustainLevel = 1.0;
    double releaseSec   = 0.01;
};

/** Multiply buf by the envelope. */
void applyEnvelope (std::vector<float>& buf, double sr, const Adsr& env);

/** Render a harmonic tone that follows a per-sample fundamental track (Hz).
    Peak level is levelDb (before any envelope). */
std::vector<float> renderTrack (double sr, const std::vector<double>& f0Hz,
                                const std::vector<float>& partialAmps, double levelDb);

/** Plain sine, peak level levelDb. */
std::vector<float> sine (double sr, double seconds, double hz, double levelDb);

/** Harmonic tone, peak level levelDb. */
std::vector<float> harmonicTone (double sr, double seconds, double hz,
                                 const std::vector<float>& partialAmps, double levelDb);

/** Harmonic tone with sinusoidal vibrato: f(t) = hz * 2^(depthCents/1200 * sin (2 pi rate t)). */
std::vector<float> vibratoTone (double sr, double seconds, double hz, double depthCents, double rateHz,
                                const std::vector<float>& partialAmps, double levelDb);

/** Hold fromHz for holdBeforeSec, glide (log-frequency, linear in cents) to
    toHz over glideSec, then hold toHz for holdAfterSec. */
std::vector<float> glideTone (double sr, double fromHz, double toHz,
                              double holdBeforeSec, double glideSec, double holdAfterSec,
                              const std::vector<float>& partialAmps, double levelDb);

//==============================================================================
// Noise. Sample values are a pure function of the seed (bit-identical across
// platforms).

/** White noise (uniform distribution) with RMS level rmsDb. */
std::vector<float> whiteNoise (double sr, double seconds, double rmsDb, std::uint64_t seed);

/** Pink (1/f) noise (Paul Kellet's filter on white noise) with RMS level rmsDb. */
std::vector<float> pinkNoise (double sr, double seconds, double rmsDb, std::uint64_t seed);

std::vector<float> silence (double sr, double seconds);

//==============================================================================
// Assembly.

/** Append src to dst. */
void append (std::vector<float>& dst, const std::vector<float>& src);

/** Add src into dst starting at offset (dst grows if needed). */
void mixInto (std::vector<float>& dst, const std::vector<float>& src, std::size_t offset, double gain = 1.0);

/** signal + noise scaled so 20 log10 (refRms / rms (noise)) == snrDb.
    refRms <= 0 means "RMS of the whole signal". The noise is repeated or
    truncated to the signal length. */
std::vector<float> mixWithSnr (const std::vector<float>& signal, const std::vector<float>& noise,
                               double snrDb, double refRms = -1.0);

} // namespace m2m::test
