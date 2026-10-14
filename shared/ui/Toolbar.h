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

        // A plugin-specific control placed left of BYPASS (e.g. a sidechain
        // source switch). The caller owns the component.
        void addExtra (juce::Component& c, int width);

        void paint (juce::Graphics&) override;
        void resized() override;

        // The preset menus, anchored to `target` (default: the toolbar's own
        // button). A skin with its own preset bar opens the same menus.
        void showPresetMenu (juce::Component* target = nullptr);
        void showSaveMenu (juce::Component* target = nullptr);
        // Deletes the current user preset after a confirmation (factory
        // presets cannot be deleted).
        void deleteCurrentPreset();

    private:
        void changeListenerCallback (juce::ChangeBroadcaster*) override;
        void timerCallback() override;
        void refresh();

        void showScaleMenu();
        void askForName (const juce::String& title, const juce::String& initial,
                         std::function<void (const juce::String&)> onOk);
        void showError (const juce::String& message);

        TleetProcessorBase& processor;

        juce::TextButton prevButton { "<" }, nextButton { ">" }, presetButton, saveButton { "SAVE" },
                         aButton { "A" }, bButton { "B" }, copyAbButton { "A>B" },
                         undoButton { "UNDO" }, redoButton { "REDO" },
                         bypassButton { "BYPASS" }, scaleButton { "100%" }, aboutButton { "i" };

        std::vector<std::pair<juce::Component*, int>> extras;
        std::unique_ptr<juce::ButtonParameterAttachment> bypassAttachment;
        std::unique_ptr<juce::AlertWindow> nameDialog;
    };
}
