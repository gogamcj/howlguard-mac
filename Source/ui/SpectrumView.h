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
    void setEngine (ChannelEngine* e) { engine = e; endDrag(); }
    void update (const ChannelEngine::Display& d, std::vector<NotchInfo> n, double sampleRate, bool bypassed);

    // Called after the user edits notches by hand (so settings can be saved).
    std::function<void()> onEdited;

    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    juce::Rectangle<float> plotArea() const;
    float xForHz (float hz) const;
    float hzForX (float x) const;
    float yForSpecDb (float db) const;
    float yForEqDb (float db) const;
    float barLength (float depthDb) const;
    juce::Point<float> markerPos (const NotchInfo&) const;
    int notchAt (juce::Point<float>) const;
    const NotchInfo* findNotch (int slot) const;
    float levelAt (const std::array<float, numDisplayPoints>&, float hz) const;
    void showNotchMenu (int slot);
    void showBackgroundMenu (float hz);
    void edited();
    void endDrag() { dragSlot = -1; dragging = false; }

    ChannelEngine* engine = nullptr;
    View view = View::both;
    ChannelEngine::Display display;
    std::vector<NotchInfo> notches;
    double sampleRate = 48000.0;
    bool bypass = false;

    int hovered = -1;
    juce::Point<float> mousePos;
    bool mouseInside = false;

    int dragSlot = -1;
    bool dragging = false;
    float dragStartFreq = 0, dragStartDepth = 0;
};

} // namespace howl::ui
