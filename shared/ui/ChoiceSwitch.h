#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace tleet::ui
{
    // Multi-position switch for choice/bool parameters: one lit segment per
    // option. Click a segment to select it; arrow keys step when focused.
    class ChoiceSwitch : public juce::Component,
                         public juce::SettableTooltipClient
    {
    public:
        ChoiceSwitch (juce::RangedAudioParameter& param, juce::String label, juce::StringArray optionLabels);

        // UI-only switch (display preferences such as meter source): not a
        // host parameter, state is owned by the caller through onSelect.
        ChoiceSwitch (juce::String label, juce::StringArray optionLabels, int initialIndex,
                      std::function<void (int)> onSelect);

        std::function<void()> onEditFinished;

        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        bool keyPressed (const juce::KeyPress&) override;
        std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

        int getSelectedIndex() const noexcept { return selected; }
        void select (int index);
        const juce::StringArray& getOptions() const noexcept { return options; }

    private:
        juce::Rectangle<int> segmentBounds (int index) const;

        void showSelection (int index);

        juce::String label;
        juce::StringArray options;
        std::unique_ptr<juce::ParameterAttachment> attachment;
        std::function<void (int)> onSelect;
        int selected = 0;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChoiceSwitch)
    };
}
