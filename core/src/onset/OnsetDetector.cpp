#include "m2m/onset/OnsetDetector.h"

#include "m2m/audio/MathUtil.h"
#include "m2m/audio/Window.h"

#include <algorithm>
#include <cmath>

namespace m2m
{

namespace
{
    constexpr float logGamma       = 1000.0f; // log compression: log (1 + gamma * |X|)
    constexpr float floorDb        = -100.0f;
    constexpr float maxRiseDb      = 40.0f;
    constexpr float riseScaleDb    = 20.0f;   // d += rise / riseScaleDb
    constexpr double medianWindowS = 0.1;
    constexpr double riseWindowS   = 0.02;
    constexpr float lowBandHz      = 500.0f;
    constexpr float midBandHz      = 4000.0f;

    float powerToDb (float p) noexcept
    {
        return p > 1.0e-10f ? 10.0f * std::log10 (p) : floorDb;
    }
}

void OnsetDetector::prepare (double sampleRate)
{
    sr = sampleRate > 0.0 ? sampleRate : 48000.0;
    const double scale = sr / 48000.0;

    // Hop is a multiple of 4 so the fine envelope (hop / 4) stays aligned.
    hop = std::max (16, 4 * static_cast<int> (std::lround (16.0 * scale)));
    subBlock = hop / 4;
    fftN = nextPowerOfTwo (std::max (64, static_cast<int> (std::lround (256.0 * scale))));
    numBins = fftN / 2 + 1;

    fft.prepare (fftN);
    window.assign (static_cast<std::size_t> (fftN), 0.0f);
    makeHann (window.data(), fftN);
    frame.assign (static_cast<std::size_t> (fftN), 0.0f);
    mags.assign (static_cast<std::size_t> (numBins), 0.0f);
    prevLog.assign (static_cast<std::size_t> (numBins), 0.0f);
    curLog.assign (static_cast<std::size_t> (numBins), 0.0f);
    history.assign (static_cast<std::size_t> (fftN), 0.0f);

    const int medianLen = std::max (5, static_cast<int> (std::lround (medianWindowS * sr / hop)));
    const int riseLen = std::max (2, static_cast<int> (std::lround (riseWindowS * sr / hop)));
    eHist.assign (static_cast<std::size_t> (riseLen), floorDb);

    dHist.assign (static_cast<std::size_t> (medianLen), 0.0f);
    medScratch.assign (static_cast<std::size_t> (medianLen), 0.0f);

    const int fineNeeded = (fftN + 4 * hop) / subBlock + 16;
    const int fineSize = nextPowerOfTwo (fineNeeded);
    fineDb.assign (static_cast<std::size_t> (fineSize), floorDb);
    fineMask = fineSize - 1;

    setParams (params);
    reset();
}

void OnsetDetector::reset() noexcept
{
    std::fill (history.begin(), history.end(), 0.0f);
    std::fill (prevLog.begin(), prevLog.end(), 0.0f);
    std::fill (dHist.begin(), dHist.end(), 0.0f);
    std::fill (fineDb.begin(), fineDb.end(), floorDb);
    std::fill (eHist.begin(), eHist.end(), floorDb);
    eWrite = 0;

    histWrite = hopFill = subFill = 0;
    hopEnergy = hopPeak = subPeak = 0.0f;
    dWrite = dCount = 0;
    dPrev1 = dPrev2 = thrPrev1 = 0.0f;
    peakPrev1 = peakCur = 0.0f;
    pos = 0;
    lastOnset = -1;
    haveLast = false;
    framesSeen = 0;
    lastD = lastThr = 0.0f;
}

void OnsetDetector::setParams (const OnsetParams& p) noexcept
{
    params = p;
    const float s = std::isfinite (p.sensitivity) ? std::clamp (p.sensitivity, 0.0f, 1.0f) : 0.5f;
    // sensitivity 0 -> margin 0.55, 0.5 -> 0.3, 1 -> 0.05 (in detection-function units)
    margin = 0.05f + 0.5f * (1.0f - s);
    const float refMs = std::isfinite (p.refractoryMs) ? std::clamp (p.refractoryMs, 0.0f, 1000.0f) : 40.0f;
    refractory = static_cast<std::int64_t> (std::lround (refMs * 0.001 * sr));
    minPeak = dbToGain (std::isfinite (p.minLevelDb) ? p.minLevelDb : -45.0f);
}

int OnsetDetector::process (const float* mono, int n, OnsetEvent* out, int maxOut) noexcept
{
    int produced = 0;

    for (int i = 0; i < n; ++i)
    {
        const float x = mono[i];
        const float ax = std::abs (x);

        history[static_cast<std::size_t> (histWrite)] = x;
        histWrite = (histWrite + 1 == fftN) ? 0 : histWrite + 1;

        hopEnergy += x * x;
        hopPeak = std::max (hopPeak, ax);
        subPeak = std::max (subPeak, ax);

        ++pos;

        if (++subFill == subBlock)
        {
            const auto sbIndex = (pos - 1) / subBlock;
            fineDb[static_cast<std::size_t> (sbIndex & fineMask)] = powerToDb (subPeak * subPeak);
            subPeak = 0.0f;
            subFill = 0;
        }

        if (++hopFill == hop)
        {
            OnsetEvent ev;
            if (analyseHop (ev) && produced < maxOut)
                out[produced++] = ev;

            hopFill = 0;
            hopEnergy = 0.0f;
            hopPeak = 0.0f;
        }
    }

    return produced;
}

float OnsetDetector::median() noexcept
{
    const int count = std::min (dCount, static_cast<int> (dHist.size()));
    if (count == 0)
        return 0.0f;

    std::copy (dHist.begin(), dHist.begin() + count, medScratch.begin());
    auto mid = medScratch.begin() + count / 2;
    std::nth_element (medScratch.begin(), mid, medScratch.begin() + count);
    return *mid;
}

bool OnsetDetector::analyseHop (OnsetEvent& ev) noexcept
{
    // Frame = last fftN samples, oldest first.
    float framePeak = 0.0f;
    for (int i = 0; i < fftN; ++i)
    {
        const float v = history[static_cast<std::size_t> ((histWrite + i) & (fftN - 1))];
        framePeak = std::max (framePeak, std::abs (v));
        frame[static_cast<std::size_t> (i)] = v * window[static_cast<std::size_t> (i)];
    }

    fft.magnitudes (frame.data(), mags.data());

    // Scale so a full-scale sine reads ~1 in its bin (Hann coherent gain = 0.5).
    const float magScale = 4.0f / static_cast<float> (fftN);
    const float binHz = static_cast<float> (sr) / static_cast<float> (fftN);
    const int lowEnd = std::clamp (static_cast<int> (lowBandHz / binHz) + 1, 1, numBins);
    const int midEnd = std::clamp (static_cast<int> (midBandHz / binHz) + 1, lowEnd + 1, numBins);

    float fluxLow = 0.0f, fluxMid = 0.0f, fluxHigh = 0.0f;
    for (int k = 0; k < numBins; ++k)
    {
        const auto ks = static_cast<std::size_t> (k);
        const float l = std::log1p (logGamma * magScale * mags[ks]);
        const float diff = std::max (0.0f, l - prevLog[ks]);
        curLog[ks] = l;

        if (k < lowEnd)       fluxLow += diff;
        else if (k < midEnd)  fluxMid += diff;
        else                  fluxHigh += diff;
    }
    std::swap (prevLog, curLog);

    fluxLow  /= static_cast<float> (lowEnd);
    fluxMid  /= static_cast<float> (midEnd - lowEnd);
    fluxHigh /= static_cast<float> (std::max (1, numBins - midEnd));
    const float flux = (fluxLow + fluxMid + fluxHigh) / 3.0f;

    // Broadband rise of the hop energy against the previous two hops.
    const float e = powerToDb (hopEnergy / static_cast<float> (hop));
    // Against the max of the last ~20 ms so the hop-energy ripple of low
    // tones (a 50 Hz kick has a 20 ms period) does not count as a rise.
    const float prevMax = *std::max_element (eHist.begin(), eHist.end());
    const float rise = std::clamp (e - prevMax, 0.0f, maxRiseDb);
    eHist[static_cast<std::size_t> (eWrite)] = e;
    eWrite = (eWrite + 1) % static_cast<int> (eHist.size());

    const float d = flux + rise / riseScaleDb;

    // Threshold for the newest frame from the history *before* it.
    const float thr = median() + margin;

    dHist[static_cast<std::size_t> (dWrite)] = d;
    dWrite = (dWrite + 1) % static_cast<int> (dHist.size());
    ++dCount;
    ++framesSeen;

    bool found = false;

    // Peak picking on the previous frame (one hop look-ahead).
    if (framesSeen >= 3
        && dPrev1 > thrPrev1
        && dPrev1 >= dPrev2
        && dPrev1 > d
        && std::max (peakPrev1, framePeak) >= minPeak)
    {
        // The previous frame ended one hop ago; search its span plus one hop
        // either side.
        const std::int64_t searchEnd = pos;
        std::int64_t searchStart = pos - fftN - 2 * hop;
        if (haveLast)
            searchStart = std::max (searchStart, lastOnset + 1);
        searchStart = std::max<std::int64_t> (searchStart, 0);

        if (searchStart < searchEnd)
        {
            const std::int64_t est = refineOnset (searchStart, searchEnd);
            if (! haveLast || est - lastOnset >= refractory)
            {
                ev.samplePosition = est;
                ev.strength = dPrev1;
                lastOnset = est;
                haveLast = true;
                found = true;
            }
        }
    }

    dPrev2 = dPrev1;
    dPrev1 = d;
    thrPrev1 = thr;
    peakPrev1 = framePeak;
    lastD = d;
    lastThr = thr;
    return found;
}

std::int64_t OnsetDetector::refineOnset (std::int64_t searchStart, std::int64_t searchEnd) const noexcept
{
    // Sub-blocks fully inside [searchStart, searchEnd); pick the one with the
    // largest level rise over the max of the four sub-blocks before it.
    const std::int64_t firstSb = (searchStart + subBlock - 1) / subBlock;
    const std::int64_t endSb = searchEnd / subBlock;

    std::int64_t best = firstSb;
    float bestRise = -1.0e9f;

    for (std::int64_t sb = firstSb; sb < endSb; ++sb)
    {
        float prevMax = floorDb;
        for (std::int64_t j = sb - 4; j < sb; ++j)
            if (j >= 0)
                prevMax = std::max (prevMax, fineDb[static_cast<std::size_t> (j & fineMask)]);

        const float rise = fineDb[static_cast<std::size_t> (sb & fineMask)] - prevMax;
        if (rise > bestRise)
        {
            bestRise = rise;
            best = sb;
        }
    }

    return best * subBlock;
}

} // namespace m2m
