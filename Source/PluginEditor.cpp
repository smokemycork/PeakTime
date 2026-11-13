#include "PluginEditor.h"

using namespace theme;

PeakTimeEditor::PeakTimeEditor (PeakTimeProcessor& p)
    : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&lnf);

    ab.attach (*proc.apvts.getParameter (ParamID::inVenue));
    ab.setItemColour (0, accent2);
    ab.setItemColour (1, accent);
    addAndMakeVisible (ab);

    for (auto* b : { &headphones, &levelMatch })
    {
        b->setClickingTogglesState (true);
        b->setColour (juce::TextButton::buttonOnColourId, accent2);
        addAndMakeVisible (*b);
    }
    hpAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, ParamID::headphones, headphones);
    lmAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, ParamID::levelMatch, levelMatch);
    help.onClick = [this] { helpOverlay.setVisible (true); helpOverlay.toFront (false); };
    addAndMakeVisible (help);

    browser.onVenueChosen = [this] (int v) { proc.applyVenueDefaults (v); };
    addAndMakeVisible (browser);
    addAndMakeVisible (floor);

    distance.attach (proc.apvts, ParamID::distance);
    crowdLevel.attach (proc.apvts, ParamID::crowdLevel);
    crowdFill.attach (proc.apvts, ParamID::crowdFill);
    output.attach (proc.apvts, ParamID::output);
    for (auto* k : { &distance, &crowdLevel, &crowdFill, &output }) addAndMakeVisible (*k);

    systemSel.attach (*proc.apvts.getParameter (ParamID::system));
    systemSel.setItemColour (0, green);
    systemSel.setItemColour (1, warn);
    systemSel.setItemColour (2, hot);
    addAndMakeVisible (systemSel);
    addAndMakeVisible (meters);
    addAndMakeVisible (spectrum);
    addAndMakeVisible (player);

    addChildComponent (helpOverlay);

    setResizable (true, true);
    setResizeLimits (1080, 740, 1800, 1200);
    setSize (1180, 790);
    startTimerHz (24);
}

PeakTimeEditor::~PeakTimeEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void PeakTimeEditor::timerCallback()
{
    const int v = proc.getVenueIndex();
    if (v != shownVenue)
    {
        shownVenue = v;
        browser.setSelectedVenue (v);
        repaint (headerArea);
    }
    repaint (statsArea);
    floor.repaint();
    meters.tick();
    player.refresh();
}

void PeakTimeEditor::paint (juce::Graphics& g)
{
    g.fillAll (bg);

    // ---- top bar
    auto top = getLocalBounds().removeFromTop (60).reduced (18, 0);
    g.setColour (accent);
    g.fillEllipse ((float) top.getX(), (float) top.getCentreY() - 7, 14, 14);
    g.setColour (accent2);
    g.fillEllipse ((float) top.getX() + 9, (float) top.getCentreY() - 7, 14, 14);
    g.setColour (text);
    g.setFont (font (22.0f, true));
    g.drawText ("PEAKTIME", top.withTrimmedLeft (34), juce::Justification::centredLeft);
    g.setColour (dim);
    g.setFont (font (11.0f, true));
    g.drawText ("VENUE SIMULATOR", top.withTrimmedLeft (148), juce::Justification::centredLeft);
    g.setColour (line);
    g.drawHorizontalLine (60, 0.0f, (float) getWidth());

    // ---- venue header
    const auto& v = pt::venues()[(size_t) proc.getVenueIndex()];
    const auto& rig = pt::rigSpec (v.rig);
    auto h = headerArea;
    g.setColour (text);
    g.setFont (font (26.0f, true));
    g.drawText (v.name, h.removeFromTop (32), juce::Justification::centredLeft, true);
    g.setColour (accent);
    g.setFont (font (12.0f, true));
    g.drawText (juce::String (v.city).toUpperCase() + "   -   " + juce::String (pt::categoryName (v.category)).toUpperCase()
                    + "   -   " + juce::String (v.capacity) + " CAP   -   " + juce::String (rig.name).toUpperCase(),
                h.removeFromTop (18), juce::Justification::centredLeft, true);
    g.setColour (text.withAlpha (0.75f));
    g.setFont (font (12.5f));
    g.drawFittedText (v.room, h, juce::Justification::topLeft, 2);

    // ---- stats line
    {
        auto s = statsArea;
        const float rt = proc.diagRtMid.load(), dc = proc.diagCriticalDistance.load(), rd = proc.diagReverbToDirect.load();
        const float align = proc.diagAlignMs.load(), match = proc.diagMatchDb.load();
        juce::StringArray items;
        items.add ("RT60 " + juce::String (rt, 2) + " s");
        items.add (v.outdoor ? juce::String ("OPEN AIR") : "CRITICAL DIST " + juce::String (juce::roundToInt (dc)) + " m");
        items.add ("ROOM/DIRECT " + juce::String (juce::Decibels::gainToDecibels (std::sqrt (juce::jmax (1.0e-6f, rd))), 1) + " dB");
        items.add ("SUB/TOP " + juce::String (align >= 0 ? "+" : "") + juce::String (align, 1) + " ms");
        items.add ("MATCH " + juce::String (match >= 0 ? "+" : "") + juce::String (match, 1) + " dB");
        const int w = s.getWidth() / items.size();
        for (auto& it : items)
        {
            auto c = s.removeFromLeft (w).reduced (3, 0);
            g.setColour (panel);
            g.fillRoundedRectangle (c.toFloat(), 6.0f);
            g.setColour (dim);
            g.setFont (font (10.5f, true));
            g.drawText (it, c, juce::Justification::centred, true);
        }
    }

    // ---- controls panel background
    g.setColour (panel);
    g.fillRoundedRectangle (controlsArea.toFloat(), 10.0f);
    g.setColour (dim);
    g.setFont (font (11.0f, true));
    g.drawText ("CONTROLS", controlsArea.reduced (14, 10).removeFromTop (16), juce::Justification::centredLeft);
    auto sysLabel = systemSel.getBounds().translated (0, -18).withHeight (16);
    g.drawText ("SYSTEM", sysLabel, juce::Justification::centredLeft);
}

void PeakTimeEditor::resized()
{
    auto r = getLocalBounds();
    auto top = r.removeFromTop (60).reduced (18, 14);
    help.setBounds (top.removeFromRight (32));
    top.removeFromRight (8);
    levelMatch.setBounds (top.removeFromRight (112));
    top.removeFromRight (8);
    headphones.setBounds (top.removeFromRight (112));
    top.removeFromRight (16);
    ab.setBounds (top.removeFromRight (240));

    r.reduce (14, 12);
    auto bottom = r.removeFromBottom (juce::jmax (220, getHeight() / 3 - 30));
    r.removeFromBottom (12);

    browser.setBounds (r.removeFromLeft (280));
    r.removeFromLeft (12);
    controlsArea = r.removeFromRight (250);
    r.removeFromRight (12);

    headerArea = r.removeFromTop (88).withTrimmedLeft (4);
    statsArea = r.removeFromBottom (28);
    r.removeFromBottom (8);
    floor.setBounds (r);

    auto c = controlsArea.reduced (14, 12);
    c.removeFromTop (22);
    auto knobRow1 = c.removeFromTop (108);
    distance.setBounds (knobRow1.removeFromLeft (knobRow1.getWidth() / 2));
    output.setBounds (knobRow1);
    c.removeFromTop (6);
    auto knobRow2 = c.removeFromTop (108);
    crowdLevel.setBounds (knobRow2.removeFromLeft (knobRow2.getWidth() / 2));
    crowdFill.setBounds (knobRow2);
    c.removeFromTop (24);
    systemSel.setBounds (c.removeFromTop (84));
    c.removeFromTop (10);
    meters.setBounds (c.removeFromTop (56));

    spectrum.setBounds (bottom.removeFromLeft (bottom.getWidth() * 46 / 100));
    bottom.removeFromLeft (12);
    player.setBounds (bottom);

    helpOverlay.setBounds (getLocalBounds());
}
