#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <cmath>

// Central parameter registry for ECHO. IDs are immutable after the first
// public release: rename display names freely, never the IDs or versions.
// Ranges, laws and their provenance: plugins/ECHO/MODEL_SPEC.md, section 10.
namespace tleet::echo::params
{
    inline constexpr const char* mode          = "mode";
    inline constexpr const char* repeatRate    = "repeatRate";
    inline constexpr const char* intensity     = "intensity";
    inline constexpr const char* echoVolume    = "echoVolume";
    inline constexpr const char* reverbVolume  = "reverbVolume";
    inline constexpr const char* bass          = "bass";
    inline constexpr const char* treble        = "treble";
    inline constexpr const char* timeMode      = "timeMode";
    inline constexpr const char* bpmSource     = "bpmSource";
    inline constexpr const char* manualBpm     = "manualBpm";
    inline constexpr const char* noteDivision  = "noteDivision";
    inline constexpr const char* feel          = "feel";
    inline constexpr const char* drive         = "drive";
    inline constexpr const char* age           = "age";
    inline constexpr const char* wow           = "wow";
    inline constexpr const char* flutter       = "flutter";
    inline constexpr const char* hpf           = "hpf";
    inline constexpr const char* lpf           = "lpf";
    inline constexpr const char* filterMode    = "filterMode";
    inline constexpr const char* stereo        = "stereo";
    inline constexpr const char* duck          = "duck";
    inline constexpr const char* mix           = "mix";
    inline constexpr const char* tapeCondition = "tapeCondition";
    inline constexpr const char* noise         = "noise";
    inline constexpr const char* character     = "character";
    inline constexpr const char* output        = "output";
    inline constexpr const char* bypass        = "bypass";

    inline constexpr int version = 1;

    // ---- discrete selectors: only real positions (addendum 4)
    // MODE: which playback heads feed the echo (MODEL_SPEC 3). "Reverb" = no head.
    inline const juce::StringArray modeNames { "Reverb", "1", "2", "3", "1+2", "1+3", "2+3", "1+2+3" };
    inline constexpr int modeHeadMask[] { 0b000, 0b001, 0b010, 0b100, 0b011, 0b101, 0b110, 0b111 };
    inline constexpr int numModes = 8;
    inline constexpr int modeDefault = 4; // "1+2"

    enum class TimeMode { free = 0, sync };
    inline const juce::StringArray timeModeNames { "Free", "Sync" };
    enum class BpmSource { host = 0, manual };
    inline const juce::StringArray bpmSourceNames { "Host", "Manual" };
    inline const juce::StringArray noteDivisionNames { "1/1", "1/2", "1/4", "1/8", "1/16", "1/32" };
    inline constexpr int numDivisions = 6;
    inline constexpr int noteDivisionDefault = 3; // "1/8"
    enum class Feel { straight = 0, dotted, triplet };
    inline const juce::StringArray feelNames { "Straight", "Dotted", "Triplet" };
    enum class FilterMode { pre = 0, loop, post };
    inline const juce::StringArray filterModeNames { "Pre", "Loop", "Post" };
    enum class TapeCondition { clean = 0, worn, magneticDust };
    inline const juce::StringArray tapeConditionNames { "Clean", "Worn", "Magnetic dust" };
    enum class Character { clean = 0, original, burn };
    inline const juce::StringArray characterNames { "Clean", "Original", "Burn" };

    // ---- ranges and defaults (the registry the editor is built against)
    inline constexpr float repeatMinMs = 20.0f, repeatMaxMs = 2000.0f, repeatDefaultMs = 200.0f;
    inline constexpr float knobMax = 10.0f;
    inline constexpr float intensityDefault = 4.0f, echoVolumeDefault = 7.0f, reverbVolumeDefault = 0.0f;
    inline constexpr float toneMaxDb = 10.0f;
    inline constexpr float bpmMin = 20.0f, bpmMax = 300.0f, bpmDefault = 120.0f;
    inline constexpr float driveDefault = 3.0f, ageDefault = 2.0f;
    inline constexpr float wowDefault = 20.0f, flutterDefault = 20.0f;
    inline constexpr float hpfMinHz = 20.0f, hpfMaxHz = 300.0f, lpfMinHz = 1000.0f, lpfMaxHz = 20000.0f;
    inline constexpr float stereoDefault = 30.0f, duckDefault = 0.0f, mixDefault = 35.0f, noiseDefault = 10.0f;
    inline constexpr float outputMaxDb = 12.0f;

    // ---- level calibration: 0 dBu = -18 dBFS (EBU R68), as the rest of the suite.
    inline constexpr float alignmentDbfs = -18.0f;

    inline constexpr float dbStep = 0.1f;

    inline float snapDb (float lo, float hi, float v) noexcept
    {
        return juce::jlimit (lo, hi, std::round (v / dbStep) * dbStep);
    }

    // Piecewise-linear dB range with 0 dB at the knob centre, 0.1 dB legal
    // values with exact snapping (as SHAPE, VEIL, WEIGHT and GLUE).
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

    // Exact logarithmic range: v = lo * (hi/lo)^n. The knob centre is the
    // geometric mean (REPEAT RATE: 200 ms; HPF 77.5 Hz; LPF 4.47 kHz).
    inline juce::NormalisableRange<float> logRange (float lo, float hi)
    {
        return juce::NormalisableRange<float> (
            lo, hi,
            [] (float a, float b, float n) { return a * std::pow (b / a, juce::jlimit (0.0f, 1.0f, n)); },
            [] (float a, float b, float v) { return std::log (juce::jlimit (a, b, v) / a) / std::log (b / a); },
            [] (float a, float b, float v) { return juce::jlimit (a, b, v); });
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
    // hardware's unitless dials.
    inline juce::String knobText (float v)
    {
        const float r = std::round (v * 10.0f) / 10.0f;
        return juce::String (std::abs (r) < 0.05f ? 0.0f : r, 1);
    }

    // "250 ms" below one second, "1.20 s" from one second on.
    inline juce::String msText (float ms)
    {
        if (std::round (ms) < 1000.0f)
            return juce::String (juce::roundToInt (ms)) + " ms";
        return juce::String (ms / 1000.0f, 2) + " s";
    }

    // "85 Hz" below 1 kHz, "4.5 kHz" from 1 kHz on.
    inline juce::String hzText (float hz)
    {
        if (std::round (hz) < 1000.0f)
            return juce::String (juce::roundToInt (hz)) + " Hz";
        return juce::String (hz / 1000.0f, 1) + " kHz";
    }

    inline juce::String bpmText (float bpm) { return juce::String (juce::roundToInt (bpm)) + " BPM"; }

    inline float numberFromText (const juce::String& t) { return t.retainCharacters ("+-0123456789.").getFloatValue(); }

    // Accepts "250", "250 ms", "1.2 s", "1.2s".
    inline float msFromText (const juce::String& t)
    {
        const auto s = t.trim().toLowerCase();
        const float v = numberFromText (s);
        if (s.endsWith ("ms"))
            return v;
        if (s.endsWith ("s"))
            return v * 1000.0f;
        return v;
    }

    // Accepts "85", "85 Hz", "4.5 kHz", "4.5k".
    inline float hzFromText (const juce::String& t)
    {
        const auto s = t.trim().toLowerCase();
        const float v = numberFromText (s);
        return s.containsChar ('k') ? v * 1000.0f : v;
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
        auto knob = [&] { return floatAttr (knobText, numberFromText); };
        auto pct = [&] { return floatAttr (percentText, numberFromText); };
        auto db = [&] { return floatAttr (dbText, numberFromText); };
        auto hz = [&] { return floatAttr (hzText, hzFromText); };
        auto knobRange = [] { return NormalisableRange<float> (0.0f, knobMax); };
        auto percentRange = [] { return NormalisableRange<float> (0.0f, 100.0f, 0.1f); };
        auto addChoice = [&layout] (const char* id, const char* name, const StringArray& choices, int def)
        {
            layout.add (std::make_unique<AudioParameterChoice> (ParameterID { id, version }, name, choices, def));
        };
        auto addFloat = [&layout] (const char* id, const char* name, NormalisableRange<float> r, float def,
                                   AudioParameterFloatAttributes a)
        {
            layout.add (std::make_unique<AudioParameterFloat> (ParameterID { id, version }, name, r, def, a));
        };

        addChoice (mode, "Mode", modeNames, modeDefault);
        addFloat (repeatRate, "Repeat Rate", logRange (repeatMinMs, repeatMaxMs), repeatDefaultMs, floatAttr (msText, msFromText));
        addFloat (intensity, "Intensity", knobRange(), intensityDefault, knob());
        addFloat (echoVolume, "Echo Volume", knobRange(), echoVolumeDefault, knob());
        addFloat (reverbVolume, "Reverb Volume", knobRange(), reverbVolumeDefault, knob());
        addFloat (bass, "Bass", centredDbRange (-toneMaxDb, toneMaxDb), 0.0f, db());
        addFloat (treble, "Treble", centredDbRange (-toneMaxDb, toneMaxDb), 0.0f, db());
        addChoice (timeMode, "Time Mode", timeModeNames, (int) TimeMode::free);
        addChoice (bpmSource, "BPM Source", bpmSourceNames, (int) BpmSource::host);
        addFloat (manualBpm, "Manual BPM", NormalisableRange<float> (bpmMin, bpmMax, 1.0f), bpmDefault,
                  floatAttr (bpmText, numberFromText));
        addChoice (noteDivision, "Note Division", noteDivisionNames, noteDivisionDefault);
        addChoice (feel, "Feel", feelNames, (int) Feel::straight);
        addFloat (drive, "Drive", knobRange(), driveDefault, knob());
        addFloat (age, "Age", knobRange(), ageDefault, knob());
        addFloat (wow, "Wow", percentRange(), wowDefault, pct());
        addFloat (flutter, "Flutter", percentRange(), flutterDefault, pct());
        addFloat (hpf, "HPF", logRange (hpfMinHz, hpfMaxHz), hpfMinHz, hz());
        addFloat (lpf, "LPF", logRange (lpfMinHz, lpfMaxHz), lpfMaxHz, hz());
        addChoice (filterMode, "Filter Mode", filterModeNames, (int) FilterMode::loop);
        addFloat (stereo, "Stereo", percentRange(), stereoDefault, pct());
        addFloat (duck, "Duck", percentRange(), duckDefault, pct());
        addFloat (mix, "Mix", percentRange(), mixDefault, pct());
        addChoice (tapeCondition, "Tape Condition", tapeConditionNames, (int) TapeCondition::clean);
        addFloat (noise, "Noise", percentRange(), noiseDefault, pct());
        addChoice (character, "Character", characterNames, (int) Character::original);
        addFloat (output, "Output", centredDbRange (-outputMaxDb, outputMaxDb), 0.0f, db());
        layout.add (std::make_unique<AudioParameterBool> (ParameterID { bypass, version }, "Bypass", false));
        return layout;
    }
}
