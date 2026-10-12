#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "core/TleetProcessorBase.h"

namespace tleet::ui
{
    // Common utility strip: presets, A/B, undo/redo, bypass, UI scale, About.
    class Toolbar : public juce::Component,
                    private juce::ChangeListener,
                    private juce::Timer
    {
    public:
        explicit Toolbar (TleetProcessorBase& processor);
        ~Toolbar() override;

        std::function<void (float)> onScaleChosen;
        std::function<void()> onAbout;

        void paint (juce::Graphics&) override;
        void resized() override;

    private:
        void changeListenerCallback (juce::ChangeBroadcaster*) override;
        void timerCallback() override;
        void refresh();

        void showPresetMenu();
        void showSaveMenu();
        void showScaleMenu();
        void askForName (const juce::String& title, const juce::String& initial,
                         std::function<void (const juce::String&)> onOk);
        void showError (const juce::String& message);

        TleetProcessorBase& processor;

        juce::TextButton prevButton { "<" }, nextButton { ">" }, presetButton, saveButton { "SAVE" },
                         aButton { "A" }, bButton { "B" }, copyAbButton { "A>B" },
                         undoButton { "UNDO" }, redoButton { "REDO" },
                         bypassButton { "BYPASS" }, scaleButton { "100%" }, aboutButton { "i" };

        std::unique_ptr<juce::ButtonParameterAttachment> bypassAttachment;
        std::unique_ptr<juce::AlertWindow> nameDialog;
    };
}
