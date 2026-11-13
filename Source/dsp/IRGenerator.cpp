#include "IRGenerator.h"
#include <cmath>
#include <algorithm>

namespace pt
{
namespace
{
constexpr int kNumBands = 8;
constexpr float kBandHz[kNumBands] = { 63.f, 125.f, 250.f, 500.f, 1000.f, 2000.f, 4000.f, 8000.f };
// how strongly a full crowd shortens the decay in each band (bodies absorb highs, not sub)
constexpr float kCrowdAbsorb[kNumBands] = { 0.02f, 0.04f, 0.08f, 0.18f, 0.30f, 0.38f, 0.42f, 0.42f };

float baseRT (const Venue& v, float f)
{
    if (f <= 125.f) return v.rtLow * (1.0f + 0.05f * std::log2 (125.f / f));
    if (f <= 1000.f)
    {
        const float t = std::log2 (f / 125.f) / 3.0f;
        return v.rtLow + (v.rtMid - v.rtLow) * t;
    }
    const float t = std::min (1.0f, std::log2 (f / 1000.f) / 3.0f);
    return v.rtMid + (v.rtHigh - v.rtMid) * t;
}

float rtFor (const Venue& v, float fill, int band)
{
    const float rt = baseRT (v, kBandHz[band]);
    return std::max (0.08f, rt / (1.0f + std::clamp (fill, 0.0f, 1.0f) * kCrowdAbsorb[band] * 1.6f));
}

inline void addImpulse (std::vector<float>& buf, double timeSec, double sr, float amp)
{
    const double pos = timeSec * sr;
    if (pos < 0) return;
    const size_t i0 = (size_t) pos;
    if (i0 + 1 >= buf.size()) return;
    const float fr = (float) (pos - (double) i0);
    buf[i0] += amp * (1.0f - fr);
    buf[i0 + 1] += amp * fr;
}

double energy (const std::vector<float>& b)
{
    double e = 0;
    for (float x : b) e += (double) x * x;
    return e;
}

struct Vec3 { double x, y, z; };
inline double dist (const Vec3& a, const Vec3& b)
{
    const double dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    return std::sqrt (dx * dx + dy * dy + dz * dz);
}
} // namespace

void IRGenerator::effectiveRT (const Venue& v, float fill, float out[7])
{
    for (int b = 1; b < kNumBands; ++b)
        out[b - 1] = rtFor (v, fill, b);
}

IRResult IRGenerator::generate (const IRSettings& s)
{
    const auto& all = venues();
    const Venue& v = all[(size_t) std::clamp (s.venueIndex, 0, (int) all.size() - 1)];
    const RigSpec& rig = rigSpec (v.rig);
    const double sr = s.sampleRate > 0 ? s.sampleRate : 48000.0;
    const float fill = std::clamp (s.crowdFill, 0.0f, 1.0f);

    float rt[kNumBands];
    float rtMax = 0.0f;
    for (int b = 0; b < kNumBands; ++b) { rt[b] = rtFor (v, fill, b); rtMax = std::max (rtMax, rt[b]); }
    const float rtMid = rt[4];

    const bool open = v.outdoor;
    const bool openWithWalls = open && v.category == Category::Club; // courtyard / terrace style
    const double L = v.lengthM, W = v.widthM, H = open ? 0.0 : v.heightM;

    // ---------------------------------------------------------------- geometry
    const double spread = std::clamp (W * 0.3, 1.5, 12.0);
    const double topZ = open ? (double) rig.topHeightM : std::clamp ((double) rig.topHeightM, 1.5, std::max (1.6, H - 0.7));
    const Vec3 spk[2] = { { 1.5, W * 0.5 - spread, topZ }, { 1.5, W * 0.5 + spread, topZ } };
    const double maxD = std::max (2.0, L - 2.5);
    const Vec3 lis { std::clamp (1.5 + (double) s.distanceM, 2.0, maxD), W * 0.5, 1.7 };
    const double r0 = dist (spk[0], lis);

    // ---------------------------------------------------------------- buffer length
    double lenSec = std::clamp ((double) rtMax * 1.15, 0.35, 4.5);
    if (open) lenSec = std::max (lenSec, 0.6);
    // make room for the strongest back-wall echo in big rooms
    if (! open || openWithWalls) lenSec = std::max (lenSec, std::min (4.5, 2.2 * L / kSpeedOfSound + 0.1));
    const size_t N = (size_t) (lenSec * sr);

    IRResult res;
    res.sampleRate = sr;
    res.left.assign (N, 0.0f);
    res.right.assign (N, 0.0f);
    res.effectiveRtMid = rtMid;

    std::vector<float> er[2] = { std::vector<float> (N, 0.0f), std::vector<float> (N, 0.0f) };

    // ---------------------------------------------------------------- surfaces
    const double aWall  = 0.05 + 0.85 * v.wallAbsorb;
    const double aStage = std::max (aWall, 0.5);           // drapes / backline behind the DJ
    const double aCeil  = 0.05 + 0.7 * v.wallAbsorb;
    const double aFloor = 0.03 + 0.55 * fill + (open ? 0.2 : 0.0);
    const double bWall = std::sqrt (1 - std::min (0.98, aWall));
    const double bStage = std::sqrt (1 - std::min (0.98, aStage));
    const double bCeil = std::sqrt (1 - std::min (0.98, aCeil));
    const double bFloor = std::sqrt (1 - std::min (0.98, aFloor));
    constexpr double dirStage = 0.45; // horns radiate forward: little energy hits the wall behind them
    constexpr double dirCeil  = 0.6;  // ...and they are aimed down at the crowd

    // ---------------------------------------------------------------- image sources
    for (int ch = 0; ch < 2; ++ch)
    {
        const Vec3& S = spk[ch];

        if (! open || openWithWalls)
        {
            const int maxOrder = open ? 2 : 3;
            for (int px = 0; px <= 1; ++px)
            for (int mx = -2; mx <= 2; ++mx)
            for (int py = 0; py <= 1; ++py)
            for (int my = -2; my <= 2; ++my)
            for (int pz = 0; pz <= 1; ++pz)
            for (int mz = -2; mz <= 2; ++mz)
            {
                const int nx0 = std::abs (mx - px), nxL = std::abs (mx);
                const int ny0 = std::abs (my - py), nyW = std::abs (my);
                const int nz0 = std::abs (mz - pz), nzH = std::abs (mz);
                if (open && nzH > 0) continue;   // no ceiling
                if (open && nz0 > 1) continue;
                const int order = nx0 + nxL + ny0 + nyW + nz0 + nzH;
                if (order == 0 || order > maxOrder) continue;

                const Vec3 img { (1 - 2 * px) * S.x + 2 * mx * L,
                                 (1 - 2 * py) * S.y + 2 * my * W,
                                 (1 - 2 * pz) * S.z + 2 * mz * (open ? 0.0 : H) };
                const double r = dist (img, lis);
                double amp = r0 / r;
                amp *= std::pow (bStage * dirStage, nx0) * std::pow (bWall, nxL + ny0 + nyW)
                     * std::pow (bFloor, nz0) * std::pow (bCeil * dirCeil, nzH);
                addImpulse (er[ch], (r - r0) / kSpeedOfSound, sr, (float) amp);
            }
        }
        else
        {
            // open field: ground bounce only
            const Vec3 img { S.x, S.y, -S.z };
            const double r = dist (img, lis);
            addImpulse (er[ch], (r - r0) / kSpeedOfSound, sr, (float) (r0 / r * bFloor));
        }

        if (open)
        {
            // scatter: trees, delay towers, bars, buildings far away
            Rng rng (0xC0FFEEu + (uint32_t) s.venueIndex * 977u + (uint32_t) ch * 31u);
            const float hardness = 1.0f - v.wallAbsorb;
            const int count = v.rig == RigType::HornLoaded && v.category == Category::Festival ? 70 : 28; // forest = dense
            for (int i = 0; i < count; ++i)
            {
                const double t = 0.02 + 0.33 * rng.uni();
                const float amp = rng.gauss() * 0.05f * hardness * (float) std::exp (-t / 0.18);
                addImpulse (er[ch], t, sr, amp);
            }
        }
    }

    // reflections lose top end at the surfaces, and the crowd soaks up more
    for (int ch = 0; ch < 2; ++ch)
    {
        Biquad lp, hs;
        lp.lowpass (sr, 4000.0 + 10000.0 * (1.0 - v.wallAbsorb), 0.6);
        hs.highShelf (sr, 2000.0, -4.0 * fill);
        lp.processBlock (er[ch].data(), (int) N);
        hs.processBlock (er[ch].data(), (int) N);
    }
    const double eEr = 0.5 * (energy (er[0]) + energy (er[1]));

    // ---------------------------------------------------------------- reverb / direct balance
    double ratio;
    if (! open)
    {
        const double V = L * W * H;
        const double S = 2.0 * (L * W + L * H + W * H);
        double abar = 1.0 - std::exp (-0.161 * V / (S * std::max (0.1f, rtMid))); // Eyring
        abar = std::clamp (abar, 0.02, 0.95);
        const double R = S * abar / (1.0 - abar);
        const double Q = rig.directivityQ;
        constexpr double perceptual = 1.6; // real rooms are less ideal than the diffuse-field model
        ratio = std::clamp (16.0 * kPi * r0 * r0 / (Q * R) * perceptual, 0.03, 12.0);
        // very large rooms: the diffuse model over-credits seat absorption, keep a realistic floor
        ratio = std::max (ratio, std::min (12.0, 0.3 * rtMid * std::pow (r0 / 20.0, 1.5)));
        res.criticalDistanceM = (float) (r0 / std::sqrt (ratio));
    }
    else
    {
        const double k = r0 / 30.0;
        ratio = std::clamp (0.015 + 0.03 * v.rtMid * k * k + (openWithWalls ? 0.08 : 0.0), 0.01, 0.3);
        res.criticalDistanceM = 0.0f;
    }
    res.reverbToDirect = (float) ratio;
    const double tailTarget = std::max (ratio - eEr, 0.35 * ratio);

    // ---------------------------------------------------------------- late tail
    const double V = std::max (50.0, L * W * std::max (H, 4.0));
    const double tMix = std::clamp (std::sqrt (V) / 1000.0, 0.02, 0.15);
    const double tStart = std::clamp (0.3 * tMix, 0.004, 0.04);

    std::vector<float> tail[2] = { std::vector<float> (N, 0.0f), std::vector<float> (N, 0.0f) };
    for (int ch = 0; ch < 2; ++ch)
    {
        Rng rng (0x9E3779B9u ^ (uint32_t) (ch * 0x85EBCA6Bu) ^ (uint32_t) s.venueIndex);
        std::vector<float> noise (N);
        for (auto& x : noise) x = rng.gauss();

        for (int b = 0; b < kNumBands; ++b)
        {
            Biquad bp1, bp2;
            bp1.bandpass (sr, kBandHz[b], 1.0);
            bp2.bandpass (sr, kBandHz[b], 1.0);
            const double decayPerSample = std::exp (-6.9078 / ((double) rt[b] * sr));
            double env = 1.0;
            for (size_t i = 0; i < N; ++i)
            {
                const float y = bp2.process (bp1.process (noise[i]));
                tail[ch][i] += y * (float) env;
                env *= decayPerSample;
            }
        }

        // fade-in: no tail before the first reflections, full density after the mixing time
        for (size_t i = 0; i < N; ++i)
        {
            const double t = (double) i / sr;
            double g;
            if (t < tStart) g = 0.0;
            else if (t < tMix) { const double u = (t - tStart) / (tMix - tStart); g = 0.5 - 0.5 * std::cos (kPi * u); }
            else g = 1.0;
            tail[ch][i] *= (float) g;
        }
        // gentle fade-out over the last 10% so the buffer never truncates abruptly
        const size_t fadeN = N / 10;
        for (size_t i = 0; i < fadeN; ++i)
            tail[ch][N - 1 - i] *= (float) i / (float) fadeN;
    }

    const double eTail = 0.5 * (energy (tail[0]) + energy (tail[1]));
    const float tailGain = eTail > 0 ? (float) std::sqrt (tailTarget / eTail) : 0.0f;

    // ---------------------------------------------------------------- assemble + normalise
    for (size_t i = 0; i < N; ++i)
    {
        res.left[i]  = er[0][i] + tail[0][i] * tailGain;
        res.right[i] = er[1][i] + tail[1][i] * tailGain;
    }
    res.left[0] += 1.0f;
    res.right[0] += 1.0f;

    const double eTot = 0.5 * (energy (res.left) + energy (res.right));
    const float norm = eTot > 0 ? (float) (1.0 / std::sqrt (eTot)) : 1.0f;
    for (size_t i = 0; i < N; ++i) { res.left[i] *= norm; res.right[i] *= norm; }

    return res;
}

} // namespace pt
