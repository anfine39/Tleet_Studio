#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace tleet::ui
{
    // Skinned illuminated push button for a bool parameter. The faceplate
    // shows it released; when on, the lit sprite (label baked in) is drawn.
    class LampToggle : public juce::Component,
                       public juce::SettableTooltipClient
    {
    public:
        // litBounds: where the sprite (with its glow) is drawn; hitArea: the
        // clickable button. Both in component coordinates.
        LampToggle (juce::RangedAudioParameter& param, juce::String label, juce::Image litSprite,
                    juce::Rectangle<float> litBounds, juce::Rectangle<float> hitArea);

        std::function<void()> onEditFinished;

        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        bool hitTest (int x, int y) override { return hitArea.contains ((float) x, (float) y); }
        juce::Rectangle<float> getHitArea() const noexcept { return hitArea; }
        bool keyPressed (const juce::KeyPress&) override;
        void focusGained (FocusChangeType) override { repaint(); }
        void focusLost (FocusChangeType) override   { repaint(); }
        std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

        bool isOn() const noexcept { return on; }
        void setOn (bool shouldBeOn);

    private:
        juce::String label;
        juce::Image lit;
        juce::Rectangle<float> litBounds, hitArea;
        juce::ParameterAttachment attachment;
        bool on = false;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LampToggle)
    };
}
