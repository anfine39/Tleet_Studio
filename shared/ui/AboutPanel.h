#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace tleet::ui
{
    // Overlay with product info, licences and the donation placeholders.
    // Buttons with an empty URL are shown disabled; nothing touches the
    // network until the user clicks an enabled link.
    class AboutPanel : public juce::Component
    {
    public:
        AboutPanel (juce::String pluginName, juce::String displayName);

        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseUp (const juce::MouseEvent&) override;

    private:
        juce::String pluginName, displayName;
        juce::TextButton vkButton { "VK Donut" }, boostyButton { "Boosty" },
                         sourceButton { "Source code" }, closeButton { "Close" };
        juce::Rectangle<int> card;
    };
}
