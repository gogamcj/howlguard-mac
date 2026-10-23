#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

#include "ChannelEngine.h"

namespace howl::ui
{

class SpectrumView final : public juce::Component
{
public:
    enum class View { spectrum, notches, both };

    void setView (View v) { view = v; repaint(); }
    void update (const ChannelEngine::Display& d, std::vector<NotchInfo> n, double sampleRate, int widthIndex, bool bypassed);

    std::function<void (int slot)> onRemoveNotch;
    std::function<void (int slot)> onToggleFixed;

    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    juce::Rectangle<float> plotArea() const;
    float xForHz (float hz) const;
    float hzForX (float x) const;
    float yForSpecDb (float db) const;
    float yForEqDb (float db) const;
    juce::Point<float> markerPos (const NotchInfo&) const;
    int notchAt (juce::Point<float>) const;

    View view = View::both;
    ChannelEngine::Display display;
    std::vector<NotchInfo> notches;
    double sampleRate = 48000.0;
    int width = 1;
    bool bypass = false;
    int hovered = -1;
};

} // namespace howl::ui
