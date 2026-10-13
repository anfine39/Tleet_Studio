#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <cmath>

// Central parameter registry for VEIL. IDs are immutable after the first
// public release: rename display names freely, never the IDs or versions.
// Ranges, laws and their provenance: plugins/VEIL/MODEL_SPEC.md, section 10.
namespace tleet::veil::params
{
    inline constexpr const char* input      = "input";
    inline constexpr const char* threshold  = "threshold";
    inline constexpr const char* attack     = "attack";
    inline constexpr const char* recovery   = "recovery";
    inline constexpr const char* compress   = "compress";
    inline constexpr const char* mix        = "mix";
    inline constexpr const char* output     = "output";
    inline constexpr const char* tubeDrive  = "tubeDrive";
    inline constexpr const char* harmonics  = "harmonics";
    inline constexpr const char* saturation = "saturation";
    inline constexpr const char* dryWet     = "dryWet";
    inline constexpr const char* hpf        = "hpf";
    inline constexpr const char* scHpf      = "scHpf";
    inline constexpr const char* character  = "character";
    inline constexpr const char* scExternal = "scExternal";
    inline constexpr const char* stereoLink = "stereoLink";
    inline constexpr const char* meterMode  = "meterMode";
    inline constexpr const char* bypass     = "bypass";

    inline constexpr int version = 1;

    // CHARACTER: three calibrated voicings of the tube stages (MODEL_SPEC 6.4).
    enum class Character { clean = 0, veil, thick };
    // METER: what the two VU meters show. Display only, never reaches the DSP.
    enum class MeterMode { gr = 0, vu, inOut };

    inline const juce::StringArray characterNames { "Clean", "Veil", "Thick" };
    inline const juce::StringArray meterModeNames { "GR", "VU", "In/Out" };

    // ---- level calibration (MODEL_SPEC 2.6, 8.2)
    // Levels are AES17 dBFS: a sine peaking at P dBFS reads P. The alignment
    // level is EBU R68 (0 dBu = -18 dBFS); the hardware's 0 VU (+4 dBu) is
    // placed at that alignment level, the usual plug-in convention.
    //   THRESHOLD knob 0 dB  = -18 dBFS (the knee centre of the vari-mu law),
    //   so the -20..+20 dB travel covers -38..+2 dBFS.
    //   VU meters: 0 VU = a sine at -18 dBFS (RMS 0.0890), see vuDb().
    inline constexpr float alignmentDbfs = -18.0f;
    inline constexpr float thresholdMinDb = -20.0f, thresholdMaxDb = 20.0f;
    inline constexpr float thresholdToDbfs (float knobDb) noexcept { return knobDb + alignmentDbfs; }

    // VU reading (dB re 0 VU) of an RMS value: a sine at -18 dBFS reads 0 VU.
    inline float vuDb (float rms) noexcept
    {
        return 20.0f * std::log10 (std::max (rms, 1.0e-9f) * 1.41421356f) - alignmentDbfs;
    }

    inline constexpr float inputMinDb = -20.0f, inputMaxDb = 20.0f;
    inline constexpr float outputMinDb = -20.0f, outputMaxDb = 20.0f;

    inline constexpr float attackMinMs = 10.0f, attackMaxMs = 1000.0f, attackDefaultMs = 50.0f;
    inline constexpr float recoveryMinMs = 50.0f, recoveryMaxMs = 2000.0f, recoveryCentreMs = 400.0f, recoveryDefaultMs = 400.0f;

    inline constexpr float compressMax = 10.0f, compressDefault = 5.0f;

    // Filters: the minimum (20 Hz) is OFF (the filter is bypassed, not set to 20 Hz).
    inline constexpr float filterOffHz = 20.0f, filterMaxHz = 300.0f;
    inline constexpr float scHpfCentreHz = 80.0f, scHpfDefaultHz = 60.0f;
    inline constexpr float hpfCentreHz = 100.0f, hpfDefaultHz = filterOffHz;
    inline constexpr bool filterIsOff (float hz) noexcept { return hz <= filterOffHz + 1.0e-3f; }

    inline constexpr float tubeDriveDefault = 28.0f, harmonicsDefault = 42.0f, saturationDefault = 18.0f;

    // ---- ranges
    inline juce::NormalisableRange<float> logRange (float lo, float hi)
    {
        return juce::NormalisableRange<float> (
            lo, hi,
            [] (float a, float b, float n) { return a * std::pow (b / a, juce::jlimit (0.0f, 1.0f, n)); },
            [] (float a, float b, float v) { return std::log (juce::jlimit (a, b, v) / a) / std::log (b / a); },
            [] (float a, float b, float v) { return juce::jlimit (a, b, v); });
    }

    inline juce::NormalisableRange<float> skewedRange (float lo, float hi, float centre)
    {
        juce::NormalisableRange<float> r (lo, hi);
        r.setSkewForCentre (centre);
        return r;
    }

    // Piecewise-linear dB range with 0 dB at the knob centre (normalised 0.5),
    // 0.1 dB legal values with exact snapping (as WEIGHT and GLUE).
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

    // "120 ms" below one second, "1.2 s" from one second (rounded to the shown digits).
    inline juce::String timeText (float ms)
    {
        if (std::round (ms) < 1000.0f)
            return juce::String (juce::roundToInt (ms)) + " ms";
        return juce::String (ms / 1000.0f, 1) + " s";
    }

    inline juce::String percentText (float v)
    {
        const float r = std::round (v * 10.0f) / 10.0f;
        return (std::abs (r - std::round (r)) < 1.0e-4f ? juce::String (juce::roundToInt (r)) : juce::String (r, 1)) + " %";
    }

    inline juce::String hzText (float hz)
    {
        return filterIsOff (hz) ? juce::String ("Off") : juce::String (juce::roundToInt (hz)) + " Hz";
    }

    inline float numberFromText (const juce::String& t) { return t.retainCharacters ("+-0123456789.").getFloatValue(); }

    inline float timeFromText (const juce::String& t)
    {
        const auto s = t.trim().toLowerCase();
        const float v = numberFromText (s);
        const bool seconds = s.endsWith ("s") && ! s.endsWith ("ms");
        return seconds ? v * 1000.0f : v;
    }

    inline float hzFromText (const juce::String& t)
    {
        const auto s = t.trim().toLowerCase();
        if (s.isEmpty() || s.startsWith ("off"))
            return filterOffHz;
        const float v = numberFromText (s);
        return s.containsIgnoreCase ("k") ? v * 1000.0f : v;
    }

    inline juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
    {
        using namespace juce;
        AudioProcessorValueTreeState::ParameterLayout layout;

        auto dbAttr = []
        {
            return AudioParameterFloatAttributes()
                .withStringFromValueFunction ([] (float v, int) { return dbText (v); })
                .withValueFromStringFunction ([] (const String& t) { return numberFromText (t); });
        };
        auto pctAttr = []
        {
            return AudioParameterFloatAttributes()
                .withStringFromValueFunction ([] (float v, int) { return percentText (v); })
                .withValueFromStringFunction ([] (const String& t) { return numberFromText (t); });
        };
        auto timeAttr = []
        {
            return AudioParameterFloatAttributes()
                .withStringFromValueFunction ([] (float v, int) { return timeText (v); })
                .withValueFromStringFunction ([] (const String& t) { return timeFromText (t); });
        };
        auto hzAttr = []
        {
            return AudioParameterFloatAttributes()
                .withStringFromValueFunction ([] (float v, int) { return hzText (v); })
                .withValueFromStringFunction ([] (const String& t) { return hzFromText (t); });
        };
        auto percentRange = [] { return NormalisableRange<float> (0.0f, 100.0f, 0.1f); };

        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { input, version }, "Input",
                                                           centredDbRange (inputMinDb, inputMaxDb), 0.0f, dbAttr()));
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { threshold, version }, "Threshold",
                                                           centredDbRange (thresholdMinDb, thresholdMaxDb), 0.0f, dbAttr()));
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { attack, version }, "Attack",
                                                           logRange (attackMinMs, attackMaxMs), attackDefaultMs, timeAttr()));
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { recovery, version }, "Recovery",
                                                           skewedRange (recoveryMinMs, recoveryMaxMs, recoveryCentreMs),
                                                           recoveryDefaultMs, timeAttr()));
        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { compress, version }, "Compress", NormalisableRange<float> (0.0f, compressMax), compressDefault,
            AudioParameterFloatAttributes()
                .withStringFromValueFunction ([] (float v, int) { return String (v, 1); })
                .withValueFromStringFunction ([] (const String& t) { return numberFromText (t); })));
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { mix, version }, "Mix", percentRange(), 100.0f, pctAttr()));
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { output, version }, "Output",
                                                           centredDbRange (outputMinDb, outputMaxDb), 0.0f, dbAttr()));
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { tubeDrive, version }, "Tube Drive", percentRange(),
                                                           tubeDriveDefault, pctAttr()));
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { harmonics, version }, "Harmonics", percentRange(),
                                                           harmonicsDefault, pctAttr()));
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { saturation, version }, "Saturation", percentRange(),
                                                           saturationDefault, pctAttr()));
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { dryWet, version }, "Dry/Wet", percentRange(), 100.0f, pctAttr()));
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { hpf, version }, "HPF",
                                                           skewedRange (filterOffHz, filterMaxHz, hpfCentreHz), hpfDefaultHz, hzAttr()));
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { scHpf, version }, "SC HPF",
                                                           skewedRange (filterOffHz, filterMaxHz, scHpfCentreHz), scHpfDefaultHz, hzAttr()));
        layout.add (std::make_unique<AudioParameterChoice> (ParameterID { character, version }, "Character", characterNames,
                                                            (int) Character::veil));
        layout.add (std::make_unique<AudioParameterBool> (
            ParameterID { scExternal, version }, "Sidechain", false,
            AudioParameterBoolAttributes()
                .withStringFromValueFunction ([] (bool v, int) { return String (v ? "Ext" : "Int"); })
                .withValueFromStringFunction ([] (const String& t) { return t.trim().startsWithIgnoreCase ("e") || t.trim() == "1"; })));
        layout.add (std::make_unique<AudioParameterBool> (
            ParameterID { stereoLink, version }, "Stereo Link", true,
            AudioParameterBoolAttributes()
                .withStringFromValueFunction ([] (bool v, int) { return String (v ? "Linked" : "Dual Mono"); })
                .withValueFromStringFunction ([] (const String& t) { return t.trim().startsWithIgnoreCase ("l") || t.trim() == "1"; })));
        layout.add (std::make_unique<AudioParameterChoice> (ParameterID { meterMode, version }, "Meter", meterModeNames,
                                                            (int) MeterMode::gr));
        layout.add (std::make_unique<AudioParameterBool> (ParameterID { bypass, version }, "Bypass", false));
        return layout;
    }
}
