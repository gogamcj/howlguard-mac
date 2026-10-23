#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <atomic>
#include <vector>

#include "dsp/Compressor.h"
#include "dsp/FeedbackDetector.h"
#include "dsp/Notch.h"

namespace howl
{

constexpr int maxNotches = 32;
constexpr int numDisplayPoints = 480;
constexpr float displayMinHz = 20.0f, displayMaxHz = 20000.0f;

inline float displayPointToHz (int i)
{
    return displayMinHz * std::pow (displayMaxHz / displayMinHz, (float) i / (float) (numDisplayPoints - 1));
}

// Per-channel user settings. Written on the message thread, read by audio/analysis threads.
struct Settings
{
    std::atomic<int>   mode { 0 };
    std::atomic<float> strength { 0.75f };
    std::atomic<float> sensitivity { 0.5f };
    std::atomic<float> releaseSec { 10.0f };
    std::atomic<int>   width { 1 };
    std::atomic<bool>  bypass { false };
    std::atomic<bool>  ringOut { false };

    std::atomic<bool>  compOn { false };
    std::atomic<float> compThreshold { -20.0f };
    std::atomic<float> compRatio { 3.0f };
    std::atomic<float> compAttack { 5.0f };
    std::atomic<float> compRelease { 150.0f };
    std::atomic<float> compMakeup { 0.0f };

    juce::ValueTree toValueTree() const;
    void fromValueTree (const juce::ValueTree&);
    void resetToDefaults();
};

struct NotchInfo
{
    int slot;
    float freq;
    float depthDb;
    bool fixed;
};

// One independent processing chain: detector + 32 notches + compressor.
// In the plug-in, one engine processes all (linked) channels; the standalone app runs one per channel.
class ChannelEngine
{
public:
    ChannelEngine();

    // --- lifecycle (message thread, audio stopped)
    void prepare (double sampleRate, int maxBlockSize);

    // --- audio thread
    void process (float* const* chans, int numChans, int numSamples) noexcept;

    // --- analysis thread
    void runAnalysis (bool wantOutputSpectrum);

    // --- message thread (UI)
    Settings settings;
    juce::String name;

    struct Display
    {
        std::array<float, numDisplayPoints> inputDb {}, outputDb {};
        std::vector<float> watching;
    };
    void getDisplay (Display& d) const;
    std::vector<NotchInfo> getNotches() const;
    double getSampleRate() const noexcept { return sampleRate; }

    float getInputPeakDb() noexcept  { return inPeakDb.exchange (-100.0f); }
    float getOutputPeakDb() noexcept { return outPeakDb.exchange (-100.0f); }
    float getGainReductionDb() const noexcept { return grDb.load(); }
    bool isWatching() const noexcept { return watchingFlag.load(); }
    int getActiveNotchCount() const noexcept { return activeCount.load(); }
    int getFixedNotchCount() const noexcept { return fixedCount.load(); }

    void removeNotch (int slot);
    void toggleFixed (int slot);
    void fixAll();
    void clearNotches (bool includeFixed);

    // Save/restore notches (venue files, plug-in state).
    juce::ValueTree notchesToValueTree (bool includeAuto) const;
    void notchesFromValueTree (const juce::ValueTree&);

private:
    struct Notch
    {
        bool active = false;
        bool fixed = false;
        float freq = 1000.0f;
        float depth = 0.0f;        // dB, <= 0
        float deepest = 0.0f;      // deepest depth reached (for fast re-capture)
        double lastHowl = 0.0;
    };

    struct Memory
    {
        float freq, depth;
        double time;
    };

    struct PublishedSlot
    {
        std::atomic<bool>  active { false };
        std::atomic<float> freq { 1000.0f };
        std::atomic<float> gainDb { 0.0f };
        std::atomic<float> q { 28.0f };
    };

    struct AudioSlot
    {
        float freq = 1000.0f, q = 28.0f, gainDb = 0.0f;
        BiquadCoeffs coeffs;
        std::array<BiquadState, 16> state;
        bool inUse = false;
    };

    // bank logic, guarded by bankLock (analysis + message thread only)
    void onHowl (float freq, double now);
    void tickRelease (double now);
    bool isRemembered (float freq, double now) const;
    void publishLocked();
    double toleranceOct() const;

    double sampleRate = 48000.0;
    int maxBlock = 512;

    juce::CriticalSection bankLock;
    std::array<Notch, maxNotches> notches;
    std::vector<Memory> memory;
    double lastTick = 0.0;

    std::array<PublishedSlot, maxNotches> published;
    std::array<AudioSlot, maxNotches> audioSlots;
    Compressor compressor;

    // audio -> analysis sample transport
    juce::AbstractFifo inFifo { 1 }, outFifo { 1 };
    std::vector<float> inFifoBuf, outFifoBuf, monoScratch;

    // analysis-thread state
    FeedbackDetector detector;
    std::vector<float> inRing, outRing, frame, outSpectrum;
    int ringPos = 0, outRingPos = 0, pendingSamples = 0;
    long long analysedSamples = 0;
    std::vector<FeedbackDetector::Detection> detections;
    std::vector<float> watchScratch;

    mutable juce::SpinLock displayLock;
    Display display;

    std::atomic<float> inPeakDb { -100.0f }, outPeakDb { -100.0f }, grDb { 0.0f };
    std::atomic<bool> watchingFlag { false };
    std::atomic<int> activeCount { 0 }, fixedCount { 0 };
    std::atomic<bool> resetFilters { false };

    JUCE_DECLARE_NON_COPYABLE (ChannelEngine)
};

} // namespace howl
