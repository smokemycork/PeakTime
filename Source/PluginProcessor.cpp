#include "PluginProcessor.h"
#include "PluginEditor.h"

juce::AudioProcessorValueTreeState::ParameterLayout PeakTimeProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    StringArray venueNames;
    for (const auto& v : pt::venues())
        venueNames.add (String (v.name) + " (" + v.city + ")");
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ParamID::venue, 1 }, "Venue", venueNames, 1));

    NormalisableRange<float> distRange (1.0f, 150.0f, 0.1f);
    distRange.setSkewForCentre (20.0f);
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamID::distance, 1 }, "Distance", distRange, 8.0f,
        AudioParameterFloatAttributes().withLabel ("m")
            .withStringFromValueFunction ([] (float v, int) { return (v < 10.0f ? String (v, 1) : String (roundToInt (v))) + " m"; })));

    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamID::crowdLevel, 1 }, "Crowd level",
        NormalisableRange<float> (-60.0f, 0.0f, 0.1f), -26.0f,
        AudioParameterFloatAttributes().withLabel ("dB")
            .withStringFromValueFunction ([] (float v, int) { return v <= -59.9f ? String ("Off") : String (v, 1) + " dB"; })));

    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamID::crowdFill, 1 }, "Crowd fill",
        NormalisableRange<float> (0.0f, 100.0f, 1.0f), 80.0f,
        AudioParameterFloatAttributes().withLabel ("%")
            .withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v)) + " %"; })));

    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ParamID::system, 1 }, "System",
        StringArray { "Clean", "Working", "Red line" }, 1));

    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamID::inVenue, 1 }, "In venue", true));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamID::headphones, 1 }, "Headphones", false));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamID::levelMatch, 1 }, "Level match", true));

    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamID::output, 1 }, "Output",
        NormalisableRange<float> (-24.0f, 12.0f, 0.1f), 0.0f,
        AudioParameterFloatAttributes().withLabel ("dB")
            .withStringFromValueFunction ([] (float v, int) { return String (v, 1) + " dB"; })));

    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ParamID::source, 1 }, "Source",
        StringArray { "DAW input", "My track", "Reference" }, 0));

    return layout;
}

PeakTimeProcessor::PeakTimeProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PEAKTIME", createLayout())
{
    pVenue      = apvts.getRawParameterValue (ParamID::venue);
    pDistance   = apvts.getRawParameterValue (ParamID::distance);
    pCrowdLevel = apvts.getRawParameterValue (ParamID::crowdLevel);
    pCrowdFill  = apvts.getRawParameterValue (ParamID::crowdFill);
    pSystem     = apvts.getRawParameterValue (ParamID::system);
    pInVenue    = apvts.getRawParameterValue (ParamID::inVenue);
    pHeadphones = apvts.getRawParameterValue (ParamID::headphones);
    pLevelMatch = apvts.getRawParameterValue (ParamID::levelMatch);
    pOutput     = apvts.getRawParameterValue (ParamID::output);
    pSource     = apvts.getRawParameterValue (ParamID::source);

    irWorker.startThread();
    startTimerHz (20);
}

PeakTimeProcessor::~PeakTimeProcessor()
{
    stopTimer();
    irWorker.signalThreadShouldExit();
    irWorker.notify();
    irWorker.stopThread (4000);
}

bool PeakTimeProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;
    const auto in = layouts.getMainInputChannelSet();
    return in == juce::AudioChannelSet::stereo() || in == juce::AudioChannelSet::mono() || in.isDisabled();
}

int PeakTimeProcessor::getVenueIndex() const
{
    return juce::jlimit (0, pt::numVenues() - 1, (int) std::lround (pVenue->load()));
}

float PeakTimeProcessor::getDistance() const
{
    const auto& v = pt::venues()[(size_t) getVenueIndex()];
    return juce::jlimit (1.0f, pt::maxDistanceFor (v), pDistance->load());
}

pt::IRSettings PeakTimeProcessor::currentIRSettings() const
{
    pt::IRSettings s;
    s.venueIndex = getVenueIndex();
    s.distanceM = std::round (getDistance() * 4.0f) / 4.0f;           // 25 cm steps
    s.crowdFill = std::round (pCrowdFill->load() * 0.5f) / 50.0f;     // 2 % steps
    s.sampleRate = getSampleRateSafe();
    return s;
}

void PeakTimeProcessor::loadIR (pt::IRResult&& r)
{
    const int n = (int) r.left.size();
    if (n <= 0) return;
    juce::AudioBuffer<float> buf (2, n);
    std::copy (r.left.begin(), r.left.end(), buf.getWritePointer (0));
    std::copy (r.right.begin(), r.right.end(), buf.getWritePointer (1));
    convolution.loadImpulseResponse (std::move (buf), r.sampleRate,
                                     juce::dsp::Convolution::Stereo::yes,
                                     juce::dsp::Convolution::Trim::no,
                                     juce::dsp::Convolution::Normalise::no);
    diagReverbToDirect.store (r.reverbToDirect);
    diagCriticalDistance.store (r.criticalDistanceM);
    diagRtMid.store (r.effectiveRtMid);
}

void PeakTimeProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    prepared.store (false);
    currentSampleRate = sampleRate;
    const int block = juce::jmax (1, samplesPerBlock);

    convolution.prepare ({ sampleRate, (juce::uint32) block, 2 });
    convolution.reset();
    rig.prepare (sampleRate, block);
    post.prepare (sampleRate, block);
    for (int c = 0; c < 2; ++c) { kwDry[c].prepare (sampleRate); kwWet[c].prepare (sampleRate); }

    dryBuffer.setSize (2, block, false, false, true);
    monoDry.assign ((size_t) block, 0.0f);
    monoWet.assign ((size_t) block, 0.0f);
    msDry = msWet = systemMs = 0.0f;
    matchGain = 1.0f;
    abMix = pInVenue->load() > 0.5f ? 1.0f : 0.0f;
    outGain = juce::Decibels::decibelsToGain (pOutput->load());

    // build the first room synchronously so playback starts in the right venue
    lastRequested = currentIRSettings();
    loadIR (pt::IRGenerator::generate (lastRequested));
    prepared.store (true);
}

void PeakTimeProcessor::timerCallback()
{
    player.collectGarbage();
    if (! prepared.load()) return;
    const auto s = currentIRSettings();
    if (s != lastRequested)
    {
        lastRequested = s;
        irWorker.request (s); // the worker always builds the newest request, older ones are dropped
    }
}

void PeakTimeProcessor::applyVenueDefaults (int venueIndex)
{
    venueIndex = juce::jlimit (0, pt::numVenues() - 1, venueIndex);
    const auto& v = pt::venues()[(size_t) venueIndex];
    setParam (ParamID::venue, (float) venueIndex);
    setParam (ParamID::distance, v.defaultDistanceM);
    setParam (ParamID::crowdFill, v.defaultFill * 100.0f);
    setParam (ParamID::crowdLevel, v.defaultCrowdDb);
    setParam (ParamID::system, (float) v.defaultSystem);
}

void PeakTimeProcessor::setParam (const char* id, float value)
{
    if (auto* p = apvts.getParameter (id))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (value));
        p->endChangeGesture();
    }
}

void PeakTimeProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    const int numCh = buffer.getNumChannels();
    if (n <= 0 || numCh < 2) return;

    // mono input -> both channels
    if (getTotalNumInputChannels() == 1)
        buffer.copyFrom (1, 0, buffer, 0, 0, n);
    else if (getTotalNumInputChannels() == 0)
        buffer.clear();

    // ---- source: DAW input, or one of the player slots
    const int source = juce::jlimit (0, 2, (int) std::lround (pSource->load()));
    if (source > 0)
        player.render (buffer, n, currentSampleRate, source - 1);

    if (dryBuffer.getNumSamples() < n)
    {
        dryBuffer.setSize (2, n, false, false, true);
        monoDry.resize ((size_t) n);
        monoWet.resize ((size_t) n);
    }
    dryBuffer.copyFrom (0, 0, buffer, 0, 0, n);
    dryBuffer.copyFrom (1, 0, buffer, 1, 0, n);

    // ---- parameters
    const int venue = getVenueIndex();
    const float distance = getDistance();
    const float fill = juce::jlimit (0.0f, 1.0f, pCrowdFill->load() / 100.0f);
    rig.setVenue (venue);
    rig.setDistance (distance);
    rig.setDrive ((float) juce::jlimit (0, 2, (int) std::lround (pSystem->load())));
    post.setVenue (venue);
    post.setDistance (distance);
    post.setCrowd (fill, pCrowdLevel->load());
    post.setHeadphones (pHeadphones->load() > 0.5f);

    float* L = buffer.getWritePointer (0);
    float* R = buffer.getWritePointer (1);

    // ---- the venue: rig -> room -> air, crowd, ears
    rig.process (L, R, n);
    {
        juce::dsp::AudioBlock<float> block (buffer);
        auto stereo = block.getSubsetChannelBlock (0, 2);
        convolution.process (juce::dsp::ProcessContextReplacing<float> (stereo));
    }

    double sysSum = 0.0;
    for (int i = 0; i < n; ++i) sysSum += (double) L[i] * L[i] + (double) R[i] * R[i];
    const float blockCoef3s = 1.0f - std::exp (-(float) n / (float) (currentSampleRate * 3.0));
    systemMs += blockCoef3s * ((float) (sysSum / (2.0 * n)) - systemMs);

    post.process (L, R, n, std::sqrt (systemMs));

    // ---- level match: compare K-weighted loudness of original and in-venue signals
    const float* dL = dryBuffer.getReadPointer (0);
    const float* dR = dryBuffer.getReadPointer (1);
    double kd = 0.0, kw = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const float a = kwDry[0].process (dL[i]), b = kwDry[1].process (dR[i]);
        const float c = kwWet[0].process (L[i]),  d = kwWet[1].process (R[i]);
        kd += (double) a * a + (double) b * b;
        kw += (double) c * c + (double) d * d;
    }
    // re-lock quickly after a venue / system change: fast tracking while the new room
    // crossfades in, then a running mean that settles into a 2 s window
    const int sysIndex = juce::jlimit (0, 2, (int) std::lround (pSystem->load()));
    if (venue != matchVenue || sysIndex != matchSystem)
    {
        matchVenue = venue; matchSystem = sysIndex;
        matchBlocks = -juce::jmax (1, (int) (0.3 * currentSampleRate / n));
    }
    const float blockCoef2s = 1.0f - std::exp (-(float) n / (float) (currentSampleRate * 2.0));
    float coef = blockCoef2s;
    if (matchBlocks <= 0) { coef = 0.25f; ++matchBlocks; if (matchBlocks == 0) matchBlocks = 1; }
    else { coef = juce::jmax (blockCoef2s, 1.0f / (float) matchBlocks); if (matchBlocks < 1000000) ++matchBlocks; }
    msDry += coef * ((float) (kd / (2.0 * n)) - msDry);
    msWet += coef * ((float) (kw / (2.0 * n)) - msWet);

    float targetMatch = matchGain;
    if (pLevelMatch->load() < 0.5f) targetMatch = 1.0f;
    else if (msDry > 1.0e-7f && msWet > 1.0e-7f)               // hold the last value through silence
        targetMatch = juce::jlimit (0.125f, 8.0f, std::sqrt (msDry / msWet));
    diagMatchDb.store (juce::Decibels::gainToDecibels (targetMatch));

    // ---- A/B (original vs in venue), output, analyser
    const float targetMix = pInVenue->load() > 0.5f ? 1.0f : 0.0f;
    const float targetOut = juce::Decibels::decibelsToGain (pOutput->load());
    const float mixStep = 1.0f / (float) (currentSampleRate * 0.03);
    const float gStart = matchGain, oStart = outGain;

    for (int i = 0; i < n; ++i)
    {
        const float t = (float) (i + 1) / (float) n;
        const float g = gStart + (targetMatch - gStart) * t;
        const float o = oStart + (targetOut - oStart) * t;
        if (abMix < targetMix) abMix = juce::jmin (targetMix, abMix + mixStep);
        else if (abMix > targetMix) abMix = juce::jmax (targetMix, abMix - mixStep);

        float wl = L[i] * g, wr = R[i] * g;
        // gentle safety ceiling on the simulated signal only (your original is never touched)
        auto ceiling = [] (float x) { const float a = std::abs (x); return a > 1.0f ? std::copysign (1.0f + 0.25f * std::tanh ((a - 1.0f) * 4.0f), x) : x; };
        wl = ceiling (wl); wr = ceiling (wr);

        monoDry[(size_t) i] = 0.5f * (dL[i] + dR[i]);
        monoWet[(size_t) i] = 0.5f * (wl + wr);

        L[i] = (dL[i] + abMix * (wl - dL[i])) * o;
        R[i] = (dR[i] + abMix * (wr - dR[i])) * o;
    }
    matchGain = targetMatch;
    outGain = targetOut;

    analyser.push (monoDry.data(), monoWet.data(), n);

    for (int c = 2; c < numCh; ++c) buffer.clear (c, 0, n);

    diagSubGr.store (rig.getSubGainReductionDb());
    diagTopGr.store (rig.getTopGainReductionDb());
    diagAlignMs.store (rig.getAlignmentErrorMs());
}

void PeakTimeProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    for (int s = 0; s < TrackPlayer::numSlots; ++s)
    {
        auto t = player.get (s);
        state.setProperty ("slot" + juce::String (s), t != nullptr ? t->path : juce::String(), nullptr);
    }
    state.setProperty ("loop", player.isLooping(), nullptr);
    state.setProperty ("matchRefs", player.getMatchLoudness(), nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void PeakTimeProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType())) return;
    auto state = juce::ValueTree::fromXml (*xml);
    apvts.replaceState (state);

    player.setLooping ((bool) state.getProperty ("loop", true));
    player.setMatchLoudness ((bool) state.getProperty ("matchRefs", true));
    for (int s = 0; s < TrackPlayer::numSlots; ++s)
    {
        const juce::String path = state.getProperty ("slot" + juce::String (s)).toString();
        if (path.isNotEmpty())
        {
            const juce::File f (path);
            if (f.existsAsFile()) player.loadAsync (s, f);
        }
    }
}

juce::AudioProcessorEditor* PeakTimeProcessor::createEditor()
{
    return new PeakTimeEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PeakTimeProcessor();
}
