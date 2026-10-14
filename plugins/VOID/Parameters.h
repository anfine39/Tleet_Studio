#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <cmath>

// Central parameter registry for VOID. IDs are immutable after the first
// public release: rename display names freely, never the IDs or versions.
// Ranges, laws and their provenance: plugins/VOID/MODEL_SPEC.md, section 11.
namespace tleet::voidrev::params
{
    inline constexpr const char* algorithm  = "algorithm";
    inline constexpr const char* color      = "color";
    inline constexpr const char* preDelay   = "preDelay";
    inline constexpr const char* size       = "size";
    inline constexpr const char* decay      = "decay";
    inline constexpr const char* diffusion  = "diffusion";
    inline constexpr const char* drown      = "drown";
    inline constexpr const char* modRate    = "modRate";
    inline constexpr const char* modDepth   = "modDepth";
    inline constexpr const char* width      = "width";
    inline constexpr const char* hpf        = "hpf";
    inline constexpr const char* lpf        = "lpf";
    inline constexpr const char* filterMode = "filterMode";
    inline constexpr const char* duck       = "duck";
    inline constexpr const char* mix        = "mix";
    inline constexpr const char* freeze     = "freeze";
    inline constexpr const char* output     = "output";
    inline constexpr const char* bypass     = "bypass";

    inline constexpr int version = 1;

    // ---- discrete selectors: only real positions (addendum 4)
    enum class Algorithm { room = 0, plate, chamber, hall, voidAlgo };
    inline const juce::StringArray algorithmNames { "Room", "Plate", "Chamber", "Hall", "Void" };
    inline constexpr int numAlgorithms = 5;
    inline constexpr int algorithmDefault = (int) Algorithm::hall;

    enum class Color { clean = 0, dark, ruin };
    inline const juce::StringArray colorNames { "Clean", "Dark", "Ruin" };
    inline constexpr int numColors = 3;

    enum class FilterMode { pre = 0, loop, post };
    inline const juce::StringArray filterModeNames { "Pre", "Loop", "Post" };

    // ---- ranges and defaults (the registry the editor is built against)
    inline constexpr float preDelayMaxMs = 250.0f, preDelayCentreMs = 60.0f, preDelayDefaultMs = 20.0f;
    inline constexpr float sizeDefault = 50.0f;
    inline constexpr float decayMinS = 0.1f, decayMaxS = 60.0f, decayDefaultS = 2.5f;
    inline constexpr float diffusionDefault = 70.0f, drownDefault = 0.0f;
    inline constexpr float modRateMinHz = 0.01f, modRateMaxHz = 5.0f, modRateDefaultHz = 0.5f;
    inline constexpr float modDepthDefault = 30.0f;
    inline constexpr float widthMax = 200.0f, widthDefault = 100.0f;
    inline constexpr float hpfMinHz = 20.0f, hpfMaxHz = 1000.0f, lpfMinHz = 1000.0f, lpfMaxHz = 20000.0f;
    inline constexpr float duckDefault = 0.0f, mixDefault = 35.0f;
    inline constexpr float outputMaxDb = 12.0f;

    // DECAY at its maximum is infinite (MODEL_SPEC 6.3): the loop is lossless.
    inline bool isInfiniteDecay (float seconds) noexcept { return seconds >= decayMaxS * 0.9999f; }

    inline constexpr float dbStep = 0.1f;

    inline float snapDb (float lo, float hi, float v) noexcept
    {
        return juce::jlimit (lo, hi, std::round (v / dbStep) * dbStep);
    }

    // Piecewise-linear dB range with 0 dB at the knob centre, 0.1 dB legal
    // values with exact snapping (as the rest of the suite).
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
    // geometric mean (DECAY 2.45 s, MOD RATE 0.22 Hz, HPF 141 Hz, LPF 4.47 kHz).
    inline juce::NormalisableRange<float> logRange (float lo, float hi)
    {
        return juce::NormalisableRange<float> (
            lo, hi,
            [] (float a, float b, float n) { return a * std::pow (b / a, juce::jlimit (0.0f, 1.0f, n)); },
            [] (float a, float b, float v) { return std::log (juce::jlimit (a, b, v) / a) / std::log (b / a); },
            [] (float a, float b, float v) { return juce::jlimit (a, b, v); });
    }

    // PRE-DELAY: 0..250 ms, power law with 60 ms at the knob centre:
    // v = 250 ms * n^k, k = ln(60/250)/ln(0.5) = 2.0589.
    inline juce::NormalisableRange<float> preDelayRange()
    {
        juce::NormalisableRange<float> r (0.0f, preDelayMaxMs);
        r.setSkewForCentre (preDelayCentreMs);
        return r;
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

    // "42 ms"; below 10 ms one decimal ("4.5 ms"); "0 ms".
    inline juce::String msText (float ms)
    {
        if (ms < 9.95f)
        {
            const float r = std::round (ms * 10.0f) / 10.0f;
            return (std::abs (r - std::round (r)) < 1.0e-4f ? juce::String (juce::roundToInt (r)) : juce::String (r, 1)) + " ms";
        }
        return juce::String (juce::roundToInt (ms)) + " ms";
    }

    // "2.50 s", "12.5 s", "∞" at the maximum.
    inline juce::String decayText (float s)
    {
        if (isInfiniteDecay (s))
            return juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x9e"));
        if (s < 9.995f)
            return juce::String (s, 2) + " s";
        return juce::String (s, 1) + " s";
    }

    // "0.42 Hz", "5.00 Hz".
    inline juce::String rateText (float hz) { return juce::String (hz, 2) + " Hz"; }

    // "120 Hz" below 1 kHz, "8.5 kHz" from 1 kHz on.
    inline juce::String hzText (float hz)
    {
        if (std::round (hz) < 1000.0f)
            return juce::String (juce::roundToInt (hz)) + " Hz";
        return juce::String (hz / 1000.0f, 1) + " kHz";
    }

    inline float numberFromText (const juce::String& t) { return t.retainCharacters ("+-0123456789.").getFloatValue(); }

    // Accepts "42", "42 ms", "0.1 s".
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

    // Accepts "2.5", "2.5 s", "800 ms", "inf", "∞".
    inline float decayFromText (const juce::String& t)
    {
        const auto s = t.trim().toLowerCase();
        if (s.contains ("inf") || s.contains (juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x9e"))))
            return decayMaxS;
        const float v = numberFromText (s);
        return s.endsWith ("ms") ? v * 0.001f : v;
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
        auto pct = [&] { return floatAttr (percentText, numberFromText); };
        auto hz = [&] { return floatAttr (hzText, hzFromText); };
        auto percentRange = [] (float hi) { return NormalisableRange<float> (0.0f, hi, 0.1f); };
        auto addChoice = [&layout] (const char* id, const char* name, const StringArray& choices, int def)
        {
            layout.add (std::make_unique<AudioParameterChoice> (ParameterID { id, version }, name, choices, def));
        };
        auto addFloat = [&layout] (const char* id, const char* name, NormalisableRange<float> r, float def,
                                   AudioParameterFloatAttributes a)
        {
            layout.add (std::make_unique<AudioParameterFloat> (ParameterID { id, version }, name, r, def, a));
        };

        addChoice (algorithm, "Algorithm", algorithmNames, algorithmDefault);
        addChoice (color, "Color", colorNames, (int) Color::clean);
        addFloat (preDelay, "Pre-Delay", preDelayRange(), preDelayDefaultMs, floatAttr (msText, msFromText));
        addFloat (size, "Size", percentRange (100.0f), sizeDefault, pct());
        addFloat (decay, "Decay", logRange (decayMinS, decayMaxS), decayDefaultS, floatAttr (decayText, decayFromText));
        addFloat (diffusion, "Diffusion", percentRange (100.0f), diffusionDefault, pct());
        addFloat (drown, "Drown", percentRange (100.0f), drownDefault, pct());
        addFloat (modRate, "Mod Rate", logRange (modRateMinHz, modRateMaxHz), modRateDefaultHz, floatAttr (rateText, numberFromText));
        addFloat (modDepth, "Mod Depth", percentRange (100.0f), modDepthDefault, pct());
        addFloat (width, "Width", percentRange (widthMax), widthDefault, pct());
        addFloat (hpf, "HPF", logRange (hpfMinHz, hpfMaxHz), hpfMinHz, hz());
        addFloat (lpf, "LPF", logRange (lpfMinHz, lpfMaxHz), lpfMaxHz, hz());
        addChoice (filterMode, "Filter Mode", filterModeNames, (int) FilterMode::loop);
        addFloat (duck, "Duck", percentRange (100.0f), duckDefault, pct());
        addFloat (mix, "Mix", percentRange (100.0f), mixDefault, pct());
        layout.add (std::make_unique<AudioParameterBool> (ParameterID { freeze, version }, "Freeze", false));
        addFloat (output, "Output", centredDbRange (-outputMaxDb, outputMaxDb), 0.0f, floatAttr (dbText, numberFromText));
        layout.add (std::make_unique<AudioParameterBool> (ParameterID { bypass, version }, "Bypass", false));
        return layout;
    }
}
