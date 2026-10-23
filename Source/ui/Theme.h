#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace howl::ui
{

inline juce::String K (const char* utf8) { return juce::String::fromUTF8 (utf8); }

namespace colours
{
    const juce::Colour background { 0xff15181d };
    const juce::Colour panel      { 0xff1e2229 };
    const juce::Colour panelEdge  { 0xff2b313a };
    const juce::Colour text       { 0xffe3e6ea };
    const juce::Colour dimText    { 0xff8a929c };
    const juce::Colour grid       { 0xff2a2f37 };
    const juce::Colour input      { 0xff8b9097 };
    const juce::Colour output     { 0xff3d9df2 };
    const juce::Colour autoNotch  { 0xff3d9df2 };
    const juce::Colour fixedNotch { 0xfff29a3d };
    const juce::Colour watch      { 0xffe84a4a };
    const juce::Colour ok         { 0xff3fbf6a };
    const juce::Colour accent     { 0xff3d9df2 };
}

class LookAndFeel final : public juce::LookAndFeel_V4
{
public:
    LookAndFeel()
    {
        setColourScheme ({ colours::background, colours::panel, colours::panel, colours::panelEdge,
                           colours::text, colours::accent, juce::Colours::white, colours::panelEdge,
                           colours::text });
        setColour (juce::ResizableWindow::backgroundColourId, colours::background);
        setColour (juce::TextButton::buttonColourId, colours::panel);
        setColour (juce::TextButton::buttonOnColourId, colours::accent);
        setColour (juce::TextButton::textColourOffId, colours::text);
        setColour (juce::TextButton::textColourOnId, juce::Colours::white);
        setColour (juce::Slider::rotarySliderFillColourId, colours::accent);
        setColour (juce::Slider::rotarySliderOutlineColourId, colours::panelEdge);
        setColour (juce::Slider::thumbColourId, colours::text);
        setColour (juce::Slider::trackColourId, colours::accent);
        setColour (juce::Slider::backgroundColourId, colours::panelEdge);
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::Label::textColourId, colours::text);
        setColour (juce::PopupMenu::backgroundColourId, colours::panel);

       #if JUCE_MAC
        setDefaultSansSerifTypefaceName ("Apple SD Gothic Neo");
       #elif JUCE_WINDOWS
        setDefaultSansSerifTypefaceName ("Malgun Gothic");
       #endif
    }

    // Horizontal bar sliders: filled bar with the value written on it.
    void drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float sliderPos, float, float,
                           juce::Slider::SliderStyle style, juce::Slider& s) override
    {
        if (style != juce::Slider::LinearBar)
        {
            LookAndFeel_V4::drawLinearSlider (g, x, y, w, h, sliderPos, 0, 0, style, s);
            return;
        }

        auto r = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (0.5f);
        g.setColour (colours::panelEdge);
        g.fillRoundedRectangle (r, 3.0f);
        g.setColour (colours::accent.withAlpha (s.isEnabled() ? 0.75f : 0.3f));
        g.fillRoundedRectangle (r.withRight (sliderPos), 3.0f);
    }
};

} // namespace howl::ui
