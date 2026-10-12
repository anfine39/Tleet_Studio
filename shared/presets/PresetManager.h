#pragma once

#include "state/PluginState.h"
#include <vector>

namespace tleet
{
    // Preset storage. Factory presets are compiled into the binary as JSON;
    // user presets are human-readable JSON files in
    //   <user app data>/TLEET Audio/<Plugin>/Presets/*.tleetpreset
    // A preset carries its plugin identifier and a format version; a preset
    // for another plugin or a newer format is rejected with a message.
    // Message thread only (file I/O never runs on the audio thread).
    class PresetManager : public juce::ChangeBroadcaster
    {
    public:
        struct Preset
        {
            juce::String name;
            bool isFactory = false;
            juce::File file;
            ParamSnapshot params;
            juce::var pluginSpecific;
        };

        PresetManager (juce::String pluginId, juce::File userPresetDirectory);

        static juce::File defaultUserDirectory (const juce::String& pluginId);
        static constexpr const char* fileExtension = ".tleetpreset";

        juce::Result addFactoryPreset (const juce::String& json);
        void rescanUserPresets();

        const std::vector<Preset>& getPresets() const noexcept { return presets; }
        int indexOf (const juce::String& name, bool factory) const;

        // Bookkeeping of what is loaded, used for the "modified" marker.
        void setCurrent (int index);
        void setCurrentToInit (const ParamSnapshot& defaults);
        void setCurrentByName (const juce::String& name, bool isFactory, const ParamSnapshot& reference);
        int getCurrentIndex() const noexcept { return currentIndex; }
        juce::String getCurrentName() const;
        bool isCurrentFactory() const noexcept { return currentIsFactory; }
        bool isModified (const ParamSnapshot& now) const;

        juce::Result saveUserPreset (const juce::String& name, const ParamSnapshot& params,
                                     const juce::var& pluginSpecific);
        juce::Result renameUserPreset (int index, const juce::String& newName);
        juce::Result deleteUserPreset (int index);

        juce::String toJson (const juce::String& name, const ParamSnapshot& params,
                             const juce::var& pluginSpecific) const;
        juce::Result parse (const juce::String& json, Preset& out) const;

        const juce::String& getPluginId() const noexcept { return pluginId; }
        const juce::File& getUserDirectory() const noexcept { return userDir; }

    private:
        void sortPresets();

        juce::String pluginId;
        juce::File userDir;
        std::vector<Preset> presets;

        int currentIndex = -1;
        juce::String currentName { "Init" };
        bool currentIsFactory = true;
        ParamSnapshot reference;
    };
}
