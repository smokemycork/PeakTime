#include "Components.h"
#include "../PluginProcessor.h"

using namespace theme;

// =============================================================================== Segmented
Segmented::Segmented (juce::StringArray i, bool v) : items (std::move (i)), vertical (v) {}

void Segmented::attach (juce::RangedAudioParameter& p)
{
    attachment = std::make_unique<juce::ParameterAttachment> (p, [this] (float value) { setIndex (juce::roundToInt (value)); });
    onChange = [this] (int i) { attachment->setValueAsCompleteGesture ((float) i); };
    attachment->sendInitialUpdate();
}

void Segmented::setIndex (int i)
{
    i = juce::jlimit (0, juce::jmax (0, items.size() - 1), i);
    if (i != index) { index = i; repaint(); }
}

void Segmented::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (panel2);
    g.fillRoundedRectangle (r, 7.0f);
    g.setColour (line);
    g.drawRoundedRectangle (r, 7.0f, 1.0f);

    const int n = juce::jmax (1, items.size());
    for (int k = 0; k < n; ++k)
    {
        juce::Rectangle<float> seg = vertical
            ? juce::Rectangle<float> (r.getX(), r.getY() + r.getHeight() * k / n, r.getWidth(), r.getHeight() / n)
            : juce::Rectangle<float> (r.getX() + r.getWidth() * k / n, r.getY(), r.getWidth() / n, r.getHeight());
        const auto col = colours.contains (k) ? colours[k] : accent;
        if (k == index)
        {
            auto s = seg.reduced (3.0f);
            g.setColour (col.withAlpha (0.22f));
            g.fillRoundedRectangle (s, 5.0f);
            g.setColour (col);
            g.drawRoundedRectangle (s, 5.0f, 1.2f);
        }
        g.setColour (k == index ? text : dim);
        g.setFont (font (juce::jmin (13.0f, seg.getHeight() * 0.42f), true));
        g.drawFittedText (items[k], seg.toNearestInt().reduced (4, 0), juce::Justification::centred, 1);
    }
}

void Segmented::mouseDown (const juce::MouseEvent& e)
{
    const int n = juce::jmax (1, items.size());
    const float pos = vertical ? (float) e.y / (float) juce::jmax (1, getHeight()) : (float) e.x / (float) juce::jmax (1, getWidth());
    const int k = juce::jlimit (0, n - 1, (int) (pos * n));
    setIndex (k);
    if (onChange) onChange (k);
}

// =============================================================================== Knob
Knob::Knob (const juce::String& t, juce::Colour c) : title (t)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 90, 18);
    slider.setColour (juce::Slider::rotarySliderFillColourId, c);
    slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    addAndMakeVisible (slider);
}

void Knob::attach (juce::AudioProcessorValueTreeState& s, const juce::String& id)
{
    att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (s, id, slider);
}

void Knob::resized() { slider.setBounds (getLocalBounds().withTrimmedTop (16)); }

void Knob::paint (juce::Graphics& g)
{
    g.setColour (dim);
    g.setFont (font (11.0f, true));
    g.drawText (title.toUpperCase(), getLocalBounds().removeFromTop (16), juce::Justification::centred);
}

// =============================================================================== VenueBrowser
VenueBrowser::VenueBrowser()
{
    addAndMakeVisible (categories);
    categories.onChange = [this] (int) { refilter(); };

    search.setTextToShowWhenEmpty ("Search venues, cities...", faint);
    search.setFont (font (13.0f));
    search.onTextChange = [this] { refilter(); };
    addAndMakeVisible (search);

    list.setModel (this);
    list.setRowHeight (42);
    addAndMakeVisible (list);
    refilter();
}

void VenueBrowser::refilter()
{
    const auto q = search.getText().trim().toLowerCase();
    const int cat = categories.getIndex(); // 0 all, 1 club, 2 arena, 3 festival
    filtered.clear();
    for (int i = 0; i < pt::numVenues(); ++i)
    {
        const auto& v = pt::venues()[(size_t) i];
        if (cat > 0 && (int) v.category != cat - 1) continue;
        if (q.isNotEmpty())
        {
            const auto hay = (juce::String (v.name) + " " + v.city + " " + pt::categoryName (v.category)).toLowerCase();
            if (! hay.contains (q)) continue;
        }
        filtered.push_back (i);
    }
    list.updateContent();
    setSelectedVenue (selected);
    list.repaint();
}

void VenueBrowser::setSelectedVenue (int v)
{
    selected = v;
    const auto it = std::find (filtered.begin(), filtered.end(), v);
    const juce::ScopedValueSetter<bool> svs (suppress, true);
    if (it == filtered.end()) list.deselectAllRows();
    else
    {
        const int row = (int) std::distance (filtered.begin(), it);
        if (list.getSelectedRow() != row)
        {
            list.selectRow (row, false, true);
        }
    }
}

void VenueBrowser::selectedRowsChanged (int row)
{
    if (suppress || row < 0 || row >= (int) filtered.size()) return;
    const int v = filtered[(size_t) row];
    if (v == selected) return;
    selected = v;
    if (onVenueChosen) onVenueChosen (v);
}

void VenueBrowser::paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool isSelected)
{
    if (row < 0 || row >= (int) filtered.size()) return;
    const auto& v = pt::venues()[(size_t) filtered[(size_t) row]];
    auto r = juce::Rectangle<int> (0, 0, w, h).reduced (6, 2);
    if (isSelected)
    {
        g.setColour (accent.withAlpha (0.16f));
        g.fillRoundedRectangle (r.toFloat(), 6.0f);
        g.setColour (accent);
        g.fillRoundedRectangle (r.toFloat().removeFromLeft (3.0f), 1.5f);
    }
    auto t = r.reduced (12, 4);
    const juce::Colour tag = v.category == pt::Category::Club ? accent : v.category == pt::Category::Arena ? accent2 : green;
    auto tagR = t.removeFromRight (64).withSizeKeepingCentre (64, 16);
    g.setColour (tag.withAlpha (0.14f));
    g.fillRoundedRectangle (tagR.toFloat(), 8.0f);
    g.setColour (tag);
    g.setFont (font (9.5f, true));
    g.drawText (juce::String (pt::categoryName (v.category)).toUpperCase(), tagR, juce::Justification::centred);

    g.setColour (isSelected ? text : text.withAlpha (0.88f));
    g.setFont (font (13.5f, true));
    g.drawText (v.name, t.removeFromTop (t.getHeight() / 2 + 1), juce::Justification::bottomLeft, true);
    g.setColour (dim);
    g.setFont (font (11.5f));
    g.drawText (juce::String (v.city) + "  -  " + juce::String (v.capacity) + " cap", t, juce::Justification::topLeft, true);
}

void VenueBrowser::resized()
{
    auto r = getLocalBounds().reduced (10);
    r.removeFromTop (22);
    categories.setBounds (r.removeFromTop (30));
    r.removeFromTop (8);
    search.setBounds (r.removeFromTop (28));
    r.removeFromTop (8);
    list.setBounds (r);
}

void VenueBrowser::paint (juce::Graphics& g)
{
    g.setColour (panel);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 10.0f);
    g.setColour (dim);
    g.setFont (font (11.0f, true));
    g.drawText ("VENUES  (" + juce::String (pt::numVenues()) + ")", getLocalBounds().reduced (14, 10).removeFromTop (18), juce::Justification::centredLeft);
}

// =============================================================================== FloorPlan
FloorPlan::Geometry FloorPlan::geometry() const
{
    const auto& v = pt::venues()[(size_t) proc.getVenueIndex()];
    auto b = getLocalBounds().toFloat().reduced (18.0f, 30.0f).withTrimmedBottom (-12.0f);
    const float aspect = juce::jlimit (0.45f, 1.6f, v.widthM / v.lengthM);
    float h = b.getHeight(), w = h * aspect;
    if (w > b.getWidth()) { w = b.getWidth(); h = w / aspect; }
    Geometry g;
    g.room = juce::Rectangle<float> (w, h).withCentre (b.getCentre());
    g.pxPerM = g.room.getHeight() / v.lengthM;
    g.stageY = g.room.getY() + 1.5f * g.pxPerM;
    g.maxDist = pt::maxDistanceFor (v);
    return g;
}

void FloorPlan::paint (juce::Graphics& g)
{
    g.setColour (panel);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 10.0f);

    const auto& v = pt::venues()[(size_t) proc.getVenueIndex()];
    const auto geo = geometry();
    const auto room = geo.room;
    const float pxX = room.getWidth() / v.widthM;
    const float fill = proc.apvts.getRawParameterValue (ParamID::crowdFill)->load() / 100.0f;
    const float d = proc.getDistance();

    // floor
    if (v.outdoor)
    {
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff0f1c16), room.getCentreX(), room.getY(),
                                                 juce::Colour (0xff0b120f), room.getCentreX(), room.getBottom(), false));
        g.fillRoundedRectangle (room, 18.0f);
        juce::Path border;
        border.addRoundedRectangle (room, 18.0f);
        juce::Path dashed;
        const float dashes[] = { 5.0f, 6.0f };
        juce::PathStrokeType (1.0f).createDashedStroke (dashed, border, dashes, 2);
        g.setColour (green.withAlpha (0.35f));
        g.fillPath (dashed);
        if (v.category == pt::Category::Club) // courtyard walls on three sides
        {
            g.setColour (dim);
            juce::Path walls;
            walls.startNewSubPath (room.getX(), room.getY());
            walls.lineTo (room.getX(), room.getBottom());
            walls.lineTo (room.getRight(), room.getBottom());
            walls.lineTo (room.getRight(), room.getY());
            g.strokePath (walls, juce::PathStrokeType (2.0f));
        }
    }
    else
    {
        g.setColour (panel2);
        g.fillRect (room);
        g.setColour (juce::Colour (0xff3a3a52).interpolatedWith (dim, 1.0f - v.wallAbsorb));
        g.drawRect (room, 2.5f);
    }

    // distance grid
    const float step = v.lengthM <= 40 ? 5.0f : v.lengthM <= 130 ? 10.0f : 25.0f;
    g.setFont (font (10.0f));
    for (float m = step; 1.5f + m < v.lengthM - 0.5f; m += step)
    {
        const float y = room.getY() + (1.5f + m) * geo.pxPerM;
        g.setColour (line.withAlpha (0.7f));
        g.drawHorizontalLine ((int) y, room.getX() + 4, room.getRight() - 4);
        g.setColour (faint);
        g.drawText (juce::String ((int) m) + " m", juce::Rectangle<float> (room.getRight() + 4, y - 7, 44, 14), juce::Justification::centredLeft);
    }

    // crowd
    {
        pt::Rng rng (0xBADC0DEu + (uint32_t) proc.getVenueIndex());
        const int count = (int) (fill * 520.0f);
        const float top = geo.stageY + 6, bottom = room.getBottom() - 5;
        for (int i = 0; i < count; ++i)
        {
            const float x = room.getX() + 5 + rng.uni() * (room.getWidth() - 10);
            // crowds bunch up towards the stage
            const float u = rng.uni();
            const float y = top + (bottom - top) * (u * u * 0.6f + u * 0.4f);
            g.setColour (text.withAlpha (0.10f + 0.12f * rng.uni()));
            g.fillEllipse (x - 1.7f, y - 1.7f, 3.4f, 3.4f);
        }
    }

    // stage + rig
    {
        const auto stage = juce::Rectangle<float> (room.getX() + room.getWidth() * 0.2f, room.getY(), room.getWidth() * 0.6f, juce::jmax (8.0f, geo.stageY - room.getY()));
        g.setColour (accent.withAlpha (0.18f));
        g.fillRect (stage);
        g.setColour (accent.withAlpha (0.8f));
        g.drawLine (stage.getX(), stage.getBottom(), stage.getRight(), stage.getBottom(), 1.5f);
        g.setFont (font (9.5f, true));
        g.setColour (text.withAlpha (0.8f));
        g.drawText ("STAGE", stage, juce::Justification::centred);

        const float spread = juce::jlimit (1.5f, 12.0f, v.widthM * 0.3f) * pxX;
        for (int s : { -1, 1 })
        {
            const auto spk = juce::Rectangle<float> (12, 14).withCentre ({ room.getCentreX() + s * spread, stage.getBottom() - 2 });
            g.setColour (text);
            g.fillRoundedRectangle (spk, 2.0f);
            g.setColour (accent);
            g.drawRoundedRectangle (spk.expanded (2), 3.0f, 1.0f);
        }
    }

    // FOH
    {
        const float foh = juce::jlimit (8.0f, geo.maxDist, v.defaultDistanceM * 1.4f);
        const float y = room.getY() + (1.5f + foh) * geo.pxPerM;
        const auto r = juce::Rectangle<float> (26, 12).withCentre ({ room.getCentreX() + room.getWidth() * 0.22f, y });
        g.setColour (panel);
        g.fillRect (r);
        g.setColour (dim);
        g.drawRect (r, 1.0f);
        g.setFont (font (8.5f, true));
        g.drawText ("FOH", r, juce::Justification::centred);
    }

    // listener
    {
        const float y = room.getY() + (1.5f + d) * geo.pxPerM;
        const juce::Point<float> c (room.getCentreX(), y);
        for (int k = 3; k > 0; --k)
        {
            g.setColour (accent2.withAlpha (0.07f * (float) k));
            const float rr = 6.0f + 6.0f * (float) k;
            g.fillEllipse (c.x - rr, c.y - rr, rr * 2, rr * 2);
        }
        g.setColour (accent2);
        g.fillEllipse (c.x - 6, c.y - 6, 12, 12);
        g.setColour (bg);
        g.fillEllipse (c.x - 2.5f, c.y - 2.5f, 5, 5);

        const auto label = juce::String ("YOU  ") + (d < 10 ? juce::String (d, 1) : juce::String (juce::roundToInt (d))) + " m";
        auto lr = juce::Rectangle<float> (86, 20).withCentre ({ c.x - 64, c.y });
        if (lr.getX() < room.getX() - 40) lr.setX (c.x + 14);
        g.setColour (bg.withAlpha (0.85f));
        g.fillRoundedRectangle (lr, 10.0f);
        g.setColour (accent2);
        g.setFont (font (11.0f, true));
        g.drawText (label, lr, juce::Justification::centred);
    }

    g.setColour (faint);
    g.setFont (font (10.5f, true));
    g.drawText ("TOP VIEW  -  DRAG TO CHANGE WHERE YOU STAND", getLocalBounds().reduced (14, 8).removeFromTop (16), juce::Justification::centredLeft);
    g.drawText (juce::String ((int) v.lengthM) + " x " + juce::String ((int) v.widthM) + (v.outdoor ? " m  open air" : " x " + juce::String (v.heightM, 1) + " m"),
                getLocalBounds().reduced (14, 8).removeFromTop (16), juce::Justification::centredRight);
}

void FloorPlan::setDistanceFromY (float y)
{
    const auto geo = geometry();
    const float d = juce::jlimit (1.0f, geo.maxDist, (y - geo.stageY) / geo.pxPerM);
    if (auto* p = proc.apvts.getParameter (ParamID::distance))
        p->setValueNotifyingHost (p->convertTo0to1 (d));
}

void FloorPlan::mouseDown (const juce::MouseEvent& e)
{
    if (auto* p = proc.apvts.getParameter (ParamID::distance)) p->beginChangeGesture();
    dragging = true;
    setDistanceFromY ((float) e.y);
}

void FloorPlan::mouseDrag (const juce::MouseEvent& e) { if (dragging) setDistanceFromY ((float) e.y); }

void FloorPlan::mouseUp (const juce::MouseEvent&)
{
    if (dragging)
        if (auto* p = proc.apvts.getParameter (ParamID::distance)) p->endChangeGesture();
    dragging = false;
}

// =============================================================================== SpectrumView
SpectrumView::SpectrumView (PeakTimeProcessor& p) : proc (p) { startTimerHz (30); }

void SpectrumView::timerCallback()
{
    int got = 0, total = 0;
    while ((got = proc.analyser.pull (tmpDry.data(), tmpWet.data(), (int) tmpDry.size())) > 0)
    {
        total += got;
        for (int i = 0; i < got; ++i)
        {
            ringDry[(size_t) ringPos] = tmpDry[(size_t) i];
            ringWet[(size_t) ringPos] = tmpWet[(size_t) i];
            ringPos = (ringPos + 1) % fftSize;
        }
    }
    if (total == 0) return;

    const double sr = proc.getSampleRateSafe();
    const float binHz = (float) (sr / fftSize);
    const float edges[numBands + 1] = { 20.f, 60.f, 250.f, 2000.f, 8000.f, 20000.f };
    double bandDry[numBands] = {}, bandWet[numBands] = {};

    auto analyse = [&] (const std::vector<float>& ring, std::vector<float>& spec, double* bands)
    {
        for (int i = 0; i < fftSize; ++i) fftData[(size_t) i] = ring[(size_t) ((ringPos + i) % fftSize)];
        std::fill (fftData.begin() + fftSize, fftData.end(), 0.0f);
        window.multiplyWithWindowingTable (fftData.data(), (size_t) fftSize);
        fft.performFrequencyOnlyForwardTransform (fftData.data());
        for (int b = 1; b < numBins; ++b)
        {
            const float mag = fftData[(size_t) b] / (float) fftSize * 4.0f;
            const float db = juce::Decibels::gainToDecibels (mag, -120.0f);
            auto& s = spec[(size_t) b];
            s += (db > s ? 0.45f : 0.12f) * (db - s);
            const float f = (float) b * binHz;
            for (int k = 0; k < numBands; ++k)
                if (f >= edges[k] && f < edges[k + 1]) { bands[k] += (double) mag * mag; break; }
        }
    };
    analyse (ringDry, specDry, bandDry);
    analyse (ringWet, specWet, bandWet);

    for (int k = 0; k < numBands; ++k)
        if (bandDry[k] > 1e-12 && bandWet[k] > 1e-14)
        {
            const float delta = (float) (10.0 * std::log10 (bandWet[k] / bandDry[k]));
            bandDelta[k] += 0.15f * (juce::jlimit (-30.0f, 30.0f, delta) - bandDelta[k]);
        }
    repaint();
}

juce::Path SpectrumView::makePath (const std::vector<float>& spec, juce::Rectangle<float> a) const
{
    const double sr = proc.getSampleRateSafe();
    const float binHz = (float) (sr / fftSize);
    juce::Path p;
    const int points = juce::jmax (40, (int) a.getWidth() / 2);
    for (int i = 0; i <= points; ++i)
    {
        const float t0 = (float) i / points, t1 = (float) (i + 1) / points;
        const float f0 = 20.0f * std::pow (1000.0f, t0), f1 = 20.0f * std::pow (1000.0f, t1);
        int b0 = juce::jlimit (1, numBins - 1, (int) (f0 / binHz)), b1 = juce::jlimit (b0 + 1, numBins, (int) (f1 / binHz) + 1);
        float m = -120.0f;
        for (int b = b0; b < b1; ++b) m = juce::jmax (m, spec[(size_t) b]);
        const float tilt = 3.0f * std::log2 (juce::jmax (20.0f, f0) / 1000.0f); // pink-noise tilt so a balanced mix reads flat
        const float db = m + tilt;
        const float y = juce::jmap (juce::jlimit (-84.0f, 0.0f, db), -84.0f, 0.0f, a.getBottom(), a.getY());
        const float x = a.getX() + a.getWidth() * t0;
        if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
    }
    return p;
}

void SpectrumView::paint (juce::Graphics& g)
{
    g.setColour (panel);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 10.0f);
    auto r = getLocalBounds().toFloat().reduced (12.0f, 10.0f);
    auto header = r.removeFromTop (18);
    auto tiles = r.removeFromBottom (40);
    r.removeFromBottom (6);
    auto plot = r;

    g.setFont (font (11.0f, true));
    g.setColour (dim);
    g.drawText ("TRANSLATION", header, juce::Justification::centredLeft);
    auto legend = header.removeFromRight (190);
    g.setColour (accent2);
    g.drawText ("ORIGINAL", legend.removeFromLeft (85), juce::Justification::centredRight);
    g.setColour (accent);
    g.drawText ("IN VENUE", legend, juce::Justification::centredRight);

    // grid
    g.setColour (line.withAlpha (0.6f));
    for (float f : { 50.f, 100.f, 200.f, 500.f, 1000.f, 2000.f, 5000.f, 10000.f })
    {
        const float x = plot.getX() + plot.getWidth() * std::log (f / 20.0f) / std::log (1000.0f);
        g.drawVerticalLine ((int) x, plot.getY(), plot.getBottom());
        g.setColour (faint);
        g.setFont (font (9.0f));
        g.drawText (f >= 1000 ? juce::String ((int) (f / 1000)) + "k" : juce::String ((int) f), juce::Rectangle<float> (x + 2, plot.getBottom() - 12, 30, 12), juce::Justification::left);
        g.setColour (line.withAlpha (0.6f));
    }

    const auto dryPath = makePath (specDry, plot);
    const auto wetPath = makePath (specWet, plot);
    juce::Path wetFill (wetPath);
    wetFill.lineTo (plot.getRight(), plot.getBottom());
    wetFill.lineTo (plot.getX(), plot.getBottom());
    wetFill.closeSubPath();
    g.setGradientFill (juce::ColourGradient (accent.withAlpha (0.25f), 0, plot.getY(), accent.withAlpha (0.0f), 0, plot.getBottom(), false));
    g.fillPath (wetFill);
    g.setColour (accent2.withAlpha (0.8f));
    g.strokePath (dryPath, juce::PathStrokeType (1.2f));
    g.setColour (accent);
    g.strokePath (wetPath, juce::PathStrokeType (2.0f));

    // band tiles: what survives the trip to where you are standing
    const char* names[numBands] = { "SUB", "BASS", "MIDS", "HIGHS", "AIR" };
    const float tw = tiles.getWidth() / numBands;
    for (int k = 0; k < numBands; ++k)
    {
        auto t = juce::Rectangle<float> (tiles.getX() + tw * k, tiles.getY(), tw, tiles.getHeight()).reduced (3.0f, 0.0f);
        const float dlt = bandDelta[k];
        const juce::Colour c = dlt < -6.0f ? hot : dlt < -2.0f ? warn : dlt > 2.0f ? accent2 : green;
        g.setColour (c.withAlpha (0.12f));
        g.fillRoundedRectangle (t, 6.0f);
        g.setColour (dim);
        g.setFont (font (9.5f, true));
        g.drawText (names[k], t.removeFromTop (16).translated (0, 3), juce::Justification::centred);
        g.setColour (c);
        g.setFont (font (13.0f, true));
        g.drawText ((dlt >= 0 ? "+" : "") + juce::String (dlt, 1) + " dB", t, juce::Justification::centred);
    }
}

// =============================================================================== RigMeters
void RigMeters::tick()
{
    sub = juce::jmax (proc.diagSubGr.load(), sub * 0.85f);
    top = juce::jmax (proc.diagTopGr.load(), top * 0.85f);
    repaint();
}

void RigMeters::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    auto bar = [&] (juce::Rectangle<float> a, const juce::String& name, float grDb)
    {
        g.setColour (dim);
        g.setFont (font (10.0f, true));
        auto head = a.removeFromTop (14);
        g.drawText (name, head, juce::Justification::centredLeft);
        g.drawText (juce::String (grDb, 1) + " dB GR", head, juce::Justification::centredRight);
        auto b = a.removeFromTop (8);
        g.setColour (panel2);
        g.fillRoundedRectangle (b, 4.0f);
        const float t = juce::jlimit (0.0f, 1.0f, grDb / 12.0f);
        g.setColour (t > 0.66f ? hot : t > 0.33f ? warn : green);
        g.fillRoundedRectangle (b.withWidth (b.getWidth() * t), 4.0f);
    };
    bar (r.removeFromTop (r.getHeight() / 2).reduced (0, 2), "SUB LIMITER", sub);
    bar (r.reduced (0, 2), "RIG LIMITER", top);
}

// =============================================================================== PlayerStrip
PlayerStrip::SlotRow::SlotRow (PlayerStrip& o, int s) : owner (o), slot (s)
{
    addAndMakeVisible (load);
    addAndMakeVisible (clear);
    load.onClick = [this] { owner.choose (slot); };
    clear.onClick = [this] { owner.proc.player.clear (slot); repaint(); };
}

juce::Rectangle<int> PlayerStrip::SlotRow::waveArea() const
{
    return getLocalBounds().reduced (6).withTrimmedLeft (180).withTrimmedRight (92);
}

void PlayerStrip::SlotRow::resized()
{
    auto r = getLocalBounds().reduced (8);
    clear.setBounds (r.removeFromRight (26).withSizeKeepingCentre (26, 26));
    r.removeFromRight (6);
    load.setBounds (r.removeFromRight (52).withSizeKeepingCentre (52, 26));
}

void PlayerStrip::SlotRow::paint (juce::Graphics& g)
{
    auto& player = owner.proc.player;
    const auto data = player.get (slot);
    const bool active = owner.activeSlot() == slot;
    const juce::Colour col = slot == 0 ? accent : warn;

    auto r = getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (active ? col.withAlpha (0.08f) : panel2.withAlpha (0.6f));
    g.fillRoundedRectangle (r, 8.0f);
    g.setColour (dragOver ? col : active ? col.withAlpha (0.7f) : line);
    g.drawRoundedRectangle (r, 8.0f, dragOver ? 2.0f : 1.0f);

    // tag + info
    auto info = getLocalBounds().reduced (12, 8).removeFromLeft (166);
    g.setColour (active ? col : dim);
    g.fillEllipse ((float) info.getX(), (float) info.getY() + 4, 8, 8);
    g.setFont (font (11.0f, true));
    g.drawText (slot == 0 ? "MY TRACK" : "REFERENCE", info.removeFromTop (16).withTrimmedLeft (14), juce::Justification::centredLeft);
    g.setColour (text);
    g.setFont (font (12.5f, true));
    if (player.isLoading (slot))
        g.drawText ("Loading...", info.removeFromTop (18), juce::Justification::centredLeft, true);
    else if (data != nullptr)
    {
        g.drawText (data->name, info.removeFromTop (18), juce::Justification::centredLeft, true);
        g.setColour (dim);
        g.setFont (font (11.0f));
        juce::String l = juce::String (data->lufs, 1) + " LUFS";
        const float mg = player.getMatchGainDb (slot);
        if (std::abs (mg) > 0.05f) l << "  (" << (mg > 0 ? "+" : "") << juce::String (mg, 1) << " dB)";
        g.drawText (l, info.removeFromTop (16), juce::Justification::centredLeft, true);
    }
    else
    {
        const auto err = player.getError (slot);
        g.setColour (err.isNotEmpty() ? hot : faint);
        g.setFont (font (11.0f));
        g.drawText (err.isNotEmpty() ? err : juce::String ("Empty"), info.removeFromTop (18), juce::Justification::centredLeft, true);
    }

    // waveform
    auto w = waveArea().toFloat();
    g.setColour (bg.withAlpha (0.6f));
    g.fillRoundedRectangle (w, 6.0f);
    if (data != nullptr && ! data->peaks.empty())
    {
        const float pos = data->lengthSec > 0 ? (float) (std::fmod (player.getPositionSec(), data->lengthSec) / data->lengthSec) : 0.0f;
        const int np = (int) data->peaks.size();
        const float mid = w.getCentreY(), half = w.getHeight() * 0.45f;
        for (int x = 0; x < (int) w.getWidth(); ++x)
        {
            const float t = (float) x / w.getWidth();
            const float pk = data->peaks[(size_t) juce::jlimit (0, np - 1, (int) (t * np))];
            g.setColour (t <= pos && active ? col.withAlpha (0.95f) : (active ? col.withAlpha (0.35f) : dim.withAlpha (0.35f)));
            g.drawVerticalLine ((int) (w.getX() + x), mid - pk * half, mid + pk * half);
        }
        if (active)
        {
            g.setColour (text);
            g.drawVerticalLine ((int) (w.getX() + pos * w.getWidth()), w.getY(), w.getBottom());
        }
    }
    else
    {
        g.setColour (faint);
        g.setFont (font (11.0f));
        g.drawText ("Drop a WAV / AIFF / FLAC / MP3 here, or press LOAD", w, juce::Justification::centred, true);
    }
}

void PlayerStrip::SlotRow::seekFrom (int x)
{
    const auto data = owner.proc.player.get (slot);
    const auto w = waveArea();
    if (data == nullptr || w.getWidth() <= 0) return;
    const double t = juce::jlimit (0.0, 1.0, (double) (x - w.getX()) / w.getWidth());
    owner.proc.player.setPositionSec (t * data->lengthSec);
}

void PlayerStrip::SlotRow::mouseDown (const juce::MouseEvent& e)
{
    owner.select (slot);
    if (waveArea().contains (e.getPosition())) seekFrom (e.x);
}

void PlayerStrip::SlotRow::mouseDrag (const juce::MouseEvent& e)
{
    if (waveArea().contains (e.getMouseDownPosition())) seekFrom (e.x);
}

bool PlayerStrip::SlotRow::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files)
        if (owner.proc.player.canLoad (juce::File (f))) return true;
    return false;
}

void PlayerStrip::SlotRow::filesDropped (const juce::StringArray& files, int, int)
{
    dragOver = false;
    for (auto& f : files)
    {
        const juce::File file (f);
        if (owner.proc.player.canLoad (file)) { owner.loadInto (slot, file); break; }
    }
    repaint();
}

PlayerStrip::PlayerStrip (PeakTimeProcessor& p) : proc (p)
{
    for (auto* b : { &play, &loop, &match, &daw })
    {
        b->setClickingTogglesState (false);
        addAndMakeVisible (*b);
    }
    play.setColour (juce::TextButton::buttonOnColourId, green);
    loop.setColour (juce::TextButton::buttonOnColourId, accent2);
    match.setColour (juce::TextButton::buttonOnColourId, accent2);
    daw.setColour (juce::TextButton::buttonOnColourId, accent2);

    play.onClick = [this]
    {
        auto& pl = proc.player;
        if (! pl.isPlaying() && activeSlot() < 0)
            select (pl.get (0) != nullptr || pl.get (1) == nullptr ? 0 : 1);
        pl.setPlaying (! pl.isPlaying());
        refresh();
    };
    loop.onClick = [this] { proc.player.setLooping (! proc.player.isLooping()); refresh(); };
    match.onClick = [this] { proc.player.setMatchLoudness (! proc.player.getMatchLoudness()); refresh(); };
    daw.onClick = [this] { proc.setParam (ParamID::source, 0.0f); refresh(); };

    addAndMakeVisible (rowA);
    addAndMakeVisible (rowB);
}

int PlayerStrip::activeSlot() const
{
    const int s = juce::roundToInt (proc.apvts.getRawParameterValue (ParamID::source)->load());
    return s - 1;
}

void PlayerStrip::select (int slot) { proc.setParam (ParamID::source, (float) (slot + 1)); refresh(); }

void PlayerStrip::choose (int slot)
{
    chooser = std::make_unique<juce::FileChooser> (slot == 0 ? "Load your track" : "Load a reference track", juce::File(), proc.player.getWildcard());
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this, slot] (const juce::FileChooser& fc)
                          {
                              const auto f = fc.getResult();
                              if (f.existsAsFile()) loadInto (slot, f);
                          });
}

void PlayerStrip::loadInto (int slot, const juce::File& f)
{
    if (! proc.player.canLoad (f)) return;
    if (! proc.player.isPlaying()) proc.player.setPositionSec (0.0);
    proc.player.loadAsync (slot, f);
    select (slot);
    proc.player.setPlaying (true);
    refresh();
}

void PlayerStrip::refresh()
{
    play.setToggleState (proc.player.isPlaying(), juce::dontSendNotification);
    play.setButtonText (proc.player.isPlaying() ? "PAUSE" : "PLAY");
    loop.setToggleState (proc.player.isLooping(), juce::dontSendNotification);
    match.setToggleState (proc.player.getMatchLoudness(), juce::dontSendNotification);
    daw.setToggleState (activeSlot() < 0, juce::dontSendNotification);
    rowA.repaint();
    rowB.repaint();
    repaint (getLocalBounds().removeFromTop (44));
}

void PlayerStrip::resized()
{
    auto r = getLocalBounds().reduced (10);
    auto top = r.removeFromTop (30);
    top.removeFromLeft (110);
    daw.setBounds (top.removeFromLeft (92));
    top.removeFromLeft (6);
    play.setBounds (top.removeFromLeft (72));
    top.removeFromLeft (6);
    loop.setBounds (top.removeFromLeft (60));
    top.removeFromLeft (6);
    match.setBounds (top.removeFromLeft (96));
    r.removeFromTop (8);
    const int h = (r.getHeight() - 6) / 2;
    rowA.setBounds (r.removeFromTop (h));
    r.removeFromTop (6);
    rowB.setBounds (r);
}

void PlayerStrip::paint (juce::Graphics& g)
{
    g.setColour (panel);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 10.0f);
    auto r = getLocalBounds().reduced (14, 10).removeFromTop (30);
    g.setColour (dim);
    g.setFont (font (11.0f, true));
    g.drawText ("REFERENCE PLAYER", r, juce::Justification::centredLeft);

    const int s = activeSlot();
    if (s >= 0)
        if (auto d = proc.player.get (s))
        {
            auto fmt = [] (double sec) { const int t = (int) sec; return juce::String (t / 60) + ":" + juce::String (t % 60).paddedLeft ('0', 2); };
            g.setColour (text);
            g.setFont (font (12.0f, true));
            g.drawText (fmt (std::fmod (proc.player.getPositionSec(), juce::jmax (0.01, d->lengthSec))) + " / " + fmt (d->lengthSec), r, juce::Justification::centredRight);
        }
}

// =============================================================================== HelpOverlay
void HelpOverlay::paint (juce::Graphics& g)
{
    g.fillAll (bg.withAlpha (0.88f));
    auto r = getLocalBounds().reduced (juce::jmax (40, getWidth() / 8), 40).toFloat();
    g.setColour (panel);
    g.fillRoundedRectangle (r, 14.0f);
    g.setColour (accent);
    g.drawRoundedRectangle (r, 14.0f, 1.0f);

    auto t = r.reduced (32.0f, 26.0f);
    g.setColour (text);
    g.setFont (font (22.0f, true));
    g.drawText ("How to use PeakTime", t.removeFromTop (32), juce::Justification::centredLeft);
    t.removeFromTop (8);

    const char* sections[][2] = {
        { "1. Pick a venue",       "Every venue arrives set up: room size, surfaces, rig, crowd and a typical listening spot. Step through clubs, arenas and festivals to see whether your mix travels or only works in one kind of room." },
        { "2. Distance",           "Drag yourself on the floor plan or turn the knob. Up close: mostly the rig. Further back: later, louder reflections, less top end through the air and more crowd. Most of the audience is far from the stage - check there." },
        { "3. Crowd",              "Level is the crowd against the system, Fill is how many bodies are in the room. People absorb highs and damp reflections, so a full room is a different space, not the same space turned down." },
        { "4. System",             "Clean, Working or Red line. Busy nights run rigs hard: too much sub or an over-limited master behaves very differently once the limiters and amps are pushed. Watch the limiter meters." },
        { "5. Original / In venue","The A/B. Level match keeps both at the same loudness so you hear what disappears, what turns to mud and what survives - not just a volume change. The Translation panel shows it per band." },
        { "6. Headphones",         "Turn on when monitoring on headphones: adds the crossfeed of a real room, where both ears hear both stacks." },
        { "7. Reference player",   "Drop your bounce into MY TRACK and a pro track into REFERENCE. The reference is loudness-matched to yours, the playhead is shared, and both play through the same venue - so you compare like with like. Works in the standalone app too." },
    };
    for (auto& s : sections)
    {
        g.setColour (accent);
        g.setFont (font (13.0f, true));
        g.drawText (s[0], t.removeFromTop (18), juce::Justification::centredLeft);
        g.setColour (text.withAlpha (0.85f));
        g.setFont (font (12.5f));
        g.drawFittedText (s[1], t.removeFromTop (36).toNearestInt(), juce::Justification::topLeft, 3);
        t.removeFromTop (6);
    }
    g.setColour (faint);
    g.setFont (font (11.0f));
    g.drawText ("Venue models are acoustic estimates from public info, not official measurements. Click anywhere to close.", t.removeFromBottom (18), juce::Justification::centredLeft);
}
