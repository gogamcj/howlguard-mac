#include "SpectrumView.h"
#include "Theme.h"

namespace howl::ui
{

void SpectrumView::update (const ChannelEngine::Display& d, std::vector<NotchInfo> n, double sr, int widthIndex, bool bypassed)
{
    display = d;
    notches = std::move (n);
    sampleRate = sr;
    width = widthIndex;
    bypass = bypassed;
    if (hovered >= 0 && std::none_of (notches.begin(), notches.end(), [this] (const NotchInfo& x) { return x.slot == hovered; }))
        hovered = -1;
    repaint();
}

juce::Rectangle<float> SpectrumView::plotArea() const
{
    return getLocalBounds().toFloat().withTrimmedLeft (36).withTrimmedRight (36).withTrimmedBottom (18).withTrimmedTop (6);
}

float SpectrumView::xForHz (float hz) const
{
    const auto r = plotArea();
    return r.getX() + r.getWidth() * std::log (hz / displayMinHz) / std::log (displayMaxHz / displayMinHz);
}

float SpectrumView::hzForX (float x) const
{
    const auto r = plotArea();
    return displayMinHz * std::pow (displayMaxHz / displayMinHz, (x - r.getX()) / r.getWidth());
}

float SpectrumView::yForSpecDb (float db) const
{
    const auto r = plotArea();
    return juce::jmap (juce::jlimit (-100.0f, 0.0f, db), 0.0f, -100.0f, r.getY(), r.getBottom());
}

float SpectrumView::yForEqDb (float db) const
{
    const auto r = plotArea();
    return juce::jmap (juce::jlimit (-36.0f, 0.0f, db), 0.0f, -36.0f, r.getY() + r.getHeight() * 0.1f, r.getBottom());
}

juce::Point<float> SpectrumView::markerPos (const NotchInfo& n) const
{
    return { xForHz (n.freq), plotArea().getY() + 10.0f };
}

int SpectrumView::notchAt (juce::Point<float> p) const
{
    if (view == View::spectrum)
        return -1;

    int best = -1;
    float bestDist = 9.0f;
    const auto r = plotArea();
    for (const auto& n : notches)
    {
        const auto m = markerPos (n);
        const float barEnd = m.y + std::abs (n.depthDb) / 30.0f * r.getHeight() * 0.45f;
        const float dx = std::abs (p.x - m.x);
        const float dy = p.y < m.y ? m.y - p.y : (p.y > barEnd ? p.y - barEnd : 0.0f);
        const float d = std::sqrt (dx * dx + dy * dy);
        if (d < bestDist)
        {
            bestDist = d;
            best = n.slot;
        }
    }
    return best;
}

void SpectrumView::paint (juce::Graphics& g)
{
    g.fillAll (colours::panel);
    const auto r = plotArea();

    // --- grid
    g.setFont (juce::FontOptions (11.0f));
    for (float hz : { 20.f, 50.f, 100.f, 200.f, 500.f, 1000.f, 2000.f, 5000.f, 10000.f, 20000.f })
    {
        const float x = xForHz (hz);
        g.setColour (colours::grid);
        g.drawVerticalLine ((int) x, r.getY(), r.getBottom());
        g.setColour (colours::dimText);
        const auto label = hz >= 1000.0f ? juce::String (hz / 1000.0f, 0) + "k" : juce::String ((int) hz);
        g.drawText (label, juce::Rectangle<float> (x - 20, r.getBottom() + 2, 40, 14), juce::Justification::centred);
    }

    if (view != View::notches)
    {
        for (int db = 0; db >= -100; db -= 20)
        {
            const float y = yForSpecDb ((float) db);
            g.setColour (colours::grid);
            g.drawHorizontalLine ((int) y, r.getX(), r.getRight());
            g.setColour (colours::dimText);
            g.drawText (juce::String (db), juce::Rectangle<float> (0, y - 7, 32, 14), juce::Justification::centredRight);
        }
    }

    if (view != View::spectrum)
    {
        g.setColour (colours::fixedNotch.withAlpha (0.7f));
        for (int db = 0; db >= -36; db -= 12)
        {
            const float y = yForEqDb ((float) db);
            if (view == View::notches)
            {
                g.setColour (colours::grid);
                g.drawHorizontalLine ((int) y, r.getX(), r.getRight());
            }
            g.setColour (colours::fixedNotch.withAlpha (0.7f));
            g.drawText (juce::String (db), juce::Rectangle<float> (r.getRight() + 4, y - 7, 32, 14), juce::Justification::centredLeft);
        }
    }

    g.saveState();
    g.reduceClipRegion (r.toNearestInt());

    // --- spectra
    if (view != View::notches)
    {
        auto drawSpectrum = [&] (const std::array<float, numDisplayPoints>& db, juce::Colour c, bool fill)
        {
            juce::Path p;
            for (int i = 0; i < numDisplayPoints; ++i)
            {
                const float x = xForHz (displayPointToHz (i));
                const float y = yForSpecDb (db[(size_t) i]);
                if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
            }
            if (fill)
            {
                juce::Path f (p);
                f.lineTo (r.getRight(), r.getBottom());
                f.lineTo (r.getX(), r.getBottom());
                f.closeSubPath();
                g.setColour (c.withAlpha (0.12f));
                g.fillPath (f);
            }
            g.setColour (c);
            g.strokePath (p, juce::PathStrokeType (1.3f));
        };

        drawSpectrum (display.inputDb, colours::input, false);
        drawSpectrum (display.outputDb, colours::output, true);

        g.setColour (colours::watch);
        for (float f : display.watching)
        {
            const float x = xForHz (f);
            g.drawLine (x, r.getY(), x, r.getBottom(), 1.5f);
        }
    }

    // --- notches and combined EQ curve
    if (view != View::spectrum && ! notches.empty())
    {
        const double q = bandwidthToQ (notchWidthOctaves (width));
        std::vector<BiquadCoeffs> coeffs;
        for (const auto& n : notches)
            coeffs.push_back (BiquadCoeffs::peaking (sampleRate, n.freq, q, n.depthDb));

        juce::Path eq;
        const int steps = (int) r.getWidth();
        for (int i = 0; i <= steps; ++i)
        {
            const float x = r.getX() + (float) i;
            const double hz = hzForX (x);
            double db = 0.0;
            if (hz < sampleRate * 0.5)
                for (const auto& c : coeffs)
                    db += c.magnitudeDb (sampleRate, hz);
            const float y = yForEqDb ((float) db);
            if (i == 0) eq.startNewSubPath (x, y); else eq.lineTo (x, y);
        }
        g.setColour (colours::fixedNotch.withAlpha (bypass ? 0.35f : 0.9f));
        g.strokePath (eq, juce::PathStrokeType (1.6f));

        for (const auto& n : notches)
        {
            const auto m = markerPos (n);
            const float len = std::abs (n.depthDb) / 30.0f * r.getHeight() * 0.45f;
            const auto c = (n.fixed ? colours::fixedNotch : colours::autoNotch).withAlpha (bypass ? 0.45f : 1.0f);
            g.setColour (c.withAlpha (0.55f));
            g.fillRect (juce::Rectangle<float> (m.x - 1.5f, m.y, 3.0f, len));
            g.setColour (c);
            const float rad = n.slot == hovered ? 6.0f : 4.5f;
            g.fillEllipse (m.x - rad, m.y - rad, rad * 2, rad * 2);
        }
    }

    g.restoreState();

    // --- hover info
    if (hovered >= 0)
    {
        for (const auto& n : notches)
        {
            if (n.slot != hovered)
                continue;
            const auto m = markerPos (n);
            const auto txt = (n.freq >= 1000.0f ? juce::String (n.freq / 1000.0f, 2) + " kHz" : juce::String (n.freq, 1) + " Hz")
                             + "   " + juce::String (n.depthDb, 1) + " dB"
                             + (n.fixed ? K ("   고정") : K ("   자동"));
            const float w = 190.0f;
            auto box = juce::Rectangle<float> (m.x + 10, m.y + 4, w, 22);
            if (box.getRight() > r.getRight())
                box.setX (m.x - 10 - w);
            g.setColour (juce::Colours::black.withAlpha (0.8f));
            g.fillRoundedRectangle (box, 4.0f);
            g.setColour (colours::text);
            g.setFont (juce::FontOptions (12.0f));
            g.drawText (txt, box, juce::Justification::centred);
        }
    }
}

void SpectrumView::mouseMove (const juce::MouseEvent& e)
{
    const int h = notchAt (e.position);
    if (h != hovered)
    {
        hovered = h;
        repaint();
    }
}

void SpectrumView::mouseExit (const juce::MouseEvent&)
{
    hovered = -1;
    repaint();
}

void SpectrumView::mouseDown (const juce::MouseEvent& e)
{
    if (! e.mods.isPopupMenu())
        return;

    const int slot = notchAt (e.position);
    if (slot < 0)
        return;

    bool fixed = false;
    for (const auto& n : notches)
        if (n.slot == slot)
            fixed = n.fixed;

    juce::PopupMenu m;
    m.addItem (1, K ("노치 삭제"));
    m.addItem (2, fixed ? K ("고정 해제") : K ("고정"));
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition(),
                     [safe = juce::Component::SafePointer<SpectrumView> (this), slot] (int r)
                     {
                         if (safe == nullptr)
                             return;
                         if (r == 1 && safe->onRemoveNotch) safe->onRemoveNotch (slot);
                         if (r == 2 && safe->onToggleFixed) safe->onToggleFixed (slot);
                     });
}

} // namespace howl::ui
