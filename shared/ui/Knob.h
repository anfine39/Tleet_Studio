#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace tleet::ui
{
    // Rotary control bound to one parameter.
    //  - vertical drag, Shift = fine, mouse wheel
    //  - double-click knob, Alt-click or Ctrl/Cmd-click = default value
    //  - double-click the value text = type a value
    //  - right-click = reset / copy / paste / enter value
    //  - arrow keys when focused, tooltip with exact value and unit
    //  - host automation moves the knob through the ParameterAttachment
    class Knob : public juce::Component,
                 public juce::SettableTooltipClient
    {
    public:
        enum class Size { large, medium, small };

        Knob (juce::RangedAudioParameter& param, juce::String label, Size size = Size::medium);
        ~Knob() override;

        // Called after a user edit finished (used for undo history).
        std::function<void()> onEditFinished;

        // Skinned knobs: the faceplate already shows the knob body, so only
        // the moving pointer is drawn, at `centre` (component coordinates)
        // between the given angles (radians, 0 = 12 o'clock, clockwise).
        struct SkinPointer
        {
            juce::Point<float> centre;
            float innerRadius = 0, outerRadius = 0, width = 4;
            float startAngle = 0, endAngle = 0;
        };
        void setSkinPointer (SkinPointer p) { skin = p; hasSkin = true; resized(); repaint(); }

        void mouseEnter (const juce::MouseEvent&) override { hovering = true; repaint(); }
        void mouseExit (const juce::MouseEvent&) override  { hovering = false; repaint(); }

        void paint (juce::Graphics&) override;
        void resized() override;

        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseDoubleClick (const juce::MouseEvent&) override;
        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
        bool keyPressed (const juce::KeyPress&) override;
        void focusGained (FocusChangeType) override { repaint(); }
        void focusLost (FocusChangeType) override   { repaint(); }

        std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

        juce::RangedAudioParameter& getParameter() noexcept { return param; }
        float getNormalisedValue() const noexcept { return normalised; }
        void setNormalisedFromUser (float newNormalised, bool singleShotGesture);

    private:
        void valueChangedFromParameter (float plainValue);
        void resetToDefault();
        void showContextMenu();
        void showValueEntry();
        juce::String valueText() const;

        juce::RangedAudioParameter& param;
        juce::String label;
        Size size;
        juce::ParameterAttachment attachment;
        std::unique_ptr<juce::TextEditor> valueEditor;

        float normalised = 0.0f;
        float dragStartValue = 0.0f;
        int dragStartY = 0;
        bool dragging = false, hovering = false, hasSkin = false;
        SkinPointer skin;
        void paintSkin (juce::Graphics&);
        juce::Rectangle<int> knobArea, valueArea, labelArea;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Knob)
    };
}
