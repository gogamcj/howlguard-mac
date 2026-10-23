#include "PluginEditor.h"

#if JucePlugin_Build_Standalone
 #include <juce_audio_utils/juce_audio_utils.h>
 #include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>
#endif

using namespace howl::ui;

namespace
{
    juce::StandalonePluginHolder* standaloneHolder()
    {
       #if JucePlugin_Build_Standalone
        return juce::StandalonePluginHolder::getInstance();
       #else
        return nullptr;
       #endif
    }

    juce::String hzText (float f)
    {
        return f >= 1000.0f ? juce::String (f / 1000.0f, 2) + " kHz" : juce::String (juce::roundToInt (f)) + " Hz";
    }
}

//==============================================================================
namespace howl::ui
{

int ChannelTabs::tabAt (float x) const
{
    if (tabs.empty())
        return -1;
    const float w = (float) getWidth() / (float) tabs.size();
    return juce::jlimit (0, (int) tabs.size() - 1, (int) (x / w));
}

void ChannelTabs::paint (juce::Graphics& g)
{
    if (tabs.empty())
        return;
    const float w = (float) getWidth() / (float) tabs.size();
    for (size_t i = 0; i < tabs.size(); ++i)
    {
        auto r = juce::Rectangle<float> ((float) i * w, 0, w, (float) getHeight()).reduced (1.5f, 0);
        const bool sel = (int) i == selected;
        g.setColour (sel ? colours::panelEdge.brighter (0.25f) : colours::panel);
        g.fillRoundedRectangle (r, 4.0f);
        if (sel)
        {
            g.setColour (colours::accent);
            g.fillRect (r.removeFromBottom (2.0f));
        }

        auto content = r.reduced (6, 0);
        g.setColour (tabs[i].dot);
        g.fillEllipse (content.removeFromLeft (8).withSizeKeepingCentre (8, 8));
        content.removeFromLeft (5);

        g.setFont (juce::FontOptions (12.0f));
        auto countArea = content.removeFromRight (18);
        g.setColour (colours::dimText);
        if (tabs[i].notchCount > 0)
            g.drawText (juce::String (tabs[i].notchCount), countArea, juce::Justification::centredRight);
        g.setColour (sel ? colours::text : colours::dimText.brighter (0.3f));
        g.drawFittedText (tabs[i].name, content.toNearestInt(), juce::Justification::centredLeft, 1, 0.8f);
    }
}

void ChannelTabs::mouseDown (const juce::MouseEvent& e)
{
    const int t = tabAt (e.position.x);
    if (t >= 0 && onSelect)
        onSelect (t);
}

void ChannelTabs::mouseDoubleClick (const juce::MouseEvent& e)
{
    const int t = tabAt (e.position.x);
    if (t >= 0 && onRename)
        onRename (t);
}

//==============================================================================
void LevelMeter::setLevel (float db)
{
    level = db > level ? db : std::max (db, level - 1.5f);
    if (level >= hold) { hold = level; holdFrames = 45; }
    else if (--holdFrames <= 0) hold = std::max (level, hold - 1.0f);
    repaint();
}

void LevelMeter::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (colours::panelEdge);
    g.fillRoundedRectangle (r, 2.0f);

    constexpr int segments = 30;
    const float segW = r.getWidth() / segments;
    for (int i = 0; i < segments; ++i)
    {
        const float segDb = grStyle ? (float) i / segments * 24.0f : -60.0f + (float) (i + 1) / segments * 60.0f;
        const bool lit = grStyle ? level > segDb : level >= segDb - 2.0f;
        if (! lit)
            continue;
        juce::Colour c = grStyle ? colours::fixedNotch
                                 : (segDb > -2.0f ? colours::watch : segDb > -12.0f ? juce::Colour (0xffe8c547) : colours::ok);
        g.setColour (c);
        g.fillRect (juce::Rectangle<float> (r.getX() + i * segW + 1, r.getY() + 2, segW - 2, r.getHeight() - 4));
    }

    if (! grStyle && hold > -60.0f)
    {
        const float x = r.getX() + (hold + 60.0f) / 60.0f * r.getWidth();
        g.setColour (colours::text);
        g.fillRect (juce::Rectangle<float> (juce::jmin (x, r.getRight() - 2), r.getY() + 1, 2, r.getHeight() - 2));
    }
}

} // namespace howl::ui

//==============================================================================
HowlGuardEditor::HowlGuardEditor (HowlGuardProcessor& p)
    : AudioProcessorEditor (p), proc (p)
{
    setLookAndFeel (&lnf);
    selected = juce::jlimit (0, juce::jmax (0, proc.getNumEngines() - 1), proc.displayChannel.load());

    // In the standalone app the input must never be muted: we are a live processor.
    if (auto* h = standaloneHolder())
        h->getMuteInputValue().setValue (false);

    // --- channel tabs
    addAndMakeVisible (tabs);
    tabs.onSelect = [this] (int i) { selectChannel (i); };
    tabs.onRename = [this] (int i) { renameChannel (i); };

    // --- mode buttons
    const char* modeNames[] = { "스피치", "노래", "멀티", "모니터" };
    for (int i = 0; i < 4; ++i)
    {
        auto& b = modeButtons[i];
        b.setButtonText (K (modeNames[i]));
        b.setClickingTogglesState (true);
        b.setRadioGroupId (1001);
        b.onClick = [this, i]
        {
            if (modeButtons[i].getToggleState())
            {
                engine().settings.mode = i;
                proc.markDirty();
            }
        };
        addAndMakeVisible (b);
    }
    modeButtons[0].setConnectedEdges (juce::Button::ConnectedOnRight);
    modeButtons[1].setConnectedEdges (juce::Button::ConnectedOnLeft | juce::Button::ConnectedOnRight);
    modeButtons[2].setConnectedEdges (juce::Button::ConnectedOnLeft | juce::Button::ConnectedOnRight);
    modeButtons[3].setConnectedEdges (juce::Button::ConnectedOnLeft);

    bypassButton.setButtonText (K ("바이패스"));
    bypassButton.setClickingTogglesState (true);
    bypassButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff8a5a20));
    bypassButton.onClick = [this] { engine().settings.bypass = bypassButton.getToggleState(); proc.markDirty(); };
    addAndMakeVisible (bypassButton);

    ringOutButton.setButtonText (K ("링아웃"));
    ringOutButton.setClickingTogglesState (true);
    ringOutButton.setColour (juce::TextButton::buttonOnColourId, colours::fixedNotch.darker (0.2f));
    ringOutButton.onClick = [this] { engine().settings.ringOut = ringOutButton.getToggleState(); };
    addAndMakeVisible (ringOutButton);

    fixAllButton.setButtonText (K ("모두 고정"));
    fixAllButton.onClick = [this] { engine().fixAll(); proc.markDirty(); };
    addAndMakeVisible (fixAllButton);

    clearButton.setButtonText (K ("지우기"));
    clearButton.onClick = [this] { showClearMenu(); };
    addAndMakeVisible (clearButton);

    if (standaloneHolder() != nullptr)
    {
        audioButton.setButtonText (K ("오디오 설정"));
        audioButton.onClick = [] { if (auto* h = standaloneHolder()) h->showAudioSettingsDialog(); };
        addAndMakeVisible (audioButton);
    }

    deviceLabel.setJustificationType (juce::Justification::centredRight);
    deviceLabel.setColour (juce::Label::textColourId, colours::dimText);
    deviceLabel.setFont (juce::FontOptions (12.0f));
    addAndMakeVisible (deviceLabel);

    // --- view buttons
    const char* viewNames[] = { "스펙트럼", "노치", "둘 다" };
    for (int i = 0; i < 3; ++i)
    {
        auto& b = viewButtons[i];
        b.setButtonText (K (viewNames[i]));
        b.setClickingTogglesState (true);
        b.setRadioGroupId (1002);
        b.onClick = [this, i] { if (viewButtons[i].getToggleState()) spectrum.setView ((SpectrumView::View) i); };
        addAndMakeVisible (b);
    }
    viewButtons[2].setToggleState (true, juce::dontSendNotification);
    addAndMakeVisible (spectrum);
    spectrum.onRemoveNotch = [this] (int slot) { engine().removeNotch (slot); proc.markDirty(); };
    spectrum.onToggleFixed = [this] (int slot) { engine().toggleFixed (slot); proc.markDirty(); };

    // --- knobs
    setupKnob (strength, strengthLabel, K ("강도"));
    strength.setRange (0.0, 100.0, 1.0);
    strength.setDoubleClickReturnValue (true, 75.0);
    strength.textFromValueFunction = [] (double v) { return juce::String (juce::roundToInt (v)) + "%"; };
    strength.onValueChange = [this] { engine().settings.strength = (float) strength.getValue() / 100.0f; proc.markDirty(); };

    setupKnob (sensitivity, sensitivityLabel, K ("감도"));
    sensitivity.setRange (0.0, 100.0, 1.0);
    sensitivity.setDoubleClickReturnValue (true, 50.0);
    sensitivity.textFromValueFunction = [] (double v) { return juce::String (juce::roundToInt (v)) + "%"; };
    sensitivity.onValueChange = [this] { engine().settings.sensitivity = (float) sensitivity.getValue() / 100.0f; proc.markDirty(); };

    setupKnob (release, releaseLabel, K ("복원 시간"));
    release.setRange (1.0, 60.0, 0.1);
    release.setSkewFactorFromMidPoint (10.0);
    release.setDoubleClickReturnValue (true, 10.0);
    release.textFromValueFunction = [] (double v) { return juce::String (v, 1) + " s"; };
    release.onValueChange = [this] { engine().settings.releaseSec = (float) release.getValue(); proc.markDirty(); };

    setupKnob (width, widthLabel, K ("노치 폭"));
    width.setRange (0.0, 2.0, 1.0);
    width.setDoubleClickReturnValue (true, 1.0);
    width.textFromValueFunction = [] (double v)
    {
        const int i = juce::roundToInt (v);
        return i == 0 ? K ("좁게 1/40") : i == 2 ? K ("넓게 1/10") : K ("보통 1/20");
    };
    width.onValueChange = [this] { engine().settings.width = juce::roundToInt (width.getValue()); proc.markDirty(); };

    // --- compressor
    compOnButton.setButtonText (K ("컴프레서 켜기"));
    compOnButton.setClickingTogglesState (true);
    compOnButton.onClick = [this] { engine().settings.compOn = compOnButton.getToggleState(); proc.markDirty(); };
    addAndMakeVisible (compOnButton);

    setupBar (compThreshold, compThresholdLabel, K ("스레숄드"));
    compThreshold.setRange (-60.0, 0.0, 0.1);
    compThreshold.setDoubleClickReturnValue (true, -20.0);
    compThreshold.textFromValueFunction = [] (double v) { return juce::String (v, 1) + " dB"; };
    compThreshold.onValueChange = [this] { engine().settings.compThreshold = (float) compThreshold.getValue(); proc.markDirty(); };

    setupBar (compRatio, compRatioLabel, K ("비율"));
    compRatio.setRange (1.0, 20.0, 0.1);
    compRatio.setSkewFactorFromMidPoint (4.0);
    compRatio.setDoubleClickReturnValue (true, 3.0);
    compRatio.textFromValueFunction = [] (double v) { return juce::String (v, 1) + " : 1"; };
    compRatio.onValueChange = [this] { engine().settings.compRatio = (float) compRatio.getValue(); proc.markDirty(); };

    setupBar (compAttack, compAttackLabel, K ("어택"));
    compAttack.setRange (0.1, 100.0, 0.1);
    compAttack.setSkewFactorFromMidPoint (10.0);
    compAttack.setDoubleClickReturnValue (true, 5.0);
    compAttack.textFromValueFunction = [] (double v) { return juce::String (v, 1) + " ms"; };
    compAttack.onValueChange = [this] { engine().settings.compAttack = (float) compAttack.getValue(); proc.markDirty(); };

    setupBar (compRelease, compReleaseLabel, K ("릴리즈"));
    compRelease.setRange (10.0, 2000.0, 1.0);
    compRelease.setSkewFactorFromMidPoint (200.0);
    compRelease.setDoubleClickReturnValue (true, 150.0);
    compRelease.textFromValueFunction = [] (double v) { return juce::String (juce::roundToInt (v)) + " ms"; };
    compRelease.onValueChange = [this] { engine().settings.compRelease = (float) compRelease.getValue(); proc.markDirty(); };

    setupBar (compMakeup, compMakeupLabel, K ("메이크업"));
    compMakeup.setRange (0.0, 24.0, 0.1);
    compMakeup.setDoubleClickReturnValue (true, 0.0);
    compMakeup.textFromValueFunction = [] (double v) { return juce::String (v, 1) + " dB"; };
    compMakeup.onValueChange = [this] { engine().settings.compMakeup = (float) compMakeup.getValue(); proc.markDirty(); };

    grLabel.setText (K ("게인 리덕션"), juce::dontSendNotification);
    grLabel.setFont (juce::FontOptions (12.0f));
    addAndMakeVisible (grLabel);
    grMeter.setGainReductionStyle (true);
    addAndMakeVisible (grMeter);

    // --- bottom row
    for (auto* l : { &activeLabel, &watchLabel })
    {
        l->setFont (juce::FontOptions (13.0f));
        addAndMakeVisible (*l);
    }
    inLabel.setText (K ("입력"), juce::dontSendNotification);
    outLabel.setText (K ("출력"), juce::dontSendNotification);
    for (auto* l : { &inLabel, &outLabel })
    {
        l->setFont (juce::FontOptions (12.0f));
        l->setColour (juce::Label::textColourId, colours::dimText);
        addAndMakeVisible (*l);
    }
    addAndMakeVisible (inMeter);
    addAndMakeVisible (outMeter);

    saveVenueButton.setButtonText (K ("베뉴 저장"));
    saveVenueButton.onClick = [this] { saveVenue(); };
    loadVenueButton.setButtonText (K ("베뉴 불러오기"));
    loadVenueButton.onClick = [this] { loadVenue(); };
    abButton.onClick = [this] { proc.toggleAB (selected); refreshControls(); };
    abButton.setTooltip (K ("설정 A와 B를 오가며 비교합니다"));
    copyButton.setButtonText (K ("복사"));
    copyButton.setTooltip (K ("지금 설정을 다른 슬롯(A/B)에 복사합니다"));
    copyButton.onClick = [this] { proc.copyToOtherSlot (selected); };
    for (auto* b : { &saveVenueButton, &loadVenueButton, &abButton, &copyButton })
        addAndMakeVisible (*b);

    setResizable (true, true);
    setResizeLimits (900, 600, 1900, 1300);
    setSize (1080, 700);

    lastSavedVersion = proc.getStateVersion();
    refreshControls();
    updateDeviceLabel();
    startTimerHz (30);
}

HowlGuardEditor::~HowlGuardEditor()
{
    stopTimer();
    if (auto* h = standaloneHolder())
        h->savePluginState();
    setLookAndFeel (nullptr);
}

void HowlGuardEditor::setupKnob (juce::Slider& s, juce::Label& l, const juce::String& title)
{
    s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    s.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    s.setTextBoxStyle (juce::Slider::TextBoxBelow, true, 90, 18);
    addAndMakeVisible (s);
    l.setText (title, juce::dontSendNotification);
    l.setJustificationType (juce::Justification::centred);
    l.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    addAndMakeVisible (l);
}

void HowlGuardEditor::setupBar (juce::Slider& s, juce::Label& l, const juce::String& title)
{
    s.setSliderStyle (juce::Slider::LinearBar);
    s.setTextBoxIsEditable (false);
    s.setColour (juce::Slider::textBoxTextColourId, colours::text);
    addAndMakeVisible (s);
    l.setText (title, juce::dontSendNotification);
    l.setFont (juce::FontOptions (12.0f));
    addAndMakeVisible (l);
}

//==============================================================================
void HowlGuardEditor::selectChannel (int i)
{
    selected = juce::jlimit (0, juce::jmax (0, proc.getNumEngines() - 1), i);
    proc.displayChannel = selected;
    tabs.selected = selected;
    tabs.repaint();
    refreshControls();
}

void HowlGuardEditor::refreshControls()
{
    auto& s = engine().settings;
    const auto dn = juce::dontSendNotification;

    modeButtons[juce::jlimit (0, 3, s.mode.load())].setToggleState (true, dn);
    bypassButton.setToggleState (s.bypass.load(), dn);
    ringOutButton.setToggleState (s.ringOut.load(), dn);

    strength.setValue (s.strength.load() * 100.0, dn);
    sensitivity.setValue (s.sensitivity.load() * 100.0, dn);
    release.setValue (s.releaseSec.load(), dn);
    width.setValue (s.width.load(), dn);

    compOnButton.setToggleState (s.compOn.load(), dn);
    compThreshold.setValue (s.compThreshold.load(), dn);
    compRatio.setValue (s.compRatio.load(), dn);
    compAttack.setValue (s.compAttack.load(), dn);
    compRelease.setValue (s.compRelease.load(), dn);
    compMakeup.setValue (s.compMakeup.load(), dn);

    abButton.setButtonText (proc.getABSlot (selected) == 0 ? K ("A / b") : K ("a / B"));
}

void HowlGuardEditor::showClearMenu()
{
    juce::PopupMenu m;
    m.addItem (1, K ("자동 노치만 지우기"));
    m.addItem (2, K ("고정 노치까지 모두 지우기"));
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&clearButton),
                     [safe = juce::Component::SafePointer<HowlGuardEditor> (this)] (int r)
                     {
                         if (safe == nullptr || r == 0)
                             return;
                         safe->engine().clearNotches (r == 2);
                         safe->proc.markDirty();
                     });
}

static juce::File venueFolder()
{
    auto dir = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("HowlGuard Venues");
    dir.createDirectory();
    return dir;
}

void HowlGuardEditor::saveVenue()
{
    chooser = std::make_unique<juce::FileChooser> (K ("베뉴 저장"), venueFolder().getChildFile (engine().name + ".howlvenue"), "*.howlvenue");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this] (const juce::FileChooser& fc)
                          {
                              auto f = fc.getResult();
                              if (f == juce::File())
                                  return;
                              f = f.withFileExtension ("howlvenue");
                              if (auto xml = proc.createVenue (selected).createXml())
                                  if (! xml->writeTo (f))
                                      juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "HowlGuard",
                                                                              K ("파일을 저장하지 못했습니다."));
                          });
}

void HowlGuardEditor::loadVenue()
{
    chooser = std::make_unique<juce::FileChooser> (K ("베뉴 불러오기"), venueFolder(), "*.howlvenue");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
                          {
                              const auto f = fc.getResult();
                              if (! f.existsAsFile())
                                  return;
                              auto xml = juce::parseXML (f);
                              auto v = xml != nullptr ? juce::ValueTree::fromXml (*xml) : juce::ValueTree();
                              if (! v.hasType ("HOWLVENUE"))
                              {
                                  juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "HowlGuard",
                                                                          K ("HowlGuard 베뉴 파일이 아닙니다."));
                                  return;
                              }
                              proc.applyVenue (selected, v);
                              refreshControls();
                          });
}

void HowlGuardEditor::renameChannel (int i)
{
    auto* w = new juce::AlertWindow (K ("채널 이름"), K ("새 이름을 입력하세요"), juce::MessageBoxIconType::NoIcon, this);
    w->addTextEditor ("name", proc.getEngine (i).name);
    w->addButton (K ("확인"), 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton (K ("취소"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
    w->enterModalState (true, juce::ModalCallbackFunction::create (
        [this, i, w] (int r)
        {
            if (r == 1)
            {
                const auto name = w->getTextEditorContents ("name").trim();
                if (name.isNotEmpty())
                {
                    proc.getEngine (i).name = name;
                    proc.markDirty();
                }
            }
        }), true);
}

void HowlGuardEditor::updateDeviceLabel()
{
    auto* h = standaloneHolder();
    if (h == nullptr)
    {
        deviceLabel.setText ({}, juce::dontSendNotification);
        return;
    }

    if (auto* dev = h->deviceManager.getCurrentAudioDevice())
    {
        const double sr = dev->getCurrentSampleRate();
        const int buf = dev->getCurrentBufferSizeSamples();
        const double rt = sr > 0 ? (dev->getInputLatencyInSamples() + dev->getOutputLatencyInSamples() + 2 * buf) / sr * 1000.0 : 0.0;
        deviceLabel.setText (dev->getName() + "  |  " + juce::String (sr / 1000.0, 1) + " kHz  |  "
                                 + juce::String (buf) + K (" 샘플  |  왕복 약 ") + juce::String (rt, 1) + " ms",
                             juce::dontSendNotification);
    }
    else
    {
        deviceLabel.setText (K ("오디오 장치 없음 - [오디오 설정]에서 고르세요"), juce::dontSendNotification);
    }
}

//==============================================================================
void HowlGuardEditor::timerCallback()
{
    const int n = proc.getNumEngines();
    if (n != shownEngines)
    {
        shownEngines = n;
        tabs.setVisible (proc.isStandaloneApp() && n > 1);
        if (selected >= n)
            selectChannel (n - 1);
        resized();
    }

    auto& e = engine();

    howl::ChannelEngine::Display d;
    e.getDisplay (d);
    spectrum.update (d, e.getNotches(), e.getSampleRate(), e.settings.width.load(), e.settings.bypass.load());

    inMeter.setLevel (e.getInputPeakDb());
    outMeter.setLevel (e.getOutputPeakDb());
    grMeter.setLevel (e.getGainReductionDb());

    activeLabel.setText (K ("활성 노치  ") + juce::String (e.getActiveNotchCount()) + " / " + juce::String (howl::maxNotches)
                             + K ("   (고정 ") + juce::String (e.getFixedNotchCount()) + ")",
                         juce::dontSendNotification);

    juce::String watch;
    for (size_t i = 0; i < d.watching.size() && i < 4; ++i)
        watch << (i > 0 ? ", " : "") << hzText (d.watching[i]);
    watchLabel.setText (K ("울림 감지  ") + (watch.isEmpty() ? K ("없음") : watch), juce::dontSendNotification);
    watchLabel.setColour (juce::Label::textColourId, watch.isEmpty() ? colours::text : colours::watch);

    if (tabs.isVisible())
    {
        tabs.tabs.resize ((size_t) n);
        for (int i = 0; i < n; ++i)
        {
            auto& ce = proc.getEngine (i);
            auto& t = tabs.tabs[(size_t) i];
            t.name = ce.name;
            t.dot = ce.settings.bypass.load() ? colours::dimText : ce.isWatching() ? colours::watch : colours::ok;
            t.notchCount = ce.getActiveNotchCount();
        }
        tabs.selected = selected;
        tabs.repaint();
    }

    if (++tick % 30 == 0)
        updateDeviceLabel();

    // standalone: auto-save settings a few seconds after a change
    if (tick % 150 == 0 && proc.getStateVersion() != lastSavedVersion)
    {
        lastSavedVersion = proc.getStateVersion();
        if (auto* h = standaloneHolder())
            h->savePluginState();
    }
}

//==============================================================================
void HowlGuardEditor::paint (juce::Graphics& g)
{
    g.fillAll (colours::background);

    for (auto r : { rightPanel, compPanel })
    {
        if (r.isEmpty())
            continue;
        g.setColour (colours::panel);
        g.fillRoundedRectangle (r.toFloat(), 6.0f);
    }

    g.setColour (colours::dimText);
    g.setFont (juce::FontOptions (11.0f));
    g.drawText ("HowlGuard 1.2.0", getLocalBounds().reduced (12, 4).removeFromBottom (12), juce::Justification::bottomRight);
}

void HowlGuardEditor::resized()
{
    auto area = getLocalBounds().reduced (10);
    area.removeFromBottom (8);

    if (tabs.isVisible())
    {
        tabs.setBounds (area.removeFromTop (30));
        area.removeFromTop (6);
    }

    // --- top bar
    auto top = area.removeFromTop (34);
    for (auto& b : modeButtons)
        b.setBounds (top.removeFromLeft (72));
    top.removeFromLeft (14);
    for (auto* b : { &bypassButton, &ringOutButton, &fixAllButton, &clearButton })
    {
        b->setBounds (top.removeFromLeft (84));
        top.removeFromLeft (4);
    }
    if (audioButton.isVisible())
        audioButton.setBounds (top.removeFromRight (104));
    top.removeFromRight (8);
    deviceLabel.setBounds (top);
    area.removeFromTop (8);

    // --- bottom bar
    auto bottom = area.removeFromBottom (52);
    area.removeFromBottom (8);
    {
        auto left = bottom.removeFromLeft (270);
        activeLabel.setBounds (left.removeFromTop (26));
        watchLabel.setBounds (left);

        auto buttons = bottom.removeFromRight (380);
        buttons = buttons.withSizeKeepingCentre (buttons.getWidth(), 30);
        for (auto* b : { &saveVenueButton, &loadVenueButton, &abButton, &copyButton })
        {
            b->setBounds (buttons.removeFromLeft (b == &abButton || b == &copyButton ? 70 : 110));
            buttons.removeFromLeft (5);
        }

        auto meters = bottom.reduced (16, 4);
        auto inRow = meters.removeFromTop (meters.getHeight() / 2);
        inLabel.setBounds (inRow.removeFromLeft (34));
        inMeter.setBounds (inRow.reduced (0, 4));
        outLabel.setBounds (meters.removeFromLeft (34));
        outMeter.setBounds (meters.reduced (0, 4));
    }

    // --- right panel: knobs + compressor
    auto right = area.removeFromRight (260);
    area.removeFromRight (10);

    const int knobBlock = juce::jmin (130, (right.getHeight() - 230) / 2);
    rightPanel = right.removeFromTop (knobBlock * 2 + 12);
    {
        auto k = rightPanel.reduced (6);
        auto row1 = k.removeFromTop (k.getHeight() / 2);
        auto row2 = k;
        auto place = [] (juce::Rectangle<int> r, juce::Slider& s, juce::Label& l)
        {
            l.setBounds (r.removeFromTop (18));
            s.setBounds (r);
        };
        place (row1.removeFromLeft (row1.getWidth() / 2), strength, strengthLabel);
        place (row1, sensitivity, sensitivityLabel);
        place (row2.removeFromLeft (row2.getWidth() / 2), release, releaseLabel);
        place (row2, width, widthLabel);
    }
    right.removeFromTop (10);

    compPanel = right;
    {
        auto c = compPanel.reduced (10, 8);
        compOnButton.setBounds (c.removeFromTop (28));
        c.removeFromTop (6);
        const int rowH = juce::jmin (28, (c.getHeight() - 24) / 6);
        auto row = [&] (juce::Slider& s, juce::Label& l)
        {
            auto r = c.removeFromTop (rowH);
            l.setBounds (r.removeFromLeft (70));
            s.setBounds (r.reduced (0, 3));
        };
        row (compThreshold, compThresholdLabel);
        row (compRatio, compRatioLabel);
        row (compAttack, compAttackLabel);
        row (compRelease, compReleaseLabel);
        row (compMakeup, compMakeupLabel);
        auto r = c.removeFromTop (rowH);
        grLabel.setBounds (r.removeFromLeft (70));
        grMeter.setBounds (r.reduced (0, 6));
    }

    // --- spectrum
    auto viewRow = area.removeFromTop (26);
    for (auto& b : viewButtons)
    {
        b.setBounds (viewRow.removeFromLeft (76));
        viewRow.removeFromLeft (4);
    }
    area.removeFromTop (6);
    spectrum.setBounds (area);
}
