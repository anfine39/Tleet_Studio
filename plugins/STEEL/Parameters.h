#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>
#include <cmath>
#include <memory>

// Central parameter registry for STEEL. IDs are immutable after the first
// public release: rename display names freely, never the IDs or versions.
// Ranges, laws and their provenance: plugins/STEEL/MODEL_SPEC.md, section 10.
namespace tleet::steel::params
{
    inline constexpr const char* input          = "input";
    inline constexpr const char* ceiling        = "ceiling";
    inline constexpr const char* mode           = "mode";
    inline constexpr const char* releaseMode    = "releaseMode";
    inline constexpr const char* release        = "release";
    inline constexpr const char* lookahead      = "lookahead";
    inline constexpr const char* truePeak       = "truePeak";
    inline constexpr const char* oversampling   = "oversampling";
    inline constexpr const char* stereoLink     = "stereoLink";
    inline constexpr const char* softClip       = "softClip";
    inline constexpr const char* softClipAmount = "softClipAmount";
    inline constexpr const char* subFilter      = "subFilter";
    inline constexpr const char* delta          = "delta";
    inline constexpr const char* levelMatch     = "levelMatch";
    inline constexpr const char* output         = "output";
    inline constexpr const char* bypass         = "bypass";

    inline constexpr int version = 1;

    // ---- discrete selectors: only real positions (addendum 4)
    enum class ModeChoice { clean = 0, punch, dense, steel };
    inline const juce::StringArray modeNames { "Clean", "Punch", "Dense", "Steel" };
    inline constexpr int modeDefault = (int) ModeChoice::steel;

    enum class ReleaseMode { autoRelease = 0, manual };
    inline const juce::StringArray releaseModeNames { "Auto", "Manual" };

    inline const juce::StringArray oversamplingNames { "1x", "2x", "4x", "8x" };
    inline constexpr int oversamplingDefault = 2; // 4x

    inline const juce::StringArray subFilterNames { "Off", "20", "25", "30" };
    inline constexpr float subFilterHz[4] { 0.0f, 20.0f, 25.0f, 30.0f };

    // ---- ranges and defaults (the registry the editor is built against)
    inline constexpr float inputMinDb = -12.0f, inputMaxDb = 24.0f;
    inline constexpr float ceilingMinDb = -12.0f, ceilingMaxDb = 0.0f, ceilingDefaultDb = -1.0f;
    inline constexpr float releaseMinMs = 20.0f, releaseMaxMs = 1000.0f, releaseCentreMs = 250.0f, releaseDefaultMs = 180.0f;
    inline constexpr float lookaheadMaxMs = 10.0f, lookaheadDefaultMs = 3.0f;
    inline constexpr float stereoLinkDefault = 100.0f, softClipAmountDefault = 30.0f;
    inline constexpr float outputMaxDb = 12.0f;
    inline constexpr float dbStep = 0.1f;

    inline float snapStep (float lo, float hi, float v) noexcept
    {
        return juce::jlimit (lo, hi, std::round (v / dbStep) * dbStep);
    }

    // Linear range with exact 0.1 steps (INPUT, CEILING, LOOKAHEAD).
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

    // RELEASE: 20..1000 ms, skewed so that the knob centre is exactly 250 ms
    // (the skin prints 20 / 250 / 1K): v = 20 + 980 n^k, k = ln(230/980)/ln(0.5).
    inline juce::NormalisableRange<float> releaseRange()
    {
        juce::NormalisableRange<float> r (releaseMinMs, releaseMaxMs);
        r.setSkewForCentre (releaseCentreMs);
        return r;
    }

    // ---- text (the skin's readouts print these strings verbatim)
    inline juce::String dbText (float v)
    {
        const float r = std::round (v * 10.0f) / 10.0f;
        return (r > 0.0f ? "+" : "") + juce::String (std::abs (r) < 0.05f ? 0.0f : r, 1) + " dB";
    }

    // "-1.0 dBTP" with TRUE PEAK on, "-1.0 dBFS" with it off (a sample-peak ceiling).
    inline juce::String ceilingText (float v, bool truePeakOn)
    {
        const float r = std::round (v * 10.0f) / 10.0f;
        return juce::String (std::abs (r) < 0.05f ? 0.0f : r, 1) + (truePeakOn ? " dBTP" : " dBFS");
    }

    inline juce::String percentText (float v)
    {
        const float r = std::round (v * 10.0f) / 10.0f;
        return (std::abs (r - std::round (r)) < 1.0e-4f ? juce::String (juce::roundToInt (r)) : juce::String (r, 1)) + " %";
    }

    inline juce::String releaseText (float ms) { return juce::String (juce::roundToInt (ms)) + " ms"; }

    inline juce::String lookaheadText (float ms) { return juce::String (std::round (ms * 10.0f) / 10.0f, 1) + " ms"; }

    inline float numberFromText (const juce::String& t) { return t.retainCharacters ("+-0123456789.").getFloatValue(); }

    // Accepts "180", "180 ms", "0.5 s", "1k".
    inline float msFromText (const juce::String& t)
    {
        const auto s = t.trim().toLowerCase();
        const float v = numberFromText (s);
        if (s.endsWith ("ms"))
            return v;
        if (s.endsWith ("s"))
            return v * 1000.0f;
        if (s.endsWith ("k"))
            return v * 1000.0f;
        return v;
    }

    // The CEILING text depends on TRUE PEAK, so the parameter's text function
    // reads the TRUE PEAK parameter through this per-instance context (filled
    // in by the processor after the tree exists).
    struct TextContext
    {
        std::atomic<std::atomic<float>*> truePeak { nullptr };
        bool truePeakOn() const noexcept
        {
            auto* p = truePeak.load (std::memory_order_acquire);
            return p == nullptr || p->load (std::memory_order_relaxed) > 0.5f;
        }
    };

    inline juce::AudioProcessorValueTreeState::ParameterLayout createLayout (std::shared_ptr<TextContext> ctx = std::make_shared<TextContext>())
    {
        using namespace juce;
        AudioProcessorValueTreeState::ParameterLayout layout;

        auto floatAttr = [] (std::function<String (float)> toText, std::function<float (const String&)> fromText)
        {
            return AudioParameterFloatAttributes()
                .withStringFromValueFunction ([toText] (float v, int) { return toText (v); })
                .withValueFromStringFunction ([fromText] (const String& t) { return fromText (t); });
        };
        auto addChoice = [&layout] (const char* id, const char* name, const StringArray& choices, int def)
        {
            layout.add (std::make_unique<AudioParameterChoice> (ParameterID { id, version }, name, choices, def));
        };
        auto addFloat = [&layout] (const char* id, const char* name, NormalisableRange<float> r, float def,
                                   AudioParameterFloatAttributes a)
        {
            layout.add (std::make_unique<AudioParameterFloat> (ParameterID { id, version }, name, r, def, a));
        };
        auto addBool = [&layout] (const char* id, const char* name, bool def)
        {
            layout.add (std::make_unique<AudioParameterBool> (ParameterID { id, version }, name, def));
        };
        auto pct = [&] { return floatAttr (percentText, numberFromText); };
        auto percentRange = [] { return NormalisableRange<float> (0.0f, 100.0f, 0.1f); };

        addFloat (input, "Input", steppedRange (inputMinDb, inputMaxDb), 0.0f, floatAttr (dbText, numberFromText));
        addFloat (ceiling, "Ceiling", steppedRange (ceilingMinDb, ceilingMaxDb), ceilingDefaultDb,
                  floatAttr ([ctx] (float v) { return ceilingText (v, ctx->truePeakOn()); }, numberFromText));
        addChoice (mode, "Mode", modeNames, modeDefault);
        addChoice (releaseMode, "Release Mode", releaseModeNames, (int) ReleaseMode::autoRelease);
        addFloat (release, "Release", releaseRange(), releaseDefaultMs, floatAttr (releaseText, msFromText));
        addFloat (lookahead, "Lookahead", steppedRange (0.0f, lookaheadMaxMs), lookaheadDefaultMs, floatAttr (lookaheadText, msFromText));
        addBool (truePeak, "True Peak", true);
        addChoice (oversampling, "Oversampling", oversamplingNames, oversamplingDefault);
        addFloat (stereoLink, "Stereo Link", percentRange(), stereoLinkDefault, pct());
        addBool (softClip, "Soft Clip", false);
        addFloat (softClipAmount, "Soft Clip Amount", percentRange(), softClipAmountDefault, pct());
        addChoice (subFilter, "Sub Filter", subFilterNames, 0);
        addBool (delta, "Delta", false);
        addBool (levelMatch, "Level Match", false);
        addFloat (output, "Output", centredDbRange (-outputMaxDb, outputMaxDb), 0.0f, floatAttr (dbText, numberFromText));
        addBool (bypass, "Bypass", false);
        return layout;
    }
}
