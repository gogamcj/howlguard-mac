#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <memory>

#include "ChannelEngine.h"

class HowlGuardProcessor final : public juce::AudioProcessor
{
public:
    static constexpr int maxChannels = 16;

    HowlGuardProcessor();
    ~HowlGuardProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "HowlGuard"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    //==============================================================================
    bool isStandaloneApp() const noexcept { return standalone; }

    // Number of independently processed channels right now (1 in the plug-in).
    int getNumEngines() const noexcept { return numEngines.load(); }
    howl::ChannelEngine& getEngine (int i) { return *engines[(size_t) juce::jlimit (0, maxChannels - 1, i)]; }

    std::atomic<int> displayChannel { 0 };

    // Venue files and A/B compare work on one channel.
    juce::ValueTree createVenue (int channel);
    void applyVenue (int channel, const juce::ValueTree&);
    void toggleAB (int channel);
    void copyToOtherSlot (int channel);
    int getABSlot (int channel) const { return ab[(size_t) channel].current; }

    // Bumped whenever the user changes something, so the standalone app can auto-save.
    void markDirty() noexcept { ++stateVersion; }
    int getStateVersion() const noexcept { return stateVersion.load(); }

private:
    class AnalysisThread;
    static BusesProperties makeBuses();

    struct ABState
    {
        juce::ValueTree other;
        int current = 0;
    };

    bool standalone = false;
    std::array<std::unique_ptr<howl::ChannelEngine>, maxChannels> engines;
    std::array<ABState, maxChannels> ab;
    std::atomic<int> numEngines { 1 };
    std::atomic<int> stateVersion { 0 };
    std::unique_ptr<AnalysisThread> analysis;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HowlGuardProcessor)
};
