#include "VenueEngine.h"
#include <cmath>
#include <algorithm>

namespace pt
{
// =============================================================================== RigModel
void RigModel::prepare (double sampleRate, int)
{
    sr = sampleRate;
    for (int c = 0; c < 2; ++c)
    {
        subDelay[c].prepare ((int) (sr * 0.03));
        topDelay[c].prepare ((int) (sr * 0.03));
    }
    subEnv.set (sr, 5.0f, 150.0f);
    topEnv.set (sr, 1.5f, 80.0f);
    curDistance = -1.f;
    updateStaticFilters();
    reset();
}

void RigModel::reset()
{
    for (int c = 0; c < 2; ++c)
    {
        xover[c].reset(); subHp1[c].reset(); subHp2[c].reset(); subPeak[c].reset();
        presence[c].reset(); hfShelf[c].reset(); hfLp1[c].reset(); hfLp2[c].reset();
        roomShelf[c].reset(); dcTop[c].reset(); subDelay[c].reset(); topDelay[c].reset();
    }
    subEnv.env = topEnv.env = 0.f;
}

void RigModel::setVenue (int v)
{
    v = std::clamp (v, 0, numVenues() - 1);
    if (v == venue) return;
    venue = v;
    updateStaticFilters();
    curDistance = -1.f; // force re-alignment
}

void RigModel::setDistance (float m) { targetDistance = m; }
void RigModel::setDrive (float d) { targetDrive = std::clamp (d, 0.f, 2.f); }

void RigModel::updateStaticFilters()
{
    const Venue& v = venues()[(size_t) venue];
    const RigSpec& r = rigSpec (v.rig);
    for (int c = 0; c < 2; ++c)
    {
        xover[c].set (sr, r.xoverHz);
        subHp1[c].highpass (sr, r.lowCutHz, 0.5412);
        subHp2[c].highpass (sr, r.lowCutHz, 1.3066);
        subPeak[c].peak (sr, r.subTuneHz, r.subQ, r.subBumpDb + v.subTrimDb);
        presence[c].peak (sr, r.presenceHz, 0.9, r.presenceDb);
        hfShelf[c].highShelf (sr, 10000.0, r.hfShelfDb);
        hfLp1[c].lowpass (sr, r.hfCutHz, 0.5412);
        hfLp2[c].lowpass (sr, r.hfCutHz, 1.3066);
        dcTop[c].highpass (sr, 15.0, 0.7071);

        if (v.outdoor)
            roomShelf[c].lowShelf (sr, 90.0, -3.5);  // nothing to reinforce the low end
        else
        {
            const double V = (double) v.lengthM * v.widthM * v.heightM;
            const double g = std::clamp (6.0 - 1.3 * std::log (V / 300.0), 0.5, 5.0);
            roomShelf[c].lowShelf (sr, 80.0, g);  // room gain / boundary loading
        }
    }
    width = v.stereoWidth;
}

void RigModel::updateAlignment (float d)
{
    const Venue& v = venues()[(size_t) venue];
    const RigSpec& r = rigSpec (v.rig);
    const double topZ = v.outdoor ? (double) r.topHeightM
                                  : std::clamp ((double) r.topHeightM, 1.5, std::max (1.6, (double) v.heightM - 0.7));
    const double subZ = 1.0;
    auto offset = [&] (double dist)
    {
        const double dt = std::sqrt (dist * dist + (topZ - 1.7) * (topZ - 1.7));
        const double ds = std::sqrt (dist * dist + (1.7 - subZ) * (1.7 - subZ));
        return (dt - ds) / kSpeedOfSound; // how much later the tops arrive than the subs
    };
    const double foh = std::clamp ((double) v.defaultDistanceM * 1.4, 8.0, (double) maxDistanceFor (v));
    const double err = offset (std::max (1.0, (double) d)) - offset (foh); // system is time-aligned at FOH
    alignErrMs = (float) (err * 1000.0);
    if (err >= 0) { topDelaySmp = (float) (err * sr); subDelaySmp = 0.f; }
    else          { subDelaySmp = (float) (-err * sr); topDelaySmp = 0.f; }
}

void RigModel::process (float* L, float* R, int n) noexcept
{
    // per-block parameter smoothing
    if (curDistance < 0.f) curDistance = targetDistance;
    else curDistance += 0.25f * (targetDistance - curDistance);
    const float prevSub = subDelaySmp, prevTop = topDelaySmp;
    updateAlignment (curDistance);
    const float subStart = prevSub, topStart = prevTop;

    const float d0 = curDrive;
    curDrive += 0.2f * (targetDrive - curDrive);
    const float d = curDrive;
    (void) d0;

    const Venue& v = venues()[(size_t) venue];
    const RigSpec& r = rigSpec (v.rig);

    const float preDb   = d <= 1.f ? 4.f * d : 4.f + 5.f * (d - 1.f);  // 0 / +4 / +9 dB into the rig
    const float pre     = dbToGain (preDb);
    const float topThr  = -2.f - 3.5f * d;      // rig limiter
    const float topRat  = 2.f + 3.f * d;
    const float subThr  = -4.f - 4.f * d;       // sub excursion protection
    const float subRat  = 4.f + 4.f * d;
    const float asym    = r.hornDrive * 0.05f * d * d;
    const float clipIn  = 1.f + 0.6f * d;
    const float makeup  = dbToGain (-preDb * 0.8f);

    float maxSubGr = 0.f, maxTopGr = 0.f;
    const float invN = n > 0 ? 1.f / (float) n : 0.f;

    for (int i = 0; i < n; ++i)
    {
        // install width (lots of club systems are close to mono)
        const float m = 0.5f * (L[i] + R[i]);
        const float s = 0.5f * (L[i] - R[i]) * width;
        float in[2] = { (m + s) * pre, (m - s) * pre };

        float lo[2], hi[2];
        for (int c = 0; c < 2; ++c)
        {
            xover[c].process (in[c], lo[c], hi[c]);
            lo[c] = subPeak[c].process (subHp2[c].process (subHp1[c].process (lo[c])));
            hi[c] = hfLp2[c].process (hfLp1[c].process (hfShelf[c].process (presence[c].process (hi[c]))));
        }

        // stereo-linked dynamics
        const float subLevel = subEnv.process (std::max (std::fabs (lo[0]), std::fabs (lo[1])));
        const float topLevel = topEnv.process (std::max (std::fabs (hi[0]), std::fabs (hi[1])));
        const float subOver = gainToDb (subLevel) - subThr;
        const float topOver = gainToDb (topLevel) - topThr;
        const float subGr = subOver > 0.f ? subOver * (1.f - 1.f / subRat) : 0.f;
        const float topGr = topOver > 0.f ? topOver * (1.f - 1.f / topRat) : 0.f;
        maxSubGr = std::max (maxSubGr, subGr);
        maxTopGr = std::max (maxTopGr, topGr);
        const float subG = dbToGain (-subGr), topG = dbToGain (-topGr);

        const float frac = (float) i * invN;
        const float sd = subStart + (subDelaySmp - subStart) * frac;
        const float td = topStart + (topDelaySmp - topStart) * frac;

        float out[2];
        for (int c = 0; c < 2; ++c)
        {
            float sl = softClip (lo[c] * subG * clipIn) / clipIn;
            float th = hi[c] * topG;
            th = dcTop[c].process (th + asym * th * th);   // horn / driver 2nd-harmonic when pushed
            th = softClip (th * clipIn) / clipIn;          // amp clipping
            sl = subDelay[c].process (sl, sd);
            th = topDelay[c].process (th, td);
            out[c] = roomShelf[c].process (sl + th) * makeup;
        }
        L[i] = out[0];
        R[i] = out[1];
    }

    subGrDb = maxSubGr;
    topGrDb = maxTopGr;
}

// =============================================================================== CrowdNoise
namespace { constexpr float kCrowdNorm = 4.9f; } // raw crowd generator -> ~unit RMS (measured)

void CrowdNoise::prepare (double sampleRate)
{
    sr = sampleRate;
    for (int c = 0; c < 2; ++c)
    {
        chatter[c].bandpass (sr, 900.0, 0.7);
        chatter2[c].bandpass (sr, 2600.0, 1.2);
        rumble[c].lowpass (sr, 140.0, 0.7);
        cheerBp[c].bandpass (sr, 2200.0, 1.0);
        cheerBp2[c].bandpass (sr, 3600.0, 1.4);
    }
    samplesToNextCheer = (int) (sr * (3.0 + 6.0 * rng.uni()));
    samplesToNextWhistle = (int) (sr * (2.0 + 6.0 * rng.uni()));
}

void CrowdNoise::process (float* L, float* R, int n, float targetRms, float density) noexcept
{
    const float target = targetRms * std::sqrt (std::clamp (density, 0.f, 1.f));
    const float gSm = 1.f - (float) std::exp (-1.0 / (sr * 0.05));
    const float cheerAtt = 1.f - (float) std::exp (-1.0 / (sr * 0.6));
    const float cheerRel = 1.f - (float) std::exp (-1.0 / (sr * 2.0));
    double lfoInc[3];
    for (int k = 0; k < 3; ++k) lfoInc[k] = 2 * kPi * lfoRate[k] / sr;

    for (int i = 0; i < n; ++i)
    {
        // slow movement of the crowd's overall level
        double lfo = 0;
        for (int k = 0; k < 3; ++k) { lfoPhase[k] += lfoInc[k]; if (lfoPhase[k] > 2 * kPi) lfoPhase[k] -= 2 * kPi; lfo += std::sin (lfoPhase[k]); }
        const float mod = 0.78f + 0.22f * (float) (lfo / 3.0);

        // cheers
        if (--samplesToNextCheer <= 0)
        {
            cheerTarget = 0.6f + 0.7f * rng.uni();
            cheerHold = (int) (sr * (0.8 + 1.2 * rng.uni()));
            samplesToNextCheer = (int) (sr * (7.0 + 12.0 * rng.uni()));
        }
        if (cheerHold > 0 && --cheerHold == 0) cheerTarget = 0.f;
        cheerEnv += (cheerTarget > cheerEnv ? cheerAtt : cheerRel) * (cheerTarget - cheerEnv);

        // whistles
        float whistle = 0.f;
        if (whistleLeft > 0)
        {
            const float u = 1.f - (float) whistleLeft / (float) whistleLen;
            const float env = std::sin ((float) kPi * u);
            vibPhase += 2 * kPi * 6.0 / sr;
            whistlePhase += 2 * kPi * (whistleFreq + 45.0 * std::sin (vibPhase)) / sr;
            if (whistlePhase > 2 * kPi) whistlePhase -= 2 * kPi;
            whistle = 0.22f * env * env * (float) std::sin (whistlePhase);
            --whistleLeft;
        }
        else if (--samplesToNextWhistle <= 0)
        {
            whistleLen = whistleLeft = (int) (sr * (0.3 + 0.6 * rng.uni()));
            whistleFreq = 2600.0 + 1000.0 * rng.uni();
            whistlePan = rng.uni();
            samplesToNextWhistle = (int) (sr * (4.0 + 11.0 * rng.uni()));
        }

        gainSmoothed += gSm * (target - gainSmoothed);

        float out[2];
        for (int c = 0; c < 2; ++c)
        {
            const float p = pink[c].process (rng.gauss());
            float x = chatter[c].process (p) * 1.0f + chatter2[c].process (p) * 0.4f + rumble[c].process (p) * 0.6f;
            x *= mod;
            x += cheerEnv * (cheerBp[c].process (p) * 1.6f + cheerBp2[c].process (p) * 0.8f);
            x += whistle * (c == 0 ? (1.f - whistlePan) : whistlePan);
            out[c] = x * kCrowdNorm * gainSmoothed;
        }
        L[i] += out[0];
        R[i] += out[1];
    }
}

// =============================================================================== Crossfeed
void Crossfeed::prepare (double sampleRate)
{
    lpL.lowpass (sampleRate, 700.0, 0.6); lpR.lowpass (sampleRate, 700.0, 0.6);
    shL.highShelf (sampleRate, 3000.0, -1.0); shR.highShelf (sampleRate, 3000.0, -1.0);
    dL.prepare ((int) (sampleRate * 0.002)); dR.prepare ((int) (sampleRate * 0.002));
    delaySmp = (float) (sampleRate * 0.0003);
}

void Crossfeed::process (float* L, float* R, int n) noexcept
{
    constexpr float g = 0.45f;               // ~ -7 dB
    constexpr float norm = 1.f / (1.f + g * 0.5f);
    for (int i = 0; i < n; ++i)
    {
        const float l = L[i], r = R[i];
        const float xl = lpL.process (dR.process (r, delaySmp)); // right speaker reaching left ear
        const float xr = lpR.process (dL.process (l, delaySmp));
        L[i] = (shL.process (l) + g * xl) * norm;
        R[i] = (shR.process (r) + g * xr) * norm;
    }
}

// =============================================================================== PostChain
void PostChain::prepare (double sampleRate, int)
{
    sr = sampleRate;
    crowd.prepare (sr);
    xfeed.prepare (sr);
    curDistance = curFill = -1.f;
    updateFilters();
    reset();
}

void PostChain::reset()
{
    for (int c = 0; c < 2; ++c) { air1[c].reset(); air2[c].reset(); shadow[c].reset(); }
}

void PostChain::updateFilters()
{
    const Venue& v = venues()[(size_t) std::clamp (venue, 0, numVenues() - 1)];
    const float d = std::max (0.f, curDistance);
    const float fill = std::max (0.f, curFill);
    const float a1 = -std::min (15.f, 0.04f * d);   // air absorption, ~20C / 50% RH
    const float a2 = -std::min (20.f, 0.10f * d);
    const float sh = -std::min (6.f, fill * d * 0.035f * (v.outdoor ? 1.0f : 0.7f)); // heads in the way
    for (int c = 0; c < 2; ++c)
    {
        air1[c].highShelf (sr, 4500.0, a1);
        air2[c].highShelf (sr, 11000.0, a2);
        shadow[c].highShelf (sr, 2500.0, sh);
    }
}

void PostChain::process (float* L, float* R, int n, float systemRms) noexcept
{
    const float prevD = curDistance, prevF = curFill;
    curDistance = curDistance < 0.f ? targetDistance : curDistance + 0.25f * (targetDistance - curDistance);
    curFill = curFill < 0.f ? targetFill : curFill + 0.25f * (targetFill - curFill);
    if (std::fabs (curDistance - prevD) > 0.01f || std::fabs (curFill - prevF) > 0.002f)
        updateFilters();

    for (int i = 0; i < n; ++i)
    {
        L[i] = shadow[0].process (air2[0].process (air1[0].process (L[i])));
        R[i] = shadow[1].process (air2[1].process (air1[1].process (R[i])));
    }

    // crowd sits against the system level; never fully disappears in breakdowns
    const float crowdRms = std::max (systemRms, 0.03f) * dbToGain (crowdLevelDb);
    if (crowdLevelDb > -59.f && curFill > 0.001f)
        crowd.process (L, R, n, crowdRms, curFill);

    if (headphones)
        xfeed.process (L, R, n);
}

// =============================================================================== Loudness
void KWeighting::prepare (double sr)
{
    {   // stage 1: head / shoulder high shelf
        const double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
        const double K = std::tan (kPi * f0 / sr);
        const double Vh = std::pow (10.0, G / 20.0), Vb = std::pow (Vh, 0.4996667741545416);
        const double a0 = 1.0 + K / Q + K * K;
        s1.set ((Vh + Vb * K / Q + K * K), 2.0 * (K * K - Vh), (Vh - Vb * K / Q + K * K),
                a0, 2.0 * (K * K - 1.0), (1.0 - K / Q + K * K));
    }
    {   // stage 2: RLB high-pass
        const double f0 = 38.13547087602444, Q = 0.5003270373238773;
        const double K = std::tan (kPi * f0 / sr);
        const double a0 = 1.0 + K / Q + K * K;
        s2.set (1.0, -2.0, 1.0, a0, 2.0 * (K * K - 1.0), (1.0 - K / Q + K * K));
        // normalise numerator so passband gain is 1 (matches reference implementation)
        s2.b0 = 1.0; s2.b1 = -2.0; s2.b2 = 1.0;
        s2.a1 = 2.0 * (K * K - 1.0) / a0; s2.a2 = (1.0 - K / Q + K * K) / a0;
    }
    s1.reset(); s2.reset();
}

float integratedLufs (const float* const* ch, int numCh, int numSamples, double sr)
{
    if (numCh <= 0 || numSamples <= 0) return -70.f;
    const int hop = std::max (1, (int) (sr * 0.1));
    const int numHops = numSamples / hop;
    std::vector<double> hopPower ((size_t) std::max (1, numHops), 0.0);
    double totalPower = 0.0;

    for (int c = 0; c < std::min (numCh, 2); ++c)
    {
        KWeighting kw; kw.prepare (sr);
        for (int i = 0; i < numSamples; ++i)
        {
            const double y = kw.process (ch[c][i]);
            const double p = y * y;
            totalPower += p;
            const int h = i / hop;
            if (h < numHops) hopPower[(size_t) h] += p;
        }
    }

    auto toLufs = [] (double ms) { return ms > 0 ? -0.691 + 10.0 * std::log10 (ms) : -120.0; };
    if (numHops < 4) return (float) toLufs (totalPower / numSamples);

    std::vector<double> blocks;
    for (int b = 0; b + 3 < numHops; ++b)
        blocks.push_back ((hopPower[(size_t) b] + hopPower[(size_t) b + 1] + hopPower[(size_t) b + 2] + hopPower[(size_t) b + 3]) / (4.0 * hop));

    double sum = 0; int cnt = 0;
    for (double z : blocks) if (toLufs (z) > -70.0) { sum += z; ++cnt; }
    if (cnt == 0) return -70.f;
    const double relGate = toLufs (sum / cnt) - 10.0;
    sum = 0; cnt = 0;
    for (double z : blocks) if (toLufs (z) > -70.0 && toLufs (z) > relGate) { sum += z; ++cnt; }
    return cnt > 0 ? (float) toLufs (sum / cnt) : -70.f;
}

} // namespace pt
