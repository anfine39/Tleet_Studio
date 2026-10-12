#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

// Central parameter registry for DOUBLE. IDs are immutable after the first
// public release: rename display names freely, never the IDs or versions.
namespace tleet::dbl::params
{
    inline constexpr const char* voices     = "voices";
    inline constexpr const char* mode       = "mode";
    inline constexpr const char* detune     = "detune";
    inline constexpr const char* delay      = "delay";
    inline constexpr const char* humanize   = "humanize";
    inline constexpr const char* width      = "width";
    inline constexpr const char* hpf        = "hpf";
    inline constexpr const char* lpf        = "lpf";
    inline constexpr const char* monoSafe   = "monoSafe";
    inline constexpr const char* autoLevel  = "autoLevel";
    inline constexpr const char* mix        = "mix";
    inline constexpr const char* output     = "output";
    inline constexpr const char* bypass     = "bypass";

    inline constexpr int version = 1;

    // VOICES = number of added (doubled) voices next to the dry signal.
    enum class Voices { one = 0, two, four };
    enum class Mode { tight = 0, wide, ghost };

    inline const juce::StringArray voicesNames { "1", "2", "4" };
    inline const juce::StringArray modeNames { "Tight", "Wide", "Ghost" };

    inline int voiceCount (int choiceIndex) noexcept { return choiceIndex <= 0 ? 1 : choiceIndex == 1 ? 2 : 4; }

    inline juce::NormalisableRange<float> logRange (float lo, float hi)
    {
        return { lo, hi,
                 [] (float a, float b, float n) { return a * std::pow (b / a, n); },
                 [] (float a, float b, float v) { return std::log (v / a) / std::log (b / a); },
                 [] (float a, float b, float v) { return juce::jlimit (a, b, v); } };
    }

    inline juce::String formatHz (float hz)
    {
        return hz < 1000.0f ? juce::String (juce::roundToInt (hz)) + " Hz"
                            : juce::String (hz * 0.001f, hz < 9995.0f ? 1 : 0) + " kHz";
    }

    inline juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
    {
        using namespace juce;
        AudioProcessorValueTreeState::ParameterLayout layout;

        layout.add (std::make_unique<AudioParameterChoice> (ParameterID { voices, version }, "Voices", voicesNames, 1));
        layout.add (std::make_unique<AudioParameterChoice> (ParameterID { mode, version }, "Mode", modeNames, 0));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { detune, version }, "Detune", NormalisableRange<float> (-25.0f, 25.0f, 0.1f), 8.0f,
            AudioParameterFloatAttributes().withLabel ("cents")
                .withStringFromValueFunction ([] (float v, int) { return String (v, std::abs (v) < 9.95f ? 1 : 0); })));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { delay, version }, "Delay", NormalisableRange<float> (0.0f, 40.0f, 0.1f), 15.0f,
            AudioParameterFloatAttributes().withLabel ("ms")
                .withStringFromValueFunction ([] (float v, int) { return String (v, v < 9.95f ? 1 : 0); })));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { humanize, version }, "Humanize", NormalisableRange<float> (0.0f, 100.0f, 0.1f), 35.0f,
            AudioParameterFloatAttributes().withLabel ("%")
                .withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v)); })));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { width, version }, "Width", NormalisableRange<float> (0.0f, 200.0f, 0.1f), 100.0f,
            AudioParameterFloatAttributes().withLabel ("%")
                .withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v)); })));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { hpf, version }, "HPF", logRange (20.0f, 1000.0f), 20.0f,
            AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return formatHz (v); })));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { lpf, version }, "LPF", logRange (1000.0f, 20000.0f), 20000.0f,
            AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return formatHz (v); })));

        layout.add (std::make_unique<AudioParameterBool> (ParameterID { monoSafe, version }, "Mono Safe", true));
        layout.add (std::make_unique<AudioParameterBool> (ParameterID { autoLevel, version }, "Auto Level", true));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { mix, version }, "Mix", NormalisableRange<float> (0.0f, 100.0f, 0.1f), 50.0f,
            AudioParameterFloatAttributes().withLabel ("%")
                .withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v)); })));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { output, version }, "Output", NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f,
            AudioParameterFloatAttributes().withLabel ("dB")
                .withStringFromValueFunction ([] (float v, int) { return (v > 0.04f ? "+" : "") + String (v, 1); })));

        layout.add (std::make_unique<AudioParameterBool> (ParameterID { bypass, version }, "Bypass", false));
        return layout;
    }
}
