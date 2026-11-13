#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace theme
{
    const juce::Colour bg       { 0xff09090e };
    const juce::Colour panel    { 0xff12121a };
    const juce::Colour panel2   { 0xff1a1a25 };
    const juce::Colour line     { 0xff2a2a3b };
    const juce::Colour text     { 0xffececf4 };
    const juce::Colour dim      { 0xff8b8ba3 };
    const juce::Colour faint    { 0xff4a4a60 };
    const juce::Colour accent   { 0xffff2e88 }; // magenta  - "in venue"
    const juce::Colour accent2  { 0xff19e3ff }; // cyan     - "original" / listener
    const juce::Colour warn     { 0xffffb020 };
    const juce::Colour hot      { 0xffff4646 };
    const juce::Colour green    { 0xff38e08c };

    inline juce::Font font (float h, bool bold = false)
    {
        return juce::Font (juce::FontOptions (h, bold ? juce::Font::bold : juce::Font::plain));
    }
}

class PeakTimeLookAndFeel : public juce::LookAndFeel_V4
{
public:
    PeakTimeLookAndFeel()
    {
        setColour (juce::ResizableWindow::backgroundColourId, theme::bg);
        setColour (juce::Slider::textBoxTextColourId, theme::text);
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxHighlightColourId, theme::accent.withAlpha (0.4f));
        setColour (juce::Label::textColourId, theme::text);
        setColour (juce::TextEditor::backgroundColourId, theme::panel2);
        setColour (juce::TextEditor::textColourId, theme::text);
        setColour (juce::TextEditor::outlineColourId, theme::line);
        setColour (juce::TextEditor::focusedOutlineColourId, theme::accent);
        setColour (juce::TextEditor::highlightColourId, theme::accent.withAlpha (0.35f));
        setColour (juce::CaretComponent::caretColourId, theme::accent);
        setColour (juce::ListBox::backgroundColourId, theme::panel);
        setColour (juce::ListBox::outlineColourId, juce::Colours::transparentBlack);
        setColour (juce::ScrollBar::thumbColourId, theme::line);
        setColour (juce::TextButton::buttonColourId, theme::panel2);
        setColour (juce::TextButton::buttonOnColourId, theme::accent);
        setColour (juce::TextButton::textColourOffId, theme::dim);
        setColour (juce::TextButton::textColourOnId, juce::Colours::white);
        setColour (juce::PopupMenu::backgroundColourId, theme::panel2);
        setColour (juce::PopupMenu::textColourId, theme::text);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, theme::accent.withAlpha (0.5f));
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider& s) override
    {
        const auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (6.0f);
        const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
        const auto c = bounds.getCentre();
        const float angle = startAngle + pos * (endAngle - startAngle);
        const auto col = s.findColour (juce::Slider::rotarySliderFillColourId);

        juce::Path track;
        track.addCentredArc (c.x, c.y, radius - 3, radius - 3, 0.0f, startAngle, endAngle, true);
        g.setColour (theme::line);
        g.strokePath (track, juce::PathStrokeType (4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        juce::Path val;
        val.addCentredArc (c.x, c.y, radius - 3, radius - 3, 0.0f, startAngle, angle, true);
        g.setColour (col.withAlpha (0.25f));
        g.strokePath (val, juce::PathStrokeType (9.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (col);
        g.strokePath (val, juce::PathStrokeType (4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        const float knobR = radius - 11;
        g.setGradientFill (juce::ColourGradient (theme::panel2.brighter (0.15f), c.x, c.y - knobR, theme::panel, c.x, c.y + knobR, false));
        g.fillEllipse (c.x - knobR, c.y - knobR, knobR * 2, knobR * 2);
        g.setColour (theme::line);
        g.drawEllipse (c.x - knobR, c.y - knobR, knobR * 2, knobR * 2, 1.0f);

        juce::Path tick;
        tick.addRoundedRectangle (-1.5f, -knobR + 4, 3.0f, knobR * 0.45f, 1.5f);
        g.setColour (theme::text);
        g.fillPath (tick, juce::AffineTransform::rotation (angle).translated (c.x, c.y));
    }

    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down) override
    {
        auto r = b.getLocalBounds().toFloat().reduced (0.5f);
        const bool on = b.getToggleState();
        const auto onCol = b.findColour (juce::TextButton::buttonOnColourId);
        g.setColour (on ? onCol.withAlpha (0.22f) : theme::panel2.brighter (over ? 0.08f : 0.0f).darker (down ? 0.2f : 0.0f));
        g.fillRoundedRectangle (r, 6.0f);
        g.setColour (on ? onCol : theme::line);
        g.drawRoundedRectangle (r, 6.0f, 1.0f);
    }

    void drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool) override
    {
        g.setFont (theme::font (12.0f, true));
        g.setColour (b.getToggleState() ? theme::text : theme::dim);
        g.drawFittedText (b.getButtonText(), b.getLocalBounds().reduced (4, 0), juce::Justification::centred, 1);
    }

    juce::Font getTextButtonFont (juce::TextButton&, int) override { return theme::font (12.0f, true); }
    juce::Font getLabelFont (juce::Label&) override { return theme::font (13.0f); }
};
