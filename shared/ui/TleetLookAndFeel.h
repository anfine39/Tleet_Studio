#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace tleet::ui
{
    // Interim vector visual language until the reference skins are prepared.
    // Every colour lives here so the skin pass only touches one file.
    namespace colours
    {
        inline const juce::Colour background   { 0xff0d0d0d };
        inline const juce::Colour panel        { 0xff181818 };
        inline const juce::Colour panelLight   { 0xff262626 };
        inline const juce::Colour edge         { 0xff353535 };
        inline const juce::Colour text         { 0xffd4d0ca };
        inline const juce::Colour textDim      { 0xff808080 };
        inline const juce::Colour accent       { 0xfff21e25 };
        inline const juce::Colour accentDim    { 0xff730f12 };
        inline const juce::Colour meterFace    { 0xffd8d3cd };
        inline const juce::Colour meterInk     { 0xff171717 };
        inline const juce::Colour warning      { 0xffff3a2e };
    }

    // "РИЛАЙ" as a juce::String, and the emblem from the logo (RGBA).
    juce::String brandName();
    juce::Image brandEmblem();
    juce::Image brandWordmark();

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
