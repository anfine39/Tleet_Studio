#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

// Central parameter registry for ASH. IDs are immutable after the first
// public release: rename display names freely, never the IDs or versions.
namespace tleet::ash::params
{
    inline constexpr const char* peakReduction = "peakReduction";
    inline constexpr const char* gain          = "gain";
    inline constexpr const char* mode          = "mode";
    inline constexpr const char* mix           = "mix";
    inline constexpr const char* character     = "character";
    inline constexpr const char* output        = "output";
    inline constexpr const char* bypass        = "bypass";

    inline constexpr int version = 1;

    // Gain knob 0..100 -> makeup in dB; 50 = unity.  [APPROXIMATION of the hardware taper]
    inline float gainKnobToDb (float knob) noexcept { return (knob - 50.0f) * 0.8f; }

    inline juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
    {
        using namespace juce;
        AudioProcessorValueTreeState::ParameterLayout layout;

        auto pct = [] (float v, int) { return String (v, 1); };

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { peakReduction, version }, "Peak Reduction", NormalisableRange<float> (0.0f, 100.0f, 0.1f), 40.0f,
            AudioParameterFloatAttributes().withStringFromValueFunction (pct)));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { gain, version }, "Gain", NormalisableRange<float> (0.0f, 100.0f, 0.1f), 50.0f,
            AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
            {
                const float db = gainKnobToDb (v);
                return String (v, 1) + " (" + (db >= 0 ? "+" : "") + String (db, 1) + " dB)";
            })));

        layout.add (std::make_unique<AudioParameterChoice> (
            ParameterID { mode, version }, "Mode", StringArray { "Compress", "Limit" }, 0));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { mix, version }, "Mix", NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f,
            AudioParameterFloatAttributes().withLabel ("%").withStringFromValueFunction (pct)));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { character, version }, "Character", NormalisableRange<float> (0.0f, 100.0f, 0.1f), 50.0f,
            AudioParameterFloatAttributes().withLabel ("%").withStringFromValueFunction (pct)));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { output, version }, "Output", NormalisableRange<float> (-20.0f, 20.0f, 0.1f), 0.0f,
            AudioParameterFloatAttributes().withLabel ("dB").withStringFromValueFunction ([] (float v, int)
            {
                return (v > 0 ? "+" : "") + String (v, 1);
            })));

        layout.add (std::make_unique<AudioParameterBool> (ParameterID { bypass, version }, "Bypass", false));

        return layout;
    }
}
