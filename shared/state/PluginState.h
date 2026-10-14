#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <map>

namespace tleet
{
    // Plain-unit values of every preset-relevant parameter, keyed by the
    // immutable parameter ID. Used for presets, A/B slots, undo history and
    // copy/paste so that all of them share one format.
    struct ParamSnapshot
    {
        std::map<juce::String, float> values;

        bool isEmpty() const noexcept { return values.empty(); }
        bool approximatelyEquals (const ParamSnapshot& other, float tolerance = 1.0e-4f) const;

        juce::var toVar() const;
        static ParamSnapshot fromVar (const juce::var& v);
    };

    namespace state
    {
        // Parameters that never travel with presets, A/B or undo (e.g. bypass).
        bool isExcludedFromSnapshots (const juce::String& paramId);

        ParamSnapshot capture (juce::AudioProcessorValueTreeState& apvts);

        // Applies values with host notification and gestures, on the message thread.
        // Parameters missing from the snapshot are reset to their defaults when
        // `resetMissingToDefault` is set (used when loading presets).
        void apply (juce::AudioProcessorValueTreeState& apvts, const ParamSnapshot& snapshot,
                    bool resetMissingToDefault);

        ParamSnapshot defaults (juce::AudioProcessorValueTreeState& apvts);
    }
}
