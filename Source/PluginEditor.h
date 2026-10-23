#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"
#include "ui/SpectrumView.h"
#include "ui/Theme.h"

namespace howl::ui
{

// Channel selector shown above everything in the multichannel standalone app.
class ChannelTabs final : public juce::Component
{
public:
    struct Tab
    {
        juce::String name;
        juce::Colour dot;
        int notchCount = 0;
    };

    std::vector<Tab> tabs;
    int selected = 0;
    std::function<void (int)> onSelect, onRename;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

private:
    int tabAt (float x) const;
};

// Horizontal segmented level meter, -60..0 dBFS.
class LevelMeter final : public juce::Component
{
public:
    void setLevel (float db);
    void setGainReductionStyle (bool gr) { grStyle = gr; }
    void paint (juce::Graphics&) override;

private:
    float level = -100.0f, hold = -100.0f;
    int holdFrames = 0;
    bool grStyle = false;
};

} // namespace howl::ui

class HowlGuardEditor final : public juce::AudioProcessorEditor,
                              private juce::Timer
{
public:
    explicit HowlGuardEditor (HowlGuardProcessor&);
    ~HowlGuardEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void selectChannel (int);
    void refreshControls();
    void showClearMenu();
    void saveVenue();
    void loadVenue();
    void renameChannel (int);
    void updateDeviceLabel();
    howl::ChannelEngine& engine() { return proc.getEngine (selected); }

    void setupKnob (juce::Slider&, juce::Label&, const juce::String& title);
    void setupBar (juce::Slider&, juce::Label&, const juce::String& title);

    HowlGuardProcessor& proc;
    howl::ui::LookAndFeel lnf;
    int selected = 0;
    int shownEngines = -1;

    howl::ui::ChannelTabs tabs;
    juce::TextButton modeButtons[4];
    juce::TextButton bypassButton, ringOutButton, fixAllButton, clearButton, audioButton;
    juce::Label deviceLabel;

    juce::TextButton viewButtons[3];
    howl::ui::SpectrumView spectrum;

    juce::Slider strength, sensitivity, release, width;
    juce::Label strengthLabel, sensitivityLabel, releaseLabel, widthLabel;

    juce::TextButton compOnButton;
    juce::Slider compThreshold, compRatio, compAttack, compRelease, compMakeup;
    juce::Label compThresholdLabel, compRatioLabel, compAttackLabel, compReleaseLabel, compMakeupLabel, grLabel;
    howl::ui::LevelMeter grMeter;

    juce::Label activeLabel, watchLabel, inLabel, outLabel;
    howl::ui::LevelMeter inMeter, outMeter;
    juce::TextButton saveVenueButton, loadVenueButton, abButton, copyButton;

    std::unique_ptr<juce::FileChooser> chooser;
    juce::Rectangle<int> rightPanel, compPanel;
    int lastSavedVersion = 0;
    int tick = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HowlGuardEditor)
};
