#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <cmath>
#include <limits>

// Central parameter registry for PUNCH. IDs are immutable after the first
// public release: rename display names freely, never the IDs or versions.
// Ranges, steps and their provenance: plugins/PUNCH/MODEL_SPEC.md, section 8.
namespace tleet::punch::params
{
    inline constexpr const char* threshold = "threshold";
    inline constexpr const char* ratio     = "ratio";
    inline constexpr const char* overEasy  = "overEasy";
    inline constexpr const char* grit      = "grit";
    inline constexpr const char* mix       = "mix";
    inline constexpr const char* output    = "output";
    inline constexpr const char* bypass    = "bypass";

    inline constexpr int version = 1;

    // ---- THRESHOLD knob (dB) and its calibration.
    // The detector measures true RMS. Its level is in dB re full scale of
    // the mean square: a full-scale square wave reads 0 dB and a sine that
    // peaks at P dBFS reads P - 3.01 dB. Knob 0 dB = -15 dB RMS (a sine
    // peaking at -12 dBFS), so the -20..+10 travel covers -35..-5 dB RMS
    // (MODEL_SPEC 2.4, RESEARCH F6).
    inline constexpr float thresholdMinDb = -20.0f, thresholdMaxDb = 10.0f, thresholdDefaultDb = 0.0f;
    inline constexpr float thresholdReferenceDb = -15.0f;
    inline constexpr float thresholdToDetectorDb (float knobDb) noexcept { return knobDb + thresholdReferenceDb; }

    // ---- COMPRESSION (ratio). The plain value is the compression slope
    // s = 1 - 1/ratio, 0..1, mapped linearly onto the knob. s is the
    // fraction of the detector's control voltage that reaches the VCA, which
    // is how a dbx-style feed-forward ratio pot works (RATIO = 1/(1 - R),
    // RESEARCH F3). 1:1 is fully left (s = 0); infinity is fully right
    // (s = 1) and reachable; automation moves the gain law smoothly.
    // Knob positions of the editor's labels: 2:1 at 0.5, 4:1 at 0.75,
    // 8:1 at 0.875, 20:1 at 0.95, infinity at 1.
    inline constexpr float ratioDefault = 4.0f;

    inline float ratioFromPlain (float slope) noexcept
    {
        if (! (slope < 1.0f))
            return std::numeric_limits<float>::infinity();
        return 1.0f / (1.0f - juce::jmax (0.0f, slope));
    }

    inline float plainFromRatio (float r) noexcept
    {
        if (std::isnan (r) || r <= 1.0f)
            return 0.0f;
        if (std::isinf (r))
            return 1.0f;
        return 1.0f - 1.0f / r;
    }

    inline juce::String ratioText (float slope)
    {
        const float r = ratioFromPlain (slope);
        if (std::isinf (r))
            return juce::String::fromUTF8 ("\xe2\x88\x9e:1");
        if (r < 10.0f)
            return juce::String (r, 1) + ":1";
        return juce::String (juce::roundToInt (r)) + ":1";
    }

    // Accepts "4:1", "4", "4.5 : 1", "inf", "oo" or the infinity sign.
    inline float ratioPlainFromText (const juce::String& text)
    {
        const auto t = text.trim().upToFirstOccurrenceOf (":", false, false).trim().toLowerCase();
        if (t.startsWith ("inf") || t.startsWith ("oo") || t.startsWith (juce::String::fromUTF8 ("\xe2\x88\x9e")))
            return 1.0f;
        return plainFromRatio (t.getFloatValue());
    }

    inline constexpr float gritDefault = 20.0f; // subtle VCA colour by default (MODEL_SPEC 5)
    inline constexpr float outputMinDb = -20.0f, outputMaxDb = 20.0f;

    // Piecewise-linear dB range with 0 dB at the knob centre (normalised 0.5):
    // lo..0 on [0, 0.5] and 0..hi on [0.5, 1], 0.1 dB legal values (the skin
    // prints 0 at the top of the THRESHOLD knob). convertTo0to1 is the exact
    // inverse of convertFrom0to1 on each segment; snapping rounds to the
    // 0.1 dB grid, so text entry, automation and state all round-trip.
    // (Same law as GLUE's centredDbRange.)
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

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { ratio, version }, "Compression", NormalisableRange<float> (0.0f, 1.0f), plainFromRatio (ratioDefault),
            AudioParameterFloatAttributes()
                .withStringFromValueFunction ([] (float v, int) { return ratioText (v); })
                .withValueFromStringFunction ([] (const String& t) { return ratioPlainFromText (t); })));

        layout.add (std::make_unique<AudioParameterBool> (ParameterID { overEasy, version }, "OverEasy", false));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { grit, version }, "Grit", NormalisableRange<float> (0.0f, 100.0f, 0.1f), gritDefault,
            AudioParameterFloatAttributes().withLabel ("%").withStringFromValueFunction (percent)));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { mix, version }, "Mix", NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f,
            AudioParameterFloatAttributes().withLabel ("%").withStringFromValueFunction (percent)));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { output, version }, "Output", NormalisableRange<float> (outputMinDb, outputMaxDb, 0.1f), 0.0f,
            AudioParameterFloatAttributes().withLabel ("dB").withStringFromValueFunction (db)));

        layout.add (std::make_unique<AudioParameterBool> (ParameterID { bypass, version }, "Bypass", false));
        return layout;
    }
}
