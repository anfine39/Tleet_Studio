#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "state/PluginState.h"
#include "state/StateHistory.h"
#include "presets/PresetManager.h"
#include "dsp/MeterSource.h"

namespace tleet
{
    // Common plugin shell: parameter tree, saved-state format with schema
    // version, presets, A/B, undo/redo, copy/paste and UI preferences.
    // Plugins derive from this and only add DSP and their editor.
    //
    // Saved state layout (XML of this ValueTree):
    //   TLEET_STATE { version, plugin }
    //     PARAMS          (the APVTS tree)
    //     PRESET { name, factory }
    //     AB { active, A = json, B = json }
    //     UI { scale, ... plugin-defined display preferences }
    //     PLUGIN_SPECIFIC { json }
    class TleetProcessorBase : public juce::AudioProcessor
    {
    public:
        TleetProcessorBase (const BusesProperties& buses,
                            juce::AudioProcessorValueTreeState::ParameterLayout layout,
                            const juce::String& pluginId);
        ~TleetProcessorBase() override;

        // ---- juce::AudioProcessor boilerplate shared by all plugins
        const juce::String getName() const override { return "TLEET " + pluginId; }
        bool acceptsMidi() const override  { return false; }
        bool producesMidi() const override { return false; }
        bool isMidiEffect() const override { return false; }
        int getNumPrograms() override { return 1; }
        int getCurrentProgram() override { return 0; }
        void setCurrentProgram (int) override {}
        const juce::String getProgramName (int) override { return {}; }
        void changeProgramName (int, const juce::String&) override {}
        bool hasEditor() const override { return true; }
        juce::AudioProcessorParameter* getBypassParameter() const override;

        void getStateInformation (juce::MemoryBlock& dest) override;
        void setStateInformation (const void* data, int size) override;

        // ---- message-thread API used by the editor
        juce::AudioProcessorValueTreeState apvts;
        PresetManager presets;

        const juce::String& getPluginId() const noexcept { return pluginId; }

        void loadPreset (int index);
        void loadInitPreset();
        void stepPreset (int delta);
        juce::Result saveUserPreset (const juce::String& name);
        bool isPresetModified();

        void commitHistory();      // call after a user edit finished (gesture end)
        void undo();
        void redo();
        bool canUndo() const noexcept { return history.canUndo(); }
        bool canRedo() const noexcept { return history.canRedo(); }

        int getActiveSlot() const noexcept { return activeSlot; }
        void switchToSlot (int slot);  // 0 = A, 1 = B
        void copyActiveSlotToOther();

        juce::String stateToClipboardJson();
        juce::Result pasteStateFromJson (const juce::String& json);

        // UI preferences (scale, meter modes...). Saved with the project.
        juce::var getUiProperty (const juce::Identifier& id, const juce::var& fallback) const;
        void setUiProperty (const juce::Identifier& id, const juce::var& value);

        // Broadcast after preset/AB/undo/state changes so the toolbar can refresh.
        juce::ChangeBroadcaster stateChanged;

        // Meter hand-off (audio thread writes, editor reads).
        dsp::MeterSource inputMeter, outputMeter;

    protected:
        // Optional non-parameter state that must travel with presets/projects.
        virtual juce::var getPluginSpecificState() const { return {}; }
        virtual void setPluginSpecificState (const juce::var&) {}

        // Called after a state of an older schema version was read.
        virtual void migrateState (juce::ValueTree& stateTree, int fromVersion);

        // Factory presets are registered by the derived constructor.
        void addFactoryPresets (const juce::StringArray& jsonPresets);

    private:
        void applySnapshot (const ParamSnapshot& s);
        void restoreFromTree (juce::ValueTree tree);

        juce::String pluginId;
        StateHistory history;
        ParamSnapshot slots[2];
        int activeSlot = 0;
        juce::ValueTree uiState { "UI" };
        mutable juce::CriticalSection stateLock; // guards non-parameter state for host threads

        JUCE_DECLARE_WEAK_REFERENCEABLE (TleetProcessorBase)
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TleetProcessorBase)
    };
}
