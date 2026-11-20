#include "SpectrumView.h"
#include "Theme.h"

namespace howl::ui
{

namespace
{
    juce::String hzText (float f)
    {
        return f >= 1000.0f ? juce::String (f / 1000.0f, 2) + " kHz" : (f < 100.0f ? juce::String (f, 1) : juce::String (juce::roundToInt (f))) + " Hz";
    }

    juce::String octText (float oct)
    {
        if (oct >= 0.95f)
            return juce::String (oct, 2) + K (" 옥타브");
        return "1/" + juce::String (juce::roundToInt (1.0f / oct)) + K (" 옥타브");
    }

    const float menuWidths[] = { 1.0f / 40, 1.0f / 20, 1.0f / 10, 1.0f / 6, 1.0f / 3, 1.0f / 2, 1.0f };
    const float menuDepths[] = { -3, -6, -9, -12, -15, -18, -24, -30, -40 };
}

void SpectrumView::update (const ChannelEngine::Display& d, std::vector<NotchInfo> n, double sr, bool bypassed)
{
    display = d;
    notches = std::move (n);
    sampleRate = sr;
    bypass = bypassed;
    if (hovered >= 0 && findNotch (hovered) == nullptr)
        hovered = -1;
    if (dragSlot >= 0 && findNotch (dragSlot) == nullptr)
        endDrag();
    repaint();
}

const NotchInfo* SpectrumView::findNotch (int slot) const
{
    for (const auto& n : notches)
        if (n.slot == slot)
            return &n;
    return nullptr;
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
    return juce::jmap (juce::jlimit (-48.0f, 0.0f, db), 0.0f, -48.0f, r.getY() + r.getHeight() * 0.1f, r.getBottom());
}

float SpectrumView::barLength (float depthDb) const
{
    return std::abs (depthDb) / 40.0f * plotArea().getHeight() * 0.55f;
}

juce::Point<float> SpectrumView::markerPos (const NotchInfo& n) const
{
    return { xForHz (n.freq), plotArea().getY() + 10.0f };
}

float SpectrumView::levelAt (const std::array<float, numDisplayPoints>& db, float hz) const
{
    const float pos = std::log (hz / displayMinHz) / std::log (displayMaxHz / displayMinHz) * (numDisplayPoints - 1);
    return db[(size_t) juce::jlimit (0, numDisplayPoints - 1, juce::roundToInt (pos))];
}

int SpectrumView::notchAt (juce::Point<float> p) const
{
    if (view == View::spectrum)
        return -1;

    int best = -1;
    float bestDist = 9.0f;
    for (const auto& n : notches)
    {
        const auto m = markerPos (n);
        const float barEnd = m.y + barLength (n.depthDb);
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

//==============================================================================
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
        for (int db = 0; db >= -48; db -= 12)
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
        std::vector<BiquadCoeffs> coeffs;
        for (const auto& n : notches)
            coeffs.push_back (BiquadCoeffs::peaking (sampleRate, n.freq, bandwidthToQ (n.widthOct), n.depthDb));

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
            const auto base = n.manual ? colours::manualNotch : n.fixed ? colours::fixedNotch : colours::autoNotch;
            const auto c = base.withAlpha (bypass ? 0.45f : 1.0f);
            g.setColour (c.withAlpha (0.55f));
            g.fillRect (juce::Rectangle<float> (m.x - 1.5f, m.y, 3.0f, barLength (n.depthDb)));
            g.setColour (c);
            const float rad = (n.slot == hovered || n.slot == dragSlot) ? 6.0f : 4.5f;
            g.fillEllipse (m.x - rad, m.y - rad, rad * 2, rad * 2);
        }
    }

    // --- cursor readout: dashed line + frequency and levels
    if (mouseInside && hovered < 0 && ! dragging && r.contains (mousePos))
    {
        const float hz = hzForX (mousePos.x);
        const float dashes[] = { 4.0f, 4.0f };
        g.setColour (colours::text.withAlpha (0.5f));
        g.drawDashedLine (juce::Line<float> (mousePos.x, r.getY(), mousePos.x, r.getBottom()), dashes, 2, 1.0f);

        juce::String txt = hzText (hz);
        if (view != View::notches)
            txt << K ("   입력 ") << juce::String (levelAt (display.inputDb, hz), 1) << " dB"
                << K ("   출력 ") << juce::String (levelAt (display.outputDb, hz), 1) << " dB";

        const float w = view != View::notches ? 250.0f : 90.0f;
        auto box = juce::Rectangle<float> (mousePos.x + 8, r.getY() + 24, w, 20);
        if (box.getRight() > r.getRight())
            box.setX (mousePos.x - 8 - w);
        g.setColour (juce::Colours::black.withAlpha (0.75f));
        g.fillRoundedRectangle (box, 4.0f);
        g.setColour (colours::text);
        g.setFont (juce::FontOptions (12.0f));
        g.drawText (txt, box, juce::Justification::centred);
    }

    g.restoreState();

    // --- notch info
    const int infoSlot = dragSlot >= 0 ? dragSlot : hovered;
    if (const auto* n = findNotch (infoSlot))
    {
        const auto m = markerPos (*n);
        const auto kind = n->manual ? K ("수동") : n->fixed ? K ("고정") : K ("자동");
        const auto txt = hzText (n->freq) + "   " + juce::String (n->depthDb, 1) + " dB   " + octText (n->widthOct) + "   " + kind;
        const float w = 260.0f;
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

//==============================================================================
void SpectrumView::edited()
{
    if (onEdited)
        onEdited();
}

void SpectrumView::mouseMove (const juce::MouseEvent& e)
{
    mousePos = e.position;
    mouseInside = true;
    hovered = notchAt (e.position);
    repaint();
}

void SpectrumView::mouseExit (const juce::MouseEvent&)
{
    mouseInside = false;
    hovered = -1;
    repaint();
}

void SpectrumView::mouseDown (const juce::MouseEvent& e)
{
    mousePos = e.position;
    const int slot = notchAt (e.position);

    if (e.mods.isPopupMenu())
    {
        if (slot >= 0)
            showNotchMenu (slot);
        else if (plotArea().contains (e.position) && view != View::spectrum)
            showBackgroundMenu (hzForX (e.position.x));
        return;
    }

    endDrag();
    if (const auto* n = findNotch (slot))
    {
        dragSlot = slot;
        dragStartFreq = n->freq;
        dragStartDepth = n->depthDb;
    }
}

void SpectrumView::mouseDrag (const juce::MouseEvent& e)
{
    mousePos = e.position;
    if (dragSlot < 0 || engine == nullptr)
        return;

    if (! dragging && e.getDistanceFromDragStart() < 4)
        return; // a plain click must not turn an auto notch into a manual one
    dragging = true;

    const auto d = e.getOffsetFromDragStart().toFloat();
    const float freq  = juce::jlimit (20.0f, 20000.0f, hzForX (xForHz (dragStartFreq) + d.x));
    const float depth = juce::jlimit (maxManualDepthDb, 0.0f, dragStartDepth - d.y * 40.0f / (plotArea().getHeight() * 0.55f));

    engine->setNotchFreq (dragSlot, freq);
    engine->setNotchDepth (dragSlot, depth);

    for (auto& n : notches)
        if (n.slot == dragSlot)
        {
            n.freq = freq;
            n.depthDb = depth;
            n.manual = true;
        }
    repaint();
}

void SpectrumView::mouseUp (const juce::MouseEvent&)
{
    if (dragging)
        edited();
    endDrag();
    repaint();
}

void SpectrumView::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (engine == nullptr || view == View::spectrum || notchAt (e.position) >= 0 || ! plotArea().contains (e.position))
        return;
    if (engine->addManualNotch (hzForX (e.position.x)) >= 0)
        edited();
}

void SpectrumView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    const int slot = notchAt (e.position);
    const auto* n = findNotch (slot);
    if (n == nullptr || engine == nullptr)
    {
        Component::mouseWheelMove (e, w);
        return;
    }

    // wheel up = narrower, wheel down = wider
    const float delta = w.isReversed ? -w.deltaY : w.deltaY;
    const float width = (float) juce::jlimit (minNotchWidthOct, maxNotchWidthOct, (double) n->widthOct * std::pow (2.0, -3.0 * delta));
    engine->setNotchWidth (slot, width);
    for (auto& x : notches)
        if (x.slot == slot)
        {
            x.widthOct = width;
            x.manual = true;
        }
    edited();
    repaint();
}

//==============================================================================
void SpectrumView::showNotchMenu (int slot)
{
    const auto* n = findNotch (slot);
    if (n == nullptr || engine == nullptr)
        return;

    juce::PopupMenu widthMenu, depthMenu;
    for (int i = 0; i < (int) std::size (menuWidths); ++i)
        widthMenu.addItem (100 + i, octText (menuWidths[i]), true, std::abs (std::log2 (n->widthOct / menuWidths[i])) < 0.05f);
    for (int i = 0; i < (int) std::size (menuDepths); ++i)
        depthMenu.addItem (200 + i, juce::String ((int) menuDepths[i]) + " dB", true, std::abs (n->depthDb - menuDepths[i]) < 0.25f);

    juce::PopupMenu m;
    m.addSectionHeader (hzText (n->freq));
    m.addItem (1, K ("노치 삭제"));
    m.addSubMenu (K ("폭"), widthMenu);
    m.addSubMenu (K ("깊이"), depthMenu);
    if (n->manual)
        m.addItem (4, K ("자동 노치로 되돌리기"));
    else
        m.addItem (3, n->fixed ? K ("고정 해제") : K ("고정"));

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition(),
                     [safe = juce::Component::SafePointer<SpectrumView> (this), eng = engine, slot] (int r)
                     {
                         if (safe == nullptr || r == 0)
                             return;
                         auto& e = *eng;
                         if (r == 1)       e.removeNotch (slot);
                         else if (r == 3)  e.toggleFixed (slot);
                         else if (r == 4)  e.revertToAuto (slot);
                         else if (r >= 200) e.setNotchDepth (slot, menuDepths[r - 200]);
                         else if (r >= 100) e.setNotchWidth (slot, menuWidths[r - 100]);
                         safe->edited();
                     });
}

void SpectrumView::showBackgroundMenu (float hz)
{
    juce::PopupMenu m;
    m.addItem (1, K ("노치 추가 (") + hzText (hz) + ")");
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition(),
                     [safe = juce::Component::SafePointer<SpectrumView> (this), eng = engine, hz] (int r)
                     {
                         if (safe != nullptr && r == 1 && eng->addManualNotch (hz) >= 0)
                             safe->edited();
                     });
}

} // namespace howl::ui
