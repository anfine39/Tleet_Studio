#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace tleet::ui
{
    // Interim vector visual language until the reference skins are prepared.
    // Every colour lives here so the skin pass only touches one file.
    namespace colours
    {
        inline const juce::Colour background   { 0xff0f0f10 };
        inline const juce::Colour panel        { 0xff1b1b1d };
        inline const juce::Colour panelLight   { 0xff2a2a2d };
        inline const juce::Colour edge         { 0xff3a3a3e };
        inline const juce::Colour text         { 0xffddd6c8 };
        inline const juce::Colour textDim      { 0xff8b857b };
        inline const juce::Colour accent       { 0xffff7a1a };
        inline const juce::Colour accentDim    { 0xff7a3a0c };
        inline const juce::Colour meterFace    { 0xfff1d9a0 };
        inline const juce::Colour meterInk     { 0xff201a12 };
        inline const juce::Colour warning      { 0xffe0402a };
    }

    class TleetLookAndFeel : public juce::LookAndFeel_V4
    {
    public:
        TleetLookAndFeel();

        static juce::Font labelFont (float height, bool bold = true);

        void drawPopupMenuBackground (juce::Graphics&, int width, int height) override;
        void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&,
                                   bool highlighted, bool down) override;
        juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
        juce::Font getPopupMenuFont() override;
    };
}
