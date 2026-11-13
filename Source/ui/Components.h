#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "Theme.h"

class PeakTimeProcessor;

// ------------------------------------------------------------------------- segmented switch
class Segmented : public juce::Component
{
public:
    Segmented (juce::StringArray items, bool vertical = false);
    void attach (juce::RangedAudioParameter& p);
    void setIndex (int i);
    int getIndex() const { return index; }
    void setItemColour (int item, juce::Colour c) { colours.set (item, c); repaint(); }
    std::function<void (int)> onChange;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    juce::StringArray items;
    bool vertical;
    int index = 0;
    juce::HashMap<int, juce::Colour> colours;
    std::unique_ptr<juce::ParameterAttachment> attachment;
};

// ------------------------------------------------------------------------- labelled knob
class Knob : public juce::Component
{
public:
    Knob (const juce::String& title, juce::Colour colour);
    void attach (juce::AudioProcessorValueTreeState& s, const juce::String& id);
    void resized() override;
    void paint (juce::Graphics&) override;
    juce::Slider slider;
private:
    juce::String title;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> att;
};

// ------------------------------------------------------------------------- venue browser
class VenueBrowser : public juce::Component, private juce::ListBoxModel
{
public:
    VenueBrowser();
    std::function<void (int)> onVenueChosen;
    void setSelectedVenue (int venueIndex);
    void resized() override;
    void paint (juce::Graphics&) override;

private:
    int getNumRows() override { return (int) filtered.size(); }
    void paintListBoxItem (int row, juce::Graphics&, int w, int h, bool selected) override;
    void selectedRowsChanged (int lastRowSelected) override;
    void refilter();

    Segmented categories { { "ALL", "CLUBS", "ARENAS", "FESTIVALS" } };
    juce::TextEditor search;
    juce::ListBox list;
    std::vector<int> filtered;
    int selected = -1;
    bool suppress = false;
};

// ------------------------------------------------------------------------- floor plan (top view)
class FloorPlan : public juce::Component
{
public:
    explicit FloorPlan (PeakTimeProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    struct Geometry { juce::Rectangle<float> room; float pxPerM = 1.0f; float stageY = 0.0f; float maxDist = 10.0f; };
    Geometry geometry() const;
    void setDistanceFromY (float y);
    PeakTimeProcessor& proc;
    bool dragging = false;
};

// ------------------------------------------------------------------------- original vs in-venue spectrum
class SpectrumView : public juce::Component, private juce::Timer
{
public:
    explicit SpectrumView (PeakTimeProcessor& p);
    void paint (juce::Graphics&) override;

private:
    void timerCallback() override;
    static constexpr int order = 12, fftSize = 1 << order, numBins = fftSize / 2;
    PeakTimeProcessor& proc;
    juce::dsp::FFT fft { order };
    juce::dsp::WindowingFunction<float> window { (size_t) fftSize, juce::dsp::WindowingFunction<float>::hann };
    std::vector<float> ringDry = std::vector<float> (fftSize, 0.f), ringWet = std::vector<float> (fftSize, 0.f);
    int ringPos = 0;
    std::vector<float> fftData = std::vector<float> (fftSize * 2, 0.f);
    std::vector<float> specDry = std::vector<float> (numBins, -100.f), specWet = std::vector<float> (numBins, -100.f);
    std::vector<float> tmpDry = std::vector<float> (8192), tmpWet = std::vector<float> (8192);
    static constexpr int numBands = 5;
    float bandDelta[numBands] = {};
    juce::Path makePath (const std::vector<float>& spec, juce::Rectangle<float> area) const;
};

// ------------------------------------------------------------------------- rig meters
class RigMeters : public juce::Component
{
public:
    explicit RigMeters (PeakTimeProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;
    void tick();
private:
    PeakTimeProcessor& proc;
    float sub = 0.f, top = 0.f;
};

// ------------------------------------------------------------------------- player
class PlayerStrip : public juce::Component
{
public:
    explicit PlayerStrip (PeakTimeProcessor& p);
    void resized() override;
    void paint (juce::Graphics&) override;
    void refresh(); // called by the editor timer

private:
    class SlotRow : public juce::Component, public juce::FileDragAndDropTarget
    {
    public:
        SlotRow (PlayerStrip& owner, int slot);
        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        bool isInterestedInFileDrag (const juce::StringArray& files) override;
        void filesDropped (const juce::StringArray& files, int, int) override;
        void fileDragEnter (const juce::StringArray&, int, int) override { dragOver = true; repaint(); }
        void fileDragExit (const juce::StringArray&) override { dragOver = false; repaint(); }
        juce::TextButton load { "LOAD" }, clear { "X" };
    private:
        juce::Rectangle<int> waveArea() const;
        void seekFrom (int x);
        PlayerStrip& owner;
        int slot;
        bool dragOver = false;
    };

    void loadInto (int slot, const juce::File& f);
    void choose (int slot);
    void select (int slot);
    int activeSlot() const; // -1 when source is the DAW

    PeakTimeProcessor& proc;
    SlotRow rowA { *this, 0 }, rowB { *this, 1 };
    juce::TextButton play { "PLAY" }, loop { "LOOP" }, match { "MATCH LUFS" }, daw { "DAW INPUT" };
    std::unique_ptr<juce::FileChooser> chooser;
    int lastVersion = -1;
    friend class SlotRow;
};

// ------------------------------------------------------------------------- built-in guide
class HelpOverlay : public juce::Component
{
public:
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override { setVisible (false); }
};
