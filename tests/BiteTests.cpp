#include "TestFramework.h"
#include "TestSignals.h"
#include <cstring>
#include "../plugins/BITE/PluginProcessor.h"
#include "../plugins/BITE/Parameters.h"
#include "../plugins/BITE/dsp/BiteModel.h"

using namespace tleet;
using namespace tleet::test;
using tleet::bite::BiteProcessor;
using tleet::bite::BiteModel;

namespace
{
    enum RatioIdx { r4 = 0, r8, r12, r20, rAll };
    enum CharIdx { cClean = 0, cTleet, cBurn };

    void setParam (BiteProcessor& p, const char* id, float plain)
    {
        auto* r = p.apvts.getParameter (id);
        r->setValueNotifyingHost (r->convertTo0to1 (plain));
    }

    void setNormalised (BiteProcessor& p, const char* id, float normalised)
    {
        p.apvts.getParameter (id)->setValueNotifyingHost (normalised);
    }

    // Bit-exact sample comparison (also avoids -Wfloat-equal).
    bool sameBits (float a, float b) noexcept { return std::memcmp (&a, &b, sizeof (float)) == 0; }

    float plainValue (BiteProcessor& p, const char* id)
    {
        auto* r = p.apvts.getParameter (id);
        return r->convertFrom0to1 (r->getValue());
    }

    // Renders `input` through the processor in blocks of `block` samples.
    juce::AudioBuffer<float> render (BiteProcessor& p, const juce::AudioBuffer<float>& input, int block)
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

    std::unique_ptr<BiteProcessor> makeBite (double fs, int block, int channels = 2)
    {
        auto p = std::make_unique<BiteProcessor>();
        const auto set = channels == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo();
        BiteProcessor::BusesLayout layout;
        layout.inputBuses.add (set);
        layout.outputBuses.add (set);
        p->setBusesLayout (layout);
        p->setRateAndBufferSizeDetails (fs, block);
        p->prepareToPlay (fs, block);
        return p;
    }

    // Plain attack/release value at a knob position (0 = fully anticlockwise).
    float knobToPlain (const char* id, float normalised)
    {
        BiteProcessor p;
        return p.apvts.getParameter (id)->convertFrom0to1 (normalised);
    }

    BiteModel makeModel (double fs, int ratio, int character, float attackUs, float releaseMs)
    {
        BiteModel m;
        m.setRatio (ratio);
        m.setCharacter (character);
        m.setAttackMicroseconds (attackUs);
        m.setReleaseMilliseconds (releaseMs);
        m.setInputDb (0.0f);
        m.setOutputDb (0.0f);
        m.prepare (fs);
        return m;
    }

    // Runs the bare model sample by sample on `sig` and returns the GR (dB) per sample.
    std::vector<float> grTrace (BiteModel& m, std::vector<float>& sig)
    {
        std::vector<float> gr;
        gr.reserve (sig.size());
        for (auto& s : sig)
        {
            float* ch[] = { &s };
            m.process (ch, 1, 1);
            gr.push_back (m.getGainReductionDb());
        }
        return gr;
    }

    // Constant-envelope test signal (+A, -A, ...): the rectified detector input
    // is a clean step, so attack and release are measured without carrier ripple.
    std::vector<float> alternating (float amp, int n)
    {
        std::vector<float> v ((size_t) n);
        for (int i = 0; i < n; ++i)
            v[(size_t) i] = (i & 1) ? -amp : amp;
        return v;
    }

    // Steady-state point of the bare model for a 1 kHz sine at `peakDb` dBFS peak.
    struct StaticPoint { float inDb, outDb, grDb; };
    StaticPoint staticPoint (int ratio, float peakDb, int character = cClean)
    {
        const double fs = 96000.0;
        auto m = makeModel (fs, ratio, character, 20.0f, 1100.0f);
        auto sig = sine (fs, 1000.0, juce::Decibels::decibelsToGain (peakDb), (int) (fs * 2.0));
        float* ch[] = { sig.data() };
        m.process (ch, 1, (int) sig.size());
        const int tail = (int) (fs * 0.5);
        const float out = rms (sig.data() + sig.size() - (size_t) tail, tail);
        return { peakDb, db (out) + 3.0103f, -db (m.getCurrentGain()) };
    }

    float measuredRatio (int ratio, float loDb, float hiDb)
    {
        const auto a = staticPoint (ratio, loDb), b = staticPoint (ratio, hiDb);
        return (b.inDb - a.inDb) / (b.outDb - a.outDb);
    }

    // Time (s) for the bare model's GR to reach 63 % of its settled value after a step.
    float attackTime (int ratio, float attackUs, double fs = 192000.0)
    {
        auto m = makeModel (fs, ratio, cClean, attackUs, 1100.0f);
        const int pre = (int) (0.01 * fs), post = (int) (0.03 * fs);
        auto sig = alternating (0.0f, pre);
        auto loud = alternating (juce::Decibels::decibelsToGain (5.0f), post); // T(4:1) + 20 dB
        sig.insert (sig.end(), loud.begin(), loud.end());
        const auto gr = grTrace (m, sig);
        const float final = gr.back();
        for (int i = pre; i < (int) gr.size(); ++i)
            if (gr[(size_t) i] >= 0.632f * final)
                return (float) ((i - pre + 1) / fs); // +1: the step lands on sample `pre`
        return -1.0f;
    }

    // Time (s) for GR to fall by 63 % after a burst of `burstS` seconds ends.
    float releaseTime (int ratio, float releaseMs, double burstS, double fs = 96000.0)
    {
        auto m = makeModel (fs, ratio, cClean, 20.0f, releaseMs);
        const int burst = (int) (burstS * fs), after = (int) (5.0 * fs);
        auto sig = alternating (juce::Decibels::decibelsToGain (5.0f), burst);
        sig.resize ((size_t) (burst + after), 0.0f);
        const auto gr = grTrace (m, sig);
        const float start = gr[(size_t) burst - 1];
        for (int i = burst; i < (int) gr.size(); ++i)
            if (gr[(size_t) i] <= 0.368f * start)
                return (float) ((i - burst + 1) / fs);
        return -1.0f;
    }

    // Processor render of a 1 kHz sine; returns THD of the last half second
    // and the GR telemetry of the final block.
    struct ToneResult { double thd, h2, h3; float grDb, gainDb; };
    ToneResult renderTone (int ratio, int character, float peakDb, float inputDb = 0.0f,
                           float attackUs = 800.0f, float releaseMs = 1100.0f)
    {
        const double fs = 48000.0;
        auto p = makeBite (fs, 480);
        setParam (*p, "ratio", (float) ratio);
        setParam (*p, "character", (float) character);
        setParam (*p, "input", inputDb);
        setParam (*p, "attack", attackUs);
        setParam (*p, "release", releaseMs);
        p->prepareToPlay (fs, 480);
        auto in = toBuffer (sine (fs, 1000.0, juce::Decibels::decibelsToGain (peakDb), (int) (fs * 2.0)), 2);
        auto out = render (*p, in, 480);
        const int n = 24000, start = out.getNumSamples() - n;
        p->grMeter.takeGainReduction();
        render (*p, toBuffer (sine (fs, 1000.0, juce::Decibels::decibelsToGain (peakDb), 480), 2), 480);
        const float gr = p->grMeter.takeGainReduction();
        const float* d = out.getReadPointer (0) + start;
        const double f = toneAmplitude (d, n, fs, 1000.0);
        return { thd (d, n, fs, 1000.0), toneAmplitude (d, n, fs, 2000.0) / f, toneAmplitude (d, n, fs, 3000.0) / f,
                 gr, db (rms (d, n)) - db (rms (in.getReadPointer (0) + start, n)) };
    }
}

TEST ("BITE: parameter IDs, ranges, defaults and knob direction match the registry")
{
    BiteProcessor p;
    const char* ids[] = { "input", "output", "attack", "release", "ratio", "mix", "character", "bypass" };
    for (auto* id : ids)
        CHECK_MSG (p.apvts.getParameter (id) != nullptr, id);
    CHECK (p.getParameters().size() == 8);

    auto def = [&p] (const char* id) { auto* r = p.apvts.getParameter (id); return r->convertFrom0to1 (r->getDefaultValue()); };
    CHECK_NEAR (def ("input"), 0.0f, 1e-4);
    CHECK_NEAR (def ("output"), 0.0f, 1e-4);
    CHECK_NEAR (def ("attack"), 250.0f, 0.01);
    CHECK_NEAR (def ("release"), 300.0f, 0.01);
    CHECK_NEAR (def ("ratio"), 0.0f, 1e-4);
    CHECK_NEAR (def ("mix"), 100.0f, 1e-4);
    CHECK_NEAR (def ("character"), 1.0f, 1e-4);

    // Clockwise (normalised 1) is the FASTEST time, like the hardware.
    auto* a = p.apvts.getParameter ("attack");
    auto* r = p.apvts.getParameter ("release");
    CHECK_NEAR (a->convertFrom0to1 (0.0f), 800.0f, 0.01);
    CHECK_NEAR (a->convertFrom0to1 (1.0f), 20.0f, 0.01);
    CHECK_NEAR (r->convertFrom0to1 (0.0f), 1100.0f, 0.01);
    CHECK_NEAR (r->convertFrom0to1 (1.0f), 50.0f, 0.01);
    CHECK (a->convertFrom0to1 (0.3f) > a->convertFrom0to1 (0.7f));

    auto* ratio = dynamic_cast<juce::AudioParameterChoice*> (p.apvts.getParameter ("ratio"));
    auto* chr = dynamic_cast<juce::AudioParameterChoice*> (p.apvts.getParameter ("character"));
    CHECK (ratio != nullptr && ratio->choices == juce::StringArray ({ "4", "8", "12", "20", "ALL" }));
    CHECK (chr != nullptr && chr->choices == juce::StringArray ({ "Clean", "TLEET", "Burn" }));

    CHECK_NEAR (p.apvts.getParameter ("input")->convertFrom0to1 (0.0f), -24.0f, 1e-4);
    CHECK_NEAR (p.apvts.getParameter ("input")->convertFrom0to1 (1.0f), 24.0f, 1e-4);
    CHECK_NEAR (p.apvts.getParameter ("output")->convertFrom0to1 (0.0f), -24.0f, 1e-4);
    CHECK_NEAR (p.apvts.getParameter ("output")->convertFrom0to1 (1.0f), 24.0f, 1e-4);
}

TEST ("BITE: all sample rates and block sizes, mono and stereo: finite output, no steady-state allocation")
{
    for (double fs : { 44100.0, 48000.0, 88200.0, 96000.0, 192000.0 })
        for (int block : { 1, 16, 32, 64, 128, 256, 512, 1024, 2048 })
            for (int channels : { 1, 2 })
            {
                auto p = makeBite (fs, block, channels);
                setParam (*p, "input", 18.0f);
                setParam (*p, "ratio", (float) rAll);
                setParam (*p, "character", (float) cBurn);
                setNormalised (*p, "attack", 1.0f);
                setNormalised (*p, "release", 1.0f);
                auto in = toBuffer (sine (fs, 220.0, 0.7f, std::max (block * 4, 4096)), channels);
                render (*p, in, block); // warm-up

                juce::AudioBuffer<float> out (in);
                juce::MidiBuffer midi;
                long allocs;
                {
                    ScopedAllocationCounter counter;
                    for (int pos = 0; pos < out.getNumSamples(); pos += block)
                    {
                        const int n = std::min (block, out.getNumSamples() - pos);
                        juce::AudioBuffer<float> view (out.getArrayOfWritePointers(), channels, pos, n);
                        p->processBlock (view, midi);
                    }
                    allocs = counter.count();
                }
                const auto tag = juce::String (fs) + " Hz, block " + juce::String (block) + ", " + juce::String (channels) + " ch";
                CHECK_MSG (allFinite (out), tag);
                CHECK_MSG (allocs == 0, tag + ": " + juce::String (allocs) + " allocations in processBlock");
                CHECK_MSG (rms (out.getReadPointer (0), out.getNumSamples()) > 0.01f, tag + ": no output");
            }
}

TEST ("BITE: NaN/Inf input never reaches the output or poisons the state")
{
    auto p = makeBite (48000.0, 256);
    setParam (*p, "input", 12.0f);
    setParam (*p, "ratio", (float) rAll);
    auto in = toBuffer (sine (48000.0, 440.0, 0.5f, 4800), 2);
    in.setSample (0, 100, std::numeric_limits<float>::quiet_NaN());
    in.setSample (1, 200, std::numeric_limits<float>::infinity());
    in.setSample (0, 300, -std::numeric_limits<float>::infinity());
    auto out = render (*p, in, 256);
    CHECK (allFinite (out));
    auto after = render (*p, toBuffer (sine (48000.0, 440.0, 0.5f, 4800), 2), 256);
    CHECK (allFinite (after));
    CHECK (rms (after.getReadPointer (0) + 2400, 2400) > 0.02f);
    const float gr = p->grMeter.takeGainReduction();
    CHECK (std::isfinite (gr) && gr > 1.0f);
}

TEST ("BITE: silence in gives silence out; DC is rejected; denormal-level input stays clean")
{
    auto p = makeBite (48000.0, 512);
    setParam (*p, "input", 24.0f);
    juce::AudioBuffer<float> silence (2, 48000);
    silence.clear();
    auto out = render (*p, silence, 512);
    CHECK (out.getMagnitude (0, out.getNumSamples()) < 1.0e-6f);

    juce::AudioBuffer<float> dc (2, 96000);
    for (int ch = 0; ch < 2; ++ch)
        juce::FloatVectorOperations::fill (dc.getWritePointer (ch), 0.25f, dc.getNumSamples());
    setParam (*p, "input", 0.0f);
    out = render (*p, dc, 512);
    float mean = 0;
    for (int i = 48000; i < 96000; ++i) mean += out.getSample (0, i);
    mean /= 48000.0f;
    MEASURE ("residual DC after 1 s", mean);
    CHECK (std::abs (mean) < 0.002f);

    // Denormal-level input into a fresh instance with the most gain.
    auto fresh = makeBite (48000.0, 512);
    setParam (*fresh, "input", 24.0f);
    setParam (*fresh, "output", 24.0f);
    juce::AudioBuffer<float> tiny (2, 48000);
    for (int i = 0; i < tiny.getNumSamples(); ++i)
        for (int ch = 0; ch < 2; ++ch)
            tiny.setSample (ch, i, (i & 1) ? 1.0e-38f : -1.0e-38f);
    out = render (*fresh, tiny, 512);
    CHECK (allFinite (out));
    CHECK (out.getMagnitude (0, out.getNumSamples()) < 1.0e-6f);
}

TEST ("BITE: reported latency equals measured impulse delay; Mix=0 and bypass are bit-exact delayed dry")
{
    for (double fs : { 44100.0, 96000.0 })
    {
        auto p = makeBite (fs, 128);
        const int latency = p->getLatencySamples();
        MEASURE ("reported latency @" + juce::String (fs), latency);
        CHECK (latency > 0);

        setParam (*p, "input", -24.0f); // stay below the knee: a linear path
        setParam (*p, "output", 24.0f);
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

        // Mix = 0: delayed dry, bit-exact, even while the wet path compresses hard.
        auto sig = toBuffer (sine (fs, 997.0, 0.5f, 8192), 2);
        setParam (*p, "input", 20.0f);
        setParam (*p, "ratio", (float) rAll);
        setParam (*p, "mix", 0.0f);
        p->prepareToPlay (fs, 128);
        out = render (*p, sig, 128);
        bool exact = true;
        for (int i = latency; i < sig.getNumSamples(); ++i)
            exact = exact && sameBits (out.getSample (0, i), sig.getSample (0, i - latency));
        CHECK_MSG (exact, "Mix=0 is not bit-exact dry");

        // Bypass: delayed dry, bit-exact, whatever the other settings.
        setParam (*p, "mix", 100.0f);
        setParam (*p, "bypass", 1.0f);
        p->prepareToPlay (fs, 128);
        out = render (*p, sig, 128);
        exact = true;
        for (int i = latency; i < sig.getNumSamples(); ++i)
            exact = exact && sameBits (out.getSample (1, i), sig.getSample (1, i - latency));
        CHECK_MSG (exact, "Bypass is not bit-exact dry");
    }
}

TEST ("BITE: wet path is time-aligned with dry (no comb filtering at Mix 50%)")
{
    const double fs = 48000.0;
    auto p = makeBite (fs, 256);
    setParam (*p, "input", -24.0f);
    setParam (*p, "output", 24.0f);
    setParam (*p, "character", (float) cClean);
    setParam (*p, "mix", 50.0f);
    p->prepareToPlay (fs, 256);

    for (double hz : { 1000.0, 10000.0, 18000.0 })
    {
        auto sig = toBuffer (sine (fs, hz, 0.1f, 24000), 2);
        auto out = render (*p, sig, 256);
        const float change = db (rms (out.getReadPointer (0) + 12000, 12000) / rms (sig.getReadPointer (0) + 12000, 12000));
        MEASURE ("level change at " + juce::String (hz) + " Hz (dB)", change);
        CHECK (std::abs (change) < 0.5f);
    }
}

TEST ("BITE: static curves: ratios 4 < 8 < 12 < 20, ALL over-compresses; knee calibrated to -18 dBFS RMS")
{
    const int ratios[] = { r4, r8, r12, r20 };
    const float nominal[] = { 4.0f, 8.0f, 12.0f, 20.0f };
    float prev = 1.0f;
    for (int i = 0; i < 4; ++i)
    {
        const float r = measuredRatio (ratios[i], -2.0f, 10.0f);
        MEASURE ("measured ratio " + bite::params::ratioNames[ratios[i]] + ":1 (-2..+10 dBFS peak)", r);
        CHECK (r > prev * 1.3f);
        CHECK (std::abs (r - nominal[i]) < 0.15f * nominal[i]);
        prev = r;
    }

    // ALL: output FALLS as the input rises well past the knee (negative ratio)...
    const auto allMid = staticPoint (rAll, -2.0f), allHigh = staticPoint (rAll, 10.0f);
    const auto r20High = staticPoint (r20, 10.0f);
    MEASURE ("ALL output at -2 dBFS in (dBFS peak)", allMid.outDb);
    MEASURE ("ALL output at +10 dBFS in (dBFS peak)", allHigh.outDb);
    MEASURE ("ALL slope dOut/dIn (-2..+10)", (allHigh.outDb - allMid.outDb) / 12.0f);
    CHECK (allHigh.outDb < allMid.outDb - 0.5f);
    // ...and reduces much more than 20:1 at the same input.
    MEASURE ("GR at +10 dBFS: ALL vs 20 (dB)", allHigh.grDb - r20High.grDb);
    CHECK (allHigh.grDb > r20High.grDb + 2.0f);

    // Calibration: at Input 0 dB a -18 dBFS RMS sine (-15 dBFS peak) sits in the 4:1 knee.
    const auto knee = staticPoint (r4, -15.0f);
    const auto below = staticPoint (r4, -30.0f);
    MEASURE ("4:1 GR at -18 dBFS RMS sine (dB)", knee.grDb);
    MEASURE ("4:1 GR at -33 dBFS RMS sine (dB)", below.grDb);
    CHECK (knee.grDb > 0.3f && knee.grDb < 3.0f);
    CHECK (below.grDb < 0.01f);

    auto csv = outputDirectory().getChildFile ("BITE_static_curves.csv");
    juce::String text ("input_dbfs_peak");
    for (auto& n : bite::params::ratioNames)
        text << ",out_" << n << ",gr_" << n;
    text << "\n";
    for (float l = -40.0f; l <= 12.01f; l += 1.0f)
    {
        text << l;
        for (int r = 0; r < BiteModel::numRatios; ++r)
        {
            const auto pt = staticPoint (r, l);
            text << "," << pt.outDb << "," << pt.grDb;
        }
        text << "\n";
    }
    csv.replaceWithText (text);
}

TEST ("BITE: attack sweep 20-800 us, clockwise faster; the Attack knob reaches the DSP")
{
    // Bare model timing at 192 kHz (5 us resolution).
    juce::String csv ("attack_knob,attack_param_us,measured_us\n");
    float prev = 1.0e9f, fastest = 0, slowest = 0;
    for (int step = 0; step <= 4; ++step)
    {
        const float knob = (float) step * 0.25f;
        const float us = knobToPlain ("attack", knob);
        const float t = attackTime (r4, us) * 1.0e6f;
        MEASURE ("attack knob " + juce::String (knob, 2) + " (" + juce::String (us, 1) + " us) measured 63% (us)", t);
        csv << knob << "," << us << "," << t << "\n";
        CHECK (t > 0.0f && t < prev);
        CHECK (std::abs (t - us) < 0.3f * us + 6.0f);
        prev = t;
        if (step == 0) slowest = t;
        if (step == 4) fastest = t;
    }
    outputDirectory().getChildFile ("BITE_attack_sweep.csv").replaceWithText (csv);
    CHECK (slowest > 20.0f * fastest);

    // The same holds at every ratio (feedback loop compensated).
    for (int r : { r8, r12, r20 })
    {
        const float t = attackTime (r, 250.0f) * 1.0e6f;
        MEASURE ("attack 250 us at ratio " + bite::params::ratioNames[r] + " (us)", t);
        CHECK (std::abs (t - 250.0f) < 75.0f);
    }

    // Through the processor and the GR telemetry: DC step at 192 kHz, block size 1.
    auto riseTime = [] (float knob)
    {
        const double fs = 192000.0;
        auto p = makeBite (fs, 1);
        setNormalised (*p, "attack", knob);
        setParam (*p, "release", 1100.0f);
        p->prepareToPlay (fs, 1);
        const int pre = 2000, n = (int) (0.006 * fs);
        juce::AudioBuffer<float> in (2, pre + n);
        in.clear();
        for (int ch = 0; ch < 2; ++ch)
            juce::FloatVectorOperations::fill (in.getWritePointer (ch, pre), 1.0f, n);
        std::vector<float> gr;
        juce::MidiBuffer midi;
        for (int i = 0; i < in.getNumSamples(); ++i)
        {
            juce::AudioBuffer<float> view (in.getArrayOfWritePointers(), 2, i, 1);
            p->processBlock (view, midi);
            gr.push_back (std::max (0.0f, p->grMeter.takeGainReduction()));
        }
        const float final = *std::max_element (gr.begin(), gr.end());
        int start = pre;
        while (start < (int) gr.size() && gr[(size_t) start] < 0.05f) ++start;
        for (int i = start; i < (int) gr.size(); ++i)
            if (gr[(size_t) i] >= 0.632f * final)
                return (float) ((i - start + 1) * 1.0e6 / fs);
        return -1.0f;
    };
    const float slow = riseTime (0.0f), fast = riseTime (1.0f);
    MEASURE ("processor GR rise to 63%, Attack fully anticlockwise (us)", slow);
    MEASURE ("processor GR rise to 63%, Attack fully clockwise (us)", fast);
    CHECK (fast > 0.0f && slow > 8.0f * fast);
    CHECK (slow > 500.0f && slow < 1200.0f);
}

TEST ("BITE: release sweep 50-1100 ms, clockwise faster, program dependent; the Release knob reaches the DSP")
{
    juce::String csv ("release_knob,release_param_ms,measured_ms\n");
    float prev = 1.0e9f, fastest = 0, slowest = 0;
    for (int step = 0; step <= 4; ++step)
    {
        const float knob = (float) step * 0.25f;
        const float ms = knobToPlain ("release", knob);
        const float t = releaseTime (r4, ms, 0.05) * 1000.0f;
        MEASURE ("release knob " + juce::String (knob, 2) + " (" + juce::String (ms, 1) + " ms) measured 63% (ms)", t);
        csv << knob << "," << ms << "," << t << "\n";
        CHECK (t > 0.0f && t < prev);
        CHECK (std::abs (t - ms) < 0.15f * ms);
        prev = t;
        if (step == 0) slowest = t;
        if (step == 4) fastest = t;
    }
    outputDirectory().getChildFile ("BITE_release_sweep.csv").replaceWithText (csv);
    CHECK (slowest > 15.0f * fastest);

    // Program dependence: sustained reduction stretches the release.
    const float afterShort = releaseTime (r4, 300.0f, 0.05) * 1000.0f;
    const float afterLong = releaseTime (r4, 300.0f, 3.0) * 1000.0f;
    MEASURE ("release 300 ms after a 50 ms burst (ms)", afterShort);
    MEASURE ("release 300 ms after a 3 s burst (ms)", afterLong);
    CHECK (afterLong > afterShort * 1.15f);

    // Processor: GR telemetry after a burst decays faster with Release clockwise.
    auto recovery = [] (float knob)
    {
        const double fs = 48000.0;
        auto p = makeBite (fs, 64);
        setNormalised (*p, "release", knob);
        p->prepareToPlay (fs, 64);
        auto sig = sine (fs, 1000.0, 1.0f, (int) (0.3 * fs));
        sig.resize ((size_t) (3.0 * fs), 0.0f);
        auto in = toBuffer (sig, 2);
        juce::MidiBuffer midi;
        std::vector<float> gr;
        for (int pos = 0; pos < in.getNumSamples(); pos += 64)
        {
            juce::AudioBuffer<float> view (in.getArrayOfWritePointers(), 2, pos, 64);
            p->processBlock (view, midi);
            gr.push_back (p->grMeter.takeGainReduction());
        }
        const int endBlock = (int) (0.3 * fs) / 64 + 1;
        const float start = gr[(size_t) endBlock];
        for (int b = endBlock; b < (int) gr.size(); ++b)
            if (gr[(size_t) b] <= 0.368f * start)
                return (float) ((b - endBlock) * 64 * 1000.0 / fs);
        return -1.0f;
    };
    const float slow = recovery (0.0f), fast = recovery (1.0f);
    MEASURE ("processor GR recovery to 37%, Release fully anticlockwise (ms)", slow);
    MEASURE ("processor GR recovery to 37%, Release fully clockwise (ms)", fast);
    CHECK (fast > 0.0f && slow > 10.0f * fast);
}

TEST ("BITE: ALL differs from 20:1 in harmonics and envelope, not only in ratio")
{
    // Harmonics: same input, same timing, TLEET voicing.
    const auto t20 = renderTone (r20, cTleet, -6.0f, 6.0f);
    const auto all = renderTone (rAll, cTleet, -6.0f, 6.0f);
    MEASURE ("20:1 THD %", t20.thd * 100);
    MEASURE ("ALL THD %", all.thd * 100);
    MEASURE ("20:1 H2 %", t20.h2 * 100);
    MEASURE ("ALL H2 %", all.h2 * 100);
    MEASURE ("20:1 H3 %", t20.h3 * 100);
    MEASURE ("ALL H3 %", all.h3 * 100);
    MEASURE ("20:1 GR (dB)", t20.grDb);
    MEASURE ("ALL GR (dB)", all.grDb);
    CHECK (all.thd > 2.0 * t20.thd);
    CHECK (all.h2 > 2.0 * t20.h2);
    CHECK (all.h3 > 1.5 * t20.h3);

    // Envelope: faster attack, different release at the same knob settings.
    const float atk20 = attackTime (r20, 400.0f) * 1.0e6f, atkAll = attackTime (rAll, 400.0f) * 1.0e6f;
    MEASURE ("attack 400 us knob: 20:1 measured (us)", atk20);
    MEASURE ("attack 400 us knob: ALL measured (us)", atkAll);
    CHECK (atkAll < 0.7f * atk20);

    const float relShort20 = releaseTime (r20, 300.0f, 0.05) * 1000.0f, relShortAll = releaseTime (rAll, 300.0f, 0.05) * 1000.0f;
    const float relLong20 = releaseTime (r20, 300.0f, 3.0) * 1000.0f, relLongAll = releaseTime (rAll, 300.0f, 3.0) * 1000.0f;
    MEASURE ("release 300 ms knob after 50 ms burst: 20:1 (ms)", relShort20);
    MEASURE ("release 300 ms knob after 50 ms burst: ALL (ms)", relShortAll);
    MEASURE ("release 300 ms knob after 3 s burst: 20:1 (ms)", relLong20);
    MEASURE ("release 300 ms knob after 3 s burst: ALL (ms)", relLongAll);
    CHECK (relShortAll < 0.85f * relShort20);
    CHECK (relLongAll / relShortAll > 1.3f * (relLong20 / relShort20));
}

TEST ("BITE: THD grows with operating level and with gain reduction (FET nonlinearity)")
{
    double prevThd = 0.0;
    float prevGr = -1.0f;
    for (float level : { -24.0f, -12.0f, -6.0f, 0.0f })
    {
        const auto t = renderTone (r4, cTleet, level, 6.0f);
        MEASURE ("TLEET 4:1 THD % at " + juce::String (level) + " dBFS peak in (+6 dB Input)", t.thd * 100);
        MEASURE ("TLEET 4:1 GR at " + juce::String (level) + " dBFS peak in (dB)", t.grDb);
        CHECK (t.thd > prevThd * 1.2);
        CHECK (t.grDb > prevGr);
        prevThd = t.thd;
        prevGr = t.grDb;
    }

    // Clean has no amplifier colour: what remains is the FET, and it only
    // distorts when it is turned on (gain reduction).
    const auto noGr = renderTone (r4, cClean, -6.0f, -18.0f);
    const auto withGr = renderTone (r4, cClean, -6.0f, 12.0f);
    MEASURE ("Clean THD % without GR", noGr.thd * 100);
    MEASURE ("Clean GR (dB) with Input +12", withGr.grDb);
    MEASURE ("Clean THD % with Input +12", withGr.thd * 100);
    CHECK (noGr.grDb < 0.05f);
    CHECK (withGr.thd > 5.0 * noGr.thd);
}

TEST ("BITE: Character Clean < TLEET < Burn harmonics at equal gain reduction")
{
    const auto clean = renderTone (r4, cClean, -6.0f, 6.0f);
    const auto tleetV = renderTone (r4, cTleet, -6.0f, 6.0f);
    const auto burn = renderTone (r4, cBurn, -6.0f, 6.0f);
    MEASURE ("Clean THD %", clean.thd * 100);
    MEASURE ("TLEET THD %", tleetV.thd * 100);
    MEASURE ("Burn THD %", burn.thd * 100);
    MEASURE ("Clean GR (dB)", clean.grDb);
    MEASURE ("TLEET GR (dB)", tleetV.grDb);
    MEASURE ("Burn GR (dB)", burn.grDb);
    CHECK (clean.grDb > 3.0f);
    CHECK (std::abs (tleetV.grDb - clean.grDb) < 0.1f);
    CHECK (std::abs (burn.grDb - clean.grDb) < 0.1f);
    CHECK (tleetV.thd > 2.0 * clean.thd);
    CHECK (burn.thd > 2.0 * tleetV.thd);

    // Character never moves the level much (it is colour, not compression).
    CHECK (std::abs (burn.gainDb - clean.gainDb) < 1.0f);
}

TEST ("BITE: GR telemetry equals the attenuation measured on the audio")
{
    const double fs = 48000.0;
    struct Case { int ratio, character; double tolerance; };
    // The telemetry is the FET's small-signal gain, which is the attenuation
    // (<= 0.05 dB). Burn drives the line amplifier into saturation, which
    // lowers the fundamental a little more on top of the applied gain;
    // bounded here and documented in MODEL_SPEC.md.
    const Case cases[] = { { r4, cClean, 0.05 }, { r4, cTleet, 0.05 }, { r20, cClean, 0.05 }, { r20, cTleet, 0.05 },
                           { rAll, cTleet, 0.05 }, { r4, cBurn, 0.25 } };
    for (const auto& cs : cases)
    {
        const int ratio = cs.ratio, character = cs.character;
        auto p = makeBite (fs, 512);
        setParam (*p, "ratio", (float) ratio);
        setParam (*p, "character", (float) character);
        setParam (*p, "attack", 800.0f);
        setParam (*p, "release", 1100.0f);
        p->prepareToPlay (fs, 512);

        const float inAmp = juce::Decibels::decibelsToGain (-6.0f);
        render (*p, toBuffer (sine (fs, 1000.0, inAmp, (int) (fs * 3)), 2), 512);
        p->grMeter.takeGainReduction();

        // one more steady stretch, telemetry read for exactly that stretch
        auto last = toBuffer (sine (fs, 1000.0, inAmp, 4800), 2);
        auto lastOut = render (*p, last, 4800);
        const float telemetry = p->grMeter.takeGainReduction();
        const float measured = db (rms (last.getReadPointer (0), 4800)) - db (rms (lastOut.getReadPointer (0), 4800));
        const auto tag = bite::params::ratioNames[ratio] + " / " + bite::params::characterNames[character];
        MEASURE ("GR telemetry (dB) " + tag, telemetry);
        MEASURE ("GR measured on audio (dB) " + tag, measured);
        CHECK (telemetry > 3.0f);
        CHECK_NEAR (telemetry, measured, cs.tolerance);
    }
}

TEST ("BITE: stereo is linked: one detector, the quiet channel follows the loud one")
{
    const double fs = 48000.0;
    auto p = makeBite (fs, 256);
    setParam (*p, "character", (float) cClean);
    setParam (*p, "attack", 800.0f);
    setParam (*p, "release", 1100.0f);
    p->prepareToPlay (fs, 256);

    juce::AudioBuffer<float> in (2, (int) fs * 2);
    auto loud = sine (fs, 1000.0, 0.7f, in.getNumSamples());
    auto quiet = sine (fs, 700.0, 0.01f, in.getNumSamples());
    in.copyFrom (0, 0, loud.data(), in.getNumSamples());
    in.copyFrom (1, 0, quiet.data(), in.getNumSamples());
    auto out = render (*p, in, 256);
    const int n = 24000, s = in.getNumSamples() - n;
    const float grL = db (rms (in.getReadPointer (0) + s, n)) - db (rms (out.getReadPointer (0) + s, n));
    const float grR = db (rms (in.getReadPointer (1) + s, n)) - db (rms (out.getReadPointer (1) + s, n));
    MEASURE ("left (loud) attenuation (dB)", grL);
    MEASURE ("right (quiet) attenuation (dB)", grR);
    CHECK (grL > 6.0f);
    CHECK_NEAR (grR, grL, 0.2);
}

TEST ("BITE: ratio and character switching under compression is click-free")
{
    const double fs = 48000.0;
    const int block = 64, blocks = 1500;

    // Largest second difference of the output: a gain step shows up as a spike.
    auto run = [&] (bool switching, int fixedRatio, int fixedChar)
    {
        auto p = makeBite (fs, block);
        setParam (*p, "input", 12.0f);
        setNormalised (*p, "attack", 1.0f);  // fastest: worst case for clicks
        setNormalised (*p, "release", 1.0f);
        setParam (*p, "ratio", (float) fixedRatio);
        setParam (*p, "character", (float) fixedChar);
        p->prepareToPlay (fs, block);
        juce::MidiBuffer midi;
        float y1 = 0, y2 = 0, maxCurv = 0;
        const int ratioSeq[] = { r4, rAll, r8, r20, r12, rAll, r4 };
        for (int b = 0; b < blocks; ++b)
        {
            if (switching && b % 50 == 0)
            {
                setParam (*p, "ratio", (float) ratioSeq[(b / 50) % 7]);
                setParam (*p, "character", (float) ((b / 50) % 3));
            }
            auto buf = toBuffer (sine (fs, 100.0, 0.3f, block, 2.0 * juce::MathConstants<double>::pi * 100.0 * b * block / fs), 2);
            p->processBlock (buf, midi);
            for (int i = 0; i < block; ++i)
            {
                const float y = buf.getSample (0, i);
                if (b > 20) // after the initial attack
                    maxCurv = std::max (maxCurv, std::abs (y - 2.0f * y1 + y2));
                y2 = y1;
                y1 = y;
            }
        }
        return maxCurv;
    };

    float baseline = 0;
    for (int r = 0; r < BiteModel::numRatios; ++r)
        for (int c = 0; c < BiteModel::numCharacters; ++c)
            baseline = std::max (baseline, run (false, r, c));
    const float switched = run (true, r4, cClean);
    MEASURE ("max |2nd difference| over all fixed settings", baseline);
    MEASURE ("max |2nd difference| while switching ratio/character", switched);
    // An unsmoothed switch measures ~0.13 here (over 100x the baseline); the
    // glide through intermediate laws stays within 2x of the fixed settings.
    CHECK (switched < 2.0f * baseline);
}

TEST ("BITE: rapid automation of every parameter stays finite and click-free")
{
    const double fs = 48000.0;
    auto p = makeBite (fs, 64);
    juce::Random rng (42);
    float prevSample = 0.0f, maxJump = 0.0f;
    for (int b = 0; b < 3000; ++b)
    {
        setParam (*p, "input", -6.0f + rng.nextFloat() * 12.0f);
        setParam (*p, "output", -6.0f + rng.nextFloat() * 12.0f);
        setNormalised (*p, "attack", rng.nextFloat());
        setNormalised (*p, "release", rng.nextFloat());
        setParam (*p, "ratio", (float) rng.nextInt (5));
        setParam (*p, "mix", rng.nextFloat() * 100.0f);
        setParam (*p, "character", (float) rng.nextInt (3));
        auto out = toBuffer (sine (fs, 200.0, 0.3f, 64, 2.0 * juce::MathConstants<double>::pi * 200.0 * b * 64 / fs), 2);
        juce::MidiBuffer midi;
        p->processBlock (out, midi);
        CHECK (allFinite (out));
        for (int i = 0; i < 64; ++i)
        {
            maxJump = std::max (maxJump, std::abs (out.getSample (0, i) - prevSample));
            prevSample = out.getSample (0, i);
        }
    }
    // a 200 Hz sine at <= ~0.6 peak moves at most ~0.016 per sample
    MEASURE ("max sample-to-sample jump", maxJump);
    CHECK (maxJump < 0.06f);
}

TEST ("BITE: state round trip restores parameters, preset name, A/B and UI settings and renders identically")
{
    const double fs = 48000.0;
    auto a = makeBite (fs, 256);
    setParam (*a, "input", 9.5f);
    setParam (*a, "output", -4.5f);
    setParam (*a, "attack", 123.0f);
    setParam (*a, "release", 456.0f);
    setParam (*a, "ratio", (float) rAll);
    setParam (*a, "mix", 72.0f);
    setParam (*a, "character", (float) cBurn);
    a->setUiProperty ("scale", 1.5);
    a->setUiProperty ("meterMode", 2);

    juce::MemoryBlock state;
    a->getStateInformation (state);

    auto b = makeBite (fs, 256);
    b->setStateInformation (state.getData(), (int) state.getSize());

    for (auto* param : a->getParameters())
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (param))
            CHECK_NEAR (r->getValue(), b->apvts.getParameter (r->paramID)->getValue(), 1e-6);
    CHECK_NEAR ((double) b->getUiProperty ("scale", 1.0), 1.5, 1e-9);
    CHECK ((int) b->getUiProperty ("meterMode", 0) == 2);

    a->prepareToPlay (fs, 256);
    b->prepareToPlay (fs, 256);
    auto sig = toBuffer (sine (fs, 300.0, 0.4f, 48000), 2);
    auto oa = render (*a, sig, 256), ob = render (*b, sig, 256);
    bool identical = true;
    for (int i = 0; i < oa.getNumSamples(); ++i)
        identical = identical && sameBits (oa.getSample (0, i), ob.getSample (0, i));
    CHECK_MSG (identical, "restored instance renders differently");

    // and the restored settings are not the defaults: the render differs from a fresh instance
    auto fresh = makeBite (fs, 256);
    auto of = render (*fresh, sig, 256);
    CHECK (std::abs (rms (of.getReadPointer (0), 48000) - rms (ob.getReadPointer (0), 48000)) > 0.01f);

    // garbage and foreign state are ignored
    b->setStateInformation ("nonsense", 8);
    CHECK_NEAR (plainValue (*b, "mix"), 72.0f, 0.01);
}

TEST ("BITE: factory presets load, change parameters and reject other plugins' presets")
{
    BiteProcessor p;
    const auto& list = p.presets.getPresets();
    int factory = 0;
    for (auto& pr : list) factory += pr.isFactory ? 1 : 0;
    MEASURE ("factory presets", factory);
    CHECK (factory >= 10);

    bool sawAll = false;
    for (int i = 0; i < (int) list.size(); ++i)
        if (list[(size_t) i].isFactory)
        {
            p.loadPreset (i);
            CHECK (list[(size_t) i].params.values.size() == 7);
            for (auto& [id, v] : list[(size_t) i].params.values)
            {
                auto* r = p.apvts.getParameter (id);
                CHECK_MSG (r != nullptr, id);
                if (r) CHECK_NEAR (r->convertFrom0to1 (r->getValue()), v, 0.06);
            }
            CHECK (! p.isPresetModified());
            sawAll = sawAll || plainValue (p, "ratio") > 3.5f;
        }
    CHECK (sawAll);

    setParam (p, "mix", 13.0f);
    CHECK (p.isPresetModified());

    PresetManager::Preset out;
    const auto foreign = juce::String (R"({"format":"tleet-preset","version":1,"plugin":"ASH","name":"x","params":{}})");
    CHECK (p.presets.parse (foreign, out).failed());
    const auto future = juce::String (R"({"format":"tleet-preset","version":99,"plugin":"BITE","name":"x","params":{}})");
    CHECK (p.presets.parse (future, out).failed());

    const auto json = p.stateToClipboardJson();
    setParam (p, "mix", 99.0f);
    CHECK (p.pasteStateFromJson (json).wasOk());
    CHECK_NEAR (plainValue (p, "mix"), 13.0f, 0.06);
}
