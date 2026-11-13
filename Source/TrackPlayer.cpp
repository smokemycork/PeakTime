#include "TrackPlayer.h"
#include "dsp/VenueEngine.h"

TrackPlayer::TrackPlayer()
{
    formats.registerBasicFormats();
    for (auto& l : loading) l.store (false);
}

TrackPlayer::~TrackPlayer()
{
    pool.removeAllJobs (true, 10000);
}

bool TrackPlayer::canLoad (const juce::File& f) const
{
    return f.existsAsFile() && formats.findFormatForFileExtension (f.getFileExtension()) != nullptr;
}

std::shared_ptr<TrackData> TrackPlayer::decode (const juce::File& file, juce::String& error)
{
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    if (reader == nullptr) { error = "Can't read " + file.getFileName(); return {}; }

    const double sr = reader->sampleRate > 0 ? reader->sampleRate : 44100.0;
    const auto maxLen = (juce::int64) (sr * 60.0 * 20.0); // 20 minutes is plenty for a track
    const int len = (int) juce::jmin (reader->lengthInSamples, maxLen);
    if (len <= 0) { error = file.getFileName() + " is empty"; return {}; }

    auto d = std::make_shared<TrackData>();
    d->audio.setSize (2, len);
    d->audio.clear();
    reader->read (&d->audio, 0, len, 0, true, true);
    if (reader->numChannels == 1)
        d->audio.copyFrom (1, 0, d->audio, 0, 0, len);

    d->sampleRate = sr;
    d->lengthSec = len / sr;
    d->name = file.getFileNameWithoutExtension();
    d->path = file.getFullPathName();
    d->lufs = pt::integratedLufs (d->audio.getArrayOfReadPointers(), 2, len, sr);

    constexpr int numPeaks = 1200;
    d->peaks.assign (numPeaks, 0.0f);
    const float* l = d->audio.getReadPointer (0);
    const float* r = d->audio.getReadPointer (1);
    for (int p = 0; p < numPeaks; ++p)
    {
        const int a = (int) ((juce::int64) p * len / numPeaks);
        const int b = juce::jmax (a + 1, (int) ((juce::int64) (p + 1) * len / numPeaks));
        float m = 0.0f;
        for (int i = a; i < b && i < len; ++i)
            m = juce::jmax (m, std::abs (l[i]), std::abs (r[i]));
        d->peaks[(size_t) p] = juce::jmin (1.0f, m);
    }
    return d;
}

void TrackPlayer::loadAsync (int slot, const juce::File& file)
{
    if (slot < 0 || slot >= numSlots) return;
    loading[slot].store (true);
    ++version;

    pool.addJob ([this, slot, file]
    {
        juce::String err;
        auto data = decode (file, err);
        if (data != nullptr)
        {
            std::shared_ptr<TrackData> old;
            {
                const juce::SpinLock::ScopedLockType sl (slotLock);
                old = std::move (slots[slot]);
                slots[slot] = std::move (data);
            }
            const juce::ScopedLock ml (miscLock);
            if (old != nullptr) graveyard.push_back (std::move (old));
            errors[slot].clear();
        }
        else
        {
            const juce::ScopedLock ml (miscLock);
            errors[slot] = err;
        }
        loading[slot].store (false);
        ++version;
    });
}

void TrackPlayer::clear (int slot)
{
    if (slot < 0 || slot >= numSlots) return;
    std::shared_ptr<TrackData> old;
    {
        const juce::SpinLock::ScopedLockType sl (slotLock);
        old = std::move (slots[slot]);
        slots[slot].reset();
    }
    const juce::ScopedLock ml (miscLock);
    if (old != nullptr) graveyard.push_back (std::move (old));
    errors[slot].clear();
    ++version;
}

std::shared_ptr<const TrackData> TrackPlayer::get (int slot) const
{
    if (slot < 0 || slot >= numSlots) return {};
    const juce::SpinLock::ScopedLockType sl (slotLock);
    return slots[slot];
}

juce::String TrackPlayer::getError (int slot) const
{
    const juce::ScopedLock ml (miscLock);
    return errors[slot];
}

void TrackPlayer::collectGarbage()
{
    const juce::ScopedLock ml (miscLock);
    graveyard.erase (std::remove_if (graveyard.begin(), graveyard.end(),
                                     [] (const std::shared_ptr<TrackData>& p) { return p.use_count() <= 1; }),
                     graveyard.end());
}

float TrackPlayer::getMatchGainDb (int slot) const
{
    // the reference is matched to the loudness of your track (never the other way round)
    if (! matchLoudness.load() || slot != 1) return 0.0f;
    auto a = get (0), b = get (1);
    if (a == nullptr || b == nullptr) return 0.0f;
    return juce::jlimit (-24.0f, 24.0f, a->lufs - b->lufs);
}

void TrackPlayer::render (juce::AudioBuffer<float>& buffer, int n, double hostSr, int slot) noexcept
{
    std::shared_ptr<TrackData> t, mine;
    {
        const juce::SpinLock::ScopedTryLockType sl (slotLock);
        if (sl.isLocked())
        {
            t = slots[juce::jlimit (0, numSlots - 1, slot)];
            mine = slots[0];
        }
    }

    const int numCh = buffer.getNumChannels();
    for (int c = 0; c < numCh; ++c) buffer.clear (c, 0, n);
    if (t == nullptr || ! playing.load() || numCh < 2 || hostSr <= 0) return;

    float gain = 1.0f;
    if (slot == 1 && matchLoudness.load() && mine != nullptr)
        gain = juce::Decibels::decibelsToGain (juce::jlimit (-24.0f, 24.0f, mine->lufs - t->lufs));

    const int len = t->audio.getNumSamples();
    if (len < 2) return;
    const float* srcL = t->audio.getReadPointer (0);
    const float* srcR = t->audio.getReadPointer (1);
    float* outL = buffer.getWritePointer (0);
    float* outR = buffer.getWritePointer (1);

    const double ratio = t->sampleRate / hostSr;
    double p = positionSec.load() * t->sampleRate;
    if (p >= len - 1) p = looping.load() ? std::fmod (p, (double) (len - 1)) : (double) (len - 1);

    for (int i = 0; i < n; ++i)
    {
        if (p >= len - 1)
        {
            if (looping.load()) p = 0.0;
            else { playing.store (false); break; }
        }
        const int i0 = (int) p;
        const float fr = (float) (p - i0);
        outL[i] = (srcL[i0] + fr * (srcL[i0 + 1] - srcL[i0])) * gain;
        outR[i] = (srcR[i0] + fr * (srcR[i0 + 1] - srcR[i0])) * gain;
        p += ratio;
    }
    positionSec.store (p / t->sampleRate);
}
