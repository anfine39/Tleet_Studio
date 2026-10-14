#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

// Central parameter registry for BITE. IDs are immutable after the first
// public release: rename display names freely, never the IDs or versions.
namespace tleet::bite::params
{
    inline constexpr const char* input     = "input";
    inline constexpr const char* output    = "output";
    inline constexpr const char* attack    = "attack";
    inline constexpr const char* release   = "release";
    inline constexpr const char* ratio     = "ratio";
    inline constexpr const char* mix       = "mix";
    inline constexpr const char* character = "character";
    inline constexpr const char* bypass    = "bypass";

    inline constexpr int version = 1;

    // Hardware-reference time ranges (master spec 13.2).
    inline constexpr float attackFastestUs   = 20.0f,  attackSlowestUs   = 800.0f;
    inline constexpr float releaseFastestMs  = 50.0f,  releaseSlowestMs  = 1100.0f;

    enum class Ratio { r4 = 0, r8, r12, r20, all };
    enum class Character { clean = 0, tleet, burn };

    inline const juce::StringArray ratioNames { "4", "8", "12", "20", "ALL" };
    inline const juce::StringArray characterNames { "Clean", "TLEET", "Burn" };

    // Like the hardware, turning clockwise makes attack and release FASTER.
    // The plain value is the time; the normalised value (knob position, host
    // automation lane) runs from slowest (0) to fastest (1) on a log scale.
    inline juce::NormalisableRange<float> reversedLogRange (float fastest, float slowest)
    {
        juce::NormalisableRange<float> r (
            fastest, slowest,
            [] (float lo, float hi, float n) { return hi * std::pow (lo / hi, n); },
            [] (float lo, float hi, float v) { return std::log (v / hi) / std::log (lo / hi); },
            [] (float lo, float hi, float v) { return juce::jlimit (lo, hi, v); });
        return r;
    }

    inline juce::String formatMicroseconds (float us)
    {
        return us < 1000.0f ? juce::String (juce::roundToInt (us)) + " us"
                            : juce::String (us * 0.001f, 2) + " ms";
    }

    inline juce::String formatMilliseconds (float ms)
    {
        return ms < 1000.0f ? juce::String (juce::roundToInt (ms)) + " ms"
                            : juce::String (ms * 0.001f, 2) + " s";
    }

    inline juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
    {
        using namespace juce;
        AudioProcessorValueTreeState::ParameterLayout layout;

        auto db = [] (float v, int) { return (v > 0 ? "+" : "") + String (v, 1) + " dB"; };

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { input, version }, "Input", NormalisableRange<float> (-24.0f, 24.0f, 0.1f), 0.0f,
            AudioParameterFloatAttributes().withLabel ("dB").withStringFromValueFunction (db)));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { output, version }, "Output", NormalisableRange<float> (-24.0f, 24.0f, 0.1f), 0.0f,
            AudioParameterFloatAttributes().withLabel ("dB").withStringFromValueFunction (db)));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { attack, version }, "Attack", reversedLogRange (attackFastestUs, attackSlowestUs), 250.0f,
            AudioParameterFloatAttributes().withLabel ("us")
                .withStringFromValueFunction ([] (float v, int) { return formatMicroseconds (v); })));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { release, version }, "Release", reversedLogRange (releaseFastestMs, releaseSlowestMs), 300.0f,
            AudioParameterFloatAttributes().withLabel ("ms")
                .withStringFromValueFunction ([] (float v, int) { return formatMilliseconds (v); })));

        layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ratio, version }, "Ratio", ratioNames, 0));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { mix, version }, "Mix", NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f,
            AudioParameterFloatAttributes().withLabel ("%")
                .withStringFromValueFunction ([] (float v, int) { return String (v, 1); })));

        layout.add (std::make_unique<AudioParameterChoice> (ParameterID { character, version }, "Character", characterNames, 1));

        layout.add (std::make_unique<AudioParameterBool> (ParameterID { bypass, version }, "Bypass", false));
        return layout;
    }
}
