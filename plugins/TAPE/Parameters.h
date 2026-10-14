#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <cmath>

// Central parameter registry for TAPE. IDs are immutable after the first
// public release: rename display names freely, never the IDs or versions.
// Ranges, laws and their provenance: plugins/TAPE/MODEL_SPEC.md, section 10.
namespace tleet::tape::params
{
    inline constexpr const char* input       = "input";
    inline constexpr const char* output      = "output";
    inline constexpr const char* tapeType    = "tapeType";
    inline constexpr const char* speed       = "speed";
    inline constexpr const char* calibration = "calibration";
    inline constexpr const char* bias        = "bias";
    inline constexpr const char* saturation  = "saturation";
    inline constexpr const char* headBump    = "headBump";
    inline constexpr const char* lf          = "lf";
    inline constexpr const char* hf          = "hf";
    inline constexpr const char* age         = "age";
    inline constexpr const char* wow         = "wow";
    inline constexpr const char* flutter     = "flutter";
    inline constexpr const char* noise       = "noise";
    inline constexpr const char* noiseAmount = "noiseAmount";
    inline constexpr const char* crosstalk   = "crosstalk";
    inline constexpr const char* mix         = "mix";
    inline constexpr const char* autoGain    = "autoGain";
    inline constexpr const char* bypass      = "bypass";

    inline constexpr int version = 1;

    // ---- discrete selectors: only real positions (addendum 4)
    enum class TapeType { classic = 0, modern, hot };
    inline const juce::StringArray tapeTypeNames { "A", "B", "C" };
    inline const juce::StringArray tapeTypeLongNames { "CLASSIC", "MODERN", "HOT" };

    inline const juce::StringArray speedNames { "7.5", "15", "30" };
    inline constexpr double speedIps[3] { 7.5, 15.0, 30.0 };
    inline constexpr int speedDefault = 1;

    inline const juce::StringArray calibrationNames { "-3", "0", "+3", "+6" };
    inline constexpr float calibrationDb[4] { -3.0f, 0.0f, 3.0f, 6.0f };
    inline constexpr int calibrationDefault = 1;

    // ---- ranges and defaults (the registry the editor is built against)
    inline constexpr float inputMinDb = -12.0f, inputMaxDb = 24.0f;
    inline constexpr float outputMaxDb = 24.0f;
    inline constexpr float saturationDefault = 42.0f, headBumpDefault = 36.0f, ageDefault = 18.0f;
    inline constexpr float wowDefault = 6.0f, flutterDefault = 4.0f, noiseAmountDefault = 12.0f;
    inline constexpr float crosstalkDefault = 8.0f, mixDefault = 100.0f;
    inline constexpr float dbStep = 0.1f;

    inline float snapStep (float lo, float hi, float v) noexcept
    {
        return juce::jlimit (lo, hi, std::round (v / dbStep) * dbStep);
    }

    // Linear range with exact 0.1 steps (INPUT).
    inline juce::NormalisableRange<float> steppedRange (float lo, float hi)
    {
        juce::NormalisableRange<float> range (
            lo, hi,
            [] (float a, float b, float n) { return snapStep (a, b, a + (b - a) * juce::jlimit (0.0f, 1.0f, n)); },
            [] (float a, float b, float v) { return (juce::jlimit (a, b, v) - a) / (b - a); },
            [] (float a, float b, float v) { return snapStep (a, b, v); });
        range.interval = dbStep;
        return range;
    }

    // Piecewise-linear dB range with 0 dB at the knob centre, 0.1 dB steps (OUTPUT).
    inline juce::NormalisableRange<float> centredDbRange (float lo, float hi)
    {
        juce::NormalisableRange<float> range (
            lo, hi,
            [] (float a, float b, float n)
            {
                n = juce::jlimit (0.0f, 1.0f, n);
                return snapStep (a, b, n <= 0.5f ? a * (1.0f - 2.0f * n) : b * (2.0f * n - 1.0f));
            },
            [] (float a, float b, float v)
            {
                v = juce::jlimit (a, b, v);
                return v <= 0.0f ? 0.5f * (1.0f - v / a) : 0.5f + 0.5f * (v / b);
            },
            [] (float a, float b, float v) { return snapStep (a, b, v); });
        range.interval = dbStep;
        return range;
    }

    // ---- text (the skin's readouts print these strings verbatim)
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

    // BIAS: "0 %", "+25 %", "-40 %" (whole percent from optimum)
    inline juce::String biasText (float v)
    {
        const int r = juce::roundToInt (v);
        return (r > 0 ? "+" : "") + juce::String (r) + " %";
    }

    // LF / HF: the plain signed integer, "-12", "0", "+18"
    inline juce::String trimText (float v)
    {
        const int r = juce::roundToInt (v);
        return (r > 0 ? "+" : "") + juce::String (r);
    }

    inline juce::String speedText (int index) { return speedNames[juce::jlimit (0, 2, index)] + " IPS"; }
    inline juce::String calibrationText (int index) { return calibrationNames[juce::jlimit (0, 3, index)] + " dB"; }
    inline juce::String tapeTypeText (int index) { return tapeTypeNames[juce::jlimit (0, 2, index)]; }

    inline float numberFromText (const juce::String& t) { return t.retainCharacters ("+-0123456789.").getFloatValue(); }

    inline int indexFromText (const juce::StringArray& names, const juce::String& t)
    {
        const auto s = t.trim().upToFirstOccurrenceOf (" ", false, false);
        for (int i = 0; i < names.size(); ++i)
            if (names[i].equalsIgnoreCase (s))
                return i;
        // numeric fallback ("6" for "+6", "15.0")
        const float v = numberFromText (t);
        for (int i = 0; i < names.size(); ++i)
            if (std::abs (names[i].getFloatValue() - v) < 1.0e-3f)
                return i;
        return -1;
    }

    inline juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
    {
        using namespace juce;
        AudioProcessorValueTreeState::ParameterLayout layout;

        auto floatAttr = [] (std::function<String (float)> toText, std::function<float (const String&)> fromText)
        {
            return AudioParameterFloatAttributes()
                .withStringFromValueFunction ([toText] (float v, int) { return toText (v); })
                .withValueFromStringFunction ([fromText] (const String& t) { return fromText (t); });
        };
        auto addFloat = [&layout] (const char* id, const char* name, NormalisableRange<float> r, float def, AudioParameterFloatAttributes a)
        {
            layout.add (std::make_unique<AudioParameterFloat> (ParameterID { id, version }, name, r, def, a));
        };
        auto addChoice = [&layout] (const char* id, const char* name, const StringArray& choices, int def,
                                    std::function<String (int)> toText)
        {
            const StringArray names = choices;
            layout.add (std::make_unique<AudioParameterChoice> (
                ParameterID { id, version }, name, choices, def,
                AudioParameterChoiceAttributes()
                    .withStringFromValueFunction ([toText] (int i, int) { return toText (i); })
                    .withValueFromStringFunction ([names] (const String& t) { return jmax (0, indexFromText (names, t)); })));
        };
        auto addBool = [&layout] (const char* id, const char* name, bool def)
        {
            layout.add (std::make_unique<AudioParameterBool> (ParameterID { id, version }, name, def));
        };
        auto pct = [&] { return floatAttr (percentText, numberFromText); };
        auto percentRange = [] { return NormalisableRange<float> (0.0f, 100.0f, 0.1f); };
        auto bipolarRange = [] { return NormalisableRange<float> (-100.0f, 100.0f, 0.1f); };

        addFloat (input, "Input", steppedRange (inputMinDb, inputMaxDb), 0.0f, floatAttr (dbText, numberFromText));
        addFloat (output, "Output", centredDbRange (-outputMaxDb, outputMaxDb), 0.0f, floatAttr (dbText, numberFromText));
        addChoice (tapeType, "Tape Type", tapeTypeNames, (int) TapeType::classic, tapeTypeText);
        addChoice (speed, "Tape Speed", speedNames, speedDefault, speedText);
        addChoice (calibration, "Calibration", calibrationNames, calibrationDefault, calibrationText);
        addFloat (bias, "Bias", bipolarRange(), 0.0f, floatAttr (biasText, numberFromText));
        addFloat (saturation, "Saturation", percentRange(), saturationDefault, pct());
        addFloat (headBump, "Head Bump", percentRange(), headBumpDefault, pct());
        addFloat (lf, "LF", bipolarRange(), 0.0f, floatAttr (trimText, numberFromText));
        addFloat (hf, "HF", bipolarRange(), 0.0f, floatAttr (trimText, numberFromText));
        addFloat (age, "Age", percentRange(), ageDefault, pct());
        addFloat (wow, "Wow", percentRange(), wowDefault, pct());
        addFloat (flutter, "Flutter", percentRange(), flutterDefault, pct());
        addBool (noise, "Noise", false);
        addFloat (noiseAmount, "Noise Amount", percentRange(), noiseAmountDefault, pct());
        addFloat (crosstalk, "Crosstalk", percentRange(), crosstalkDefault, pct());
        addFloat (mix, "Mix", percentRange(), mixDefault, pct());
        addBool (autoGain, "Auto Gain", true);
        addBool (bypass, "Bypass", false);
        return layout;
    }
}
