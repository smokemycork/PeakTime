// Offline sanity tests for the PeakTime DSP engine (no JUCE needed).
// g++ -std=c++17 -O2 -I../Source tests/engine_test.cpp ../Source/dsp/*.cpp -o engine_test
#include "dsp/IRGenerator.h"
#include "dsp/VenueEngine.h"
#include <cstdio>
#include <chrono>
#include <cmath>
#include <cstring>

using namespace pt;
static int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { ++failures; std::printf ("FAIL: " __VA_ARGS__); std::printf ("\n"); } } while (0)

static bool finite (const std::vector<float>& v) { for (float x : v) if (! std::isfinite (x)) return false; return true; }
static double rms (const float* x, int n) { double s = 0; for (int i = 0; i < n; ++i) s += (double) x[i] * x[i]; return std::sqrt (s / n); }

int main (int argc, char** argv)
{
    const double sr = 48000.0;
    const bool dump = argc > 1 && std::strcmp (argv[1], "dump") == 0;

    // ------------------------------------------------------------------ venues
    CHECK (numVenues() == 40, "venue count %d", numVenues());
    for (int i = 0; i < numVenues(); ++i)
    {
        const auto& v = venues()[(size_t) i];
        CHECK (v.name != nullptr && v.city != nullptr && v.room != nullptr, "venue %d has null strings", i);
        CHECK (v.lengthM > 5 && v.widthM > 5, "venue %d dims", i);
        CHECK (v.outdoor || v.heightM > 2, "venue %d height", i);
        CHECK (v.defaultDistanceM <= maxDistanceFor (v), "venue %d default distance beyond room", i);
    }

    // ------------------------------------------------------------------ IRs
    std::printf ("%-32s %6s %7s %7s %6s %6s %6s\n", "venue", "dist", "len(s)", "R/D", "Dc(m)", "RTmid", "ms");
    for (int i = 0; i < numVenues(); ++i)
    {
        const auto& v = venues()[(size_t) i];
        for (float d : { 2.0f, v.defaultDistanceM, maxDistanceFor (v) })
        {
            IRSettings s; s.venueIndex = i; s.distanceM = d; s.crowdFill = v.defaultFill; s.sampleRate = sr;
            auto t0 = std::chrono::steady_clock::now();
            auto ir = IRGenerator::generate (s);
            const double ms = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0).count();
            CHECK (finite (ir.left) && finite (ir.right), "%s non-finite IR", v.name);
            CHECK (ir.left.size() > 1000, "%s IR too short", v.name);
            double e = 0; for (size_t k = 0; k < ir.left.size(); ++k) e += 0.5 * (ir.left[k] * ir.left[k] + ir.right[k] * ir.right[k]);
            CHECK (std::fabs (e - 1.0) < 1e-3, "%s energy %f", v.name, e);
            CHECK (ms < 400, "%s IR took %.1f ms", v.name, ms);
            if (d == v.defaultDistanceM)
                std::printf ("%-32s %6.1f %7.2f %7.3f %6.1f %6.2f %6.1f\n", v.name, d, ir.left.size() / sr, ir.reverbToDirect, ir.criticalDistanceM, ir.effectiveRtMid, ms);
            if (dump && d == v.defaultDistanceM && (i == 1 || i == 8 || i == 28 || i == 32))
            {
                char fn[64]; std::snprintf (fn, sizeof fn, "ir_%02d.f32", i);
                FILE* f = std::fopen (fn, "wb"); std::fwrite (ir.left.data(), 4, ir.left.size(), f); std::fclose (f);
            }
        }
    }

    // reverb-to-direct must rise with distance indoors
    {
        IRSettings a; a.venueIndex = 8; a.distanceM = 3; a.crowdFill = 0.8f; a.sampleRate = sr;
        IRSettings b = a; b.distanceM = 26;
        CHECK (IRGenerator::generate (a).reverbToDirect < IRGenerator::generate (b).reverbToDirect, "R/D not increasing with distance");
        IRSettings full = a, empty = a; full.crowdFill = 1.f; empty.crowdFill = 0.f;
        CHECK (IRGenerator::generate (full).effectiveRtMid < IRGenerator::generate (empty).effectiveRtMid, "crowd does not shorten RT");
    }

    // ------------------------------------------------------------------ loudness reference: 997 Hz, 0 dBFS, one channel = -3.01 LUFS
    {
        const int n = (int) sr * 5;
        std::vector<float> a (n), b (n, 0.f);
        for (int i = 0; i < n; ++i) a[i] = (float) std::sin (2 * kPi * 997.0 * i / sr);
        const float* chs[2] = { a.data(), b.data() };
        const float l = integratedLufs (chs, 2, n, sr);
        std::printf ("LUFS 997Hz 0dBFS L-only: %.2f (expect -3.01)\n", l);
        CHECK (std::fabs (l + 3.01f) < 0.15f, "LUFS reference off: %f", l);
    }

    // ------------------------------------------------------------------ rig model
    {
        const int n = (int) sr * 4;
        std::vector<float> src (n);
        Rng rng (7);
        CrowdNoise dummy; (void) dummy;
        // pink-ish test signal with a kick-like LF pulse, peaking near 0 dBFS
        float lp = 0;
        for (int i = 0; i < n; ++i)
        {
            lp = 0.98f * lp + 0.02f * rng.bi();
            const double t = std::fmod (i / sr, 0.5);
            const float kick = (float) (std::exp (-t * 18.0) * std::sin (2 * kPi * (50.0 + 120.0 * std::exp (-t * 30.0)) * t));
            src[i] = 0.25f * rng.bi() + 2.0f * lp + 0.7f * kick;
        }
        float pk = 0; for (float x : src) pk = std::max (pk, std::fabs (x));
        for (auto& x : src) x *= 0.95f / pk;

        for (int venue : { 1, 8, 27, 32 })
            for (float drive : { 0.f, 1.f, 2.f })
            {
                RigModel rig; rig.prepare (sr, 512); rig.setVenue (venue); rig.setDistance (10.f); rig.setDrive (drive);
                std::vector<float> L = src, R = src;
                float maxGr = 0;
                for (int off = 0; off < n; off += 512)
                {
                    const int bs = std::min (512, n - off);
                    rig.process (L.data() + off, R.data() + off, bs);
                    maxGr = std::max (maxGr, rig.getTopGainReductionDb());
                }
                CHECK (finite (L) && finite (R), "rig non-finite");
                {
                    KWeighting a, b; a.prepare (sr); b.prepare (sr); double pi = 0, po = 0;
                    for (int i = 0; i < n; ++i) { const float x = a.process (src[i]), y = b.process (L[i]); pi += x * x; po += y * y; }
                    std::printf ("   K-weighted rig gain %.2f dB\n", 10 * std::log10 (po / pi));
                }
                std::printf ("rig venue %2d drive %.0f: in %.3f out %.3f rms, top GR %.1f dB, align err %.2f ms\n", venue, drive, rms (src.data(), n), rms (L.data(), n), maxGr, rig.getAlignmentErrorMs());
                if (dump && venue == 1)
                {
                    char fn[64]; std::snprintf (fn, sizeof fn, "rig_d%.0f.f32", drive);
                    FILE* f = std::fopen (fn, "wb"); std::fwrite (L.data(), 4, L.size(), f); std::fclose (f);
                }
            }
        if (dump) { FILE* f = std::fopen ("src.f32", "wb"); std::fwrite (src.data(), 4, src.size(), f); std::fclose (f); }
    }

    // ------------------------------------------------------------------ crowd level calibration
    {
        CrowdNoise c; c.prepare (sr);
        const int n = (int) sr * 30;
        std::vector<float> L (n, 0.f), R (n, 0.f);
        for (int off = 0; off < n; off += 512) c.process (L.data() + off, R.data() + off, std::min (512, n - off), 1.0f, 1.0f);
        const double r = rms (L.data() + (int) sr, n - (int) sr);
        std::printf ("crowd rms at target 1.0: %.3f\n", r);
        CHECK (finite (L), "crowd non-finite");
        CHECK (r > 0.6 && r < 1.6, "crowd normalisation off: %f", r);
    }

    // ------------------------------------------------------------------ post chain + crossfeed
    {
        PostChain pc; pc.prepare (sr, 512); pc.setVenue (32); pc.setDistance (60); pc.setCrowd (0.9f, -20); pc.setHeadphones (true);
        std::vector<float> L (48000), R (48000, 0.f);
        Rng rng (3); for (auto& x : L) x = 0.3f * rng.bi();
        for (int off = 0; off < 48000; off += 512) pc.process (L.data() + off, R.data() + off, std::min (512, 48000 - off), 0.2f);
        CHECK (finite (L) && finite (R), "post non-finite");
        CHECK (rms (R.data(), 48000) > 0.01, "crossfeed/crowd produced nothing on R");
    }

    // ------------------------------------------------------------------ delay line
    {
        DelayLine d; d.prepare (100);
        float out = 0; for (int i = 0; i < 20; ++i) { const float y = d.process (i == 0 ? 1.f : 0.f, 5.5f); if (i == 5) out += y; if (i == 6) out += y; }
        CHECK (std::fabs (out - 1.f) < 1e-5, "fractional delay energy %f", out);
    }

    std::printf (failures ? "\n%d FAILURES\n" : "\nALL TESTS PASSED\n", failures);
    return failures ? 1 : 0;
}
