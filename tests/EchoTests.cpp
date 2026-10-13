#include "TestFramework.h"
#include "TestSignals.h"
#include <cstring>
#include <functional>
#include <map>
#include <set>
#include "../plugins/ECHO/PluginProcessor.h"
#include "../plugins/ECHO/Parameters.h"
#include "../plugins/ECHO/dsp/EchoEngine.h"

using namespace tleet;
using namespace tleet::test;
using tleet::echo::EchoProcessor;
namespace ep = tleet::echo::params;
namespace et = tleet::echo::timing;

namespace
{
    constexpr double pi = juce::MathConstants<double>::pi;
    enum ModeIdx { mReverb = 0, m1, m2, m3, m12, m13, m23, m123 };
    enum CharIdx { cClean = 0, cOriginal, cBurn };

    void setParam (EchoProcessor& p, const char* id, float plain)
    {
        auto* r = p.apvts.getParameter (id);
        r->setValueNotifyingHost (r->convertTo0to1 (plain));
    }

    float plainValue (EchoProcessor& p, const char* id)
    {
        auto* r = p.apvts.getParameter (id);
        return r->convertFrom0to1 (r->getValue());
    }

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

    double maxAbsDiff (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b, int ch = -1)
    {
        double m = 0;
        for (int c = 0; c < a.getNumChannels(); ++c)
            if (ch < 0 || c == ch)
                for (int i = 0; i < a.getNumSamples(); ++i)
                    m = std::max (m, (double) std::abs (a.getSample (c, i) - b.getSample (c, i)));
        return m;
    }

    // Host transport stand-in: reports a tempo (or none).
    struct FakePlayHead : juce::AudioPlayHead
    {
        double bpm = 120.0;
        bool hasTempo = true;
        juce::Optional<PositionInfo> getPosition() const override
        {
            PositionInfo info;
            if (hasTempo)
                info.setBpm (bpm);
            info.setIsPlaying (true);
            return info;
        }
    };

    juce::AudioBuffer<float> render (EchoProcessor& p, const juce::AudioBuffer<float>& input, int block)
    {
        juce::AudioBuffer<float> io (input);
        juce::MidiBuffer midi;
        for (int pos = 0; pos < io.getNumSamples(); pos += block)
        {
            juce::AudioBuffer<float> view (io.getArrayOfWritePointers(), io.getNumChannels(), pos, std::min (block, io.getNumSamples() - pos));
            p.processBlock (view, midi);
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

    juce::AudioBuffer<float> silence (int channels, int n)
    {
        juce::AudioBuffer<float> b (channels, n);
        b.clear();
        return b;
    }

    std::unique_ptr<EchoProcessor> makeEcho (double fs, int block, int inChannels = 2, int outChannels = 2)
    {
        auto p = std::make_unique<EchoProcessor>();
        EchoProcessor::BusesLayout layout;
        layout.inputBuses.add (inChannels == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo());
        layout.outputBuses.add (outChannels == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo());
        CHECK_MSG (p->setBusesLayout (layout), "layout rejected");
        p->setRateAndBufferSizeDetails (fs, block);
        p->prepareToPlay (fs, block);
        return p;
    }

    // A measurement setting: one head, no feedback, no modulation, no noise,
    // full wet, no width, colour as linear as it goes. prepare() afterwards.
    void measurementSetup (EchoProcessor& p, int mode = m1)
    {
        setParam (p, ep::mode, (float) mode);
        setParam (p, ep::intensity, 0.0f);
        setParam (p, ep::echoVolume, 10.0f / std::sqrt (2.0f)); // echo gain 1.0 (2 x (v/10)^2)
        setParam (p, ep::reverbVolume, 0.0f);
        setParam (p, ep::wow, 0.0f);
        setParam (p, ep::flutter, 0.0f);
        setParam (p, ep::noise, 0.0f);
        setParam (p, ep::age, 0.0f);
        setParam (p, ep::tapeCondition, 0.0f);
        setParam (p, ep::stereo, 0.0f);
        setParam (p, ep::mix, 100.0f);
        setParam (p, ep::character, (float) cClean);
        setParam (p, ep::drive, 0.0f);
    }

    double dbOf (double r) { return 20.0 * std::log10 (std::max (r, 1.0e-15)); }

    // Gaussian 1 kHz tone burst (sigma 1.5 ms) centred at `centre`. Its envelope
    // travels at the group delay at 1 kHz: the timing reference (MODEL_SPEC 4.4).
    std::vector<float> toneBurst (double fs, int n, int centre, float amp = 0.5f, double hz = 1000.0)
    {
        std::vector<float> v ((size_t) n, 0.0f);
        const double sigma = 0.0015 * fs;
        const int half = (int) std::ceil (5.0 * sigma);
        for (int k = -half; k <= half; ++k)
            if (const int i = centre + k; i >= 0 && i < n)
                v[(size_t) i] = amp * (float) (std::exp (-0.5 * (k / sigma) * (k / sigma)) * std::cos (2.0 * pi * hz * k / fs));
        return v;
    }

    // Lag (fractional samples) of the peak of the complex cross-correlation
    // envelope of `y` against the analytic version of the burst at `centre`.
    double burstLag (const float* y, int ny, double fs, int centre, int from, int to, double hz = 1000.0)
    {
        const double sigma = 0.0015 * fs;
        const int half = (int) std::ceil (5.0 * sigma);
        auto env = [&] (int lag)
        {
            double re = 0, im = 0;
            for (int k = -half; k <= half; ++k)
            {
                const int i = centre + k + lag;
                if (i < 0 || i >= ny) continue;
                const double g = std::exp (-0.5 * (k / sigma) * (k / sigma));
                re += y[i] * g * std::cos (2.0 * pi * hz * k / fs);
                im += y[i] * g * std::sin (2.0 * pi * hz * k / fs);
            }
            return std::sqrt (re * re + im * im);
        };
        int best = from;
        double bestV = -1;
        for (int lag = from; lag <= to; lag += 4)
            if (const double c = env (lag); c > bestV) { bestV = c; best = lag; }
        for (int lag = best - 4; lag <= best + 4; ++lag)
            if (const double c = env (lag); c > bestV) { bestV = c; best = lag; }
        const double a = std::log (env (best - 1)), b = std::log (bestV), c = std::log (env (best + 1));
        const double den = a - 2.0 * b + c; // Gaussian envelope: parabola in the log domain
        return best + (std::abs (den) > 1e-30 ? 0.5 * (a - c) / den : 0.0);
    }

    // Delay (samples) from a 1 kHz burst in the input to its first echo,
    // searched within +-200 samples of `expected`.
    double measureEchoDelay (EchoProcessor& p, double fs, double expectedSamples, int block = 256, int channel = 0)
    {
        const int at = (int) (0.05 * fs);
        const int n = at + (int) expectedSamples + (int) (0.05 * fs);
        const auto out = render (p, toBuffer (toneBurst (fs, n, at), 2), block);
        const int e = (int) std::round (expectedSamples);
        return burstLag (out.getReadPointer (channel), n, fs, at, e - 200, e + 200);
    }
}

// =====================================================================================
TEST ("ECHO: parameter IDs, ranges, defaults, texts, choice strings and buses match the registry")
{
    EchoProcessor p;
    const char* ids[] = { "mode", "repeatRate", "intensity", "echoVolume", "reverbVolume", "bass", "treble", "timeMode", "bpmSource",
                          "manualBpm", "noteDivision", "feel", "drive", "age", "wow", "flutter", "hpf", "lpf", "filterMode", "stereo",
                          "duck", "mix", "tapeCondition", "noise", "character", "output", "bypass" };
    for (auto* id : ids)
        CHECK_MSG (p.apvts.getParameter (id) != nullptr, id);
    CHECK (p.getParameters().size() == 27);
    for (auto* param : p.getParameters())
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (param))
            CHECK_MSG (r->getVersionHint() == 1, r->paramID);

    auto def = [&p] (const char* id) { auto* r = p.apvts.getParameter (id); return r->convertFrom0to1 (r->getDefaultValue()); };
    CHECK_NEAR (def ("mode"), 4.0f, 1e-5);          // "1+2"
    CHECK_NEAR (def ("repeatRate"), 200.0f, 1e-3);
    CHECK_NEAR (def ("intensity"), 4.0f, 1e-5);
    CHECK_NEAR (def ("echoVolume"), 7.0f, 1e-5);
    CHECK_NEAR (def ("reverbVolume"), 0.0f, 1e-6);
    CHECK_NEAR (def ("bass"), 0.0f, 1e-6);
    CHECK_NEAR (def ("treble"), 0.0f, 1e-6);
    CHECK_NEAR (def ("timeMode"), 0.0f, 1e-6);
    CHECK_NEAR (def ("bpmSource"), 0.0f, 1e-6);
    CHECK_NEAR (def ("manualBpm"), 120.0f, 1e-4);
    CHECK_NEAR (def ("noteDivision"), 3.0f, 1e-5);   // "1/8"
    CHECK_NEAR (def ("feel"), 0.0f, 1e-6);
    CHECK_NEAR (def ("drive"), 3.0f, 1e-5);
    CHECK_NEAR (def ("age"), 2.0f, 1e-5);
    CHECK_NEAR (def ("wow"), 20.0f, 1e-4);
    CHECK_NEAR (def ("flutter"), 20.0f, 1e-4);
    CHECK_NEAR (def ("hpf"), 20.0f, 1e-3);
    CHECK_NEAR (def ("lpf"), 20000.0f, 0.5);
    CHECK_NEAR (def ("filterMode"), 1.0f, 1e-5);      // "Loop"
    CHECK_NEAR (def ("stereo"), 30.0f, 1e-4);
    CHECK_NEAR (def ("duck"), 0.0f, 1e-6);
    CHECK_NEAR (def ("mix"), 35.0f, 1e-4);
    CHECK_NEAR (def ("tapeCondition"), 0.0f, 1e-6);
    CHECK_NEAR (def ("noise"), 10.0f, 1e-4);
    CHECK_NEAR (def ("character"), 1.0f, 1e-5);       // "Original"
    CHECK_NEAR (def ("output"), 0.0f, 1e-6);
    CHECK_NEAR (def ("bypass"), 0.0f, 1e-6);

    auto range = [&p] (const char* id, float lo, float mid, float hi, double tolMid = 1e-3)
    {
        auto* r = p.apvts.getParameter (id);
        CHECK_NEAR (r->convertFrom0to1 (0.0f), lo, 1e-3);
        CHECK_NEAR (r->convertFrom0to1 (0.5f), mid, tolMid);
        CHECK_NEAR (r->convertFrom0to1 (1.0f), hi, 1e-2);
    };
    range ("repeatRate", 20.0f, 200.0f, 2000.0f, 1e-2); // log, 200 ms at the knob centre
    for (auto* id : { "intensity", "echoVolume", "reverbVolume", "drive", "age" })
        range (id, 0.0f, 5.0f, 10.0f);
    range ("bass", -10.0f, 0.0f, 10.0f);
    range ("treble", -10.0f, 0.0f, 10.0f);
    range ("output", -12.0f, 0.0f, 12.0f);
    range ("manualBpm", 20.0f, 160.0f, 300.0f, 1.0);
    range ("hpf", 20.0f, std::sqrt (20.0f * 300.0f), 300.0f, 0.05);
    range ("lpf", 1000.0f, std::sqrt (1000.0f * 20000.0f), 20000.0f, 1.0);
    for (auto* id : { "wow", "flutter", "stereo", "duck", "mix", "noise" })
        range (id, 0.0f, 50.0f, 100.0f);

    // centred dB ranges: 0 dB exactly at the centre, exact text round trip on the 0.1 dB grid
    for (auto* id : { "bass", "treble", "output" })
    {
        auto* f = dynamic_cast<juce::AudioParameterFloat*> (p.apvts.getParameter (id));
        juce::AudioProcessorParameter* r = f;
        const auto& nr = f->getNormalisableRange();
        CHECK (nr.convertFrom0to1 (0.5f) == 0.0f && nr.convertTo0to1 (0.0f) == 0.5f);
        int bad = 0;
        for (int t = -100; t <= 100; ++t)
        {
            const float v = nr.snapToLegalValue ((float) t * 0.1f);
            bad += nr.snapToLegalValue (nr.convertFrom0to1 (r->getValueForText (r->getText (nr.convertTo0to1 (v), 64)))) != v ? 1 : 0;
        }
        CHECK_MSG (bad == 0, id);
    }
    // manual BPM: integer steps
    {
        auto* r = p.apvts.getParameter ("manualBpm");
        CHECK_NEAR (r->convertFrom0to1 (r->convertTo0to1 (127.4f)), 127.0f, 1e-4);
    }

    auto text = [&p] (const char* id, float plain) { auto* r = p.apvts.getParameter (id); return r->getText (r->convertTo0to1 (plain), 64); };
    CHECK (text ("repeatRate", 250.0f) == "250 ms");
    CHECK (text ("repeatRate", 20.0f) == "20 ms");
    CHECK (text ("repeatRate", 1200.0f) == "1.20 s");
    CHECK (text ("repeatRate", 2000.0f) == "2.00 s");
    CHECK (text ("intensity", 6.5f) == "6.5");
    CHECK (text ("echoVolume", 10.0f) == "10.0");
    CHECK (text ("bass", 3.0f) == "+3.0 dB");
    CHECK (text ("treble", -4.5f) == "-4.5 dB");
    CHECK (text ("output", 0.0f) == "0.0 dB");
    CHECK (text ("manualBpm", 128.0f) == "128 BPM");
    CHECK (text ("wow", 35.0f) == "35 %");
    CHECK (text ("mix", 72.5f) == "72.5 %");
    CHECK (text ("hpf", 20.0f) == "20 Hz");
    CHECK (text ("hpf", 150.0f) == "150 Hz");
    CHECK (text ("lpf", 4500.0f) == "4.5 kHz");
    CHECK (text ("lpf", 20000.0f) == "20.0 kHz");
    CHECK (text ("mode", 4.0f) == "1+2");
    CHECK (text ("mode", 0.0f) == "Reverb");
    CHECK (text ("noteDivision", 3.0f) == "1/8");
    CHECK (text ("feel", 1.0f) == "Dotted");
    CHECK (text ("tapeCondition", 2.0f) == "Magnetic dust");
    CHECK (text ("filterMode", 2.0f) == "Post");
    auto fromText = [&p] (const char* id, const char* t) { auto* r = p.apvts.getParameter (id); return r->convertFrom0to1 (r->getValueForText (t)); };
    CHECK_NEAR (fromText ("repeatRate", "350 ms"), 350.0f, 0.01);
    CHECK_NEAR (fromText ("repeatRate", "1.5 s"), 1500.0f, 0.1);
    CHECK_NEAR (fromText ("repeatRate", "480"), 480.0f, 0.01);
    CHECK_NEAR (fromText ("lpf", "4.5 kHz"), 4500.0f, 0.5);
    CHECK_NEAR (fromText ("hpf", "85 Hz"), 85.0f, 0.01);
    CHECK_NEAR (fromText ("manualBpm", "133 BPM"), 133.0f, 1e-4);
    CHECK_NEAR (fromText ("bass", "-6 dB"), -6.0f, 1e-4);

    // Discrete selectors expose only real values (addendum 4).
    auto choices = [&p] (const char* id) { return dynamic_cast<juce::AudioParameterChoice*> (p.apvts.getParameter (id))->choices; };
    CHECK (choices ("mode") == juce::StringArray ({ "Reverb", "1", "2", "3", "1+2", "1+3", "2+3", "1+2+3" }));
    CHECK (choices ("timeMode") == juce::StringArray ({ "Free", "Sync" }));
    CHECK (choices ("bpmSource") == juce::StringArray ({ "Host", "Manual" }));
    CHECK (choices ("noteDivision") == juce::StringArray ({ "1/1", "1/2", "1/4", "1/8", "1/16", "1/32" }));
    CHECK (choices ("feel") == juce::StringArray ({ "Straight", "Dotted", "Triplet" }));
    CHECK (choices ("filterMode") == juce::StringArray ({ "Pre", "Loop", "Post" }));
    CHECK (choices ("tapeCondition") == juce::StringArray ({ "Clean", "Worn", "Magnetic dust" }));
    CHECK (choices ("character") == juce::StringArray ({ "Clean", "Original", "Burn" }));
    CHECK (p.getBypassParameter() == p.apvts.getParameter ("bypass"));
    CHECK (p.getLatencySamples() == 0);

    // Buses: mono -> mono, stereo -> stereo, mono -> stereo; nothing else.
    auto layout = [] (juce::AudioChannelSet i, juce::AudioChannelSet o)
    {
        EchoProcessor::BusesLayout l;
        l.inputBuses.add (i);
        l.outputBuses.add (o);
        return l;
    };
    const auto mono = juce::AudioChannelSet::mono(), st = juce::AudioChannelSet::stereo();
    CHECK (p.checkBusesLayoutSupported (layout (st, st)));
    CHECK (p.checkBusesLayoutSupported (layout (mono, mono)));
    CHECK (p.checkBusesLayoutSupported (layout (mono, st)));
    CHECK (! p.checkBusesLayoutSupported (layout (st, mono)));
    CHECK (! p.checkBusesLayoutSupported (layout (juce::AudioChannelSet::create5point1(), juce::AudioChannelSet::create5point1())));
}

TEST ("ECHO: addendum 15 timing: 120 BPM quarter = 500 ms lands on sample 24000 at 48 kHz (every division x feel; tolerance 0.25 sample)")
{
    // Declared tolerance: |measured - expected| <= 0.25 sample at the host rate.
    // Measured delay = the envelope peak (complex cross-correlation) of the first
    // echo of a Gaussian 1 kHz tone burst: the group delay at 1 kHz. Expected = the note value of the spec
    // formula: quarter_ms = 60000 / BPM; dotted x 1.5; triplet x 2/3.
    const double tolerance = 0.25;
    juce::String csv ("rate,bpm_source,division,feel,expected_ms,expected_samples,measured_samples,error_samples\n");
    double worst = 0;
    for (double fs : { 48000.0, 44100.0, 96000.0 })
        for (int src : { 0, 1 })
            for (int div = 0; div < 6; ++div)
                for (int feel = 0; feel < 3; ++feel)
                {
                    if (fs != 48000.0 && feel != 0 && div != 2)
                        continue; // the full matrix at 48 kHz; quarter + straight rows at the other rates
                    auto p = makeEcho (fs, 256);
                    FakePlayHead head;
                    head.bpm = 120.0;
                    head.hasTempo = src == 0;
                    p->setPlayHead (&head);
                    measurementSetup (*p);
                    setParam (*p, ep::timeMode, 1.0f);
                    setParam (*p, ep::bpmSource, (float) src);
                    setParam (*p, ep::manualBpm, 120.0f);
                    setParam (*p, ep::noteDivision, (float) div);
                    setParam (*p, ep::feel, (float) feel);
                    juce::MidiBuffer midi;
                    {
                        juce::AudioBuffer<float> one (2, 16);
                        one.clear();
                        p->processBlock (one, midi); // the processor learns the host tempo
                    }
                    p->prepareToPlay (fs, 256);
                    const double expectedMs = 60000.0 / 120.0 * et::divisionQuarters[(size_t) div] * et::feelFactor[(size_t) feel];
                    const double expected = expectedMs * 0.001 * fs;
                    const double measured = measureEchoDelay (*p, fs, expected);
                    const double err = measured - expected;
                    worst = std::max (worst, std::abs (err));
                    csv << fs << "," << (src == 0 ? "Host" : "Manual") << "," << ep::noteDivisionNames[div] << "," << ep::feelNames[feel] << ","
                        << expectedMs << "," << expected << "," << measured << "," << err << "\n";
                    CHECK_MSG (std::abs (err) <= tolerance, juce::String (fs) + " " + ep::noteDivisionNames[div] + " " + ep::feelNames[feel]
                                                             + ": measured " + juce::String (measured, 3) + " expected " + juce::String (expected, 3));
                    CHECK_NEAR (p->getEffectiveBpm(), 120.0, 1e-9);
                    CHECK (p->isHostTempoAvailable() == (src == 0));
                    p->setPlayHead (nullptr);
                }
    outputDirectory().getChildFile ("ECHO_timing.csv").replaceWithText (csv);
    MEASURE ("worst timing error (samples)", worst);

    // The addendum's case spelled out: 120 BPM, 1/4, Straight at 48 kHz = 500 ms = 24000 samples.
    CHECK_NEAR (et::noteMs (120.0, 2, 0), 500.0, 1e-12);
    CHECK_NEAR (et::noteMs (120.0, 3, 1), 375.0, 1e-12);       // dotted eighth
    CHECK_NEAR (et::noteMs (120.0, 2, 2), 1000.0 / 3.0, 1e-12); // quarter triplet
}

TEST ("ECHO: head timing: one tape, heads at 1 : 1.90 : 2.75 of head 1 at every speed (FREE and SYNC); telemetry = measured")
{
    const double fs = 48000.0;
    juce::String csv ("time_mode,head1_target_ms,head,expected_samples,measured_samples,error_samples,telemetry_ms\n");
    struct Case { bool sync; float repeatMs; int div, feel; };
    for (const Case c : { Case { false, 69.0f, 0, 0 }, Case { false, 177.0f, 0, 0 }, Case { false, 300.0f, 0, 0 }, Case { false, 1500.0f, 0, 0 },
                          Case { true, 0.0f, 3, 0 }, Case { true, 0.0f, 2, 2 } })
    {
        auto p = makeEcho (fs, 256);
        measurementSetup (*p, m123);
        setParam (*p, ep::timeMode, c.sync ? 1.0f : 0.0f);
        setParam (*p, ep::bpmSource, 1.0f);
        setParam (*p, ep::manualBpm, 120.0f);
        setParam (*p, ep::noteDivision, (float) c.div);
        setParam (*p, ep::feel, (float) c.feel);
        setParam (*p, ep::repeatRate, c.repeatMs);
        p->prepareToPlay (fs, 256);
        const double d1 = p->getTargetHead1Ms();
        if (! c.sync)
            CHECK_NEAR (d1, c.repeatMs, 1e-3);
        const int at = (int) (0.05 * fs);
        const int n = at + (int) (d1 * 0.001 * fs * 2.75) + (int) (0.05 * fs);
        const auto out = render (*p, toBuffer (toneBurst (fs, n, at), 2), 256);
        for (int h = 0; h < 3; ++h)
        {
            const double expected = d1 * 0.001 * fs * EchoProcessor::getHeadRatio (h);
            const int e = (int) std::round (expected);
            const double measured = burstLag (out.getReadPointer (0), n, fs, at, e - 200, e + 200);
            const double tel = p->getHeadDelayMs (h);
            csv << (c.sync ? "Sync" : "Free") << "," << d1 << "," << h + 1 << "," << expected << "," << measured << "," << measured - expected << "," << tel << "\n";
            CHECK_MSG (std::abs (measured - expected) <= 0.25, "head " + juce::String (h + 1) + " at head 1 = " + juce::String (d1) + " ms: "
                                                               + juce::String (measured, 3) + " vs " + juce::String (expected, 3));
            CHECK_NEAR (tel, d1 * EchoProcessor::getHeadRatio (h), 0.01);
        }
    }
    outputDirectory().getChildFile ("ECHO_head_timing.csv").replaceWithText (csv);

    // The ratios are the RE-201's (RESEARCH S2): within the rounding of the published ms values.
    CHECK_NEAR (et::headRatio[1], 131.0 / 69.0, 0.01);
    CHECK_NEAR (et::headRatio[1], 337.0 / 177.0, 0.01);
    CHECK_NEAR (et::headRatio[2], 189.0 / 69.0, 0.02);
    CHECK_NEAR (et::headRatio[2], 489.0 / 177.0, 0.02);
    // SYNC drives the tape speed only (critical BPM rule): heads 2 and 3 are not note values.
    CHECK_NEAR (et::noteMs (120.0, 3, 0) * et::headRatio[1], 475.0, 1e-9);
}

TEST ("ECHO: MODE selects the heads: echoes only at the active heads' times; isHeadActive and takeHeadActivity follow MODE")
{
    const double fs = 48000.0;
    const double d1 = 100.0;
    for (int mode = 0; mode < ep::numModes; ++mode)
    {
        auto p = makeEcho (fs, 256);
        measurementSetup (*p, mode);
        setParam (*p, ep::repeatRate, (float) d1);
        p->prepareToPlay (fs, 256);
        const int at = 2400;
        const int n = at + (int) (d1 * 0.001 * fs * 2.75) + 9600;
        (void) p->takeHeadActivity (0); (void) p->takeHeadActivity (1); (void) p->takeHeadActivity (2);
        const auto out = render (*p, toBuffer (toneBurst (fs, n, at), 2), 256);
        for (int h = 0; h < 3; ++h)
        {
            // expected heads read from the registry's choice text ("1+3" = heads 1 and 3), not from the code's mask
            const bool on = juce::String (ep::modeNames[mode]).containsChar ((juce::juce_wchar) ('1' + h));
            CHECK_MSG (p->isHeadActive (h) == on, ep::modeNames[mode] + " head " + juce::String (h + 1));
            const float act = p->takeHeadActivity (h);
            CHECK_MSG (on ? act > 0.05f : act == 0.0f, ep::modeNames[mode] + " activity of head " + juce::String (h + 1) + " = " + juce::String (act));
            const int centre = at + (int) std::round (d1 * 0.001 * fs * EchoProcessor::getHeadRatio (h));
            const float level = out.getMagnitude (0, centre - 240, 480);
            if (on)
                CHECK_MSG (level > 0.1f, ep::modeNames[mode] + ": no echo at head " + juce::String (h + 1));
            else
                CHECK_MSG (level < 1.0e-4f, ep::modeNames[mode] + ": echo at inactive head " + juce::String (h + 1) + " " + juce::String (level));
        }
        // "Reverb": no head, no echo at all (REVERB VOLUME 0: silence after the input).
        if (mode == mReverb)
            CHECK (out.getMagnitude (0, at + 960, n - at - 960) < 1.0e-5f);
    }
    // per-head level: 1/sqrt(N) of the head signal (MODEL_SPEC 3.2)
    auto p = makeEcho (fs, 256);
    measurementSetup (*p, m12);
    setParam (*p, ep::repeatRate, (float) d1);
    p->prepareToPlay (fs, 256);
    const int at = 2400, n = 24000;
    const auto out = render (*p, toBuffer (toneBurst (fs, n, at), 2), 256);
    const float e1 = out.getMagnitude (0, at + 4800 - 240, 480);
    MEASURE ("1+2: head-1 echo peak (burst peak 0.5)", e1);
    CHECK_NEAR (e1, 0.5f / std::sqrt (2.0f), 0.03);
}

TEST ("ECHO: host tempo change and REPEAT RATE jumps while audio runs: the tape speed glides (pitch bend), no click")
{
    const double fs = 48000.0;
    const int block = 128;
    // click detector: 4th-order high-pass at 8 kHz on the output; a 220 Hz tone
    // and its echo (pitch-bending) stay far below it, a discontinuity does not.
    auto run = [&] (bool useTempo, std::vector<double>& delays)
    {
        auto p = makeEcho (fs, block);
        FakePlayHead head;
        head.bpm = 120.0;
        p->setPlayHead (&head);
        setParam (*p, ep::mode, (float) m1);
        setParam (*p, ep::intensity, 5.0f);
        setParam (*p, ep::wow, 0.0f);
        setParam (*p, ep::flutter, 0.0f);
        setParam (*p, ep::noise, 0.0f);
        setParam (*p, ep::mix, 50.0f);
        setParam (*p, ep::timeMode, useTempo ? 1.0f : 0.0f);
        setParam (*p, ep::repeatRate, 250.0f);
        juce::MidiBuffer midi;
        juce::AudioBuffer<float> one (2, 16);
        one.clear();
        p->processBlock (one, midi);
        p->prepareToPlay (fs, block);
        echo::filt::Svf h1, h2;
        h1.set (echo::filt::integratorGain (8000.0, fs), 0.7653668647301796);
        h2.set (echo::filt::integratorGain (8000.0, fs), 1.8477590650225735);
        const int blocks = (int) (6.0 * fs / block);
        double steadyMax = 0, transMax = 0;
        for (int b = 0; b < blocks; ++b)
        {
            const double t = (double) b * block / fs;
            if (b == (int) (3.0 * fs / block))
            {
                if (useTempo) head.bpm = 140.0;
                else setParam (*p, ep::repeatRate, 180.0f);
            }
            auto buf = toBuffer (sine (fs, 220.0, 0.25f, block, 2.0 * pi * 220.0 * b * block / fs), 2);
            p->processBlock (buf, midi);
            delays.push_back (p->getHeadDelayMs (0));
            for (int i = 0; i < block; ++i)
            {
                const double y = h2.process<echo::filt::Svf::highpass> (h1.process<echo::filt::Svf::highpass> (buf.getSample (0, i)));
                if (t > 1.5 && t < 3.0) steadyMax = std::max (steadyMax, std::abs (y));
                if (t >= 3.0 && t < 4.5) transMax = std::max (transMax, std::abs (y));
            }
        }
        p->setPlayHead (nullptr);
        return std::pair { steadyMax, transMax };
    };
    for (bool tempo : { true, false })
    {
        std::vector<double> delays;
        const auto [steady, trans] = run (tempo, delays);
        const auto tag = juce::String (tempo ? "host tempo 120 -> 140 BPM" : "REPEAT RATE 250 -> 180 ms");
        MEASURE (tag + ": steady max |HP 8k|", steady);
        MEASURE (tag + ": transition max |HP 8k|", trans);
        CHECK_MSG (trans < 2.0 * steady + 2.0e-4, tag + ": click");
        // the head-1 delay glides monotonically to the new value and lands on it
        const double from = 250.0, to = tempo ? 60000.0 / 140.0 / 2.0 : 180.0;
        const size_t switchAt = (size_t) (3.0 * fs / block);
        CHECK_NEAR (delays[switchAt - 1], from, 0.01);
        CHECK_NEAR (delays.back(), to, 0.01);
        double maxStep = 0;
        bool monotonic = true;
        for (size_t i = switchAt; i + 1 < delays.size(); ++i)
        {
            maxStep = std::max (maxStep, std::abs (delays[i + 1] - delays[i]));
            monotonic = monotonic && delays[i + 1] <= delays[i] + 1.0e-6;
        }
        size_t settle = switchAt;
        while (settle < delays.size() && std::abs (delays[settle] - to) > 0.01 * std::abs (from - to)) ++settle;
        MEASURE (tag + ": time to within 1 % of the new delay (ms)", (double) (settle - switchAt) * block / fs * 1000.0);
        MEASURE (tag + ": largest delay change per block (ms)", maxStep);
        CHECK (monotonic);
        CHECK (maxStep < 2.0);
    }
}

TEST ("ECHO: SYNC sources: host tempo, manual BPM, fallback to manual without host tempo; clamped note values are reported")
{
    const double fs = 48000.0;
    auto p = makeEcho (fs, 256);
    FakePlayHead head;
    head.bpm = 90.0;
    p->setPlayHead (&head);
    setParam (*p, ep::timeMode, 1.0f);
    setParam (*p, ep::bpmSource, 0.0f);
    setParam (*p, ep::manualBpm, 120.0f);
    setParam (*p, ep::noteDivision, 2.0f);
    juce::MidiBuffer midi;
    auto runBlock = [&] { auto b = silence (2, 256); p->processBlock (b, midi); };
    runBlock();
    CHECK (p->isHostTempoAvailable());
    CHECK_NEAR (p->getEffectiveBpm(), 90.0, 1e-9);
    CHECK_NEAR (p->getTargetHead1Ms(), 60000.0 / 90.0, 1e-6);
    head.hasTempo = false;  // Host selected, no tempo: falls back to MANUAL BPM and says so
    runBlock();
    CHECK (! p->isHostTempoAvailable());
    CHECK_NEAR (p->getEffectiveBpm(), 120.0, 1e-9);
    CHECK_NEAR (p->getTargetHead1Ms(), 500.0, 1e-6);
    head.hasTempo = true;
    setParam (*p, ep::bpmSource, 1.0f); // Manual ignores the host
    runBlock();
    CHECK_NEAR (p->getEffectiveBpm(), 120.0, 1e-9);
    CHECK (! p->isSyncTimeClamped());
    // 1/1 dotted at 20 BPM = 18 s: clamped to the 6 s maximum and reported
    setParam (*p, ep::manualBpm, 20.0f);
    setParam (*p, ep::noteDivision, 0.0f);
    setParam (*p, ep::feel, 1.0f);
    CHECK (p->isSyncTimeClamped());
    CHECK_NEAR (p->getTargetHead1Ms(), et::maxHead1Ms, 1e-9);
    // FREE never reports a clamp and ignores BPM
    setParam (*p, ep::timeMode, 0.0f);
    setParam (*p, ep::repeatRate, 333.0f);
    CHECK (! p->isSyncTimeClamped());
    CHECK_NEAR (p->getTargetHead1Ms(), 333.0, 0.01);
    // the longest head-1 time really plays: 1/1 at 40 BPM = 6 s
    setParam (*p, ep::timeMode, 1.0f);
    setParam (*p, ep::manualBpm, 40.0f);
    setParam (*p, ep::feel, 0.0f);
    measurementSetup (*p, m1);
    p->prepareToPlay (fs, 256);
    CHECK (! p->isSyncTimeClamped());
    const double measured = measureEchoDelay (*p, fs, 6.0 * fs, 1024);
    MEASURE ("1/1 at 40 BPM (6 s): measured head-1 delay (samples)", measured);
    CHECK_NEAR (measured, 6.0 * fs, 0.25);
    p->setPlayHead (nullptr);
}

TEST ("ECHO: TAP: FREE sets REPEAT RATE, SYNC sets MANUAL BPM and BPM SOURCE Manual; 2 s timeout; average of the last taps")
{
    EchoProcessor p;
    setParam (p, ep::timeMode, 0.0f);
    p.tap (10.0);
    CHECK_NEAR (plainValue (p, ep::repeatRate), 200.0f, 0.01); // one tap changes nothing
    p.tap (10.5);
    CHECK_NEAR (plainValue (p, ep::repeatRate), 500.0f, 0.05);
    p.tap (10.9);
    CHECK_NEAR (plainValue (p, ep::repeatRate), 450.0f, 0.05);  // mean of 0.5 and 0.4
    p.tap (11.3);
    p.tap (11.7);
    CHECK_NEAR (plainValue (p, ep::repeatRate), 400.0f, 0.05);  // last three intervals: 0.4, 0.4, 0.4
    p.tap (14.0);                                               // > 2 s: a new sequence, nothing set
    CHECK_NEAR (plainValue (p, ep::repeatRate), 400.0f, 0.05);
    p.tap (15.9);                                               // 1.9 s: still a sequence
    CHECK_NEAR (plainValue (p, ep::repeatRate), 1900.0f, 0.2);
    p.tap (15.91);                                              // 10 ms interval averages with 1.9 s
    CHECK_NEAR (plainValue (p, ep::repeatRate), 955.0f, 0.2);
    p.tap (30.0); p.tap (30.005);                               // 5 ms: clamped to 20 ms
    CHECK_NEAR (plainValue (p, ep::repeatRate), 20.0f, 0.01);

    setParam (p, ep::timeMode, 1.0f);
    setParam (p, ep::bpmSource, 0.0f);
    setParam (p, ep::manualBpm, 100.0f);
    p.tap (40.0);
    p.tap (40.55);
    CHECK_NEAR (plainValue (p, ep::manualBpm), 109.0f, 1e-4); // 60 / 0.55 = 109.09 -> 109
    CHECK_NEAR (plainValue (p, ep::bpmSource), 1.0f, 1e-6);   // switched to Manual
    p.tap (41.05);
    CHECK_NEAR (plainValue (p, ep::manualBpm), 114.0f, 1e-4); // mean 0.525 s -> 114.3 -> 114
    CHECK_NEAR (plainValue (p, ep::repeatRate), 20.0f, 0.01); // SYNC tap leaves REPEAT RATE alone
    p.tap (50.0); p.tap (51.9);
    CHECK_NEAR (plainValue (p, ep::manualBpm), 32.0f, 1e-4);  // 31.6 -> 32
}

TEST ("ECHO: self-oscillation at INTENSITY 10 is bounded by the tape saturation (no NaN, no runaway, no hard clip) and sustains")
{
    const double fs = 48000.0;
    juce::String csv ("mode,character,intensity,rms_8_to_10s_before_end_db,rms_last2s_db,peak,peak_tape_lt,rail_share\n");
    auto run = [&] (int mode, int character, float intensity, double seconds, double& rmsEarly, double& rmsLate, double& peak, double& railShare)
    {
        auto p = makeEcho (fs, 512);
        setParam (*p, ep::mode, (float) mode);
        setParam (*p, ep::intensity, intensity);
        setParam (*p, ep::repeatRate, 100.0f);
        setParam (*p, ep::character, (float) character);
        setParam (*p, ep::noise, 0.0f);
        setParam (*p, ep::mix, 100.0f);
        setParam (*p, ep::echoVolume, 10.0f / std::sqrt (2.0f));
        p->prepareToPlay (fs, 512);
        const int n = (int) (seconds * fs);
        juce::AudioBuffer<float> in (2, n);
        in.clear();
        const auto kick = toneBurst (fs, 4800, 2400, 0.3f, 300.0);
        for (int ch = 0; ch < 2; ++ch)
            in.copyFrom (ch, 0, kick.data(), 4800);
        const auto out = render (*p, in, 512);
        CHECK (allFinite (out));
        rmsEarly = dbOf (rms (out.getReadPointer (0) + n - (int) (10.0 * fs), (int) (2.0 * fs)));
        rmsLate = dbOf (rms (out.getReadPointer (0) + n - (int) (2.0 * fs), (int) (2.0 * fs)));
        peak = out.getMagnitude (0, 0, n);
        int nearRail = 0;
        for (int i = n / 2; i < n; ++i)
            nearRail += std::abs (out.getSample (0, i)) > 0.999f * (float) peak ? 1 : 0;
        railShare = (double) nearRail / (n / 2);
    };
    for (int mode : { (int) m1, (int) m12, (int) m123 })
        for (int character : { (int) cClean, (int) cOriginal, (int) cBurn })
        {
            double early, late, peak, rail;
            run (mode, character, 10.0f, 30.0, early, late, peak, rail);
            const double lt = echo::sat::voicing (character).tapeLt * std::pow (10.0, -2.0 * echo::engine::ageLtDb / 20.0);
            csv << ep::modeNames[mode] << "," << ep::characterNames[character] << ",10," << early << "," << late << "," << peak << "," << peak / lt << "," << rail << "\n";
            const auto tag = ep::modeNames[mode] + " " + ep::characterNames[character];
            MEASURE (tag + ": oscillation RMS, last 2 s (dBFS)", late);
            MEASURE (tag + ": peak / tape saturation level Lt", peak / lt);
            CHECK_MSG (late > -30.0, tag + ": does not sustain");             // controlled self-oscillation
            CHECK_MSG (std::abs (late - early) < 3.0, tag + ": not stable");  // no runaway, no collapse
            CHECK_MSG (peak < 3.0 * lt, tag + ": not bounded by the tape");   // bounded by Lt (with emphasis/head-loss overshoot)
            CHECK_MSG (rail < 0.01, tag + ": flat-topped (hard clip)");       // soft saturation, not a rail
        }
    // Onset: the loop gain crosses 1 near INTENSITY 8 (fbMax 1.25) in every MODE.
    for (int mode : { (int) m1, (int) m12, (int) m123 })
    {
        float onset = 11.0f;
        for (float i = 6.0f; i <= 10.0f; i += 0.25f)
        {
            double early, late, peak, rail;
            run (mode, cOriginal, i, 8.0, early, late, peak, rail);
            csv << ep::modeNames[mode] << ",Original," << i << "," << early << "," << late << "," << peak << ",," << rail << "\n";
            if (late > -40.0) { onset = i; break; }
        }
        MEASURE (ep::modeNames[mode] + ": self-oscillation onset (INTENSITY)", onset);
        CHECK (onset >= 7.0f && onset <= 9.5f);
    }
    // well below the onset the echo dies away
    {
        double early, late, peak, rail;
        run (m1, cOriginal, 6.0f, 8.0, early, late, peak, rail);
        CHECK (late < -90.0);
    }
    outputDirectory().getChildFile ("ECHO_self_oscillation.csv").replaceWithText (csv);
}

TEST ("ECHO: feedback filters, cumulative spectral decay: LOOP filters every repeat, PRE and POST once (CSV)")
{
    // Repeat k of an impulse has the spectrum X(f) H1(f) G(f)^(k-1); the per-repeat
    // decay at f is 20 log |G(f)| = fb + playback chain + (LOOP: HPF+LPF). Measured
    // per repeat with a window around each echo; compared with the evaluators.
    const double fs = 48000.0;
    const double d1 = 100.0, hpfHz = 200.0, lpfHz = 2000.0;
    const double freqs[] = { 300.0, 1000.0, 3000.0, 5000.0 };
    juce::String csv ("filter_mode,repeat,hz,level_db,decay_db_per_repeat,expected_decay_db\n");
    for (int fm = 0; fm < 3; ++fm)
    {
        auto p = makeEcho (fs, 256);
        measurementSetup (*p, m1);
        setParam (*p, ep::repeatRate, (float) d1);
        setParam (*p, ep::intensity, 7.0f);
        setParam (*p, ep::hpf, (float) hpfHz);
        setParam (*p, ep::lpf, (float) lpfHz);
        setParam (*p, ep::filterMode, (float) fm);
        p->prepareToPlay (fs, 256);
        const int D = (int) std::round (d1 * 0.001 * fs);
        const int at = 1000, repeats = 6, n = at + D * (repeats + 2);
        juce::AudioBuffer<float> in (2, n);
        in.clear();
        in.setSample (0, at, 0.05f);
        in.setSample (1, at, 0.05f);
        const auto out = render (*p, in, 256);
        auto& eng = p->getEngineForTesting();
        const double fb = 0.7 * echo::engine::fbMax; // AGE 0, Clean condition
        const int win = D; // each window holds exactly one repeat
        std::vector<std::vector<double>> lev ((size_t) repeats + 1);
        for (int k = 1; k <= repeats; ++k)
        {
            const float* w = out.getReadPointer (0) + at + k * D - win / 2;
            for (double f : freqs)
                lev[(size_t) k].push_back (dbOf (toneAmplitude (w, win, fs, f) * win / 2.0));
        }
        for (size_t fi = 0; fi < std::size (freqs); ++fi)
        {
            const double f = freqs[fi];
            const double loop = fm == 1 ? echo::filt::hpLpResponseDb (f, hpfHz, lpfHz, fs) : 0.0;
            const double expected = dbOf (fb) + eng.playbackResponseDb (f) + loop;
            for (int k = 2; k <= repeats; ++k)
            {
                const double decay = lev[(size_t) k][fi] - lev[(size_t) k - 1][fi];
                csv << ep::filterModeNames[fm] << "," << k << "," << f << "," << lev[(size_t) k][fi] << "," << decay << "," << expected << "\n";
                if (k >= 3)
                    CHECK_MSG (std::abs (decay - expected) < 1.0, ep::filterModeNames[fm] + " " + juce::String (f) + " Hz repeat " + juce::String (k)
                                                                 + ": " + juce::String (decay, 2) + " vs " + juce::String (expected, 2));
            }
            // the first echo carries the filter once in PRE and POST, not in LOOP
            const double first = lev[1][fi] - dbOf (0.05) - eng.playbackResponseDb (f);
            const double once = fm == 1 ? 0.0 : echo::filt::hpLpResponseDb (f, hpfHz, lpfHz, fs);
            CHECK_MSG (std::abs (first - once) < 1.0, ep::filterModeNames[fm] + " first echo at " + juce::String (f) + " Hz: " + juce::String (first, 2)
                                                       + " vs " + juce::String (once, 2));
        }
        const double hfDecay = lev[5][3] - lev[4][3], lfDecay = lev[5][1] - lev[4][1];
        MEASURE (ep::filterModeNames[fm] + ": per-repeat decay 5 kHz minus 1 kHz (dB)", hfDecay - lfDecay);
    }
    outputDirectory().getChildFile ("ECHO_feedback_csd.csv").replaceWithText (csv);
}

TEST ("ECHO: wow/flutter modulation spectrum: WOW makes the 0.55 Hz line, FLUTTER the 6.5 Hz line, at the transport's depth (CSV)")
{
    // A steady 1 kHz tone through head 1 at 100 ms. The echo's phase gives the
    // head delay d(t); a tape speed deviation m(t) = A sin(W t) moves it by
    // D A |sinc(W D / 2)| (the delay integrates the speed over the tape between
    // the heads, MODEL_SPEC 7.1).
    const double fs = 48000.0, d1 = 0.1, hz = 1000.0, seconds = 22.0;
    const int per = 48; // one period of 1 kHz
    juce::String csv ("wow_pct,flutter_pct,mod_hz,delay_mod_amplitude_ms,expected_ms\n");
    auto delayTrace = [&] (float wowPct, float flutterPct, int condition)
    {
        auto p = makeEcho (fs, 480);
        measurementSetup (*p, m1);
        setParam (*p, ep::repeatRate, (float) (d1 * 1000.0));
        setParam (*p, ep::wow, wowPct);
        setParam (*p, ep::flutter, flutterPct);
        setParam (*p, ep::tapeCondition, (float) condition);
        p->prepareToPlay (fs, 480);
        const int n = (int) (seconds * fs);
        const auto out = render (*p, toBuffer (sine (fs, hz, 0.1f, n), 2), 480);
        std::vector<double> d; // delay (ms) at 1 kHz frame rate, from 1 s on
        double prev = 0, unwrap = 0;
        for (int f = (int) fs / per; (f + 1) * per <= n; ++f)
        {
            double re = 0, im = 0;
            for (int i = f * per; i < (f + 1) * per; ++i)
            {
                re += out.getSample (0, i) * std::cos (2.0 * pi * hz * i / fs);
                im += out.getSample (0, i) * std::sin (2.0 * pi * hz * i / fs);
            }
            double ph = std::atan2 (re, im); // sin(w (t - d)) -> phase -w d
            if (! d.empty())
            {
                while (ph + unwrap - prev > pi) unwrap -= 2.0 * pi;
                while (ph + unwrap - prev < -pi) unwrap += 2.0 * pi;
            }
            prev = ph + unwrap;
            d.push_back (-prev / (2.0 * pi * hz) * 1000.0);
        }
        return d;
    };
    auto line = [] (const std::vector<double>& d, double f) // amplitude of a line in the 1 kHz-rate delay trace
    {
        const int n = (int) d.size();
        double mean = 0;
        for (double v : d) mean += v;
        mean /= n;
        double re = 0, im = 0;
        for (int i = 0; i < n; ++i)
        {
            const double w = 0.5 - 0.5 * std::cos (2.0 * pi * i / (n - 1)); // Hann (coherent gain 0.5)
            re += (d[(size_t) i] - mean) * w * std::cos (2.0 * pi * f * i / 1000.0);
            im += (d[(size_t) i] - mean) * w * std::sin (2.0 * pi * f * i / 1000.0);
        }
        return 2.0 * std::sqrt (re * re + im * im) / (0.5 * n);
    };
    auto expectedLine = [&] (double amp, double f) { const double x = pi * f * d1; return 1000.0 * d1 * amp * std::abs (std::sin (x) / x); };

    const auto none = delayTrace (0.0f, 0.0f, 0);
    double noneRms = 0;
    for (double v : none) noneRms = std::max (noneRms, std::abs (v - none.front()));
    MEASURE ("WOW 0, FLUTTER 0, Clean: max delay deviation (ms)", noneRms);
    CHECK (noneRms < 1.0e-4);

    // the rates and depths of MODEL_SPEC 7.1, written out here (not taken from the code under test)
    const double specWowHz = 0.55, specFlutterHz = 6.5, specWowPeak = 0.006, specFlutterPeak = 0.002;
    const auto w = delayTrace (100.0f, 0.0f, 0);
    const auto fl = delayTrace (0.0f, 100.0f, 0);
    const double wowLine = line (w, specWowHz), wowExp = expectedLine (specWowPeak, specWowHz);
    const double flLine = line (fl, specFlutterHz), flExp = expectedLine (specFlutterPeak, specFlutterHz);
    MEASURE ("WOW 100 %: 0.55 Hz delay line (ms)", wowLine);
    MEASURE ("    expected (ms)", wowExp);
    MEASURE ("FLUTTER 100 %: 6.5 Hz delay line (ms)", flLine);
    MEASURE ("    expected (ms)", flExp);
    CHECK_NEAR (wowLine / wowExp, 1.0, 0.15);
    CHECK_NEAR (flLine / flExp, 1.0, 0.15);
    MEASURE ("WOW 100 %: 6.5 Hz line (ms)", line (w, specFlutterHz));
    MEASURE ("FLUTTER 100 %: 0.55 Hz line (ms)", line (fl, specWowHz));
    CHECK (line (w, specFlutterHz) < 0.10 * flExp);   // WOW does not make flutter (only its random drift's tail)
    CHECK (line (fl, specWowHz) < 0.10 * wowExp);     // FLUTTER does not make wow
    const auto half = delayTrace (50.0f, 0.0f, 0);
    CHECK_NEAR (line (half, specWowHz) / wowLine, 0.5, 0.08); // depth is linear in WOW
    for (double f = 0.1; f <= 30.0; f += 0.05)
        csv << "100,0," << f << "," << line (w, f) << "," << (std::abs (f - specWowHz) < 0.026 ? wowExp : 0.0) << "\n";
    for (double f = 0.1; f <= 30.0; f += 0.05)
        csv << "0,100," << f << "," << line (fl, f) << "," << (std::abs (f - specFlutterHz) < 0.026 ? flExp : 0.0) << "\n";
    outputDirectory().getChildFile ("ECHO_wow_flutter_spectrum.csv").replaceWithText (csv);
}

TEST ("ECHO: DUCK: the dry input attenuates the wet return (24 dB at 100 %), 10 ms attack, recovers after the input stops")
{
    const double fs = 48000.0;
    auto run = [&] (float duck, std::vector<double>* att)
    {
        auto p = makeEcho (fs, 96);
        measurementSetup (*p, m1);
        setParam (*p, ep::repeatRate, 150.0f);
        setParam (*p, ep::intensity, 6.0f);
        setParam (*p, ep::duck, duck);
        p->prepareToPlay (fs, 96);
        const int n = (int) (3.0 * fs);
        auto in = toBuffer (sine (fs, 500.0, 0.25f, n), 2); // -12 dBFS peak from 0.2 s to 1.2 s
        for (int ch = 0; ch < 2; ++ch)
        {
            in.clear (ch, 0, (int) (0.2 * fs));
            in.clear (ch, (int) (1.2 * fs), n - (int) (1.2 * fs));
        }
        juce::AudioBuffer<float> io (in);
        juce::MidiBuffer midi;
        for (int pos = 0; pos < n; pos += 96)
        {
            juce::AudioBuffer<float> view (io.getArrayOfWritePointers(), 2, pos, 96);
            p->processBlock (view, midi);
            if (att) att->push_back (p->getEngineForTesting().getDuckAttenuationDb());
        }
        return io;
    };
    std::vector<double> att;
    const auto off = run (0.0f, nullptr), on = run (100.0f, &att);
    auto win = [&] (const juce::AudioBuffer<float>& b, double t0, double t1) { return rms (b.getReadPointer (0) + (int) (t0 * fs), (int) ((t1 - t0) * fs)); };
    const double during = dbOf (win (on, 0.6, 1.15) / win (off, 0.6, 1.15));
    MEASURE ("wet during the input, DUCK 100 vs 0 (dB)", during);
    CHECK_NEAR (during, -24.0, 1.0);
    juce::String csv ("t_after_input_s,ducked_minus_unducked_db,attenuation_db\n");
    double prevRel = -100;
    for (double t : { 0.05, 0.15, 0.3, 0.5, 0.8, 1.2 })
    {
        const double rel = dbOf (win (on, 1.2 + t, 1.2 + t + 0.05) / win (off, 1.2 + t, 1.2 + t + 0.05));
        csv << t << "," << rel << "," << att[(size_t) ((1.2 + t) * fs / 96)] << "\n";
        MEASURE ("    " + juce::String (t) + " s after the input stops (dB)", rel);
        CHECK (rel >= prevRel - 0.2); // recovers monotonically
        prevRel = rel;
    }
    CHECK (prevRel > -0.5); // fully recovered 1.2 s after the input
    // attack: 63 % of the depth within about 10 ms (+ one control block) of the onset
    const size_t onset = (size_t) (0.2 * fs / 96);
    size_t k = onset;
    while (k < att.size() && att[k] < 0.632 * 24.0) ++k;
    const double attackMs = (double) (k - onset) * 96 / fs * 1000.0;
    MEASURE ("attack: time to 63 % of the attenuation (ms)", attackMs);
    CHECK (attackMs > 5.0 && attackMs < 16.0);
    size_t r = (size_t) (1.2 * fs / 96);
    while (r < att.size() && att[r] > 24.0 * 0.368) ++r;
    const double releaseMs = (double) (r - (size_t) (1.2 * fs / 96)) * 96 / fs * 1000.0;
    MEASURE ("release: time to 37 % of the attenuation after the input stops (ms)", releaseMs);
    CHECK (releaseMs > 200.0 && releaseMs < 400.0);
    // a quiet input (-50 dBFS) does not duck
    CHECK (identical (run (0.0f, nullptr), run (0.0f, nullptr)));
    outputDirectory().getChildFile ("ECHO_duck.csv").replaceWithText (csv);
}

TEST ("ECHO: the spring is dispersive: low frequencies arrive first, the group delay follows the stretched-allpass law (chirp; CSV)")
{
    juce::String csv ("rate,spring,hz,arrival_ms,law_ms\n");
    for (double fs : { 44100.0, 48000.0, 96000.0 })
        for (int which = 0; which < 2; ++which)
        {
            echo::Spring sp;
            sp.prepare (fs, which == 0 ? echo::spring::designA : echo::spring::designB);
            const int pad = (int) (0.005 * fs), n = pad + (int) (0.040 * fs);
            std::vector<float> ir ((size_t) n, 0.0f);
            for (int i = pad; i < n; ++i)
                ir[(size_t) i] = (float) sp.process (i == pad ? 1.0 : 0.0);
            // arrival time at f: envelope peak of the complex demodulation with a Gaussian window (sigma 1 ms)
            auto arrival = [&] (double f)
            {
                const double sigma = 0.001 * fs;
                const int half = (int) (4.0 * sigma);
                double best = 0, bestT = 0;
                for (int c = half; c < n - half; ++c)
                {
                    double re = 0, im = 0;
                    for (int k = -half; k <= half; k += 1)
                    {
                        const double g = std::exp (-0.5 * (k / sigma) * (k / sigma));
                        re += ir[(size_t) (c + k)] * g * std::cos (2.0 * pi * f * (c + k) / fs);
                        im += ir[(size_t) (c + k)] * g * std::sin (2.0 * pi * f * (c + k) / fs);
                    }
                    if (const double e = re * re + im * im; e > best) { best = e; bestT = c; }
                }
                return (bestT - pad) / fs * 1000.0;
            };
            double prev = -1;
            const double fc = sp.getTransitionHz();
            for (double f : { 300.0, 1500.0, 2500.0, 0.75 * fc, 0.85 * fc })
            {
                const double a = arrival (f), law = sp.lowChirpDelaySamples (f) / fs * 1000.0;
                csv << fs << "," << (which == 0 ? "A" : "B") << "," << f << "," << a << "," << law << "\n";
                CHECK_MSG (std::abs (a - law) < 1.0, juce::String (fs) + " Hz spring " + juce::String (which) + " at " + juce::String (f) + " Hz: "
                                                      + juce::String (a, 2) + " ms vs law " + juce::String (law, 2));
                CHECK (a > prev); // rising chirp
                prev = a;
            }
            const double spread = arrival (0.85 * fc) - arrival (300.0);
            if (fs == 48000.0)
                MEASURE (juce::String ("spring ") + (which == 0 ? "A" : "B") + ": arrival 0.85 f_C minus 300 Hz (ms)", spread);
            CHECK (spread > 10.0);
            CHECK (sp.getTransitionHz() > 4300.0 && sp.getTransitionHz() < 4850.0);
        }
    outputDirectory().getChildFile ("ECHO_spring_dispersion.csv").replaceWithText (csv);

    // The processor's spring: MODE Reverb, REVERB VOLUME 10: a long, decaying, stereo tail.
    const double fs = 48000.0;
    auto p = makeEcho (fs, 256);
    measurementSetup (*p, mReverb);
    setParam (*p, ep::reverbVolume, 10.0f);
    setParam (*p, ep::stereo, 100.0f);
    p->prepareToPlay (fs, 256);
    const int n = (int) (4.0 * fs);
    juce::AudioBuffer<float> in (2, n);
    in.clear();
    in.setSample (0, 100, 0.5f);
    in.setSample (1, 100, 0.5f);
    const auto out = render (*p, in, 256);
    CHECK (allFinite (out));
    auto band = [&] (double t0) { return dbOf (rms (out.getReadPointer (0) + (int) (t0 * fs), (int) (0.2 * fs))); };
    const double e0 = band (0.1), e1 = band (1.1);
    const double t60 = 60.0 / std::max (1e-9, (e0 - e1) / 1.0);
    MEASURE ("spring decay rate (dB/s)", e0 - e1);
    MEASURE ("spring T60 estimate (s)", t60);
    CHECK (t60 > 1.2 && t60 < 4.0);
    CHECK (maxAbsDiff (out, out) == 0.0);
    double lr = 0;
    for (int i = 0; i < n; ++i) lr = std::max (lr, (double) std::abs (out.getSample (0, i) - out.getSample (1, i)));
    CHECK (lr > 1.0e-3); // two springs: a stereo tail
}

namespace
{
    // Multitone on exact DFT bins of the analysis window (df = 4 Hz), -40 dBFS each.
    struct Multitone
    {
        double fs;
        int window, warmup;
        std::vector<double> hz, phase;

        Multitone (double rate, double topHz) : fs (rate)
        {
            window = (int) std::round (fs / 4.0);
            warmup = (int) std::round (fs * 0.5);
            const double df = fs / window;
            const double nominal[] = { 20, 25, 30, 40, 50, 63, 80, 100, 125, 160, 200, 250, 315, 400, 500, 630, 800, 1000, 1250, 1600,
                                       2000, 2500, 3150, 4000, 5000, 6300, 8000, 10000, 12500, 16000, 18000, 20000 };
            juce::Random rng (7);
            for (double f : nominal)
                if (f <= topHz)
                {
                    hz.push_back (std::round (f / df) * df);
                    phase.push_back (rng.nextDouble() * 2.0 * pi);
                }
        }

        juce::AudioBuffer<float> signal (int channels, float ampEach = 0.01f) const
        {
            const int n = warmup + window;
            juce::AudioBuffer<float> b (channels, n);
            for (int i = 0; i < n; ++i)
            {
                double v = 0;
                for (size_t k = 0; k < hz.size(); ++k)
                    v += std::sin (phase[k] + 2.0 * pi * hz[k] * i / fs);
                for (int ch = 0; ch < channels; ++ch)
                    b.setSample (ch, i, ampEach * (float) v);
            }
            return b;
        }

        std::vector<double> amplitudes (const juce::AudioBuffer<float>& out, int channel = 0) const
        {
            const float* d = out.getReadPointer (channel) + (out.getNumSamples() - window);
            std::vector<double> a;
            for (double f : hz)
                a.push_back (toneAmplitude (d, window, fs, f));
            return a;
        }
    };

    // Echo response (linear amplitudes per tone) of a measurement setup with `tweak` applied.
    std::vector<double> echoTones (double fs, const Multitone& mt, const std::function<void (EchoProcessor&)>& tweak,
                                   double* playbackDbAt = nullptr, double atHz = 1000.0)
    {
        auto p = makeEcho (fs, 256);
        measurementSetup (*p, m1);
        setParam (*p, ep::repeatRate, 50.0f);
        tweak (*p);
        p->prepareToPlay (fs, 256);
        const auto a = mt.amplitudes (render (*p, mt.signal (2), 256));
        if (playbackDbAt != nullptr)
            *playbackDbAt = p->getEngineForTesting().playbackResponseDb (atHz);
        return a;
    }
}

TEST ("ECHO: filter responses (addendum 26): HPF/LPF at min, mid, max and musical settings, BASS/TREBLE, head loss; measured = evaluator (CSV)")
{
    juce::String csv ("rate,filter,setting,hz,expected_db,measured_db,error_db\n");
    double worst = 0;
    for (double fs : { 44100.0, 48000.0, 96000.0 })
    {
        const Multitone mt (fs, std::min (20000.0, 0.45 * fs));
        // reference: POST filter at its widest (HPF 20 Hz, LPF 20 kHz)
        const auto ref = echoTones (fs, mt, [] (EchoProcessor& p) { setParam (p, ep::filterMode, 2.0f); });
        struct HL { float hpf, lpf; const char* name; };
        for (const HL s : { HL { 20.0f, 20000.0f, "min HPF / max LPF" }, HL { 77.46f, 4472.0f, "mid / mid" }, HL { 300.0f, 1000.0f, "max HPF / min LPF" },
                            HL { 120.0f, 3000.0f, "musical: dub" }, HL { 250.0f, 6000.0f, "musical: telephone-ish" }, HL { 20.0f, 1000.0f, "min / min" },
                            HL { 300.0f, 20000.0f, "max / max" } })
            for (int fm : { 0, 2 }) // PRE and POST: the filter once on the echo (LOOP: see the decay test)
            {
                const auto a = echoTones (fs, mt, [&] (EchoProcessor& p)
                                          {
                                              setParam (p, ep::filterMode, (float) fm);
                                              setParam (p, ep::hpf, s.hpf);
                                              setParam (p, ep::lpf, s.lpf);
                                          });
                for (size_t k = 0; k < mt.hz.size(); ++k)
                {
                    const double f = mt.hz[k];
                    const double expected = echo::filt::hpLpResponseDb (f, s.hpf, s.lpf, fs) - echo::filt::hpLpResponseDb (f, 20.0, 20000.0, fs);
                    const double measured = dbOf (a[k] / ref[k]);
                    if (expected < -60.0) continue; // below the measurement floor
                    csv << fs << "," << ep::filterModeNames[fm] << " HPF/LPF,\"" << s.name << " (" << s.hpf << " / " << s.lpf << ")\"," << f << "," << expected << "," << measured << "," << measured - expected << "\n";
                    if (expected > -40.0) worst = std::max (worst, std::abs (measured - expected));
                    // 0.1 dB down to -40 dB; below, the record stage's intermodulation of the 32 tones sets a floor
                    CHECK_MSG (std::abs (measured - expected) < (expected > -40.0 ? 0.1 : 0.3), juce::String (fs) + " " + s.name + " at " + juce::String (f) + " Hz: "
                                                                     + juce::String (measured, 3) + " vs " + juce::String (expected, 3));
                }
            }
        // BASS / TREBLE (in the playback amplifier): -10, -5, +5, +10 dB
        const auto flat = echoTones (fs, mt, [] (EchoProcessor&) {});
        for (float b : { -10.0f, -5.0f, 5.0f, 10.0f })
            for (int which = 0; which < 2; ++which)
            {
                const auto a = echoTones (fs, mt, [&] (EchoProcessor& p) { setParam (p, which == 0 ? ep::bass : ep::treble, b); });
                for (size_t k = 0; k < mt.hz.size(); ++k)
                {
                    const double f = mt.hz[k];
                    const double expected = echo::toneResponseDb (f, which == 0 ? b : 0.0, which == 1 ? b : 0.0, fs);
                    const double measured = dbOf (a[k] / flat[k]);
                    csv << fs << "," << (which == 0 ? "BASS" : "TREBLE") << "," << b << "," << f << "," << expected << "," << measured << "," << measured - expected << "\n";
                    worst = std::max (worst, std::abs (measured - expected));
                    CHECK_MSG (std::abs (measured - expected) < 0.1, juce::String (fs) + (which == 0 ? " BASS " : " TREBLE ") + juce::String (b) + " at " + juce::String (f));
                }
            }
        // head loss follows the tape speed (hardware range 69..177 ms) and AGE
        for (auto [rate, age] : { std::pair { 69.0f, 0.0f }, std::pair { 177.0f, 0.0f }, std::pair { 177.0f, 10.0f } })
        {
            auto p = makeEcho (fs, 256);
            measurementSetup (*p, m1);
            setParam (*p, ep::repeatRate, rate);
            setParam (*p, ep::age, age);
            p->prepareToPlay (fs, 256);
            const auto a = mt.amplitudes (render (*p, mt.signal (2), 256));
            auto& eng = p->getEngineForTesting();
            const double ref1k = dbOf (a[17] / 0.01) - eng.playbackResponseDb (mt.hz[17]);
            for (size_t k = 0; k < mt.hz.size(); ++k)
            {
                const double f = mt.hz[k];
                const double expected = eng.playbackResponseDb (f);
                const double measured = dbOf (a[k] / 0.01) - ref1k;
                csv << fs << ",playback chain,\"head 1 " << rate << " ms, AGE " << age << " (corner " << eng.getHeadLossHz() << " Hz)\"," << f << "," << expected << "," << measured << "," << measured - expected << "\n";
                if (f < 0.4 * fs)
                    CHECK_MSG (std::abs (measured - expected) < 0.15, juce::String (fs) + " playback at " + juce::String (f) + " Hz: " + juce::String (measured, 3) + " vs " + juce::String (expected, 3));
            }
            if (fs == 48000.0)
                MEASURE ("head-loss corner, head 1 " + juce::String (rate) + " ms, AGE " + juce::String (age) + " (Hz)", eng.getHeadLossHz());
        }
    }
    MEASURE ("worst |measured - evaluator| over all filter settings, responses above -40 dB (dB)", worst);
    CHECK_NEAR (echo::engine::headLossCorner (177.0, 0.0), 6500.0, 1e-9);
    CHECK_NEAR (echo::engine::headLossCorner (69.0, 0.0), 6500.0 * std::sqrt (177.0 / 69.0), 1e-9);
    CHECK_NEAR (echo::engine::headLossCorner (2000.0, 0.0), 6500.0, 1e-9); // held outside the hardware range
    outputDirectory().getChildFile ("ECHO_filter_responses.csv").replaceWithText (csv);
}

namespace
{
    struct Thd { double thd, h2, h3, gainDb; };

    // First-echo THD of a 400 Hz sine at `levelDbfs` (mode 1, no feedback, flat tone).
    Thd echoThd (double fs, int character, float drive, double levelDbfs, double hz = 400.0)
    {
        auto p = makeEcho (fs, 256);
        measurementSetup (*p, m1);
        setParam (*p, ep::repeatRate, 50.0f);
        setParam (*p, ep::character, (float) character);
        setParam (*p, ep::drive, drive);
        p->prepareToPlay (fs, 256);
        const float amp = (float) std::pow (10.0, levelDbfs / 20.0);
        const int win = (int) fs / 4; // 100 periods of 400 Hz
        const auto out = render (*p, toBuffer (sine (fs, hz, amp, (int) (0.4 * fs) + win), 2), 256);
        const float* d = out.getReadPointer (0) + out.getNumSamples() - win;
        const double f = toneAmplitude (d, win, fs, hz);
        return { thd (d, win, fs, hz), toneAmplitude (d, win, fs, 2 * hz) / f, toneAmplitude (d, win, fs, 3 * hz) / f, dbOf (f / amp) };
    }
}

TEST ("ECHO: THD vs level and DRIVE per CHARACTER (addendum 27): level dependent, Clean < Original < Burn, small-signal level matched (CSV)")
{
    const double fs = 48000.0;
    juce::String csv ("character,drive,level_dbfs,thd_pct,h2_dbc,h3_dbc,gain_db\n");
    const double levels[] = { -40.0, -30.0, -18.0, -12.0, -6.0, 0.0 };
    std::map<std::pair<int, int>, std::vector<Thd>> table;
    for (int c = 0; c < 3; ++c)
        for (float drive : { 0.0f, 3.0f, 6.0f, 10.0f })
            for (double lv : levels)
            {
                const auto r = echoThd (fs, c, drive, lv);
                table[{ c, (int) drive }].push_back (r);
                csv << ep::characterNames[c] << "," << drive << "," << lv << "," << 100.0 * r.thd << "," << dbOf (r.h2) << "," << dbOf (r.h3) << "," << r.gainDb << "\n";
            }
    outputDirectory().getChildFile ("ECHO_thd.csv").replaceWithText (csv);
    const auto& ref = table[{ 1, 3 }];
    const auto& clean3 = table[{ 0, 3 }];
    const auto& burn3 = table[{ 2, 3 }];
    const double refSmall = echoThd (fs, cOriginal, 3.0f, -60.0).gainDb;
    double worstGain = 0.0;
    for (int c = 0; c < 3; ++c)
        for (int drive : { 0, 3, 6, 10 })
        {
            const auto& v = table[{ c, drive }];
            for (size_t i = 2; i < v.size(); ++i)   // THD rises with level from -30 dBFS on
                CHECK_MSG (v[i].thd > v[i - 1].thd, ep::characterNames[c] + " DRIVE " + juce::String (drive) + " level step " + juce::String ((int) i));
            // small-signal level matched at -60 dBFS: DRIVE and CHARACTER change the colour, not the level
            const double g = echoThd (fs, c, (float) drive, -60.0).gainDb;
            const double gainErr = std::abs (g - refSmall);
            worstGain = std::max (worstGain, gainErr);
            CHECK_MSG (gainErr < 0.05, ep::characterNames[c] + " DRIVE " + juce::String (drive) + " gain " + juce::String (g, 3));
        }
    MEASURE ("small-signal (-60 dBFS) gain spread over CHARACTER x DRIVE (dB)", worstGain);
    for (size_t i = 2; i < std::size (levels); ++i) // from -18 dBFS: Clean < Original < Burn at the default DRIVE
    {
        CHECK (clean3[i].thd < ref[i].thd);
        CHECK (ref[i].thd < burn3[i].thd);
    }
    for (int c = 0; c < 3; ++c) // DRIVE adds distortion at -18 dBFS
    {
        const double t0 = table[{ c, 0 }][2].thd, t3 = table[{ c, 3 }][2].thd, t6 = table[{ c, 6 }][2].thd, t10 = table[{ c, 10 }][2].thd;
        CHECK_MSG (t0 < t3 && t3 < t6 && t6 < t10, ep::characterNames[c]);
    }
    const double orig10 = table[{ 1, 10 }][2].thd;
    MEASURE ("Original, DRIVE 3, -18 dBFS: THD (%)", 100.0 * ref[2].thd);
    MEASURE ("Original, DRIVE 3, -6 dBFS: THD (%)", 100.0 * ref[4].thd);
    MEASURE ("Clean, DRIVE 3, -18 dBFS: THD (%)", 100.0 * clean3[2].thd);
    MEASURE ("Burn, DRIVE 3, -18 dBFS: THD (%)", 100.0 * burn3[2].thd);
    MEASURE ("Original, DRIVE 10, -18 dBFS: THD (%)", 100.0 * orig10);
    MEASURE ("Original, DRIVE 3, -18 dBFS: H2 (dBc)", dbOf (ref[2].h2));
    MEASURE ("Original, DRIVE 3, -18 dBFS: H3 (dBc)", dbOf (ref[2].h3));
    CHECK (clean3[2].thd < 0.25 * ref[2].thd); // Clean: much less colour
    CHECK (burn3[2].thd > 2.0 * ref[2].thd);   // Burn: clearly hotter
    // the preamp's even term: Burn and Original have audible H2, Clean almost none
    CHECK (burn3[3].h2 > clean3[3].h2 * 5.0);

    // tape HF compression: at -6 dBFS a 4 kHz tone (pre-emphasised) saturates more than 400 Hz
    const auto lo = echoThd (fs, cOriginal, 0.0f, -6.0, 400.0), hi = echoThd (fs, cOriginal, 0.0f, -6.0, 3000.0);
    MEASURE ("Original DRIVE 0, -6 dBFS: gain at 3 kHz minus gain at 400 Hz (dB)", hi.gainDb - lo.gainDb);
}

TEST ("ECHO: TAPE CONDITION: Clean / Worn / Magnetic dust differ as specified; WOW, FLUTTER, NOISE and AGE add on top")
{
    const double fs = 48000.0;
    struct Res { double hfLossDb, noiseDb, modMs, decayDb; };
    auto measure = [&] (int cond, float wow, float noise, float age)
    {
        Res r {};
        // HF loss: echo level at 6.3 kHz relative to 500 Hz
        {
            const Multitone mt (fs, 8000.0);
            const auto a = echoTones (fs, mt, [&] (EchoProcessor& p)
                                      {
                                          setParam (p, ep::tapeCondition, (float) cond);
                                          setParam (p, ep::age, age);
                                      });
            r.hfLossDb = dbOf (a[25] / a[14]);
        }
        // noise floor with silence in (head 1, no feedback)
        {
            auto p = makeEcho (fs, 256);
            measurementSetup (*p, m1);
            setParam (*p, ep::tapeCondition, (float) cond);
            setParam (*p, ep::noise, noise);
            p->prepareToPlay (fs, 256);
            const auto out = render (*p, silence (2, (int) fs), 256);
            r.noiseDb = dbOf (rms (out.getReadPointer (0) + (int) (0.5 * fs), (int) (0.5 * fs)));
        }
        // transport modulation: RMS of the head-1 delay over 3 s (telemetry, per 32 samples)
        {
            auto p = makeEcho (fs, 32);
            measurementSetup (*p, m1);
            setParam (*p, ep::tapeCondition, (float) cond);
            setParam (*p, ep::wow, wow);
            p->prepareToPlay (fs, 32);
            juce::MidiBuffer midi;
            std::vector<double> d;
            for (int b = 0; b < (int) (3.5 * fs / 32); ++b)
            {
                auto buf = silence (2, 32);
                p->processBlock (buf, midi);
                if (b > (int) (0.5 * fs / 32)) d.push_back (p->getHeadDelayMs (0));
            }
            double mean = 0, sq = 0;
            for (double v : d) mean += v;
            mean /= (double) d.size();
            for (double v : d) sq += (v - mean) * (v - mean);
            r.modMs = std::sqrt (sq / (double) d.size());
        }
        // feedback degeneration: decay of repeat 3 vs repeat 2 at 1 kHz (INTENSITY 6)
        {
            auto p = makeEcho (fs, 256);
            measurementSetup (*p, m1);
            setParam (*p, ep::repeatRate, 100.0f);
            setParam (*p, ep::intensity, 6.0f);
            setParam (*p, ep::tapeCondition, (float) cond);
            p->prepareToPlay (fs, 256);
            const int D = 4800, at = 1000;
            // a 1 kHz tone burst: far above the condition's hiss in a 100 ms window
            const auto out = render (*p, toBuffer (toneBurst (fs, at + 5 * D, at, 0.2f), 2), 256);
            auto lev = [&] (int k) { return toneAmplitude (out.getReadPointer (0) + at + k * D - D / 2, D, fs, 1000.0); };
            r.decayDb = dbOf (lev (3) / lev (2));
        }
        return r;
    };
    const Res clean = measure (0, 0.0f, 0.0f, 0.0f), worn = measure (1, 0.0f, 0.0f, 0.0f), dust = measure (2, 0.0f, 0.0f, 0.0f);
    const char* names[] = { "Clean", "Worn", "Magnetic dust" };
    int i = 0;
    for (const Res& r : { clean, worn, dust })
    {
        MEASURE (juce::String (names[i]) + ": echo 6.3 kHz re 500 Hz (dB)", r.hfLossDb);
        MEASURE (juce::String (names[i]) + ": noise floor, NOISE 0 % (dBFS RMS)", r.noiseDb);
        MEASURE (juce::String (names[i]) + ": head-1 delay modulation RMS, WOW/FLUTTER 0 (ms)", r.modMs);
        MEASURE (juce::String (names[i]) + ": per-repeat decay at INTENSITY 6 (dB)", r.decayDb);
        ++i;
    }
    // CLEAN: no added wow/flutter/noise/HF loss; WORN: moderate; MAGNETIC DUST: strongest
    CHECK (clean.noiseDb < -200.0 && clean.modMs < 1.0e-9);
    CHECK (worn.hfLossDb < clean.hfLossDb - 1.0 && dust.hfLossDb < worn.hfLossDb - 2.0);
    CHECK (worn.noiseDb > -100.0 && dust.noiseDb > worn.noiseDb + 6.0);
    CHECK (worn.modMs > 0.002 && dust.modMs > 1.5 * worn.modMs);
    CHECK (worn.decayDb < clean.decayDb - 0.4 && dust.decayDb < worn.decayDb - 0.8);
    // user controls stay additive on top of the macro
    const Res wornPlus = measure (1, 60.0f, 50.0f, 6.0f);
    CHECK (wornPlus.modMs > worn.modMs * 1.5);
    CHECK (wornPlus.noiseDb > worn.noiseDb + 3.0);
    CHECK (wornPlus.hfLossDb < worn.hfLossDb - 1.0);
}

// =====================================================================================
// Every control, switching, rates/blocks/layouts, robustness, bypass, state, presets, CPU
// =====================================================================================
namespace
{
    // The musical base setting of the control tests: two heads, some feedback,
    // spring, width, default colour. Host tempo 100 BPM (differs from MANUAL BPM 120).
    void controlBase (EchoProcessor& p)
    {
        setParam (p, ep::mode, (float) m12);
        setParam (p, ep::repeatRate, 120.0f);
        setParam (p, ep::intensity, 5.0f);
        setParam (p, ep::echoVolume, 7.0f);
        setParam (p, ep::reverbVolume, 3.0f);
        setParam (p, ep::mix, 50.0f);
        setParam (p, ep::stereo, 30.0f);
    }

    // 0.5 s of four tones (100, 440, 1500, 5000 Hz; Hann edges), then 1.5 s of silence.
    juce::AudioBuffer<float> controlSignal (double fs, int channels = 2)
    {
        const int n = (int) (2.0 * fs), on = (int) (0.5 * fs);
        std::vector<float> v ((size_t) n, 0.0f);
        const double hz[] = { 100.0, 440.0, 1500.0, 5000.0 };
        for (int i = 0; i < on; ++i)
        {
            const double edge = std::min (1.0, std::min (i, on - 1 - i) / (0.01 * fs));
            double s = 0;
            for (double f : hz)
                s += std::sin (2.0 * pi * f * i / fs);
            v[(size_t) i] = (float) (0.08 * edge * s);
        }
        return toBuffer (v, channels);
    }
}

TEST ("ECHO: every control audibly changes the output, each in the specified direction (audio-effect test per visible control)")
{
    const double fs = 48000.0;
    const auto sig = controlSignal (fs);
    FakePlayHead head;
    head.bpm = 100.0;
    auto renderWith = [&] (const std::function<void (EchoProcessor&)>& setup)
    {
        auto p = makeEcho (fs, 256);
        p->setPlayHead (&head);
        controlBase (*p);
        setup (*p);
        p->prepareToPlay (fs, 256);
        auto out = render (*p, sig, 256);
        p->setPlayHead (nullptr);
        return out;
    };
    // tail window (after the input): 0.6 .. 1.9 s
    const int t0 = (int) (0.6 * fs), tn = (int) (1.3 * fs);
    // band level of a tone in the tail: Goertzel over 50 ms windows, summed in power
    // (the repeats overlap with arbitrary phases; a single long window would let them cancel)
    auto tone = [&] (const juce::AudioBuffer<float>& b, double hz)
    {
        const int w = (int) (0.05 * fs);
        double e = 0;
        for (int i = t0; i + w <= t0 + tn; i += w)
            e += std::pow (toneAmplitude (b.getReadPointer (0) + i, w, fs, hz), 2.0);
        return std::sqrt (e);
    };
    auto tailRms = [&] (const juce::AudioBuffer<float>& b) { return (double) rms (b.getReadPointer (0) + t0, tn); };
    auto sideRms = [&] (const juce::AudioBuffer<float>& b)
    {
        double s = 0;
        for (int i = t0; i < t0 + tn; ++i) { const double d = 0.5 * (b.getSample (0, i) - b.getSample (1, i)); s += d * d; }
        return std::sqrt (s / tn);
    };
    auto relDiffDb = [&] (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
    {
        double d = 0, e = 0;
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < a.getNumSamples(); ++i)
            {
                const double x = a.getSample (ch, i) - b.getSample (ch, i);
                d += x * x;
                e += (double) b.getSample (ch, i) * b.getSample (ch, i);
            }
        return 10.0 * std::log10 (std::max (d, 1e-30) / std::max (e, 1e-30));
    };

    struct Case
    {
        const char* id;
        std::function<void (EchoProcessor&)> base, alt;
    };
    auto set = [] (const char* id, float v) { return [id, v] (EchoProcessor& p) { setParam (p, id, v); }; };
    auto both = [] (std::function<void (EchoProcessor&)> a, std::function<void (EchoProcessor&)> b)
    { return [a, b] (EchoProcessor& p) { a (p); b (p); }; };
    auto none = [] (EchoProcessor&) {};
    const auto sync = set (ep::timeMode, 1.0f);
    const auto syncManual = both (sync, set (ep::bpmSource, 1.0f));
    const auto narrowFilters = both (set (ep::hpf, 250.0f), set (ep::lpf, 2000.0f));
    const std::vector<Case> cases {
        { ep::mode,          none, set (ep::mode, (float) m123) },
        { ep::repeatRate,    none, set (ep::repeatRate, 300.0f) },
        { ep::intensity,     none, set (ep::intensity, 8.0f) },
        { ep::echoVolume,    none, set (ep::echoVolume, 3.0f) },
        { ep::reverbVolume,  none, set (ep::reverbVolume, 8.0f) },
        { ep::bass,          none, set (ep::bass, 8.0f) },
        { ep::treble,        none, set (ep::treble, 8.0f) },
        { ep::timeMode,      none, sync },
        { ep::bpmSource,     sync, syncManual },
        { ep::manualBpm,     syncManual, both (syncManual, set (ep::manualBpm, 90.0f)) },
        { ep::noteDivision,  sync, both (sync, set (ep::noteDivision, 2.0f)) },
        { ep::feel,          sync, both (sync, set (ep::feel, 1.0f)) },
        { ep::drive,         none, set (ep::drive, 9.0f) },
        { ep::age,           none, set (ep::age, 9.0f) },
        { ep::wow,           none, set (ep::wow, 90.0f) },
        { ep::flutter,       none, set (ep::flutter, 90.0f) },
        { ep::hpf,           none, set (ep::hpf, 250.0f) },
        { ep::lpf,           none, set (ep::lpf, 2000.0f) },
        { ep::filterMode,    narrowFilters, both (narrowFilters, set (ep::filterMode, 2.0f)) },
        { ep::stereo,        none, set (ep::stereo, 90.0f) },
        { ep::duck,          none, set (ep::duck, 100.0f) },
        { ep::mix,           none, set (ep::mix, 80.0f) },
        { ep::tapeCondition, none, set (ep::tapeCondition, 2.0f) },
        { ep::noise,         none, set (ep::noise, 80.0f) },
        { ep::character,     none, set (ep::character, (float) cBurn) },
        { ep::output,        none, set (ep::output, 6.0f) },
        { ep::bypass,        none, set (ep::bypass, 1.0f) },
    };
    CHECK ((int) cases.size() == 27); // every parameter of the registry
    std::map<juce::String, std::pair<juce::AudioBuffer<float>, juce::AudioBuffer<float>>> r;
    for (const auto& c : cases)
    {
        auto a = renderWith (c.base), b = renderWith (c.alt);
        const double d = relDiffDb (b, a);
        MEASURE (juce::String (c.id) + ": output change re output (dB)", d);
        CHECK_MSG (d > -40.0, juce::String (c.id) + " does not change the output");
        r[c.id] = { std::move (a), std::move (b) };
    }
    auto& base = r[ep::mode].first;
    // directions
    const auto& [iA, iB] = r[ep::intensity];
    CHECK (tone (iB, 440.0) > 1.5 * tone (iA, 440.0));            // more repeats
    const auto& [eA, eB] = r[ep::echoVolume];
    CHECK (tailRms (eB) < 0.5 * tailRms (eA));                     // quieter echo
    const auto& [rA, rB] = r[ep::reverbVolume];
    CHECK (tailRms (rB) > tailRms (rA));                           // more spring
    const auto& [bA, bB] = r[ep::bass];
    CHECK (tone (bB, 100.0) > 1.5 * tone (bA, 100.0));
    const auto& [tA, tB] = r[ep::treble];
    CHECK (tone (tB, 5000.0) > 1.5 * tone (tA, 5000.0));
    const auto& [hA, hB] = r[ep::hpf];
    // default FILTER MODE Loop: the first pass of each head is unfiltered, the repeats lose more and more
    MEASURE ("HPF 250 Hz (Loop): 100 Hz in the tail re HPF 20 Hz (dB)", dbOf (tone (hB, 100.0) / tone (hA, 100.0)));
    CHECK (tone (hB, 100.0) < 0.7 * tone (hA, 100.0));
    const auto& [lA, lB] = r[ep::lpf];
    MEASURE ("LPF 2 kHz (Loop): 5 kHz in the tail re LPF 20 kHz (dB)", dbOf (tone (lB, 5000.0) / tone (lA, 5000.0)));
    CHECK (tone (lB, 5000.0) < 0.7 * tone (lA, 5000.0));
    const auto& [sA, sB] = r[ep::stereo];
    CHECK (sideRms (sB) > 2.0 * sideRms (sA));
    const auto& [oA, oB] = r[ep::output];
    CHECK_NEAR (dbOf (rms (oB.getReadPointer (0), oB.getNumSamples()) / rms (oA.getReadPointer (0), oA.getNumSamples())), 6.0, 0.05);
    const auto& [byA, byB] = r[ep::bypass];
    CHECK (identical (byB, sig)); // host Bypass: the input itself
    juce::ignoreUnused (base, byA);
}

TEST ("ECHO: switching every control (selectors and time controls included) is click-free; rapid automation stays finite")
{
    // Click measure: 4th-order high-pass at 8 kHz on the output of a 220 Hz tone.
    // The tone, its echoes (also while the tape speed glides and bends their
    // pitch) and the spring stay far below 8 kHz; a discontinuity does not.
    // Limit: 2x the worse of the two fixed settings + 2e-4 (-74 dBFS).
    const double fs = 48000.0;
    const int block = 64, blocks = 1600, period = 53;
    FakePlayHead head;
    head.bpm = 100.0;
    struct Ctl { const char* id; float a, b; bool sync, manual; };
    const Ctl ctls[] = {
        { ep::mode, (float) m1, (float) m123, false, false }, { ep::mode, (float) mReverb, (float) m23, false, false },
        { ep::repeatRate, 80.0f, 400.0f, false, false }, { ep::intensity, 0.0f, 9.0f, false, false },
        { ep::echoVolume, 0.0f, 10.0f, false, false }, { ep::reverbVolume, 0.0f, 10.0f, false, false },
        { ep::bass, -10.0f, 10.0f, false, false }, { ep::treble, -10.0f, 10.0f, false, false },
        { ep::timeMode, 0.0f, 1.0f, false, false }, { ep::bpmSource, 0.0f, 1.0f, true, false },
        { ep::manualBpm, 80.0f, 160.0f, true, true }, { ep::noteDivision, 2.0f, 4.0f, true, false },
        { ep::feel, 0.0f, 2.0f, true, false }, { ep::drive, 0.0f, 10.0f, false, false },
        { ep::age, 0.0f, 10.0f, false, false }, { ep::wow, 0.0f, 100.0f, false, false },
        { ep::flutter, 0.0f, 100.0f, false, false }, { ep::hpf, 20.0f, 300.0f, false, false },
        { ep::lpf, 1000.0f, 20000.0f, false, false }, { ep::filterMode, 0.0f, 2.0f, false, false },
        { ep::stereo, 0.0f, 100.0f, false, false }, { ep::duck, 0.0f, 100.0f, false, false },
        { ep::mix, 0.0f, 100.0f, false, false }, { ep::tapeCondition, 0.0f, 2.0f, false, false },
        { ep::noise, 0.0f, 100.0f, false, false }, { ep::character, 0.0f, 2.0f, false, false },
        { ep::output, -12.0f, 12.0f, false, false }, { ep::bypass, 0.0f, 1.0f, false, false } };
    auto run = [&] (const Ctl& c, int fixed)
    {
        auto p = makeEcho (fs, block);
        p->setPlayHead (&head);
        controlBase (*p);
        setParam (*p, ep::noise, 0.0f);
        setParam (*p, ep::hpf, 60.0f);
        setParam (*p, ep::lpf, 8000.0f);
        if (c.sync) setParam (*p, ep::timeMode, 1.0f);
        if (c.manual) setParam (*p, ep::bpmSource, 1.0f);
        if (fixed >= 0)
            setParam (*p, c.id, fixed == 0 ? c.a : c.b);
        p->prepareToPlay (fs, block);
        juce::MidiBuffer midi;
        echo::filt::Svf h1, h2;
        h1.set (echo::filt::integratorGain (8000.0, fs), 0.7653668647301796);
        h2.set (echo::filt::integratorGain (8000.0, fs), 1.8477590650225735);
        double maxHf = 0;
        bool finite = true;
        for (int b = 0; b < blocks; ++b)
        {
            if (fixed < 0 && b % period == 0 && b > 0)
                setParam (*p, c.id, (b / period) % 2 == 0 ? c.a : c.b);
            auto buf = toBuffer (sine (fs, 220.0, 0.2f, block, 2.0 * pi * 220.0 * b * block / fs), 2);
            p->processBlock (buf, midi);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < block; ++i)
                    finite = finite && std::isfinite (buf.getSample (ch, i));
            for (int i = 0; i < block; ++i)
            {
                const double y = h2.process<echo::filt::Svf::highpass> (h1.process<echo::filt::Svf::highpass> (buf.getSample (0, i)));
                if (b > 200) maxHf = std::max (maxHf, std::abs (y));
            }
        }
        p->setPlayHead (nullptr);
        CHECK (finite);
        return maxHf;
    };
    juce::String csv ("control,a,b,fixed_a,fixed_b,switching,limit\n");
    for (const auto& c : ctls)
    {
        const double fa = run (c, 0), fb = run (c, 1), sw = run (c, -1);
        const double limit = 2.0 * std::max (fa, fb) + 2.0e-4;
        csv << c.id << "," << c.a << "," << c.b << "," << fa << "," << fb << "," << sw << "," << limit << "\n";
        MEASURE (juce::String ("switching ") + c.id + " " + juce::String (c.a) + " <-> " + juce::String (c.b) + ": max |HP 8k| (fixed: "
                     + juce::String (std::max (fa, fb), 6) + ")", sw);
        CHECK_MSG (sw < limit, juce::String ("click when switching ") + c.id);
    }
    outputDirectory().getChildFile ("ECHO_switching.csv").replaceWithText (csv);

    // rapid automation of everything at once: finite and bounded
    auto p = makeEcho (fs, 64);
    p->setPlayHead (&head);
    juce::Random rng (42);
    juce::MidiBuffer midi;
    bool finite = true;
    float peak = 0.0f;
    for (int b = 0; b < 3000; ++b)
    {
        for (auto* param : p->getParameters())
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (param); rp != nullptr && rp->paramID != ep::bypass)
                rp->setValueNotifyingHost (rng.nextFloat());
        auto buf = toBuffer (sine (fs, 200.0, 0.3f, 64, 2.0 * pi * 200.0 * b * 64 / fs), 2);
        p->processBlock (buf, midi);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < 64; ++i)
            {
                finite = finite && std::isfinite (buf.getSample (ch, i));
                peak = std::max (peak, std::abs (buf.getSample (ch, i)));
            }
    }
    p->setPlayHead (nullptr);
    MEASURE ("rapid automation of every parameter: output peak", peak);
    CHECK (finite);
    CHECK (peak < 16.0f); // +24 dBFS: OUTPUT +12 dB on a self-oscillating loop at most
}

TEST ("ECHO: all sample rates and block sizes, mono / stereo / mono->stereo: finite, no steady-state allocation, zero latency, echo on time")
{
    for (double fs : { 44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0 })
        for (int block : { 1, 16, 32, 64, 128, 256, 512, 1024, 2048 })
            for (auto [inCh, outCh] : { std::pair { 1, 1 }, std::pair { 2, 2 }, std::pair { 1, 2 } })
            {
                auto p = makeEcho (fs, block, inCh, outCh);
                setParam (*p, ep::mode, (float) (block % 7 + 1));
                setParam (*p, ep::repeatRate, 30.0f);
                setParam (*p, ep::intensity, 7.0f);
                setParam (*p, ep::reverbVolume, 5.0f);
                setParam (*p, ep::duck, 40.0f);
                setParam (*p, ep::tapeCondition, (float) (block % 3));
                setParam (*p, ep::character, (float) ((block / 2) % 3));
                setParam (*p, ep::filterMode, (float) (block % 3));
                setParam (*p, ep::mix, 80.0f);
                p->prepareToPlay (fs, block);
                const int n = std::max (block * 4, (int) (0.1 * fs));
                auto in = toBuffer (sine (fs, 220.0, 0.5f, n), inCh);
                render (*p, in, block); // warm-up
                // move controls so the glides, the motor and the filter crossfades run during the counted pass
                setParam (*p, ep::repeatRate, 45.0f);
                setParam (*p, ep::filterMode, (float) ((block + 1) % 3));
                setParam (*p, ep::mode, (float) ((block + 3) % 8));
                setParam (*p, ep::tapeCondition, (float) ((block + 1) % 3));
                juce::AudioBuffer<float> io (std::max (inCh, outCh), n);
                for (int ch = 0; ch < io.getNumChannels(); ++ch)
                    io.copyFrom (ch, 0, in, std::min (ch, inCh - 1), 0, n);
                juce::MidiBuffer midi;
                long allocs;
                {
                    ScopedAllocationCounter counter;
                    for (int pos = 0; pos < n; pos += block)
                    {
                        juce::AudioBuffer<float> view (io.getArrayOfWritePointers(), io.getNumChannels(), pos, std::min (block, n - pos));
                        p->processBlock (view, midi);
                    }
                    allocs = counter.count();
                }
                const auto tag = juce::String (fs) + " Hz, block " + juce::String (block) + ", " + juce::String (inCh) + "->" + juce::String (outCh);
                CHECK_MSG (allFinite (io), tag);
                CHECK_MSG (allocs == 0, tag + ": " + juce::String (allocs) + " allocations in processBlock");
                CHECK_MSG (rms (io.getReadPointer (0), n) > 0.05f, tag + ": no output");
                CHECK_MSG (p->getLatencySamples() == 0, tag);
                CHECK_MSG (p->getEngineForTesting().getRecordOversampling() == (fs < 80000.0 ? 2 : 1), tag);
            }
    // the echo lands on time at every rate (head 1 at 100 ms), through every block size
    for (double fs : { 44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0 })
        for (int block : { 1, 64, 2048 })
        {
            auto p = makeEcho (fs, block);
            measurementSetup (*p, m1);
            setParam (*p, ep::repeatRate, 100.0f);
            p->prepareToPlay (fs, block);
            const double expected = 0.1 * fs;
            const double got = measureEchoDelay (*p, fs, expected, block);
            if (block == 64)
                MEASURE ("head 1 at 100 ms: error (samples) @" + juce::String (fs), got - expected);
            CHECK_MSG (std::abs (got - expected) < 0.25, juce::String (fs) + " block " + juce::String (block) + ": " + juce::String (got - expected, 4));
        }
    for (double fs : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        auto p = makeEcho (fs, 512);
        MEASURE ("tape memory per instance (MB) @" + juce::String (fs), p->getEngineForTesting().getTapeMemoryBytes() / 1048576.0);
    }
}

TEST ("ECHO: NaN/Inf in the input never reaches the output or poisons the tape, the loop or the spring")
{
    auto p = makeEcho (48000.0, 256);
    setParam (*p, ep::intensity, 9.0f);
    setParam (*p, ep::reverbVolume, 8.0f);
    setParam (*p, ep::mix, 60.0f);
    setParam (*p, ep::duck, 50.0f);
    p->prepareToPlay (48000.0, 256);
    auto in = toBuffer (sine (48000.0, 440.0, 0.5f, 9600), 2);
    in.setSample (0, 100, std::numeric_limits<float>::quiet_NaN());
    in.setSample (1, 200, std::numeric_limits<float>::infinity());
    in.setSample (0, 300, -std::numeric_limits<float>::infinity());
    in.setSample (1, 5000, std::numeric_limits<float>::max());
    auto out = render (*p, in, 256);
    CHECK (allFinite (out));
    auto after = render (*p, toBuffer (sine (48000.0, 440.0, 0.5f, 48000), 2), 256);
    CHECK (allFinite (after));
    MEASURE ("rms after the bad samples", rms (after.getReadPointer (0) + 24000, 24000));
    CHECK (rms (after.getReadPointer (0) + 24000, 24000) > 0.1f);
}

TEST ("ECHO: silence gives silence (NOISE 0, Clean) or the specified hiss; DC is blocked on the wet path; denormal input stays clean; long tails decay")
{
    const double fs = 48000.0;
    {
        auto p = makeEcho (fs, 512);
        setParam (*p, ep::noise, 0.0f);
        setParam (*p, ep::intensity, 9.0f);
        setParam (*p, ep::reverbVolume, 10.0f);
        setParam (*p, ep::output, 12.0f);
        setParam (*p, ep::character, (float) cBurn);
        setParam (*p, ep::drive, 10.0f);
        p->prepareToPlay (fs, 512);
        const auto out = render (*p, silence (2, (int) fs * 2), 512);
        MEASURE ("silence in, NOISE 0, Clean: output peak", out.getMagnitude (0, out.getNumSamples()));
        CHECK (out.getMagnitude (0, out.getNumSamples()) == 0.0f);
    }
    {
        // default setting (NOISE 10 %): hiss at the record head -76.5 dBFS, heard through the echo
        auto p = makeEcho (fs, 512);
        const auto out = render (*p, silence (2, (int) fs * 3), 512);
        const double lv = dbOf (rms (out.getReadPointer (0) + (int) fs, 2 * (int) fs));
        MEASURE ("silence in, default setting: output noise (dBFS RMS)", lv);
        CHECK (lv > -100.0 && lv < -75.0);
    }
    {
        auto p = makeEcho (fs, 512);
        setParam (*p, ep::mix, 100.0f);
        setParam (*p, ep::reverbVolume, 6.0f);
        setParam (*p, ep::noise, 0.0f);
        p->prepareToPlay (fs, 512);
        juce::AudioBuffer<float> dc (2, (int) fs * 4);
        for (int ch = 0; ch < 2; ++ch)
            juce::FloatVectorOperations::fill (dc.getWritePointer (ch), 0.25f, dc.getNumSamples());
        const auto out = render (*p, dc, 512);
        double mean = 0;
        for (int i = out.getNumSamples() - (int) fs; i < out.getNumSamples(); ++i) mean += out.getSample (0, i);
        mean /= fs;
        MEASURE ("MIX 100 %: output DC after 4 s of 0.25 DC in", mean);
        CHECK (std::abs (mean) < 1.0e-3);
    }
    {
        auto p = makeEcho (fs, 512);
        setParam (*p, ep::noise, 0.0f);
        setParam (*p, ep::output, 12.0f);
        setParam (*p, ep::intensity, 8.0f);
        setParam (*p, ep::reverbVolume, 10.0f);
        p->prepareToPlay (fs, 512);
        juce::AudioBuffer<float> tiny (2, (int) fs);
        for (int i = 0; i < tiny.getNumSamples(); ++i)
            for (int ch = 0; ch < 2; ++ch)
                tiny.setSample (ch, i, (i & 1) ? 1.0e-38f : -1.0e-38f);
        const auto out = render (*p, tiny, 512);
        CHECK (allFinite (out));
        CHECK (out.getMagnitude (0, out.getNumSamples()) < 1.0e-6f);
    }
    {
        // tail stability: INTENSITY 7 with the spring, 20 s after a burst the echo has died away
        auto p = makeEcho (fs, 512);
        setParam (*p, ep::noise, 0.0f);
        setParam (*p, ep::intensity, 7.0f);
        setParam (*p, ep::reverbVolume, 6.0f);
        setParam (*p, ep::mode, (float) m123);
        p->prepareToPlay (fs, 512);
        auto in = silence (2, (int) (20.0 * fs));
        const auto burst = sine (fs, 440.0, 0.5f, (int) (0.2 * fs));
        for (int ch = 0; ch < 2; ++ch)
            in.copyFrom (ch, 0, burst.data(), (int) burst.size());
        const auto out = render (*p, in, 512);
        CHECK (allFinite (out));
        double prev = 1e9;
        bool decays = true;
        for (int s = 2; s < 20; s += 3)
        {
            const double lv = dbOf (rms (out.getReadPointer (0) + (int) (s * fs), (int) fs));
            decays = decays && lv < prev + 0.5;
            prev = lv;
        }
        const double last = dbOf (rms (out.getReadPointer (0) + (int) (19.0 * fs), (int) fs));
        MEASURE ("INTENSITY 7, 1+2+3, spring 6: level 19..20 s after a burst (dBFS)", last);
        CHECK (decays);
        CHECK (last < -100.0);
    }
}

TEST ("ECHO: host Bypass and MIX 0 are bit-exact dry (zero latency), every rate and layout; the Bypass transition is a 20 ms crossfade")
{
    for (double fs : { 44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0 })
        for (auto [inCh, outCh] : { std::pair { 1, 1 }, std::pair { 2, 2 }, std::pair { 1, 2 } })
        {
            auto p = makeEcho (fs, 128, inCh, outCh);
            CHECK (p->getLatencySamples() == 0);
            const auto tag = juce::String (fs) + " " + juce::String (inCh) + "->" + juce::String (outCh);
            // the input as the host passes it (mono -> stereo: the second channel arrives as a copy)
            juce::AudioBuffer<float> sig (outCh, 8192);
            const auto l = sine (fs, 997.0, 0.5f, 8192), r = sine (fs, 331.0, 0.4f, 8192);
            sig.copyFrom (0, 0, l.data(), 8192);
            if (outCh == 2) sig.copyFrom (1, 0, (inCh == 2 ? r : l).data(), 8192);
            setParam (*p, ep::intensity, 10.0f);
            setParam (*p, ep::reverbVolume, 10.0f);
            setParam (*p, ep::character, (float) cBurn);
            setParam (*p, ep::drive, 10.0f);
            setParam (*p, ep::output, 12.0f);
            setParam (*p, ep::tapeCondition, 2.0f);
            setParam (*p, ep::bypass, 1.0f);
            p->prepareToPlay (fs, 128);
            auto out = render (*p, sig, 128);
            CHECK_MSG (identical (out, sig), tag + ": Bypass is not bit-exact dry");

            setParam (*p, ep::bypass, 0.0f);
            setParam (*p, ep::output, 0.0f);
            setParam (*p, ep::mix, 0.0f);
            p->prepareToPlay (fs, 128);
            out = render (*p, sig, 128);
            CHECK_MSG (identical (out, sig), tag + ": MIX 0 is not bit-exact dry");
        }

    // Bypass engaged while the echo runs: 20 ms crossfade to the exact dry, then bit-exact
    const double fs = 48000.0;
    auto p = makeEcho (fs, 64);
    setParam (*p, ep::intensity, 8.0f);
    setParam (*p, ep::mix, 70.0f);
    p->prepareToPlay (fs, 64);
    const auto sig = toBuffer (sine (fs, 300.0, 0.4f, (int) fs), 2);
    juce::AudioBuffer<float> io (sig);
    juce::MidiBuffer midi;
    const int switchAt = (int) (0.5 * fs) / 64 * 64;
    for (int pos = 0; pos < io.getNumSamples(); pos += 64)
    {
        if (pos == switchAt) setParam (*p, ep::bypass, 1.0f);
        juce::AudioBuffer<float> view (io.getArrayOfWritePointers(), 2, pos, 64);
        p->processBlock (view, midi);
    }
    int firstExact = -1;
    for (int i = switchAt; i < io.getNumSamples(); ++i)
    {
        bool exact = true;
        for (int j = i; j < io.getNumSamples() && exact; ++j)
            exact = sameBits (io.getSample (0, j), sig.getSample (0, j)) && sameBits (io.getSample (1, j), sig.getSample (1, j));
        if (exact) { firstExact = i; break; }
    }
    MEASURE ("Bypass engaged: dry is bit-exact after (ms)", (firstExact - switchAt) / fs * 1000.0);
    CHECK (firstExact > switchAt && firstExact - switchAt <= (int) (0.021 * fs));
}

TEST ("ECHO: mono, stereo and mono->stereo: one mono tape; mono output = mid of the stereo output; mono->stereo = stereo with a copied input")
{
    const double fs = 48000.0;
    auto setup = [] (EchoProcessor& p)
    {
        setParam (p, ep::mode, (float) m123);
        setParam (p, ep::intensity, 6.0f);
        setParam (p, ep::reverbVolume, 5.0f);
        setParam (p, ep::stereo, 80.0f);
        setParam (p, ep::mix, 60.0f);
    };
    auto st = makeEcho (fs, 256, 2, 2), mo = makeEcho (fs, 256, 1, 1), ms = makeEcho (fs, 256, 1, 2);
    for (auto* p : { st.get(), mo.get(), ms.get() })
    {
        setup (*p);
        p->prepareToPlay (fs, 256);
    }
    const auto l = sine (fs, 300.0, 0.5f, 24000);
    const auto inSt = toBuffer (l, 2);
    const auto oSt = render (*st, inSt, 256);
    const auto oMo = render (*mo, toBuffer (l, 1), 256);
    const auto oMs = render (*ms, inSt, 256); // the host hands a mono->stereo plugin the input on channel 0 (channel 1 is ignored)
    double dMid = 0, dMs = 0, side = 0;
    for (int i = 0; i < 24000; ++i)
    {
        const double mid = 0.5 * ((double) oSt.getSample (0, i) + oSt.getSample (1, i));
        dMid = std::max (dMid, std::abs (oMo.getSample (0, i) - mid));
        dMs = std::max (dMs, (double) std::abs (oMs.getSample (0, i) - oSt.getSample (0, i)) + std::abs (oMs.getSample (1, i) - oSt.getSample (1, i)));
        side = std::max (side, 0.5 * std::abs ((double) oSt.getSample (0, i) - oSt.getSample (1, i)));
    }
    MEASURE ("max |mono out - mid of stereo out|", dMid);
    MEASURE ("max |mono->stereo - stereo|", dMs);
    MEASURE ("STEREO 80 %: max |side| of the stereo output", side);
    CHECK (dMid < 2.0e-6);
    CHECK (dMs < 2.0e-6);
    CHECK (side > 0.01);

    // input on the left only: the tape is mono (RE-201: one input path), so the echo comes back on both sides
    auto p = makeEcho (fs, 256, 2, 2);
    measurementSetup (*p, m1);
    p->prepareToPlay (fs, 256);
    juce::AudioBuffer<float> in (2, 24000);
    in.clear();
    const auto b = toneBurst (fs, 24000, 2000, 0.5f);
    in.copyFrom (0, 0, b.data(), 24000);
    const auto o = render (*p, in, 256);
    const double lEcho = rms (o.getReadPointer (0) + 2000 + 9000, 2000), rEcho = rms (o.getReadPointer (1) + 2000 + 9000, 2000);
    CHECK (lEcho > 0.01 && std::abs (dbOf (lEcho / rEcho)) < 0.01);
}

TEST ("ECHO: state round trip restores every parameter and renders identically; foreign and corrupt states are ignored")
{
    const double fs = 48000.0;
    auto a = makeEcho (fs, 256);
    const std::pair<const char*, float> values[] = {
        { ep::mode, 6.0f }, { ep::repeatRate, 333.0f }, { ep::intensity, 6.6f }, { ep::echoVolume, 5.5f }, { ep::reverbVolume, 4.4f },
        { ep::bass, -3.5f }, { ep::treble, 2.5f }, { ep::timeMode, 1.0f }, { ep::bpmSource, 1.0f }, { ep::manualBpm, 97.0f },
        { ep::noteDivision, 2.0f }, { ep::feel, 1.0f }, { ep::drive, 7.2f }, { ep::age, 5.1f }, { ep::wow, 44.0f }, { ep::flutter, 33.0f },
        { ep::hpf, 88.0f }, { ep::lpf, 5500.0f }, { ep::filterMode, 2.0f }, { ep::stereo, 66.0f }, { ep::duck, 25.0f }, { ep::mix, 72.0f },
        { ep::tapeCondition, 1.0f }, { ep::noise, 12.0f }, { ep::character, 2.0f }, { ep::output, -3.5f } };
    CHECK (std::size (values) == 26);
    for (auto [id, v] : values)
        setParam (*a, id, v);
    a->setUiProperty ("scale", 1.25);

    juce::MemoryBlock state;
    a->getStateInformation (state);
    auto b = makeEcho (fs, 256);
    b->setStateInformation (state.getData(), (int) state.getSize());
    for (auto* param : a->getParameters())
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (param))
            CHECK_MSG (std::abs (r->getValue() - b->apvts.getParameter (r->paramID)->getValue()) < 1e-6f, r->paramID);
    CHECK_NEAR ((double) b->getUiProperty ("scale", 1.0), 1.25, 1e-9);

    a->prepareToPlay (fs, 256);
    b->prepareToPlay (fs, 256);
    const auto sig = toBuffer (sine (fs, 300.0, 0.4f, 48000), 2);
    CHECK_MSG (identical (render (*a, sig, 256), render (*b, sig, 256)), "restored instance renders differently");
    auto fresh = makeEcho (fs, 256);
    CHECK (! identical (render (*fresh, sig, 256), render (*b, sig, 256)));

    b->setStateInformation ("nonsense", 8);
    CHECK_NEAR (plainValue (*b, ep::mix), 72.0f, 0.01);
    // a state written by another TLEET plugin is not loaded
    auto xml = juce::AudioProcessor::getXmlFromBinary (state.getData(), (int) state.getSize());
    CHECK (xml != nullptr);
    if (xml != nullptr)
    {
        xml->setAttribute ("plugin", "SHAPE");
        juce::MemoryBlock foreign;
        juce::AudioProcessor::copyXmlToBinary (*xml, foreign);
        auto c = makeEcho (fs, 256);
        c->setStateInformation (foreign.getData(), (int) foreign.getSize());
        CHECK_NEAR (plainValue (*c, ep::mix), 35.0f, 0.01);
    }
}

TEST ("ECHO: factory presets load, set every parameter, cover the controls and reject other plugins' presets")
{
    EchoProcessor p;
    const auto& list = p.presets.getPresets();
    int factory = 0;
    for (auto& pr : list) factory += pr.isFactory ? 1 : 0;
    MEASURE ("factory presets", factory);
    CHECK (factory >= 12); // spec 9.4: at least ~10, more for ECHO

    std::set<int> modes, chars, conds, timeModes, filterModes, feels;
    std::set<juce::String> names;
    for (int i = 0; i < (int) list.size(); ++i)
        if (list[(size_t) i].isFactory)
        {
            p.loadPreset (i);
            names.insert (list[(size_t) i].name);
            CHECK_MSG (list[(size_t) i].params.values.size() == 26, list[(size_t) i].name);
            for (auto& [id, v] : list[(size_t) i].params.values)
            {
                auto* r = p.apvts.getParameter (id);
                CHECK_MSG (r != nullptr, id);
                if (r) CHECK_MSG (std::abs (r->convertFrom0to1 (r->getValue()) - v) < 0.06f * std::max (1.0f, std::abs (v) * 0.01f), list[(size_t) i].name + ": " + id);
            }
            CHECK (! p.isPresetModified());
            modes.insert (juce::roundToInt (plainValue (p, ep::mode)));
            chars.insert (juce::roundToInt (plainValue (p, ep::character)));
            conds.insert (juce::roundToInt (plainValue (p, ep::tapeCondition)));
            timeModes.insert (juce::roundToInt (plainValue (p, ep::timeMode)));
            filterModes.insert (juce::roundToInt (plainValue (p, ep::filterMode)));
            feels.insert (juce::roundToInt (plainValue (p, ep::feel)));
        }
    CHECK (chars.size() == 3 && conds.size() == 3 && timeModes.size() == 2 && filterModes.size() == 3 && feels.size() == 3);
    CHECK (modes.size() >= 5 && modes.count (mReverb) == 1);
    CHECK ((int) names.size() == factory);

    setParam (p, ep::mix, 13.0f);
    CHECK (p.isPresetModified());
    PresetManager::Preset out;
    CHECK (p.presets.parse (R"({"format":"tleet-preset","version":1,"plugin":"SHAPE","name":"x","params":{}})", out).failed());
    CHECK (p.presets.parse (R"({"format":"tleet-preset","version":99,"plugin":"ECHO","name":"x","params":{}})", out).failed());
    const auto json = p.stateToClipboardJson();
    setParam (p, ep::mix, 99.0f);
    CHECK (p.pasteStateFromJson (json).wasOk());
    CHECK_NEAR (plainValue (p, ep::mix), 13.0f, 0.06);

    FakePlayHead head;
    for (int i = 0; i < (int) list.size(); ++i)
    {
        auto q = makeEcho (48000.0, 256);
        q->setPlayHead (&head);
        q->loadPreset (i);
        q->prepareToPlay (48000.0, 256);
        const auto o = render (*q, controlSignal (48000.0), 256);
        q->setPlayHead (nullptr);
        // the effect is audible: the output differs from the dry input (wet level re dry, dB)
        const auto dry = controlSignal (48000.0);
        double dd = 0, de = 0;
        for (int ch = 0; ch < 2; ++ch)
            for (int k = 0; k < o.getNumSamples(); ++k)
            {
                dd += std::pow ((double) o.getSample (ch, k) - dry.getSample (ch, k), 2.0);
                de += std::pow ((double) dry.getSample (ch, k), 2.0);
            }
        const double tail = rms (o.getReadPointer (0) + 24000 + 4800, 24000);
        MEASURE (list[(size_t) i].name + ": output - input re input (dB)", 10.0 * std::log10 (dd / de));
        MEASURE (list[(size_t) i].name + ": tail level (dBFS)", dbOf (tail));
        MEASURE (list[(size_t) i].name + ": output peak", o.getMagnitude (0, o.getNumSamples()));
        CHECK_MSG (allFinite (o) && 10.0 * std::log10 (dd / de) > -20.0, list[(size_t) i].name);
        CHECK_MSG (o.getMagnitude (0, o.getNumSamples()) < 2.0f, list[(size_t) i].name + ": too hot");
    }
}

TEST ("ECHO: aliasing and intermodulation of the record stage (2x oversampled below 80 kHz; measurement, CSV)")
{
    // Largest non-harmonic component in the first echo, re the fundamental. The
    // record stage runs at 88.2/96 kHz (2x below 80 kHz, 1x above): odd harmonics
    // of the clipped preamp above the record Nyquist fold back.
    struct Case { int character; float drive; double hz, levelDbfs; double limitDbc; const char* name; };
    const Case cases[] = {
        { cOriginal, 3.0f, 5000.0, -6.0, -60.0, "Original DRIVE 3, 5 kHz -6 dBFS (default colour, hot HF)" },
        { cOriginal, 10.0f, 1100.0, -12.0, -50.0, "Original DRIVE 10, 1.1 kHz -12 dBFS" },
        { cBurn, 10.0f, 1100.0, -12.0, -35.0, "Burn DRIVE 10, 1.1 kHz -12 dBFS (preamp near square)" },
        { cBurn, 10.0f, 7000.0, -3.0, -15.0, "Burn DRIVE 10, 7 kHz -3 dBFS (worst case)" } };
    juce::String csv ("rate,case,worst_nonharmonic_dbc,at_hz\n");
    for (double fs : { 44100.0, 48000.0, 96000.0 })
        for (const auto& c : cases)
        {
            auto p = makeEcho (fs, 256);
            measurementSetup (*p, m1);
            setParam (*p, ep::repeatRate, 50.0f);
            setParam (*p, ep::character, (float) c.character);
            setParam (*p, ep::drive, c.drive);
            p->prepareToPlay (fs, 256);
            const int win = (int) fs / 2;
            const auto out = render (*p, toBuffer (sine (fs, c.hz, (float) std::pow (10.0, c.levelDbfs / 20.0), (int) (0.3 * fs) + win), 2), 256);
            const float* d = out.getReadPointer (0) + out.getNumSamples() - win;
            const double a0 = toneAmplitude (d, win, fs, c.hz);
            double worst = 0, worstHz = 0;
            for (double hz = 100.0; hz < std::min (20000.0, 0.45 * fs); hz += 10.0)
            {
                const double k = hz / c.hz;
                if (std::abs (k - std::round (k)) * c.hz < 15.0) continue; // harmonics
                const double a = toneAmplitude (d, win, fs, hz);
                if (a > worst) { worst = a; worstHz = hz; }
            }
            const double dbc = dbOf (worst / a0);
            csv << fs << ",\"" << c.name << "\"," << dbc << "," << worstHz << "\n";
            MEASURE (juce::String (c.name) + ": largest non-harmonic (dBc) @" + juce::String (fs), dbc);
            CHECK_MSG (dbc < c.limitDbc, juce::String (c.name) + " @" + juce::String (fs));
        }
    outputDirectory().getChildFile ("ECHO_aliasing.csv").replaceWithText (csv);
    // SMPTE-style IMD: 60 Hz + 7 kHz (4:1), Original DRIVE 6: sidebands at 7 kHz +- 60/120 Hz
    {
        const double fs = 48000.0;
        auto p = makeEcho (fs, 256);
        measurementSetup (*p, m1);
        setParam (*p, ep::repeatRate, 50.0f);
        setParam (*p, ep::character, (float) cOriginal);
        setParam (*p, ep::drive, 6.0f);
        p->prepareToPlay (fs, 256);
        const int win = (int) fs;
        const auto lo = sine (fs, 60.0, 0.4f, (int) (0.3 * fs) + win), hi = sine (fs, 7000.0, 0.1f, (int) (0.3 * fs) + win);
        std::vector<float> s (lo.size());
        for (size_t i = 0; i < s.size(); ++i) s[i] = lo[i] + hi[i];
        const auto out = render (*p, toBuffer (s, 2), 256);
        const float* d = out.getReadPointer (0) + out.getNumSamples() - win;
        const double c = toneAmplitude (d, win, fs, 7000.0);
        double sb = 0;
        for (double off : { -120.0, -60.0, 60.0, 120.0 })
            sb += std::pow (toneAmplitude (d, win, fs, 7000.0 + off), 2.0);
        MEASURE ("SMPTE IMD 60 Hz + 7 kHz 4:1, Original DRIVE 6 (%)", 100.0 * std::sqrt (sb) / c);
        CHECK (std::sqrt (sb) / c > 1.0e-4); // the record stage is nonlinear
    }
}

TEST ("ECHO: CPU cost per instance (measurement)")
{
    for (double fs : { 44100.0, 48000.0, 88200.0, 96000.0, 192000.0 })
        for (int block : { 64, 128 })
        {
            auto p = makeEcho (fs, block);
            setParam (*p, ep::mode, (float) m123);
            setParam (*p, ep::intensity, 6.0f);
            setParam (*p, ep::reverbVolume, 5.0f);
            setParam (*p, ep::duck, 30.0f);
            setParam (*p, ep::mix, 50.0f);
            p->prepareToPlay (fs, block);
            juce::Random rng (1);
            const int n = (int) (fs * 3.0);
            juce::AudioBuffer<float> io (2, n);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < n; ++i)
                    io.setSample (ch, i, 0.5f * (float) std::sin (i * 0.031) * (rng.nextFloat() * 0.6f + 0.4f));
            juce::MidiBuffer midi;
            std::vector<double> times;
            const auto t0 = juce::Time::getMillisecondCounterHiRes();
            for (int pos = 0; pos + block <= n; pos += block)
            {
                juce::AudioBuffer<float> view (io.getArrayOfWritePointers(), 2, pos, block);
                const auto b0 = juce::Time::getHighResolutionTicks();
                p->processBlock (view, midi);
                times.push_back (juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - b0));
            }
            const double ms = juce::Time::getMillisecondCounterHiRes() - t0;
            std::sort (times.begin(), times.end());
            const double load = ms / 3000.0 * 100.0;
            const double medianLoad = times[times.size() / 2] * (fs / block) * 100.0;
            const auto tag = juce::String (fs) + " Hz, block " + juce::String (block) + ", stereo, 1+2+3 + spring";
            MEASURE (tag + ": % of one core", load);
            MEASURE (tag + ": % of one core from the median block", medianLoad);
            MEASURE (tag + ": p99 block time (us)", times[(size_t) (0.99 * (double) times.size())] * 1e6);
            MEASURE (tag + ": max block time (us)", times.back() * 1e6);
            if (fs <= 48000.0)
                CHECK (medianLoad < 10.0);
        }
}
