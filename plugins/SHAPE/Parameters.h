#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <cmath>

// Central parameter registry for SHAPE. IDs are immutable after the first
// public release: rename display names freely, never the IDs or versions.
// Ranges, laws and their provenance: plugins/SHAPE/MODEL_SPEC.md, section 9.
namespace tleet::shape::params
{
    inline constexpr const char* lfFreq      = "lfFreq";
    inline constexpr const char* lfBoost     = "lfBoost";
    inline constexpr const char* lfAtten     = "lfAtten";
    inline constexpr const char* hfFreq      = "hfFreq";
    inline constexpr const char* hfBoost     = "hfBoost";
    inline constexpr const char* hfBandwidth = "hfBandwidth";
    inline constexpr const char* hfAttenFreq = "hfAttenFreq";
    inline constexpr const char* hfAtten     = "hfAtten";
    inline constexpr const char* drive       = "drive";
    inline constexpr const char* character   = "character";
    inline constexpr const char* mix         = "mix";
    inline constexpr const char* output      = "output";
    inline constexpr const char* eqIn        = "eqIn";
    inline constexpr const char* bypass      = "bypass";

    inline constexpr int version = 1;

    // Discrete selectors: only the hardware's real positions (MODEL_SPEC 3).
    inline const juce::StringArray lfFreqNames { "20 Hz", "30 Hz", "60 Hz", "100 Hz" };
    inline const juce::StringArray hfFreqNames { "3 kHz", "4 kHz", "5 kHz", "8 kHz", "10 kHz", "12 kHz", "16 kHz" };
    inline const juce::StringArray hfAttenFreqNames { "5 kHz", "10 kHz", "20 kHz" };
    inline constexpr double lfFreqHz[] { 20.0, 30.0, 60.0, 100.0 };
    inline constexpr double hfFreqHz[] { 3000.0, 4000.0, 5000.0, 8000.0, 10000.0, 12000.0, 16000.0 };
    inline constexpr double hfAttenFreqHz[] { 5000.0, 10000.0, 20000.0 };
    inline constexpr int numLfFreqs = 4, numHfFreqs = 7, numHfAttenFreqs = 3;

    // CHARACTER: three voicings of the transformers and the make-up amplifier (MODEL_SPEC 5.4).
    enum class Character { clean = 0, original, burn };
    inline const juce::StringArray characterNames { "Clean", "Original", "Burn" };

    // ---- defaults: the hardware's flat setting (every boost/atten at 0), the
    // usual reference positions (LF 60 Hz, HF 10 kHz, atten 20 kHz), bandwidth
    // in the middle, Original voicing with a moderate drive.
    inline constexpr int lfFreqDefault = 2;       // 60 Hz
    inline constexpr int hfFreqDefault = 4;       // 10 kHz
    inline constexpr int hfAttenFreqDefault = 2;  // 20 kHz
    inline constexpr float knobMax = 10.0f;
    inline constexpr float bandwidthDefault = 5.0f;
    inline constexpr float driveDefault = 25.0f;
    inline constexpr float outputMinDb = -20.0f, outputMaxDb = 20.0f;

    // ---- level calibration (MODEL_SPEC 5.1): EBU R68, 0 dBu = -18 dBFS (sine
    // peak, AES17). DRIVE 0 % puts the analog stages at that alignment.
    inline constexpr float alignmentDbfs = -18.0f;

    // Piecewise-linear dB range with 0 dB at the knob centre (normalised 0.5),
    // 0.1 dB legal values with exact snapping (as VEIL, WEIGHT and GLUE).
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
        range.interval = dbStep;
        return range;
    }

    // ---- text (the skin's readouts print these strings verbatim; no labels)
    inline juce::String dbText (float v)
    {
        const float r = std::round (v * 10.0f) / 10.0f;
        return (r > 0.0f ? "+" : "") + juce::String (std::abs (r) < 0.05f ? 0.0f : r, 1) + " dB";
    }

    inline juce::String percentText (float v)
    {
        const float r = std::round (v * 10.0f) / 10.0f;
        return (std::abs (r - std::round (r)) < 1.0e-4f ? juce::String (juce::roundToInt (r)) : juce::String (r, 1)) + " %";
    }

    // The 0..10 knobs print a bare number with one decimal ("6.5"), like the
    // hardware's unitless dial.
    inline juce::String knobText (float v)
    {
        const float r = std::round (v * 10.0f) / 10.0f;
        return juce::String (std::abs (r) < 0.05f ? 0.0f : r, 1);
    }

    inline float numberFromText (const juce::String& t) { return t.retainCharacters ("+-0123456789.").getFloatValue(); }

    // BANDWIDTH text input also accepts the dial's end labels.
    inline float bandwidthFromText (const juce::String& t)
    {
        const auto s = t.trim().toLowerCase();
        if (s.startsWith ("sharp")) return 0.0f;
        if (s.startsWith ("broad")) return knobMax;
        return numberFromText (s);
    }

    inline juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
    {
        using namespace juce;
        AudioProcessorValueTreeState::ParameterLayout layout;

        auto knobAttr = []
        {
            return AudioParameterFloatAttributes()
                .withStringFromValueFunction ([] (float v, int) { return knobText (v); })
                .withValueFromStringFunction ([] (const String& t) { return numberFromText (t); });
        };
        auto pctAttr = []
        {
            return AudioParameterFloatAttributes()
                .withStringFromValueFunction ([] (float v, int) { return percentText (v); })
                .withValueFromStringFunction ([] (const String& t) { return numberFromText (t); });
        };
        auto knobRange = [] { return NormalisableRange<float> (0.0f, knobMax); };
        auto percentRange = [] { return NormalisableRange<float> (0.0f, 100.0f, 0.1f); };

        layout.add (std::make_unique<AudioParameterChoice> (ParameterID { lfFreq, version }, "LF Frequency", lfFreqNames, lfFreqDefault));
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { lfBoost, version }, "LF Boost", knobRange(), 0.0f, knobAttr()));
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { lfAtten, version }, "LF Atten", knobRange(), 0.0f, knobAttr()));
        layout.add (std::make_unique<AudioParameterChoice> (ParameterID { hfFreq, version }, "HF Frequency", hfFreqNames, hfFreqDefault));
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { hfBoost, version }, "HF Boost", knobRange(), 0.0f, knobAttr()));
        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { hfBandwidth, version }, "HF Bandwidth", knobRange(), bandwidthDefault,
            AudioParameterFloatAttributes()
                .withStringFromValueFunction ([] (float v, int) { return knobText (v); })
                .withValueFromStringFunction ([] (const String& t) { return bandwidthFromText (t); })));
        layout.add (std::make_unique<AudioParameterChoice> (ParameterID { hfAttenFreq, version }, "HF Atten Frequency",
                                                            hfAttenFreqNames, hfAttenFreqDefault));
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { hfAtten, version }, "HF Atten", knobRange(), 0.0f, knobAttr()));
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { drive, version }, "Drive", percentRange(), driveDefault, pctAttr()));
        layout.add (std::make_unique<AudioParameterChoice> (ParameterID { character, version }, "Character", characterNames,
                                                            (int) Character::original));
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { mix, version }, "Mix", percentRange(), 100.0f, pctAttr()));
        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { output, version }, "Output", centredDbRange (outputMinDb, outputMaxDb), 0.0f,
            AudioParameterFloatAttributes()
                .withStringFromValueFunction ([] (float v, int) { return dbText (v); })
                .withValueFromStringFunction ([] (const String& t) { return numberFromText (t); })));
        layout.add (std::make_unique<AudioParameterBool> (
            ParameterID { eqIn, version }, "EQ In", true,
            AudioParameterBoolAttributes()
                .withStringFromValueFunction ([] (bool v, int) { return String (v ? "In" : "Out"); })
                .withValueFromStringFunction ([] (const String& t)
                                              {
                                                  const auto s = t.trim().toLowerCase();
                                                  return s == "in" || s == "1" || s == "on" || s == "true";
                                              })));
        layout.add (std::make_unique<AudioParameterBool> (ParameterID { bypass, version }, "Bypass", false));
        return layout;
    }
}
