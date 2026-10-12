#include "TestFramework.h"
#include "TestSignals.h"
#include <cstring>
#include <complex>
#include <set>
#include <tuple>
#include "../plugins/GLUE/PluginProcessor.h"
#include "../plugins/GLUE/Parameters.h"
#include "../plugins/GLUE/dsp/GlueModel.h"
#include "../plugins/GLUE/dsp/DetectorFilter.h"

using namespace tleet;
using namespace tleet::test;
using tleet::glue::GlueProcessor;
using tleet::glue::GlueModel;

namespace
{
    enum RatioIdx { r1_5 = 0, r2, r4, r10, rInf };
    enum CharIdx { cClean = 0, cTleet, cBurn };
    enum AttackIdx { a0_1 = 0, a0_3, a1, a3, a10, a30 };
    enum ReleaseIdx { rel0_1 = 0, rel0_3, rel0_6, rel1_2, relAuto };

    constexpr double beta = GlueModel::ln10Over20;

    void setParam (GlueProcessor& p, const char* id, float plain)
    {
        auto* r = p.apvts.getParameter (id);
        r->setValueNotifyingHost (r->convertTo0to1 (plain));
    }

    float plainValue (GlueProcessor& p, const char* id)
    {
        auto* r = p.apvts.getParameter (id);
        return r->convertFrom0to1 (r->getValue());
    }

    // Threshold knob value for a detector threshold in dBFS peak.
    float knobFor (float thresholdDbfs) { return thresholdDbfs - glue::params::thresholdReferenceDbfs; }

    bool sameBits (float a, float b) noexcept { return std::memcmp (&a, &b, sizeof (float)) == 0; }

    bool identical (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
    {
        if (a.getNumChannels() != b.getNumChannels() || a.getNumSamples() != b.getNumSamples())
            return false;
        for (int ch = 0; ch < a.getNumChannels(); ++ch)
            for (int i = 0; i < a.getNumSamples(); ++i)
                if (! sameBits (a.getSample (ch, i), b.getSample (ch, i)))
                    return false;
        return true;
    }

    juce::String name (int ratio) { return glue::params::ratioNames[ratio].replace (juce::String::fromUTF8 ("\xe2\x88\x9e"), "inf"); }

    // Renders main (+ optional key) through the processor in blocks; returns the main output.
    juce::AudioBuffer<float> renderKeyed (GlueProcessor& p, const juce::AudioBuffer<float>& main,
                                          const juce::AudioBuffer<float>* key, int block,
                                          std::vector<float>* grPerBlock = nullptr)
    {
        const int nm = main.getNumChannels(), nk = key != nullptr ? key->getNumChannels() : 0, n = main.getNumSamples();
        juce::AudioBuffer<float> io (nm + nk, n);
        for (int ch = 0; ch < nm; ++ch) io.copyFrom (ch, 0, main, ch, 0, n);
        for (int k = 0; k < nk; ++k)    io.copyFrom (nm + k, 0, *key, k, 0, n);
        juce::MidiBuffer midi;
        for (int pos = 0; pos < n; pos += block)
        {
            const int len = std::min (block, n - pos);
            juce::AudioBuffer<float> view (io.getArrayOfWritePointers(), nm + nk, pos, len);
            p.processBlock (view, midi);
            if (grPerBlock != nullptr)
                grPerBlock->push_back (std::max (0.0f, p.grMeter.takeGainReduction()));
        }
        juce::AudioBuffer<float> out (nm, n);
        for (int ch = 0; ch < nm; ++ch) out.copyFrom (ch, 0, io, ch, 0, n);
        return out;
    }

    juce::AudioBuffer<float> render (GlueProcessor& p, const juce::AudioBuffer<float>& input, int block,
                                     std::vector<float>* grPerBlock = nullptr)
    {
        return renderKeyed (p, input, nullptr, block, grPerBlock);
    }

    juce::AudioBuffer<float> toBuffer (const std::vector<float>& mono, int channels)
    {
        juce::AudioBuffer<float> b (channels, (int) mono.size());
        for (int ch = 0; ch < channels; ++ch)
            b.copyFrom (ch, 0, mono.data(), (int) mono.size());
        return b;
    }

    juce::AudioBuffer<float> stereo (const std::vector<float>& l, const std::vector<float>& r)
    {
        juce::AudioBuffer<float> b (2, (int) l.size());
        b.copyFrom (0, 0, l.data(), (int) l.size());
        b.copyFrom (1, 0, r.data(), (int) r.size());
        return b;
    }

    std::unique_ptr<GlueProcessor> makeGlue (double fs, int block, int channels = 2, int keyChannels = 0)
    {
        auto p = std::make_unique<GlueProcessor>();
        const auto set = channels == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo();
        GlueProcessor::BusesLayout layout;
        layout.inputBuses.add (set);
        layout.inputBuses.add (keyChannels == 0 ? juce::AudioChannelSet::disabled()
                                                : keyChannels == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo());
        layout.outputBuses.add (set);
        CHECK_MSG (p->setBusesLayout (layout), "layout rejected");
        p->setRateAndBufferSizeDetails (fs, block);
        p->prepareToPlay (fs, block);
        return p;
    }

    // Constant-envelope test signal (+A, -A, ...): the rectified detector input is a clean step.
    std::vector<float> alternating (float amp, int n)
    {
        std::vector<float> v ((size_t) n);
        for (int i = 0; i < n; ++i)
            v[(size_t) i] = (i & 1) ? -amp : amp;
        return v;
    }

    std::vector<float> concat (std::vector<float> a, const std::vector<float>& b)
    {
        a.insert (a.end(), b.begin(), b.end());
        return a;
    }

    GlueModel makeModel (double fs, int ratio, int attack, int release, float thresholdDbfs = -20.0f,
                         int character = cClean, float crush = 0.0f)
    {
        GlueModel m;
        m.setRatio (ratio);
        m.setAttack (attack);
        m.setRelease (release);
        m.setThresholdDb (thresholdDbfs);
        m.setCharacter (character);
        m.setCrush (crush);
        m.setMakeupDb (0.0f);
        m.setScHpf (20.0f);
        m.prepare (fs);
        return m;
    }

    // Runs the bare model on `l` (and `r` if given) and returns GR (dB) per sample.
    std::vector<double> grTrace (GlueModel& m, std::vector<float> l, std::vector<float> r = {})
    {
        std::vector<double> gr;
        gr.reserve (l.size());
        const int nch = r.empty() ? 1 : 2;
        for (size_t i = 0; i < l.size(); ++i)
        {
            float* ch[] = { &l[i], r.empty() ? nullptr : &r[i] };
            m.process (ch, nch, nullptr, 0, 1);
            gr.push_back (m.getGainReductionDb());
        }
        return gr;
    }

    // Time (s) at which `trace` (starting at index `from`) first crosses `level`, linearly interpolated.
    double crossing (const std::vector<double>& trace, int from, double level, bool rising, double fs)
    {
        for (int i = from; i < (int) trace.size(); ++i)
        {
            const double v = trace[(size_t) i];
            if (rising ? v >= level : v <= level)
            {
                if (i == from)
                    return 0.0;
                const double p = trace[(size_t) i - 1];
                const double frac = std::abs (v - p) > 1.0e-15 ? (level - p) / (v - p) : 1.0;
                return (i - 1 - from + frac + 1) / fs; // the step lands on sample `from`
            }
        }
        return -1.0;
    }

    struct Fit { double amp, phase; };
    Fit sineFit (const double* d, int n, double fs, double hz)
    {
        double ss = 0, cc = 0, sc = 0, ys = 0, yc = 0;
        const double w = 2.0 * juce::MathConstants<double>::pi * hz / fs;
        for (int i = 0; i < n; ++i)
        {
            const double s = std::sin (w * i), c = std::cos (w * i);
            ss += s * s; cc += c * c; sc += s * c;
            ys += d[i] * s; yc += d[i] * c;
        }
        const double det = ss * cc - sc * sc;
        const double a = (ys * cc - yc * sc) / det, b = (yc * ss - ys * sc) / det;
        return { std::sqrt (a * a + b * b), std::atan2 (b, a) };
    }

    // Processor render of a 1 kHz sine; THD and GR telemetry of the last second.
    struct ToneResult { double thd, h2, h3; float grDb, outDb; };
    ToneResult renderTone (int character, float peakDbfs, float thresholdDbfs, int ratio = r4,
                           float crush = 0.0f, float makeup = 0.0f, double hz = 1000.0)
    {
        const double fs = 48000.0;
        auto p = makeGlue (fs, 480);
        setParam (*p, "character", (float) character);
        setParam (*p, "threshold", knobFor (thresholdDbfs));
        setParam (*p, "ratio", (float) ratio);
        setParam (*p, "attack", (float) a30);
        setParam (*p, "release", (float) rel1_2);
        setParam (*p, "crush", crush);
        setParam (*p, "makeup", makeup);
        p->prepareToPlay (fs, 480);
        auto in = toBuffer (sine (fs, hz, juce::Decibels::decibelsToGain (peakDbfs), (int) (fs * 3.0)), 2);
        std::vector<float> gr;
        auto out = render (*p, in, 480, &gr);
        const int n = 48000, start = out.getNumSamples() - n;
        const float* d = out.getReadPointer (0) + start;
        const double f = toneAmplitude (d, n, fs, hz);
        return { thd (d, n, fs, hz), toneAmplitude (d, n, fs, hz * 2) / f, toneAmplitude (d, n, fs, hz * 3) / f,
                 gr.back(), db (rms (d, n)) };
    }

    double steadyGrOfModel (int ratio, double levelDbfs, double thresholdDbfs, int attack = a0_1, int release = rel1_2,
                            float crush = 0.0f, double fs = 96000.0)
    {
        auto m = makeModel (fs, ratio, attack, release, (float) thresholdDbfs, cClean, crush);
        std::vector<float> sig = alternating ((float) std::pow (10.0, levelDbfs / 20.0), (int) (fs * 0.6));
        float* ch[] = { sig.data() };
        m.process (ch, 1, nullptr, 0, (int) sig.size());
        return m.getGainReductionDb();
    }
}

// =====================================================================================
TEST ("GLUE: parameter IDs, ranges, steps and defaults match the registry")
{
    GlueProcessor p;
    const char* ids[] = { "threshold", "attack", "release", "ratio", "makeup", "scHpf", "mix",
                          "character", "dark", "crush", "output", "scExternal", "bypass" };
    for (auto* id : ids)
        CHECK_MSG (p.apvts.getParameter (id) != nullptr, id);
    CHECK (p.getParameters().size() == 13);

    auto def = [&p] (const char* id) { auto* r = p.apvts.getParameter (id); return r->convertFrom0to1 (r->getDefaultValue()); };
    CHECK_NEAR (def ("threshold"), 0.0f, 1e-4);
    CHECK_NEAR (def ("attack"), (float) a10, 1e-4);
    CHECK_NEAR (def ("release"), (float) relAuto, 1e-4);
    CHECK_NEAR (def ("ratio"), (float) r4, 1e-4);
    CHECK_NEAR (def ("makeup"), 0.0f, 1e-4);
    CHECK_NEAR (def ("scHpf"), 20.0f, 1e-3);
    CHECK_NEAR (def ("mix"), 100.0f, 1e-4);
    CHECK_NEAR (def ("character"), (float) cTleet, 1e-4);
    CHECK_NEAR (def ("dark"), 0.0f, 1e-4);
    CHECK_NEAR (def ("crush"), 0.0f, 1e-4);
    CHECK_NEAR (def ("output"), 0.0f, 1e-4);
    CHECK_NEAR (def ("scExternal"), 0.0f, 1e-4);
    CHECK_NEAR (def ("bypass"), 0.0f, 1e-4);

    auto range = [&p] (const char* id, float lo, float hi)
    {
        auto* r = p.apvts.getParameter (id);
        CHECK_NEAR (r->convertFrom0to1 (0.0f), lo, 1e-3);
        CHECK_NEAR (r->convertFrom0to1 (1.0f), hi, 1e-3);
    };
    range ("threshold", -20.0f, 10.0f);
    range ("makeup", -10.0f, 20.0f);
    range ("scHpf", 20.0f, 300.0f);
    range ("mix", 0.0f, 100.0f);
    range ("dark", 0.0f, 100.0f);
    range ("crush", 0.0f, 100.0f);
    range ("output", -20.0f, 20.0f);

    // THRESHOLD and MAKEUP: piecewise linear, 0 dB at the knob centre (the
    // skin prints 0 at the top), 0.1 dB legal values, exact round trip.
    for (auto [id, lo, hi] : { std::tuple { "threshold", -20.0f, 10.0f }, std::tuple { "makeup", -10.0f, 20.0f } })
    {
        auto* f = dynamic_cast<juce::AudioParameterFloat*> (p.apvts.getParameter (id));
        juce::AudioProcessorParameter* r = f; // the host-facing interface
        const auto& nr = f->getNormalisableRange();
        CHECK_MSG (nr.convertFrom0to1 (0.5f) == 0.0f && nr.convertTo0to1 (0.0f) == 0.5f, id);
        CHECK_NEAR (nr.convertFrom0to1 (0.25f), 0.5f * lo, 1e-5);
        CHECK_NEAR (nr.convertFrom0to1 (0.75f), 0.5f * hi, 1e-5);
        CHECK_NEAR (r->getDefaultValue(), 0.5f, 1e-7);
        CHECK (nr.interval == 0.1f);
        int bad = 0, badSnap = 0, badMono = 0, badText = 0;
        float prevN = -1.0f;
        for (int t = juce::roundToInt (lo * 10.0f); t <= juce::roundToInt (hi * 10.0f); ++t)
        {
            const float v = nr.snapToLegalValue ((float) t * 0.1f);
            const float n = nr.convertTo0to1 (v);
            bad += nr.snapToLegalValue (nr.convertFrom0to1 (n)) != v ? 1 : 0;
            badSnap += std::abs (v - (float) t * 0.1f) > 1e-5f ? 1 : 0;
            badMono += n <= prevN ? 1 : 0;
            prevN = n;
            r->setValueNotifyingHost (n);            // through the parameter itself
            bad += f->get() != v ? 1 : 0;
            badText += nr.snapToLegalValue (nr.convertFrom0to1 (r->getValueForText (r->getText (n, 64)))) != v ? 1 : 0;
        }
        CHECK_MSG (bad == 0 && badSnap == 0 && badMono == 0 && badText == 0, id);
        // Off-grid values snap to the 0.1 dB grid; the ends clamp.
        CHECK_NEAR (nr.snapToLegalValue (-3.04f), -3.0f, 1e-6);
        CHECK_NEAR (nr.snapToLegalValue (2.06f), 2.1f, 1e-6);
        CHECK (nr.snapToLegalValue (hi + 5.0f) == hi && nr.snapToLegalValue (lo - 5.0f) == lo);
        r->setValueNotifyingHost (r->getDefaultValue());
    }

    // SC HPF is logarithmic: the middle of the travel is the geometric mean.
    CHECK_NEAR (p.apvts.getParameter ("scHpf")->convertFrom0to1 (0.5f), std::sqrt (20.0f * 300.0f), 0.05);
    // Threshold calibration: knob 0 dB = -10 dBFS peak.
    CHECK_NEAR (glue::params::thresholdToDbfs (0.0f), -10.0f, 1e-6);

    auto choices = [&p] (const char* id) { return dynamic_cast<juce::AudioParameterChoice*> (p.apvts.getParameter (id))->choices; };
    CHECK (choices ("attack") == juce::StringArray ({ "0.1 ms", "0.3 ms", "1 ms", "3 ms", "10 ms", "30 ms" }));
    CHECK (choices ("release") == juce::StringArray ({ "0.1 s", "0.3 s", "0.6 s", "1.2 s", "Auto" }));
    CHECK (choices ("ratio").size() == 5 && choices ("ratio")[0] == "1.5" && choices ("ratio")[3] == "10");
    CHECK (choices ("character") == juce::StringArray ({ "Clean", "TLEET", "Burn" }));

    // The model's tables agree with the registry's labels.
    const double nominal[] = { 0.0001, 0.0003, 0.001, 0.003, 0.010, 0.030 };
    for (int i = 0; i < GlueModel::numAttacks; ++i)
        CHECK_NEAR (GlueModel::attackSeconds (i), nominal[i], 1e-9);
    const double rel[] = { 0.1, 0.3, 0.6, 1.2 };
    for (int i = 0; i < 4; ++i)
        CHECK_NEAR (GlueModel::releaseSeconds (i), rel[i], 1e-7);
    const float ratios[] = { 1.5f, 2.0f, 4.0f, 10.0f };
    for (int i = 0; i < 4; ++i)
        CHECK_NEAR (GlueModel::ratioLaw (i).ratio, ratios[i], 1e-6);
    CHECK (GlueModel::ratioLaw (rInf).limiter && ! GlueModel::ratioLaw (r10).limiter);

    // Buses: main stereo/mono in == out, plus an optional Sidechain input (disabled by default).
    CHECK (p.getBusCount (true) == 2 && p.getBusCount (false) == 1);
    CHECK (p.getBus (true, 1)->getName() == "Sidechain");
    CHECK (! p.getBus (true, 1)->isEnabledByDefault());
    GlueProcessor::BusesLayout l;
    l.inputBuses.add (juce::AudioChannelSet::stereo());
    l.inputBuses.add (juce::AudioChannelSet::mono());
    l.outputBuses.add (juce::AudioChannelSet::stereo());
    CHECK (p.checkBusesLayoutSupported (l));
    l.inputBuses.getReference (1) = juce::AudioChannelSet::create5point1();
    CHECK (! p.checkBusesLayoutSupported (l));
    l.inputBuses.getReference (1) = juce::AudioChannelSet::disabled();
    l.outputBuses.getReference (0) = juce::AudioChannelSet::mono();
    CHECK (! p.checkBusesLayoutSupported (l)); // stereo in, mono out
}

TEST ("GLUE: all sample rates and block sizes, mono and stereo, with and without a key: finite output, no steady-state allocation")
{
    for (double fs : { 44100.0, 48000.0, 88200.0, 96000.0, 192000.0 })
        for (int block : { 1, 16, 32, 64, 128, 256, 512, 1024, 2048 })
            for (int channels : { 1, 2 })
                for (int keyChannels : { 0, 2 })
                {
                    auto p = makeGlue (fs, block, channels, keyChannels);
                    setParam (*p, "threshold", -20.0f);
                    setParam (*p, "ratio", (float) (keyChannels > 0 ? rInf : r10));
                    setParam (*p, "attack", (float) a0_1);
                    setParam (*p, "release", (float) (block % 3 == 0 ? relAuto : rel0_1));
                    setParam (*p, "character", (float) cBurn);
                    setParam (*p, "crush", 100.0f);
                    setParam (*p, "dark", 100.0f);
                    setParam (*p, "scHpf", 150.0f);
                    setParam (*p, "makeup", 20.0f);
                    setParam (*p, "mix", 70.0f);
                    setParam (*p, "scExternal", keyChannels > 0 ? 1.0f : 0.0f);
                    const int n = std::max (block * 4, 4096);
                    auto in = toBuffer (sine (fs, 220.0, 0.7f, n), channels);
                    auto key = toBuffer (sine (fs, 90.0, 0.9f, n), std::max (1, keyChannels));
                    renderKeyed (*p, in, keyChannels > 0 ? &key : nullptr, block); // warm-up

                    juce::AudioBuffer<float> io (channels + keyChannels, n);
                    for (int ch = 0; ch < channels; ++ch) io.copyFrom (ch, 0, in, ch, 0, n);
                    for (int k = 0; k < keyChannels; ++k) io.copyFrom (channels + k, 0, key, k, 0, n);
                    juce::MidiBuffer midi;
                    long allocs;
                    {
                        ScopedAllocationCounter counter;
                        for (int pos = 0; pos < n; pos += block)
                        {
                            const int len = std::min (block, n - pos);
                            juce::AudioBuffer<float> view (io.getArrayOfWritePointers(), channels + keyChannels, pos, len);
                            p->processBlock (view, midi);
                        }
                        allocs = counter.count();
                    }
                    const auto tag = juce::String (fs) + " Hz, block " + juce::String (block) + ", " + juce::String (channels)
                                   + " ch, key " + juce::String (keyChannels);
                    bool finite = true;
                    for (int ch = 0; ch < channels; ++ch)
                        for (int i = 0; i < n; ++i)
                            finite = finite && std::isfinite (io.getSample (ch, i));
                    CHECK_MSG (finite, tag);
                    CHECK_MSG (allocs == 0, tag + ": " + juce::String (allocs) + " allocations in processBlock");
                    CHECK_MSG (rms (io.getReadPointer (0), n) > 0.01f, tag + ": no output");
                    CHECK_MSG (p->isUsingExternalSidechain() == (keyChannels > 0), tag);
                }
}

TEST ("GLUE: NaN/Inf in the input or the key never reaches the output or poisons the state")
{
    for (int keyChannels : { 0, 2 })
    {
        auto p = makeGlue (48000.0, 256, 2, keyChannels);
        setParam (*p, "threshold", -15.0f);
        setParam (*p, "scExternal", keyChannels > 0 ? 1.0f : 0.0f);
        auto in = toBuffer (sine (48000.0, 440.0, 0.5f, 4800), 2);
        in.setSample (0, 100, std::numeric_limits<float>::quiet_NaN());
        in.setSample (1, 200, std::numeric_limits<float>::infinity());
        in.setSample (0, 300, -std::numeric_limits<float>::infinity());
        auto key = toBuffer (sine (48000.0, 440.0, 0.5f, 4800), 2);
        key.setSample (0, 150, std::numeric_limits<float>::quiet_NaN());
        key.setSample (1, 250, std::numeric_limits<float>::infinity());
        auto out = renderKeyed (*p, in, keyChannels > 0 ? &key : nullptr, 256);
        CHECK (allFinite (out));
        auto clean = toBuffer (sine (48000.0, 440.0, 0.5f, 9600), 2);
        auto cleanKey = toBuffer (sine (48000.0, 440.0, 0.5f, 9600), 2);
        auto after = renderKeyed (*p, clean, keyChannels > 0 ? &cleanKey : nullptr, 256);
        CHECK (allFinite (after));
        CHECK (rms (after.getReadPointer (0) + 4800, 4800) > 0.02f);
        const float gr = p->grMeter.takeGainReduction();
        MEASURE ("GR after the bad samples (dB), key " + juce::String (keyChannels), gr);
        CHECK (std::isfinite (gr) && gr > 1.0f && gr < 30.0f);
    }
}

TEST ("GLUE: silence gives silence; DC is rejected by the wet path and the detector; denormal input stays clean")
{
    auto p = makeGlue (48000.0, 512);
    setParam (*p, "makeup", 20.0f);
    setParam (*p, "threshold", -20.0f);
    juce::AudioBuffer<float> silence (2, 48000);
    silence.clear();
    auto out = render (*p, silence, 512);
    CHECK (out.getMagnitude (0, out.getNumSamples()) < 1.0e-6f);

    // DC: the input coupling removes it from the VCA and from the detector.
    auto q = makeGlue (48000.0, 512);
    setParam (*q, "threshold", -20.0f);
    juce::AudioBuffer<float> dc (2, 48000 * 3);
    for (int ch = 0; ch < 2; ++ch)
        juce::FloatVectorOperations::fill (dc.getWritePointer (ch), 0.25f, dc.getNumSamples());
    std::vector<float> gr;
    out = render (*q, dc, 512, &gr);
    double mean = 0;
    for (int i = 48000 * 2; i < 48000 * 3; ++i) mean += out.getSample (0, i);
    mean /= 48000.0;
    MEASURE ("residual DC after 2 s (input 0.25)", mean);
    MEASURE ("GR from a 0.25 DC input after 3 s (dB)", gr.back());
    CHECK (std::abs (mean) < 1.0e-3);
    CHECK (gr.back() < 0.05f);

    // Denormal-level input into a fresh instance with the most gain.
    auto fresh = makeGlue (48000.0, 512);
    setParam (*fresh, "makeup", 20.0f);
    setParam (*fresh, "output", 20.0f);
    juce::AudioBuffer<float> tiny (2, 48000);
    for (int i = 0; i < tiny.getNumSamples(); ++i)
        for (int ch = 0; ch < 2; ++ch)
            tiny.setSample (ch, i, (i & 1) ? 1.0e-38f : -1.0e-38f);
    out = render (*fresh, tiny, 512);
    CHECK (allFinite (out));
    CHECK (out.getMagnitude (0, out.getNumSamples()) < 1.0e-6f);
}

TEST ("GLUE: reported latency equals measured impulse delay; Mix 0 and Bypass are bit-exact delayed dry (also with a key)")
{
    for (double fs : { 44100.0, 48000.0, 96000.0 })
    {
        auto p = makeGlue (fs, 128, 2, 2);
        const int latency = p->getLatencySamples();
        MEASURE ("reported latency @" + juce::String (fs), latency);
        CHECK (latency > 0);

        setParam (*p, "threshold", 10.0f); // 0 dBFS: a linear path for a small impulse
        setParam (*p, "character", (float) cClean);
        p->prepareToPlay (fs, 128);

        juce::AudioBuffer<float> imp (2, 4096);
        imp.clear();
        imp.setSample (0, 100, 0.25f);
        imp.setSample (1, 100, 0.25f);
        auto out = render (*p, imp, 128);
        int peakAt = 0;
        for (int i = 0; i < out.getNumSamples(); ++i)
            if (std::abs (out.getSample (0, i)) > std::abs (out.getSample (0, peakAt))) peakAt = i;
        MEASURE ("measured impulse peak delay @" + juce::String (fs), peakAt - 100);
        CHECK (peakAt - 100 == latency);

        // Mix = 0: delayed dry, bit-exact, while the wet path compresses hard on an external key.
        auto sig = toBuffer (sine (fs, 997.0, 0.5f, 8192), 2);
        auto key = toBuffer (sine (fs, 300.0, 0.9f, 8192), 2);
        setParam (*p, "threshold", -20.0f);
        setParam (*p, "ratio", (float) rInf);
        setParam (*p, "character", (float) cBurn);
        setParam (*p, "crush", 100.0f);
        setParam (*p, "makeup", 12.0f);
        setParam (*p, "scExternal", 1.0f);
        setParam (*p, "mix", 0.0f);
        p->prepareToPlay (fs, 128);
        out = renderKeyed (*p, sig, &key, 128);
        bool exact = true;
        for (int ch = 0; ch < 2; ++ch)
            for (int i = latency; i < sig.getNumSamples(); ++i)
                exact = exact && sameBits (out.getSample (ch, i), sig.getSample (ch, i - latency));
        CHECK_MSG (exact, "Mix=0 is not bit-exact dry");

        // Bypass: delayed dry, bit-exact, whatever the other settings (Output trim included).
        setParam (*p, "mix", 100.0f);
        setParam (*p, "output", 7.0f);
        setParam (*p, "bypass", 1.0f);
        p->prepareToPlay (fs, 128);
        out = renderKeyed (*p, sig, &key, 128);
        exact = true;
        for (int ch = 0; ch < 2; ++ch)
            for (int i = latency; i < sig.getNumSamples(); ++i)
                exact = exact && sameBits (out.getSample (ch, i), sig.getSample (ch, i - latency));
        CHECK_MSG (exact, "Bypass is not bit-exact dry");
    }
}

TEST ("GLUE: wet path is time-aligned with dry (no comb filtering at Mix 50 %); Output trims after Mix")
{
    const double fs = 48000.0;
    auto p = makeGlue (fs, 256);
    setParam (*p, "threshold", 10.0f);
    setParam (*p, "character", (float) cClean);
    setParam (*p, "mix", 50.0f);
    p->prepareToPlay (fs, 256);
    for (double hz : { 40.0, 1000.0, 10000.0, 18000.0 })
    {
        auto sig = toBuffer (sine (fs, hz, 0.1f, 48000), 2);
        auto out = render (*p, sig, 256);
        const float change = db (rms (out.getReadPointer (0) + 24000, 24000) / rms (sig.getReadPointer (0) + 24000, 24000));
        MEASURE ("level change at " + juce::String (hz) + " Hz, Mix 50 % (dB)", change);
        CHECK (std::abs (change) < 0.1f);
    }

    // Output acts on the blend (dry included): Mix 0 + Output -6 dB = dry - 6 dB.
    auto q = makeGlue (fs, 256);
    setParam (*q, "mix", 0.0f);
    setParam (*q, "output", -6.0f);
    q->prepareToPlay (fs, 256);
    auto sig = toBuffer (sine (fs, 500.0, 0.3f, 24000), 2);
    auto out = render (*q, sig, 256);
    const float change = db (rms (out.getReadPointer (0) + 12000, 12000) / rms (sig.getReadPointer (0) + 12000, 12000));
    MEASURE ("Mix 0 + Output -6 dB: level change (dB)", change);
    CHECK_NEAR (change, -6.0f, 0.01);
}

TEST ("GLUE: Mix is a linear blend: out(m) = dry + m (wet - dry) (null test)")
{
    const double fs = 48000.0;
    auto renderAt = [&] (float mix)
    {
        auto p = makeGlue (fs, 128);
        setParam (*p, "threshold", -15.0f);
        setParam (*p, "ratio", (float) r10);
        setParam (*p, "attack", (float) a1);
        setParam (*p, "release", (float) rel0_1);
        setParam (*p, "character", (float) cBurn);
        setParam (*p, "makeup", 6.0f);
        setParam (*p, "mix", mix);
        p->prepareToPlay (fs, 128);
        std::vector<float> sig = sine (fs, 220.0, 0.6f, 24000);
        for (size_t i = 0; i < sig.size(); ++i)
            sig[i] *= (i / 4000) % 2 == 0 ? 1.0f : 0.2f; // level steps: compression moves
        return render (*p, toBuffer (sig, 2), 128);
    };
    const auto wet = renderAt (100.0f), dryOut = renderAt (0.0f);
    double worst = 0, wetDryDiff = 0;
    for (float m : { 25.0f, 50.0f, 75.0f })
    {
        const auto out = renderAt (m);
        double maxResidual = 0;
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < out.getNumSamples(); ++i)
            {
                const double expected = dryOut.getSample (ch, i) + (m / 100.0) * (wet.getSample (ch, i) - dryOut.getSample (ch, i));
                maxResidual = std::max (maxResidual, std::abs (out.getSample (ch, i) - expected));
                wetDryDiff = std::max (wetDryDiff, (double) std::abs (wet.getSample (ch, i) - dryOut.getSample (ch, i)));
            }
        MEASURE ("max |out - linear blend| at Mix " + juce::String (m) + " %", maxResidual);
        worst = std::max (worst, maxResidual);
    }
    MEASURE ("max |wet - dry| (the blend is not trivial)", wetDryDiff);
    CHECK (wetDryDiff > 0.1);
    CHECK (worst < 1.0e-6);
}

TEST ("GLUE: static curves: GR vs level for every ratio and threshold, knee included, match MODEL_SPEC (CSV)")
{
    // Bare model at 96 kHz (the model rate at 48 kHz), constant-envelope
    // detector input, attack 0.1 ms / release 1.2 s (rho = 0.99992).
    juce::String csv ("ratio,threshold_dbfs,level_dbfs_peak,gr_model_db,gr_law_db,error_db\n");
    double maxErr = 0;
    for (int r = 0; r < GlueModel::numRatios; ++r)
        for (double thr : { -30.0, -20.0, -10.0 })
            for (double level = thr - 15.0; level <= std::min (thr + 30.0, 6.0); level += 1.0)
            {
                const double model = steadyGrOfModel (r, level, thr);
                const double law = GlueModel::staticGainReductionDb (level, thr, r, a0_1, rel1_2);
                maxErr = std::max (maxErr, std::abs (model - law));
                csv << name (r) << "," << thr << "," << level << "," << model << "," << law << "," << model - law << "\n";
            }
    outputDirectory().getChildFile ("GLUE_static_curves.csv").replaceWithText (csv);
    MEASURE ("max |model - static law| over all ratios, thresholds and levels (dB)", maxErr);
    CHECK (maxErr < 0.02);

    // Ratio labels (independent of the law code): output rise for the first
    // 10 dB of input above each ratio's own threshold (knob + offset).
    const double thr = -20.0;
    for (int r : { r1_5, r2, r4, r10 })
    {
        const auto& law = GlueModel::ratioLaw (r);
        const double t = thr + law.thresholdOffsetDb;
        const double y = 10.0 - steadyGrOfModel (r, t + 10.0, thr);
        MEASURE ("ratio " + name (r) + ": measured secant ratio over the first 10 dB", 10.0 / y);
        CHECK_NEAR (10.0 / y, law.ratio, 0.01 * law.ratio);
    }

    // Infinity is a true limiter above the knee: 20 dB more input, < 0.01 dB more output.
    const double lim10 = (thr + 10.0) - steadyGrOfModel (rInf, thr + 10.0, thr);
    const double lim30 = (thr + 30.0) - steadyGrOfModel (rInf, thr + 30.0, thr);
    MEASURE ("infinity: output at threshold +10 dB (dB re threshold)", lim10 - thr);
    MEASURE ("infinity: output rise from +10 to +30 dB over threshold (dB)", lim30 - lim10);
    CHECK (std::abs (lim30 - lim10) < 0.01);
    CHECK (std::abs (lim10 - thr) < 0.01);

    // Lower ratios start lower (ratio-dependent threshold), and the low
    // ratios curve upward above threshold much more than 10:1 (the measured
    // G-bus behaviour, MODEL_SPEC 2.3).
    double prevOnset = -1000.0;
    for (int r : { r1_5, r2, r4, r10 })
    {
        double onset = thr - 20.0;
        while (steadyGrOfModel (r, onset, thr) < 0.1 && onset < thr + 5.0)
            onset += 0.25;
        MEASURE ("ratio " + name (r) + ": level where GR reaches 0.1 dB (dBFS)", onset);
        CHECK (onset > prevOnset);
        prevOnset = onset;
    }
    auto localRatio = [&] (int r, double over)
    {
        const double t = thr + GlueModel::ratioLaw (r).thresholdOffsetDb;
        const double y0 = (t + over) - steadyGrOfModel (r, t + over, thr);
        const double y1 = (t + over + 1.0) - steadyGrOfModel (r, t + over + 1.0, thr);
        return 1.0 / (y1 - y0);
    };
    const double p2 = localRatio (r2, 20.0) / localRatio (r2, 5.0), p10 = localRatio (r10, 20.0) / localRatio (r10, 5.0);
    MEASURE ("2:1 local ratio at +20 dB / at +5 dB", p2);
    MEASURE ("10:1 local ratio at +20 dB / at +5 dB", p10);
    CHECK (p2 > 1.3 && p10 < 1.25 && p2 > p10 + 0.1);

    // Through the processor (2x oversampling, 1 kHz sine): the GR telemetry
    // follows the same law at the sine's peak level.
    double maxProcErr = 0;
    juce::String pcsv ("ratio,level_dbfs_peak,telemetry_db,law_db\n");
    for (int r : { r2, r4, r10, rInf })
        for (float level : { -26.0f, -20.0f, -14.0f, -6.0f })
        {
            auto p = makeGlue (48000.0, 480);
            setParam (*p, "threshold", knobFor (-20.0f));
            setParam (*p, "ratio", (float) r);
            setParam (*p, "attack", (float) a0_1);
            setParam (*p, "release", (float) rel1_2);
            setParam (*p, "character", (float) cClean);
            setParam (*p, "scHpf", 20.0f);
            p->prepareToPlay (48000.0, 480);
            std::vector<float> gr;
            render (*p, toBuffer (sine (48000.0, 1000.0, juce::Decibels::decibelsToGain (level), 48000), 2), 480, &gr);
            const double law = GlueModel::staticGainReductionDb (level, -20.0, r, a0_1, rel1_2);
            maxProcErr = std::max (maxProcErr, std::abs (gr.back() - law));
            pcsv << name (r) << "," << level << "," << gr.back() << "," << law << "\n";
        }
    outputDirectory().getChildFile ("GLUE_static_curves_processor.csv").replaceWithText (pcsv);
    MEASURE ("processor: max |GR telemetry - static law| (1 kHz sine) (dB)", maxProcErr);
    CHECK (maxProcErr < 0.15);
}

namespace
{
    // Independent reference of the documented closed-loop network (MODEL_SPEC 3):
    // RK4 on the continuous-time equations with many sub-steps per sample.
    std::vector<double> referenceStep (int ratio, double levelOverRatioThresholdDb, int attack, int release, double fs, int n)
    {
        const auto& law = GlueModel::ratioLaw (ratio);
        const double a = std::pow (10.0, levelOverRatioThresholdDb / 20.0);
        const double ta = GlueModel::attackSeconds (attack);
        const bool isAuto = release == relAuto;
        const double tr = isAuto ? GlueModel::autoMainS : GlueModel::releaseSeconds (release);
        auto target = [&] (double c)
        {
            if (law.limiter)
                return GlueModel::softKnee (levelOverRatioThresholdDb, GlueModel::limiterKneeDb);
            return law.slopeDb * GlueModel::softKnee (a * std::exp (-beta * c) - 1.0, law.knee);
        };
        auto deriv = [&] (double c, double d, double& dc, double& dd)
        {
            const double t = target (c);
            if (law.limiter)
            {
                // infinity: charge without leak toward the overshoot, release through the network
                dc = t > c ? (t - c) / ta : -c / tr - (isAuto ? (c - d) / GlueModel::autoCouplingS : 0.0);
            }
            else
            {
                dc = std::max (0.0, t - c) / ta - c / tr - (isAuto ? (c - d) / GlueModel::autoCouplingS : 0.0);
            }
            dd = isAuto ? (c - d) / GlueModel::autoSlowS : 0.0;
        };
        const double loopGain = law.limiter ? 1.0 : 1.0 + law.slopeDb * beta * 2.0;
        const int sub = juce::jlimit (8, 4096, (int) std::ceil (400.0 * loopGain / (ta * fs)));
        const double h = 1.0 / (fs * sub);
        std::vector<double> out ((size_t) n);
        double c = 0, d = 0;
        for (int i = 0; i < n; ++i)
        {
            for (int s = 0; s < sub; ++s)
            {
                double k1c, k1d, k2c, k2d, k3c, k3d, k4c, k4d;
                deriv (c, d, k1c, k1d);
                deriv (c + 0.5 * h * k1c, d + 0.5 * h * k1d, k2c, k2d);
                deriv (c + 0.5 * h * k2c, d + 0.5 * h * k2d, k3c, k3d);
                deriv (c + h * k3c, d + h * k3d, k4c, k4d);
                c += h / 6.0 * (k1c + 2 * k2c + 2 * k3c + k4c);
                d += h / 6.0 * (k1d + 2 * k2d + 2 * k3d + k4d);
            }
            out[(size_t) i] = c;
        }
        return out;
    }
}

TEST ("GLUE: attack per stepped setting: the loop follows the documented network, accelerated by the loop gain (CSV)")
{
    // Bare model at 384 kHz (the model rate at 192 kHz) against an RK4
    // reference of MODEL_SPEC section 3; step from silence to 15 dB above
    // the ratio's threshold (constant envelope).
    const double fs = 384000.0;
    juce::String csv ("ratio,attack_label_ms,effective_63_model_ms,effective_63_reference_ms,error_pct,final_gr_db\n");
    for (int r : { r2, r4, r10, rInf })
        for (int atk = 0; atk < GlueModel::numAttacks; ++atk)
        {
            const double thr = -20.0;
            const double over = 15.0;
            const double level = thr + GlueModel::ratioLaw (r).thresholdOffsetDb + over;
            const double ta = GlueModel::attackSeconds (atk);
            const int pre = 64, n = (int) (fs * std::max (0.01, ta * 12.0));
            auto m = makeModel (fs, r, atk, rel1_2, (float) thr);
            auto sig = concat (alternating (0.0f, pre), alternating ((float) std::pow (10.0, level / 20.0), n));
            const auto gr = grTrace (m, sig);
            std::vector<double> modelTrace (gr.begin() + pre, gr.end());
            const auto ref = referenceStep (r, over, atk, rel1_2, fs, n);
            const double finalModel = modelTrace.back(), finalRef = ref.back();
            const double tModel = crossing (modelTrace, 0, 0.632 * finalModel, true, fs);
            const double tRef = crossing (ref, 0, 0.632 * finalRef, true, fs);
            const double errPct = 100.0 * (tModel - tRef) / tRef;
            csv << name (r) << "," << ta * 1000.0 << "," << tModel * 1000.0 << "," << tRef * 1000.0 << "," << errPct << "," << finalModel << "\n";
            const auto tag = name (r) + ":1, attack " + glue::params::attackNames[atk];
            MEASURE (tag + ": effective 63 % attack (ms), model", tModel * 1000.0);
            CHECK_NEAR (finalModel, finalRef, 0.01);
            // Closed-loop attacks shorter than ~8 model samples are slowed by the
            // backward-Euler step (documented in MODEL_SPEC 3.4).
            const double tol = tRef * fs < 8.0 ? 0.30 : 0.03;
            CHECK_MSG (std::abs (tModel - tRef) <= tol * tRef + 0.5 / fs, tag + ": " + juce::String (errPct, 2) + " %");
            if (GlueModel::ratioLaw (r).limiter)
                CHECK_NEAR (tModel, ta, 0.01 * ta + 1.0 / fs); // infinity: exactly the label
            else
                CHECK (tModel < ta); // the loop accelerates the attack (T&H 2026)
        }
    outputDirectory().getChildFile ("GLUE_attack_steps.csv").replaceWithText (csv);

    // Through the processor and the GR telemetry: Attack reaches the DSP.
    auto rise = [] (int atk)
    {
        const double hostFs = 48000.0;
        auto p = makeGlue (hostFs, 16);
        setParam (*p, "attack", (float) atk);
        setParam (*p, "release", (float) rel1_2);
        setParam (*p, "threshold", knobFor (-30.0f));
        p->prepareToPlay (hostFs, 16);
        auto sig = concat (std::vector<float> (4800, 0.0f), sine (hostFs, 2000.0, 0.5f, 9600));
        std::vector<float> gr;
        render (*p, toBuffer (sig, 2), 16, &gr);
        std::vector<double> g (gr.begin(), gr.end());
        const double final = g.back();
        return crossing (g, 4800 / 16, 0.632 * final, true, hostFs / 16.0) * 1000.0;
    };
    const double fast = rise (a0_1), slow = rise (a30);
    MEASURE ("processor: GR 63 % rise, Attack 0.1 ms (ms, 16-sample resolution)", fast);
    MEASURE ("processor: GR 63 % rise, Attack 30 ms (ms)", slow);
    CHECK (slow > 5.0 * std::max (fast, 0.34)); // the loop accelerates 30 ms to a few ms at 24 dB over
}

TEST ("GLUE: release per stepped setting equals the label; Auto is the documented two-capacitor network (CSV)")
{
    const double fs = 96000.0;
    juce::String csv ("ratio,release,label_ms,measured_63_ms,error_pct\n");
    for (int r : { r1_5, r4, r10, rInf })
        for (int rel = 0; rel < 4; ++rel)
        {
            auto m = makeModel (fs, r, a0_1, rel, -20.0f);
            const int burst = (int) (0.3 * fs);
            const double label = GlueModel::releaseSeconds (rel);
            auto sig = concat (alternating (juce::Decibels::decibelsToGain (-5.0f), burst), alternating (0.0f, (int) (label * 3.0 * fs)));
            const auto gr = grTrace (m, sig);
            const double start = gr[(size_t) burst - 1];
            const double t = crossing (gr, burst, 0.36788 * start, false, fs);
            const double err = 100.0 * (t - label) / label;
            csv << name (r) << "," << glue::params::releaseNames[rel] << "," << label * 1000 << "," << t * 1000 << "," << err << "\n";
            MEASURE (name (r) + ":1 release " + glue::params::releaseNames[rel] + " measured 63 % (ms)", t * 1000.0);
            CHECK (start > 5.0);
            CHECK_NEAR (t, label, 0.001 * label + 1.0 / fs);
        }
    outputDirectory().getChildFile ("GLUE_release_steps.csv").replaceWithText (csv);

    // Auto: after the burst the network is linear (target 0), so its decay
    // must follow the analytic solution of MODEL_SPEC 3.3 from the state
    // (C1, C2) the model holds when the burst ends.
    auto autoCase = [&] (double burstS, double& recovery37, double& maxErr)
    {
        auto m = makeModel (fs, r4, a0_1, relAuto, -20.0f);
        const int burst = (int) (burstS * fs), after = (int) (8.0 * fs);
        std::vector<float> sig = alternating (juce::Decibels::decibelsToGain (-5.0f), burst);
        float* ch[] = { sig.data() };
        m.process (ch, 1, nullptr, 0, burst);
        const double c0 = m.getGainReductionDb(), d0 = m.getSlowCapacitorDb();

        const double a11 = -1.0 / GlueModel::autoMainS - 1.0 / GlueModel::autoCouplingS, a12 = 1.0 / GlueModel::autoCouplingS;
        const double a21 = 1.0 / GlueModel::autoSlowS, a22 = -1.0 / GlueModel::autoSlowS;
        const double tr = a11 + a22, det = a11 * a22 - a12 * a21;
        const double l1 = 0.5 * (tr + std::sqrt (tr * tr - 4 * det)), l2 = 0.5 * (tr - std::sqrt (tr * tr - 4 * det));
        // c(t) = A e^{l1 t} + B e^{l2 t}, c(0) = c0, c'(0) = a11 c0 + a12 d0
        const double dc0 = a11 * c0 + a12 * d0;
        const double B = (dc0 - l1 * c0) / (l2 - l1), A = c0 - B;

        std::vector<float> zeros ((size_t) after, 0.0f);
        const auto gr = grTrace (m, zeros);
        maxErr = 0;
        recovery37 = -1;
        for (int i = 0; i < after; ++i)
        {
            const double t = (i + 1) / fs;
            const double expected = A * std::exp (l1 * t) + B * std::exp (l2 * t);
            maxErr = std::max (maxErr, std::abs (gr[(size_t) i] - expected) / c0);
            if (recovery37 < 0 && gr[(size_t) i] <= 0.36788 * c0)
                recovery37 = t;
        }
        MEASURE ("Auto after a " + juce::String (burstS * 1000) + " ms burst: GR at burst end (dB)", c0);
        MEASURE ("Auto after a " + juce::String (burstS * 1000) + " ms burst: slow capacitor (dB)", d0);
        MEASURE ("Auto after a " + juce::String (burstS * 1000) + " ms burst: recovery to 37 % (ms)", recovery37 * 1000.0);
        MEASURE ("Auto after a " + juce::String (burstS * 1000) + " ms burst: max |model - analytic| / GR0", maxErr);
        MEASURE ("Auto network eigen time constants (s)", -1.0 / l1);
        MEASURE ("Auto network eigen time constants (s) ", -1.0 / l2);
    };
    double shortRec, longRec, shortErr, longErr;
    autoCase (0.02, shortRec, shortErr);
    autoCase (3.0, longRec, longErr);
    CHECK (shortErr < 0.005 && longErr < 0.005);
    CHECK (shortRec > 0.0 && shortRec < 0.1);  // a transient recovers within ~40 ms
    CHECK (longRec > 2.0);                     // sustained reduction takes seconds
    CHECK (longRec > 20.0 * shortRec);

    // Through the processor: telemetry recovery per fixed step and Auto's program dependence.
    auto recovery = [] (int rel, double burstS)
    {
        const double hostFs = 48000.0;
        auto p = makeGlue (hostFs, 64);
        setParam (*p, "release", (float) rel);
        setParam (*p, "attack", (float) a0_1);
        setParam (*p, "threshold", knobFor (-30.0f));
        p->prepareToPlay (hostFs, 64);
        const int burst = (int) (burstS * hostFs) / 64 * 64;
        auto sig = sine (hostFs, 1000.0, 0.5f, burst);
        sig.resize ((size_t) (burst + 10.0 * hostFs), 0.0f);
        std::vector<float> gr;
        render (*p, toBuffer (sig, 2), 64, &gr);
        const int endBlock = burst / 64 + 1;
        const float start = gr[(size_t) endBlock];
        for (int b = endBlock; b < (int) gr.size(); ++b)
            if (gr[(size_t) b] <= 0.36788f * start)
                return (b - endBlock) * 64.0 / hostFs;
        return -1.0;
    };
    for (int rel = 0; rel < 4; ++rel)
    {
        const double t = recovery (rel, 0.5);
        MEASURE ("processor: telemetry recovery to 37 %, release " + glue::params::releaseNames[rel] + " (ms)", t * 1000.0);
        CHECK_NEAR (t, GlueModel::releaseSeconds (rel), 0.03 * GlueModel::releaseSeconds (rel) + 0.003);
    }
    const double autoShort = recovery (relAuto, 0.03), autoLong = recovery (relAuto, 4.0);
    MEASURE ("processor: Auto recovery to 37 % after a 30 ms burst (ms)", autoShort * 1000.0);
    MEASURE ("processor: Auto recovery to 37 % after a 4 s burst (ms)", autoLong * 1000.0);
    CHECK (autoShort > 0.0 && autoLong > 10.0 * autoShort);
}

TEST ("GLUE: stereo link: a transient only in L reduces R by exactly the same gain")
{
    const double fs = 48000.0;
    auto p = makeGlue (fs, 64);
    setParam (*p, "character", (float) cClean);
    setParam (*p, "threshold", knobFor (-20.0f));
    setParam (*p, "attack", (float) a0_1);
    setParam (*p, "release", (float) rel1_2);
    p->prepareToPlay (fs, 64);

    const int n = (int) (fs * 1.2);
    auto l = std::vector<float> ((size_t) n, 0.0f);
    const auto burst = sine (fs, 1000.0, 0.5f, (int) (0.3 * fs));
    std::copy (burst.begin(), burst.end(), l.begin() + (int) (0.5 * fs));
    const auto r = sine (fs, 3000.0, 0.01f, n); // -40 dBFS: far below threshold on its own
    std::vector<float> gr;
    const auto in = stereo (l, r);
    const auto out = render (*p, in, 64, &gr);
    const int latency = p->getLatencySamples();

    // steady window inside the burst (after the attack), latency compensated
    const int w0 = (int) (0.65 * fs), wn = (int) (0.1 * fs);
    const float attL = db (rms (in.getReadPointer (0) + w0, wn)) - db (rms (out.getReadPointer (0) + w0 + latency, wn));
    const float attR = db (rms (in.getReadPointer (1) + w0, wn)) - db (rms (out.getReadPointer (1) + w0 + latency, wn));
    const float telemetry = gr[(size_t) ((w0 + wn) / 64)];
    MEASURE ("L (transient) attenuation in the burst (dB)", attL);
    MEASURE ("R (quiet, steady) attenuation in the burst (dB)", attR);
    MEASURE ("GR telemetry (dB)", telemetry);
    CHECK (attL > 6.0f);
    CHECK_NEAR (attR, attL, 0.02);
    CHECK_NEAR (attR, telemetry, 0.05);

    // Before the transient, R alone is not reduced at all.
    const float before = db (rms (in.getReadPointer (1) + 12000, 9600)) - db (rms (out.getReadPointer (1) + 12000 + latency, 9600));
    MEASURE ("R attenuation before the transient (dB)", before);
    CHECK (std::abs (before) < 0.01f);
}

TEST ("GLUE: dominant-channel detector: the louder channel sets the GR; polarity and the quieter channel do not matter")
{
    // Bare model, constant envelope. A sum or average detector would give
    // less GR for (A, 0) and none for (A, -A); a dominant (max) detector
    // gives the same GR in every case (SSL: L and R rectified separately,
    // the louder one controls both).
    const double fs = 96000.0;
    const float A = juce::Decibels::decibelsToGain (-8.0f);
    auto gr = [&] (float l, float r)
    {
        auto m = makeModel (fs, r4, a0_1, rel1_2, -20.0f);
        const int n = (int) (0.5 * fs);
        return grTrace (m, alternating (l, n), alternating (r, n)).back();
    };
    const double both = gr (A, A), leftOnly = gr (A, 0.0f), rightOnly = gr (0.0f, A), anti = gr (A, -A),
                 louderL = gr (A, 0.5f * A), louderR = gr (0.3f * A, A);
    auto mono = makeModel (fs, r4, a0_1, rel1_2, -20.0f);
    const double monoGr = grTrace (mono, alternating (A, (int) (0.5 * fs))).back();
    MEASURE ("GR (A, A)", both);
    MEASURE ("GR (A, 0)", leftOnly);
    MEASURE ("GR (0, A)", rightOnly);
    MEASURE ("GR (A, -A)", anti);
    MEASURE ("GR (A, A/2)", louderL);
    MEASURE ("GR (0.3 A, A)", louderR);
    CHECK (both > 5.0);
    for (double v : { leftOnly, rightOnly, anti, louderL, louderR, monoGr })
        CHECK_NEAR (v, both, 1.0e-4);
    // and the quieter channel never adds: (A, A) equals a single channel at A
    const double summedEquivalent = GlueModel::staticGainReductionDb (-8.0 + 6.0206, -20.0, r4, a0_1, rel1_2);
    MEASURE ("what a summing detector would give for (A, A) (dB)", summedEquivalent);
    CHECK (summedEquivalent > both + 2.0);
}

TEST ("GLUE: SC HPF and DARK: detector filter sweep matches detectorResponseDb (min, mid, max, musical) (CSV)")
{
    struct Case { const char* name; double fs, hpf, dark; };
    const Case cases[] = {
        { "hpf20_dark0_96k",    96000.0,  20.0, 0.0 },
        { "hpf77_dark0_96k",    96000.0,  77.46, 0.0 },
        { "hpf300_dark0_96k",   96000.0, 300.0, 0.0 },
        { "hpf120_dark50_96k",  96000.0, 120.0, 0.5 },
        { "hpf60_dark100_88k",  88200.0,  60.0, 1.0 },
        { "hpf200_dark100_384k",384000.0, 200.0, 1.0 },
        { "hpf20_dark100_192k", 192000.0, 20.0, 1.0 },
    };
    juce::String summary ("case,points,max_abs_error_db\n");
    for (const auto& c : cases)
    {
        glue::DetectorFilterControl ctl;
        ctl.prepare (c.fs);
        ctl.setTargets (c.hpf, c.dark);
        ctl.snap();
        juce::String csv ("hz,expected_db,measured_db,error_db\n");
        double maxErr = 0;
        int points = 0;
        for (double hz = 10.0; hz <= 20000.0; hz *= std::pow (2.0, 1.0 / 6.0))
        {
            glue::DetectorFilter f;
            const int settle = (int) (0.25 * c.fs), n = (int) (std::max (0.1, 20.0 / hz) * c.fs);
            std::vector<double> y ((size_t) n);
            const double w = 2.0 * juce::MathConstants<double>::pi * hz / c.fs;
            for (int i = 0; i < settle + n; ++i)
            {
                const double out = f.process (0.25 * std::sin (w * i), ctl);
                if (i >= settle)
                    y[(size_t) (i - settle)] = out;
            }
            // fit at the sample index offset of the window
            std::vector<double> shifted (y);
            const auto fit = sineFit (shifted.data(), n, c.fs, hz);
            const double measured = 20.0 * std::log10 (fit.amp / 0.25);
            const double expected = glue::detectorResponseDb (hz, c.hpf, c.dark, c.fs);
            csv << hz << "," << expected << "," << measured << "," << measured - expected << "\n";
            if (expected > -60.0)
            {
                maxErr = std::max (maxErr, std::abs (measured - expected));
                ++points;
            }
        }
        outputDirectory().getChildFile (juce::String ("GLUE_detector_filter_") + c.name + ".csv").replaceWithText (csv);
        summary << c.name << "," << points << "," << maxErr << "\n";
        MEASURE (juce::String (c.name) + ": max |measured - detectorResponseDb| (dB)", maxErr);
        CHECK_MSG (maxErr < 0.01, c.name);
        CHECK (points > 40);
    }
    outputDirectory().getChildFile ("GLUE_detector_filter_summary.csv").replaceWithText (summary);

    // Documented anchors: 12 dB/oct Butterworth (-3 dB at the cutoff), and the
    // gentlest 20 Hz setting only touches subsonic content.
    CHECK_NEAR (glue::detectorResponseDb (150.0, 150.0, 0.0, 96000.0), -3.0103, 0.01);
    CHECK_NEAR (glue::detectorResponseDb (37.5, 150.0, 0.0, 96000.0), -24.1, 0.2);
    MEASURE ("SC HPF 20 Hz: response at 40 Hz (dB)", glue::detectorResponseDb (40.0, 20.0, 0.0, 96000.0));
    MEASURE ("SC HPF 20 Hz: response at 10 Hz (dB)", glue::detectorResponseDb (10.0, 20.0, 0.0, 96000.0));
    CHECK (glue::detectorResponseDb (40.0, 20.0, 0.0, 96000.0) > -0.3);
    MEASURE ("DARK 100 %: detector emphasis at 10 kHz (dB)", glue::detectorResponseDb (10000.0, 20.0, 1.0, 96000.0));
    MEASURE ("DARK 100 %: detector emphasis at 1 kHz (dB)", glue::detectorResponseDb (1000.0, 20.0, 1.0, 96000.0));
    MEASURE ("DARK 100 %: detector emphasis at 100 Hz (dB)", glue::detectorResponseDb (100.0, 20.0, 1.0, 96000.0));
    CHECK (glue::detectorResponseDb (10000.0, 20.0, 1.0, 96000.0) > 10.0);
    CHECK (std::abs (glue::detectorResponseDb (100.0, 300.0, 1.0, 96000.0) - glue::detectorResponseDb (100.0, 300.0, 0.0, 96000.0)) < 0.2);
}

TEST ("GLUE: the processor's detector sees exactly detectorResponseDb (SC HPF on lows, DARK on highs)")
{
    // GR for a tone at level L equals the static law at L + response(f):
    // the real detector filters are the ones the response function describes.
    struct Case { double hz; float hpf, dark; float level; };
    const Case cases[] = { { 60.0, 150.0f, 0.0f, -6.0f }, { 40.0, 80.0f, 0.0f, -8.0f }, { 1000.0, 300.0f, 0.0f, -12.0f },
                           { 8000.0, 20.0f, 100.0f, -24.0f }, { 4000.0, 20.0f, 50.0f, -20.0f }, { 60.0, 300.0f, 100.0f, 0.0f } };
    double maxErr = 0;
    for (const auto& c : cases)
    {
        const double fs = 48000.0;
        auto p = makeGlue (fs, 480);
        setParam (*p, "threshold", knobFor (-20.0f));
        setParam (*p, "ratio", (float) r4);
        setParam (*p, "attack", (float) a0_1);
        setParam (*p, "release", (float) rel1_2);
        setParam (*p, "scHpf", c.hpf);
        setParam (*p, "dark", c.dark);
        p->prepareToPlay (fs, 480);
        std::vector<float> gr;
        // 100 ms raised-cosine fade-in: an abrupt start rings the SC HPF and
        // the 1.2 s release would carry that onset into the measurement.
        auto tone = sine (fs, c.hz, juce::Decibels::decibelsToGain (c.level), (int) (2.0 * fs));
        const int fade = (int) (0.1 * fs);
        for (int i = 0; i < fade; ++i)
            tone[(size_t) i] *= 0.5f - 0.5f * (float) std::cos (juce::MathConstants<double>::pi * i / fade);
        render (*p, toBuffer (tone, 2), 480, &gr);
        const double resp = glue::detectorResponseDb (c.hz, plainValue (*p, "scHpf"), c.dark * 0.01, p->getDetectorSampleRate());
        const double expected = GlueModel::staticGainReductionDb (c.level + resp, -20.0, r4, a0_1, rel1_2);
        const double err = gr.back() - expected;
        MEASURE (juce::String (c.hz) + " Hz at " + juce::String (c.level) + " dBFS, HPF " + juce::String (c.hpf) + ", DARK "
                     + juce::String (c.dark) + ": GR telemetry / expected (dB)", gr.back());
        MEASURE ("   expected from detectorResponseDb (" + juce::String (resp, 2) + " dB)", expected);
        maxErr = std::max (maxErr, std::abs (err));
    }
    MEASURE ("max |GR - static law at the filtered level| (dB)", maxErr);
    CHECK (maxErr < 0.25);
    CHECK (std::abs (48000.0 * 2 - makeGlue (48000.0, 64)->getDetectorSampleRate()) < 1e-9);
}

TEST ("GLUE: SC HPF reduces low-frequency pumping (kick-like 50 Hz bursts under a sustained 1 kHz tone)")
{
    const double fs = 48000.0;
    const int n = (int) (fs * 4.0);
    std::vector<float> sig = sine (fs, 1000.0, juce::Decibels::decibelsToGain (-18.0f), n);
    for (int k = 0; k < 10; ++k)
    {
        const int start = (int) (fs * (0.2 + 0.4 * k));
        for (int i = 0; i < (int) (0.25 * fs) && start + i < n; ++i)
        {
            const double t = i / fs;
            sig[(size_t) (start + i)] += (float) (0.7 * std::exp (-t / 0.06) * std::sin (2.0 * juce::MathConstants<double>::pi * 50.0 * t));
        }
    }

    auto pumping = [&] (float hpf, double& grSwing)
    {
        auto p = makeGlue (fs, 64);
        setParam (*p, "threshold", knobFor (-20.0f));
        setParam (*p, "ratio", (float) r4);
        setParam (*p, "attack", (float) a1);
        setParam (*p, "release", (float) rel0_1);
        setParam (*p, "character", (float) cClean);
        setParam (*p, "scHpf", hpf);
        p->prepareToPlay (fs, 64);
        std::vector<float> gr;
        const auto out = render (*p, toBuffer (sig, 2), 64, &gr);
        // Envelope of the 1 kHz tone: demodulate over 20 ms windows (whole
        // periods of both 1 kHz and 50 Hz, so the kick does not leak in).
        const int win = (int) (0.02 * fs), lat = p->getLatencySamples();
        double lo = 1e9, hi = -1e9;
        grSwing = 0;
        double gLo = 1e9, gHi = -1e9;
        for (int s = (int) (0.6 * fs); s + win + lat < n; s += win / 2)
        {
            double re = 0, im = 0;
            for (int i = 0; i < win; ++i)
            {
                const double ph = 2.0 * juce::MathConstants<double>::pi * 1000.0 * (s + i) / fs;
                re += out.getSample (0, s + lat + i) * std::cos (ph);
                im += out.getSample (0, s + lat + i) * std::sin (ph);
            }
            const double level = 20.0 * std::log10 (2.0 * std::sqrt (re * re + im * im) / win);
            lo = std::min (lo, level);
            hi = std::max (hi, level);
        }
        for (size_t b = (size_t) (0.6 * fs / 64); b < gr.size(); ++b)
        {
            gLo = std::min (gLo, (double) gr[b]);
            gHi = std::max (gHi, (double) gr[b]);
        }
        grSwing = gHi - gLo;
        return hi - lo;
    };
    double grOff, grOn;
    const double swingOff = pumping (20.0f, grOff), swingOn = pumping (200.0f, grOn);
    MEASURE ("1 kHz tone level swing, SC HPF 20 Hz (dB)", swingOff);
    MEASURE ("1 kHz tone level swing, SC HPF 200 Hz (dB)", swingOn);
    MEASURE ("GR swing, SC HPF 20 Hz (dB)", grOff);
    MEASURE ("GR swing, SC HPF 200 Hz (dB)", grOn);
    MEASURE ("pumping reduction factor", swingOff / std::max (swingOn, 1.0e-3));
    CHECK (swingOff > 4.0);
    CHECK (swingOn < swingOff / 4.0);
    CHECK (grOn < grOff / 4.0);
}

TEST ("GLUE: DARK emphasises the detector's highs: more GR on bright tones, the same on lows, no tonal change of the audio")
{
    auto grFor = [] (double hz, float dark, float level)
    {
        const double fs = 48000.0;
        auto p = makeGlue (fs, 480);
        setParam (*p, "threshold", knobFor (-20.0f));
        setParam (*p, "attack", (float) a1);
        setParam (*p, "release", (float) rel1_2);
        setParam (*p, "dark", dark);
        p->prepareToPlay (fs, 480);
        std::vector<float> gr;
        render (*p, toBuffer (sine (fs, hz, juce::Decibels::decibelsToGain (level), (int) fs), 2), 480, &gr);
        return gr.back();
    };
    const float hi0 = grFor (8000.0, 0.0f, -16.0f), hi50 = grFor (8000.0, 50.0f, -16.0f), hi100 = grFor (8000.0, 100.0f, -16.0f);
    const float lo0 = grFor (100.0, 0.0f, -16.0f), lo100 = grFor (100.0, 100.0f, -16.0f);
    MEASURE ("8 kHz GR at DARK 0 / 50 / 100 (dB)", hi0);
    MEASURE ("   DARK 50", hi50);
    MEASURE ("   DARK 100", hi100);
    MEASURE ("100 Hz GR at DARK 0 / 100 (dB)", lo0);
    MEASURE ("   DARK 100 ", lo100);
    CHECK (hi50 > hi0 + 1.0f && hi100 > hi50 + 1.0f);
    CHECK (std::abs (lo100 - lo0) < 0.15f);

    // No audible tonal component: without gain reduction the output is
    // bit-identical whatever DARK is.
    auto renderDark = [] (float dark)
    {
        auto p = makeGlue (48000.0, 256);
        setParam (*p, "threshold", 10.0f);
        setParam (*p, "dark", dark);
        p->prepareToPlay (48000.0, 256);
        juce::Random rng (7);
        juce::AudioBuffer<float> noise (2, 48000);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < noise.getNumSamples(); ++i)
                noise.setSample (ch, i, (rng.nextFloat() * 2.0f - 1.0f) * 0.05f);
        return render (*p, noise, 256);
    };
    CHECK_MSG (identical (renderDark (0.0f), renderDark (100.0f)), "DARK changed the audio path");
}

TEST ("GLUE: CRUSH: more GR (sensitivity, harder slope), faster release and more harmonics, as documented; Clean stays clean")
{
    // static: GR at a fixed input grows with CRUSH and follows the law
    double prev = -1.0;
    for (float crush : { 0.0f, 0.5f, 1.0f })
    {
        const double model = steadyGrOfModel (r4, -14.0, -20.0, a0_1, rel1_2, crush);
        const double law = GlueModel::staticGainReductionDb (-14.0, -20.0, r4, a0_1, rel1_2, crush);
        MEASURE ("4:1, 6 dB over threshold: GR at CRUSH " + juce::String (crush * 100) + " % (dB)", model);
        CHECK_NEAR (model, law, 0.02);
        CHECK (model > prev + 1.0);
        prev = model;
    }
    // harder slope: the secant over 10 dB above the (CRUSH-lowered) threshold rises
    const double t = -20.0 - GlueModel::crushScGainDb + GlueModel::ratioLaw (r4).thresholdOffsetDb;
    const double y = 10.0 - steadyGrOfModel (r4, t + 10.0, -20.0, a0_1, rel1_2, 1.0f);
    MEASURE ("4:1 at CRUSH 100 %: secant ratio over the first 10 dB", 10.0 / y);
    CHECK (10.0 / y > 6.0);

    // release: x (1 - 0.5 CRUSH)
    const double fs = 96000.0;
    auto m = makeModel (fs, r4, a0_1, rel0_6, -20.0f, cClean, 1.0f);
    const int burst = (int) (0.2 * fs);
    const auto gr = grTrace (m, concat (alternating (0.5f, burst), alternating (0.0f, (int) fs)));
    const double tRel = crossing (gr, burst, 0.36788 * gr[(size_t) burst - 1], false, fs);
    MEASURE ("release 0.6 s at CRUSH 100 % (ms)", tRel * 1000.0);
    CHECK_NEAR (tRel, 0.3, 0.001);

    // harmonics: TLEET THD grows with CRUSH at the same input
    const auto t0 = renderTone (cTleet, -6.0f, -16.0f, r4, 0.0f);
    const auto t100 = renderTone (cTleet, -6.0f, -16.0f, r4, 100.0f);
    MEASURE ("TLEET THD % at CRUSH 0", t0.thd * 100);
    MEASURE ("TLEET THD % at CRUSH 100", t100.thd * 100);
    CHECK (t100.thd > 3.0 * t0.thd);

    // Clean stays clean with CRUSH 100 (its drive acts on a linear stage)
    // (-18 dBFS: below the CRUSH-lowered threshold, so no GR ripple either)
    const auto c100 = renderTone (cClean, -18.0f, 0.0f, r4, 100.0f);
    MEASURE ("Clean THD % at CRUSH 100, no GR", c100.thd * 100);
    CHECK (c100.grDb == 0.0f);
    CHECK (c100.thd < 1.0e-7);
}

TEST ("GLUE: Character: Clean is clean, TLEET and Burn add bounded harmonics (TLEET mostly under GR, Burn with level); GR unchanged")
{
    // No GR: threshold 0 dBFS, -6 dBFS tone. With GR: threshold -16 dBFS.
    juce::String csv ("character,condition,thd_pct,h2_pct,h3_pct,gr_db\n");
    ToneResult res[3][2];
    for (int c : { cClean, cTleet, cBurn })
    {
        res[c][0] = renderTone (c, -6.0f, 0.0f);
        res[c][1] = renderTone (c, -6.0f, -16.0f);
        for (int g : { 0, 1 })
        {
            const auto& r = res[c][g];
            const juce::String tag = glue::params::characterNames[c] + (g == 0 ? ", no GR" : ", with GR");
            MEASURE (tag + ": THD %", r.thd * 100);
            MEASURE (tag + ": GR (dB)", r.grDb);
            csv << glue::params::characterNames[c] << "," << (g == 0 ? "no_gr" : "gr") << "," << r.thd * 100 << ","
                << r.h2 * 100 << "," << r.h3 * 100 << "," << r.grDb << "\n";
        }
    }
    // Burn: THD vs level (the output stage is level dependent)
    double prevThd = 0;
    for (float level : { -24.0f, -12.0f, -6.0f, 0.0f })
    {
        const auto r = renderTone (cBurn, level, 6.0f);
        MEASURE ("Burn, no GR: THD % at " + juce::String (level) + " dBFS", r.thd * 100);
        csv << "Burn,level_" << level << "," << r.thd * 100 << "," << r.h2 * 100 << "," << r.h3 * 100 << "," << r.grDb << "\n";
        CHECK (r.thd > prevThd * 2.0);
        prevThd = r.thd;
    }
    outputDirectory().getChildFile ("GLUE_character_thd.csv").replaceWithText (csv);

    // Clean bound: < 0.00001 % (-140 dB) without GR; with GR only the
    // detector ripple of the compression remains: < 0.01 %.
    CHECK (res[cClean][0].thd < 1.0e-7);
    CHECK (res[cClean][1].thd < 1.0e-4);
    // TLEET: measurable and subtle; more with gain reduction (the VCA term).
    CHECK (res[cTleet][0].thd > 20.0 * res[cClean][0].thd && res[cTleet][0].thd < 1.0e-3);
    CHECK (res[cTleet][1].thd > 1.5 * res[cTleet][0].thd && res[cTleet][1].thd < 2.0e-3);
    // Burn: clearly driven, bounded.
    CHECK (res[cBurn][0].thd > 10.0 * res[cTleet][0].thd && res[cBurn][0].thd < 0.03);
    CHECK (res[cBurn][1].thd > 5.0 * res[cTleet][1].thd && res[cBurn][1].thd < 0.03);
    // Character never changes the gain law.
    CHECK (res[cTleet][1].grDb > 3.0f);
    CHECK_NEAR (res[cTleet][1].grDb, res[cClean][1].grDb, 1.0e-3);
    CHECK_NEAR (res[cBurn][1].grDb, res[cClean][1].grDb, 1.0e-3);

    // Aliasing inspection: Burn, 15 kHz at 0 dBFS, 48 kHz host. The 3rd
    // harmonic (45 kHz) would fold to 3 kHz without oversampling.
    {
        const double fs = 48000.0;
        auto p = makeGlue (fs, 480);
        setParam (*p, "character", (float) cBurn);
        setParam (*p, "threshold", 10.0f);
        setParam (*p, "crush", 100.0f);
        p->prepareToPlay (fs, 480);
        auto out = render (*p, toBuffer (sine (fs, 15000.0, 0.9f, 96000), 2), 480);
        const float* d = out.getReadPointer (0) + 48000;
        const double fund = toneAmplitude (d, 48000, fs, 15000.0);
        const double alias3 = toneAmplitude (d, 48000, fs, 3000.0);
        MEASURE ("Burn + CRUSH 100, 15 kHz: 3rd-harmonic alias at 3 kHz (dBc)", 20.0 * std::log10 (alias3 / fund + 1e-12));
        CHECK (alias3 / fund < 1.0e-4);
    }
}

TEST ("GLUE: GR telemetry equals the applied VCA gain (error trace) and the attenuation measured on the audio")
{
    const double fs = 48000.0;
    const int block = 64;
    // Deterministic input: 1 kHz with level steps and bursts.
    const int n = (int) (fs * 3.0);
    std::vector<float> sig = sine (fs, 1000.0, 1.0f, n);
    const float levels[] = { 0.05f, 0.3f, 0.6f, 0.15f, 0.9f, 0.4f };
    for (int i = 0; i < n; ++i)
        sig[(size_t) i] *= levels[(i / (int) (0.5 * fs)) % 6];

    juce::String trace ("block,telemetry_db,applied_db,error_db\n");
    double maxErr = 0;
    for (int ratio : { r2, r4, r10, rInf })
    {
        auto p = makeGlue (fs, block);
        setParam (*p, "threshold", knobFor (-20.0f));
        setParam (*p, "ratio", (float) ratio);
        setParam (*p, "attack", (float) a1);
        setParam (*p, "release", (float) rel0_3);
        setParam (*p, "character", (float) cClean);
        p->prepareToPlay (fs, block);

        std::vector<float> applied ((size_t) (2 * block));
        auto& model = p->getModelForTesting();
        auto io = toBuffer (sig, 2);
        juce::MidiBuffer midi;
        std::vector<float> telemetry, measuredMin;
        for (int pos = 0; pos < n; pos += block)
        {
            model.setGainTraceForTesting (applied.data(), (int) applied.size());
            juce::AudioBuffer<float> view (io.getArrayOfWritePointers(), 2, pos, block);
            p->processBlock (view, midi);
            const float tel = p->grMeter.takeGainReduction();
            float g = 1.0f;
            for (int i = 0; i < model.getGainTraceCount(); ++i)
                g = std::min (g, applied[(size_t) i]);
            const double appliedDb = -20.0 * std::log10 ((double) g);
            const double err = tel - appliedDb;
            maxErr = std::max (maxErr, std::abs (err));
            if (ratio == r4)
                trace << pos / block << "," << tel << "," << appliedDb << "," << err << "\n";
            telemetry.push_back (tel);
            CHECK (model.getGainTraceCount() == 2 * block);
        }
        model.setGainTraceForTesting (nullptr, 0);

        // Audio check (Clean): in windows where the telemetry is steady, the
        // attenuation measured on the audio equals the telemetry.
        const int lat = p->getLatencySamples(), win = 960;
        double maxAudioErr = 0;
        int windows = 0;
        for (int s = 4800; s + win + lat < n; s += win)
        {
            const int b0 = s / block, b1 = (s + win) / block;
            float tMin = 1e9f, tMax = -1e9f;
            double tMean = 0;
            for (int b = b0; b < b1; ++b)
            {
                tMin = std::min (tMin, telemetry[(size_t) b]);
                tMax = std::max (tMax, telemetry[(size_t) b]);
                tMean += telemetry[(size_t) b];
            }
            tMean /= (b1 - b0);
            if (tMax - tMin > 0.02f || tMean < 0.5)
                continue;
            const double att = db (rms (sig.data() + s, win)) - db (rms (io.getReadPointer (0) + s + lat, win));
            maxAudioErr = std::max (maxAudioErr, std::abs (att - tMean));
            ++windows;
        }
        MEASURE (name (ratio) + ":1 max |telemetry - attenuation on the audio| (dB), steady windows", maxAudioErr);
        MEASURE (name (ratio) + ":1 steady windows compared", windows);
        CHECK (windows >= 20);
        CHECK (maxAudioErr < 0.05);
    }
    outputDirectory().getChildFile ("GLUE_gr_telemetry_error_trace.csv").replaceWithText (trace);
    MEASURE ("max |telemetry - applied VCA gain| over all blocks and ratios (dB)", maxErr);
    CHECK (maxErr < 1.0e-4);

    // Beyond the 20 dB the LED meter shows, the telemetry stays honest.
    auto p = makeGlue (fs, 480);
    setParam (*p, "threshold", knobFor (-30.0f));
    setParam (*p, "ratio", (float) rInf);
    setParam (*p, "attack", (float) a0_1);
    p->prepareToPlay (fs, 480);
    std::vector<float> gr;
    render (*p, toBuffer (sine (fs, 1000.0, 0.9f, 24000), 2), 480, &gr);
    MEASURE ("GR telemetry for 29 dB of overshoot at infinity (dB)", gr.back());
    CHECK (gr.back() > 27.0f);
}

TEST ("GLUE: sidechain: internal detection ignores an enabled key; external with a silent key gives no GR")
{
    const double fs = 48000.0;
    const auto main = toBuffer (sine (fs, 500.0, 0.5f, 24000), 2);
    auto loudKey = toBuffer (sine (fs, 200.0, 0.9f, 24000), 2);
    juce::AudioBuffer<float> silentKey (2, 24000);
    silentKey.clear();

    auto setup = [&] (int keyChannels, bool external)
    {
        auto p = makeGlue (fs, 256, 2, keyChannels);
        setParam (*p, "threshold", knobFor (-20.0f));
        setParam (*p, "scExternal", external ? 1.0f : 0.0f);
        p->prepareToPlay (fs, 256);
        return p;
    };

    // internal: an enabled but unselected key changes nothing (bit-exact vs no key bus)
    auto noBus = setup (0, false);
    auto withKey = setup (2, false);
    const auto ref = render (*noBus, main, 256);
    const auto internal = renderKeyed (*withKey, main, &loudKey, 256);
    CHECK_MSG (identical (ref, internal), "an unselected key altered the output");
    CHECK (! withKey->isUsingExternalSidechain());

    // external + silent key: no gain reduction at all, although the main input is 14 dB over
    auto ext = setup (2, true);
    std::vector<float> gr;
    const auto out = renderKeyed (*ext, main, &silentKey, 256, &gr);
    CHECK (ext->isUsingExternalSidechain());
    MEASURE ("external, silent key: max GR (dB)", *std::max_element (gr.begin(), gr.end()));
    CHECK (*std::max_element (gr.begin(), gr.end()) < 1.0e-6f);
    std::vector<float> grInt;
    render (*setup (0, false), main, 256, &grInt);
    MEASURE ("internal: GR for the same main input (dB)", grInt.back());
    CHECK (grInt.back() > 5.0f);
    const int lat = ext->getLatencySamples();
    const float att = db (rms (main.getReadPointer (0) + 12000, 9600)) - db (rms (out.getReadPointer (0) + 12000 + lat, 9600));
    MEASURE ("external, silent key: attenuation of the main signal (dB)", att);
    CHECK (std::abs (att) < 0.01f);
}

TEST ("GLUE: sidechain: a sine key follows the static law; a transient key ducks the main signal with the documented timing")
{
    const double fs = 48000.0;
    auto p = makeGlue (fs, 64, 2, 2);
    setParam (*p, "threshold", knobFor (-20.0f));
    setParam (*p, "attack", (float) a0_1);
    setParam (*p, "release", (float) rel0_3);
    setParam (*p, "character", (float) cClean);
    setParam (*p, "scExternal", 1.0f);
    p->prepareToPlay (fs, 64);

    // main below threshold, key 1 kHz at -8 dBFS: GR = the static law at the key level
    const auto main = toBuffer (sine (fs, 300.0, juce::Decibels::decibelsToGain (-30.0f), 48000), 2);
    const auto key = toBuffer (sine (fs, 1000.0, juce::Decibels::decibelsToGain (-8.0f), 48000), 2);
    std::vector<float> gr;
    const auto out = renderKeyed (*p, main, &key, 64, &gr);
    const double law = GlueModel::staticGainReductionDb (-8.0, -20.0, r4, a0_1, rel0_3);
    MEASURE ("sine key at -8 dBFS: GR telemetry (dB)", gr.back());
    MEASURE ("static law at -8 dBFS (dB)", law);
    CHECK_NEAR (gr.back(), law, 0.15);
    const int lat = p->getLatencySamples();
    const float att = db (rms (main.getReadPointer (0) + 36000, 9600)) - db (rms (out.getReadPointer (0) + 36000 + lat, 9600));
    MEASURE ("main signal attenuation (dB)", att);
    CHECK_NEAR (att, gr.back(), 0.05);

    // transient key: 50 ms bursts every 500 ms; the main tone ducks and recovers with the release (0.3 s)
    auto q = makeGlue (fs, 64, 2, 2);
    setParam (*q, "threshold", knobFor (-20.0f));
    setParam (*q, "attack", (float) a0_1);
    setParam (*q, "release", (float) rel0_3);
    setParam (*q, "ratio", (float) rInf);
    setParam (*q, "scExternal", 1.0f);
    q->prepareToPlay (fs, 64);
    std::vector<float> k ((size_t) (fs * 2.0), 0.0f);
    for (int b = 0; b < 4; ++b)
        for (int i = 0; i < (int) (0.05 * fs); ++i)
            k[(size_t) ((int) (fs * (0.1 + 0.5 * b)) + i)] = 0.5f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 1000.0 * i / fs);
    std::vector<float> grT;
    const auto burstKey = toBuffer (k, 2);
    renderKeyed (*q, toBuffer (sine (fs, 300.0, 0.05f, (int) (fs * 2.0)), 2), &burstKey, 64, &grT);
    const int burstEndBlock = (int) (fs * 0.15) / 64;
    const double peak = grT[(size_t) burstEndBlock];
    const double after = grT[(size_t) (burstEndBlock + (int) (0.3 * fs) / 64)];
    MEASURE ("transient key: GR at the end of a burst (dB)", peak);
    MEASURE ("transient key: GR 0.3 s later (dB)", after);
    CHECK_NEAR (peak, GlueModel::staticGainReductionDb (-6.02, -20.0, rInf, a0_1, rel0_3), 0.1);
    CHECK_NEAR (after / peak, std::exp (-1.0), 0.03);
    CHECK (grT[(size_t) ((int) (fs * 0.08) / 64)] < 1.0e-6f); // nothing before the first burst
}

TEST ("GLUE: sidechain: a stereo key is dominant-linked (L only, R only, anti-phase); a mono key works")
{
    const double fs = 48000.0;
    const auto main = toBuffer (sine (fs, 300.0, 0.01f, 24000), 2);
    const auto tone = sine (fs, 1000.0, juce::Decibels::decibelsToGain (-10.0f), 24000);
    const std::vector<float> zero (tone.size(), 0.0f);
    std::vector<float> neg (tone);
    for (auto& v : neg) v = -v;

    auto grWith = [&] (const juce::AudioBuffer<float>& key)
    {
        auto p = makeGlue (fs, 128, 2, key.getNumChannels());
        setParam (*p, "threshold", knobFor (-20.0f));
        setParam (*p, "attack", (float) a0_1);
        setParam (*p, "release", (float) rel1_2);
        setParam (*p, "scExternal", 1.0f);
        p->prepareToPlay (fs, 128);
        std::vector<float> gr;
        renderKeyed (*p, main, &key, 128, &gr);
        CHECK (p->isUsingExternalSidechain());
        return gr.back();
    };
    const float both = grWith (stereo (tone, tone)), lOnly = grWith (stereo (tone, zero)), rOnly = grWith (stereo (zero, tone)),
                anti = grWith (stereo (tone, neg)), monoKey = grWith (toBuffer (tone, 1));
    MEASURE ("stereo key L=R: GR (dB)", both);
    MEASURE ("stereo key L only", lOnly);
    MEASURE ("stereo key R only", rOnly);
    MEASURE ("stereo key anti-phase", anti);
    MEASURE ("mono key", monoKey);
    CHECK (both > 5.0f);
    for (float v : { lOnly, rOnly, anti, monoKey })
        CHECK_NEAR (v, both, 0.01);
}

TEST ("GLUE: sidechain: External SC with the bus disabled falls back to the internal detector; the source is reported")
{
    const double fs = 48000.0;
    const auto main = toBuffer (sine (fs, 700.0, 0.5f, 24000), 2);

    auto run = [&] (int keyChannels, bool external)
    {
        auto p = makeGlue (fs, 256, 2, keyChannels);
        setParam (*p, "threshold", knobFor (-20.0f));
        setParam (*p, "scExternal", external ? 1.0f : 0.0f);
        p->prepareToPlay (fs, 256);
        auto key = toBuffer (sine (fs, 100.0, 0.9f, 24000), std::max (1, keyChannels));
        auto out = renderKeyed (*p, main, keyChannels > 0 ? &key : nullptr, 256);
        return std::make_pair (out, p->isUsingExternalSidechain());
    };
    const auto internalRef = run (0, false);
    const auto fallback = run (0, true);
    CHECK (! internalRef.second);
    CHECK (! fallback.second);
    CHECK_MSG (identical (internalRef.first, fallback.first), "external selected without a bus is not the internal detector");

    // switching External SC on and off at run time crossfades without a click
    auto p = makeGlue (fs, 64, 2, 2);
    setParam (*p, "threshold", knobFor (-20.0f));
    setParam (*p, "attack", (float) a0_1);
    setParam (*p, "release", (float) rel0_1);
    p->prepareToPlay (fs, 64);
    juce::MidiBuffer midi;
    float prev = 0.0f, maxJump = 0.0f;
    bool sawExternal = false;
    for (int b = 0; b < 1500; ++b)
    {
        if (b % 100 == 50)
            setParam (*p, "scExternal", plainValue (*p, "scExternal") > 0.5f ? 0.0f : 1.0f);
        juce::AudioBuffer<float> io (4, 64);
        const auto m = sine (fs, 200.0, 0.3f, 64, 2.0 * juce::MathConstants<double>::pi * 200.0 * b * 64 / fs);
        const auto k = sine (fs, 90.0, 0.9f, 64, 2.0 * juce::MathConstants<double>::pi * 90.0 * b * 64 / fs);
        for (int ch = 0; ch < 2; ++ch) { io.copyFrom (ch, 0, m.data(), 64); io.copyFrom (2 + ch, 0, k.data(), 64); }
        p->processBlock (io, midi);
        sawExternal = sawExternal || p->isUsingExternalSidechain();
        for (int i = 0; i < 64; ++i)
        {
            maxJump = std::max (maxJump, std::abs (io.getSample (0, i) - prev));
            prev = io.getSample (0, i);
        }
    }
    MEASURE ("max sample jump while toggling External SC", maxJump);
    CHECK (sawExternal);
    CHECK (maxJump < 0.05f); // a 200 Hz sine at 0.3 moves <= 0.008 per sample
}

TEST ("GLUE: sidechain: the key never leaks into the output")
{
    const double fs = 48000.0;
    auto p = makeGlue (fs, 128, 2, 2);
    setParam (*p, "threshold", knobFor (-20.0f));
    setParam (*p, "character", (float) cBurn);
    setParam (*p, "crush", 100.0f);
    setParam (*p, "makeup", 20.0f);
    setParam (*p, "scExternal", 1.0f);
    p->prepareToPlay (fs, 128);

    // silent main, loud key: the output is silent
    juce::AudioBuffer<float> silent (2, 48000);
    silent.clear();
    const auto key = toBuffer (sine (fs, 1000.0, 0.9f, 48000), 2);
    const auto out = renderKeyed (*p, silent, &key, 128);
    MEASURE ("silent main, loud key: output peak", out.getMagnitude (0, out.getNumSamples()));
    CHECK (out.getMagnitude (0, out.getNumSamples()) < 1.0e-7f);

    // main 300 Hz, key 1234 Hz below threshold (no GR): no 1234 Hz in the output
    auto q = makeGlue (fs, 128, 2, 2);
    setParam (*q, "threshold", 10.0f);
    setParam (*q, "scExternal", 1.0f);
    q->prepareToPlay (fs, 128);
    const auto main = toBuffer (sine (fs, 300.0, 0.5f, 96000), 2);
    const auto quietKey = toBuffer (sine (fs, 1234.0, 0.2f, 96000), 2);
    const auto o2 = renderKeyed (*q, main, &quietKey, 128);
    const double leak = toneAmplitude (o2.getReadPointer (0) + 48000, 48000, fs, 1234.0);
    MEASURE ("key-frequency component in the output (dBFS)", 20.0 * std::log10 (leak + 1e-15));
    CHECK (leak < 1.0e-6);
}

TEST ("GLUE: sidechain: SC HPF and DARK act on the external key too")
{
    const double fs = 48000.0;
    auto grFor = [&] (double keyHz, float hpf, float dark)
    {
        auto p = makeGlue (fs, 256, 2, 2);
        setParam (*p, "threshold", knobFor (-20.0f));
        setParam (*p, "attack", (float) a0_1);
        setParam (*p, "release", (float) rel1_2);
        setParam (*p, "scHpf", hpf);
        setParam (*p, "dark", dark);
        setParam (*p, "scExternal", 1.0f);
        p->prepareToPlay (fs, 256);
        std::vector<float> gr;
        const auto keyTone = toBuffer (sine (fs, keyHz, juce::Decibels::decibelsToGain (-10.0f), 48000), 2);
        renderKeyed (*p, toBuffer (sine (fs, 500.0, 0.01f, 48000), 2), &keyTone, 256, &gr);
        const double resp = glue::detectorResponseDb (keyHz, hpf, dark * 0.01, p->getDetectorSampleRate());
        const double expected = GlueModel::staticGainReductionDb (-10.0 + resp, -20.0, r4, a0_1, rel1_2);
        MEASURE ("key " + juce::String (keyHz) + " Hz, HPF " + juce::String (hpf) + ", DARK " + juce::String (dark)
                     + ": GR / expected (dB)", gr.back());
        MEASURE ("   expected", expected);
        CHECK_NEAR (gr.back(), expected, 0.2);
        return gr.back();
    };
    const float low20 = grFor (50.0, 20.0f, 0.0f), low300 = grFor (50.0, 300.0f, 0.0f);
    const float hi0 = grFor (8000.0, 20.0f, 0.0f), hi100 = grFor (8000.0, 20.0f, 100.0f);
    CHECK (low300 < low20 - 3.0f);
    CHECK (hi100 > hi0 + 3.0f);
}

TEST ("GLUE: ratio, attack, release, character and SC HPF switching under compression is click-free")
{
    const double fs = 48000.0;
    const int block = 64, blocks = 1600;
    auto run = [&] (bool switching, int fixedRatio, int fixedChar)
    {
        auto p = makeGlue (fs, block);
        setParam (*p, "threshold", knobFor (-26.0f));
        setParam (*p, "attack", (float) a0_3);
        setParam (*p, "release", (float) rel0_1);
        setParam (*p, "ratio", (float) fixedRatio);
        setParam (*p, "character", (float) fixedChar);
        p->prepareToPlay (fs, block);
        juce::MidiBuffer midi;
        float y1 = 0, y2 = 0, maxCurv = 0;
        const int ratioSeq[] = { r2, rInf, r1_5, r10, r4, rInf, r2 };
        for (int b = 0; b < blocks; ++b)
        {
            if (switching && b % 50 == 0)
            {
                const int k = b / 50;
                setParam (*p, "ratio", (float) ratioSeq[k % 7]);
                setParam (*p, "character", (float) (k % 3));
                setParam (*p, "release", (float) (k % 5));
                setParam (*p, "attack", (float) (k % 6));
                setParam (*p, "scHpf", k % 2 == 0 ? 20.0f : 300.0f);
            }
            auto buf = toBuffer (sine (fs, 100.0, 0.3f, block, 2.0 * juce::MathConstants<double>::pi * 100.0 * b * block / fs), 2);
            p->processBlock (buf, midi);
            for (int i = 0; i < block; ++i)
            {
                const float y = buf.getSample (0, i);
                if (b > 20)
                    maxCurv = std::max (maxCurv, std::abs (y - 2.0f * y1 + y2));
                y2 = y1;
                y1 = y;
            }
        }
        return maxCurv;
    };
    float baseline = 0;
    for (int r = 0; r < GlueModel::numRatios; ++r)
        for (int c = 0; c < GlueModel::numCharacters; ++c)
            baseline = std::max (baseline, run (false, r, c));
    const float switched = run (true, r4, cClean);
    MEASURE ("max |2nd difference| over all fixed ratio/character settings", baseline);
    MEASURE ("max |2nd difference| while switching ratio/character/attack/release/SC HPF", switched);
    CHECK (switched < 2.0f * baseline);
}

TEST ("GLUE: rapid automation of every parameter stays finite and click-free")
{
    const double fs = 48000.0;
    auto p = makeGlue (fs, 64, 2, 2);
    juce::Random rng (42);
    float prevSample = 0.0f, maxJump = 0.0f;
    juce::MidiBuffer midi;
    for (int b = 0; b < 3000; ++b)
    {
        setParam (*p, "threshold", -20.0f + rng.nextFloat() * 30.0f);
        setParam (*p, "attack", (float) rng.nextInt (6));
        setParam (*p, "release", (float) rng.nextInt (5));
        setParam (*p, "ratio", (float) rng.nextInt (5));
        setParam (*p, "makeup", -6.0f + rng.nextFloat() * 12.0f);
        setParam (*p, "scHpf", 20.0f + rng.nextFloat() * 280.0f);
        setParam (*p, "mix", rng.nextFloat() * 100.0f);
        setParam (*p, "character", (float) rng.nextInt (3));
        setParam (*p, "dark", rng.nextFloat() * 100.0f);
        setParam (*p, "crush", rng.nextFloat() * 100.0f);
        setParam (*p, "output", -6.0f + rng.nextFloat() * 12.0f);
        setParam (*p, "scExternal", rng.nextBool() ? 1.0f : 0.0f);
        juce::AudioBuffer<float> io (4, 64);
        const auto m = sine (fs, 200.0, 0.3f, 64, 2.0 * juce::MathConstants<double>::pi * 200.0 * b * 64 / fs);
        const auto k = sine (fs, 77.0, 0.8f, 64, 2.0 * juce::MathConstants<double>::pi * 77.0 * b * 64 / fs);
        for (int ch = 0; ch < 2; ++ch) { io.copyFrom (ch, 0, m.data(), 64); io.copyFrom (2 + ch, 0, k.data(), 64); }
        p->processBlock (io, midi);
        for (int i = 0; i < 64; ++i)
        {
            CHECK (std::isfinite (io.getSample (0, i)));
            maxJump = std::max (maxJump, std::abs (io.getSample (0, i) - prevSample));
            prevSample = io.getSample (0, i);
        }
    }
    // a 200 Hz sine at up to ~0.6 peak moves at most ~0.016 per sample
    MEASURE ("max sample-to-sample jump", maxJump);
    CHECK (maxJump < 0.06f);
}

TEST ("GLUE: the editor (open, resized, UI properties) does not alter the audio")
{
    const double fs = 48000.0;
    auto renderWith = [&] (bool withEditor)
    {
        auto p = makeGlue (fs, 256);
        setParam (*p, "threshold", knobFor (-24.0f));
        setParam (*p, "dark", 40.0f);
        setParam (*p, "crush", 30.0f);
        p->prepareToPlay (fs, 256);
        std::unique_ptr<juce::AudioProcessorEditor> ed;
        if (withEditor)
        {
            ed.reset (p->createEditor());
            CHECK (ed != nullptr);
            ed->setSize (900, 700);
            p->setUiProperty ("scale", 1.5);
            p->setUiProperty ("meterMode", 1);
        }
        juce::Random rng (3);
        juce::AudioBuffer<float> in (2, 48000);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < in.getNumSamples(); ++i)
                in.setSample (ch, i, 0.4f * (float) std::sin (i * 0.05 + ch) + (rng.nextFloat() - 0.5f) * 0.2f);
        juce::AudioBuffer<float> out (in);
        juce::MidiBuffer midi;
        for (int pos = 0; pos < out.getNumSamples(); pos += 256)
        {
            juce::AudioBuffer<float> view (out.getArrayOfWritePointers(), 2, pos, 256);
            p->processBlock (view, midi);
            if (ed != nullptr && pos % 4096 == 0)
            {
                ed->setSize (900 + pos % 300, 700);
                juce::Image img (juce::Image::ARGB, ed->getWidth(), ed->getHeight(), true);
                juce::Graphics g (img);
                ed->paintEntireComponent (g, false);
                p->grMeter.takeGainReduction(); // a UI reading the meters
                p->inputMeter.takeRms();
                p->outputMeter.takePeak();
            }
        }
        ed.reset();
        return out;
    };
    CHECK_MSG (identical (renderWith (false), renderWith (true)), "the editor altered the audio");
}

TEST ("GLUE: state round trip restores every parameter and UI settings and renders identically")
{
    const double fs = 48000.0;
    auto a = makeGlue (fs, 256);
    setParam (*a, "threshold", -7.5f);
    setParam (*a, "attack", (float) a3);
    setParam (*a, "release", (float) rel0_6);
    setParam (*a, "ratio", (float) r10);
    setParam (*a, "makeup", 4.5f);
    setParam (*a, "scHpf", 133.0f);
    setParam (*a, "mix", 72.0f);
    setParam (*a, "character", (float) cBurn);
    setParam (*a, "dark", 35.0f);
    setParam (*a, "crush", 61.0f);
    setParam (*a, "output", -3.5f);
    setParam (*a, "scExternal", 1.0f);
    a->setUiProperty ("scale", 1.25);

    juce::MemoryBlock state;
    a->getStateInformation (state);
    auto b = makeGlue (fs, 256);
    b->setStateInformation (state.getData(), (int) state.getSize());
    for (auto* param : a->getParameters())
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (param))
            CHECK_NEAR (r->getValue(), b->apvts.getParameter (r->paramID)->getValue(), 1e-6);
    CHECK_NEAR ((double) b->getUiProperty ("scale", 1.0), 1.25, 1e-9);

    a->prepareToPlay (fs, 256);
    b->prepareToPlay (fs, 256);
    const auto sig = toBuffer (sine (fs, 300.0, 0.4f, 48000), 2);
    CHECK_MSG (identical (render (*a, sig, 256), render (*b, sig, 256)), "restored instance renders differently");
    auto fresh = makeGlue (fs, 256);
    CHECK (! identical (render (*fresh, sig, 256), render (*b, sig, 256)));

    b->setStateInformation ("nonsense", 8);
    CHECK_NEAR (plainValue (*b, "mix"), 72.0f, 0.01);
}

TEST ("GLUE: factory presets load, set every parameter and reject other plugins' presets")
{
    GlueProcessor p;
    const auto& list = p.presets.getPresets();
    int factory = 0;
    for (auto& pr : list) factory += pr.isFactory ? 1 : 0;
    MEASURE ("factory presets", factory);
    CHECK (factory >= 10);

    std::set<int> ratiosSeen, charactersSeen;
    for (int i = 0; i < (int) list.size(); ++i)
        if (list[(size_t) i].isFactory)
        {
            p.loadPreset (i);
            CHECK (list[(size_t) i].params.values.size() == 12);
            for (auto& [id, v] : list[(size_t) i].params.values)
            {
                auto* r = p.apvts.getParameter (id);
                CHECK_MSG (r != nullptr, id);
                if (r) CHECK_NEAR (r->convertFrom0to1 (r->getValue()), v, 0.06);
            }
            CHECK (! p.isPresetModified());
            CHECK (plainValue (p, "scExternal") < 0.5f); // factory presets never switch the routing on
            ratiosSeen.insert (juce::roundToInt (plainValue (p, "ratio")));
            charactersSeen.insert (juce::roundToInt (plainValue (p, "character")));
        }
    CHECK (ratiosSeen.size() >= 4 && charactersSeen.size() == 3);

    setParam (p, "mix", 13.0f);
    CHECK (p.isPresetModified());
    PresetManager::Preset out;
    CHECK (p.presets.parse (R"({"format":"tleet-preset","version":1,"plugin":"BITE","name":"x","params":{}})", out).failed());
    CHECK (p.presets.parse (R"({"format":"tleet-preset","version":99,"plugin":"GLUE","name":"x","params":{}})", out).failed());
    const auto json = p.stateToClipboardJson();
    setParam (p, "mix", 99.0f);
    CHECK (p.pasteStateFromJson (json).wasOk());
    CHECK_NEAR (plainValue (p, "mix"), 13.0f, 0.06);
}

TEST ("GLUE: CPU cost per instance (measurement)")
{
    for (double fs : { 44100.0, 48000.0, 96000.0 })
        for (int keyChannels : { 0, 2 })
        {
            auto p = makeGlue (fs, 128, 2, keyChannels);
            setParam (*p, "threshold", knobFor (-24.0f));
            setParam (*p, "release", (float) relAuto);
            setParam (*p, "character", (float) cTleet);
            setParam (*p, "dark", 30.0f);
            setParam (*p, "scHpf", 80.0f);
            setParam (*p, "scExternal", keyChannels > 0 ? 1.0f : 0.0f);
            setParam (*p, "mix", 80.0f);
            juce::Random rng (1);
            const int n = (int) (fs * 10.0);
            juce::AudioBuffer<float> in (2, n), key (2, n);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < n; ++i)
                {
                    in.setSample (ch, i, 0.5f * (float) std::sin (i * 0.031) * (rng.nextFloat() * 0.6f + 0.4f));
                    key.setSample (ch, i, 0.5f * (float) std::sin (i * 0.007));
                }
            const auto t0 = juce::Time::getMillisecondCounterHiRes();
            renderKeyed (*p, in, keyChannels > 0 ? &key : nullptr, 128);
            const double ms = juce::Time::getMillisecondCounterHiRes() - t0;
            const double load = ms / 10000.0 * 100.0;
            MEASURE (juce::String (fs) + " Hz, block 128, key " + juce::String (keyChannels) + ": % of one core", load);
            CHECK (load < 10.0);
        }
}
