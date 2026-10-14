#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace tleet::ui
{
    // Two-position toggle switch drawn from a lever sprite cut from the skin.
    // The lever angle is derived only from the parameter value; clicking the
    // left half selects state 0, the right half state 1.
    class LeverSwitch : public juce::Component,
                        public juce::SettableTooltipClient
    {
    public:
        struct Geometry
        {
            juce::Image lever;               // sprite, pointing straight down
            juce::Point<float> spritePivot;  // pivot inside the sprite (sprite pixels)
            juce::Point<float> pivot;        // pivot in component coordinates
            float spriteScale = 1.0f;        // sprite pixels -> component units
            float angleState0 = 0.9f;        // radians, positive = clockwise
            float angleState1 = -0.9f;
        };

        LeverSwitch (juce::RangedAudioParameter& param, juce::String label, juce::StringArray states, Geometry g);

        std::function<void()> onEditFinished;

        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        bool keyPressed (const juce::KeyPress&) override;
        std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

        int getState() const noexcept { return state; }
        void setState (int s);
        const juce::StringArray& getStates() const noexcept { return states; }

    private:
        juce::String label;
        juce::StringArray states;
        Geometry geo;
        juce::ParameterAttachment attachment;
        int state = 0;
        float shownAngle = 0.0f;
    };
}
