#include "TleetLookAndFeel.h"

namespace tleet::ui
{
    TleetLookAndFeel::TleetLookAndFeel()
    {
        setColour (juce::PopupMenu::backgroundColourId, colours::panel);
        setColour (juce::PopupMenu::textColourId, colours::text);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, colours::accentDim);
        setColour (juce::PopupMenu::highlightedTextColourId, colours::text);
        setColour (juce::PopupMenu::headerTextColourId, colours::accent);
        setColour (juce::TextButton::buttonColourId, colours::panelLight);
        setColour (juce::TextButton::buttonOnColourId, colours::accentDim);
        setColour (juce::TextButton::textColourOffId, colours::text);
        setColour (juce::TextButton::textColourOnId, colours::accent);
        setColour (juce::TooltipWindow::backgroundColourId, colours::panel);
        setColour (juce::TooltipWindow::textColourId, colours::text);
        setColour (juce::TooltipWindow::outlineColourId, colours::edge);
        setColour (juce::AlertWindow::backgroundColourId, colours::panel);
        setColour (juce::AlertWindow::textColourId, colours::text);
        setColour (juce::AlertWindow::outlineColourId, colours::edge);
        setColour (juce::TextEditor::backgroundColourId, colours::background);
        setColour (juce::TextEditor::textColourId, colours::text);
        setColour (juce::TextEditor::outlineColourId, colours::edge);
        setColour (juce::TextEditor::focusedOutlineColourId, colours::accent);
        setColour (juce::Label::textColourId, colours::text);
    }

    juce::Font TleetLookAndFeel::labelFont (float height, bool bold)
    {
        return juce::Font (juce::FontOptions (height, bold ? juce::Font::bold : juce::Font::plain))
                   .withExtraKerningFactor (0.08f);
    }

    void TleetLookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int width, int height)
    {
        g.fillAll (colours::panel);
        g.setColour (colours::edge);
        g.drawRect (0, 0, width, height);
    }

    void TleetLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&,
                                                 bool highlighted, bool down)
    {
        auto r = b.getLocalBounds().toFloat().reduced (0.5f);
        auto base = b.getToggleState() ? colours::accentDim : colours::panelLight;
        if (highlighted) base = base.brighter (0.12f);
        if (down)        base = base.darker (0.2f);
        if (! b.isEnabled()) base = base.withAlpha (0.4f);

        g.setColour (base);
        g.fillRoundedRectangle (r, 3.0f);
        g.setColour (b.getToggleState() ? colours::accent : colours::edge);
        g.drawRoundedRectangle (r, 3.0f, 1.0f);
    }

    juce::Font TleetLookAndFeel::getTextButtonFont (juce::TextButton&, int buttonHeight)
    {
        return labelFont (juce::jmin (13.0f, (float) buttonHeight * 0.55f));
    }

    juce::Font TleetLookAndFeel::getPopupMenuFont()
    {
        return labelFont (14.0f, false);
    }
}
