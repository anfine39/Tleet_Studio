#include "TestFramework.h"
#include "TestSignals.h"
#include <cstring>
#include <set>
#include <tuple>
#include <limits>
#include "../plugins/PUNCH/PluginProcessor.h"
#include "../plugins/PUNCH/Parameters.h"
#include "../plugins/PUNCH/dsp/PunchModel.h"

using namespace tleet;
using namespace tleet::test;
using tleet::punch::PunchProcessor;
using tleet::punch::PunchModel;
namespace pp = tleet::punch::params;

namespace
{
    // Independent constants (MODEL_SPEC.md), written out here rather than
    // taken from the model, so a changed model constant fails the tests.
    constexpr double specReleaseRate = 125.0;                                    // dB/s, dbx 160A data sheet
    const double specTau = 10.0 / std::log (10.0) / specReleaseRate;              // 34.74 ms
    constexpr double specOverEasyScale = 2.5;                                    // dB (softplus scale)
    constexpr double specKnobReference = -15.0;                                  // knob 0 dB = -15 dB RMS

    const float infinity = std::numeric_limits<float>::infinity();

    double slopeOf (double ratio) { return std::isinf (ratio) ? 1.0 : 1.0 - 1.0 / ratio; }
    juce::String ratioName (double ratio) { return std::isinf (ratio) ? juce::String ("inf") : juce::String (ratio); }

    // MODEL_SPEC 2.3, written independently of PunchModel::gainComputerDb.
    double specGr (double levelDb, double thresholdDb, double ratio, bool overEasy)
    {
        const double x = levelDb - thresholdDb, s = slopeOf (ratio);
        const double k = overEasy ? specOverEasyScale * std::log (1.0 + std::exp (x / specOverEasyScale)) : std::max (0.0, x);
        return std::min (60.0, s * k);
    }

    void setParam (PunchProcessor& p, const char* id, float plain)
    {
        auto* r = p.apvts.getParameter (id);
        r->setValueNotifyingHost (r->convertTo0to1 (plain));
    }

    float plainValue (PunchProcessor& p, const char* id)
    {
        auto* r = p.apvts.getParameter (id);
        return r->convertFrom0to1 (r->getValue());
    }

    // Threshold knob value for a detector threshold in dB RMS.
    float knobFor (float thresholdDb) { return thresholdDb - (float) specKnobReference; }

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

    juce::AudioBuffer<float> render (PunchProcessor& p, const juce::AudioBuffer<float>& input, int block,
                                     std::vector<float>* grPerBlock = nullptr)
    {
        const int nch = input.getNumChannels(), n = input.getNumSamples();
        juce::AudioBuffer<float> io (input);
        juce::MidiBuffer midi;
        for (int pos = 0; pos < n; pos += block)
        {
            const int len = std::min (block, n - pos);
            juce::AudioBuffer<float> view (io.getArrayOfWritePointers(), nch, pos, len);
            p.processBlock (view, midi);
            if (grPerBlock != nullptr)
                grPerBlock->push_back (std::max (0.0f, p.grMeter.takeGainReduction()));
        }
        return io;
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

    std::unique_ptr<PunchProcessor> makePunch (double fs, int block, int channels = 2)
    {
        auto p = std::make_unique<PunchProcessor>();
        const auto set = channels == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo();
        PunchProcessor::BusesLayout layout;
        layout.inputBuses.add (set);
        layout.outputBuses.add (set);
        CHECK_MSG (p->setBusesLayout (layout), "layout rejected");
        p->setRateAndBufferSizeDetails (fs, block);
        p->prepareToPlay (fs, block);
        return p;
    }

    // Configures and re-prepares (so the smoothers start on the settings).
    void configure (PunchProcessor& p, double fs, int block, float thresholdDb, double ratio, bool overEasy, float grit)
    {
        setParam (p, "threshold", knobFor (thresholdDb));
        setParam (p, "ratio", pp::plainFromRatio ((float) ratio));
        setParam (p, "overEasy", overEasy ? 1.0f : 0.0f);
        setParam (p, "grit", grit);
        p.prepareToPlay (fs, block);
    }

    // Constant-envelope test signal (+A, -A, ...): its mean square is exactly A^2.
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

    float ampForRmsDb (double rmsDb) { return (float) std::pow (10.0, rmsDb / 20.0); } // constant envelope

    PunchModel makeModel (double fs, double ratio, double thresholdDb, bool overEasy = false, float grit = 0.0f)
    {
        PunchModel m;
        m.setSlope ((float) slopeOf (ratio));
        m.setThresholdDb ((float) thresholdDb);
        m.setOverEasy (overEasy);
        m.setGrit (grit);
        m.prepare (fs);
        return m;
    }

    // Runs the bare model on `l` (and `r` if given) and returns GR (dB) per sample.
    std::vector<double> grTrace (PunchModel& m, std::vector<float> l, std::vector<float> r = {})
    {
        std::vector<double> gr;
        gr.reserve (l.size());
        const int nch = r.empty() ? 1 : 2;
        for (size_t i = 0; i < l.size(); ++i)
        {
            float* ch[] = { &l[i], r.empty() ? nullptr : &r[i] };
            m.process (ch, nch, 1);
            gr.push_back (m.getGainReductionDb());
        }
        return gr;
    }

    // Time (s) after sample `from` at which `trace` first crosses `level`, linearly interpolated.
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

    double steadyGrOfModel (double ratio, double levelDb, double thresholdDb, bool overEasy, double* meanSquare = nullptr)
    {
        const double fs = 96000.0;
        auto m = makeModel (fs, ratio, thresholdDb, overEasy);
        std::vector<float> sig = alternating (ampForRmsDb (levelDb), (int) (fs * 1.0));
        float* ch[] = { sig.data() };
        m.process (ch, 1, (int) sig.size());
        if (meanSquare != nullptr)
            *meanSquare = m.getMeanSquare();
        return m.getGainReductionDb();
    }

    // Processor render of a 1 kHz sine; THD and GR telemetry of the last second.
    struct ToneResult { double thd, h2, h3; float grDb, outDb; };
    ToneResult renderTone (float grit, float peakDbfs, float thresholdDb, double ratio, bool overEasy = false, double hz = 1000.0)
    {
        const double fs = 48000.0;
        auto p = makePunch (fs, 480);
        configure (*p, fs, 480, thresholdDb, ratio, overEasy, grit);
        auto in = toBuffer (sine (fs, hz, juce::Decibels::decibelsToGain (peakDbfs), (int) (fs * 3.0)), 2);
        std::vector<float> gr;
        auto out = render (*p, in, 480, &gr);
        const int n = 48000, start = out.getNumSamples() - n;
        const float* d = out.getReadPointer (0) + start;
        const double f = toneAmplitude (d, n, fs, hz);
        return { thd (d, n, fs, hz), toneAmplitude (d, n, fs, hz * 2) / f, toneAmplitude (d, n, fs, hz * 3) / f,
                 gr.back(), db (rms (d, n)) };
    }
}

// =====================================================================================
TEST ("PUNCH: parameter IDs, ranges, steps, defaults and the ratio helpers match the registry")
{
    PunchProcessor p;
    const char* ids[] = { "threshold", "ratio", "overEasy", "grit", "mix", "output", "bypass" };
    for (auto* id : ids)
        CHECK_MSG (p.apvts.getParameter (id) != nullptr, id);
    CHECK (p.getParameters().size() == 7);

    auto def = [&p] (const char* id) { auto* r = p.apvts.getParameter (id); return r->convertFrom0to1 (r->getDefaultValue()); };
    CHECK_NEAR (def ("threshold"), 0.0f, 1e-4);
    CHECK_NEAR (def ("ratio"), 0.75f, 1e-6);                         // 4:1
    CHECK_NEAR (pp::ratioFromPlain (def ("ratio")), 4.0f, 1e-4);
    CHECK_NEAR (def ("overEasy"), 0.0f, 1e-6);                       // hard knee, as on the hardware
    CHECK_NEAR (def ("grit"), 20.0f, 1e-4);
    CHECK_NEAR (def ("mix"), 100.0f, 1e-4);
    CHECK_NEAR (def ("output"), 0.0f, 1e-4);
    CHECK_NEAR (def ("bypass"), 0.0f, 1e-6);
    CHECK (dynamic_cast<juce::AudioParameterBool*> (p.apvts.getParameter ("overEasy")) != nullptr);
    CHECK (dynamic_cast<juce::AudioParameterBool*> (p.apvts.getParameter ("bypass")) != nullptr);

    auto range = [&p] (const char* id, float lo, float hi)
    {
        auto* r = p.apvts.getParameter (id);
        CHECK_NEAR (r->convertFrom0to1 (0.0f), lo, 1e-4);
        CHECK_NEAR (r->convertFrom0to1 (1.0f), hi, 1e-4);
    };
    range ("threshold", -20.0f, 10.0f);
    range ("ratio", 0.0f, 1.0f);
    range ("grit", 0.0f, 100.0f);
    range ("mix", 0.0f, 100.0f);
    range ("output", -20.0f, 20.0f);

    // THRESHOLD: piecewise linear, 0 dB at the knob centre, 0.1 dB grid, exact round trip.
    {
        auto* f = dynamic_cast<juce::AudioParameterFloat*> (p.apvts.getParameter ("threshold"));
        juce::AudioProcessorParameter* r = f;
        const auto& nr = f->getNormalisableRange();
        CHECK (nr.convertFrom0to1 (0.5f) == 0.0f && nr.convertTo0to1 (0.0f) == 0.5f);
        CHECK_NEAR (nr.convertFrom0to1 (0.25f), -10.0f, 1e-5);
        CHECK_NEAR (nr.convertFrom0to1 (0.75f), 5.0f, 1e-5);
        CHECK_NEAR (r->getDefaultValue(), 0.5f, 1e-7);
        CHECK (nr.interval == 0.1f);
        int bad = 0, badSnap = 0, badMono = 0, badText = 0;
        float prevN = -1.0f;
        for (int t = -200; t <= 100; ++t)
        {
            const float v = nr.snapToLegalValue ((float) t * 0.1f);
            const float n = nr.convertTo0to1 (v);
            bad += nr.snapToLegalValue (nr.convertFrom0to1 (n)) != v ? 1 : 0;
            badSnap += std::abs (v - (float) t * 0.1f) > 1e-5f ? 1 : 0;
            badMono += n <= prevN ? 1 : 0;
            prevN = n;
            r->setValueNotifyingHost (n);
            bad += f->get() != v ? 1 : 0;
            badText += nr.snapToLegalValue (nr.convertFrom0to1 (r->getValueForText (r->getText (n, 64)))) != v ? 1 : 0;
        }
        CHECK (bad == 0 && badSnap == 0 && badMono == 0 && badText == 0);
        CHECK_NEAR (nr.snapToLegalValue (-3.04f), -3.0f, 1e-6);
        CHECK (nr.snapToLegalValue (15.0f) == 10.0f && nr.snapToLegalValue (-25.0f) == -20.0f);
        r->setValueNotifyingHost (r->getDefaultValue());
    }
    // Threshold calibration: knob 0 dB = -15 dB RMS (a sine peaking at -12 dBFS).
    CHECK_NEAR (pp::thresholdToDetectorDb (0.0f), specKnobReference, 1e-6);
    CHECK_NEAR (pp::thresholdToDetectorDb (-20.0f), -35.0f, 1e-6);
    CHECK_NEAR (pp::thresholdToDetectorDb (10.0f), -5.0f, 1e-6);

    // COMPRESSION: plain value = slope 1 - 1/ratio, linear on the knob, infinity reachable.
    auto* ratio = p.apvts.getParameter ("ratio");
    CHECK (ratio->getNumSteps() > 1000); // continuous for hosts
    for (float r : { 1.0f, 1.5f, 2.0f, 3.0f, 4.0f, 8.0f, 20.0f, 100.0f, 1000.0f })
    {
        CHECK_NEAR (pp::ratioFromPlain (pp::plainFromRatio (r)), r, 1e-4 * r * r);
        CHECK_NEAR (pp::plainFromRatio (r), 1.0 - 1.0 / r, 1e-7);
    }
    CHECK (std::isinf (pp::ratioFromPlain (1.0f)) && pp::plainFromRatio (infinity) == 1.0f);
    CHECK (pp::ratioFromPlain (0.0f) == 1.0f && pp::plainFromRatio (0.5f) == 0.0f);
    CHECK (std::isinf (pp::ratioFromPlain (ratio->convertFrom0to1 (1.0f))));
    CHECK (ratio->convertFrom0to1 (1.0f) == 1.0f);
    // the editor's label positions (normalised knob travel)
    const std::pair<float, float> labels[] = { { 1.0f, 0.0f }, { 2.0f, 0.5f }, { 4.0f, 0.75f }, { 8.0f, 0.875f }, { 20.0f, 0.95f }, { infinity, 1.0f } };
    for (auto [r, n] : labels)
        CHECK_NEAR (ratio->convertTo0to1 (pp::plainFromRatio (r)), n, 1e-6);
    CHECK (ratio->getText (0.75f, 32) == "4.0:1");
    CHECK (ratio->getText (0.0f, 32) == "1.0:1");
    CHECK (ratio->getText (0.95f, 32) == "20:1");
    CHECK (ratio->getText (1.0f, 32) == juce::String::fromUTF8 ("\xe2\x88\x9e:1"));
    CHECK_NEAR (ratio->getValueForText ("4:1"), 0.75f, 1e-6);
    CHECK_NEAR (ratio->getValueForText ("8"), 0.875f, 1e-6);
    CHECK_NEAR (ratio->getValueForText ("inf"), 1.0f, 1e-6);
    CHECK_NEAR (ratio->getValueForText (juce::String::fromUTF8 ("\xe2\x88\x9e:1")), 1.0f, 1e-6);
    CHECK_NEAR (ratio->getValueForText (ratio->getText (0.6f, 32)), 0.6f, 1e-3);

    // Model constants agree with the data sheet.
    CHECK_NEAR (PunchModel::detectorTauSeconds, specTau, 1e-12);
    MEASURE ("RMS detector time constant (ms)", PunchModel::detectorTauSeconds * 1000.0);

    // Buses: stereo/mono in == out only, no sidechain bus.
    CHECK (p.getBusCount (true) == 1 && p.getBusCount (false) == 1);
    PunchProcessor::BusesLayout l;
    l.inputBuses.add (juce::AudioChannelSet::mono());
    l.outputBuses.add (juce::AudioChannelSet::mono());
    CHECK (p.checkBusesLayoutSupported (l));
    l.outputBuses.getReference (0) = juce::AudioChannelSet::stereo();
    CHECK (! p.checkBusesLayoutSupported (l));
    l.inputBuses.getReference (0) = juce::AudioChannelSet::create5point1();
    l.outputBuses.getReference (0) = juce::AudioChannelSet::create5point1();
    CHECK (! p.checkBusesLayoutSupported (l));
}

TEST ("PUNCH: all sample rates and block sizes, mono and stereo: finite output, no steady-state allocation")
{
    for (double fs : { 44100.0, 48000.0, 88200.0, 96000.0, 192000.0 })
        for (int block : { 1, 16, 32, 64, 128, 256, 512, 1024, 2048 })
            for (int channels : { 1, 2 })
            {
                auto p = makePunch (fs, block, channels);
                setParam (*p, "threshold", -20.0f);
                setParam (*p, "ratio", block % 3 == 0 ? 1.0f : 0.8f);
                setParam (*p, "overEasy", block % 2 == 0 ? 1.0f : 0.0f);
                setParam (*p, "grit", 100.0f);
                setParam (*p, "mix", 70.0f);
                setParam (*p, "output", 6.0f);
                const int n = std::max (block * 4, 4096);
                auto in = toBuffer (sine (fs, 220.0, 0.7f, n), channels);
                render (*p, in, block); // warm-up

                juce::AudioBuffer<float> io (in);
                juce::MidiBuffer midi;
                long allocs;
                {
                    ScopedAllocationCounter counter;
                    for (int pos = 0; pos < n; pos += block)
                    {
                        juce::AudioBuffer<float> view (io.getArrayOfWritePointers(), channels, pos, std::min (block, n - pos));
                        p->processBlock (view, midi);
                    }
                    allocs = counter.count();
                }
                const auto tag = juce::String (fs) + " Hz, block " + juce::String (block) + ", " + juce::String (channels) + " ch";
                CHECK_MSG (allFinite (io), tag);
                CHECK_MSG (allocs == 0, tag + ": " + juce::String (allocs) + " allocations in processBlock");
                CHECK_MSG (rms (io.getReadPointer (0), n) > 0.01f, tag + ": no output");
                CHECK_MSG (p->grMeter.takeGainReduction() > 3.0f, tag + ": no compression");
            }
}

TEST ("PUNCH: audio test matrix (impulse, sines, stepped sine, log sweep, multitone, noise, bursts, asymmetric stereo): bounded, honest GR, realtime == offline")
{
    const double fs = 48000.0;
    const int n = (int) (fs * 2.0);
    const double twoPi = 2.0 * juce::MathConstants<double>::pi;
    juce::Random rng (11);
    std::vector<std::pair<juce::String, juce::AudioBuffer<float>>> signals;
    auto add = [&] (const juce::String& name, std::vector<float> l, std::vector<float> r = {})
    {
        signals.emplace_back (name, r.empty() ? toBuffer (l, 2) : stereo (l, r));
    };
    std::vector<float> v ((size_t) n, 0.0f);
    v[1000] = 0.9f;
    add ("impulse", v);
    add ("sine 1 kHz -6 dBFS", sine (fs, 1000.0, 0.5f, n));
    for (int i = 0; i < n; ++i) v[(size_t) i] = 0.9f * (float) std::sin (twoPi * 300.0 * i / fs) * std::pow (10.0f, -0.05f * 6.0f * (float) (i / 9600));
    add ("stepped sine 300 Hz, 6 dB steps", v);
    for (int i = 0; i < n; ++i) { const double t = i / fs; v[(size_t) i] = 0.5f * (float) std::sin (twoPi * 20.0 * 2.0 / std::log (1000.0) * (std::exp (std::log (1000.0) * t / 2.0) - 1.0)); }
    add ("log sweep 20 Hz - 20 kHz", v);
    for (int i = 0; i < n; ++i) { double y = 0; for (double hz : { 63.0, 250.0, 1000.0, 4000.0, 12500.0 }) y += 0.15 * std::sin (twoPi * hz * i / fs + hz); v[(size_t) i] = (float) y; }
    add ("multitone", v);
    for (auto& x : v) x = 0.5f * (rng.nextFloat() * 2.0f - 1.0f);
    add ("white noise", v);
    { double b0 = 0, b1 = 0, b2 = 0; // Paul Kellet's economy pink filter
      for (auto& x : v) { const double w = rng.nextFloat() * 2.0 - 1.0; b0 = 0.99765 * b0 + w * 0.0990460; b1 = 0.96300 * b1 + w * 0.2965164; b2 = 0.57000 * b2 + w * 1.0526913;
                          x = (float) (0.25 * (b0 + b1 + b2 + w * 0.1848)); } }
    add ("pink noise", v);
    for (int i = 0; i < n; ++i) v[(size_t) i] = (i % 12000) < 600 ? 0.9f * (float) std::sin (twoPi * 180.0 * i / fs) * std::exp (-(float) (i % 12000) / 200.0f) : 0.0f;
    add ("transient bursts", v);
    add ("asymmetric stereo", sine (fs, 200.0, 0.8f, n), sine (fs, 2500.0, 0.05f, n, 1.0));

    for (auto& [name, in] : signals)
    {
        auto renderMode = [&] (bool offline, std::vector<float>* gr, double* maxTelErr)
        {
            auto p = makePunch (fs, 256);
            p->setNonRealtime (offline);
            configure (*p, fs, 256, -30.0f, 6.0, false, 60.0f);
            std::vector<float> applied (512);
            auto& model = p->getModelForTesting();
            juce::AudioBuffer<float> io (in);
            juce::MidiBuffer midi;
            for (int pos = 0; pos < n; pos += 256)
            {
                model.setGainTraceForTesting (applied.data(), 512);
                juce::AudioBuffer<float> view (io.getArrayOfWritePointers(), 2, pos, 256);
                p->processBlock (view, midi);
                const float tel = std::max (0.0f, p->grMeter.takeGainReduction());
                float g = 1.0f;
                for (int i = 0; i < model.getGainTraceCount(); ++i) g = std::min (g, applied[(size_t) i]);
                if (maxTelErr != nullptr) *maxTelErr = std::max (*maxTelErr, std::abs (tel + 20.0 * std::log10 ((double) g)));
                if (gr != nullptr) gr->push_back (tel);
            }
            model.setGainTraceForTesting (nullptr, 0);
            return io;
        };
        std::vector<float> gr;
        double telErr = 0;
        const auto rt = renderMode (false, &gr, &telErr);
        const auto off = renderMode (true, nullptr, nullptr);
        const float inPeak = in.getMagnitude (0, n), outPeak = rt.getMagnitude (0, n);
        MEASURE (name + ": max GR (dB)", *std::max_element (gr.begin(), gr.end()));
        MEASURE (name + ": output peak / input peak (dB)", db (outPeak / inPeak));
        CHECK_MSG (allFinite (rt), name);
        CHECK_MSG (identical (rt, off), name + ": offline render differs from realtime");
        CHECK_MSG (outPeak < 1.3f * inPeak + 1.0e-3f, name);
        CHECK_MSG (telErr < 1.0e-4, name + ": telemetry != applied gain");
        if (name.startsWith ("white") || name.startsWith ("pink"))
        {
            writeWav (outputDirectory().getChildFile ("PUNCH_matrix_" + name.replaceCharacter (' ', '_') + "_input.wav"), in, fs);
            writeWav (outputDirectory().getChildFile ("PUNCH_matrix_" + name.replaceCharacter (' ', '_') + "_output.wav"), rt, fs);
        }
    }
}

TEST ("PUNCH: NaN/Inf never reaches the output or poisons the state")
{
    auto p = makePunch (48000.0, 256);
    configure (*p, 48000.0, 256, -25.0f, 4.0, false, 100.0f);
    auto in = toBuffer (sine (48000.0, 440.0, 0.5f, 4800), 2);
    in.setSample (0, 100, std::numeric_limits<float>::quiet_NaN());
    in.setSample (1, 200, std::numeric_limits<float>::infinity());
    in.setSample (0, 300, -std::numeric_limits<float>::infinity());
    in.setSample (1, 301, std::numeric_limits<float>::max());
    auto out = render (*p, in, 256);
    CHECK (allFinite (out));
    auto after = render (*p, toBuffer (sine (48000.0, 440.0, 0.5f, 48000), 2), 256);
    CHECK (allFinite (after));
    CHECK (rms (after.getReadPointer (0) + 24000, 24000) > 0.05f);
    const float gr = p->grMeter.takeGainReduction();
    const double expected = specGr (db (0.5f) - 3.0103, -25.0, 4.0, false);
    MEASURE ("GR 1 s after the bad samples (dB)", gr);
    MEASURE ("   static law for the clean tone (dB)", expected);
    CHECK_NEAR (gr, expected, 0.1);
}

TEST ("PUNCH: silence gives silence; DC is rejected by the wet path and the detector; denormal input stays clean")
{
    auto p = makePunch (48000.0, 512);
    configure (*p, 48000.0, 512, -35.0f, infinity, true, 100.0f);
    setParam (*p, "output", 20.0f);
    juce::AudioBuffer<float> silence (2, 48000);
    silence.clear();
    std::vector<float> gr;
    auto out = render (*p, silence, 512, &gr);
    CHECK (out.getMagnitude (0, out.getNumSamples()) == 0.0f);
    CHECK (*std::max_element (gr.begin(), gr.end()) == 0.0f);

    // DC: the 0.5 Hz input coupling removes it from the VCA and the detector.
    auto q = makePunch (48000.0, 512);
    configure (*q, 48000.0, 512, -35.0f, 4.0, false, 0.0f);
    juce::AudioBuffer<float> dc (2, 48000 * 6);
    for (int ch = 0; ch < 2; ++ch)
        juce::FloatVectorOperations::fill (dc.getWritePointer (ch), 0.25f, dc.getNumSamples());
    gr.clear();
    out = render (*q, dc, 512, &gr);
    double mean = 0;
    for (int i = 48000 * 5; i < 48000 * 6; ++i) mean += out.getSample (0, i);
    mean /= 48000.0;
    MEASURE ("residual DC in the last second of 6 s (input 0.25)", mean);
    MEASURE ("GR from a 0.25 DC input after 6 s (dB)", gr.back());
    CHECK (std::abs (mean) < 1.0e-4);
    CHECK (gr.back() < 0.01f);

    // Denormal-level input into a fresh instance with the most gain.
    auto fresh = makePunch (48000.0, 512);
    setParam (*fresh, "output", 20.0f);
    setParam (*fresh, "grit", 100.0f);
    juce::AudioBuffer<float> tiny (2, 48000);
    for (int i = 0; i < tiny.getNumSamples(); ++i)
        for (int ch = 0; ch < 2; ++ch)
            tiny.setSample (ch, i, (i & 1) ? 1.0e-38f : -1.0e-38f);
    out = render (*fresh, tiny, 512);
    CHECK (allFinite (out));
    CHECK (out.getMagnitude (0, out.getNumSamples()) < 1.0e-6f);
}

TEST ("PUNCH: reported latency equals measured impulse delay; Mix 0 and Bypass are bit-exact delayed dry")
{
    for (double fs : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        auto p = makePunch (fs, 128);
        const int latency = p->getLatencySamples();
        MEASURE ("reported latency @" + juce::String (fs), latency);
        CHECK (latency > 0);

        configure (*p, fs, 128, -5.0f, 4.0, false, 0.0f); // a small impulse stays far below threshold
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

        // Mix = 0: delayed dry, bit-exact, while the wet path compresses hard with GRIT 100 %.
        const auto sig = toBuffer (sine (fs, 997.0, 0.5f, 8192), 2);
        configure (*p, fs, 128, -35.0f, infinity, true, 100.0f);
        setParam (*p, "mix", 0.0f);
        p->prepareToPlay (fs, 128);
        std::vector<float> gr;
        out = render (*p, sig, 128, &gr);
        bool exact = true;
        for (int ch = 0; ch < 2; ++ch)
            for (int i = latency; i < sig.getNumSamples(); ++i)
                exact = exact && sameBits (out.getSample (ch, i), sig.getSample (ch, i - latency));
        CHECK_MSG (exact, "Mix=0 is not bit-exact dry");
        CHECK (gr.back() > 20.0f); // the wet path really was compressing

        // Bypass: delayed dry, bit-exact, whatever the other settings (Output trim included).
        setParam (*p, "mix", 100.0f);
        setParam (*p, "output", 7.0f);
        setParam (*p, "bypass", 1.0f);
        p->prepareToPlay (fs, 128);
        out = render (*p, sig, 128);
        exact = true;
        for (int ch = 0; ch < 2; ++ch)
            for (int i = latency; i < sig.getNumSamples(); ++i)
                exact = exact && sameBits (out.getSample (ch, i), sig.getSample (ch, i - latency));
        CHECK_MSG (exact, "Bypass is not bit-exact dry");
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < latency; ++i)
                exact = exact && out.getSample (ch, i) == 0.0f;
        CHECK_MSG (exact, "Bypass: the first `latency` samples are not silent");
    }
}

TEST ("PUNCH: wet path is time-aligned with dry (no comb filtering at Mix 50 %); Output trims after Mix")
{
    const double fs = 48000.0;
    auto p = makePunch (fs, 256);
    configure (*p, fs, 256, -5.0f, 4.0, false, 0.0f);
    setParam (*p, "mix", 50.0f);
    p->prepareToPlay (fs, 256);
    for (double hz : { 20.0, 40.0, 1000.0, 10000.0, 18000.0, 20000.0 })
    {
        auto sig = toBuffer (sine (fs, hz, 0.1f, 96000), 2);
        auto out = render (*p, sig, 256);
        const float change = db (rms (out.getReadPointer (0) + 48000, 48000) / rms (sig.getReadPointer (0) + 48000, 48000));
        MEASURE ("level change at " + juce::String (hz) + " Hz, Mix 50 % (dB)", change);
        CHECK (std::abs (change) < 0.1f);
    }

    // Output acts on the blend (dry included): Mix 0 + Output -6 dB = dry - 6 dB.
    auto q = makePunch (fs, 256);
    setParam (*q, "mix", 0.0f);
    setParam (*q, "output", -6.0f);
    q->prepareToPlay (fs, 256);
    auto sig = toBuffer (sine (fs, 500.0, 0.3f, 24000), 2);
    auto out = render (*q, sig, 256);
    const float change = db (rms (out.getReadPointer (0) + 12000, 12000) / rms (sig.getReadPointer (0) + 12000, 12000));
    MEASURE ("Mix 0 + Output -6 dB: level change (dB)", change);
    CHECK_NEAR (change, -6.0f, 0.01);

    // Frequency response of the wet path without GR (160A: 20 Hz..20 kHz +0/-0.5 dB).
    auto w = makePunch (fs, 256);
    configure (*w, fs, 256, -5.0f, 4.0, false, 0.0f);
    for (double hz : { 20.0, 1000.0, 20000.0 })
    {
        auto s = toBuffer (sine (fs, hz, 0.1f, 96000), 2);
        auto o = render (*w, s, 256);
        const float g = db (rms (o.getReadPointer (0) + 48000, 48000) / rms (s.getReadPointer (0) + 48000, 48000));
        MEASURE ("wet response at " + juce::String (hz) + " Hz (dB)", g);
        CHECK (g < 0.05f && g > -0.5f);
    }
}

TEST ("PUNCH: Mix is a linear blend: out(m) = dry + m (wet - dry) (null test)")
{
    const double fs = 48000.0;
    auto renderAt = [&] (float mix)
    {
        auto p = makePunch (fs, 128);
        configure (*p, fs, 128, -30.0f, 8.0, false, 100.0f);
        setParam (*p, "mix", mix);
        setParam (*p, "output", 3.0f);
        p->prepareToPlay (fs, 128);
        std::vector<float> sig = sine (fs, 220.0, 0.6f, 24000);
        for (size_t i = 0; i < sig.size(); ++i)
            sig[i] *= (i / 4000) % 2 == 0 ? 1.0f : 0.1f; // level steps: compression moves
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

TEST ("PUNCH: static curves: GR vs level for a continuous ratio sweep, thresholds, hard knee and OverEasy match MODEL_SPEC (CSV)")
{
    // Bare model at 96 kHz, constant-envelope input (mean square exactly A^2).
    const double ratios[] = { 1.0, 1.5, 2.0, 3.0, 4.0, 8.0, 20.0, (double) infinity };
    juce::String csv ("ratio,knee,threshold_db,level_db_rms,gr_model_db,gr_function_db,gr_spec_db,error_vs_function_db,error_vs_spec_db\n");
    double maxErrFn = 0, maxErrSpec = 0, maxLevelErr = 0;
    for (double r : ratios)
        for (bool oe : { false, true })
            for (double thr : { -35.0, -20.0, -5.0 })
                for (double level = thr - 20.0; level <= std::min (thr + 30.0, 3.0); level += 1.0)
                {
                    double ms = 0;
                    const double model = steadyGrOfModel (r, level, thr, oe, &ms);
                    const double fn = PunchModel::gainComputerDb (PunchModel::levelDb (ms), thr, slopeOf (r), oe ? 1.0 : 0.0);
                    const double spec = specGr (level, thr, r, oe);
                    maxErrFn = std::max (maxErrFn, std::abs (model - fn));
                    maxErrSpec = std::max (maxErrSpec, std::abs (model - spec));
                    maxLevelErr = std::max (maxLevelErr, std::abs (PunchModel::levelDb (ms) - level));
                    csv << ratioName (r) << "," << (oe ? "overeasy" : "hard") << "," << thr << "," << level << "," << model << ","
                        << fn << "," << spec << "," << model - fn << "," << model - spec << "\n";
                }
    outputDirectory().getChildFile ("PUNCH_static_curves.csv").replaceWithText (csv);
    MEASURE ("max |model - gainComputerDb(detector level)| (dB)", maxErrFn);
    MEASURE ("max |model - MODEL_SPEC law at the input level| (dB)", maxErrSpec);
    MEASURE ("max |detector level - input RMS level| (dB)", maxLevelErr);
    CHECK (maxErrFn < 1.0e-5);
    CHECK (maxErrSpec < 2.0e-3); // input coupling's +0.00014 dB at Nyquist, times the slope
    CHECK (maxLevelErr < 2.0e-3);

    // Hard knee: the measured ratio above threshold is the setting (continuous sweep).
    juce::String rcsv ("ratio_set,slope,ratio_measured\n");
    for (double r = 1.0; r <= 64.0; r *= std::pow (2.0, 0.25))
    {
        const double y0 = -10.0 - steadyGrOfModel (r, -10.0, -30.0, false);
        const double y1 = 0.0 - steadyGrOfModel (r, 0.0, -30.0, false);
        const double measured = 10.0 / (y1 - y0);
        rcsv << r << "," << slopeOf (r) << "," << measured << "\n";
        CHECK_MSG (std::abs (measured - r) < 2.0e-4 * r * r, "ratio " + juce::String (r) + " measured " + juce::String (measured));
    }
    outputDirectory().getChildFile ("PUNCH_ratio_sweep.csv").replaceWithText (rcsv);
    // Infinity: output does not rise above threshold.
    const double outA = -15.0 - steadyGrOfModel (infinity, -15.0, -30.0, false);
    const double outB = 0.0 - steadyGrOfModel (infinity, 0.0, -30.0, false);
    MEASURE ("infinity: output rise for +15 dB of input above threshold (dB)", outB - outA);
    CHECK (std::abs (outB - outA) < 1.0e-3);
    CHECK (std::abs (outA + 30.0) < 2.0e-3);
    // Hard knee: nothing below threshold.
    CHECK (steadyGrOfModel (infinity, -30.5, -30.0, false) == 0.0);

    // OverEasy vs hard knee (plot data): threshold -20 dB RMS, ratios 2, 4, infinity.
    juce::String ocsv ("ratio,level_db_rms,out_hard_db,out_overeasy_db,gr_hard_db,gr_overeasy_db\n");
    for (double r : { 2.0, 4.0, (double) infinity })
    {
        const double s = slopeOf (r);
        for (double level = -45.0; level <= 0.0; level += 0.5)
        {
            const double h = steadyGrOfModel (r, level, -20.0, false), o = steadyGrOfModel (r, level, -20.0, true);
            ocsv << ratioName (r) << "," << level << "," << level - h << "," << level - o << "," << h << "," << o << "\n";
        }
        // Documented OverEasy anchors (MODEL_SPEC 2.3): at threshold GR = s * 2.5 ln 2;
        // the onset (0.1 dB at infinity) about 8 dB below; full ratio within 0.01 dB 15 dB above.
        const double atT = steadyGrOfModel (r, -20.0, -20.0, true);
        MEASURE ("OverEasy " + ratioName (r) + ":1: GR at threshold (dB)", atT);
        CHECK_NEAR (atT, s * specOverEasyScale * std::log (2.0), 2.0e-3);
        CHECK (steadyGrOfModel (r, -20.0, -20.0, false) < 2.0e-3);
        CHECK (steadyGrOfModel (r, -28.0, -20.0, true) > 0.05 * s && steadyGrOfModel (r, -28.0, -20.0, false) == 0.0);
        CHECK (std::abs (steadyGrOfModel (r, -5.0, -20.0, true) - steadyGrOfModel (r, -5.0, -20.0, false)) < 0.01);
        // threshold = midpoint: the local slope there is half the set slope
        const double d = (steadyGrOfModel (r, -19.5, -20.0, true) - steadyGrOfModel (r, -20.5, -20.0, true));
        CHECK_NEAR (d, 0.5 * s, 0.01 * s);
    }
    const double onset = [] { double l = -40.0; while (steadyGrOfModel (infinity, l, -20.0, true) < 0.1) l += 0.1; return l; }();
    MEASURE ("OverEasy infinity: level where GR reaches 0.1 dB (dB re threshold)", onset + 20.0);
    CHECK (onset + 20.0 < -7.0 && onset + 20.0 > -9.0);
    outputDirectory().getChildFile ("PUNCH_overeasy_vs_hard.csv").replaceWithText (ocsv);

    // Through the processor (2x oversampling, 1 kHz sine, THRESHOLD knob):
    // the GR telemetry follows the same law at the sine's RMS level.
    double maxProcErr = 0;
    juce::String pcsv ("ratio,knee,threshold_knob_db,level_dbfs_peak,telemetry_db,law_db\n");
    for (double r : { 1.5, 2.0, 4.0, 8.0, 20.0, (double) infinity })
        for (bool oe : { false, true })
            for (float knob : { -20.0f, 0.0f, 10.0f })
                for (float peak : { -30.0f, -18.0f, -6.0f, 0.0f })
                {
                    const double fs = 48000.0;
                    auto p = makePunch (fs, 480);
                    setParam (*p, "threshold", knob);
                    setParam (*p, "ratio", pp::plainFromRatio ((float) r));
                    setParam (*p, "overEasy", oe ? 1.0f : 0.0f);
                    setParam (*p, "grit", 0.0f);
                    p->prepareToPlay (fs, 480);
                    std::vector<float> gr;
                    render (*p, toBuffer (sine (fs, 1000.0, juce::Decibels::decibelsToGain (peak), 24000), 2), 480, &gr);
                    const double law = specGr (peak - 3.0103, knob + specKnobReference, r, oe);
                    maxProcErr = std::max (maxProcErr, std::abs (gr.back() - law));
                    pcsv << ratioName (r) << "," << (oe ? "overeasy" : "hard") << "," << knob << "," << peak << "," << gr.back() << "," << law << "\n";
                }
    outputDirectory().getChildFile ("PUNCH_static_curves_processor.csv").replaceWithText (pcsv);
    MEASURE ("processor: max |GR telemetry - static law| (1 kHz sine) (dB)", maxProcErr);
    CHECK (maxProcErr < 0.03);
}

TEST ("PUNCH: program-dependent attack at 10/20/30 dB steps follows the RMS detector and the dbx figures (CSV)")
{
    // Step from exactly the threshold to D dB above it. Attack time = time
    // to 63 % of the final GR (MODEL_SPEC 3.2). The single-pole mean-square
    // detector gives t = -tau ln(1 - (10^(-0.0368 D) - 10^(-D/10)) / (1 - 10^(-D/10))).
    const double fs = 96000.0;
    const double dbxMs[] = { 15.0, 5.0, 3.0 };
    const double dbxTol[] = { 0.10, 0.40, 0.10 }; // MODEL_SPEC 3.2: 20 dB is the documented outlier
    juce::String csv ("ratio,knee,step_db,attack_63_model_ms,attack_63_analytic_ms,dbx_typical_ms,error_vs_dbx_pct,final_gr_db\n");
    for (double r : { 4.0, (double) infinity })
        for (bool oe : { false, true })
            for (int k = 0; k < 3; ++k)
            {
                const double D = 10.0 * (k + 1), thr = -40.0;
                auto m = makeModel (fs, r, thr, oe);
                const int pre = (int) fs, n = (int) (0.5 * fs);
                const auto gr = grTrace (m, concat (alternating (ampForRmsDb (thr), pre), alternating (ampForRmsDb (thr + D), n)));
                const double g0 = gr[(size_t) pre - 1], gEnd = gr.back();
                const double t = crossing (gr, pre, g0 + 0.632 * (gEnd - g0), true, fs);
                // analytic, from the mean square, for the hard knee
                const double a = std::pow (10.0, -D / 10.0), target = std::pow (10.0, -0.368 * D / 10.0);
                const double tAnalytic = -specTau * std::log (1.0 - (target - a) / (1.0 - a));
                const double err = 100.0 * (t * 1000.0 - dbxMs[k]) / dbxMs[k];
                csv << ratioName (r) << "," << (oe ? "overeasy" : "hard") << "," << D << "," << t * 1000.0 << "," << tAnalytic * 1000.0
                    << "," << dbxMs[k] << "," << err << "," << gEnd << "\n";
                const auto tag = ratioName (r) + ":1 " + (oe ? "OverEasy" : "hard") + ", " + juce::String (D) + " dB step";
                MEASURE (tag + ": attack to 63 % (ms)", t * 1000.0);
                if (! oe)
                {
                    CHECK_MSG (std::abs (t - tAnalytic) < 0.005 * tAnalytic + 1.0 / fs, tag);
                    CHECK_NEAR (gEnd, slopeOf (r) * D, 0.01);
                    CHECK_MSG (std::abs (t * 1000.0 - dbxMs[k]) <= dbxTol[k] * dbxMs[k], tag + ": vs dbx " + juce::String (err, 1) + " %");
                }
                else
                {
                    CHECK (t > 0.0 && t < 1.6 * dbxMs[k] / 1000.0 + 0.002);
                }
            }
    outputDirectory().getChildFile ("PUNCH_attack_steps.csv").replaceWithText (csv);

    // Program dependence: bigger steps attack faster, from silence too.
    auto fromSilence = [&] (double D)
    {
        auto m = makeModel (fs, infinity, -40.0, false);
        const int pre = 9600, n = (int) (0.5 * fs);
        const auto gr = grTrace (m, concat (alternating (0.0f, pre), alternating (ampForRmsDb (-40.0 + D), n)));
        return crossing (gr, pre, 0.632 * gr.back(), true, fs);
    };
    const double s10 = fromSilence (10.0), s20 = fromSilence (20.0), s30 = fromSilence (30.0);
    MEASURE ("from silence: attack to 63 % for 10 / 20 / 30 dB over threshold (ms)", s10 * 1000.0);
    MEASURE ("   20 dB", s20 * 1000.0);
    MEASURE ("   30 dB", s30 * 1000.0);
    CHECK (s10 > 2.0 * s20 && s20 > 2.0 * s30);

    // Through the processor and its GR telemetry (48 kHz, 16-sample blocks).
    auto procAttack = [] (double D)
    {
        const double hostFs = 48000.0;
        const int block = 16;
        auto p = makePunch (hostFs, block);
        configure (*p, hostFs, block, -30.0f, infinity, false, 0.0f);
        const float a0 = juce::Decibels::decibelsToGain (-30.0f + 3.0103f), a1 = juce::Decibels::decibelsToGain ((float) (-30.0 + D + 3.0103));
        const int pre = 48000, n = 24000;
        auto sig = concat (sine (hostFs, 1000.0, a0, pre), sine (hostFs, 1000.0, a1, n, 2.0 * juce::MathConstants<double>::pi * 1000.0 * pre / hostFs));
        std::vector<float> gr;
        auto out = render (*p, toBuffer (sig, 2), block, &gr);
        if (D == 20.0)
        {
            writeWav (outputDirectory().getChildFile ("PUNCH_attack_20dB_input.wav"), toBuffer (sig, 1), hostFs);
            writeWav (outputDirectory().getChildFile ("PUNCH_attack_20dB_output.wav"), out, hostFs);
        }
        std::vector<double> g (gr.begin(), gr.end());
        // the oversampler's up-sampling filter delays the detector by about half the round-trip latency
        const double delay = 0.5 * p->getLatencySamples() / hostFs;
        return crossing (g, pre / block, g[(size_t) (pre / block) - 1] + 0.632 * (g.back() - g[(size_t) (pre / block) - 1]), true, hostFs / block) - delay;
    };
    for (int k = 0; k < 3; ++k)
    {
        const double D = 10.0 * (k + 1);
        const double t = procAttack (D);
        const double a = std::pow (10.0, -D / 10.0), target = std::pow (10.0, -0.368 * D / 10.0);
        const double tAnalytic = -specTau * std::log (1.0 - (target - a) / (1.0 - a));
        MEASURE ("processor: attack to 63 % for a " + juce::String (D) + " dB step (ms)", t * 1000.0);
        CHECK_NEAR (t * 1000.0, tAnalytic * 1000.0, 0.6); // 16-sample telemetry blocks = 0.33 ms
    }
}

TEST ("PUNCH: program-dependent release: 125 dB/s, 8 / 80 / 400 ms for 1 / 10 / 50 dB (CSV)")
{
    const double fs = 96000.0;
    juce::String csv ("ratio,gr_db,recovery_model_ms,recovery_dbx_ms,error_pct,gr_at_half_time_db\n");
    for (double r : { 2.0, 4.0, (double) infinity })
        for (double G : { 1.0, 10.0, 20.0, 50.0 })
        {
            const double s = slopeOf (r), thr = -60.0;
            auto m = makeModel (fs, r, thr, false);
            const int pre = (int) fs;
            const double over = G / s;
            if (thr + over > 6.0)
                continue;
            const double expected = over / specReleaseRate; // the detector falls at 125 dB/s
            const auto gr = grTrace (m, concat (alternating (ampForRmsDb (thr + over), pre), alternating (0.0f, (int) ((expected + 0.2) * fs))));
            const double start = gr[(size_t) pre - 1];
            int zeroAt = -1;
            for (int i = pre; i < (int) gr.size(); ++i)
                if (gr[(size_t) i] == 0.0) { zeroAt = i; break; }
            const double t = (zeroAt - pre) / fs;
            const double half = gr[(size_t) (pre + (int) (0.5 * expected * fs))];
            const double err = 100.0 * (t - expected) / expected;
            csv << ratioName (r) << "," << start << "," << t * 1000.0 << "," << expected * 1000.0 << "," << err << "," << half << "\n";
            const auto tag = ratioName (r) + ":1, " + juce::String (G) + " dB";
            MEASURE (tag + ": recovery to 0 dB GR (ms)", t * 1000.0);
            CHECK_NEAR (start, G, 0.01);
            CHECK_MSG (zeroAt > 0 && std::abs (t - expected) < 0.002 * expected + 2.0 / fs, tag);
            CHECK_MSG (std::abs (half - 0.5 * G) < 0.01 * G + 0.01, tag + ": not linear in dB"); // straight-line decay in dB
        }
    outputDirectory().getChildFile ("PUNCH_release_steps.csv").replaceWithText (csv);
    // The data sheet's own figures at infinity (GR = level over threshold).
    for (auto [G, ms] : { std::pair { 1.0, 8.0 }, std::pair { 10.0, 80.0 }, std::pair { 50.0, 400.0 } })
        CHECK_NEAR (G / specReleaseRate * 1000.0, ms, 1e-9);

    // Program dependence: a drop to a level still above threshold releases
    // exponentially in the mean square (slower in dB as it nears the new level).
    auto m = makeModel (fs, infinity, -40.0, false);
    const auto gr = grTrace (m, concat (alternating (ampForRmsDb (-20.0), (int) fs), alternating (ampForRmsDb (-30.0), (int) fs)));
    const double t15 = crossing (gr, (int) fs, 15.0, false, fs), t11 = crossing (gr, (int) fs, 11.0, false, fs);
    const auto expectT = [] (double grDb) { const double a = std::pow (10.0, 2.0), b = std::pow (10.0, 1.0); // mean squares re threshold
                                            return -specTau * std::log ((std::pow (10.0, grDb / 10.0) - b) / (a - b)); };
    MEASURE ("20 -> 10 dB over threshold: time to 15 dB GR (ms)", t15 * 1000.0);
    MEASURE ("20 -> 10 dB over threshold: time to 11 dB GR (ms)", t11 * 1000.0);
    CHECK_NEAR (t15, expectT (15.0), 1.0e-4);
    CHECK_NEAR (t11, expectT (11.0), 1.0e-4);
    CHECK ((t11 - t15) / 4.0 > 1.5 * (t15 / 5.0)); // the last dB are slower than the first

    // Through the processor and its GR telemetry (48 kHz, 64-sample blocks).
    for (double G : { 10.0, 30.0 })
    {
        const double hostFs = 48000.0;
        auto p = makePunch (hostFs, 64);
        configure (*p, hostFs, 64, -35.0f, infinity, false, 0.0f);
        const int burst = 48000;
        auto sig = sine (hostFs, 1000.0, juce::Decibels::decibelsToGain ((float) (-35.0 + G + 3.0103)), burst);
        sig.resize ((size_t) (burst + hostFs), 0.0f);
        std::vector<float> tel;
        render (*p, toBuffer (sig, 2), 64, &tel);
        const int endBlock = burst / 64;
        double t = -1;
        for (int b = endBlock; b < (int) tel.size(); ++b)
            if (tel[(size_t) b] < 0.01f) { t = (b - endBlock) * 64.0 / hostFs; break; }
        MEASURE ("processor: telemetry recovery from " + juce::String (G) + " dB (ms)", t * 1000.0);
        CHECK_NEAR (t, G / specReleaseRate, 0.02 * G / specReleaseRate + 0.003);
    }
}

TEST ("PUNCH: linked stereo: a transient only in L reduces R by the same gain; detector is a true-RMS power sum")
{
    const double fs = 48000.0;
    auto p = makePunch (fs, 64);
    configure (*p, fs, 64, -30.0f, 8.0, false, 0.0f);
    const int n = (int) (fs * 1.5);
    auto l = std::vector<float> ((size_t) n, 0.0f);
    const auto burst = sine (fs, 1000.0, 0.5f, (int) (0.5 * fs));
    std::copy (burst.begin(), burst.end(), l.begin() + (int) (0.5 * fs));
    const auto r = sine (fs, 3000.0, 0.01f, n); // -43 dB RMS: far below threshold on its own
    std::vector<float> gr;
    const auto in = stereo (l, r);
    const auto out = render (*p, in, 64, &gr);
    const int latency = p->getLatencySamples();

    const int w0 = (int) (0.75 * fs), wn = (int) (0.2 * fs);
    const float attL = db (rms (in.getReadPointer (0) + w0, wn)) - db (rms (out.getReadPointer (0) + w0 + latency, wn));
    const float attR = db (rms (in.getReadPointer (1) + w0, wn)) - db (rms (out.getReadPointer (1) + w0 + latency, wn));
    const float telemetry = gr[(size_t) ((w0 + wn) / 64)];
    MEASURE ("L (transient) attenuation in the burst (dB)", attL);
    MEASURE ("R (quiet, steady) attenuation in the burst (dB)", attR);
    MEASURE ("GR telemetry (dB)", telemetry);
    CHECK (attL > 6.0f);
    CHECK_NEAR (attR, attL, 0.02);
    CHECK_NEAR (attR, telemetry, 0.05);
    const float before = db (rms (in.getReadPointer (1) + 12000, 9600)) - db (rms (out.getReadPointer (1) + 12000 + latency, 9600));
    MEASURE ("R attenuation before the transient (dB)", before);
    CHECK (std::abs (before) < 0.01f);

    // Power summing (bare model, constant envelope): (A, A) = mono A = (A, -A);
    // (A, 0) and (0, A) read 3.01 dB lower; (A, B) reads the mean of the powers.
    const double mfs = 96000.0;
    const float A = ampForRmsDb (-10.0);
    auto grOf = [&] (float a, float b, bool monoInput = false)
    {
        auto m = makeModel (mfs, infinity, -30.0, false);
        const int len = (int) (0.6 * mfs);
        return monoInput ? grTrace (m, alternating (a, len)).back() : grTrace (m, alternating (a, len), alternating (b, len)).back();
    };
    const double both = grOf (A, A), mono = grOf (A, 0.0f, true), anti = grOf (A, -A), lOnly = grOf (A, 0.0f), rOnly = grOf (0.0f, A),
                 mixed = grOf (A, 0.5f * A);
    MEASURE ("GR (A, A)", both);
    MEASURE ("GR mono A", mono);
    MEASURE ("GR (A, -A)", anti);
    MEASURE ("GR (A, 0)", lOnly);
    MEASURE ("GR (0, A)", rOnly);
    MEASURE ("GR (A, A/2)", mixed);
    CHECK_NEAR (both, 20.0, 0.01);
    CHECK_NEAR (mono, both, 1e-9);
    CHECK_NEAR (anti, both, 1e-9);
    CHECK_NEAR (lOnly, both - 3.0103, 0.001);
    CHECK_NEAR (rOnly, lOnly, 1e-9);
    CHECK_NEAR (mixed, both + 10.0 * std::log10 ((1.0 + 0.25) / 2.0), 0.001);
}

TEST ("PUNCH: GR telemetry equals the applied VCA gain (error trace) and the attenuation measured on the audio")
{
    const double fs = 48000.0;
    const int block = 64;
    const int n = (int) (fs * 3.0);
    std::vector<float> sig = sine (fs, 1000.0, 1.0f, n);
    const float levels[] = { 0.02f, 0.3f, 0.6f, 0.1f, 0.9f, 0.4f };
    for (int i = 0; i < n; ++i)
        sig[(size_t) i] *= levels[(i / (int) (0.5 * fs)) % 6];

    juce::String trace ("ratio,knee,block,telemetry_db,applied_db,error_db\n");
    double maxErr = 0;
    for (double ratio : { 2.0, 4.0, 20.0, (double) infinity })
        for (bool oe : { false, true })
        {
            auto p = makePunch (fs, block);
            configure (*p, fs, block, -30.0f, ratio, oe, 0.0f);
            std::vector<float> applied ((size_t) (2 * block));
            auto& model = p->getModelForTesting();
            auto io = toBuffer (sig, 2);
            juce::MidiBuffer midi;
            std::vector<float> telemetry;
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
                if (ratio == 4.0)
                    trace << ratioName (ratio) << "," << (oe ? "overeasy" : "hard") << "," << pos / block << "," << tel << "," << appliedDb << "," << err << "\n";
                telemetry.push_back (tel);
                CHECK (model.getGainTraceCount() == 2 * block);
            }
            model.setGainTraceForTesting (nullptr, 0);

            // Audio check (GRIT 0): in windows where the telemetry is steady, the
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
                if (tMax - tMin > 0.03f || tMean < 0.5)
                    continue;
                const double att = db (rms (sig.data() + s, win)) - db (rms (io.getReadPointer (0) + s + lat, win));
                maxAudioErr = std::max (maxAudioErr, std::abs (att - tMean));
                ++windows;
            }
            const auto tag = ratioName (ratio) + ":1 " + (oe ? "OverEasy" : "hard");
            MEASURE (tag + ": max |telemetry - attenuation on the audio| (dB), steady windows", maxAudioErr);
            MEASURE (tag + ": steady windows compared", windows);
            CHECK_MSG (windows >= 20, tag);
            CHECK_MSG (maxAudioErr < 0.05, tag);
        }
    outputDirectory().getChildFile ("PUNCH_gr_telemetry_error_trace.csv").replaceWithText (trace);
    MEASURE ("max |telemetry - applied VCA gain| over all blocks, ratios and knees (dB)", maxErr);
    CHECK (maxErr < 1.0e-4);

    // Beyond the 20 dB the LED meter shows, the telemetry stays honest (not clipped).
    auto p = makePunch (fs, 480);
    configure (*p, fs, 480, -35.0f, infinity, false, 0.0f);
    std::vector<float> gr;
    render (*p, toBuffer (sine (fs, 1000.0, 0.9f, 48000), 2), 480, &gr);
    const double law = specGr (db (0.9f) - 3.0103, -35.0, infinity, false);
    MEASURE ("GR telemetry for 31 dB over threshold at infinity (dB)", gr.back());
    CHECK_NEAR (gr.back(), law, 0.03);
    CHECK (gr.back() > 30.0f);
}

TEST ("PUNCH: GRIT: Clean is clean; Punch adds bounded, level- and GR-dependent harmonics; the gain law is unchanged (CSV)")
{
    // No GR: ratio 1:1. With GR: 4:1, threshold -25 dB RMS, a -6 dBFS sine (16 dB over, 12 dB GR).
    juce::String csv ("grit_pct,condition,level_dbfs_peak,thd_pct,h2_pct,h3_pct,gr_db\n");
    auto row = [&csv] (float grit, const char* cond, float level, const ToneResult& r)
    {
        csv << grit << "," << cond << "," << level << "," << r.thd * 100 << "," << r.h2 * 100 << "," << r.h3 * 100 << "," << r.grDb << "\n";
    };
    const float grits[] = { 0.0f, 20.0f, 50.0f, 100.0f };
    ToneResult noGr[4], withGr[4];
    for (int k = 0; k < 4; ++k)
    {
        noGr[k] = renderTone (grits[k], -6.0f, -25.0f, 1.0);
        withGr[k] = renderTone (grits[k], -6.0f, -25.0f, 4.0);
        row (grits[k], "no_gr", -6.0f, noGr[k]);
        row (grits[k], "gr", -6.0f, withGr[k]);
        MEASURE ("GRIT " + juce::String (grits[k]) + " %, no GR: THD %", noGr[k].thd * 100);
        MEASURE ("GRIT " + juce::String (grits[k]) + " %, 12 dB GR: THD %", withGr[k].thd * 100);
        MEASURE ("GRIT " + juce::String (grits[k]) + " %, 12 dB GR: GR (dB)", withGr[k].grDb);
    }
    // Clean bound: -140 dB without GR (numerically linear). With GR, only the
    // hardware's own RMS-detector ripple remains: < 0.2 % (160A data sheet).
    CHECK (noGr[0].thd < 1.0e-7);
    CHECK (withGr[0].thd < 2.0e-3);
    // Monotonic in GRIT, measurable at the default, bounded at Punch.
    for (int k = 1; k < 4; ++k)
    {
        CHECK (noGr[k].thd > 1.5 * noGr[k - 1].thd);
        CHECK (withGr[k].thd > 1.5 * withGr[k - 1].thd);
    }
    CHECK (noGr[1].thd > 2.0e-4 && withGr[1].thd < 5.0e-3); // default 20 %: subtle but present
    CHECK (withGr[3].thd < 0.05 && noGr[3].thd < 0.05);       // Punch: bounded
    // Tied to gain reduction: at the same input level, more GR -> more GRIT harmonics.
    CHECK (withGr[3].thd > 1.5 * noGr[3].thd);
    // GRIT never changes the gain law.
    CHECK (withGr[0].grDb > 10.0f);
    for (int k = 1; k < 4; ++k)
        CHECK_NEAR (withGr[k].grDb, withGr[0].grDb, 1.0e-3);

    // Level dependence (no GR, Punch): THD grows with level.
    double prev = 0;
    for (float level : { -30.0f, -18.0f, -12.0f, -6.0f, 0.0f })
    {
        const auto r = renderTone (100.0f, level, -25.0f, 1.0);
        row (100.0f, "level_no_gr", level, r);
        MEASURE ("Punch, no GR: THD % at " + juce::String (level) + " dBFS", r.thd * 100);
        CHECK (r.thd > prev * 1.5);
        prev = r.thd;
    }
    // GR dependence at a fixed input (Punch): deeper GR, more VCA colour.
    double prevGr = 0;
    for (float thr : { -5.0f, -13.0f, -21.0f, -29.0f })
    {
        const auto r = renderTone (100.0f, -6.0f, thr, infinity);
        row (100.0f, "gr_sweep", -6.0f, r);
        MEASURE ("Punch, -6 dBFS in, GR " + juce::String (r.grDb, 1) + " dB: VCA 2nd harmonic % of output", r.h2 * 100);
        if (r.grDb > 0.5f)
            CHECK (r.h2 > prevGr * 1.2);
        prevGr = r.h2;
    }
    outputDirectory().getChildFile ("PUNCH_grit_thd.csv").replaceWithText (csv);

    // Aliasing inspection: Punch, 15 kHz at -1 dBFS, 12 dB GR, 48 kHz host.
    // Without oversampling the 3rd harmonic (45 kHz) would fold to 3 kHz.
    {
        const double fs = 48000.0;
        auto p = makePunch (fs, 480);
        configure (*p, fs, 480, -25.0f, 4.0, false, 100.0f);
        auto out = render (*p, toBuffer (sine (fs, 15000.0, 0.89f, 96000), 2), 480);
        const float* d = out.getReadPointer (0) + 48000;
        const double fund = toneAmplitude (d, 48000, fs, 15000.0);
        double worst = 0;
        for (double hz : { 3000.0, 6000.0, 9000.0, 12000.0, 18000.0, 21000.0 }) // folds of harmonics 2..6
        {
            const double a = toneAmplitude (d, 48000, fs, hz);
            MEASURE ("Punch, 15 kHz: component at " + juce::String (hz) + " Hz (dBc)", 20.0 * std::log10 (a / fund + 1e-12));
            worst = std::max (worst, a);
        }
        MEASURE ("Punch, 15 kHz: worst alias component (dBc)", 20.0 * std::log10 (worst / fund + 1e-12));
        CHECK (worst / fund < 1.0e-5); // -100 dBc
    }
}

TEST ("PUNCH: OverEasy, ratio and GRIT changes under compression are click-free; rapid automation stays finite")
{
    const double fs = 48000.0;
    const int block = 64, blocks = 1600;
    auto run = [&] (bool switching, float slope, bool oe, float grit)
    {
        auto p = makePunch (fs, block);
        setParam (*p, "threshold", knobFor (-30.0f));
        setParam (*p, "ratio", slope);
        setParam (*p, "overEasy", oe ? 1.0f : 0.0f);
        setParam (*p, "grit", grit);
        p->prepareToPlay (fs, block);
        juce::MidiBuffer midi;
        float y1 = 0, y2 = 0, maxCurv = 0;
        for (int b = 0; b < blocks; ++b)
        {
            if (switching && b % 50 == 0)
            {
                const int k = b / 50;
                setParam (*p, "ratio", k % 2 == 0 ? 1.0f : 0.0f);   // 1:1 <-> infinity, the largest jump
                setParam (*p, "overEasy", (k / 2) % 2 == 0 ? 1.0f : 0.0f);
                setParam (*p, "grit", (k / 3) % 2 == 0 ? 100.0f : 0.0f);
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
    for (float s : { 0.0f, 0.5f, 0.75f, 0.95f, 1.0f })
        for (bool oe : { false, true })
            for (float g : { 0.0f, 100.0f })
                baseline = std::max (baseline, run (false, s, oe, g));
    const float switched = run (true, 0.75f, false, 0.0f);
    MEASURE ("max |2nd difference| over fixed settings", baseline);
    MEASURE ("max |2nd difference| while jumping ratio 1:1 <-> inf, OverEasy and GRIT", switched);
    MEASURE ("equivalent instant gain step while switching (dB)", switched / (0.3 * 0.1151));
    CHECK (switched < 4.0f * baseline);
    CHECK (switched < 2.0e-3f);

    // Rapid random automation of every parameter.
    auto p = makePunch (fs, 64);
    juce::Random rng (42);
    float prevSample = 0.0f, maxJump = 0.0f;
    bool finite = true;
    juce::MidiBuffer midi;
    for (int b = 0; b < 3000; ++b)
    {
        setParam (*p, "threshold", -20.0f + rng.nextFloat() * 30.0f);
        setParam (*p, "ratio", rng.nextFloat());
        setParam (*p, "overEasy", rng.nextBool() ? 1.0f : 0.0f);
        setParam (*p, "grit", rng.nextFloat() * 100.0f);
        setParam (*p, "mix", rng.nextFloat() * 100.0f);
        setParam (*p, "output", -6.0f + rng.nextFloat() * 12.0f);
        auto io = toBuffer (sine (fs, 200.0, 0.3f, 64, 2.0 * juce::MathConstants<double>::pi * 200.0 * b * 64 / fs), 2);
        p->processBlock (io, midi);
        for (int i = 0; i < 64; ++i)
        {
            finite = finite && std::isfinite (io.getSample (0, i));
            maxJump = std::max (maxJump, std::abs (io.getSample (0, i) - prevSample));
            prevSample = io.getSample (0, i);
        }
    }
    MEASURE ("max sample-to-sample jump under random automation", maxJump);
    CHECK (finite);
    CHECK (maxJump < 0.06f); // a 200 Hz sine at up to ~0.6 peak moves at most ~0.016 per sample
}

TEST ("PUNCH: the editor (open, resized, UI properties, meter reads) does not alter the audio")
{
    const double fs = 48000.0;
    auto renderWith = [&] (bool withEditor)
    {
        auto p = makePunch (fs, 256);
        configure (*p, fs, 256, -28.0f, 6.0, true, 40.0f);
        std::unique_ptr<juce::AudioProcessorEditor> ed;
        if (withEditor)
        {
            ed.reset (p->createEditor());
            CHECK (ed != nullptr);
            ed->setSize (900, 700);
            p->setUiProperty ("scale", 1.5);
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
            juce::AudioBuffer<float> view (out.getArrayOfWritePointers(), 2, pos, std::min (256, out.getNumSamples() - pos));
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
                p->getDetectorLevelDb();
            }
        }
        ed.reset();
        return out;
    };
    CHECK_MSG (identical (renderWith (false), renderWith (true)), "the editor altered the audio");
}

TEST ("PUNCH: the detector level hand-off sits on the static curve")
{
    const double fs = 48000.0;
    auto p = makePunch (fs, 512);
    configure (*p, fs, 512, -20.0f, 4.0, true, 0.0f);
    std::vector<float> gr;
    render (*p, toBuffer (sine (fs, 1000.0, juce::Decibels::decibelsToGain (-9.0f), 48000), 2), 512, &gr);
    const float level = p->getDetectorLevelDb();
    MEASURE ("detector level for a -9 dBFS sine (dB RMS)", level);
    CHECK_NEAR (level, -12.0103, 0.02);
    CHECK_NEAR (gr.back(), PunchModel::gainComputerDb (level, -20.0, 0.75, 1.0), 0.02);
}

TEST ("PUNCH: state round trip restores every parameter and UI settings and renders identically")
{
    const double fs = 48000.0;
    auto a = makePunch (fs, 256);
    setParam (*a, "threshold", -7.5f);
    setParam (*a, "ratio", 0.9f);
    setParam (*a, "overEasy", 1.0f);
    setParam (*a, "grit", 61.0f);
    setParam (*a, "mix", 72.0f);
    setParam (*a, "output", -3.5f);
    a->setUiProperty ("scale", 1.25);

    juce::MemoryBlock state;
    a->getStateInformation (state);
    auto b = makePunch (fs, 256);
    b->setStateInformation (state.getData(), (int) state.getSize());
    for (auto* param : a->getParameters())
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (param))
            CHECK_NEAR (r->getValue(), b->apvts.getParameter (r->paramID)->getValue(), 1e-6);
    CHECK_NEAR ((double) b->getUiProperty ("scale", 1.0), 1.25, 1e-9);

    a->prepareToPlay (fs, 256);
    b->prepareToPlay (fs, 256);
    const auto sig = toBuffer (sine (fs, 300.0, 0.4f, 48000), 2);
    CHECK_MSG (identical (render (*a, sig, 256), render (*b, sig, 256)), "restored instance renders differently");
    auto fresh = makePunch (fs, 256);
    CHECK (! identical (render (*fresh, sig, 256), render (*b, sig, 256)));

    b->setStateInformation ("nonsense", 8);
    CHECK_NEAR (plainValue (*b, "mix"), 72.0f, 0.01);
}

TEST ("PUNCH: factory presets load, set every parameter and reject other plugins' presets")
{
    PunchProcessor p;
    const auto& list = p.presets.getPresets();
    int factory = 0;
    for (auto& pr : list) factory += pr.isFactory ? 1 : 0;
    MEASURE ("factory presets", factory);
    CHECK (factory >= 10);
    CHECK (list.size() > 0 && list[0].isFactory);

    std::set<int> ratiosSeen;
    std::set<bool> kneesSeen;
    bool sawInfinity = false, sawParallel = false;
    for (int i = 0; i < (int) list.size(); ++i)
        if (list[(size_t) i].isFactory)
        {
            p.loadPreset (i);
            CHECK (list[(size_t) i].params.values.size() == 6);
            for (auto& [id, v] : list[(size_t) i].params.values)
            {
                auto* r = p.apvts.getParameter (id);
                CHECK_MSG (r != nullptr, id);
                if (r) CHECK_NEAR (r->convertFrom0to1 (r->getValue()), v, 1e-3);
            }
            CHECK (! p.isPresetModified());
            ratiosSeen.insert (juce::roundToInt (plainValue (p, "ratio") * 100.0f));
            kneesSeen.insert (plainValue (p, "overEasy") > 0.5f);
            sawInfinity = sawInfinity || std::isinf (pp::ratioFromPlain (plainValue (p, "ratio")));
            sawParallel = sawParallel || plainValue (p, "mix") < 99.0f;
        }
    CHECK (ratiosSeen.size() >= 6 && kneesSeen.size() == 2 && sawInfinity && sawParallel);

    setParam (p, "mix", 13.0f);
    CHECK (p.isPresetModified());
    PresetManager::Preset out;
    CHECK (p.presets.parse (R"({"format":"tleet-preset","version":1,"plugin":"GLUE","name":"x","params":{}})", out).failed());
    CHECK (p.presets.parse (R"({"format":"tleet-preset","version":99,"plugin":"PUNCH","name":"x","params":{}})", out).failed());
    const auto json = p.stateToClipboardJson();
    setParam (p, "mix", 99.0f);
    CHECK (p.pasteStateFromJson (json).wasOk());
    CHECK_NEAR (plainValue (p, "mix"), 13.0f, 0.06);
}

TEST ("PUNCH: CPU cost per instance and per-block timing (measurement)")
{
    for (double fs : { 44100.0, 48000.0, 96000.0, 192000.0 })
        for (int block : { 64, 512 })
        {
            auto p = makePunch (fs, block);
            configure (*p, fs, block, -25.0f, 4.0, true, 50.0f);
            setParam (*p, "mix", 80.0f);
            juce::Random rng (1);
            const int n = (int) (fs * 5.0);
            juce::AudioBuffer<float> in (2, n);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < n; ++i)
                    in.setSample (ch, i, 0.5f * (float) std::sin (i * 0.031) * (rng.nextFloat() * 0.6f + 0.4f));
            std::vector<double> times;
            times.reserve ((size_t) (n / block + 1));
            juce::MidiBuffer midi;
            const auto t0 = juce::Time::getMillisecondCounterHiRes();
            for (int pos = 0; pos + block <= n; pos += block)
            {
                juce::AudioBuffer<float> view (in.getArrayOfWritePointers(), 2, pos, block);
                const auto s = juce::Time::getHighResolutionTicks();
                p->processBlock (view, midi);
                times.push_back (juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - s));
            }
            const double ms = juce::Time::getMillisecondCounterHiRes() - t0;
            std::sort (times.begin(), times.end());
            const double blockS = block / fs;
            const auto tag = juce::String (fs) + " Hz, block " + juce::String (block);
            MEASURE (tag + ": % of one core", ms / 5000.0 * 100.0);
            MEASURE (tag + ": p99 block time (% of the block period)", 100.0 * times[(size_t) (0.99 * (times.size() - 1))] / blockS);
            MEASURE (tag + ": max block time (% of the block period)", 100.0 * times.back() / blockS);
            // budget grows with the rate (the cost per sample is fixed): 5 % of a core at 48 kHz,
            // more than twice the measured cost, so scheduler noise on a busy machine does not fail it
            CHECK (ms / 5000.0 * 100.0 < 5.0 * fs / 48000.0);
        }

    // Breakdown at 48 kHz, block 128, stereo: the 2x model alone and the oversampler alone.
    {
        const double fs = 48000.0;
        const int block = 128, n = (int) (fs * 5.0);
        auto m = makeModel (2.0 * fs, 4.0, -25.0, true, 0.5f);
        juce::AudioBuffer<float> buf (2, 2 * block);
        juce::Random rng (2);
        double modelS = 0, osS = 0;
        for (int pos = 0; pos + block <= n; pos += block)
        {
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 2 * block; ++i)
                    buf.setSample (ch, i, 0.4f * (float) std::sin ((pos * 2 + i) * 0.0155) * (rng.nextFloat() * 0.6f + 0.4f));
            const auto s0 = juce::Time::getHighResolutionTicks();
            m.process (buf.getArrayOfWritePointers(), 2, 2 * block);
            modelS += juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - s0);
        }
        juce::dsp::Oversampling<float> os (2, 1, juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple, true, true);
        os.initProcessing ((size_t) block);
        juce::AudioBuffer<float> hb (2, block);
        for (int pos = 0; pos + block <= n; pos += block)
        {
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < block; ++i)
                    hb.setSample (ch, i, 0.4f * (float) std::sin ((pos + i) * 0.031));
            juce::dsp::AudioBlock<float> b (hb);
            const auto s0 = juce::Time::getHighResolutionTicks();
            os.processSamplesUp (b);
            os.processSamplesDown (b);
            osS += juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - s0);
        }
        MEASURE ("48 kHz breakdown: PunchModel at 2x (% of one core)", modelS / 5.0 * 100.0);
        MEASURE ("48 kHz breakdown: 2x FIR oversampler up + down (% of one core)", osS / 5.0 * 100.0);
    }
}
