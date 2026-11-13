#pragma once
// PeakTime - built-in player for referencing: "My track" and "Reference" slots.
// Files are decoded on a background thread, loudness-analysed (integrated LUFS),
// and played through the venue with a shared playhead so A/B switching keeps your place.

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>
#include <atomic>
#include <memory>
#include <vector>

struct TrackData
{
    juce::AudioBuffer<float> audio;
    double sampleRate = 44100.0;
    juce::String name, path;
    float lufs = -70.0f;
    double lengthSec = 0.0;
    std::vector<float> peaks; // waveform overview, 0..1
};

class TrackPlayer
{
public:
    static constexpr int numSlots = 2; // 0 = my track, 1 = reference

    TrackPlayer();
    ~TrackPlayer();

    void loadAsync (int slot, const juce::File& file);
    void clear (int slot);

    // audio thread: writes the selected slot into channels 0/1 of buffer (replacing them)
    void render (juce::AudioBuffer<float>& buffer, int numSamples, double hostSampleRate, int slot) noexcept;

    // message thread
    std::shared_ptr<const TrackData> get (int slot) const;
    void collectGarbage();
    juce::String getError (int slot) const;
    bool isLoading (int slot) const { return loading[slot].load(); }
    int getVersion() const { return version.load(); }
    float getMatchGainDb (int slot) const;

    void setPlaying (bool p) { playing.store (p); }
    bool isPlaying() const { return playing.load(); }
    void setPositionSec (double s) { positionSec.store (juce::jmax (0.0, s)); }
    double getPositionSec() const { return positionSec.load(); }
    void setLooping (bool l) { looping.store (l); }
    bool isLooping() const { return looping.load(); }
    void setMatchLoudness (bool m) { matchLoudness.store (m); ++version; }
    bool getMatchLoudness() const { return matchLoudness.load(); }

    juce::String getWildcard() const { return "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg;*.m4a"; }
    bool canLoad (const juce::File& f) const;

private:
    std::shared_ptr<TrackData> decode (const juce::File& file, juce::String& error);

    juce::AudioFormatManager formats;
    mutable juce::SpinLock slotLock;
    std::shared_ptr<TrackData> slots[numSlots];

    mutable juce::CriticalSection miscLock;
    std::vector<std::shared_ptr<TrackData>> graveyard;
    juce::String errors[numSlots];

    std::atomic<bool> playing { false }, looping { true }, matchLoudness { true };
    std::atomic<bool> loading[numSlots];
    std::atomic<double> positionSec { 0.0 };
    std::atomic<int> version { 0 };

    juce::ThreadPool pool { 1 }; // declared last: destroyed first, so jobs never outlive the members

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TrackPlayer)
};
