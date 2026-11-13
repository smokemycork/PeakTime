#pragma once
#include "PluginProcessor.h"
#include "ui/Components.h"

class PeakTimeEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit PeakTimeEditor (PeakTimeProcessor&);
    ~PeakTimeEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    PeakTimeProcessor& proc;
    PeakTimeLookAndFeel lnf;

    Segmented ab { { "ORIGINAL", "IN VENUE" } };
    juce::TextButton headphones { "HEADPHONES" }, levelMatch { "LEVEL MATCH" }, help { "?" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> hpAtt, lmAtt;

    VenueBrowser browser;
    FloorPlan floor { proc };
    Knob distance { "Distance", theme::accent2 }, crowdLevel { "Crowd level", theme::warn },
         crowdFill { "Crowd fill", theme::warn }, output { "Output", theme::dim };
    Segmented systemSel { { "CLEAN", "WORKING", "RED LINE" }, true };
    RigMeters meters { proc };
    SpectrumView spectrum { proc };
    PlayerStrip player { proc };
    HelpOverlay helpOverlay;

    juce::Rectangle<int> headerArea, statsArea, controlsArea;
    int shownVenue = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PeakTimeEditor)
};
