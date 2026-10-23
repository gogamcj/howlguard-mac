#include "FeedbackDetector.h"

#include <algorithm>
#include <cmath>

namespace howl
{

namespace
{
    struct ModeParams
    {
        double confirmSec;     // how long a tone must persist before we cut it
        double reactSec;       // minimum time between repeated cuts on the same tone
        float maxSpreadCents;  // pitch wobble allowed (vibrato rejection)
        float thresholdShift;  // dB added to PAPR/PNPR thresholds (+ = more cautious)
        float harmonicShift;   // dB added to the harmonic test (+ = stricter)
    };

    ModeParams paramsFor (Mode m)
    {
        switch (m)
        {
            case Mode::song:    return { 0.30, 0.18,  6.0f,  2.0f,  4.0f };
            case Mode::multi:   return { 0.45, 0.25,  6.0f,  4.0f,  6.0f };
            case Mode::monitor: return { 0.12, 0.08, 25.0f, -4.0f, -4.0f };
            case Mode::speech:
            default:            return { 0.20, 0.12, 20.0f,  0.0f,  0.0f };
        }
    }

    inline float centsBetween (float a, float b)
    {
        return 1200.0f * std::abs (std::log2 (a / b));
    }

    struct Peak
    {
        float freq, db;
        float papr, pnpr, phpr;
    };
}

float FeedbackDetector::Candidate::freqSpreadCents (int n) const
{
    n = std::min (n, histCount);
    if (n < 2)
        return 0.0f;

    float lo = 1.0e9f, hi = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        const float f = freqs[(histPos - 1 - i + 2 * historySize) % historySize];
        lo = std::min (lo, f);
        hi = std::max (hi, f);
    }
    return centsBetween (hi, lo);
}

void FeedbackDetector::prepare (double sr)
{
    sampleRate = sr;
    // ~170 ms analysis window, rounded to a power of two (8192 at 44.1/48 kHz).
    fftOrder = juce::jlimit (11, 15, (int) std::lround (std::log2 (0.17 * sr)));
    fftSize  = 1 << fftOrder;
    fft = std::make_unique<juce::dsp::FFT> (fftOrder);

    window.resize ((size_t) fftSize);
    double sum = 0.0;
    for (int i = 0; i < fftSize; ++i)
    {
        window[(size_t) i] = 0.5f - 0.5f * std::cos (2.0f * juce::MathConstants<float>::pi * (float) i / (float) fftSize);
        sum += window[(size_t) i];
    }
    windowNorm = (float) (sum * 0.5); // sine amplitude A -> |X| = A * sum/2

    fftBuffer.assign ((size_t) fftSize * 2, 0.0f);
    spectrumDb.assign ((size_t) fftSize / 2 + 1, -140.0f);
    powerLin.assign ((size_t) fftSize / 2 + 1, 0.0f);
    reset();
}

void FeedbackDetector::reset()
{
    candidates.clear();
    std::fill (spectrumDb.begin(), spectrumDb.end(), -140.0f);
}

void FeedbackDetector::computeSpectrumDb (const float* frame, std::vector<float>& outDb)
{
    for (int i = 0; i < fftSize; ++i)
        fftBuffer[(size_t) i] = frame[i] * window[(size_t) i];
    std::fill (fftBuffer.begin() + fftSize, fftBuffer.end(), 0.0f);

    fft->performFrequencyOnlyForwardTransform (fftBuffer.data(), true);

    const int numBins = fftSize / 2 + 1;
    outDb.resize ((size_t) numBins);
    const float inv = 1.0f / windowNorm;
    for (int k = 0; k < numBins; ++k)
    {
        const float mag = fftBuffer[(size_t) k] * inv;
        outDb[(size_t) k] = 20.0f * std::log10 (mag + 1.0e-9f);
    }
}

float FeedbackDetector::bandPeakDb (int centreBin, int halfWidth) const
{
    const int n = (int) spectrumDb.size();
    float best = -140.0f;
    for (int k = std::max (1, centreBin - halfWidth); k <= std::min (n - 1, centreBin + halfWidth); ++k)
        best = std::max (best, spectrumDb[(size_t) k]);
    return best;
}

float FeedbackDetector::meanDbOver (int from, int to) const
{
    const int n = (int) powerLin.size();
    from = std::max (1, from);
    to   = std::min (n - 1, to);
    if (to < from)
        return -140.0f;

    double s = 0.0;
    for (int k = from; k <= to; ++k)
        s += powerLin[(size_t) k];
    return (float) (10.0 * std::log10 (s / (to - from + 1) + 1.0e-14));
}

void FeedbackDetector::analyse (const float* frame, double now, Mode mode, float sensitivity,
                                const std::function<bool (float)>& isRemembered,
                                std::vector<Detection>& out)
{
    computeSpectrumDb (frame, spectrumDb);

    const int numBins = (int) spectrumDb.size();
    for (int k = 0; k < numBins; ++k)
        powerLin[(size_t) k] = std::pow (10.0f, spectrumDb[(size_t) k] * 0.1f);

    const auto mp  = paramsFor (mode);
    const float s  = juce::jlimit (0.0f, 1.0f, sensitivity);
    const float paprThr = 36.0f - 18.0f * s + mp.thresholdShift;
    const float pnprThr = 22.0f - 10.0f * s + mp.thresholdShift;
    const float phprThr = 18.0f -  8.0f * s + mp.harmonicShift;
    const float absThr  = -66.0f - 12.0f * s;
    const float growthThr = 4.5f - 3.0f * s; // dB over 3 hops (~64 ms) = runaway howl

    const double binHz = getBinHz();
    const int kMin = std::max (3, (int) std::ceil (60.0 / binHz));
    const int kMax = std::min (numBins - 14, (int) std::floor (std::min (16000.0, sampleRate * 0.45) / binHz));

    const float bandMeanDb = meanDbOver (kMin, kMax);

    // --- find spectral peaks that look like pure, isolated tones
    std::vector<Peak> peaks;
    for (int k = kMin; k <= kMax; ++k)
    {
        const float v = spectrumDb[(size_t) k];
        if (v < absThr)
            continue;
        if (! (v > spectrumDb[(size_t) k - 1] && v >= spectrumDb[(size_t) k + 1]
               && v > spectrumDb[(size_t) k - 2] && v >= spectrumDb[(size_t) k + 2]))
            continue;

        const float papr = v - bandMeanDb;
        if (papr < paprThr)
            continue;

        // energy close by but outside the main lobe of a pure tone
        const float nb = std::max (meanDbOver (k - 12, k - 4), meanDbOver (k + 4, k + 12));
        const float pnpr = v - nb;
        if (pnpr < pnprThr)
            continue;

        // parabolic interpolation for a precise frequency
        const float a = spectrumDb[(size_t) k - 1], b = v, c = spectrumDb[(size_t) k + 1];
        const float denom = a - 2.0f * b + c;
        const float delta = std::abs (denom) > 1.0e-6f ? juce::jlimit (-0.5f, 0.5f, 0.5f * (a - c) / denom) : 0.0f;
        const float freq  = (float) ((k + delta) * binHz);
        const float peakDb = b - 0.25f * (a - c) * delta;

        // harmonic structure: music/voice has partials at 2f, 3f or f/2; howling usually doesn't
        float phpr = 60.0f;
        for (float h : { 2.0f, 3.0f, 0.5f })
        {
            const int hk = (int) std::lround ((k + delta) * h);
            if (hk >= kMin / 2 && hk < numBins - 3)
                phpr = std::min (phpr, peakDb - bandPeakDb (hk, 2));
        }

        peaks.push_back ({ freq, peakDb, papr, pnpr, phpr });
    }

    std::sort (peaks.begin(), peaks.end(), [] (const Peak& x, const Peak& y) { return x.db > y.db; });
    if (peaks.size() > 10)
        peaks.resize (10);

    // --- track candidates over time
    for (auto& c : candidates)
    {
        c.updatedThisFrame = false;
        ++c.frames;
    }

    for (const auto& p : peaks)
    {
        Candidate* match = nullptr;
        float bestCents = 40.0f;
        for (auto& c : candidates)
        {
            const float d = centsBetween (p.freq, c.freq);
            if (d < bestCents && ! c.updatedThisFrame)
            {
                bestCents = d;
                match = &c;
            }
        }

        const bool harmonicOk = p.phpr >= phprThr;
        const bool looseHarmonicOk = p.phpr >= phprThr - 6.0f;

        if (match == nullptr)
        {
            if (! looseHarmonicOk || candidates.size() >= 24)
                continue;
            Candidate c;
            c.freq = p.freq;
            c.firstSeen = now;
            c.frames = 1;
            candidates.push_back (c);
            match = &candidates.back();
        }

        match->updatedThisFrame = true;
        match->misses = 0;
        match->freq = p.freq;
        match->push (p.freq, p.db);
        if (harmonicOk || mode == Mode::speech || mode == Mode::monitor)
            ++match->hits;
    }

    // --- decide
    for (auto& c : candidates)
    {
        if (! c.updatedThisFrame)
        {
            ++c.misses;
            continue;
        }

        double confirmSec = mp.confirmSec;
        if (isRemembered && isRemembered (c.freq))
            confirmSec *= 0.5;

        const double age = now - c.firstSeen;
        const float hitRatio = (float) c.hits / (float) std::max (1, c.frames);
        const int recent = std::max (3, (int) std::lround (confirmSec * sampleRate / getHopSize()));
        const float spread = c.freqSpreadCents (recent);
        const bool sustained = c.histCount < 2 || c.levelAgo (0) >= c.levelAgo (recent) - 6.0f;

        bool confirmed = age >= confirmSec && hitRatio >= 0.75f && spread <= mp.maxSpreadCents && sustained;

        // a fast-growing pure tone is a howl that is starting: act immediately
        if (! confirmed && c.histCount >= 4 && hitRatio >= 0.75f)
        {
            const float growth = c.levelAgo (0) - c.levelAgo (3);
            if (growth >= growthThr && c.freqSpreadCents (4) <= 10.0f)
                confirmed = true;
        }

        if (confirmed && now - c.lastAction >= mp.reactSec)
        {
            out.push_back ({ c.freq, c.levelAgo (0) });
            c.lastAction = now;
            c.acted = true;
        }
    }

    candidates.erase (std::remove_if (candidates.begin(), candidates.end(),
                                      [] (const Candidate& c) { return c.misses > 2; }),
                      candidates.end());
}

void FeedbackDetector::getWatching (std::vector<float>& freqs) const
{
    freqs.clear();
    for (const auto& c : candidates)
        if (c.misses == 0 && c.hits >= 2)
            freqs.push_back (c.freq);
}

} // namespace howl
