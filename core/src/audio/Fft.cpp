#include "m2m/audio/Fft.h"
#include "m2m/audio/MathUtil.h"

#include <cmath>
#include <cstddef>
#include <numbers>

namespace m2m
{

namespace
{
    std::size_t idx (int i) noexcept { return static_cast<std::size_t> (i); }
}

void Fft::prepare (int size)
{
    n = nextPowerOfTwo (size < 2 ? 2 : size);
    m = n / 2;

    int bits = 0;
    while ((1 << bits) < m)
        ++bits;

    bitRev.assign (idx (m), 0);
    for (int i = 0; i < m; ++i)
    {
        int r = 0;
        for (int b = 0; b < bits; ++b)
            if (i & (1 << b))
                r |= 1 << (bits - 1 - b);
        bitRev[idx (i)] = r;
    }

    const double twoPi = 2.0 * std::numbers::pi;

    twRe.assign (idx (m / 2 > 0 ? m / 2 : 1), 1.0f);
    twIm.assign (twRe.size(), 0.0f);
    for (int j = 0; j < m / 2; ++j)
    {
        const double a = -twoPi * j / m;
        twRe[idx (j)] = static_cast<float> (std::cos (a));
        twIm[idx (j)] = static_cast<float> (std::sin (a));
    }

    splitRe.assign (idx (m + 1), 0.0f);
    splitIm.assign (idx (m + 1), 0.0f);
    for (int k = 0; k <= m; ++k)
    {
        const double a = -twoPi * k / n;
        splitRe[idx (k)] = static_cast<float> (std::cos (a));
        splitIm[idx (k)] = static_cast<float> (std::sin (a));
    }

    workRe.assign (idx (m), 0.0f);
    workIm.assign (idx (m), 0.0f);
    magRe.assign (idx (m + 1), 0.0f);
    magIm.assign (idx (m + 1), 0.0f);
}

void Fft::complexForward (float* re, float* im) const noexcept
{
    for (int i = 0; i < m; ++i)
    {
        const int j = bitRev[idx (i)];
        if (j > i)
        {
            const float tr = re[i]; re[i] = re[j]; re[j] = tr;
            const float ti = im[i]; im[i] = im[j]; im[j] = ti;
        }
    }

    for (int len = 2; len <= m; len <<= 1)
    {
        const int half = len / 2;
        const int step = m / len;
        for (int start = 0; start < m; start += len)
        {
            for (int k = 0; k < half; ++k)
            {
                const float wr = twRe[idx (k * step)];
                const float wi = twIm[idx (k * step)];
                const int a = start + k;
                const int b = a + half;
                const float xr = re[b] * wr - im[b] * wi;
                const float xi = re[b] * wi + im[b] * wr;
                re[b] = re[a] - xr;
                im[b] = im[a] - xi;
                re[a] += xr;
                im[a] += xi;
            }
        }
    }
}

void Fft::forward (const float* in, float* outRe, float* outIm) noexcept
{
    // Pack even/odd samples as one complex sequence of half length.
    for (int k = 0; k < m; ++k)
    {
        workRe[idx (k)] = in[2 * k];
        workIm[idx (k)] = in[2 * k + 1];
    }

    complexForward (workRe.data(), workIm.data());

    // Split: X[k] = E[k] + W^k O[k], with
    // E = (Z[k] + conj Z[m-k]) / 2, O = (Z[k] - conj Z[m-k]) / 2i.
    for (int k = 0; k <= m; ++k)
    {
        const int a = k == m ? 0 : k;
        const int b = k == 0 ? 0 : m - k;
        const float zr = workRe[idx (a)], zi = workIm[idx (a)];
        const float cr = workRe[idx (b)], ci = -workIm[idx (b)];

        const float er = 0.5f * (zr + cr);
        const float ei = 0.5f * (zi + ci);
        // (d) / 2i where d = z - c  ->  (di / 2, -dr / 2)
        const float or_ = 0.5f * (zi - ci);
        const float oi = -0.5f * (zr - cr);

        const float wr = splitRe[idx (k)], wi = splitIm[idx (k)];
        outRe[k] = er + (or_ * wr - oi * wi);
        outIm[k] = ei + (or_ * wi + oi * wr);
    }
}

void Fft::inverse (const float* inRe, const float* inIm, float* out) noexcept
{
    // E[k] = (X[k] + conj X[m-k]) / 2, O[k] = (X[k] - conj X[m-k]) / 2 * W^-k,
    // Z[k] = E[k] + i O[k]. DC/Nyquist imaginary parts are treated as zero.
    for (int k = 0; k < m; ++k)
    {
        const float xr = inRe[k], xi = k == 0 ? 0.0f : inIm[k];
        const float cr = inRe[m - k], ci = k == 0 ? 0.0f : -inIm[m - k];

        const float er = 0.5f * (xr + cr);
        const float ei = 0.5f * (xi + ci);
        const float dr = 0.5f * (xr - cr);
        const float di = 0.5f * (xi - ci);

        // multiply by conj (W^k)
        const float wr = splitRe[idx (k)], wi = -splitIm[idx (k)];
        const float or_ = dr * wr - di * wi;
        const float oi = dr * wi + di * wr;

        // Z = E + i O; store conjugated so a forward transform computes the inverse.
        workRe[idx (k)] = er - oi;
        workIm[idx (k)] = -(ei + or_);
    }

    complexForward (workRe.data(), workIm.data());

    const float scale = 1.0f / static_cast<float> (m);
    for (int k = 0; k < m; ++k)
    {
        out[2 * k]     = workRe[idx (k)] * scale;
        out[2 * k + 1] = -workIm[idx (k)] * scale;
    }
}

void Fft::magnitudes (const float* in, float* outMag) noexcept
{
    forward (in, magRe.data(), magIm.data());
    for (int k = 0; k <= m; ++k)
        outMag[k] = std::sqrt (magRe[idx (k)] * magRe[idx (k)] + magIm[idx (k)] * magIm[idx (k)]);
}

} // namespace m2m
