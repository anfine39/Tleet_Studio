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

    // Band-limited test pulse: a Hann-windowed sinc (cutoff 3 kHz), centred at `centre`.
    std::vector<float> pulse (double fs, int n, int centre, float amp = 0.5f)
    {
        std::vector<float> v ((size_t) n, 0.0f);
        const int half = (int) std::round (fs * 0.002);
        const double fc = 3000.0 / fs;
        for (int k = -half; k <= half; ++k)
        {
            const int i = centre + k;
            if (i < 0 || i >= n) continue;
            const double w = 0.5 + 0.5 * std::cos (pi * k / half);
            const double s = k == 0 ? 2.0 * fc : std::sin (2.0 * pi * fc * k) / (pi * k);
            v[(size_t) i] = amp * (float) (w * s / (2.0 * fc));
        }
        return v;
    }

    // Lag (fractional samples) of the cross-correlation peak of `y` against
    // `ref` (a pulse centred at refCentre), searched in [from, to].
    double correlationLag (const float* y, int ny, const std::vector<float>& ref, int refCentre, int from, int to)
    {
        int k0 = 0, k1 = (int) ref.size() - 1;
        while (k0 < k1 && ref[(size_t) k0] == 0.0f) ++k0;
        while (k1 > k0 && ref[(size_t) k1] == 0.0f) --k1;
        auto corr = [&] (int lag)
        {
            double acc = 0;
            for (int k = k0; k <= k1; ++k)
            {
                const int i = k + lag;
                if (i >= 0 && i < ny)
                    acc += (double) y[i] * ref[(size_t) k];
            }
            return acc;
        };
        int best = from;
        double bestV = -1e300;
        for (int lag = from; lag <= to; ++lag)
            if (const double c = corr (lag); c > bestV) { bestV = c; best = lag; }
        const double a = corr (best - 1), b = bestV, c = corr (best + 1);
        const double den = a - 2.0 * b + c;
        const double frac = std::abs (den) > 1e-30 ? 0.5 * (a - c) / den : 0.0;
        juce::ignoreUnused (refCentre);
        return best + frac;
    }

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

    double goertzelPhase (const float* d, int n, double fs, double hz)
    {
        double re = 0, im = 0;
        for (int i = 0; i < n; ++i)
        {
            re += d[i] * std::cos (2.0 * pi * hz * i / fs);
            im -= d[i] * std::sin (2.0 * pi * hz * i / fs);
        }
        return std::atan2 (im, re);
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
        const int mask = ep::modeHeadMask[mode];
        for (int h = 0; h < 3; ++h)
        {
            const bool on = ((mask >> h) & 1) != 0;
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

    const auto w = delayTrace (100.0f, 0.0f, 0);
    const auto fl = delayTrace (0.0f, 100.0f, 0);
    const double wowLine = line (w, echo::mod::wowHz), wowExp = expectedLine (echo::mod::wowMax, echo::mod::wowHz);
    const double flLine = line (fl, echo::mod::flutterHz), flExp = expectedLine (echo::mod::flutterMax, echo::mod::flutterHz);
    MEASURE ("WOW 100 %: 0.55 Hz delay line (ms)", wowLine);
    MEASURE ("    expected (ms)", wowExp);
    MEASURE ("FLUTTER 100 %: 6.5 Hz delay line (ms)", flLine);
    MEASURE ("    expected (ms)", flExp);
    CHECK_NEAR (wowLine / wowExp, 1.0, 0.15);
    CHECK_NEAR (flLine / flExp, 1.0, 0.15);
    MEASURE ("WOW 100 %: 6.5 Hz line (ms)", line (w, echo::mod::flutterHz));
    MEASURE ("FLUTTER 100 %: 0.55 Hz line (ms)", line (fl, echo::mod::wowHz));
    CHECK (line (w, echo::mod::flutterHz) < 0.05 * flExp);   // WOW does not make flutter
    CHECK (line (fl, echo::mod::wowHz) < 0.05 * wowExp);     // FLUTTER does not make wow
    const auto half = delayTrace (50.0f, 0.0f, 0);
    CHECK_NEAR (line (half, echo::mod::wowHz) / wowLine, 0.5, 0.08); // depth is linear in WOW
    for (double f = 0.1; f <= 30.0; f += 0.05)
        csv << "100,0," << f << "," << line (w, f) << "," << (std::abs (f - echo::mod::wowHz) < 0.026 ? wowExp : 0.0) << "\n";
    for (double f = 0.1; f <= 30.0; f += 0.05)
        csv << "0,100," << f << "," << line (fl, f) << "," << (std::abs (f - echo::mod::flutterHz) < 0.026 ? flExp : 0.0) << "\n";
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
