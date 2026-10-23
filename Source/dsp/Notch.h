#pragma once

#include <cmath>
#include <complex>

namespace howl
{

// Bandwidth in octaves -> Q for a peaking/notch filter.
inline double bandwidthToQ (double octaves)
{
    const double p = std::pow (2.0, octaves);
    return std::sqrt (p) / (p - 1.0);
}

inline double notchWidthOctaves (int widthIndex)
{
    switch (widthIndex)
    {
        case 0:  return 1.0 / 40.0;
        case 2:  return 1.0 / 10.0;
        default: return 1.0 / 20.0;
    }
}

struct BiquadCoeffs
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;

    // RBJ peaking EQ. gainDb < 0 gives a narrow cut (our "notch").
    static BiquadCoeffs peaking (double sampleRate, double freq, double q, double gainDb)
    {
        BiquadCoeffs c;
        const double A     = std::pow (10.0, gainDb / 40.0);
        const double w0    = 2.0 * M_PI * freq / sampleRate;
        const double cw    = std::cos (w0);
        const double alpha = std::sin (w0) / (2.0 * q);
        const double a0    = 1.0 + alpha / A;

        c.b0 = (1.0 + alpha * A) / a0;
        c.b1 = (-2.0 * cw) / a0;
        c.b2 = (1.0 - alpha * A) / a0;
        c.a1 = (-2.0 * cw) / a0;
        c.a2 = (1.0 - alpha / A) / a0;
        return c;
    }

    double magnitudeDb (double sampleRate, double freq) const
    {
        const double w = 2.0 * M_PI * freq / sampleRate;
        const std::complex<double> z1 = std::polar (1.0, -w);
        const std::complex<double> z2 = z1 * z1;
        const auto num = b0 + b1 * z1 + b2 * z2;
        const auto den = 1.0 + a1 * z1 + a2 * z2;
        return 20.0 * std::log10 (std::abs (num / den) + 1.0e-12);
    }
};

// Transposed direct form II, one instance per audio channel.
struct BiquadState
{
    double z1 = 0, z2 = 0;

    inline float process (const BiquadCoeffs& c, float xf) noexcept
    {
        const double x = xf;
        const double y = c.b0 * x + z1;
        z1 = c.b1 * x - c.a1 * y + z2;
        z2 = c.b2 * x - c.a2 * y;
        return (float) y;
    }

    void reset() noexcept { z1 = z2 = 0; }
};

} // namespace howl
