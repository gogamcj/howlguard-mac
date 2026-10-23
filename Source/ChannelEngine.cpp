#include "ChannelEngine.h"

namespace howl
{

//==============================================================================
juce::ValueTree Settings::toValueTree() const
{
    juce::ValueTree v ("SETTINGS");
    v.setProperty ("mode", mode.load(), nullptr);
    v.setProperty ("strength", strength.load(), nullptr);
    v.setProperty ("sensitivity", sensitivity.load(), nullptr);
    v.setProperty ("releaseSec", releaseSec.load(), nullptr);
    v.setProperty ("width", width.load(), nullptr);
    v.setProperty ("bypass", bypass.load(), nullptr);
    v.setProperty ("compOn", compOn.load(), nullptr);
    v.setProperty ("compThreshold", compThreshold.load(), nullptr);
    v.setProperty ("compRatio", compRatio.load(), nullptr);
    v.setProperty ("compAttack", compAttack.load(), nullptr);
    v.setProperty ("compRelease", compRelease.load(), nullptr);
    v.setProperty ("compMakeup", compMakeup.load(), nullptr);
    return v;
}

void Settings::fromValueTree (const juce::ValueTree& v)
{
    if (! v.isValid())
        return;
    mode          = juce::jlimit (0, 3, (int) v.getProperty ("mode", mode.load()));
    strength      = (float) v.getProperty ("strength", strength.load());
    sensitivity   = (float) v.getProperty ("sensitivity", sensitivity.load());
    releaseSec    = (float) v.getProperty ("releaseSec", releaseSec.load());
    width         = juce::jlimit (0, 2, (int) v.getProperty ("width", width.load()));
    bypass        = (bool) v.getProperty ("bypass", bypass.load());
    compOn        = (bool) v.getProperty ("compOn", compOn.load());
    compThreshold = (float) v.getProperty ("compThreshold", compThreshold.load());
    compRatio     = (float) v.getProperty ("compRatio", compRatio.load());
    compAttack    = (float) v.getProperty ("compAttack", compAttack.load());
    compRelease   = (float) v.getProperty ("compRelease", compRelease.load());
    compMakeup    = (float) v.getProperty ("compMakeup", compMakeup.load());
}

void Settings::resetToDefaults()
{
    Settings d;
    fromValueTree (d.toValueTree());
    ringOut = false;
}

//==============================================================================
ChannelEngine::ChannelEngine()
{
    for (auto& d : display.inputDb)  d = -120.0f;
    for (auto& d : display.outputDb) d = -120.0f;
}

void ChannelEngine::prepare (double sr, int maxBlockSize)
{
    sampleRate = sr;
    maxBlock = std::max (32, maxBlockSize);

    const int fifoSize = (int) sr + maxBlock * 4;
    inFifo.setTotalSize (fifoSize);
    outFifo.setTotalSize (fifoSize);
    inFifo.reset();
    outFifo.reset();
    inFifoBuf.assign ((size_t) fifoSize, 0.0f);
    outFifoBuf.assign ((size_t) fifoSize, 0.0f);
    monoScratch.assign ((size_t) maxBlock, 0.0f);

    detector.prepare (sr);
    const int n = detector.getFftSize();
    inRing.assign ((size_t) n, 0.0f);
    outRing.assign ((size_t) n, 0.0f);
    frame.assign ((size_t) n, 0.0f);
    ringPos = outRingPos = pendingSamples = 0;
    analysedSamples = 0;

    compressor.prepare (sr);
    for (auto& s : audioSlots)
    {
        s.gainDb = 0.0f;
        s.inUse = false;
        for (auto& st : s.state)
            st.reset();
    }

    const juce::ScopedLock sl (bankLock);
    // timestamps restart from zero with the new stream
    for (auto& nt : notches)
        nt.lastHowl = 0.0;
    memory.clear();
    lastTick = 0.0;
    publishLocked();
}

//==============================================================================
void ChannelEngine::process (float* const* chans, int numChans, int numSamples) noexcept
{
    numChans = std::min (numChans, 16);
    if (numChans <= 0 || numSamples <= 0)
        return;

    // --- input meter + feed detector with the mono sum
    float inPeak = 0.0f;
    for (int start = 0; start < numSamples; start += maxBlock)
    {
        const int n = std::min (maxBlock, numSamples - start);
        const float scale = 1.0f / (float) numChans;
        for (int i = 0; i < n; ++i)
        {
            float sum = 0.0f;
            for (int c = 0; c < numChans; ++c)
            {
                const float x = chans[c][start + i];
                inPeak = std::max (inPeak, std::abs (x));
                sum += x;
            }
            monoScratch[(size_t) i] = sum * scale;
        }

        const auto w = inFifo.write (n);
        std::copy (monoScratch.begin(), monoScratch.begin() + w.blockSize1, inFifoBuf.begin() + w.startIndex1);
        std::copy (monoScratch.begin() + w.blockSize1, monoScratch.begin() + w.blockSize1 + w.blockSize2, inFifoBuf.begin() + w.startIndex2);
    }
    inPeakDb = std::max (inPeakDb.load(), juce::Decibels::gainToDecibels (inPeak, -100.0f));

    if (resetFilters.exchange (false))
        for (auto& s : audioSlots)
            for (auto& st : s.state)
                st.reset();

    // --- follow the published notch bank with smoothed gains
    const float maxStep = 200.0f * (float) numSamples / (float) sampleRate; // dB per block
    for (int i = 0; i < maxNotches; ++i)
    {
        auto& p = published[(size_t) i];
        auto& a = audioSlots[(size_t) i];

        const float tFreq = p.freq.load (std::memory_order_relaxed);
        const float tQ    = p.q.load (std::memory_order_relaxed);
        float tGain = p.active.load (std::memory_order_relaxed) ? p.gainDb.load (std::memory_order_relaxed) : 0.0f;

        bool dirty = false;
        if (std::abs (tFreq - a.freq) > 0.01f || std::abs (tQ - a.q) > 1.0e-3f)
        {
            if (a.gainDb > -0.05f)
            {
                a.freq = tFreq;
                a.q = tQ;
                dirty = true;
            }
            else
            {
                tGain = 0.0f; // fade the old notch out before moving it
            }
        }

        if (std::abs (a.gainDb - tGain) > 1.0e-6f)
        {
            a.gainDb = tGain > a.gainDb ? std::min (tGain, a.gainDb + maxStep)
                                        : std::max (tGain, a.gainDb - maxStep);
            dirty = true;
        }

        const bool use = a.gainDb < -0.01f;
        if (! use && a.inUse)
            for (auto& st : a.state)
                st.reset();
        a.inUse = use;

        if (dirty && use)
            a.coeffs = BiquadCoeffs::peaking (sampleRate, juce::jlimit (20.0, sampleRate * 0.49, (double) a.freq), a.q, a.gainDb);
    }

    if (! settings.bypass.load())
    {
        for (auto& a : audioSlots)
        {
            if (! a.inUse)
                continue;
            for (int c = 0; c < numChans; ++c)
            {
                auto& st = a.state[(size_t) c];
                float* d = chans[c];
                for (int i = 0; i < numSamples; ++i)
                    d[i] = st.process (a.coeffs, d[i]);
            }
        }

        if (settings.compOn.load())
        {
            compressor.setParams (settings.compThreshold.load(), settings.compRatio.load(),
                                  settings.compAttack.load(), settings.compRelease.load(),
                                  settings.compMakeup.load());
            grDb = compressor.process (chans, numChans, numSamples);
        }
        else
        {
            compressor.reset();
            grDb = 0.0f;
        }
    }
    else
    {
        grDb = 0.0f;
    }

    // --- output meter + output spectrum feed
    float outPeak = 0.0f;
    for (int start = 0; start < numSamples; start += maxBlock)
    {
        const int n = std::min (maxBlock, numSamples - start);
        const float scale = 1.0f / (float) numChans;
        for (int i = 0; i < n; ++i)
        {
            float sum = 0.0f;
            for (int c = 0; c < numChans; ++c)
            {
                const float x = chans[c][start + i];
                outPeak = std::max (outPeak, std::abs (x));
                sum += x;
            }
            monoScratch[(size_t) i] = sum * scale;
        }

        const auto w = outFifo.write (n);
        std::copy (monoScratch.begin(), monoScratch.begin() + w.blockSize1, outFifoBuf.begin() + w.startIndex1);
        std::copy (monoScratch.begin() + w.blockSize1, monoScratch.begin() + w.blockSize1 + w.blockSize2, outFifoBuf.begin() + w.startIndex2);
    }
    outPeakDb = std::max (outPeakDb.load(), juce::Decibels::gainToDecibels (outPeak, -100.0f));
}

//==============================================================================
static void drainFifo (juce::AbstractFifo& fifo, const std::vector<float>& buf,
                       std::vector<float>& ring, int& pos, int* counter)
{
    const auto r = fifo.read (fifo.getNumReady());
    const int ringSize = (int) ring.size();
    auto copyBlock = [&] (int start, int size)
    {
        for (int i = 0; i < size; ++i)
        {
            ring[(size_t) pos] = buf[(size_t) (start + i)];
            pos = (pos + 1) % ringSize;
        }
    };
    copyBlock (r.startIndex1, r.blockSize1);
    copyBlock (r.startIndex2, r.blockSize2);
    if (counter != nullptr)
        *counter += r.blockSize1 + r.blockSize2;
}

static void mapToDisplay (const std::vector<float>& binsDb, double binHz,
                          std::array<float, numDisplayPoints>& outDb)
{
    const int numBins = (int) binsDb.size();
    for (int i = 0; i < numDisplayPoints; ++i)
    {
        const float f0 = displayPointToHz (i) / std::pow (displayMaxHz / displayMinHz, 0.5f / numDisplayPoints);
        const float f1 = f0 * std::pow (displayMaxHz / displayMinHz, 1.0f / numDisplayPoints);
        const int k0 = std::max (1, (int) std::floor (f0 / binHz));
        const int k1 = std::min (numBins - 1, std::max (k0, (int) std::ceil (f1 / binHz)));
        float v = -140.0f;
        for (int k = k0; k <= k1; ++k)
            v = std::max (v, binsDb[(size_t) k]);
        outDb[(size_t) i] = 0.55f * outDb[(size_t) i] + 0.45f * v;
    }
}

void ChannelEngine::runAnalysis (bool wantOutputSpectrum)
{
    const int n = detector.getFftSize();
    const int hop = detector.getHopSize();
    if ((int) inRing.size() != n)
        return;

    drainFifo (inFifo, inFifoBuf, inRing, ringPos, &pendingSamples);
    drainFifo (outFifo, outFifoBuf, outRing, outRingPos, nullptr);

    if (pendingSamples < hop)
        return;

    // if we fell behind, only analyse the newest frame
    const int hops = pendingSamples / hop;
    pendingSamples -= hops * hop;
    analysedSamples += (long long) hops * hop;
    const double now = (double) analysedSamples / sampleRate;

    // unroll the ring (ringPos is the oldest sample)
    for (int i = 0; i < n; ++i)
        frame[(size_t) i] = inRing[(size_t) ((ringPos + i) % n)];

    const auto mode = (Mode) settings.mode.load();
    const float sens = settings.sensitivity.load();

    detections.clear();
    detector.analyse (frame.data(), now, mode, sens,
                      [this, now] (float f) { const juce::ScopedLock sl (bankLock); return isRemembered (f, now); },
                      detections);

    {
        const juce::ScopedLock sl (bankLock);
        for (const auto& d : detections)
            onHowl (d.freq, now);
        tickRelease (now);
        publishLocked();
    }

    detector.getWatching (watchScratch);
    watchingFlag = ! watchScratch.empty();

    if (wantOutputSpectrum)
    {
        for (int i = 0; i < n; ++i)
            frame[(size_t) i] = outRing[(size_t) ((outRingPos + i) % n)];
        detector.computeSpectrumDb (frame.data(), outSpectrum);
    }

    const juce::SpinLock::ScopedLockType sl (displayLock);
    mapToDisplay (detector.getLastSpectrumDb(), detector.getBinHz(), display.inputDb);
    if (wantOutputSpectrum)
        mapToDisplay (outSpectrum, detector.getBinHz(), display.outputDb);
    display.watching = watchScratch;
}

void ChannelEngine::getDisplay (Display& d) const
{
    const juce::SpinLock::ScopedLockType sl (displayLock);
    d = display;
}

//==============================================================================
double ChannelEngine::toleranceOct() const
{
    return std::max (notchWidthOctaves (settings.width.load()) * 0.75, 1.0 / 48.0);
}

void ChannelEngine::onHowl (float freq, double now)
{
    const float s = settings.strength.load();
    const float maxDepth = -(3.0f + 27.0f * s);
    const float step = 2.0f + 6.0f * s;
    const bool ringOut = settings.ringOut.load();
    const double tol = toleranceOct();

    // existing notch close enough?
    Notch* best = nullptr;
    double bestDist = tol;
    for (auto& nt : notches)
    {
        if (! nt.active)
            continue;
        const double d = std::abs (std::log2 ((double) freq / nt.freq));
        if (d < bestDist)
        {
            bestDist = d;
            best = &nt;
        }
    }

    if (best != nullptr)
    {
        if (! best->fixed)
            best->freq = (float) std::exp2 (0.7 * std::log2 ((double) best->freq) + 0.3 * std::log2 ((double) freq));

        if (best->depth > best->deepest + 0.5f)
            best->depth = best->deepest;          // it was releasing: snap back
        else
            best->depth = std::max (maxDepth, best->depth - step);

        best->depth = std::min (best->depth, -step * 0.5f);
        best->deepest = std::min (best->deepest, best->depth);
        best->lastHowl = now;
        if (ringOut)
            best->fixed = true;
        return;
    }

    // new notch: free slot, otherwise replace the shallowest automatic one
    Notch* slot = nullptr;
    for (auto& nt : notches)
        if (! nt.active) { slot = &nt; break; }

    if (slot == nullptr)
    {
        for (auto& nt : notches)
        {
            if (nt.fixed)
                continue;
            if (slot == nullptr || nt.depth > slot->depth
                || (std::abs (nt.depth - slot->depth) < 1.0e-6f && nt.lastHowl < slot->lastHowl))
                slot = &nt;
        }
    }

    if (slot == nullptr)
        return; // all 32 are fixed

    float depth = -step;
    for (auto it = memory.begin(); it != memory.end(); ++it)
    {
        if (std::abs (std::log2 ((double) freq / it->freq)) < tol && now - it->time < 60.0)
        {
            depth = std::max (maxDepth, std::min (depth, it->depth));
            memory.erase (it);
            break;
        }
    }

    slot->active = true;
    slot->fixed = ringOut;
    slot->freq = freq;
    slot->depth = depth;
    slot->deepest = depth;
    slot->lastHowl = now;
}

void ChannelEngine::tickRelease (double now)
{
    const double dt = std::max (0.0, now - lastTick);
    lastTick = now;
    const float rate = 6.0f / std::max (0.5f, settings.releaseSec.load()); // dB per second

    for (auto& nt : notches)
    {
        if (! nt.active || nt.fixed || now - nt.lastHowl < 3.0)
            continue;

        nt.depth += rate * (float) dt;
        if (nt.depth >= -0.3f)
        {
            memory.push_back ({ nt.freq, nt.deepest, now });
            nt.active = false;
            nt.depth = 0.0f;
        }
    }

    memory.erase (std::remove_if (memory.begin(), memory.end(),
                                  [now] (const Memory& m) { return now - m.time > 60.0; }),
                  memory.end());
}

bool ChannelEngine::isRemembered (float freq, double now) const
{
    const double tol = toleranceOct();
    for (const auto& m : memory)
        if (now - m.time < 60.0 && std::abs (std::log2 ((double) freq / m.freq)) < tol)
            return true;
    for (const auto& nt : notches)
        if (nt.active && std::abs (std::log2 ((double) freq / nt.freq)) < tol)
            return true;
    return false;
}

void ChannelEngine::publishLocked()
{
    const float q = (float) bandwidthToQ (notchWidthOctaves (settings.width.load()));
    int active = 0, fixed = 0;
    for (int i = 0; i < maxNotches; ++i)
    {
        const auto& nt = notches[(size_t) i];
        auto& p = published[(size_t) i];
        p.freq.store (nt.freq, std::memory_order_relaxed);
        p.q.store (q, std::memory_order_relaxed);
        p.gainDb.store (nt.depth, std::memory_order_relaxed);
        p.active.store (nt.active, std::memory_order_relaxed);
        if (nt.active)
        {
            ++active;
            if (nt.fixed)
                ++fixed;
        }
    }
    activeCount = active;
    fixedCount = fixed;
}

//==============================================================================
std::vector<NotchInfo> ChannelEngine::getNotches() const
{
    std::vector<NotchInfo> v;
    const juce::ScopedLock sl (bankLock);
    for (int i = 0; i < maxNotches; ++i)
        if (notches[(size_t) i].active)
            v.push_back ({ i, notches[(size_t) i].freq, notches[(size_t) i].depth, notches[(size_t) i].fixed });
    return v;
}

void ChannelEngine::removeNotch (int slot)
{
    const juce::ScopedLock sl (bankLock);
    if (juce::isPositiveAndBelow (slot, maxNotches))
        notches[(size_t) slot] = Notch();
    publishLocked();
}

void ChannelEngine::toggleFixed (int slot)
{
    const juce::ScopedLock sl (bankLock);
    if (juce::isPositiveAndBelow (slot, maxNotches) && notches[(size_t) slot].active)
    {
        auto& nt = notches[(size_t) slot];
        nt.fixed = ! nt.fixed;
        nt.lastHowl = lastTick;
    }
    publishLocked();
}

void ChannelEngine::fixAll()
{
    const juce::ScopedLock sl (bankLock);
    for (auto& nt : notches)
        if (nt.active)
            nt.fixed = true;
    publishLocked();
}

void ChannelEngine::clearNotches (bool includeFixed)
{
    const juce::ScopedLock sl (bankLock);
    for (auto& nt : notches)
        if (includeFixed || ! nt.fixed)
            nt = Notch();
    memory.clear();
    publishLocked();
}

juce::ValueTree ChannelEngine::notchesToValueTree (bool includeAuto) const
{
    juce::ValueTree v ("NOTCHES");
    const juce::ScopedLock sl (bankLock);
    for (const auto& nt : notches)
    {
        if (! nt.active || (! nt.fixed && ! includeAuto))
            continue;
        juce::ValueTree n ("NOTCH");
        n.setProperty ("freq", nt.freq, nullptr);
        n.setProperty ("depth", nt.depth, nullptr);
        n.setProperty ("fixed", nt.fixed, nullptr);
        v.appendChild (n, nullptr);
    }
    return v;
}

void ChannelEngine::notchesFromValueTree (const juce::ValueTree& v)
{
    const juce::ScopedLock sl (bankLock);
    for (auto& nt : notches)
        nt = Notch();
    memory.clear();

    int i = 0;
    for (const auto& n : v)
    {
        if (i >= maxNotches)
            break;
        auto& nt = notches[(size_t) i++];
        nt.active = true;
        nt.freq = juce::jlimit (20.0f, 20000.0f, (float) n.getProperty ("freq", 1000.0f));
        nt.depth = juce::jlimit (-30.0f, -0.5f, (float) n.getProperty ("depth", -6.0f));
        nt.deepest = nt.depth;
        nt.fixed = (bool) n.getProperty ("fixed", true);
        nt.lastHowl = lastTick;
    }
    publishLocked();
}

} // namespace howl
