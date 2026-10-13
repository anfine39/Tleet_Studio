#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <cmath>

// Central parameter registry for WEIGHT. IDs are immutable after the first
// public release: rename display names freely, never the IDs or versions.
// Ranges, steps and their provenance: plugins/WEIGHT/MODEL_SPEC.md, section 9.
namespace tleet::weight::params
{
    inline constexpr const char* threshold  = "threshold";
    inline constexpr const char* attack     = "attack";
    inline constexpr const char* release    = "release";
    inline constexpr const char* ratio      = "ratio";
    inline constexpr const char* makeup     = "makeup";
    inline constexpr const char* thrust     = "thrust";
    inline constexpr const char* detector   = "detector";
    inline constexpr const char* tone       = "tone";
    inline constexpr const char* scHpf      = "scHpf";
    inline constexpr const char* scExternal = "scExternal";
    inline constexpr const char* mix        = "mix";
    inline constexpr const char* output     = "output";
    inline constexpr const char* link       = "link";
    inline constexpr const char* grMode     = "grMode";
    inline constexpr const char* bypass     = "bypass";

    inline constexpr int version = 1;

    // Stepped controls. Attack, release and ratio are the API 2500's own
    // switch positions (RESEARCH.md F2-F4). The hardware's VARIABLE release
    // position has no control on the skin and is not modelled.
    enum class Attack { us30 = 0, us100, us300, ms1, ms3, ms10, ms30 };
    enum class Release { ms50 = 0, ms100, ms200, ms500, s1, s2 };
    enum class Ratio { r1_5 = 0, r2, r3, r4, r6, r10, rInf };
    // THRUST: TLEET labels of the API positions NORM / MED / LOUD (detector only).
    enum class Thrust { clean = 0, neutral, hard };
    // DETECTOR: API NEW (feed-forward) / OLD (feedback).
    enum class Detector { feedForward = 0, feedBack };
    // TONE: output-stage voicing (TLEET extension, not the API OLD/NEW switch).
    enum class Tone { old = 0, neutral, modern };
    // GR MODE: what the GR meters display. Never changes the audio.
    enum class GrMode { peak = 0, rms, average };

    inline const juce::StringArray attackNames { "0.03 ms", "0.1 ms", "0.3 ms", "1 ms", "3 ms", "10 ms", "30 ms" };
    inline const juce::StringArray releaseNames { "0.05 s", "0.1 s", "0.2 s", "0.5 s", "1 s", "2 s" };
    inline const juce::StringArray ratioNames { "1.5", "2", "3", "4", "6", "10", juce::String::fromUTF8 ("\xe2\x88\x9e") };
    inline const juce::StringArray thrustNames { "Clean", "Neutral", "Hard" };
    inline const juce::StringArray detectorNames { "Feed Forward", "Feed Back" };
    inline const juce::StringArray toneNames { "Old", "Neutral", "New" };
    inline const juce::StringArray grModeNames { "Peak", "RMS", "Average" };

    // Threshold knob (dB) and its calibration. The knob reads the hardware's
    // dBu scale (+10..-20 dBu, API 2500 manual), aligned with EBU R68
    // (0 dBu = -18 dBFS). The detector is true RMS; its level is in AES17
    // dBFS RMS (a sine peaking at P dBFS reads P). Knob 0 dB = -18 dBFS RMS,
    // so the -20..+10 travel covers -38..-8 dBFS RMS (MODEL_SPEC 2.5).
    inline constexpr float thresholdMinDb = -20.0f, thresholdMaxDb = 10.0f, thresholdDefaultDb = 0.0f;
    inline constexpr float thresholdReferenceDbfs = -18.0f;
    inline constexpr float thresholdToDbfs (float knobDb) noexcept { return knobDb + thresholdReferenceDbfs; }

    inline constexpr float makeupMinDb = -12.0f, makeupMaxDb = 24.0f;
    inline constexpr float outputMinDb = -12.0f, outputMaxDb = 12.0f;

    // Side-chain high-pass (12 dB/oct, detector path only), log 20..240 Hz.
    // 20 Hz is the gentlest setting: it only removes subsonic content.
    inline constexpr float scHpfMinHz = 20.0f, scHpfMaxHz = 240.0f;

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
        layout.add (std::make_unique<AudioParameterChoice> (ParameterID { release, version }, "Release", releaseNames, (int) Release::ms200));
        layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ratio, version }, "Ratio", ratioNames, (int) Ratio::r2));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { makeup, version }, "Makeup", centredDbRange (makeupMinDb, makeupMaxDb), 0.0f,
            AudioParameterFloatAttributes().withLabel ("dB").withStringFromValueFunction (db)));

        layout.add (std::make_unique<AudioParameterChoice> (ParameterID { thrust, version }, "Thrust", thrustNames, (int) Thrust::clean));
        layout.add (std::make_unique<AudioParameterChoice> (ParameterID { detector, version }, "Detector", detectorNames, (int) Detector::feedForward));
        layout.add (std::make_unique<AudioParameterChoice> (ParameterID { tone, version }, "Tone", toneNames, (int) Tone::neutral));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { scHpf, version }, "SC HPF", logRange (scHpfMinHz, scHpfMaxHz), scHpfMinHz,
            AudioParameterFloatAttributes().withLabel ("Hz")
                .withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v)) + " Hz"; })));

        layout.add (std::make_unique<AudioParameterBool> (ParameterID { scExternal, version }, "External SC", false));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { mix, version }, "Mix", NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f,
            AudioParameterFloatAttributes().withLabel ("%").withStringFromValueFunction (percent)));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { output, version }, "Output", NormalisableRange<float> (outputMinDb, outputMaxDb, 0.1f), 0.0f,
            AudioParameterFloatAttributes().withLabel ("dB").withStringFromValueFunction (db)));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { link, version }, "Stereo Link", NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f,
            AudioParameterFloatAttributes().withLabel ("%").withStringFromValueFunction (percent)));

        layout.add (std::make_unique<AudioParameterChoice> (ParameterID { grMode, version }, "GR Mode", grModeNames, (int) GrMode::rms));
        layout.add (std::make_unique<AudioParameterBool> (ParameterID { bypass, version }, "Bypass", false));
        return layout;
    }
}
