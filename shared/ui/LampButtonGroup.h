#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>

namespace tleet::ui
{
    // Row of skinned push buttons that select one state of a choice
    // parameter (radio behaviour, like the interlocked buttons of the
    // hardware). The faceplate shows every button released with a dark lamp;
    // the selected one is drawn pressed and lit from a sprite of the artwork.
    class LampButtonGroup : public juce::Component,
                            public juce::SettableTooltipClient
    {
    public:
        struct Geometry
        {
            std::vector<juce::Rectangle<float>> hitboxes; // one per state, component coordinates
            std::vector<juce::Rectangle<float>> litBounds; // where the lit sprite goes for each state
            juce::Image lit;                               // pressed button with its lamp on
            std::vector<juce::Image> litPerState;          // optional: one sprite per state (label baked in)
        };

        LampButtonGroup (juce::RangedAudioParameter& param, juce::String label, juce::StringArray states, Geometry g);

        std::function<void()> onEditFinished;

        void paint (juce::Graphics&) override;
        bool hitTest (int x, int y) override;
        void mouseDown (const juce::MouseEvent&) override;
        bool keyPressed (const juce::KeyPress&) override;
        void focusGained (FocusChangeType) override { repaint(); }
        void focusLost (FocusChangeType) override   { repaint(); }
        std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

        int getSelectedIndex() const noexcept { return selected; }
        int indexAt (juce::Point<float> p) const noexcept;
        void select (int index);
        const juce::StringArray& getStates() const noexcept { return states; }
        const Geometry& getGeometry() const noexcept { return geo; }

    private:
        juce::String label;
        juce::StringArray states;
        Geometry geo;
        juce::ParameterAttachment attachment;
        int selected = 0;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LampButtonGroup)
    };
}
