#include "TestFramework.h"
#include "TestSignals.h"
#include <cstring>
#include <map>
#include <set>
#include "../plugins/DOUBLE/PluginProcessor.h"
#include "../plugins/DOUBLE/Parameters.h"
#include "../plugins/DOUBLE/dsp/WetFilter.h"
#include "../plugins/DOUBLE/dsp/VoiceTemplates.h"

// DSP tests for DOUBLE (plugins/DOUBLE/MODEL_SPEC.md). Measurements are
// printed and written to test-output/ (DOUBLE_*.csv).

using namespace tleet;
using namespace tleet::test;
using tleet::dbl::DoubleProcessor;
using tleet::dbl::DoubleEngine;
using tleet::dbl::DoubleTelemetry;
namespace tpl = tleet::dbl::templates;

namespace
{
    enum ModeIdx { mTight = 0, mWide, mGhost };
    enum VoicesIdx { v1 = 0, v2, v4 };
    const char* modeName (int m) { return m == mTight ? "TIGHT" : m == mWide ? "WIDE" : "GHOST"; }

    void setParam (DoubleProcessor& p, const char* id, float plain)
    {
        auto* r = p.apvts.getParameter (id);
        r->setValueNotifyingHost (r->convertTo0to1 (plain));
    }

    float plainValue (DoubleProcessor& p, const char* id)
    {
        auto* r = p.apvts.getParameter (id);
        return r->convertFrom0to1 (r->getValue());
    }

    bool sameBits (float a, float b) noexcept { return std::memcmp (&a, &b, sizeof (float)) == 0; }

    std::unique_ptr<DoubleProcessor> makeDouble (double fs, int block, int channels = 2)
    {
        auto p = std::make_unique<DoubleProcessor>();
        const auto set = channels == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo();
        DoubleProcessor::BusesLayout layout;
        layout.inputBuses.add (set);
        layout.outputBuses.add (set);
        p->setBusesLayout (layout);
        p->setRateAndBufferSizeDetails (fs, block);
        p->prepareToPlay (fs, block);
        return p;
    }

    juce::AudioBuffer<float> render (DoubleProcessor& p, const juce::AudioBuffer<float>& input, int block)
    {
        juce::AudioBuffer<float> out (input);
        juce::MidiBuffer midi;
        for (int pos = 0; pos < out.getNumSamples(); pos += block)
        {
            const int n = std::min (block, out.getNumSamples() - pos);
            juce::AudioBuffer<float> view (out.getArrayOfWritePointers(), out.getNumChannels(), pos, n);
            p.processBlock (view, midi);
        }
        return out;
    }

    juce::AudioBuffer<float> toBuffer (const std::vector<float>& mono, int channels)
    {
        juce::AudioBuffer<float> b (channels, (int) mono.size());
        for (int ch = 0; ch < channels; ++ch)
            b.copyFrom (ch, 0, mono.data(), (int) mono.size());
        return b;
    }

    // Deterministic pink-ish noise (Paul Kellet's economy filter on white noise).
    std::vector<float> pinkNoise (int n, int seed, float amp = 0.25f)
    {
        juce::Random rng (seed);
        std::vector<float> v ((size_t) n);
        float b0 = 0, b1 = 0, b2 = 0;
        for (int i = 0; i < n; ++i)
        {
            const float w = rng.nextFloat() * 2.0f - 1.0f;
            b0 = 0.99765f * b0 + w * 0.0990460f;
            b1 = 0.96300f * b1 + w * 0.2965164f;
            b2 = 0.57000f * b2 + w * 1.0526913f;
            v[(size_t) i] = amp * 0.3f * (b0 + b1 + b2 + w * 0.1848f);
        }
        return v;
    }

    std::vector<float> whiteNoise (int n, int seed, float amp = 0.25f)
    {
        juce::Random rng (seed);
        std::vector<float> v ((size_t) n);
        for (auto& x : v)
            x = amp * (rng.nextFloat() * 2.0f - 1.0f);
        return v;
    }

    // Mean frequency from linearly interpolated rising zero crossings.
    double zeroCrossingHz (const float* d, int n, double fs)
    {
        double first = -1, last = -1;
        int count = 0;
        for (int i = 1; i < n; ++i)
            if (d[i - 1] < 0.0f && d[i] >= 0.0f)
            {
                const double t = (i - 1) + d[i - 1] / (double) (d[i - 1] - d[i]);
                if (first < 0) first = t;
                last = t;
                ++count;
            }
        return count > 1 ? (count - 1) * fs / (last - first) : 0.0;
    }

    double cents (double f, double ref) { return 1200.0 * std::log2 (f / ref); }

    // Exact least-squares fit of a sinusoid at `hz`: amplitude and phase.
    struct Fit { double amp, phase; };
    Fit sineFit (const float* d, int n, double fs, double hz, int offset = 0)
    {
        double ss = 0, cc = 0, sc = 0, ys = 0, yc = 0;
        const double w = 2.0 * juce::MathConstants<double>::pi * hz / fs;
        for (int i = 0; i < n; ++i)
        {
            const double s = std::sin (w * (i + offset)), c = std::cos (w * (i + offset));
            ss += s * s; cc += c * c; sc += s * c;
            ys += d[i] * s; yc += d[i] * c;
        }
        const double det = ss * cc - sc * sc;
        const double a = (ys * cc - yc * sc) / det, b = (yc * ss - ys * sc) / det;
        return { std::sqrt (a * a + b * b), std::atan2 (b, a) };
    }

    double powerOf (const float* d, int n)
    {
        double s = 0;
        for (int i = 0; i < n; ++i) s += (double) d[i] * d[i];
        return s / std::max (1, n);
    }

    double levelDb (const juce::AudioBuffer<float>& b, int start, int n)
    {
        double s = 0;
        for (int ch = 0; ch < b.getNumChannels(); ++ch)
            s += powerOf (b.getReadPointer (ch, start), n);
        return 10.0 * std::log10 (std::max (1.0e-20, s / b.getNumChannels()));
    }

    double correlation (const float* a, const float* b, int n)
    {
        double ab = 0, aa = 0, bb = 0;
        for (int i = 0; i < n; ++i) { ab += (double) a[i] * b[i]; aa += (double) a[i] * a[i]; bb += (double) b[i] * b[i]; }
        return ab / std::sqrt (aa * bb + 1.0e-30);
    }

    // Level of the mono fold-down M = (L + R) / 2 relative to the mean channel level (dB).
    double monoLossDb (const juce::AudioBuffer<float>& b, int start, int n)
    {
        double m = 0, l = 0, r = 0;
        for (int i = start; i < start + n; ++i)
        {
            const double x = b.getSample (0, i), y = b.getSample (1, i);
            m += 0.25 * (x + y) * (x + y);
            l += x * x;
            r += y * y;
        }
        return 10.0 * std::log10 (m / (0.5 * (l + r)));
    }

    // Wet only, no humanize, filters open, no Mono Safe, Auto Level on.
    void neutral (DoubleProcessor& p)
    {
        setParam (p, "mix", 100.0f);
        setParam (p, "humanize", 0.0f);
        setParam (p, "hpf", 20.0f);
        setParam (p, "lpf", 20000.0f);
        setParam (p, "monoSafe", 0.0f);
        setParam (p, "autoLevel", 1.0f);
        setParam (p, "output", 0.0f);
    }

    struct Setup
    {
        int voices = v4, mode = mWide;
        float detune = 8.0f, delay = 15.0f, humanize = 0.0f, width = 100.0f;
    };

    std::unique_ptr<DoubleProcessor> makeSetup (const Setup& s, double fs = 48000.0, int block = 256, int channels = 2)
    {
        auto p = makeDouble (fs, block, channels);
        neutral (*p);
        setParam (*p, "voices", (float) s.voices);
        setParam (*p, "mode", (float) s.mode);
        setParam (*p, "detune", s.detune);
        setParam (*p, "delay", s.delay);
        setParam (*p, "humanize", s.humanize);
        setParam (*p, "width", s.width);
        p->prepareToPlay (fs, block); // start settled on these settings
        return p;
    }
}

// ============================================================================ registry

TEST ("DOUBLE: parameter IDs, ranges and defaults match the registry")
{
    DoubleProcessor p;
    const char* ids[] = { "voices", "mode", "detune", "delay", "humanize", "width", "hpf", "lpf",
                          "monoSafe", "autoLevel", "mix", "output", "bypass" };
    for (auto* id : ids)
        CHECK_MSG (p.apvts.getParameter (id) != nullptr, id);
    CHECK (p.getParameters().size() == 13);

    auto def = [&p] (const char* id) { auto* r = p.apvts.getParameter (id); return r->convertFrom0to1 (r->getDefaultValue()); };
    auto lo = [&p] (const char* id) { return p.apvts.getParameter (id)->convertFrom0to1 (0.0f); };
    auto hi = [&p] (const char* id) { return p.apvts.getParameter (id)->convertFrom0to1 (1.0f); };

    CHECK_NEAR (def ("voices"), 1.0f, 1e-4);   // "2"
    CHECK_NEAR (def ("mode"), 0.0f, 1e-4);     // TIGHT
    CHECK_NEAR (def ("detune"), 8.0f, 1e-3);
    CHECK_NEAR (def ("delay"), 15.0f, 1e-3);
    CHECK_NEAR (def ("humanize"), 35.0f, 1e-3);
    CHECK_NEAR (def ("width"), 100.0f, 1e-3);
    CHECK_NEAR (def ("hpf"), 20.0f, 1e-2);
    CHECK_NEAR (def ("lpf"), 20000.0f, 1.0);
    CHECK_NEAR (def ("monoSafe"), 1.0f, 1e-4);
    CHECK_NEAR (def ("autoLevel"), 1.0f, 1e-4);
    CHECK_NEAR (def ("mix"), 50.0f, 1e-3);
    CHECK_NEAR (def ("output"), 0.0f, 1e-4);
    CHECK_NEAR (def ("bypass"), 0.0f, 1e-4);

    CHECK_NEAR (lo ("detune"), -25.0f, 1e-4);  CHECK_NEAR (hi ("detune"), 25.0f, 1e-4);
    CHECK_NEAR (lo ("delay"), 0.0f, 1e-4);     CHECK_NEAR (hi ("delay"), 40.0f, 1e-4);
    CHECK_NEAR (lo ("humanize"), 0.0f, 1e-4);  CHECK_NEAR (hi ("humanize"), 100.0f, 1e-4);
    CHECK_NEAR (lo ("width"), 0.0f, 1e-4);     CHECK_NEAR (hi ("width"), 200.0f, 1e-4);
    CHECK_NEAR (lo ("hpf"), 20.0f, 1e-3);      CHECK_NEAR (hi ("hpf"), 1000.0f, 1e-2);
    CHECK_NEAR (lo ("lpf"), 1000.0f, 1e-2);    CHECK_NEAR (hi ("lpf"), 20000.0f, 0.1);
    CHECK_NEAR (lo ("mix"), 0.0f, 1e-4);       CHECK_NEAR (hi ("mix"), 100.0f, 1e-4);
    CHECK_NEAR (lo ("output"), -12.0f, 1e-4);  CHECK_NEAR (hi ("output"), 12.0f, 1e-4);
    // filters are log-tapered: the knob centre is the geometric mean
    CHECK_NEAR (p.apvts.getParameter ("hpf")->convertFrom0to1 (0.5f), std::sqrt (20.0f * 1000.0f), 0.5);
    CHECK_NEAR (p.apvts.getParameter ("lpf")->convertFrom0to1 (0.5f), std::sqrt (1000.0f * 20000.0f), 5.0);

    auto* voices = dynamic_cast<juce::AudioParameterChoice*> (p.apvts.getParameter ("voices"));
    auto* mode = dynamic_cast<juce::AudioParameterChoice*> (p.apvts.getParameter ("mode"));
    CHECK (voices != nullptr && voices->choices == juce::StringArray ({ "1", "2", "4" }));
    CHECK (mode != nullptr && mode->choices == juce::StringArray ({ "Tight", "Wide", "Ghost" }));
    CHECK (dbl::params::voiceCount (0) == 1 && dbl::params::voiceCount (1) == 2 && dbl::params::voiceCount (2) == 4);
    CHECK (p.getLatencySamples() == 0);
}

// ============================================================================ robustness

TEST ("DOUBLE: all sample rates and block sizes, mono and stereo: finite output, no steady-state allocation")
{
    for (double fs : { 44100.0, 48000.0, 88200.0, 96000.0, 192000.0 })
        for (int block : { 1, 16, 32, 64, 128, 256, 512, 1024, 2048 })
            for (int channels : { 1, 2 })
            {
                // the busiest settings: 4 voices, GHOST, max detune (splices every ~0.5 s), full humanize
                Setup s;
                s.voices = v4; s.mode = mGhost; s.detune = 25.0f; s.delay = 40.0f; s.humanize = 100.0f; s.width = 200.0f;
                auto p = makeSetup (s, fs, block, channels);
                setParam (*p, "monoSafe", channels == 2 ? 1.0f : 0.0f);
                setParam (*p, "mix", 60.0f);
                const int n = std::max (block * 4, (int) (fs * 1.2));
                auto in = toBuffer (sine (fs, 220.0, 0.5f, n), channels);
                render (*p, in, block); // warm-up (first splices, all buffers touched)
                const int splicesBefore = p->getEngine().getVoiceInfo (0).splices;

                juce::AudioBuffer<float> out (in);
                juce::MidiBuffer midi;
                long allocs;
                {
                    ScopedAllocationCounter counter;
                    for (int pos = 0; pos < out.getNumSamples(); pos += block)
                    {
                        const int len = std::min (block, out.getNumSamples() - pos);
                        juce::AudioBuffer<float> view (out.getArrayOfWritePointers(), channels, pos, len);
                        p->processBlock (view, midi);
                    }
                    allocs = counter.count();
                }
                const auto tag = juce::String (fs) + " Hz, block " + juce::String (block) + ", " + juce::String (channels) + " ch";
                CHECK_MSG (allFinite (out), tag);
                CHECK_MSG (allocs == 0, tag + ": " + juce::String (allocs) + " allocations in processBlock");
                CHECK_MSG (rms (out.getReadPointer (0), out.getNumSamples()) > 0.05f, tag + ": no output");
                CHECK_MSG (p->getEngine().getVoiceInfo (0).splices > splicesBefore, tag + ": no splice inside the counted run");
            }
}

TEST ("DOUBLE: NaN/Inf input never reaches the output or poisons the state")
{
    Setup s;
    s.mode = mGhost; s.humanize = 60.0f;
    auto p = makeSetup (s);
    setParam (*p, "mix", 50.0f);
    auto in = toBuffer (sine (48000.0, 440.0, 0.5f, 9600), 2);
    in.setSample (0, 100, std::numeric_limits<float>::quiet_NaN());
    in.setSample (1, 200, std::numeric_limits<float>::infinity());
    in.setSample (0, 300, -std::numeric_limits<float>::infinity());
    auto out = render (*p, in, 256);
    CHECK (allFinite (out));
    auto after = render (*p, toBuffer (sine (48000.0, 440.0, 0.5f, 9600), 2), 256);
    CHECK (allFinite (after));
    CHECK (rms (after.getReadPointer (0) + 4800, 4800) > 0.1f);
    for (int i = 0; i < 4; ++i)
    {
        const auto t = p->telemetry.voice (i);
        CHECK (std::isfinite (t.pan) && std::isfinite (t.delayMs) && std::isfinite (t.detuneCents) && std::isfinite (t.gain));
    }
}

TEST ("DOUBLE: silence gives silence; DC passes only on the dry side (the wet high-pass removes it); denormal input stays clean")
{
    Setup s;
    s.mode = mGhost; s.humanize = 100.0f; s.detune = 25.0f;
    auto p = makeSetup (s);
    juce::AudioBuffer<float> silence (2, 48000);
    silence.clear();
    auto out = render (*p, silence, 512);
    CHECK (out.getMagnitude (0, out.getNumSamples()) == 0.0f);

    // DC: the wet HPF (20 Hz minimum) removes it; Mix 50 leaves half of the dry DC.
    for (float mix : { 100.0f, 50.0f })
    {
        auto q = makeSetup (s);
        setParam (*q, "mix", mix);
        juce::AudioBuffer<float> dc (2, 96000);
        for (int ch = 0; ch < 2; ++ch)
            juce::FloatVectorOperations::fill (dc.getWritePointer (ch), 0.25f, dc.getNumSamples());
        out = render (*q, dc, 512);
        double mean = 0;
        for (int i = 48000; i < 96000; ++i) mean += out.getSample (0, i);
        mean /= 48000.0;
        MEASURE ("DC out (Mix " + juce::String (mix) + "), 0.25 DC in", mean);
        CHECK_NEAR (mean, 0.25 * (1.0 - mix / 100.0), 1.0e-3);
        CHECK (allFinite (out));
    }

    auto fresh = makeSetup (s);
    setParam (*fresh, "output", 12.0f);
    juce::AudioBuffer<float> tiny (2, 48000);
    for (int i = 0; i < tiny.getNumSamples(); ++i)
        for (int ch = 0; ch < 2; ++ch)
            tiny.setSample (ch, i, (i & 1) ? 1.0e-38f : -1.0e-38f);
    out = render (*fresh, tiny, 512);
    CHECK (allFinite (out));
    CHECK (out.getMagnitude (0, out.getNumSamples()) < 1.0e-30f);
}

// ============================================================================ latency, mix, bypass

TEST ("DOUBLE: zero latency reported and measured; Mix 0 and Bypass are bit-exact dry")
{
    for (double fs : { 44100.0, 96000.0 })
    {
        // Delay 0, Detune 0, Humanize 0, one centred voice: the wet IS the input
        // through the wet filters (HPF 20 Hz / LPF 20 kHz), with no added delay.
        Setup s;
        s.voices = v1; s.mode = mTight; s.detune = 0.0f; s.delay = 0.0f; s.width = 0.0f;
        auto p = makeSetup (s, fs, 128);
        CHECK (p->getLatencySamples() == 0);
        juce::AudioBuffer<float> imp (2, 4096);
        imp.clear();
        imp.setSample (0, 100, 0.5f);
        imp.setSample (1, 100, 0.5f);
        auto out = render (*p, imp, 128);
        int onset = -1;
        for (int i = 0; i < out.getNumSamples() && onset < 0; ++i)
            if (out.getSample (0, i) != 0.0f) onset = i;

        // reference: the same impulse through a stand-alone WetFilter (same class, same settings)
        dbl::WetFilter ref;
        ref.setTargets (20.0, 20000.0, 1.0);
        ref.prepare (fs);
        std::vector<float> refIr ((size_t) imp.getNumSamples());
        ref.process (imp.getReadPointer (0), refIr.data(), imp.getNumSamples());
        double maxDiff = 0;
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < out.getNumSamples(); ++i)
                maxDiff = std::max (maxDiff, (double) std::abs (out.getSample (ch, i) - refIr[(size_t) i]));
        MEASURE ("impulse onset re input, Mix 100, Delay 0 @" + juce::String (fs) + " (samples)", onset - 100);
        MEASURE ("max |wet - wet filter alone| (impulse 0.5)", maxDiff);
        CHECK (onset == 100);
        CHECK (maxDiff < 1.0e-6);

        // Mix 0: bit-exact dry while four detuned voices run.
        Setup busy;
        busy.mode = mGhost; busy.detune = 25.0f; busy.humanize = 100.0f; busy.width = 200.0f;
        auto q = makeSetup (busy, fs, 128);
        setParam (*q, "mix", 0.0f);
        q->prepareToPlay (fs, 128);
        auto sig = toBuffer (pinkNoise (16384, 3), 2);
        sig.applyGain (1, 0, sig.getNumSamples(), 0.7f); // asymmetric stereo
        out = render (*q, sig, 128);
        bool exact = true;
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < sig.getNumSamples(); ++i)
                exact = exact && sameBits (out.getSample (ch, i), sig.getSample (ch, i));
        CHECK_MSG (exact, "Mix=0 is not bit-exact dry");

        setParam (*q, "mix", 100.0f);
        setParam (*q, "output", 9.0f);
        setParam (*q, "bypass", 1.0f);
        q->prepareToPlay (fs, 128);
        out = render (*q, sig, 128);
        exact = true;
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < sig.getNumSamples(); ++i)
                exact = exact && sameBits (out.getSample (ch, i), sig.getSample (ch, i));
        CHECK_MSG (exact, "Bypass is not bit-exact dry");
    }
}

// ============================================================================ pitch

TEST ("DOUBLE: every voice's pitch matches its template in cents (all modes, both signs, splices included)")
{
    const double fs = 48000.0, f0 = 1000.0;
    juce::String csv ("mode,detune_knob,slot,expected_cents,measured_cents,error_cents,telemetry_cents,splices\n");
    double maxErr = 0, maxTelErr = 0;
    int splicesSeen = 0;
    for (int mode : { mTight, mWide, mGhost })
        for (float d : { -25.0f, -12.5f, -3.0f, 0.5f, 8.0f, 25.0f })
            for (int slot = 0; slot < 4; ++slot)
            {
                Setup s;
                s.voices = v4; s.mode = mode; s.detune = d; s.delay = 15.0f; s.width = 0.0f;
                auto p = makeSetup (s);
                p->setSoloVoiceForTesting (slot);
                auto out = render (*p, toBuffer (sine (fs, f0, 0.5f, (int) (fs * 3.5)), 2), 256);
                const int n = (int) (fs * 3.0);
                const double f = zeroCrossingHz (out.getReadPointer (0) + out.getNumSamples() - n, n, fs);
                const double expected = tpl::voiceDetuneCents (d, mode, slot);
                const auto info = p->getEngine().getVoiceInfo (slot);
                const double err = cents (f, f0) - expected;
                maxErr = std::max (maxErr, std::abs (err));
                maxTelErr = std::max (maxTelErr, std::abs ((double) p->telemetry.voice (slot).detuneCents - expected));
                splicesSeen += info.splices;
                csv << modeName (mode) << "," << d << "," << slot << "," << expected << "," << cents (f, f0) << ","
                    << err << "," << p->telemetry.voice (slot).detuneCents << "," << info.splices << "\n";
                CHECK_MSG (std::abs (err) < 0.01, juce::String (modeName (mode)) + " detune " + juce::String (d) + " slot " + juce::String (slot)
                                                      + ": error " + juce::String (err, 5) + " cents");
            }
    outputDirectory().getChildFile ("DOUBLE_pitch_accuracy.csv").replaceWithText (csv);
    MEASURE ("max pitch error over 72 voices (cents)", maxErr);
    MEASURE ("max telemetry detune error (cents)", maxTelErr);
    MEASURE ("splices inside the measured runs", splicesSeen);
    CHECK (maxTelErr < 0.01);
    CHECK (splicesSeen > 50);

    // Sign: positive Detune puts the sharp voices on the left, negative on the right.
    for (float d : { 10.0f, -10.0f })
    {
        Setup s;
        s.voices = v2; s.mode = mWide; s.detune = d; s.width = 200.0f;
        auto p = makeSetup (s);
        auto out = render (*p, toBuffer (sine (fs, f0, 0.5f, (int) (fs * 2.5)), 2), 256);
        const int n = (int) (fs * 2.0);
        const double fl = zeroCrossingHz (out.getReadPointer (0) + out.getNumSamples() - n, n, fs);
        const double fr = zeroCrossingHz (out.getReadPointer (1) + out.getNumSamples() - n, n, fs);
        MEASURE ("Detune " + juce::String (d) + ": left voice (cents)", cents (fl, f0));
        MEASURE ("Detune " + juce::String (d) + ": right voice (cents)", cents (fr, f0));
        CHECK_NEAR (cents (fl, f0), d, 0.01);
        CHECK_NEAR (cents (fr, f0), -d, 0.01);
    }
}

TEST ("DOUBLE: splices are seamless on tones: steady level, continuous phase, no spatter")
{
    const double fs = 48000.0;
    juce::String csv ("signal,detune_cents,splices,level_ripple_db,max_phase_jump_deg,residual_db\n");
    for (double f0 : { 110.0, 220.0, 1000.0, 3150.0 })
        for (float d : { 25.0f, -25.0f })
        {
            // one voice at +-25 cents (WIDE: the knob value), centred
            Setup s;
            s.voices = v1; s.mode = mWide; s.detune = d; s.delay = 20.0f; s.width = 0.0f;
            auto p = makeSetup (s);
            const int total = (int) (fs * 6.0);
            auto out = render (*p, toBuffer (sine (fs, f0, 0.5f, total), 2), 256);
            const auto info = p->getEngine().getVoiceInfo (0);
            const double fout = f0 * std::pow (2.0, d / 1200.0);

            // 10 ms windows: amplitude and phase against the expected tone
            const int w = (int) (0.01 * fs), start = (int) (0.5 * fs);
            double ampMin = 1e9, ampMax = 0, maxJump = 0, prevPhase = 0;
            bool first = true;
            for (int pos = start; pos + w <= total; pos += w)
            {
                const auto fit = sineFit (out.getReadPointer (0) + pos, w, fs, fout, pos);
                ampMin = std::min (ampMin, fit.amp);
                ampMax = std::max (ampMax, fit.amp);
                if (! first)
                {
                    double dp = fit.phase - prevPhase;
                    while (dp > juce::MathConstants<double>::pi) dp -= 2 * juce::MathConstants<double>::pi;
                    while (dp < -juce::MathConstants<double>::pi) dp += 2 * juce::MathConstants<double>::pi;
                    maxJump = std::max (maxJump, std::abs (dp));
                }
                prevPhase = fit.phase;
                first = false;
            }
            // residual after removing the ideal shifted tone, over the last 5 s
            const int n = total - start;
            const auto fit = sineFit (out.getReadPointer (0) + start, n, fs, fout, start);
            double res = 0, sig = 0;
            const double wv = 2.0 * juce::MathConstants<double>::pi * fout / fs;
            for (int i = 0; i < n; ++i)
            {
                const double ideal = fit.amp * std::sin (wv * (i + start) + fit.phase);
                const double e = out.getSample (0, start + i) - ideal;
                res += e * e;
                sig += ideal * ideal;
            }
            const double rippleDb = 20.0 * std::log10 (ampMax / ampMin);
            const double residualDb = 10.0 * std::log10 (res / sig);
            const auto tag = juce::String (f0) + " Hz, " + juce::String (d) + " cents";
            MEASURE (tag + ": splices", info.splices);
            MEASURE (tag + ": level ripple (dB)", rippleDb);
            MEASURE (tag + ": max phase jump between 10 ms windows (deg)", maxJump * 180.0 / juce::MathConstants<double>::pi);
            MEASURE (tag + ": residual vs ideal shifted tone (dB)", residualDb);
            csv << f0 << " Hz sine," << d << "," << info.splices << "," << rippleDb << "," << maxJump * 180.0 / juce::MathConstants<double>::pi << "," << residualDb << "\n";
            CHECK (info.splices >= 2);
            CHECK_MSG (rippleDb < 0.1, tag);
            CHECK_MSG (maxJump * 180.0 / juce::MathConstants<double>::pi < 1.0, tag);
            CHECK_MSG (residualDb < -50.0, tag);
        }

    // A harmonic tone (voice-like, 3 partials): short-time level stays steady across splices.
    {
        Setup s;
        s.voices = v1; s.mode = mWide; s.detune = 25.0f; s.delay = 20.0f; s.width = 0.0f;
        auto p = makeSetup (s);
        const int total = (int) (fs * 6.0);
        std::vector<float> tone ((size_t) total);
        for (int i = 0; i < total; ++i)
        {
            const double ph = 2.0 * juce::MathConstants<double>::pi * 196.0 * i / fs;
            tone[(size_t) i] = (float) (0.3 * std::sin (ph) + 0.15 * std::sin (2 * ph + 0.3) + 0.08 * std::sin (3 * ph + 1.1));
        }
        auto out = render (*p, toBuffer (tone, 2), 256);
        const int w = (int) (0.02 * fs);
        double lo = 1e9, hi = 0;
        for (int pos = (int) (0.5 * fs); pos + w <= total; pos += w / 2)
        {
            const double r = std::sqrt (powerOf (out.getReadPointer (0) + pos, w));
            lo = std::min (lo, r);
            hi = std::max (hi, r);
        }
        const double ripple = 20.0 * std::log10 (hi / lo);
        MEASURE ("196 Hz harmonic tone, +25 cents: splices", p->getEngine().getVoiceInfo (0).splices);
        MEASURE ("196 Hz harmonic tone, +25 cents: 20 ms level ripple (dB)", ripple);
        csv << "196 Hz harmonic tone,25," << p->getEngine().getVoiceInfo (0).splices << "," << ripple << ",,\n";
        CHECK (ripple < 0.3);
    }
    outputDirectory().getChildFile ("DOUBLE_splices.csv").replaceWithText (csv);
}

// ============================================================================ delay

TEST ("DOUBLE: every voice's delay matches its template (impulse, Detune 0)")
{
    // Each voice's impulse response is the wet-filter response, delayed by the
    // voice's delay. The delay is read from the phase of H_voice(f) / H_ref(f)
    // (H_ref: the same impulse through a stand-alone WetFilter), 200 Hz - 4 kHz.
    const double fs = 48000.0;
    const int len = 32768, at = 1000;
    juce::AudioBuffer<float> imp (2, len);
    imp.clear();
    imp.setSample (0, at, 1.0f);
    imp.setSample (1, at, 1.0f);
    dbl::WetFilter ref;
    ref.setTargets (20.0, 20000.0, 1.0);
    ref.prepare (fs);
    std::vector<float> refIr ((size_t) len);
    ref.process (imp.getReadPointer (0), refIr.data(), len);

    auto dft = [&] (const float* x, double hz)
    {
        const std::complex<double> step = std::polar (1.0, -2.0 * juce::MathConstants<double>::pi * hz / fs);
        std::complex<double> ph (1.0, 0.0), acc;
        for (int i = 0; i < len; ++i, ph *= step)
            acc += (double) x[i] * ph;
        return acc;
    };

    juce::String csv ("mode,delay_knob,slot,expected_ms,measured_ms,error_ms,telemetry_ms\n");
    double maxErr = 0, maxTel = 0;
    for (int mode : { mTight, mWide })
        for (float d : { 0.0f, 2.5f, 13.3f, 27.7f, 40.0f })
            for (int slot = 0; slot < 4; ++slot)
            {
                Setup s;
                s.voices = v4; s.mode = mode; s.detune = 0.0f; s.delay = d; s.width = 0.0f;
                auto p = makeSetup (s);
                p->setSoloVoiceForTesting (slot);
                auto out = render (*p, imp, 256);
                const double expected = tpl::voiceDelayMs (d, mode, slot);
                // least-squares slope of the unwrapped phase through the origin
                double num = 0, den = 0;
                for (double hz = 200.0; hz <= 4000.0; hz += 100.0)
                {
                    const auto ratio = dft (out.getReadPointer (0), hz) / dft (refIr.data(), hz);
                    double phi = std::arg (ratio);
                    const double w = 2.0 * juce::MathConstants<double>::pi * hz / fs;
                    const double predicted = -w * expected * fs / 1000.0; // unwrap near the prediction
                    phi += 2.0 * juce::MathConstants<double>::pi * std::round ((predicted - phi) / (2.0 * juce::MathConstants<double>::pi));
                    num += -phi * w;
                    den += w * w;
                }
                const double measured = num / den * 1000.0 / fs;
                const double telemetry = p->telemetry.voice (slot).delayMs;
                const double err = measured - expected;
                maxErr = std::max (maxErr, std::abs (err));
                maxTel = std::max (maxTel, std::abs (telemetry - expected));
                csv << modeName (mode) << "," << d << "," << slot << "," << expected << "," << measured << "," << err << "," << telemetry << "\n";
                CHECK_MSG (std::abs (err) * fs / 1000.0 < 0.02, juce::String (modeName (mode)) + " delay " + juce::String (d) + " slot " + juce::String (slot)
                                                                  + ": " + juce::String (measured, 5) + " ms vs " + juce::String (expected, 5));
                CHECK_NEAR (telemetry, expected, 1.0e-4);
            }
    outputDirectory().getChildFile ("DOUBLE_delay_accuracy.csv").replaceWithText (csv);
    MEASURE ("max delay error, 40 voice/setting pairs (ms)", maxErr);
    MEASURE ("max delay error (samples @ 48 kHz)", maxErr * 48.0);
    MEASURE ("max |telemetry - template| delay (ms)", maxTel);

    // GHOST (diffused, so not a pure delay): telemetry, and the onset of the response
    // (within the 12-tap interpolator's half length of the template delay)
    for (int slot = 0; slot < 4; ++slot)
    {
        Setup s;
        s.voices = v4; s.mode = mGhost; s.detune = 0.0f; s.delay = 20.0f; s.width = 0.0f;
        auto p = makeSetup (s);
        p->setSoloVoiceForTesting (slot);
        auto out = render (*p, imp, 256);
        int onset = -1;
        for (int i = 0; i < len && onset < 0; ++i)
            if (std::abs (out.getSample (0, i)) > 1.0e-3f) onset = i;
        const double expected = tpl::voiceDelayMs (20.0f, mGhost, slot);
        MEASURE ("GHOST Delay 20, slot " + juce::String (slot) + ": template / onset (ms)", expected);
        MEASURE ("                                         ", (onset - at) * 1000.0 / fs);
        CHECK_NEAR (p->telemetry.voice (slot).delayMs, expected, 1.0e-4);
        CHECK (std::abs ((onset - at) - expected * fs / 1000.0) <= 7.0);
    }
}

TEST ("DOUBLE: a detuned voice's read point stays in its micro-pitch window around the centre delay")
{
    const double fs = 48000.0;
    const int block = 480;
    for (float d : { 25.0f, -25.0f, 4.0f })
    {
        Setup s;
        s.voices = v1; s.mode = mWide; s.detune = d; s.delay = 20.0f; s.width = 0.0f;
        auto p = makeSetup (s, fs, block);
        auto in = toBuffer (whiteNoise ((int) (fs * 6), 11), 2);
        juce::AudioBuffer<float> out (in);
        juce::MidiBuffer midi;
        std::vector<double> tap, centre;
        std::vector<bool> splicing;
        tap.push_back (p->getEngine().getVoiceInfo (0).tapDelayMs);
        for (int pos = 0; pos < out.getNumSamples(); pos += block)
        {
            juce::AudioBuffer<float> view (out.getArrayOfWritePointers(), 2, pos, block);
            p->processBlock (view, midi);
            const auto info = p->getEngine().getVoiceInfo (0);
            tap.push_back (info.tapDelayMs);
            centre.push_back (info.delayMs);
            splicing.push_back (info.splicing);
        }
        const auto info = p->getEngine().getVoiceInfo (0);
        const double half = 0.5 * info.windowMs + 0.6; // + crossfade overshoot bound

        // the voices read the wet-filtered feed: compare against the same filter's output
        dbl::WetFilter ref;
        ref.setTargets (20.0, 20000.0, 1.0);
        ref.prepare (fs);
        std::vector<float> feed ((size_t) in.getNumSamples());
        ref.process (in.getReadPointer (0), feed.data(), in.getNumSamples());

        double worstOutside = 0, worstAudio = 0, minR = 1.0;
        int checked = 0;
        const int w = 128;
        for (size_t b = 1; b < centre.size(); ++b)
        {
            worstOutside = std::max (worstOutside, std::abs (tap[b + 1] - centre[b]) - half);
            if (splicing[b] || splicing[b - 1] || b * (size_t) block < (size_t) (0.06 * fs))
                continue; // (the first 60 ms: the voice still reads the silence before the input)
            // measured delay at the block centre: normalised cross-correlation peak
            // (128-sample window, all lags up to 50 ms), parabolic sub-sample
            const int pos = (int) b * block + block / 2 - w / 2;
            const float* y = out.getReadPointer (0) + pos;
            const int maxLag = std::min (pos, (int) (0.05 * fs));
            std::vector<double> r ((size_t) maxLag + 1);
            int best = 0;
            for (int lag = 0; lag <= maxLag; ++lag)
            {
                r[(size_t) lag] = correlation (y, feed.data() + pos - lag, w);
                if (r[(size_t) lag] > r[(size_t) best]) best = lag;
            }
            double lagHat = best;
            if (best > 0 && best < maxLag)
            {
                const double rm = r[(size_t) best - 1], r0 = r[(size_t) best], rp = r[(size_t) best + 1];
                const double den = rm - 2.0 * r0 + rp;
                if (den < 0.0) lagHat += 0.5 * (rm - rp) / den;
            }
            minR = std::min (minR, r[(size_t) best]);
            const double expected = 0.5 * (tap[b] + tap[b + 1]) * fs / 1000.0; // tap delay at the block centre
            worstAudio = std::max (worstAudio, std::abs (lagHat - expected));
            ++checked;
        }
        const auto tag = "Detune " + juce::String (d) + ": ";
        MEASURE (tag + "centre delay (ms)", centre.back());
        MEASURE (tag + "micro-pitch window (ms)", info.windowMs);
        MEASURE (tag + "splices in 6 s", info.splices);
        MEASURE (tag + "max excursion beyond window/2 + overshoot (ms)", worstOutside);
        MEASURE (tag + "max |audio delay - tap delay| (samples)", worstAudio);
        MEASURE (tag + "lowest correlation peak", minR);
        CHECK (worstOutside <= 0.0);
        CHECK (checked > 400);
        CHECK (worstAudio <= 0.5);
        CHECK_NEAR (centre.back(), tpl::voiceDelayMs (20.0f, mWide, 0), 1e-3);
    }

    // The micro-pitch floor: Delay 0 with Detune gives every voice at least window/2 + guard.
    Setup s;
    s.voices = v4; s.mode = mWide; s.detune = 8.0f; s.delay = 0.0f;
    auto p = makeSetup (s);
    render (*p, toBuffer (sine (48000.0, 300.0, 0.3f, 4800), 2), 256);
    for (int slot = 0; slot < 4; ++slot)
    {
        const auto info = p->getEngine().getVoiceInfo (slot);
        MEASURE ("Delay 0, Detune 8: slot " + juce::String (slot) + " centre delay (ms)", info.delayMs);
        CHECK (info.delayMs >= 0.5f * info.windowMs);
        CHECK (info.delayMs < 0.5f * info.windowMs + 1.0f);
    }
}

// ============================================================================ voices, width

TEST ("DOUBLE: VOICES 1 / 2 / 4 run exactly 1 / 2 / 4 voices (telemetry and audio)")
{
    const double fs = 48000.0, f0 = 1000.0;
    for (int choice : { v1, v2, v4, v2, v1 })
    {
        Setup s;
        s.voices = v4; s.mode = mWide; s.detune = 20.0f; s.width = 0.0f;
        auto p = makeSetup (s);
        setParam (*p, "voices", (float) choice);
        // switch at runtime (not via prepare), then let the fades finish
        auto out = render (*p, toBuffer (sine (fs, f0, 0.5f, (int) (fs * 2.5)), 2), 256);
        const int expectedCount = dbl::params::voiceCount (choice);
        int active = 0;
        for (int i = 0; i < 4; ++i)
            active += p->telemetry.voice (i).active ? 1 : 0;
        CHECK_MSG (active == expectedCount, "telemetry shows " + juce::String (active) + " voices for VOICES " + juce::String (expectedCount));

        // audio: one spectral line per running voice, at its own detune
        const int n = (int) (fs * 2.0);
        const float* y = out.getReadPointer (0) + out.getNumSamples() - n;
        std::vector<float> win ((size_t) n);
        for (int i = 0; i < n; ++i)
            win[(size_t) i] = y[i] * (float) (0.5 - 0.5 * std::cos (2.0 * juce::MathConstants<double>::pi * i / n));
        int lines = 0;
        for (int slot = 0; slot < 4; ++slot)
        {
            const double hz = f0 * std::pow (2.0, tpl::voiceDetuneCents (20.0f, mWide, slot) / 1200.0);
            const double a = toneAmplitude (win.data(), n, fs, hz) * 2.0; // Hann gain
            const bool on = a > 0.05;
            lines += on ? 1 : 0;
            MEASURE ("VOICES " + juce::String (expectedCount) + ": line of slot " + juce::String (slot) + " (amplitude)", a);
            CHECK_MSG (on == (slot < expectedCount), "slot " + juce::String (slot) + " amplitude " + juce::String (a));
            if (slot >= expectedCount)
                CHECK (a < 1.0e-3);
        }
        CHECK (lines == expectedCount);
    }
}

TEST ("DOUBLE: Width 0 stacks every voice in the centre (L == R, pans 0); Width 200 hard-pans the WIDE pair")
{
    const double fs = 48000.0;
    for (int mode : { mTight, mWide, mGhost })
        for (bool monoSafe : { false, true })
        {
            Setup s;
            s.voices = v4; s.mode = mode; s.detune = 15.0f; s.humanize = 80.0f; s.width = 120.0f;
            auto p = makeSetup (s);
            setParam (*p, "monoSafe", monoSafe ? 1.0f : 0.0f);
            setParam (*p, "width", 0.0f); // automated to 0 while running
            auto in = toBuffer (pinkNoise ((int) fs * 2, 5), 2);
            auto out = render (*p, in, 256);
            bool equal = true;
            for (int i = (int) fs; i < out.getNumSamples(); ++i)
                equal = equal && sameBits (out.getSample (0, i), out.getSample (1, i));
            CHECK_MSG (equal, juce::String (modeName (mode)) + (monoSafe ? " Mono Safe" : "") + ": L != R at Width 0");
            for (int i = 0; i < 4; ++i)
                CHECK (p->telemetry.voice (i).pan == 0.0f);
        }

    // Width 200, WIDE, 2 voices: pans -1 / +1, each channel carries one voice only.
    Setup s;
    s.voices = v2; s.mode = mWide; s.detune = 20.0f; s.width = 200.0f;
    auto p = makeSetup (s);
    auto out = render (*p, toBuffer (sine (fs, 1000.0, 0.5f, (int) fs * 2), 2), 256);
    CHECK_NEAR (p->telemetry.voice (0).pan, -1.0f, 1e-6);
    CHECK_NEAR (p->telemetry.voice (1).pan, 1.0f, 1e-6);
    const int n = (int) fs;
    const float* l = out.getReadPointer (0) + out.getNumSamples() - n;
    const double up = 1000.0 * std::pow (2.0, 20.0 / 1200.0), down = 1000.0 * std::pow (2.0, -20.0 / 1200.0);
    std::vector<float> win ((size_t) n);
    for (int i = 0; i < n; ++i)
        win[(size_t) i] = l[i] * (float) (0.5 - 0.5 * std::cos (2.0 * juce::MathConstants<double>::pi * i / n));
    const double leftUp = 2.0 * toneAmplitude (win.data(), n, fs, up), leftDown = 2.0 * toneAmplitude (win.data(), n, fs, down);
    MEASURE ("Width 200: left channel, sharp voice amplitude", leftUp);
    MEASURE ("Width 200: left channel, flat voice amplitude", leftDown);
    CHECK (leftUp > 0.4 && leftDown < 1.0e-4);
}

// ============================================================================ mono safe, auto level

TEST ("DOUBLE: Mono Safe raises L/R correlation and cuts the mono fold-down loss, and keeps the double")
{
    const double fs = 48000.0;
    juce::String csv ("mode,voices,width,mix,mono_safe,correlation,mono_loss_db,side_below_150Hz_db,side_to_mid_db\n");
    for (int mode : { mTight, mWide, mGhost })
        for (int voices : { v2, v4 })
            for (float mix : { 100.0f, 50.0f })
            {
                double corr[2] {}, loss[2] {}, sideLf[2] {}, sideMid[2] {};
                for (int safe = 0; safe < 2; ++safe)
                {
                    Setup s;
                    s.voices = voices; s.mode = mode; s.detune = 10.0f; s.delay = 18.0f; s.humanize = 35.0f; s.width = 200.0f;
                    auto p = makeSetup (s);
                    setParam (*p, "monoSafe", (float) safe);
                    setParam (*p, "mix", mix);
                    p->prepareToPlay (fs, 256);
                    auto out = render (*p, toBuffer (pinkNoise ((int) (fs * 4), 9), 2), 256);
                    const int start = (int) fs, n = out.getNumSamples() - start;
                    corr[safe] = correlation (out.getReadPointer (0, start), out.getReadPointer (1, start), n);
                    loss[safe] = monoLossDb (out, start, n);
                    // side signal below 150 Hz (one-pole low-pass twice) relative to the total side
                    std::vector<float> side ((size_t) n), mid ((size_t) n), lf ((size_t) n);
                    double a1 = 0, a2 = 0;
                    const double k = 1.0 - std::exp (-2.0 * juce::MathConstants<double>::pi * 150.0 / fs);
                    for (int i = 0; i < n; ++i)
                    {
                        side[(size_t) i] = 0.5f * (out.getSample (0, start + i) - out.getSample (1, start + i));
                        mid[(size_t) i] = 0.5f * (out.getSample (0, start + i) + out.getSample (1, start + i));
                        a1 += k * (side[(size_t) i] - a1);
                        a2 += k * (a1 - a2);
                        lf[(size_t) i] = (float) a2;
                    }
                    sideLf[safe] = 10.0 * std::log10 (powerOf (lf.data(), n) / powerOf (mid.data(), n));
                    sideMid[safe] = 10.0 * std::log10 (powerOf (side.data(), n) / powerOf (mid.data(), n));
                    csv << modeName (mode) << "," << dbl::params::voiceCount (voices) << ",200," << mix << "," << safe << ","
                        << corr[safe] << "," << loss[safe] << "," << sideLf[safe] << "," << sideMid[safe] << "\n";
                }
                const auto tag = juce::String (modeName (mode)) + " " + juce::String (dbl::params::voiceCount (voices)) + " voices, Mix " + juce::String (mix);
                MEASURE (tag + ": correlation off -> on", corr[0]);
                MEASURE (tag + ":                     ", corr[1]);
                MEASURE (tag + ": mono fold-down loss off -> on (dB)", loss[0]);
                MEASURE (tag + ":                                  ", loss[1]);
                MEASURE (tag + ": LF side re mid off -> on (dB)", sideLf[0]);
                MEASURE (tag + ":                              ", sideLf[1]);
                CHECK_MSG (corr[1] > corr[0] + 0.1, tag);
                CHECK_MSG (loss[1] > 0.5 * loss[0], tag);           // at least halves the level lost in mono (dB)
                CHECK_MSG (sideLf[1] < sideLf[0] - 6.0, tag);       // bass made mono
                CHECK_MSG (corr[1] > 0.0 && loss[1] > -3.0, tag);   // never anti-phase
                CHECK_MSG (sideMid[1] > -20.0, tag);                // still a stereo double
            }
    outputDirectory().getChildFile ("DOUBLE_mono_safe.csv").replaceWithText (csv);

    // The double itself is kept: same voices, same detunes, same delays.
    Setup s;
    s.voices = v4; s.mode = mWide; s.detune = 10.0f; s.width = 200.0f;
    auto off = makeSetup (s), on = makeSetup (s);
    setParam (*on, "monoSafe", 1.0f);
    auto sig = toBuffer (pinkNoise ((int) fs, 4), 2);
    render (*off, sig, 256);
    render (*on, sig, 256);
    for (int i = 0; i < 4; ++i)
    {
        CHECK (on->telemetry.voice (i).active);
        CHECK_NEAR (on->telemetry.voice (i).detuneCents, off->telemetry.voice (i).detuneCents, 1e-4);
        CHECK_NEAR (on->telemetry.voice (i).delayMs, off->telemetry.voice (i).delayMs, 1e-4);
        CHECK_NEAR (on->telemetry.voice (i).pan, tpl::voicePan (2.0f, mWide, i, 4, true), 1e-6);
        CHECK (std::abs (on->telemetry.voice (i).pan) < std::abs (off->telemetry.voice (i).pan));
    }
}

namespace
{
    // Level of the voices' feed: the input through the wet filters (HPF 20 Hz,
    // LPF 20 kHz, GHOST shelf), i.e. what a single centred voice reproduces.
    double feedLevelDb (const juce::AudioBuffer<float>& in, int mode, int start, int n)
    {
        dbl::WetFilter f;
        f.setTargets (20.0, 20000.0, dbl::wetfilter::shelfGain (mode));
        f.prepare (48000.0);
        std::vector<float> y ((size_t) in.getNumSamples());
        f.process (in.getReadPointer (0), y.data(), in.getNumSamples());
        return 10.0 * std::log10 (std::max (1.0e-20, powerOf (y.data() + start, n)));
    }
}

TEST ("DOUBLE: Auto Level keeps 1 -> 2 -> 4 voices level on decorrelated content; off, the voices' powers add")
{
    // Auto Level holds the summed voice POWER constant, which is exact when the
    // voices are mutually decorrelated (broadband content, or anything above
    // ~150 Hz at these delays and detunes). Low partials of LF-heavy material
    // sum partly coherently between voices on the same side (delays differ by
    // a fraction of their period, detunes beat slowly): full-band pink noise
    // gains up to ~1 dB from 1 to 4 voices. Both are measured here.
    const double fs = 48000.0;
    const int len = (int) (fs * 6);
    std::vector<float> pinkHp = pinkNoise (len, 21);
    {
        dbl::WetFilter f;
        f.setTargets (150.0, 20000.0, 1.0);
        f.prepare (fs);
        f.process (pinkHp.data(), pinkHp.data(), len);
    }
    struct Sig { const char* name; std::vector<float> x; bool decorrelated; };
    const Sig sigs[] = { { "white noise", whiteNoise (len, 3), true },
                         { "pink noise > 150 Hz", pinkHp, true },
                         { "pink noise, full band", pinkNoise (len, 21), false } };
    juce::String csv ("signal,mode,auto_level,voices,wet_level_db_re_feed\n");
    double worstDecorrelated = 0, worstPink = 0;
    for (const auto& sg : sigs)
        for (int mode : { mTight, mWide, mGhost })
            for (int autoLevel : { 1, 0 })
            {
                if (! autoLevel && ! sg.decorrelated)
                    continue;
                double lv[3] {};
                for (int choice : { v1, v2, v4 })
                {
                    Setup s;
                    s.voices = choice; s.mode = mode; s.detune = 8.0f; s.delay = 15.0f; s.humanize = 35.0f; s.width = 100.0f;
                    auto p = makeSetup (s);
                    setParam (*p, "autoLevel", (float) autoLevel);
                    p->prepareToPlay (fs, 256);
                    auto in = toBuffer (sg.x, 2);
                    auto out = render (*p, in, 256);
                    const int start = (int) fs, n = out.getNumSamples() - start;
                    lv[choice] = levelDb (out, start, n) - feedLevelDb (in, mode, start, n);
                    csv << sg.name << "," << modeName (mode) << "," << autoLevel << "," << dbl::params::voiceCount (choice) << "," << lv[choice] << "\n";
                }
                const auto tag = juce::String (sg.name) + ", " + modeName (mode) + (autoLevel ? ", Auto Level on" : ", Auto Level off");
                MEASURE (tag + ": 1 voice re feed (dB)", lv[0]);
                MEASURE (tag + ": 2 voices - 1 voice (dB)", lv[1] - lv[0]);
                MEASURE (tag + ": 4 voices - 2 voices (dB)", lv[2] - lv[1]);
                const double spread = std::max ({ lv[0], lv[1], lv[2] }) - std::min ({ lv[0], lv[1], lv[2] });
                if (autoLevel && sg.decorrelated)
                {
                    worstDecorrelated = std::max (worstDecorrelated, spread);
                    CHECK_MSG (spread < 0.25, tag);
                    CHECK_MSG (std::abs (lv[0]) < 0.25, tag); // a single voice sits at the dry's level
                }
                else if (autoLevel)
                {
                    worstPink = std::max (worstPink, spread);
                    CHECK_MSG (std::abs (lv[1] - lv[0]) < 1.0 && std::abs (lv[2] - lv[1]) < 1.0, tag);
                    CHECK_MSG (lv[2] - lv[0] < 1.25, tag);
                }
                else
                {
                    // decorrelated voices: powers add (inner pair 1 dB lower: 2 -> 4 is +2.54 dB)
                    CHECK_MSG (std::abs (lv[1] - lv[0] - 3.01) < 0.3, tag);
                    CHECK_MSG (std::abs (lv[2] - lv[1] - 10.0 * std::log10 ((2.0 + 2.0 * std::pow (10.0, -0.1)) / 2.0)) < 0.3, tag);
                }
            }
    outputDirectory().getChildFile ("DOUBLE_auto_level.csv").replaceWithText (csv);

    // Switching while running: 50 ms levels through 1 -> 2 -> 4 -> 1 changes stay flat (on) / step (off)
    for (int autoLevel : { 1, 0 })
    {
        Setup s;
        s.voices = v1; s.mode = mWide; s.detune = 8.0f; s.delay = 15.0f; s.humanize = 35.0f; s.width = 100.0f;
        auto p = makeSetup (s);
        setParam (*p, "autoLevel", (float) autoLevel);
        p->prepareToPlay (fs, 256);
        auto in = toBuffer (whiteNoise ((int) (fs * 8), 8), 2);
        juce::AudioBuffer<float> out (in);
        juce::MidiBuffer midi;
        const int block = 240;
        for (int pos = 0, b = 0; pos < out.getNumSamples(); pos += block, ++b)
        {
            if (pos == (int) (fs * 2)) setParam (*p, "voices", (float) v2);
            if (pos == (int) (fs * 4)) setParam (*p, "voices", (float) v4);
            if (pos == (int) (fs * 6)) setParam (*p, "voices", (float) v1);
            juce::AudioBuffer<float> view (out.getArrayOfWritePointers(), 2, pos, block);
            p->processBlock (view, midi);
        }
        const int w = (int) (0.05 * fs);
        double lo = 1e9, hi = -1e9;
        for (int pos = (int) fs; pos + w <= out.getNumSamples(); pos += w)
        {
            const double l = levelDb (out, pos, w);
            lo = std::min (lo, l);
            hi = std::max (hi, l);
        }
        MEASURE (juce::String ("switching 1 -> 2 -> 4 -> 1 voices, Auto Level ") + (autoLevel ? "on" : "off") + ": 50 ms level range (dB)", hi - lo);
        if (autoLevel)
            CHECK (hi - lo < 1.0);   // window-to-window noise of white noise is ~ +-0.3 dB
        else
            CHECK (hi - lo > 4.5);   // 1 -> 4 voices adds 5.5 dB without Auto Level
    }

    MEASURE ("Auto Level on, decorrelated content: worst 1/2/4-voice level spread (dB)", worstDecorrelated);
    MEASURE ("Auto Level on, full-band pink noise: worst 1/2/4-voice level spread (dB)", worstPink);
}

// ============================================================================ humanize

TEST ("DOUBLE: Humanize 0 is static; Humanize 100 drifts slowly, bounded and without a period")
{
    const double fs = 48000.0;
    const int block = 48; // telemetry sampled at 1 kHz
    for (float h : { 0.0f, 100.0f })
    {
        Setup s;
        s.voices = v1; s.mode = mWide; s.detune = 8.0f; s.delay = 20.0f; s.humanize = h; s.width = 0.0f;
        auto p = makeSetup (s, fs, block);
        const int seconds = 30;
        auto in = toBuffer (sine (fs, 1000.0, 0.5f, (int) fs * seconds), 2);
        juce::AudioBuffer<float> out (in);
        juce::MidiBuffer midi;
        std::vector<double> det, del, gain;
        for (int pos = 0; pos < out.getNumSamples(); pos += block)
        {
            juce::AudioBuffer<float> view (out.getArrayOfWritePointers(), 2, pos, block);
            p->processBlock (view, midi);
            const auto t = p->telemetry.voice (0);
            det.push_back (t.detuneCents);
            del.push_back (t.delayMs);
            gain.push_back (t.gain);
        }
        auto stats = [] (const std::vector<double>& v, double& mean, double& sd, double& lo, double& hi)
        {
            mean = 0; for (double x : v) mean += x; mean /= (double) v.size();
            sd = 0; lo = 1e9; hi = -1e9;
            for (double x : v) { sd += (x - mean) * (x - mean); lo = std::min (lo, x); hi = std::max (hi, x); }
            sd = std::sqrt (sd / (double) v.size());
        };
        double mDet, sdDet, loDet, hiDet, mDel, sdDel, loDel, hiDel, mG, sdG, loG, hiG;
        stats (det, mDet, sdDet, loDet, hiDet);
        stats (del, mDel, sdDel, loDel, hiDel);
        stats (gain, mG, sdG, loG, hiG);
        const auto tag = "Humanize " + juce::String (h) + ": ";
        MEASURE (tag + "pitch sd (cents)", sdDet);
        MEASURE (tag + "pitch range (cents)", hiDet - loDet);
        MEASURE (tag + "delay sd (ms)", sdDel);
        MEASURE (tag + "delay range (ms)", hiDel - loDel);
        MEASURE (tag + "gain range (dB)", 20.0 * std::log10 (hiG / loG));

        if (h == 0.0f)
        {
            CHECK (hiDet - loDet < 1.0e-4);
            CHECK (hiDel - loDel < 1.0e-4);
            CHECK (hiG / loG < 1.0 + 1.0e-6);
            continue;
        }

        CHECK (sdDet > 0.5);
        CHECK (sdDel > 0.1);
        // bounded: pitch drift <= 5 cents plus the Doppler of the delay drift (< 3 cents measured bound)
        CHECK (loDet > 8.0 - 8.0 && hiDet < 8.0 + 8.0);
        CHECK (hiDel - mDel <= tpl::humanizeDelayMs + 1e-3 && mDel - loDel <= tpl::humanizeDelayMs + 1e-3);
        CHECK (20.0 * std::log10 (hiG / loG) <= 2.0 * tpl::humanizeGainDb + 1e-3);

        // slow: share of the pitch-drift energy above 3 Hz (DFT of the 1 kHz telemetry track)
        auto highShare = [] (std::vector<double> v, double rate, double cutHz)
        {
            double mean = 0; for (double x : v) mean += x; mean /= (double) v.size();
            for (auto& x : v) x -= mean;
            const int n = (int) v.size();
            double total = 0, high = 0;
            for (int k = 1; k < n / 2; k += 1)
            {
                const double hz = k * rate / n;
                if (hz > 40.0) break; // energy above 40 Hz is negligible and costly to scan
                double re = 0, im = 0;
                for (int i = 0; i < n; i += 4) // decimated DFT (250 Hz effective, alias-free below 40 Hz for this smooth track)
                {
                    const double w = 2.0 * juce::MathConstants<double>::pi * k * i / n;
                    re += v[(size_t) i] * std::cos (w);
                    im -= v[(size_t) i] * std::sin (w);
                }
                const double e = re * re + im * im;
                total += e;
                if (hz > cutHz) high += e;
            }
            return high / total;
        };
        const double pitchHigh = highShare (det, 1000.0, 3.0), delayHigh = highShare (del, 1000.0, 3.0);
        MEASURE (tag + "pitch drift energy above 3 Hz (share)", pitchHigh);
        MEASURE (tag + "delay drift energy above 3 Hz (share)", delayHigh);
        CHECK (pitchHigh < 0.01);
        CHECK (delayHigh < 0.01);

        // no period: the drift's autocorrelation has no strong repeat between 1 and 10 s
        double maxRepeat = -1.0;
        {
            std::vector<double> v (det);
            double mean = 0; for (double x : v) mean += x; mean /= (double) v.size();
            for (auto& x : v) x -= mean;
            double r0 = 0; for (double x : v) r0 += x * x;
            for (int lag = 1000; lag <= 10000; lag += 25)
            {
                double r = 0;
                for (size_t i = 0; i + (size_t) lag < v.size(); ++i) r += v[i] * v[i + (size_t) lag];
                maxRepeat = std::max (maxRepeat, r / r0);
            }
        }
        MEASURE (tag + "max autocorrelation of the pitch drift at 1-10 s lags", maxRepeat);
        CHECK (maxRepeat < 0.5);

        // the audio follows the telemetry: zero-crossing pitch per 100 ms vs the mean telemetry detune
        double worst = 0;
        const int w = (int) (0.1 * fs), perWindow = w / block;
        for (int k = 50; k < (int) det.size() / perWindow - 1; ++k)
        {
            const int pos = k * w;
            const double f = zeroCrossingHz (out.getReadPointer (0) + pos, w, fs);
            double tel = 0;
            for (int j = 0; j < perWindow; ++j) tel += det[(size_t) (k * perWindow + j)];
            tel /= perWindow;
            worst = std::max (worst, std::abs (cents (f, 1000.0) - tel));
        }
        MEASURE (tag + "max |audio pitch - telemetry| per 100 ms (cents)", worst);
        CHECK (worst < 0.5);
    }
}

// ============================================================================ modes

TEST ("DOUBLE: modes differ: TIGHT narrower and less detuned; GHOST darker, more decorrelated and more unstable")
{
    const double fs = 48000.0;
    struct Res { double corrLR, centroid, peakXcorr, energySpreadMs, detuneSd; float pan0, cents0, delay0; };
    std::map<int, Res> r;
    for (int mode : { mTight, mWide, mGhost })
    {
        Res res {};
        Setup s;
        s.voices = v4; s.mode = mode; s.detune = 10.0f; s.delay = 18.0f; s.humanize = 0.0f; s.width = 100.0f;
        auto p = makeSetup (s);
        auto in = toBuffer (whiteNoise ((int) (fs * 3), 17), 2);
        auto out = render (*p, in, 256);
        const int start = (int) fs, n = out.getNumSamples() - start;
        res.corrLR = correlation (out.getReadPointer (0, start), out.getReadPointer (1, start), n);
        // spectral centroid of the wet (mono sum), 64 bands of a decimated DFT
        {
            std::vector<float> m ((size_t) n);
            for (int i = 0; i < n; ++i) m[(size_t) i] = 0.5f * (out.getSample (0, start + i) + out.getSample (1, start + i));
            double num = 0, den = 0;
            for (double hz = 100.0; hz < 20000.0; hz *= 1.1)
            {
                const double a = toneAmplitude (m.data(), 16384, fs, hz);
                num += hz * a * a;
                den += a * a;
            }
            res.centroid = num / den;
        }
        // decorrelation from the dry: highest normalised cross-correlation over 0..100 ms,
        // at Detune 0 (detuned noise decorrelates in every mode; this isolates GHOST's diffusion)
        {
            Setup z = s;
            z.detune = 0.0f;
            auto q = makeSetup (z);
            auto zout = render (*q, in, 256);
            const float* x = in.getReadPointer (0, start);
            const float* y = zout.getReadPointer (0, start);
            const int len = (int) fs;
            double best = 0;
            for (int lag = 0; lag < (int) (0.1 * fs); ++lag)
                best = std::max (best, correlation (y + lag, x, len));
            res.peakXcorr = best;
        }
        // transient smear of voice 0: time holding 90 % of its impulse-response energy
        {
            auto q = makeSetup (s);
            q->setSoloVoiceForTesting (0);
            juce::AudioBuffer<float> imp (2, 16384);
            imp.clear();
            imp.setSample (0, 1000, 1.0f);
            imp.setSample (1, 1000, 1.0f);
            auto ir = render (*q, imp, 256);
            double total = 0;
            for (int i = 0; i < ir.getNumSamples(); ++i) total += (double) ir.getSample (0, i) * ir.getSample (0, i);
            double acc = 0;
            int i10 = -1, i90 = -1;
            for (int i = 0; i < ir.getNumSamples(); ++i)
            {
                acc += (double) ir.getSample (0, i) * ir.getSample (0, i);
                if (i10 < 0 && acc >= 0.05 * total) i10 = i;
                if (i90 < 0 && acc >= 0.95 * total) i90 = i;
            }
            res.energySpreadMs = (i90 - i10) * 1000.0 / fs;
        }
        res.pan0 = p->telemetry.voice (0).pan;
        res.cents0 = p->telemetry.voice (0).detuneCents;
        res.delay0 = p->telemetry.voice (0).delayMs;
        // instability: telemetry pitch spread at Humanize 50
        {
            Setup h = s;
            h.humanize = 50.0f;
            auto q = makeSetup (h, fs, 48);
            auto sig = toBuffer (sine (fs, 500.0, 0.3f, 48), 2);
            juce::MidiBuffer midi;
            std::vector<double> d;
            for (int b = 0; b < 20000; ++b)
            {
                auto buf = sig;
                q->processBlock (buf, midi);
                d.push_back (q->telemetry.voice (0).detuneCents);
            }
            double mean = 0; for (double x : d) mean += x; mean /= (double) d.size();
            double sd = 0; for (double x : d) sd += (x - mean) * (x - mean);
            res.detuneSd = std::sqrt (sd / (double) d.size());
        }
        r[mode] = res;
        const auto tag = juce::String (modeName (mode)) + ": ";
        MEASURE (tag + "outer-left pan", res.pan0);
        MEASURE (tag + "outer-left detune (cents)", res.cents0);
        MEASURE (tag + "outer-left delay (ms)", res.delay0);
        MEASURE (tag + "wet L/R correlation (white noise)", res.corrLR);
        MEASURE (tag + "wet spectral centroid (Hz)", res.centroid);
        MEASURE (tag + "peak normalised xcorr wet vs dry (Detune 0)", res.peakXcorr);
        MEASURE (tag + "voice impulse: 90 % energy spread (ms)", res.energySpreadMs);
        MEASURE (tag + "pitch drift sd at Humanize 50 (cents)", res.detuneSd);
    }
    // spread: TIGHT narrower, less detuned, earlier than WIDE
    CHECK (std::abs (r[mTight].pan0) < 0.7f * std::abs (r[mWide].pan0));
    CHECK (std::abs (r[mTight].cents0) < 0.7f * std::abs (r[mWide].cents0));
    CHECK (r[mTight].delay0 < r[mWide].delay0);
    CHECK (r[mTight].corrLR > r[mWide].corrLR + 0.1);
    // GHOST: darker, more decorrelated from the dry, smeared transients, more unstable
    CHECK (r[mGhost].centroid < 0.8 * r[mWide].centroid);
    CHECK (r[mGhost].peakXcorr < 0.7 * r[mWide].peakXcorr);
    CHECK (r[mGhost].energySpreadMs > 4.0 * std::max (0.1, r[mWide].energySpreadMs));
    CHECK (r[mGhost].detuneSd > 1.3 * r[mWide].detuneSd);
    CHECK (std::abs (r[mGhost].cents0) > std::abs (r[mWide].cents0));
    CHECK (r[mGhost].delay0 > r[mWide].delay0);
}

// ============================================================================ automation

TEST ("DOUBLE: voice count and mode changes under automation are click-free")
{
    const double fs = 48000.0;
    const int block = 64, blocks = 2400;
    // Largest |2nd difference| of the output: a step shows up as a spike.
    auto run = [&] (bool switching, int fixedVoices, int fixedMode)
    {
        Setup s;
        s.voices = fixedVoices; s.mode = fixedMode; s.detune = 12.0f; s.delay = 20.0f; s.humanize = 35.0f; s.width = 140.0f;
        auto p = makeSetup (s, fs, block);
        setParam (*p, "mix", 50.0f);
        setParam (*p, "monoSafe", 1.0f);
        juce::MidiBuffer midi;
        float y1[2] {}, y2[2] {}, maxCurv = 0;
        const int voiceSeq[] = { v1, v4, v2, v4, v1, v2 };
        for (int b = 0; b < blocks; ++b)
        {
            if (switching && b % 37 == 0)
            {
                setParam (*p, "voices", (float) voiceSeq[(b / 37) % 6]);
                setParam (*p, "mode", (float) ((b / 37 / 2) % 3));
            }
            auto buf = toBuffer (sine (fs, 110.0, 0.3f, block, 2.0 * juce::MathConstants<double>::pi * 110.0 * b * block / fs), 2);
            p->processBlock (buf, midi);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < block; ++i)
                {
                    const float y = buf.getSample (ch, i);
                    if (b > 10)
                        maxCurv = std::max (maxCurv, std::abs (y - 2.0f * y1[ch] + y2[ch]));
                    y2[ch] = y1[ch];
                    y1[ch] = y;
                }
        }
        return maxCurv;
    };

    float baseline = 0;
    for (int v : { v1, v2, v4 })
        for (int m : { mTight, mWide, mGhost })
            baseline = std::max (baseline, run (false, v, m));
    const float switched = run (true, v1, mTight);
    MEASURE ("max |2nd difference| over all fixed voice/mode settings", baseline);
    MEASURE ("max |2nd difference| while switching voices and mode every 37 blocks", switched);
    // a 110 Hz sine at 0.3 has |2nd difference| ~ 0.0016 per voice layer; a click is >> that
    CHECK (switched < 1.5f * baseline);
}

TEST ("DOUBLE: rapid automation of every parameter stays finite and bounded")
{
    const double fs = 48000.0;
    auto p = makeDouble (fs, 64);
    juce::Random rng (7);
    juce::MidiBuffer midi;
    float maxAbs = 0, maxJump = 0, prev = 0;
    for (int b = 0; b < 4000; ++b)
    {
        setParam (*p, "voices", (float) rng.nextInt (3));
        setParam (*p, "mode", (float) rng.nextInt (3));
        setParam (*p, "detune", -25.0f + 50.0f * rng.nextFloat());
        setParam (*p, "delay", 40.0f * rng.nextFloat());
        setParam (*p, "humanize", 100.0f * rng.nextFloat());
        setParam (*p, "width", 200.0f * rng.nextFloat());
        setParam (*p, "hpf", 20.0f + 980.0f * rng.nextFloat());
        setParam (*p, "lpf", 1000.0f + 19000.0f * rng.nextFloat());
        setParam (*p, "monoSafe", (float) rng.nextInt (2));
        setParam (*p, "autoLevel", (float) rng.nextInt (2));
        setParam (*p, "mix", 100.0f * rng.nextFloat());
        auto buf = toBuffer (sine (fs, 200.0, 0.3f, 64, 2.0 * juce::MathConstants<double>::pi * 200.0 * b * 64 / fs), 2);
        p->processBlock (buf, midi);
        CHECK (allFinite (buf));
        for (int i = 0; i < 64; ++i)
        {
            const float y = buf.getSample (0, i);
            maxAbs = std::max (maxAbs, std::abs (y));
            maxJump = std::max (maxJump, std::abs (y - prev));
            prev = y;
        }
    }
    MEASURE ("max |output|", maxAbs);
    MEASURE ("max sample-to-sample jump", maxJump);
    CHECK (maxAbs < 1.5f);
    CHECK (maxJump < 0.12f); // the input itself moves up to 0.008; 4 voices + dry stay well below this
}

// ============================================================================ filters

TEST ("DOUBLE: wet filter sweep matches wetFilterResponseDb at minimum, mid, maximum and musical settings")
{
    struct Case { const char* name; double fs; float hpf, lpf; int mode; float delay, detune; double maxHz; double tol; };
    const Case cases[] = {
        { "knobs_min",        48000.0,   20.0f,  1000.0f, mTight,  0.0f,  0.0f, 24000.0, 0.01 },
        { "knobs_mid",        48000.0, 141.42f,  4472.1f, mTight,  0.0f,  0.0f, 24000.0, 0.01 },
        { "knobs_max",        48000.0, 1000.0f, 20000.0f, mTight,  0.0f,  0.0f, 24000.0, 0.01 },
        { "open",             48000.0,   20.0f, 20000.0f, mWide,   0.0f,  0.0f, 24000.0, 0.01 },
        { "narrow",           48000.0, 1000.0f,  1000.0f, mWide,   0.0f,  0.0f, 24000.0, 0.01 },
        { "vocal_120_9k",     48000.0,  120.0f,  9000.0f, mTight,  0.0f,  0.0f, 24000.0, 0.01 },
        { "guitar_250_6k",    44100.0,  250.0f,  6000.0f, mWide,   0.0f,  0.0f, 22050.0, 0.01 },
        { "hires_80_16k",     96000.0,   80.0f, 16000.0f, mTight,  0.0f,  0.0f, 40000.0, 0.01 },
        { "ghost_200_8k",     48000.0,  200.0f,  8000.0f, mGhost,  0.0f,  0.0f, 24000.0, 0.01 },
        { "fractional_delay", 48000.0,  100.0f, 12000.0f, mTight, 13.3f,  0.0f, 16000.0, 0.05 },
    };
    juce::String summary ("case,points,max_abs_error_db\n");
    for (const auto& c : cases)
    {
        Setup s;
        s.voices = v1; s.mode = c.mode; s.detune = c.detune; s.delay = c.delay; s.width = 0.0f;
        auto p = makeSetup (s, c.fs, 512);
        setParam (*p, "hpf", c.hpf);
        setParam (*p, "lpf", c.lpf);
        p->prepareToPlay (c.fs, 512);
        const double sr = p->getWetSampleRate();
        CHECK (sr == c.fs);
        const float hpf = plainValue (*p, "hpf"), lpf = plainValue (*p, "lpf");

        juce::String csv ("hz,expected_db,measured_db,error_db\n");
        double maxErr = 0;
        int points = 0;
        std::vector<double> freqs;
        for (double hz = 20.0; hz <= std::min (20000.0, 0.95 * c.fs / 2); hz *= std::pow (2.0, 1.0 / 6.0))
            freqs.push_back (hz);
        for (double hz : { (double) hpf, (double) lpf, std::min (20000.0, 0.95 * c.fs / 2) })
            freqs.push_back (hz);
        std::sort (freqs.begin(), freqs.end());
        for (double hz : freqs)
        {
            const int settle = (int) (0.15 * c.fs), n = (int) (0.2 * c.fs);
            auto in = toBuffer (sine (c.fs, hz, 0.25f, settle + n), 2);
            auto out = render (*p, in, 512);
            const double measured = 20.0 * std::log10 (sineFit (out.getReadPointer (0) + settle, n, c.fs, hz).amp / 0.25);
            const double expected = c.mode == mGhost ? dbl::wetFilterResponseDb (hz, hpf, lpf, sr, c.mode)
                                                     : dbl::wetFilterResponseDb (hz, hpf, lpf, sr);
            const double err = measured - expected;
            csv << hz << "," << expected << "," << measured << "," << err << "\n";
            if (hz <= c.maxHz && expected > -60.0)
            {
                maxErr = std::max (maxErr, std::abs (err));
                ++points;
            }
        }
        outputDirectory().getChildFile (juce::String ("DOUBLE_wet_filter_") + c.name + ".csv").replaceWithText (csv);
        summary << c.name << "," << points << "," << maxErr << "\n";
        MEASURE (juce::String (c.name) + " (hpf " + juce::String (hpf, 1) + ", lpf " + juce::String (lpf, 0) + ", "
                     + modeName (c.mode) + "): max |measured - expected| (dB)", maxErr);
        CHECK_MSG (maxErr < c.tol, c.name);
        CHECK (points > 30);
    }

    // The filters act on the voice feed, ahead of the micro-pitch: a voice
    // detuned by r plays |H(f)| at f*r. Checked at +25 cents (WIDE).
    {
        Setup s;
        s.voices = v1; s.mode = mWide; s.detune = 25.0f; s.delay = 20.0f; s.width = 0.0f;
        auto p = makeSetup (s, 48000.0, 512);
        setParam (*p, "hpf", 300.0f);
        setParam (*p, "lpf", 3000.0f);
        p->prepareToPlay (48000.0, 512);
        const double ratio = std::pow (2.0, 25.0 / 1200.0);
        double maxErr = 0, maxCurveOffset = 0, lowestErr = 0, curveShift = 0;
        juce::String csv ("hz_in,hz_out,expected_at_input_db,curve_at_output_db,measured_db\n");
        for (double hz = 60.0; hz <= 15000.0; hz *= std::pow (2.0, 1.0 / 3.0))
        {
            const int settle = (int) (0.15 * 48000), n = (int) (0.5 * 48000);
            auto out = render (*p, toBuffer (sine (48000.0, hz, 0.25f, settle + n), 2), 512);
            const double measured = 20.0 * std::log10 (sineFit (out.getReadPointer (0) + settle, n, 48000.0, hz * ratio).amp / 0.25);
            const double atInput = dbl::wetFilterResponseDb (hz, 300.0, 3000.0, 48000.0);
            const double atOutput = dbl::wetFilterResponseDb (hz * ratio, 300.0, 3000.0, 48000.0);
            csv << hz << "," << hz * ratio << "," << atInput << "," << atOutput << "," << measured << "\n";
            // splices are phase-matched for periods up to the 12 ms micro-pitch window
            // (f >= 83 Hz, MODEL_SPEC.md section 4); below, a splice may step the phase
            if (hz < 85.0)
                lowestErr = std::max (lowestErr, std::abs (measured - atInput));
            else if (atInput > -40.0)
            {
                maxErr = std::max (maxErr, std::abs (measured - atInput));
                maxCurveOffset = std::max (maxCurveOffset, std::abs (measured - atOutput));
                curveShift = std::max (curveShift, std::abs (atOutput - atInput));
            }
        }
        MEASURE ("+25 cents voice, below 85 Hz (HPF 300 Hz stop band): max sine-fit deviation (dB)", lowestErr);
        outputDirectory().getChildFile ("DOUBLE_wet_filter_detuned_25c.csv").replaceWithText (csv);
        MEASURE ("+25 cents voice, 85 Hz - 15 kHz: max |measured - H(f_in)| (dB)", maxErr);
        MEASURE ("+25 cents voice: max offset from the drawn curve at f_out (dB)", maxCurveOffset);
        MEASURE ("+25 cents: largest change of the curve itself over 25 cents (dB)", curveShift);
        CHECK (maxErr < 0.05);
        CHECK (maxCurveOffset < curveShift + 0.05); // the drawn curve is off by at most its own 25-cent slope
    }
    outputDirectory().getChildFile ("DOUBLE_wet_filter_summary.csv").replaceWithText (summary);
}

// ============================================================================ telemetry

TEST ("DOUBLE: telemetry equals the audio: pans, gains, delays, detunes and voice peaks")
{
    const double fs = 48000.0;
    // pans and gains: solo each voice, a 1 kHz tone, compare the measured channel gains
    for (int mode : { mTight, mWide, mGhost })
        for (float width : { 0.0f, 65.0f, 130.0f, 200.0f })
            for (int slot = 0; slot < 4; ++slot)
            {
                Setup s;
                s.voices = v4; s.mode = mode; s.detune = 0.0f; s.delay = 10.0f; s.width = width;
                auto p = makeSetup (s);
                p->setSoloVoiceForTesting (slot);
                auto out = render (*p, toBuffer (sine (fs, 1000.0, 0.5f, (int) (fs * 0.5)), 2), 256);
                const int n = (int) (0.2 * fs);
                const double aL = sineFit (out.getReadPointer (0) + out.getNumSamples() - n, n, fs, 1000.0).amp / 0.5;
                const double aR = sineFit (out.getReadPointer (1) + out.getNumSamples() - n, n, fs, 1000.0).amp / 0.5;
                const double measuredPan = 4.0 * std::atan2 (aR, aL) / juce::MathConstants<double>::pi - 1.0;
                // re a centred voice, without the wet filters' response at 1 kHz (GHOST's shelf)
                const double measuredGain = std::sqrt (aL * aL + aR * aR) * 0.70710678
                                          / std::pow (10.0, dbl::wetFilterResponseDb (1000.0, 20.0, 20000.0, fs, mode) / 20.0);
                const auto t = p->telemetry.voice (slot);
                const auto tag = juce::String (modeName (mode)) + " width " + juce::String (width) + " slot " + juce::String (slot);
                CHECK_MSG (std::abs (measuredPan - t.pan) < 2.0e-4, tag + ": pan " + juce::String (measuredPan, 5) + " vs telemetry " + juce::String (t.pan, 5));
                CHECK_MSG (std::abs (20.0 * std::log10 (measuredGain / t.gain)) < 0.01, tag + ": gain");
                CHECK_NEAR (t.pan, tpl::voicePan (width / 100.0f, mode, slot, 4, false), 1e-6);
            }

    // voice peaks: Width 200 WIDE pair is hard-panned, so the left output IS voice 0
    Setup s;
    s.voices = v2; s.mode = mWide; s.detune = 12.0f; s.delay = 14.0f; s.humanize = 50.0f; s.width = 200.0f;
    auto p = makeSetup (s, fs, 100);
    const int slice = (int) std::lround (DoubleTelemetry::sliceSeconds * fs);
    const int slices = 200;
    const long long before = p->telemetry.sliceCount();
    std::vector<float> env ((size_t) (slices * slice));
    for (int i = 0; i < (int) env.size(); ++i)
        env[(size_t) i] = 0.5f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 3.0 * i / fs) // 3 Hz envelope
                        * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 440.0 * i / fs);
    auto out = render (*p, toBuffer (env, 2), 100);
    CHECK (p->telemetry.sliceCount() - before == slices);
    std::vector<float> hist (slices), hist1 (slices);
    p->telemetry.readHistory (0, hist.data(), slices);
    p->telemetry.readHistory (1, hist1.data(), slices);
    double worst = 0, worst1 = 0;
    for (int k = 0; k < slices; ++k)
    {
        float pl = 0, pr = 0;
        for (int i = k * slice; i < (k + 1) * slice; ++i)
        {
            pl = std::max (pl, std::abs (out.getSample (0, i)));
            pr = std::max (pr, std::abs (out.getSample (1, i)));
        }
        worst = std::max (worst, (double) std::abs (pl - hist[(size_t) k]));
        worst1 = std::max (worst1, (double) std::abs (pr - hist1[(size_t) k]));
    }
    MEASURE ("max |left slice peak - voice 0 telemetry peak|", worst);
    MEASURE ("max |right slice peak - voice 1 telemetry peak|", worst1);
    CHECK (worst < 1.0e-5 && worst1 < 1.0e-5);
    // inactive voices push 0
    p->telemetry.readHistory (2, hist.data(), slices);
    CHECK (*std::max_element (hist.begin(), hist.end()) == 0.0f);

    // delays and detunes are checked against the audio in the delay, pitch and humanize tests;
    // here: the telemetry follows a running change of the knobs
    setParam (*p, "humanize", 0.0f);
    setParam (*p, "delay", 30.0f);
    setParam (*p, "detune", -6.0f);
    render (*p, toBuffer (sine (fs, 300.0, 0.3f, (int) fs), 2), 100);
    CHECK_NEAR (p->telemetry.voice (0).delayMs, tpl::voiceDelayMs (30.0f, mWide, 0), 1e-3);
    CHECK_NEAR (p->telemetry.voice (1).delayMs, tpl::voiceDelayMs (30.0f, mWide, 1), 1e-3);
    CHECK_NEAR (p->telemetry.voice (0).detuneCents, -6.0f, 1e-3);
    CHECK_NEAR (p->telemetry.voice (1).detuneCents, 6.0f, 1e-3);
}

TEST ("DOUBLE: mono bus: voices sum at centre level, Width has no effect, telemetry pans are 0")
{
    const double fs = 48000.0;
    Setup s;
    s.voices = v4; s.mode = mWide; s.detune = 10.0f; s.humanize = 40.0f; s.width = 0.0f;
    auto a = makeSetup (s, fs, 256, 1);
    s.width = 200.0f;
    auto b = makeSetup (s, fs, 256, 1);
    setParam (*b, "monoSafe", 1.0f);
    auto in = toBuffer (whiteNoise ((int) (fs * 2), 8), 1);
    auto oa = render (*a, in, 256), ob = render (*b, in, 256);
    bool identical = true;
    for (int i = 0; i < oa.getNumSamples(); ++i)
        identical = identical && sameBits (oa.getSample (0, i), ob.getSample (0, i));
    CHECK_MSG (identical, "Width / Mono Safe changed a mono render");
    for (int i = 0; i < 4; ++i)
        CHECK (b->telemetry.voice (i).pan == 0.0f);
    const double lv = levelDb (oa, (int) fs, (int) fs) - feedLevelDb (in, mWide, (int) fs, (int) fs);
    MEASURE ("mono bus, 4 voices, Auto Level, white noise: wet re feed (dB)", lv);
    CHECK (std::abs (lv) < 0.25);
}

// ============================================================================ state, presets

TEST ("DOUBLE: state round trip restores every parameter and renders identically")
{
    const double fs = 48000.0;
    auto a = makeDouble (fs, 256);
    setParam (*a, "voices", (float) v4);
    setParam (*a, "mode", (float) mGhost);
    setParam (*a, "detune", -13.7f);
    setParam (*a, "delay", 27.3f);
    setParam (*a, "humanize", 64.0f);
    setParam (*a, "width", 155.0f);
    setParam (*a, "hpf", 180.0f);
    setParam (*a, "lpf", 7300.0f);
    setParam (*a, "monoSafe", 0.0f);
    setParam (*a, "autoLevel", 0.0f);
    setParam (*a, "mix", 63.0f);
    setParam (*a, "output", -2.5f);
    a->setUiProperty ("scale", 1.25);

    juce::MemoryBlock state;
    a->getStateInformation (state);
    auto b = makeDouble (fs, 256);
    b->setStateInformation (state.getData(), (int) state.getSize());
    for (auto* param : a->getParameters())
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (param))
            CHECK_NEAR (r->getValue(), b->apvts.getParameter (r->paramID)->getValue(), 1e-6);
    CHECK_NEAR ((double) b->getUiProperty ("scale", 1.0), 1.25, 1e-9);

    a->prepareToPlay (fs, 256);
    b->prepareToPlay (fs, 256);
    auto sig = toBuffer (pinkNoise ((int) fs * 2, 12), 2);
    auto oa = render (*a, sig, 256), ob = render (*b, sig, 256);
    bool identical = true;
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < oa.getNumSamples(); ++i)
            identical = identical && sameBits (oa.getSample (ch, i), ob.getSample (ch, i));
    CHECK_MSG (identical, "restored instance renders differently");

    auto fresh = makeDouble (fs, 256);
    auto of = render (*fresh, sig, 256);
    double diff = 0;
    for (int i = 0; i < of.getNumSamples(); ++i) diff += std::abs (of.getSample (0, i) - ob.getSample (0, i));
    CHECK (diff > 1.0);

    b->setStateInformation ("nonsense", 8);
    CHECK_NEAR (plainValue (*b, "mix"), 63.0f, 0.05);
}

TEST ("DOUBLE: factory presets load, set every parameter and reject other plugins' presets")
{
    DoubleProcessor p;
    const auto& list = p.presets.getPresets();
    int factory = 0;
    for (auto& pr : list) factory += pr.isFactory ? 1 : 0;
    MEASURE ("factory presets", factory);
    CHECK (factory >= 10);

    std::set<int> modes, voices;
    for (int i = 0; i < (int) list.size(); ++i)
        if (list[(size_t) i].isFactory)
        {
            p.loadPreset (i);
            CHECK (list[(size_t) i].params.values.size() == 12);
            for (auto& [id, v] : list[(size_t) i].params.values)
            {
                auto* r = p.apvts.getParameter (id);
                CHECK_MSG (r != nullptr, id);
                if (r) CHECK_NEAR (r->convertFrom0to1 (r->getValue()), v, 0.06 * std::max (1.0f, std::abs (v) * 0.01f));
            }
            CHECK (! p.isPresetModified());
            modes.insert (juce::roundToInt (plainValue (p, "mode")));
            voices.insert (juce::roundToInt (plainValue (p, "voices")));
        }
    CHECK (modes.size() == 3);
    CHECK (voices.size() == 3);

    setParam (p, "mix", 13.0f);
    CHECK (p.isPresetModified());

    PresetManager::Preset out;
    CHECK (p.presets.parse (R"({"format":"tleet-preset","version":1,"plugin":"BITE","name":"x","params":{}})", out).failed());
    CHECK (p.presets.parse (R"({"format":"tleet-preset","version":99,"plugin":"DOUBLE","name":"x","params":{}})", out).failed());

    // Every factory preset renders finite audio and is level-matched: its Output
    // trim compensates the linear Mix law (-10 log10((1-m)^2 + m^2) for a
    // decorrelated wet, rounded to 0.5 dB). Checked on pink noise above 150 Hz
    // (vocal / instrument band; the presets high-pass the wet at 90-400 Hz).
    for (int i = 0; i < (int) list.size(); ++i)
        if (list[(size_t) i].isFactory)
        {
            DoubleProcessor q;
            q.setRateAndBufferSizeDetails (48000.0, 256);
            q.loadPreset (i);
            q.prepareToPlay (48000.0, 256);
            std::vector<float> x = pinkNoise (96000, 30 + i);
            dbl::WetFilter band;
            band.setTargets (150.0, 20000.0, 1.0);
            band.prepare (48000.0);
            band.process (x.data(), x.data(), (int) x.size());
            auto in = toBuffer (x, 2);
            auto o = render (q, in, 256);
            const double lv = levelDb (o, 24000, 72000) - levelDb (in, 24000, 72000);
            const float m = plainValue (q, "mix") * 0.01f;
            MEASURE (list[(size_t) i].name + ": output re input (dB)", lv);
            CHECK (allFinite (o));
            CHECK_MSG (std::abs (lv) < 1.5, list[(size_t) i].name);
            CHECK_NEAR (plainValue (q, "output"), std::round (-20.0 * std::log10 ((1 - m) * (1 - m) + m * m)) * 0.5, 1e-4);
        }
}

TEST ("DOUBLE: CPU cost per instance (measurement)")
{
    const double fs = 48000.0;
    for (int choice : { v1, v4 })
        for (int mode : { mTight, mGhost })
        {
            Setup s;
            s.voices = choice; s.mode = mode; s.detune = 25.0f; s.humanize = 100.0f; s.width = 150.0f;
            auto p = makeSetup (s, fs, 128);
            setParam (*p, "monoSafe", 1.0f);
            setParam (*p, "mix", 50.0f);
            auto in = toBuffer (pinkNoise ((int) (fs * 10), 2), 2);
            const auto t0 = juce::Time::getMillisecondCounterHiRes();
            render (*p, in, 128);
            const double ms = juce::Time::getMillisecondCounterHiRes() - t0;
            const double load = ms / 10000.0 * 100.0;
            MEASURE (juce::String (dbl::params::voiceCount (choice)) + " voices " + modeName (mode) + ": % of one core at 48 kHz", load);
            CHECK (load < 10.0);
        }
}
