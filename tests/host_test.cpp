// Headless host test: runs the real processor + editor. Not part of the shipped plugin.
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <chrono>

static void pump (int ms) { juce::MessageManager::getInstance()->runDispatchLoopUntil (ms); }

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const juce::File outDir = argc > 1 ? juce::File (argv[1]) : juce::File::getCurrentWorkingDirectory();
    int failures = 0;
    auto check = [&] (bool ok, const juce::String& msg) { if (! ok) { ++failures; std::cout << "FAIL: " << msg << "\n"; } };

    const double sr = 48000.0; const int bs = 512;
    auto proc = std::make_unique<PeakTimeProcessor>();
    proc->setPlayConfigDetails (2, 2, sr, bs);
    proc->prepareToPlay (sr, bs);

    // test signal: kick + bass + hats + pad, about -10 LUFS
    const int len = (int) sr * 8;
    juce::AudioBuffer<float> sig (2, len);
    juce::Random rnd (1);
    for (int i = 0; i < len; ++i)
    {
        const double t = i / sr, beat = std::fmod (t, 0.5), off = std::fmod (t + 0.25, 0.5);
        const float kick = (float) (std::exp (-beat * 14) * std::sin (2 * juce::MathConstants<double>::pi * (48 + 140 * std::exp (-beat * 35)) * beat));
        const float bass = (float) (0.35 * std::sin (2 * juce::MathConstants<double>::pi * 55 * t) * (off < 0.2 ? 1.0 : 0.2));
        const float hat = (float) ((rnd.nextFloat() * 2 - 1) * std::exp (-off * 60) * 0.25);
        const float pad = (float) (0.08 * std::sin (2 * juce::MathConstants<double>::pi * 440 * t + 0.3 * std::sin (t)));
        sig.setSample (0, i, 0.8f * kick + bass + hat + pad);
        sig.setSample (1, i, 0.8f * kick + bass + hat * 0.7f + pad * 1.1f);
    }
    // write it so the player can load it too
    const auto wav = outDir.getChildFile ("test_track.wav");
    {
        wav.deleteFile();
        juce::WavAudioFormat fmt;
        std::unique_ptr<juce::AudioFormatWriter> w (fmt.createWriterFor (new juce::FileOutputStream (wav), sr, 2, 24, {}, 0));
        w->writeFromAudioSampleBuffer (sig, 0, len);
    }

    juce::MidiBuffer midi;
    auto runSeconds = [&] (double seconds, double& maxBlockMs, double& rmsOut, bool& finite)
    {
        juce::AudioBuffer<float> b (2, bs);
        const int blocks = (int) (seconds * sr / bs);
        double sum = 0; long count = 0; maxBlockMs = 0; finite = true;
        pt::KWeighting kw[2]; kw[0].prepare (sr); kw[1].prepare (sr);
        for (int k = 0; k < blocks; ++k)
        {
            const int off = (k * bs) % (len - bs);
            b.copyFrom (0, 0, sig, 0, off, bs); b.copyFrom (1, 0, sig, 1, off, bs);
            const auto t0 = std::chrono::steady_clock::now();
            proc->processBlock (b, midi);
            maxBlockMs = std::max (maxBlockMs, std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0).count());
            for (int c = 0; c < 2; ++c) for (int i = 0; i < bs; ++i) { const float x = b.getSample (c, i); if (! std::isfinite (x)) finite = false; const float y = kw[c].process (x); if (k > blocks / 2) { sum += y * y; ++count; } }
            if (k % 16 == 0) pump (1);
        }
        rmsOut = count ? std::sqrt (sum / count) : 0;
    };

    double inRms = 0;
    for (int c = 0; c < 2; ++c) { pt::KWeighting kw; kw.prepare (sr); for (int i = 0; i < len; ++i) { const float y = kw.process (sig.getSample (c, i)); inRms += y * y; } }
    inRms = std::sqrt (inRms / (2.0 * len));
    std::cout << "input K-weighted rms " << juce::Decibels::gainToDecibels (inRms) << " dBFS, block budget " << (bs / sr * 1000) << " ms\n";

    for (int v = 0; v < pt::numVenues(); ++v)
    {
        proc->applyVenueDefaults (v);
        pump (120);                                // processor timer -> IR worker
        double ms, rms; bool fin;
        runSeconds (3.0, ms, rms, fin);
        check (fin, juce::String ("non-finite output at venue ") + pt::venues()[(size_t) v].name);
        const float outDb = juce::Decibels::gainToDecibels ((float) rms);
        check (std::abs (outDb - juce::Decibels::gainToDecibels ((float) inRms)) < 2.0f, juce::String ("level match off at ") + pt::venues()[(size_t) v].name + " " + juce::String (outDb));
        std::cout << juce::String (pt::venues()[(size_t) v].name).paddedRight (' ', 32) << " out " << juce::String (outDb, 1)
                  << " dBFS  worst block " << juce::String (ms, 2) << " ms  RT " << juce::String (proc->diagRtMid.load(), 2) << "\n";
    }

    // A/B and headphones toggles, red line
    proc->setParam (ParamID::inVenue, 0.0f);
    { double ms, rms; bool fin; runSeconds (1.0, ms, rms, fin); check (std::abs (rms - inRms) / inRms < 0.05, "original path should be the input: " + juce::String (rms / inRms)); }
    proc->setParam (ParamID::inVenue, 1.0f);
    proc->setParam (ParamID::headphones, 1.0f);
    proc->setParam (ParamID::system, 2.0f);
    { double ms, rms; bool fin; runSeconds (1.0, ms, rms, fin); check (fin, "headphones/red line non-finite"); }

    // player: load the track into "my track" and play through the venue
    proc->applyVenueDefaults (1); // Stereo Montreal
    proc->player.loadAsync (0, wav);
    for (int i = 0; i < 100 && proc->player.get (0) == nullptr; ++i) pump (50);
    check (proc->player.get (0) != nullptr, "player failed to load wav");
    if (auto t = proc->player.get (0)) std::cout << "loaded " << t->name << " " << juce::String (t->lufs, 1) << " LUFS, " << juce::String (t->lengthSec, 1) << " s\n";
    proc->setParam (ParamID::source, 1.0f);
    proc->player.setPlaying (true);
    {
        juce::AudioBuffer<float> b (2, bs);
        double sum = 0;
        for (int k = 0; k < 300; ++k) { b.clear(); proc->processBlock (b, midi); for (int i = 0; i < bs; ++i) sum += b.getSample (0, i) * b.getSample (0, i); if (k % 16 == 0) pump (1); }
        check (sum > 1.0, "player produced silence");
        std::cout << "player position " << proc->player.getPositionSec() << " s\n";
    }

    // state round trip
    {
        juce::MemoryBlock mb; proc->getStateInformation (mb);
        auto p2 = std::make_unique<PeakTimeProcessor>();
        p2->setStateInformation (mb.getData(), (int) mb.getSize());
        check (p2->getVenueIndex() == 1, "state did not restore venue");
        for (int i = 0; i < 100 && p2->player.get (0) == nullptr; ++i) pump (50);
        check (p2->player.get (0) != nullptr, "state did not restore player track");
    }

    // editor screenshot (in-venue view at Berghain, mid floor, with spectrum running)
    proc->setParam (ParamID::headphones, 0.0f);
    proc->applyVenueDefaults (8);
    {
        std::unique_ptr<juce::AudioProcessorEditor> ed (proc->createEditor());
        ed->setSize (1180, 790);
        juce::AudioBuffer<float> b (2, bs);
        for (int k = 0; k < 400; ++k) { b.clear(); proc->processBlock (b, midi); if (k % 4 == 0) pump (2); }
        pump (200);
        auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
        juce::PNGImageFormat png;
        auto f = outDir.getChildFile ("screenshot.png"); f.deleteFile();
        juce::FileOutputStream os (f); png.writeImageToStream (img, os);
        std::cout << "screenshot " << f.getFullPathName() << "\n";

        // second shot: open-air festival + red line
        proc->applyVenueDefaults (32);
        proc->setParam (ParamID::source, 2.0f);
        for (int k = 0; k < 300; ++k) { b.clear(); proc->processBlock (b, midi); if (k % 4 == 0) pump (2); }
        pump (200);
        auto img2 = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
        auto f2 = outDir.getChildFile ("screenshot_festival.png"); f2.deleteFile();
        juce::FileOutputStream os2 (f2); png.writeImageToStream (img2, os2);
    }

    proc->releaseResources();
    proc.reset();
    std::cout << (failures ? juce::String (failures) + " FAILURES" : juce::String ("HOST TEST PASSED")) << "\n";
    return failures ? 1 : 0;
}
