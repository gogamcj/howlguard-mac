#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
class HowlGuardProcessor::AnalysisThread final : public juce::Thread
{
public:
    explicit AnalysisThread (HowlGuardProcessor& p) : juce::Thread ("HowlGuard analysis"), owner (p) {}

    void run() override
    {
        while (! threadShouldExit())
        {
            const int n = owner.getNumEngines();
            const int shown = owner.displayChannel.load();
            for (int i = 0; i < n && ! threadShouldExit(); ++i)
                owner.getEngine (i).runAnalysis (i == shown);
            wait (5);
        }
    }

private:
    HowlGuardProcessor& owner;
};

//==============================================================================
static bool loadedAsStandalone()
{
    return juce::PluginHostType::getPluginLoadedAs() == juce::AudioProcessor::wrapperType_Standalone;
}

juce::AudioProcessor::BusesProperties HowlGuardProcessor::makeBuses()
{
    if (loadedAsStandalone())
        return BusesProperties()
                   .withInput  ("Input",  juce::AudioChannelSet::discreteChannels (HowlGuardProcessor::maxChannels), true)
                   .withOutput ("Output", juce::AudioChannelSet::discreteChannels (HowlGuardProcessor::maxChannels), true);

    return BusesProperties()
               .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
               .withOutput ("Output", juce::AudioChannelSet::stereo(), true);
}

HowlGuardProcessor::HowlGuardProcessor()
    : AudioProcessor (makeBuses()),
      standalone (loadedAsStandalone())
{
    for (int i = 0; i < maxChannels; ++i)
    {
        engines[(size_t) i] = std::make_unique<howl::ChannelEngine>();
        engines[(size_t) i]->name = juce::String (juce::CharPointer_UTF8 ("\xec\xb1\x84\xeb\x84\x90 ")) + juce::String (i + 1); // "채널 n"
    }
    analysis = std::make_unique<AnalysisThread> (*this);
}

HowlGuardProcessor::~HowlGuardProcessor()
{
    analysis->stopThread (2000);
}

bool HowlGuardProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const int in  = layouts.getMainInputChannels();
    const int out = layouts.getMainOutputChannels();

    if (standalone)
        return in >= 1 && in <= maxChannels && out >= 1 && out <= maxChannels;

    const auto& i = layouts.getMainInputChannelSet();
    const auto& o = layouts.getMainOutputChannelSet();
    if (o == juce::AudioChannelSet::mono())
        return i == juce::AudioChannelSet::mono();
    if (o == juce::AudioChannelSet::stereo())
        return i == juce::AudioChannelSet::mono() || i == juce::AudioChannelSet::stereo();
    return false;
}

void HowlGuardProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    analysis->stopThread (2000);

    const int in = getTotalNumInputChannels();
    numEngines = standalone ? juce::jlimit (1, maxChannels, in) : 1;

    for (auto& e : engines)
        e->prepare (sampleRate, samplesPerBlock);

    analysis->startThread (juce::Thread::Priority::high);
}

void HowlGuardProcessor::releaseResources()
{
    analysis->stopThread (2000);
}

void HowlGuardProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numIn  = getTotalNumInputChannels();
    const int numOut = getTotalNumOutputChannels();
    const int n = buffer.getNumSamples();
    auto* const* chans = buffer.getArrayOfWritePointers();

    if (numIn <= 0)
    {
        buffer.clear();
        return;
    }

    if (standalone)
    {
        const int engineCount = std::min (numIn, numEngines.load());
        for (int c = 0; c < engineCount; ++c)
            engines[(size_t) c]->process (chans + c, 1, n);
        for (int c = engineCount; c < numIn; ++c)
            buffer.clear (c, 0, n);
    }
    else
    {
        engines[0]->process (chans, std::min (numIn, 2), n);
    }

    // mono in -> same signal on every output; otherwise input n -> output n
    for (int c = numIn; c < numOut; ++c)
    {
        if (numIn == 1)
            buffer.copyFrom (c, 0, buffer, 0, 0, n);
        else
            buffer.clear (c, 0, n);
    }
}

//==============================================================================
juce::AudioProcessorEditor* HowlGuardProcessor::createEditor()
{
    return new HowlGuardEditor (*this);
}

void HowlGuardProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    juce::ValueTree root ("HOWLGUARD");
    root.setProperty ("version", 1, nullptr);
    root.setProperty ("displayChannel", displayChannel.load(), nullptr);

    const int count = standalone ? maxChannels : 1;
    for (int i = 0; i < count; ++i)
    {
        auto& e = *engines[(size_t) i];
        juce::ValueTree ch ("CHANNEL");
        ch.setProperty ("index", i, nullptr);
        ch.setProperty ("name", e.name, nullptr);
        ch.setProperty ("abSlot", ab[(size_t) i].current, nullptr);
        ch.appendChild (e.settings.toValueTree(), nullptr);
        ch.appendChild (e.notchesToValueTree (false), nullptr);
        if (ab[(size_t) i].other.isValid())
        {
            juce::ValueTree other ("ABOTHER");
            other.appendChild (ab[(size_t) i].other.createCopy(), nullptr);
            ch.appendChild (other, nullptr);
        }
        root.appendChild (ch, nullptr);
    }

    if (auto xml = root.createXml())
        copyXmlToBinary (*xml, dest);
}

void HowlGuardProcessor::setStateInformation (const void* data, int size)
{
    auto xml = getXmlFromBinary (data, size);
    if (xml == nullptr)
        return;

    auto root = juce::ValueTree::fromXml (*xml);
    if (! root.hasType ("HOWLGUARD"))
        return;

    displayChannel = juce::jlimit (0, maxChannels - 1, (int) root.getProperty ("displayChannel", 0));

    for (const auto& ch : root)
    {
        const int i = (int) ch.getProperty ("index", -1);
        if (! juce::isPositiveAndBelow (i, maxChannels))
            continue;
        auto& e = *engines[(size_t) i];
        e.name = ch.getProperty ("name", e.name).toString();
        e.settings.fromValueTree (ch.getChildWithName ("SETTINGS"));
        e.notchesFromValueTree (ch.getChildWithName ("NOTCHES"));
        ab[(size_t) i].current = (int) ch.getProperty ("abSlot", 0) == 1 ? 1 : 0;
        const auto other = ch.getChildWithName ("ABOTHER");
        ab[(size_t) i].other = other.getNumChildren() > 0 ? other.getChild (0).createCopy() : juce::ValueTree();
    }
}

//==============================================================================
juce::ValueTree HowlGuardProcessor::createVenue (int channel)
{
    auto& e = getEngine (channel);
    juce::ValueTree v ("HOWLVENUE");
    v.setProperty ("version", 1, nullptr);
    v.setProperty ("channelName", e.name, nullptr);
    v.appendChild (e.settings.toValueTree(), nullptr);
    v.appendChild (e.notchesToValueTree (true), nullptr);
    return v;
}

void HowlGuardProcessor::applyVenue (int channel, const juce::ValueTree& v)
{
    auto& e = getEngine (channel);
    e.settings.fromValueTree (v.getChildWithName ("SETTINGS"));
    e.notchesFromValueTree (v.getChildWithName ("NOTCHES"));
    markDirty();
}

void HowlGuardProcessor::toggleAB (int channel)
{
    auto& e = getEngine (channel);
    auto& s = ab[(size_t) juce::jlimit (0, maxChannels - 1, channel)];
    auto current = e.settings.toValueTree();
    if (s.other.isValid())
        e.settings.fromValueTree (s.other);
    s.other = current;
    s.current = 1 - s.current;
    markDirty();
}

void HowlGuardProcessor::copyToOtherSlot (int channel)
{
    auto& s = ab[(size_t) juce::jlimit (0, maxChannels - 1, channel)];
    s.other = getEngine (channel).settings.toValueTree();
    markDirty();
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new HowlGuardProcessor();
}
