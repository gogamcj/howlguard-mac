#pragma once

#include <algorithm>
#include <cmath>

namespace howl
{

// Feed-forward peak compressor, linked across all channels of one engine.
class Compressor
{
public:
    void prepare (double sr) { sampleRate = sr; grDb = 0.0f; }

    void setParams (float thresholdDb, float ratio, float attackMs, float releaseMs, float makeupDb)
    {
        threshold = thresholdDb;
        slope     = 1.0f - 1.0f / std::max (1.0f, ratio);
        attCoeff  = coeff (attackMs);
        relCoeff  = coeff (releaseMs);
        makeup    = makeupDb;
    }

    // Returns the gain-reduction applied at the end of the block (dB, positive).
    float process (float* const* chans, int numChans, int numSamples) noexcept
    {
        for (int i = 0; i < numSamples; ++i)
        {
            float peak = 0.0f;
            for (int c = 0; c < numChans; ++c)
                peak = std::max (peak, std::abs (chans[c][i]));

            const float levelDb = 20.0f * std::log10 (peak + 1.0e-9f);
            const float over    = levelDb - threshold;
            const float target  = over > 0.0f ? over * slope : 0.0f;
            const float k       = target > grDb ? attCoeff : relCoeff;
            grDb = target + k * (grDb - target);

            const float g = std::pow (10.0f, (makeup - grDb) / 20.0f);
            for (int c = 0; c < numChans; ++c)
                chans[c][i] *= g;
        }
        return grDb;
    }

    void reset() noexcept { grDb = 0.0f; }

private:
    float coeff (float ms) const
    {
        const double t = std::max (0.05, (double) ms) * 0.001 * sampleRate;
        return (float) std::exp (-1.0 / t);
    }

    double sampleRate = 48000.0;
    float threshold = -20.0f, slope = 2.0f / 3.0f, makeup = 0.0f;
    float attCoeff = 0.0f, relCoeff = 0.0f;
    float grDb = 0.0f;
};

} // namespace howl
