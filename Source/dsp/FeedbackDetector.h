#pragma once

#include <juce_dsp/juce_dsp.h>
#include <functional>
#include <vector>

namespace howl
{

enum class Mode { speech = 0, song = 1, multi = 2, monitor = 3 };

// Finds howling (feedback) tones in successive analysis frames.
// Runs on the analysis thread only; never touched by the audio thread.
class FeedbackDetector
{
public:
    struct Detection
    {
        float freq;
        float levelDb;
    };

    void prepare (double sampleRate);
    void reset();

    int getFftSize() const noexcept { return fftSize; }
    int getHopSize() const noexcept { return fftSize / 8; }
    double getBinHz() const noexcept { return sampleRate / fftSize; }

    // Hann-windowed power spectrum in dBFS (a full-scale sine reads 0 dB).
    void computeSpectrumDb (const float* frame, std::vector<float>& outDb);

    // frame holds getFftSize() samples, oldest first. Confirmed howls are appended to out.
    void analyse (const float* frame, double nowSec, Mode mode, float sensitivity,
                  const std::function<bool (float)>& isRemembered,
                  std::vector<Detection>& out);

    const std::vector<float>& getLastSpectrumDb() const noexcept { return spectrumDb; }

    // Frequencies currently being watched as suspected howls (not yet acted on, or still ringing).
    void getWatching (std::vector<float>& freqs) const;

private:
    struct Candidate
    {
        static constexpr int historySize = 32;

        float freq = 0;
        double firstSeen = 0, lastAction = -1.0e9;
        int frames = 0, hits = 0, misses = 0;
        bool updatedThisFrame = false;
        bool acted = false;
        float levels[historySize] {};
        float freqs[historySize] {};
        int histCount = 0, histPos = 0;

        void push (float f, float level)
        {
            levels[histPos] = level;
            freqs[histPos]  = f;
            histPos = (histPos + 1) % historySize;
            histCount = std::min (histCount + 1, historySize);
        }

        float levelAgo (int n) const
        {
            n = std::min (n, histCount - 1);
            return levels[(histPos - 1 - n + 2 * historySize) % historySize];
        }

        float freqSpreadCents (int n) const;
    };

    float bandPeakDb (int centreBin, int halfWidth) const;
    float meanDbOver (int from, int to) const;

    double sampleRate = 48000.0;
    int fftOrder = 13, fftSize = 8192;
    std::unique_ptr<juce::dsp::FFT> fft;
    std::vector<float> window, fftBuffer, spectrumDb, powerLin;
    float windowNorm = 1.0f;

    std::vector<Candidate> candidates;
};

} // namespace howl
