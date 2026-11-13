#pragma once
// PeakTime - small, dependency-free DSP building blocks (RBJ cookbook biquads etc.)
#include <cmath>
#include <algorithm>
#include <vector>
#include <cstdint>

namespace pt
{
constexpr double kPi = 3.14159265358979323846;
constexpr double kSpeedOfSound = 343.0;

inline float dbToGain (float db) { return std::pow (10.0f, db * 0.05f); }
inline float gainToDb (float g, float floorDb = -120.0f) { return g > 0.0f ? std::max (floorDb, 20.0f * std::log10 (g)) : floorDb; }

struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    double z1 = 0, z2 = 0;

    void reset() { z1 = z2 = 0; }

    inline float process (float x) noexcept
    {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return (float) y;
    }

    void processBlock (float* d, int n) noexcept { for (int i = 0; i < n; ++i) d[i] = process (d[i]); }

    void set (double B0, double B1, double B2, double A0, double A1, double A2)
    {
        b0 = B0 / A0; b1 = B1 / A0; b2 = B2 / A0; a1 = A1 / A0; a2 = A2 / A0;
    }

    void setIdentity() { b0 = 1; b1 = b2 = a1 = a2 = 0; }

    static double clampF (double f, double sr) { return std::clamp (f, 5.0, sr * 0.49); }

    void lowpass (double sr, double f, double q = 0.70710678)
    {
        f = clampF (f, sr);
        const double w = 2 * kPi * f / sr, c = std::cos (w), al = std::sin (w) / (2 * q);
        set ((1 - c) / 2, 1 - c, (1 - c) / 2, 1 + al, -2 * c, 1 - al);
    }

    void highpass (double sr, double f, double q = 0.70710678)
    {
        f = clampF (f, sr);
        const double w = 2 * kPi * f / sr, c = std::cos (w), al = std::sin (w) / (2 * q);
        set ((1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + al, -2 * c, 1 - al);
    }

    void bandpass (double sr, double f, double q)
    {
        f = clampF (f, sr);
        const double w = 2 * kPi * f / sr, c = std::cos (w), al = std::sin (w) / (2 * q);
        set (al, 0, -al, 1 + al, -2 * c, 1 - al);
    }

    void peak (double sr, double f, double q, double gainDb)
    {
        f = clampF (f, sr);
        const double A = std::pow (10.0, gainDb / 40.0);
        const double w = 2 * kPi * f / sr, c = std::cos (w), al = std::sin (w) / (2 * q);
        set (1 + al * A, -2 * c, 1 - al * A, 1 + al / A, -2 * c, 1 - al / A);
    }

    void lowShelf (double sr, double f, double gainDb)
    {
        f = clampF (f, sr);
        const double A = std::pow (10.0, gainDb / 40.0);
        const double w = 2 * kPi * f / sr, c = std::cos (w), s = std::sin (w);
        const double al = s / 2 * std::sqrt (2.0), sa = 2 * std::sqrt (A) * al;
        set (A * ((A + 1) - (A - 1) * c + sa),
             2 * A * ((A - 1) - (A + 1) * c),
             A * ((A + 1) - (A - 1) * c - sa),
             (A + 1) + (A - 1) * c + sa,
             -2 * ((A - 1) + (A + 1) * c),
             (A + 1) + (A - 1) * c - sa);
    }

    void highShelf (double sr, double f, double gainDb)
    {
        f = clampF (f, sr);
        const double A = std::pow (10.0, gainDb / 40.0);
        const double w = 2 * kPi * f / sr, c = std::cos (w), s = std::sin (w);
        const double al = s / 2 * std::sqrt (2.0), sa = 2 * std::sqrt (A) * al;
        set (A * ((A + 1) + (A - 1) * c + sa),
             -2 * A * ((A - 1) + (A + 1) * c),
             A * ((A + 1) + (A - 1) * c - sa),
             (A + 1) - (A - 1) * c + sa,
             2 * ((A - 1) - (A + 1) * c),
             (A + 1) - (A - 1) * c - sa);
    }

    // magnitude response (for tests / UI)
    double magnitude (double sr, double f) const
    {
        const double w = 2 * kPi * f / sr;
        const double cr1 = std::cos (w), ci1 = -std::sin (w), cr2 = std::cos (2 * w), ci2 = -std::sin (2 * w);
        const double nr = b0 + b1 * cr1 + b2 * cr2, ni = b1 * ci1 + b2 * ci2;
        const double dr = 1 + a1 * cr1 + a2 * cr2, di = a1 * ci1 + a2 * ci2;
        return std::sqrt ((nr * nr + ni * ni) / (dr * dr + di * di));
    }
};

// 4th-order Linkwitz-Riley crossover (one channel). low + high sums to an allpass.
struct LR4
{
    Biquad lp1, lp2, hp1, hp2;
    void set (double sr, double f)
    {
        lp1.lowpass (sr, f); lp2.lowpass (sr, f);
        hp1.highpass (sr, f); hp2.highpass (sr, f);
    }
    void reset() { lp1.reset(); lp2.reset(); hp1.reset(); hp2.reset(); }
    inline void process (float x, float& lo, float& hi) noexcept
    {
        lo = lp2.process (lp1.process (x));
        hi = hp2.process (hp1.process (x));
    }
};

// fractional delay line (linear interpolation)
struct DelayLine
{
    std::vector<float> buf; int w = 0;
    void prepare (int maxSamples) { buf.assign ((size_t) std::max (4, maxSamples + 4), 0.0f); w = 0; }
    void reset() { std::fill (buf.begin(), buf.end(), 0.0f); w = 0; }
    inline float process (float x, float delaySamples) noexcept
    {
        const int size = (int) buf.size();
        buf[(size_t) w] = x;
        delaySamples = std::clamp (delaySamples, 0.0f, (float) (size - 3));
        float rp = (float) w - delaySamples;
        while (rp < 0) rp += (float) size;
        // rp lies between i0 (older) and i0+1 (newer)
        const int i0 = (int) rp % size;
        const float fr = rp - std::floor (rp);
        const float yi = buf[(size_t) i0] * (1.0f - fr) + buf[(size_t) ((i0 + 1) % size)] * fr;
        w = (w + 1) % size;
        return yi;
    }
};

// simple deterministic RNG (xorshift)
struct Rng
{
    uint32_t s;
    explicit Rng (uint32_t seed = 0x1234567u) : s (seed ? seed : 1u) {}
    inline uint32_t next() noexcept { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    inline float uni() noexcept { return (float) (next() >> 8) * (1.0f / 16777216.0f); }          // [0,1)
    inline float bi() noexcept { return uni() * 2.0f - 1.0f; }                                     // [-1,1)
    inline float gauss() noexcept { return (uni() + uni() + uni() + uni() - 2.0f) * 1.7320508f; } // ~N(0,1)
};

// fast soft clipper (rational tanh approximation, exact-ish in [-3,3])
inline float softClip (float x) noexcept
{
    x = std::clamp (x, -3.0f, 3.0f);
    return x * (27.0f + x * x) / (27.0f + 9.0f * x * x);
}

} // namespace pt
