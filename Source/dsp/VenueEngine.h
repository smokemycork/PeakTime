#pragma once
// PeakTime - real-time stages around the room convolution.
//
//   input -> [RigModel] -> (room convolution, done by the host wrapper) -> [PostChain] -> out
//
// RigModel : the sound system. Mono/stereo width of the install, LR4 sub/top split,
//            sub tuning, horn character, box limits, drive (clean / working / red line)
//            with sub excursion limiting, rig limiter and amp clipping, sub-vs-top
//            time misalignment that depends on where you stand, room gain.
// PostChain: air absorption over distance, HF shadowing from the crowd in front of you,
//            the crowd itself (chatter, stomps, cheers, whistles), headphone crossfeed.

#include "Biquad.h"
#include "Venues.h"

namespace pt
{
// ----------------------------------------------------------------------------- envelope
struct EnvFollower
{
    float att = 0.f, rel = 0.f, env = 0.f;
    void set (double sr, float attackMs, float releaseMs)
    {
        att = (float) std::exp (-1.0 / (sr * attackMs * 0.001));
        rel = (float) std::exp (-1.0 / (sr * releaseMs * 0.001));
    }
    inline float process (float x) noexcept
    {
        const float a = x > env ? att : rel;
        env = a * env + (1.f - a) * x;
        return env;
    }
};

// ----------------------------------------------------------------------------- rig
class RigModel
{
public:
    void prepare (double sampleRate, int maxBlock);
    void reset();
    void setVenue (int venueIndex);
    void setDistance (float metres);   // smoothed internally
    void setDrive (float drive);       // 0 clean, 1 working, 2 red line (continuous)
    void process (float* L, float* R, int n) noexcept;

    // diagnostics for the UI
    float getSubGainReductionDb() const { return subGrDb; }
    float getTopGainReductionDb() const { return topGrDb; }
    float getAlignmentErrorMs() const { return alignErrMs; }

private:
    void updateStaticFilters();
    void updateAlignment (float distance);

    double sr = 48000.0;
    int venue = 0;
    float targetDistance = 10.f, curDistance = -1.f;
    float targetDrive = 1.f, curDrive = 1.f;

    LR4 xover[2];
    Biquad subHp1[2], subHp2[2], subPeak[2];
    Biquad presence[2], hfShelf[2], hfLp1[2], hfLp2[2], roomShelf[2], dcTop[2];
    DelayLine subDelay[2], topDelay[2];
    float subDelaySmp = 0.f, topDelaySmp = 0.f;
    EnvFollower subEnv, topEnv;
    float subGrDb = 0.f, topGrDb = 0.f, alignErrMs = 0.f;
    float width = 1.f;
};

// ----------------------------------------------------------------------------- crowd
class CrowdNoise
{
public:
    void prepare (double sampleRate);
    // adds crowd into L/R at an absolute RMS level (linear)
    void process (float* L, float* R, int n, float targetRms, float density) noexcept;

private:
    struct Pink { float b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;
        inline float process (float w) noexcept
        {
            b0 = 0.99886f * b0 + w * 0.0555179f; b1 = 0.99332f * b1 + w * 0.0750759f;
            b2 = 0.96900f * b2 + w * 0.1538520f; b3 = 0.86650f * b3 + w * 0.3104856f;
            b4 = 0.55000f * b4 + w * 0.5329522f; b5 = -0.7616f * b5 - w * 0.0168980f;
            const float out = b0 + b1 + b2 + b3 + b4 + b5 + b6 + w * 0.5362f;
            b6 = w * 0.115926f;
            return out * 0.11f;
        } };

    double sr = 48000.0;
    Rng rng { 0xA5A5A5u };
    Pink pink[2];
    Biquad chatter[2], chatter2[2], rumble[2], cheerBp[2], cheerBp2[2];
    double lfoPhase[3] = { 0.1, 0.4, 0.77 };
    double lfoRate[3] = { 0.07, 0.19, 0.31 };
    // cheer swells
    int samplesToNextCheer = 0;
    float cheerEnv = 0.f, cheerTarget = 0.f;
    int cheerHold = 0;
    // whistles
    int samplesToNextWhistle = 0, whistleLeft = 0, whistleLen = 1;
    double whistlePhase = 0, whistleFreq = 3000, vibPhase = 0;
    float whistlePan = 0.5f;
    float gainSmoothed = 0.f;
};

// ----------------------------------------------------------------------------- crossfeed
class Crossfeed
{
public:
    void prepare (double sampleRate);
    void process (float* L, float* R, int n) noexcept;
private:
    Biquad lpL, lpR, shL, shR;
    DelayLine dL, dR;
    float delaySmp = 14.f;
};

// ----------------------------------------------------------------------------- post chain
class PostChain
{
public:
    void prepare (double sampleRate, int maxBlock);
    void reset();
    void setVenue (int venueIndex) { venue = venueIndex; }
    void setDistance (float m) { targetDistance = m; }
    void setCrowd (float fill01, float crowdDb) { targetFill = fill01; crowdLevelDb = crowdDb; }
    void setHeadphones (bool on) { headphones = on; }

    // systemRms: running level of the system signal (used to place the crowd against it)
    void process (float* L, float* R, int n, float systemRms) noexcept;

private:
    void updateFilters();

    double sr = 48000.0;
    int venue = 0;
    float targetDistance = 10.f, curDistance = -1.f;
    float targetFill = 0.8f, curFill = -1.f;
    float crowdLevelDb = -24.f;
    bool headphones = false;
    Biquad air1[2], air2[2], shadow[2];
    CrowdNoise crowd;
    Crossfeed xfeed;
};

// ----------------------------------------------------------------------------- loudness
// ITU-R BS.1770 style K-weighting. Used offline (integrated, gated LUFS of a loaded
// track) and online (running mean-square for level matching).
struct KWeighting
{
    Biquad s1, s2;
    void prepare (double sr);
    inline float process (float x) noexcept { return s2.process (s1.process (x)); }
};

float integratedLufs (const float* const* channels, int numChannels, int numSamples, double sr);

} // namespace pt
