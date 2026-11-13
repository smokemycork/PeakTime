#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "dsp/IRGenerator.h"
#include "dsp/VenueEngine.h"
#include "TrackPlayer.h"

namespace ParamID
{
    inline constexpr const char* venue      = "venue";
    inline constexpr const char* distance   = "distance";
    inline constexpr const char* crowdLevel = "crowdLevel";
    inline constexpr const char* crowdFill  = "crowdFill";
    inline constexpr const char* system     = "system";
    inline constexpr const char* inVenue    = "inVenue";
    inline constexpr const char* headphones = "headphones";
    inline constexpr const char* levelMatch = "levelMatch";
    inline constexpr const char* output     = "output";
    inline constexpr const char* source     = "source";
}

// lock-free hand-off of the original / in-venue signals to the spectrum display
struct AnalyserFifo
{
    static constexpr int size = 1 << 15;
    juce::AbstractFifo fifo { size };
    std::vector<float> dry = std::vector<float> (size), wet = std::vector<float> (size);

    void push (const float* d, const float* w, int n) noexcept
    {
        int s1, z1, s2, z2;
        fifo.prepareToWrite (n, s1, z1, s2, z2);
        if (z1 > 0) { std::copy (d, d + z1, dry.begin() + s1); std::copy (w, w + z1, wet.begin() + s1); }
        if (z2 > 0) { std::copy (d + z1, d + z1 + z2, dry.begin() + s2); std::copy (w + z1, w + z1 + z2, wet.begin() + s2); }
        fifo.finishedWrite (z1 + z2);
    }

    int pull (float* d, float* w, int maxN) noexcept
    {
        int s1, z1, s2, z2;
        fifo.prepareToRead (maxN, s1, z1, s2, z2);
        if (z1 > 0) { std::copy (dry.begin() + s1, dry.begin() + s1 + z1, d); std::copy (wet.begin() + s1, wet.begin() + s1 + z1, w); }
        if (z2 > 0) { std::copy (dry.begin() + s2, dry.begin() + s2 + z2, d + z1); std::copy (wet.begin() + s2, wet.begin() + s2 + z2, w + z1); }
        fifo.finishedRead (z1 + z2);
        return z1 + z2;
    }
};

class PeakTimeProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
    PeakTimeProcessor();
    ~PeakTimeProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 4.5; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    // ---- used by the editor
    juce::AudioProcessorValueTreeState apvts;
    TrackPlayer player;
    AnalyserFifo analyser;

    void applyVenueDefaults (int venueIndex); // message thread: selects venue + its stock setup
    void setParam (const char* id, float denormalisedValue);
    int getVenueIndex() const;
    float getDistance() const;

    std::atomic<float> diagReverbToDirect { 0.f }, diagCriticalDistance { 0.f }, diagRtMid { 0.f };
    std::atomic<float> diagSubGr { 0.f }, diagTopGr { 0.f }, diagAlignMs { 0.f }, diagMatchDb { 0.f };
    double getSampleRateSafe() const { return currentSampleRate > 0 ? currentSampleRate : 48000.0; }

private:
    class IRWorker : public juce::Thread
    {
    public:
        explicit IRWorker (PeakTimeProcessor& p) : juce::Thread ("PeakTime IR builder"), owner (p) {}
        void request (const pt::IRSettings& s)
        {
            { const juce::ScopedLock sl (lock); pending = s; hasPending = true; }
            notify();
        }
        void run() override
        {
            while (! threadShouldExit())
            {
                wait (-1);
                if (threadShouldExit()) break;
                pt::IRSettings s;
                {
                    const juce::ScopedLock sl (lock);
                    if (! hasPending) continue;
                    s = pending; hasPending = false;
                }
                owner.loadIR (pt::IRGenerator::generate (s));
            }
        }
    private:
        PeakTimeProcessor& owner;
        juce::CriticalSection lock;
        pt::IRSettings pending;
        bool hasPending = false;
    };

    void timerCallback() override;
    pt::IRSettings currentIRSettings() const;
    void loadIR (pt::IRResult&& r);

    std::atomic<float>* pVenue = nullptr; std::atomic<float>* pDistance = nullptr;
    std::atomic<float>* pCrowdLevel = nullptr; std::atomic<float>* pCrowdFill = nullptr;
    std::atomic<float>* pSystem = nullptr; std::atomic<float>* pInVenue = nullptr;
    std::atomic<float>* pHeadphones = nullptr; std::atomic<float>* pLevelMatch = nullptr;
    std::atomic<float>* pOutput = nullptr; std::atomic<float>* pSource = nullptr;

    pt::RigModel rig;
    pt::PostChain post;
    juce::dsp::Convolution convolution { juce::dsp::Convolution::NonUniform { 1024 } };
    IRWorker irWorker { *this };
    pt::IRSettings lastRequested;
    std::atomic<bool> prepared { false };

    juce::AudioBuffer<float> dryBuffer;
    std::vector<float> monoDry, monoWet;
    pt::KWeighting kwDry[2], kwWet[2];
    double currentSampleRate = 0.0;
    float msDry = 0.f, msWet = 0.f, systemMs = 0.f;
    float matchGain = 1.f, abMix = 1.f, outGain = 1.f;
    int matchVenue = -1, matchSystem = -1, matchBlocks = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PeakTimeProcessor)
};
