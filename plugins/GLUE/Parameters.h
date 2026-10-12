#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

// Central parameter registry for GLUE. IDs are immutable after the first
// public release: rename display names freely, never the IDs or versions.
// Ranges, steps and their provenance: plugins/GLUE/MODEL_SPEC.md, section 7.
namespace tleet::glue::params
{
    inline constexpr const char* threshold  = "threshold";
    inline constexpr const char* attack     = "attack";
    inline constexpr const char* release    = "release";
    inline constexpr const char* ratio      = "ratio";
    inline constexpr const char* makeup     = "makeup";
    inline constexpr const char* scHpf      = "scHpf";
    inline constexpr const char* mix        = "mix";
    inline constexpr const char* character  = "character";
    inline constexpr const char* dark       = "dark";
    inline constexpr const char* crush      = "crush";
    inline constexpr const char* output     = "output";
    inline constexpr const char* scExternal = "scExternal";
    inline constexpr const char* bypass     = "bypass";

    inline constexpr int version = 1;

    // Stepped controls (MODEL_SPEC.md section 3). Attack, release and the
    // ratios 2/4/10 are the hardware switch positions; 1.5 and infinity are
    // TLEET extensions taken from the GLUE skin.
    enum class Attack { ms0_1 = 0, ms0_3, ms1, ms3, ms10, ms30 };
    enum class Release { s0_1 = 0, s0_3, s0_6, s1_2, autoRelease };
    enum class Ratio { r1_5 = 0, r2, r4, r10, rInf };
    enum class Character { clean = 0, tleet, burn };

    inline const juce::StringArray attackNames { "0.1 ms", "0.3 ms", "1 ms", "3 ms", "10 ms", "30 ms" };
    inline const juce::StringArray releaseNames { "0.1 s", "0.3 s", "0.6 s", "1.2 s", "Auto" };
    inline const juce::StringArray ratioNames { "1.5", "2", "4", "10", juce::String::fromUTF8 ("\xe2\x88\x9e") };
    inline const juce::StringArray characterNames { "Clean", "TLEET", "Burn" };

    // Threshold knob (dB) and its calibration: knob 0 dB = -10 dBFS peak at
    // the detector, so -20..+10 covers -30..0 dBFS peak (MODEL_SPEC 2.4).
    inline constexpr float thresholdMinDb = -20.0f, thresholdMaxDb = 10.0f, thresholdDefaultDb = 0.0f;
    inline constexpr float thresholdReferenceDbfs = -10.0f;
    inline constexpr float thresholdToDbfs (float knobDb) noexcept { return knobDb + thresholdReferenceDbfs; }

    inline constexpr float makeupMinDb = -10.0f, makeupMaxDb = 20.0f;
    inline constexpr float outputMinDb = -20.0f, outputMaxDb = 20.0f;

    // Side-chain high-pass (12 dB/oct, detector path only), log 20..300 Hz.
    // 20 Hz is the gentlest setting: it only removes subsonic content.
    inline constexpr float scHpfMinHz = 20.0f, scHpfMaxHz = 300.0f;

    inline juce::NormalisableRange<float> logRange (float lo, float hi)
    {
        return juce::NormalisableRange<float> (
            lo, hi,
            [] (float a, float b, float n) { return a * std::pow (b / a, n); },
            [] (float a, float b, float v) { return std::log (v / a) / std::log (b / a); },
            [] (float a, float b, float v) { return juce::jlimit (a, b, v); });
    }

    // Piecewise-linear dB range with 0 dB at the knob centre (normalised 0.5):
    // lo..0 on [0, 0.5] and 0..hi on [0.5, 1], 0.1 dB legal values (the skin
    // prints 0 at the top of the THRESHOLD and MAKEUP knobs). convertTo0to1 is
    // the exact inverse of convertFrom0to1 on each segment; snapping rounds to
    // the 0.1 dB grid, so text entry, automation and state all round-trip.
    inline constexpr float dbStep = 0.1f;

    inline float snapDb (float lo, float hi, float v) noexcept
    {
        return juce::jlimit (lo, hi, std::round (v / dbStep) * dbStep);
    }

    inline juce::NormalisableRange<float> centredDbRange (float lo, float hi)
    {
        jassert (lo < 0.0f && hi > 0.0f);
        juce::NormalisableRange<float> range (
            lo, hi,
            [] (float a, float b, float n)
            {
                n = juce::jlimit (0.0f, 1.0f, n);
                return n <= 0.5f ? a * (1.0f - 2.0f * n) : b * (2.0f * n - 1.0f);
            },
            [] (float a, float b, float v)
            {
                v = juce::jlimit (a, b, v);
                return v <= 0.0f ? 0.5f * (1.0f - v / a) : 0.5f + 0.5f * (v / b);
            },
            [] (float a, float b, float v) { return snapDb (a, b, v); });
        range.interval = dbStep; // step count for hosts; snapping is the lambda above
        return range;
    }

    inline juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
    {
        using namespace juce;
        AudioProcessorValueTreeState::ParameterLayout layout;

        auto db = [] (float v, int) { return (v > 0 ? "+" : "") + String (v, 1) + " dB"; };
        auto percent = [] (float v, int) { return String (v, 1); };

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { threshold, version }, "Threshold",
            centredDbRange (thresholdMinDb, thresholdMaxDb), thresholdDefaultDb,
            AudioParameterFloatAttributes().withLabel ("dB").withStringFromValueFunction (db)));

        layout.add (std::make_unique<AudioParameterChoice> (ParameterID { attack, version }, "Attack", attackNames, (int) Attack::ms10));
        layout.add (std::make_unique<AudioParameterChoice> (ParameterID { release, version }, "Release", releaseNames, (int) Release::autoRelease));
        layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ratio, version }, "Ratio", ratioNames, (int) Ratio::r4));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { makeup, version }, "Makeup", centredDbRange (makeupMinDb, makeupMaxDb), 0.0f,
            AudioParameterFloatAttributes().withLabel ("dB").withStringFromValueFunction (db)));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { scHpf, version }, "SC HPF", logRange (scHpfMinHz, scHpfMaxHz), scHpfMinHz,
            AudioParameterFloatAttributes().withLabel ("Hz")
                .withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v)) + " Hz"; })));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { mix, version }, "Mix", NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f,
            AudioParameterFloatAttributes().withLabel ("%").withStringFromValueFunction (percent)));

        layout.add (std::make_unique<AudioParameterChoice> (ParameterID { character, version }, "Character", characterNames, (int) Character::tleet));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { dark, version }, "Dark", NormalisableRange<float> (0.0f, 100.0f, 0.1f), 0.0f,
            AudioParameterFloatAttributes().withLabel ("%").withStringFromValueFunction (percent)));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { crush, version }, "Crush", NormalisableRange<float> (0.0f, 100.0f, 0.1f), 0.0f,
            AudioParameterFloatAttributes().withLabel ("%").withStringFromValueFunction (percent)));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { output, version }, "Output", NormalisableRange<float> (outputMinDb, outputMaxDb, 0.1f), 0.0f,
            AudioParameterFloatAttributes().withLabel ("dB").withStringFromValueFunction (db)));

        layout.add (std::make_unique<AudioParameterBool> (ParameterID { scExternal, version }, "External SC", false));
        layout.add (std::make_unique<AudioParameterBool> (ParameterID { bypass, version }, "Bypass", false));
        return layout;
    }
}
