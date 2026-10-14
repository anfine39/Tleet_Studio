#include "TestFramework.h"
#include "TestSignals.h"
#include <juce_dsp/juce_dsp.h>
#include <cstring>
#include <functional>
#include <map>
#include <set>
#include "../plugins/VOID/PluginProcessor.h"
#include "../plugins/VOID/Parameters.h"
#include "../plugins/VOID/dsp/VoidEngine.h"

// Exact floating-point comparisons are deliberate here (bit-exact paths,
// snapped glides, base values that must not move).
#if defined(__clang__) || defined(__GNUC__)
 #pragma GCC diagnostic ignored "-Wfloat-equal"
#endif

using namespace tleet;
using namespace tleet::test;
using tleet::voidrev::VoidProcessor;
namespace vp = tleet::voidrev::params;
namespace vf = tleet::voidrev::filt;

namespace
{
    constexpr double pi = juce::MathConstants<double>::pi;
    enum AlgoIdx { aRoom = 0, aPlate, aChamber, aHall, aVoid };
    enum ColorIdx { cClean = 0, cDark, cRuin };
    const char* algoName (int a) { static const char* n[] = { "Room", "Plate", "Chamber", "Hall", "Void" }; return n[a]; }

    void setParam (VoidProcessor& p, const char* id, float plain)
    {
        auto* r = p.apvts.getParameter (id);
        r->setValueNotifyingHost (r->convertTo0to1 (plain));
    }

    float plainValue (VoidProcessor& p, const char* id)
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

    double dbOf (double r) { return 20.0 * std::log10 (std::max (r, 1.0e-15)); }

    juce::AudioBuffer<float> render (VoidProcessor& p, const juce::AudioBuffer<float>& input, int block)
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

    juce::AudioBuffer<float> impulse (int channels, int n, float amp = 0.5f, int at = 0)
    {
        auto b = silence (channels, n);
        for (int ch = 0; ch < channels; ++ch)
            b.setSample (ch, at, amp);
        return b;
    }

    juce::AudioBuffer<float> noise (int channels, int n, float rmsAmp, int seed = 7, bool independent = true)
    {
        juce::Random rng (seed);
        juce::AudioBuffer<float> b (channels, n);
        const float a = rmsAmp * std::sqrt (3.0f);
        for (int i = 0; i < n; ++i)
        {
            const float common = a * (2.0f * rng.nextFloat() - 1.0f);
            for (int ch = 0; ch < channels; ++ch)
                b.setSample (ch, i, ch == 0 || ! independent ? common : a * (2.0f * rng.nextFloat() - 1.0f));
        }
        return b;
    }

    std::unique_ptr<VoidProcessor> makeVoid (double fs, int block, int inChannels = 2, int outChannels = 2)
    {
        auto p = std::make_unique<VoidProcessor>();
        VoidProcessor::BusesLayout layout;
        layout.inputBuses.add (inChannels == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo());
        layout.outputBuses.add (outChannels == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo());
        CHECK_MSG (p->setBusesLayout (layout), "layout rejected");
        p->setRateAndBufferSizeDetails (fs, block);
        p->prepareToPlay (fs, block);
        return p;
    }

    // A measurement setting: full wet, no pre-delay, no modulation, Clean,
    // natural width, default filters (LOOP, 20 Hz / 20 kHz). prepare() afterwards.
    void measurementSetup (VoidProcessor& p, int algo, float decay = 2.5f)
    {
        setParam (p, vp::algorithm, (float) algo);
        setParam (p, vp::color, (float) cClean);
        setParam (p, vp::preDelay, 0.0f);
        setParam (p, vp::decay, decay);
        setParam (p, vp::modDepth, 0.0f);
        setParam (p, vp::mix, 100.0f);
        setParam (p, vp::width, 100.0f);
        setParam (p, vp::drown, 0.0f);
        setParam (p, vp::duck, 0.0f);
    }

    // ---- analysis -------------------------------------------------------------------

    // 4th-order Butterworth band pass (two 2nd-order HP + two LP), an octave around fc.
    std::vector<double> octaveBand (const float* x, int n, double fs, double fc)
    {
        vf::Svf h1, h2, l1, l2;
        const double gl = vf::integratorGain (fc / std::sqrt (2.0), fs), gh = vf::integratorGain (fc * std::sqrt (2.0), fs);
        h1.set (gl, 0.7653668647301796); h2.set (gl, 1.8477590650225735);
        l1.set (gh, 0.7653668647301796); l2.set (gh, 1.8477590650225735);
        std::vector<double> y ((size_t) n);
        for (int i = 0; i < n; ++i)
            y[(size_t) i] = l2.process<vf::Svf::lowpass> (l1.process<vf::Svf::lowpass> (
                h2.process<vf::Svf::highpass> (h1.process<vf::Svf::highpass> (x[i]))));
        return y;
    }

    // Schroeder backward integration (dB re total).
    std::vector<double> edc (const std::vector<double>& h)
    {
        std::vector<double> e (h.size());
        double acc = 0.0;
        for (size_t i = h.size(); i-- > 0;)
        {
            acc += h[i] * h[i];
            e[i] = acc;
        }
        const double total = std::max (e.empty() ? 0.0 : e[0], 1e-300);
        for (auto& v : e)
            v = 10.0 * std::log10 (std::max (v / total, 1e-300));
        return e;
    }

    // RT60 from the least-squares slope of the EDC between `hi` and `lo` dB (T30: -5..-35).
    double rt60FromEdc (const std::vector<double>& e, double fs, double hi = -5.0, double lo = -35.0)
    {
        double sx = 0, sy = 0, sxx = 0, sxy = 0;
        int n = 0;
        for (size_t i = 0; i < e.size(); ++i)
            if (e[i] <= hi && e[i] >= lo)
            {
                const double t = (double) i / fs;
                sx += t; sy += e[i]; sxx += t * t; sxy += t * e[i]; ++n;
            }
        if (n < 10)
            return 0.0;
        const double slope = (n * sxy - sx * sy) / (n * sxx - sx * sx);
        return slope < 0.0 ? -60.0 / slope : 0.0;
    }

    // Impulse response of the wet path (both channels summed in power for the EDC).
    double measureRt60 (VoidProcessor& p, double fs, double seconds, double bandHz = 1000.0, double hi = -5.0, double lo = -35.0)
    {
        const int n = (int) (seconds * fs);
        const auto out = render (p, impulse (2, n), 512);
        const auto bl = octaveBand (out.getReadPointer (0), n, fs, bandHz);
        const auto br = octaveBand (out.getReadPointer (1), n, fs, bandHz);
        std::vector<double> h ((size_t) n);
        for (size_t i = 0; i < h.size(); ++i)
            h[i] = std::sqrt (0.5 * (bl[i] * bl[i] + br[i] * br[i]));
        return rt60FromEdc (edc (h), fs, hi, lo);
    }
}


namespace
{
    // Power spectrum (Welch, Hann, 50 % overlap) of a signal, in dB per bin.
    std::vector<double> welchDb (const float* x, int n, int order = 13)
    {
        const int size = 1 << order;
        juce::dsp::FFT fft (order);
        std::vector<float> buf ((size_t) size * 2);
        std::vector<double> acc ((size_t) size / 2 + 1, 0.0);
        int frames = 0;
        for (int start = 0; start + size <= n; start += size / 2)
        {
            std::fill (buf.begin(), buf.end(), 0.0f);
            for (int i = 0; i < size; ++i)
                buf[(size_t) i] = x[start + i] * (float) (0.5 - 0.5 * std::cos (2.0 * pi * i / size));
            fft.performFrequencyOnlyForwardTransform (buf.data());
            for (size_t k = 0; k < acc.size(); ++k)
                acc[k] += (double) buf[k] * buf[k];
            ++frames;
        }
        for (auto& v : acc)
            v = 10.0 * std::log10 (std::max (v / std::max (1, frames), 1e-30));
        return acc;
    }

    // Mean of a dB spectrum over a band (power average).
    double bandDb (const std::vector<double>& specDb, double fs, int order, double lo, double hi)
    {
        const double binHz = fs / (1 << order);
        double p = 0;
        int n = 0;
        for (size_t k = 0; k < specDb.size(); ++k)
            if (k * binHz >= lo && k * binHz <= hi)
            {
                p += std::pow (10.0, specDb[k] / 10.0);
                ++n;
            }
        return 10.0 * std::log10 (std::max (p / std::max (n, 1), 1e-30));
    }

    double rmsRange (const juce::AudioBuffer<float>& b, int ch, int from, int len)
    {
        from = std::max (0, from);
        len = std::min (len, b.getNumSamples() - from);
        return len > 0 ? (double) rms (b.getReadPointer (ch) + from, len) : 0.0;
    }

    double stereoRms (const juce::AudioBuffer<float>& b, int from, int len)
    {
        const double l = rmsRange (b, 0, from, len), r = rmsRange (b, std::min (1, b.getNumChannels() - 1), from, len);
        return std::sqrt (0.5 * (l * l + r * r));
    }

    // A run of >= 4 identical consecutive samples above 0.25 in magnitude: a hard clip.
    bool hasFlatTops (const juce::AudioBuffer<float>& b)
    {
        for (int ch = 0; ch < b.getNumChannels(); ++ch)
        {
            const float* d = b.getReadPointer (ch);
            int run = 0;
            for (int i = 1; i < b.getNumSamples(); ++i)
            {
                run = (d[i] == d[i - 1] && std::abs (d[i]) > 0.25f) ? run + 1 : 0;
                if (run >= 3)
                    return true;
            }
        }
        return false;
    }
}

// =====================================================================================
TEST ("VOID: parameter IDs, ranges, defaults, texts, choice strings and buses match the registry")
{
    VoidProcessor p;
    const char* ids[] = { "algorithm", "color", "preDelay", "size", "decay", "diffusion", "drown", "modRate", "modDepth", "width",
                          "hpf", "lpf", "filterMode", "duck", "mix", "freeze", "output", "bypass" };
    for (auto* id : ids)
        CHECK_MSG (p.apvts.getParameter (id) != nullptr, id);
    CHECK (p.getParameters().size() == 18);
    for (auto* param : p.getParameters())
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (param))
            CHECK_MSG (r->getVersionHint() == 1, r->paramID);

    auto def = [&p] (const char* id) { auto* r = p.apvts.getParameter (id); return r->convertFrom0to1 (r->getDefaultValue()); };
    CHECK_NEAR (def ("algorithm"), 3.0f, 1e-5);   // "Hall"
    CHECK_NEAR (def ("color"), 0.0f, 1e-6);       // "Clean"
    CHECK_NEAR (def ("preDelay"), 20.0f, 1e-3);
    CHECK_NEAR (def ("size"), 50.0f, 1e-4);
    CHECK_NEAR (def ("decay"), 2.5f, 1e-4);
    CHECK_NEAR (def ("diffusion"), 70.0f, 1e-4);
    CHECK_NEAR (def ("drown"), 0.0f, 1e-6);
    CHECK_NEAR (def ("modRate"), 0.5f, 1e-5);
    CHECK_NEAR (def ("modDepth"), 30.0f, 1e-4);
    CHECK_NEAR (def ("width"), 100.0f, 1e-4);
    CHECK_NEAR (def ("hpf"), 20.0f, 1e-3);
    CHECK_NEAR (def ("lpf"), 20000.0f, 0.5);
    CHECK_NEAR (def ("filterMode"), 1.0f, 1e-5);  // "Loop"
    CHECK_NEAR (def ("duck"), 0.0f, 1e-6);
    CHECK_NEAR (def ("mix"), 35.0f, 1e-4);
    CHECK_NEAR (def ("freeze"), 0.0f, 1e-6);
    CHECK_NEAR (def ("output"), 0.0f, 1e-6);
    CHECK_NEAR (def ("bypass"), 0.0f, 1e-6);

    auto range = [&p] (const char* id, float lo, float mid, float hi, double tolMid = 1e-3)
    {
        auto* r = p.apvts.getParameter (id);
        CHECK_NEAR (r->convertFrom0to1 (0.0f), lo, 1e-3);
        CHECK_NEAR (r->convertFrom0to1 (0.5f), mid, tolMid);
        CHECK_NEAR (r->convertFrom0to1 (1.0f), hi, 1e-2);
    };
    range ("preDelay", 0.0f, 60.0f, 250.0f, 0.05);                        // skewed: 60 ms at the knob centre
    range ("decay", 0.1f, std::sqrt (0.1f * 60.0f), 60.0f, 1e-3);         // log
    range ("modRate", 0.01f, std::sqrt (0.01f * 5.0f), 5.0f, 1e-4);       // log
    range ("hpf", 20.0f, std::sqrt (20.0f * 1000.0f), 1000.0f, 0.05);     // log
    range ("lpf", 1000.0f, std::sqrt (1000.0f * 20000.0f), 20000.0f, 1.0); // log
    range ("width", 0.0f, 100.0f, 200.0f);
    range ("output", -12.0f, 0.0f, 12.0f);
    for (auto* id : { "size", "diffusion", "drown", "modDepth", "duck", "mix" })
        range (id, 0.0f, 50.0f, 100.0f);
    {
        auto* f = dynamic_cast<juce::AudioParameterFloat*> (p.apvts.getParameter ("output"));
        juce::AudioProcessorParameter* r = f;
        const auto& nr = f->getNormalisableRange();
        CHECK (nr.convertFrom0to1 (0.5f) == 0.0f && nr.convertTo0to1 (0.0f) == 0.5f);
        int bad = 0;
        for (int t = -120; t <= 120; ++t)
        {
            const float v = nr.snapToLegalValue ((float) t * 0.1f);
            bad += nr.snapToLegalValue (nr.convertFrom0to1 (r->getValueForText (r->getText (nr.convertTo0to1 (v), 64)))) != v ? 1 : 0;
        }
        CHECK (bad == 0);
    }

    auto text = [&p] (const char* id, float plain) { auto* r = p.apvts.getParameter (id); return r->getText (r->convertTo0to1 (plain), 64); };
    const juce::String inf (juce::CharPointer_UTF8 ("\xe2\x88\x9e"));
    CHECK (text ("preDelay", 42.0f) == "42 ms");
    CHECK (text ("preDelay", 4.5f) == "4.5 ms");
    CHECK (text ("preDelay", 0.0f) == "0 ms");
    CHECK (text ("preDelay", 250.0f) == "250 ms");
    CHECK (text ("decay", 2.5f) == "2.50 s");
    CHECK (text ("decay", 0.1f) == "0.10 s");
    CHECK (text ("decay", 12.5f) == "12.5 s");
    CHECK (text ("decay", 59.0f) == "59.0 s");
    CHECK (text ("decay", 60.0f) == inf);
    CHECK (text ("size", 72.0f) == "72 %");
    CHECK (text ("width", 150.0f) == "150 %");
    CHECK (text ("mix", 72.5f) == "72.5 %");
    CHECK (text ("modRate", 0.42f) == "0.42 Hz");
    CHECK (text ("modRate", 5.0f) == "5.00 Hz");
    CHECK (text ("hpf", 120.0f) == "120 Hz");
    CHECK (text ("hpf", 1000.0f) == "1.0 kHz");
    CHECK (text ("lpf", 8500.0f) == "8.5 kHz");
    CHECK (text ("lpf", 20000.0f) == "20.0 kHz");
    CHECK (text ("output", 3.0f) == "+3.0 dB");
    CHECK (text ("output", -4.5f) == "-4.5 dB");
    CHECK (text ("output", 0.0f) == "0.0 dB");
    CHECK (text ("algorithm", 4.0f) == "Void");
    CHECK (text ("color", 2.0f) == "Ruin");
    CHECK (text ("filterMode", 0.0f) == "Pre");
    auto fromText = [&p] (const char* id, const juce::String& t) { auto* r = p.apvts.getParameter (id); return r->convertFrom0to1 (r->getValueForText (t)); };
    CHECK_NEAR (fromText ("decay", inf), 60.0f, 1e-3);
    CHECK_NEAR (fromText ("decay", "inf"), 60.0f, 1e-3);
    CHECK_NEAR (fromText ("decay", "800 ms"), 0.8f, 1e-4);
    CHECK_NEAR (fromText ("decay", "3.2 s"), 3.2f, 1e-4);
    CHECK_NEAR (fromText ("preDelay", "0.1 s"), 100.0f, 0.01);
    CHECK_NEAR (fromText ("preDelay", "35"), 35.0f, 0.01);
    CHECK_NEAR (fromText ("lpf", "8.5k"), 8500.0f, 0.5);
    CHECK_NEAR (fromText ("hpf", "85 Hz"), 85.0f, 0.01);
    CHECK_NEAR (fromText ("modRate", "1.5 Hz"), 1.5f, 1e-4);
    CHECK_NEAR (fromText ("output", "-6 dB"), -6.0f, 1e-4);
    CHECK (vp::isInfiniteDecay (60.0f) && ! vp::isInfiniteDecay (59.9f));

    // Discrete selectors expose only real values (addendum 4).
    auto choices = [&p] (const char* id) { return dynamic_cast<juce::AudioParameterChoice*> (p.apvts.getParameter (id))->choices; };
    CHECK (choices ("algorithm") == juce::StringArray ({ "Room", "Plate", "Chamber", "Hall", "Void" }));
    CHECK (choices ("color") == juce::StringArray ({ "Clean", "Dark", "Ruin" }));
    CHECK (choices ("filterMode") == juce::StringArray ({ "Pre", "Loop", "Post" }));
    CHECK (dynamic_cast<juce::AudioParameterBool*> (p.apvts.getParameter ("freeze")) != nullptr);
    CHECK (p.getBypassParameter() == p.apvts.getParameter ("bypass"));
    CHECK (p.getLatencySamples() == 0);
    CHECK (p.getName() == "TLEET VOID");

    auto layout = [] (juce::AudioChannelSet i, juce::AudioChannelSet o)
    {
        VoidProcessor::BusesLayout l;
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

TEST ("VOID: decay-time validation: measured RT60 at 1 kHz = DECAY for every algorithm, 0.8-12 s, 44.1-192 kHz (T30, tolerance 5 %); short decays stay short (CSV)")
{
    // Impulse response, 1 kHz octave band (4th-order Butterworth band pass),
    // Schroeder backward integration, least-squares slope from -5 to -35 dB (T30,
    // ISO 3382-1). SIZE 50 %, DIFFUSION 70 %, MOD DEPTH 0, Clean, LOOP filters at
    // their defaults.
    juce::String csv ("rate,algorithm,decay_s,rt60_s,error_pct,rt60_250Hz_s,rt60_4kHz_s\n");
    double worst = 0.0;
    auto run = [&] (double fs, int a, float decay, bool bands, float modDepth = 0.0f)
    {
        auto p = makeVoid (fs, 512);
        measurementSetup (*p, a, decay);
        setParam (*p, vp::modDepth, modDepth);
        p->prepareToPlay (fs, 512);
        const double seconds = 0.75 * decay + 0.9;
        const int n = (int) (seconds * fs);
        const auto out = render (*p, impulse (2, n), 512);
        auto band = [&] (double hz)
        {
            const auto bl = octaveBand (out.getReadPointer (0), n, fs, hz);
            const auto br = octaveBand (out.getReadPointer (1), n, fs, hz);
            std::vector<double> h ((size_t) n);
            for (size_t i = 0; i < h.size(); ++i)
                h[i] = std::sqrt (0.5 * (bl[i] * bl[i] + br[i] * br[i]));
            return rt60FromEdc (edc (h), fs);
        };
        const double rt = band (1000.0);
        const double err = 100.0 * (rt / decay - 1.0);
        const double r250 = bands ? band (250.0) : 0.0, r4k = bands ? band (4000.0) : 0.0;
        csv << fs << "," << algoName (a) << "," << decay << "," << rt << "," << err << "," << r250 << "," << r4k << "\n";
        MEASURE (juce::String (algoName (a)) + " DECAY " + juce::String (decay) + " s @" + juce::String (fs) + (modDepth > 0 ? " (MOD 30 %)" : "")
                     + ": RT60(1 kHz) error %", err);
        CHECK_MSG (std::abs (err) <= 5.0, juce::String (algoName (a)) + " " + juce::String (decay) + " s @" + juce::String (fs) + ": " + juce::String (rt, 3) + " s");
        worst = std::max (worst, std::abs (err));
        return std::pair { r250 / rt, r4k / rt };
    };
    for (double fs : { 44100.0, 48000.0, 96000.0 })
        for (int a = 0; a < 5; ++a)
            for (float decay : { 0.8f, 1.5f, 4.0f, 12.0f })
            {
                const auto [lo, hi] = run (fs, a, decay, fs == 48000.0 && decay == 4.0f);
                if (fs == 48000.0 && decay == 4.0f)
                {
                    // two-band decay: the bands' RT60 ratios follow the algorithm's decay shape
                    const auto& shape = voidrev::decayShape (a);
                    MEASURE (juce::String (algoName (a)) + ": RT60(250 Hz) / RT60(1 kHz)", lo);
                    MEASURE (juce::String (algoName (a)) + ": RT60(4 kHz) / RT60(1 kHz)", hi);
                    CHECK (hi < 0.5 * (1.0 + shape.hiRatio) + 0.05);  // HF decays faster
                    if (shape.loRatio > 1.05) CHECK (lo > 1.03);      // Hall, Chamber, Void: longer bass
                }
            }
    for (int a = 0; a < 5; ++a)
    {
        run (192000.0, a, 2.5f, false);
        run (48000.0, a, 2.5f, false, 30.0f); // the default modulation does not bias the decay
    }
    MEASURE ("worst RT60 error (%)", worst);

    // Below ~0.8 s the onset (early reflections, input diffusion, Void's bloom,
    // the plate's tank transit) fills a large part of the decay and the EDC of
    // a few loop passes is a staircase, so ISO T30 is not a meaningful measure
    // there (MODEL_SPEC 5.4). What must hold: the IR falls by 60 dB (EDC) within
    // DECAY x 1.6 + 0.35 s, and shorter DECAY gives a shorter tail.
    for (int a = 0; a < 5; ++a)
    {
        double prev = 0.0;
        for (float decay : { 0.1f, 0.3f, 0.5f, 0.8f })
        {
            const double fs = 48000.0;
            auto p = makeVoid (fs, 512);
            measurementSetup (*p, a, decay);
            p->prepareToPlay (fs, 512);
            const int n = (int) (2.5 * fs);
            const auto out = render (*p, impulse (2, n), 512);
            const auto bl = octaveBand (out.getReadPointer (0), n, fs, 1000.0);
            const auto br = octaveBand (out.getReadPointer (1), n, fs, 1000.0);
            std::vector<double> h ((size_t) n);
            for (size_t i = 0; i < h.size(); ++i)
                h[i] = std::sqrt (0.5 * (bl[i] * bl[i] + br[i] * br[i]));
            const auto e = edc (h);
            double t60 = 0.0;
            for (size_t i = 0; i < e.size(); ++i)
                if (e[i] <= -60.0) { t60 = (double) i / fs; break; }
            csv << fs << "," << algoName (a) << "," << decay << ",edc60=" << t60 << ",,,\n";
            MEASURE (juce::String (algoName (a)) + " DECAY " + juce::String (decay) + " s: EDC reaches -60 dB at (s)", t60);
            CHECK_MSG (t60 > 0.0 && t60 <= decay * 1.6 + 0.35, juce::String (algoName (a)) + " " + juce::String (decay));
            CHECK_MSG (t60 > prev, juce::String (algoName (a)) + " " + juce::String (decay) + ": not shorter than the next DECAY");
            prev = t60;
        }
    }
    outputDirectory().getChildFile ("VOID_rt60.csv").replaceWithText (csv);
}

TEST ("VOID: no unstable feedback at maximum settings: every algorithm and colour, DECAY 59.9 s and infinite, 0 dBFS noise: finite, bounded, no hard clip")
{
    const double fs = 48000.0;
    juce::String csv ("algorithm,color,decay,peak,rms_3_6s,rms_last_3s,bound_gain\n");
    for (int a = 0; a < 5; ++a)
        for (int color : { cClean, cRuin })
            for (float decay : { 59.9f, 60.0f })
            {
                auto p = makeVoid (fs, 256);
                setParam (*p, vp::algorithm, (float) a);
                setParam (*p, vp::color, (float) color);
                setParam (*p, vp::decay, decay);
                setParam (*p, vp::size, 100.0f);
                setParam (*p, vp::diffusion, 100.0f);
                setParam (*p, vp::modDepth, 100.0f);
                setParam (*p, vp::modRate, 5.0f);
                setParam (*p, vp::drown, 100.0f);
                setParam (*p, vp::width, 200.0f);
                setParam (*p, vp::mix, 100.0f);
                p->prepareToPlay (fs, 256);
                const int n = (int) (12.0 * fs);
                const auto out = render (*p, noise (2, n, 0.5f, 11), 256);
                const double early = stereoRms (out, (int) (3 * fs), (int) (3 * fs)), late = stereoRms (out, n - (int) (3 * fs), (int) (3 * fs));
                const float peak = out.getMagnitude (0, n);
                const auto tag = juce::String (algoName (a)) + (color == cRuin ? " Ruin" : " Clean") + (decay >= 60.0f ? " infinite" : " 59.9 s");
                csv << algoName (a) << "," << color << "," << decay << "," << peak << "," << early << "," << late << ","
                    << p->getEngineForTesting().getLoopBoundGain() << "\n";
                MEASURE (tag + ": peak / RMS last 3 s (dBFS)", dbOf (late));
                CHECK_MSG (allFinite (out), tag);
                CHECK_MSG (peak < 8.0f, tag + ": peak " + juce::String (peak));
                CHECK_MSG (dbOf (late) < dbOf (early) + 1.0, tag + ": still growing");
                CHECK_MSG (! hasFlatTops (out), tag + ": hard clip");
            }
    outputDirectory().getChildFile ("VOID_max_settings.csv").replaceWithText (csv);
}

TEST ("VOID: FREEZE holds the tail for 2 minutes per algorithm (drift within 0.5 dB, no NaN, no DC); new input passes dry over it, not into it")
{
    const double fs = 48000.0;
    juce::String csv ("algorithm,color,window_s,rms_dbfs\n");
    struct Case { int algo, color; float depth; };
    const Case cases[] = { { aRoom, cClean, 30.0f }, { aPlate, cClean, 30.0f }, { aChamber, cClean, 30.0f }, { aHall, cClean, 30.0f },
                           { aVoid, cClean, 30.0f }, { aVoid, cRuin, 100.0f }, { aHall, cDark, 100.0f } };
    for (const auto& c : cases)
    {
        auto p = makeVoid (fs, 512);
        setParam (*p, vp::algorithm, (float) c.algo);
        setParam (*p, vp::color, (float) c.color);
        setParam (*p, vp::modDepth, c.depth);
        setParam (*p, vp::mix, 100.0f);
        p->prepareToPlay (fs, 512);
        const int block = 512;
        const int total = (int) (120.0 * fs) / block * block;
        const int freezeAt = (int) (3.0 * fs) / block * block;
        juce::MidiBuffer midi;
        juce::Random rng (3);
        std::vector<double> windows;
        double sumW = 0, sumSq = 0, mean = 0;
        int count = 0;
        bool finite = true;
        const int win = (int) (5.0 * fs) / block * block;
        juce::AudioBuffer<float> buf (2, block);
        for (int pos = 0; pos < total; pos += block)
        {
            if (pos == freezeAt)
                setParam (*p, vp::freeze, 1.0f);
            // noise keeps playing 2 s into the freeze: it must not enter the tail
            const bool in = pos < freezeAt + (int) (2.0 * fs);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < block; ++i)
                    buf.setSample (ch, i, in ? 0.1f * (2.0f * rng.nextFloat() - 1.0f) : 0.0f);
            p->processBlock (buf, midi);
            finite = finite && allFinite (buf);
            if (pos >= freezeAt + (int) (10.0 * fs))
            {
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < block; ++i)
                    {
                        const double y = buf.getSample (ch, i);
                        sumSq += y * y;
                        sumW += y;
                    }
                count += 2 * block;
                if (count >= 2 * win)
                {
                    windows.push_back (std::sqrt (sumSq / count));
                    mean += sumW / count;
                    sumSq = sumW = 0;
                    count = 0;
                }
            }
        }
        const auto [mn, mx] = std::minmax_element (windows.begin(), windows.end());
        const double drift = dbOf (*mx) - dbOf (*mn);
        const auto tag = juce::String (algoName (c.algo)) + (c.color == cRuin ? " Ruin" : (c.color == cDark ? " Dark" : " Clean")) + " MOD "
                         + juce::String (c.depth);
        for (size_t w = 0; w < windows.size(); ++w)
            csv << algoName (c.algo) << "," << c.color << "," << (13.0 + 5.0 * (double) w + 2.5) << "," << dbOf (windows[w]) << "\n";
        MEASURE (tag + ": frozen level (dBFS RMS)", dbOf (windows.front()));
        MEASURE (tag + ": level drift over 107 s (dB, max - min of 5 s windows)", drift);
        MEASURE (tag + ": mean (DC) over the frozen 107 s re RMS", std::abs (mean / (double) windows.size()) / windows.front());
        CHECK_MSG (finite, tag);
        CHECK_MSG (drift < 0.5, tag + ": drift " + juce::String (drift, 3) + " dB");
        CHECK_MSG (dbOf (windows.front()) > -40.0, tag + ": nothing frozen");
        // no DC accumulates: the network input is DC blocked and the frozen loop is
        // linear and odd-symmetric; what remains is the sub-audio part of the
        // frozen noise itself (< -40 dB re the frozen RMS)
        CHECK_MSG (std::abs (mean / (double) windows.size()) < 1.0e-2 * windows.front(), tag + ": DC");
        CHECK (p->isFrozen());
        CHECK (p->getEngineForTesting().getFreezeGate() == 0.0);
        CHECK (p->getEngineForTesting().getLossRate() == 0.0);
    }
    outputDirectory().getChildFile ("VOID_freeze.csv").replaceWithText (csv);

    // New input over the frozen tail: identical wet whatever is played; the dry passes through MIX.
    for (int a = 0; a < 5; ++a)
    {
        auto renderWith = [&] (bool playSine, float mix)
        {
            auto p = makeVoid (fs, 256);
            setParam (*p, vp::algorithm, (float) a);
            setParam (*p, vp::mix, mix);
            p->prepareToPlay (fs, 256);
            const int n = (int) (4.0 * fs);
            auto in = noise (2, n, 0.1f, 5);
            const int fz = (int) (1.0 * fs) / 256 * 256;
            // FREEZE closes the input gate in two 15 ms stages; the new phrase starts
            // once it is shut (0.5 s later) and must leave the frozen wet untouched
            const int newAt = fz + (int) (0.5 * fs);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = fz; i < n; ++i)
                    in.setSample (ch, i, playSine && i >= newAt ? 0.5f * (float) std::sin (2.0 * pi * 1000.0 * i / fs) : 0.0f);
            juce::AudioBuffer<float> io (in);
            juce::MidiBuffer midi;
            for (int pos = 0; pos < n; pos += 256)
            {
                if (pos == fz) setParam (*p, vp::freeze, 1.0f);
                juce::AudioBuffer<float> view (io.getArrayOfWritePointers(), 2, pos, 256);
                p->processBlock (view, midi);
            }
            return std::pair { io, in };
        };
        const auto [quiet, inQ] = renderWith (false, 100.0f);
        const auto [played, inP] = renderWith (true, 100.0f);
        double d = 0;
        for (int ch = 0; ch < 2; ++ch)
            for (int i = (int) (2.0 * fs); i < quiet.getNumSamples(); ++i)
                d = std::max (d, (double) std::abs (quiet.getSample (ch, i) - played.getSample (ch, i)));
        MEASURE (juce::String (algoName (a)) + ": frozen wet with and without new input, max difference", d);
        CHECK_MSG (d == 0.0, juce::String (algoName (a)) + ": new input entered the frozen tail");
        // MIX 50 %: the new input is heard dry (x 0.5) on top of the frozen tail
        const auto [q50, i50] = renderWith (false, 50.0f);
        const auto [p50, ip50] = renderWith (true, 50.0f);
        double e = 0;
        for (int i = (int) (2.0 * fs); i < q50.getNumSamples(); ++i)
            e = std::max (e, (double) std::abs ((p50.getSample (0, i) - q50.getSample (0, i)) - 0.5f * ip50.getSample (0, i)));
        CHECK_MSG (e < 1.0e-6, juce::String (algoName (a)) + ": the dry does not pass over the frozen tail");
    }

    // Release: the tail decays again at DECAY (Hall, 2.5 s)
    {
        auto p = makeVoid (fs, 512);
        measurementSetup (*p, aHall, 2.5f);
        p->prepareToPlay (fs, 512);
        const int n = (int) (8.0 * fs);
        auto in = noise (2, n, 0.1f, 9);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = (int) fs; i < n; ++i) in.setSample (ch, i, 0.0f);
        juce::AudioBuffer<float> io (in);
        juce::MidiBuffer midi;
        for (int pos = 0; pos < n; pos += 512)
        {
            if (pos == 512 * 90) setParam (*p, vp::freeze, 1.0f);
            if (pos == 512 * 470) setParam (*p, vp::freeze, 0.0f);
            juce::AudioBuffer<float> view (io.getArrayOfWritePointers(), 2, pos, 512);
            p->processBlock (view, midi);
        }
        // decay rate in the 1 kHz octave (the DECAY reference band)
        juce::AudioBuffer<float> band (2, n);
        for (int ch = 0; ch < 2; ++ch)
        {
            const auto b = octaveBand (io.getReadPointer (ch), n, fs, 1000.0);
            for (int i = 0; i < n; ++i) band.setSample (ch, i, (float) b[(size_t) i]);
        }
        const double a1 = dbOf (stereoRms (band, (int) (5.3 * fs), (int) (0.4 * fs)));
        const double a2 = dbOf (stereoRms (band, (int) (6.3 * fs), (int) (0.4 * fs)));
        MEASURE ("Hall 2.5 s after FREEZE release: decay rate at 1 kHz (dB/s)", a2 - a1);
        CHECK_NEAR (a2 - a1, -60.0 / 2.5, 3.0);
        CHECK (! p->isFrozen());
    }
}

TEST ("VOID: DECAY at its maximum is infinite: lossless loop, input keeps entering, loop energy bounded transparently for 2 minutes (no runaway, no hard clip)")
{
    const double fs = 48000.0;
    juce::String csv ("algorithm,window_s,rms_dbfs,bound_gain\n");
    for (int a = 0; a < 5; ++a)
    {
        auto p = makeVoid (fs, 512);
        setParam (*p, vp::algorithm, (float) a);
        setParam (*p, vp::decay, 60.0f);
        setParam (*p, vp::mix, 100.0f);
        p->prepareToPlay (fs, 512);
        CHECK (p->getEffectiveSettings().decayInfinite && std::isinf (p->getEffectiveSettings().decaySeconds));
        juce::MidiBuffer midi;
        juce::Random rng (21);
        juce::AudioBuffer<float> buf (2, 512);
        std::vector<double> win;
        double sq = 0;
        int cnt = 0;
        float peak = 0;
        bool finite = true, flat = false;
        const int total = (int) (120.0 * fs) / 512 * 512;
        for (int pos = 0; pos < total; pos += 512)
        {
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 512; ++i)
                    buf.setSample (ch, i, 0.25f * (2.0f * rng.nextFloat() - 1.0f)); // -15 dBFS RMS, continuous
            p->processBlock (buf, midi);
            finite = finite && allFinite (buf);
            flat = flat || hasFlatTops (buf);
            peak = std::max (peak, buf.getMagnitude (0, 512));
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 512; ++i) sq += (double) buf.getSample (ch, i) * buf.getSample (ch, i);
            cnt += 1024;
            if (cnt >= (int) (10.0 * fs) * 2)
            {
                win.push_back (std::sqrt (sq / cnt));
                csv << algoName (a) << "," << 10.0 * (double) win.size() << "," << dbOf (win.back()) << ","
                    << p->getEngineForTesting().getLoopBoundGain() << "\n";
                sq = 0;
                cnt = 0;
            }
        }
        const double bound = p->getEngineForTesting().getLoopBoundGain();
        double spread = 0;
        for (size_t w = 6; w < win.size(); ++w)
            spread = std::max (spread, std::abs (dbOf (win[w]) - dbOf (win[5])));
        const auto tag = juce::String (algoName (a));
        MEASURE (tag + ": infinite decay, continuous input: level 50-120 s (dBFS RMS)", dbOf (win.back()));
        MEASURE (tag + ": level change 60 s -> 120 s (dB)", spread);
        MEASURE (tag + ": loop bound gain", bound);
        MEASURE (tag + ": output peak", peak);
        CHECK_MSG (finite, tag);
        CHECK_MSG (! flat, tag + ": hard clip");
        CHECK_MSG (peak < 4.0f, tag);
        CHECK_MSG (spread < 1.5, tag + ": not settled");
        CHECK_MSG (bound < 1.0, tag + ": the bound did not engage"); // the loop really is lossless: the bound holds it
    }
    outputDirectory().getChildFile ("VOID_infinite.csv").replaceWithText (csv);

    // Without new input the infinite tail holds its level (lossless loop)
    for (int a = 0; a < 5; ++a)
    {
        auto p = makeVoid (fs, 512);
        setParam (*p, vp::algorithm, (float) a);
        setParam (*p, vp::decay, 60.0f);
        setParam (*p, vp::mix, 100.0f);
        p->prepareToPlay (fs, 512);
        const int n = (int) (25.0 * fs);
        auto in = noise (2, n, 0.05f, 4);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = (int) fs; i < n; ++i) in.setSample (ch, i, 0.0f);
        const auto out = render (*p, in, 512);
        const double l1 = dbOf (stereoRms (out, (int) (4 * fs), (int) (3 * fs))), l2 = dbOf (stereoRms (out, n - (int) (3 * fs), (int) (3 * fs)));
        MEASURE (juce::String (algoName (a)) + ": infinite decay without input, level change 4 s -> 22 s (dB)", l2 - l1);
        CHECK_MSG (std::abs (l2 - l1) < 0.5, algoName (a));
    }
}

namespace
{
    // Complex spectrum (zero padded to 2^order) of a real signal.
    std::vector<std::complex<double>> spectrumOf (const float* x, int n, int order)
    {
        const int size = 1 << order;
        juce::dsp::FFT fft (order);
        std::vector<float> buf ((size_t) size * 2, 0.0f);
        for (int i = 0; i < std::min (n, size); ++i)
            buf[(size_t) i] = x[i];
        fft.performRealOnlyForwardTransform (buf.data());
        std::vector<std::complex<double>> s ((size_t) size / 2 + 1);
        for (size_t k = 0; k < s.size(); ++k)
            s[k] = { buf[2 * k], buf[2 * k + 1] };
        return s;
    }

    int fftOrderFor (int n) { int o = 10; while ((1 << o) < n) ++o; return o; }

    // Engine-level render (exact settings, no parameter quantisation): stereo in -> wet stereo out.
    juce::AudioBuffer<float> renderEngine (voidrev::VoidEngine& e, const juce::AudioBuffer<float>& in, int block)
    {
        juce::AudioBuffer<float> out (2, in.getNumSamples());
        for (int pos = 0; pos < in.getNumSamples(); pos += block)
        {
            const int m = std::min (block, in.getNumSamples() - pos);
            e.process (in.getReadPointer (0) + pos, in.getReadPointer (std::min (1, in.getNumChannels() - 1)) + pos,
                       out.getWritePointer (0) + pos, out.getWritePointer (1) + pos, m);
        }
        return out;
    }

    voidrev::Settings measurementSettings (int algo, double decay)
    {
        voidrev::Settings s;
        s.algorithm = algo;
        s.decayS = decay;
        s.preDelayMs = 0.0;
        s.modDepthPct = 0.0;
        return s;
    }

    // Band power (dB) of the wet output: 4th-order octave band, both channels.
    double bandPowerDb (const juce::AudioBuffer<float>& b, double fs, double hz, int from, int len)
    {
        double e = 0;
        for (int ch = 0; ch < b.getNumChannels(); ++ch)
        {
            const auto y = octaveBand (b.getReadPointer (ch), b.getNumSamples(), fs, hz);
            for (int i = from; i < std::min (from + len, b.getNumSamples()); ++i)
                e += y[(size_t) i] * y[(size_t) i];
        }
        return 10.0 * std::log10 (std::max (e / std::max (1, len * b.getNumChannels()), 1e-30));
    }

    double bandRt60 (const juce::AudioBuffer<float>& b, double fs, double hz, double hi = -5.0, double lo = -35.0)
    {
        const int n = b.getNumSamples();
        const auto bl = octaveBand (b.getReadPointer (0), n, fs, hz);
        const auto br = octaveBand (b.getReadPointer (1), n, fs, hz);
        std::vector<double> h ((size_t) n);
        for (size_t i = 0; i < h.size(); ++i)
            h[i] = std::sqrt (0.5 * (bl[i] * bl[i] + br[i] * br[i]));
        return rt60FromEdc (edc (h), fs, hi, lo);
    }
}

TEST ("VOID: PRE-DELAY is exact: integer delays shift the wet response sample for sample, fractional ones within 0.02 sample, 44.1-192 kHz (CSV)")
{
    juce::String csv ("rate,algorithm,pre_delay_ms,expected_samples,measured_samples,error_samples,max_shift_difference\n");
    double worst = 0.0;
    for (double fs : { 44100.0, 48000.0, 96000.0, 192000.0 })
        for (int a : { aRoom, aPlate, aHall })
        {
            auto renderPre = [&] (double ms)
            {
                voidrev::VoidEngine e;
                auto s = measurementSettings (a, 0.2);
                s.preDelayMs = ms;
                e.setSettings (s);
                e.prepare (fs, 512);
                const int n = (int) (1.0 * fs);
                return std::pair { renderEngine (e, impulse (2, n), 512), e.getPreDelaySamples() };
            };
            const auto [ref, d0] = renderPre (0.0);
            CHECK (d0 == 0.0);
            const int n = ref.getNumSamples();
            const int order = fftOrderFor (n);
            const auto r0 = spectrumOf (ref.getReadPointer (0), n, order);
            for (double ms : { 1.0, 10.0, 42.0, 10.0104, 33.3333, 250.0 })
            {
                const auto [out, d] = renderPre (ms);
                const double expected = ms * 0.001 * fs;
                CHECK_NEAR (d, expected, 1e-9);
                // integer delays: the response is the reference shifted, sample for sample
                double shiftDiff = -1.0;
                if (std::abs (expected - std::round (expected)) < 1e-9)
                {
                    const int D = (int) std::round (expected);
                    shiftDiff = 0.0;
                    for (int ch = 0; ch < 2; ++ch)
                        for (int i = 0; i + D < n; ++i)
                            shiftDiff = std::max (shiftDiff, (double) std::abs (out.getSample (ch, i + D) - ref.getSample (ch, i)));
                    for (int ch = 0; ch < 2; ++ch)
                        for (int i = 0; i < D; ++i)
                            shiftDiff = std::max (shiftDiff, (double) std::abs (out.getSample (ch, i)));
                    CHECK_MSG (shiftDiff == 0.0, juce::String (fs) + " " + algoName (a) + " " + juce::String (ms) + " ms: not an exact shift");
                }
                // every delay: phase slope of the cross spectrum, 50 Hz .. 2 kHz
                const auto rp = spectrumOf (out.getReadPointer (0), n, order);
                const double T0 = std::floor (expected);
                double sw = 0, swx = 0;
                for (size_t k = 1; k < r0.size(); ++k)
                {
                    const double hz = (double) k * fs / (1 << order);
                    if (hz < 50.0 || hz > 2000.0) continue;
                    const double w = 2.0 * pi * (double) k / (1 << order);
                    const auto x = rp[k] * std::conj (r0[k]) * std::polar (1.0, w * T0);
                    const double mag = std::abs (x);
                    if (mag <= 0.0) continue;
                    swx += mag * (-std::arg (x) / w);
                    sw += mag;
                }
                const double measured = T0 + swx / sw;
                const double err = measured - expected;
                worst = std::max (worst, std::abs (err));
                csv << fs << "," << algoName (a) << "," << ms << "," << expected << "," << measured << "," << err << "," << shiftDiff << "\n";
                CHECK_MSG (std::abs (err) < 0.02, juce::String (fs) + " " + algoName (a) + " " + juce::String (ms) + " ms: " + juce::String (err, 4) + " samples");
            }
        }
    MEASURE ("worst pre-delay error (samples)", worst);
    outputDirectory().getChildFile ("VOID_predelay.csv").replaceWithText (csv);
}

TEST ("VOID: DROWN is a macro over SIZE, DECAY, DIFFUSION, MOD DEPTH, WIDTH and LPF: base values preserved, DROWN 0 = no macro, effective values drive the DSP (CSV)")
{
    const double fs = 48000.0;
    auto p = makeVoid (fs, 512);
    setParam (*p, vp::size, 40.0f);
    setParam (*p, vp::decay, 1.5f);
    setParam (*p, vp::diffusion, 50.0f);
    setParam (*p, vp::modDepth, 20.0f);
    setParam (*p, vp::width, 100.0f);
    setParam (*p, vp::lpf, 16000.0f);
    const float base[] = { plainValue (*p, vp::size), plainValue (*p, vp::decay), plainValue (*p, vp::diffusion), plainValue (*p, vp::modDepth),
                           plainValue (*p, vp::width), plainValue (*p, vp::lpf) };
    auto e0 = p->getEffectiveSettings();
    // DROWN 0: the effective values are the base values, exactly
    CHECK (e0.sizePct == (double) base[0] && e0.decaySeconds == (double) base[1] && e0.diffusionPct == (double) base[2]);
    CHECK (e0.modDepthPct == (double) base[3] && e0.widthPct == (double) base[4] && e0.lpfHz == (double) base[5]);

    juce::String csv ("drown_pct,size_pct,decay_s,diffusion_pct,mod_depth_pct,width_pct,lpf_hz\n");
    voidrev::EffectiveSettings prev = e0;
    for (int d = 0; d <= 100; d += 10)
    {
        setParam (*p, vp::drown, (float) d);
        const auto e = p->getEffectiveSettings();
        csv << d << "," << e.sizePct << "," << e.decaySeconds << "," << e.diffusionPct << "," << e.modDepthPct << "," << e.widthPct << "," << e.lpfHz << "\n";
        if (d > 0)
        {
            // coordinated and monotonic: everything grows, the LPF falls
            CHECK (e.sizePct > prev.sizePct && e.decaySeconds > prev.decaySeconds && e.diffusionPct > prev.diffusionPct);
            CHECK (e.modDepthPct > prev.modDepthPct && e.widthPct > prev.widthPct && e.lpfHz < prev.lpfHz);
        }
        prev = e;
        // the user's base values are never written
        CHECK (plainValue (*p, vp::size) == base[0] && plainValue (*p, vp::decay) == base[1] && plainValue (*p, vp::diffusion) == base[2]);
        CHECK (plainValue (*p, vp::modDepth) == base[3] && plainValue (*p, vp::width) == base[4] && plainValue (*p, vp::lpf) == base[5]);
    }
    outputDirectory().getChildFile ("VOID_drown.csv").replaceWithText (csv);
    // full DROWN: the calibrated end points (MODEL_SPEC 9)
    const auto e1 = p->getEffectiveSettings();
    CHECK_NEAR (e1.sizePct, base[0] + (100.0 - base[0]) * 0.6, 1e-6);
    CHECK_NEAR (e1.decaySeconds, base[1] * 4.0, 1e-5);
    CHECK_NEAR (e1.diffusionPct, base[2] + (100.0 - base[2]) * 0.8, 1e-6);
    CHECK_NEAR (e1.modDepthPct, base[3] + (100.0 - base[3]) * 0.5, 1e-6);
    CHECK_NEAR (e1.widthPct, base[4] + (200.0 - base[4]) * 0.25, 1e-6);
    CHECK_NEAR (e1.lpfHz, base[5] * std::exp2 (-0.75), 1e-2);
    MEASURE ("DROWN 100: effective DECAY / base", e1.decaySeconds / base[1]);
    // the macro does not overflow the ranges; infinite stays infinite
    setParam (*p, vp::decay, 30.0f);
    CHECK_NEAR (p->getEffectiveSettings().decaySeconds, 60.0, 1e-9);
    setParam (*p, vp::decay, 60.0f);
    CHECK (std::isinf (p->getEffectiveSettings().decaySeconds));
    setParam (*p, vp::decay, 1.5f);
    // back to 0: exactly the base values again
    setParam (*p, vp::drown, 0.0f);
    const auto e2 = p->getEffectiveSettings();
    CHECK (e2.sizePct == e0.sizePct && e2.decaySeconds == e0.decaySeconds && e2.lpfHz == e0.lpfHz && e2.widthPct == e0.widthPct);

    // the DSP runs on the effective values: RT60 follows the effective DECAY (DROWN 50 -> c = 0.5 -> x2)
    {
        auto q = makeVoid (fs, 512);
        measurementSetup (*q, aHall, 1.5f);
        setParam (*q, vp::drown, 50.0f);
        q->prepareToPlay (fs, 512);
        const double eff = q->getEffectiveSettings().decaySeconds;
        CHECK_NEAR (eff, 3.0, 1e-5);
        const double rt = measureRt60 (*q, fs, 0.75 * eff + 0.9);
        MEASURE ("DROWN 50, DECAY 1.5 s: RT60 (s), effective 3 s", rt);
        CHECK_NEAR (rt / eff, 1.0, 0.05);
        // the filter display follows the effective LPF
        CHECK_NEAR (q->filterResponseDb (12000.0), vf::hpLpResponseDb (12000.0, 20.0, q->getEffectiveSettings().lpfHz, fs), 1e-9);
        CHECK (q->filterResponseDb (12000.0) < -0.3);
    }
    // DROWN 0 renders exactly what an instance without the macro renders
    {
        auto q = makeVoid (fs, 256), r = makeVoid (fs, 256);
        setParam (*q, vp::drown, 70.0f);
        setParam (*q, vp::drown, 0.0f);
        q->prepareToPlay (fs, 256);
        r->prepareToPlay (fs, 256);
        const auto sig = noise (2, (int) (0.5 * fs), 0.1f, 3);
        CHECK (identical (render (*q, sig, 256), render (*r, sig, 256)));
    }
}

TEST ("VOID: filter responses (addendum 26): HPF and LPF at min, mid, max and musical settings, PRE and POST: measured = filterResponseDb, -3 dB at the cutoff, open end stops (CSV)")
{
    const double fs = 48000.0;
    juce::String csv ("mode,hpf_hz,lpf_hz,freq_hz,expected_db,measured_db,error_db\n");
    struct Set { float hpf, lpf; const char* what; };
    const Set sets[] = { { 20.0f, 20000.0f, "both open (min HPF, max LPF)" }, { 141.42f, 20000.0f, "HPF mid" }, { 1000.0f, 20000.0f, "HPF max" },
                         { 80.0f, 20000.0f, "HPF 80 (musical)" }, { 250.0f, 20000.0f, "HPF 250 (musical)" }, { 20.0f, 1000.0f, "LPF min" },
                         { 20.0f, 4472.1f, "LPF mid" }, { 20.0f, 6000.0f, "LPF 6 k (musical)" }, { 20.0f, 12000.0f, "LPF 12 k (musical)" },
                         { 120.0f, 8500.0f, "band 120 - 8.5 k (musical)" }, { 22.0f, 18000.0f, "inside the open zones" } };
    double worst = 0.0;
    for (int mode : { 0, 2 })
    {
        auto renderWith = [&] (float hpf, float lpf)
        {
            auto p = makeVoid (fs, 512);
            measurementSetup (*p, aHall, 0.3f);
            setParam (*p, vp::filterMode, (float) mode);
            setParam (*p, vp::hpf, hpf);
            setParam (*p, vp::lpf, lpf);
            p->prepareToPlay (fs, 512);
            const int n = (int) (0.65 * fs);
            return std::pair { render (*p, impulse (2, n), 512), std::move (p) };
        };
        auto [ref, pr] = renderWith (20.0f, 20000.0f);
        const int n = ref.getNumSamples();
        const int order = fftOrderFor (n);
        const auto r0 = spectrumOf (ref.getReadPointer (0), n, order);
        double peak0 = 0;
        for (auto& c : r0) peak0 = std::max (peak0, std::abs (c));
        for (const auto& s : sets)
        {
            auto [out, p] = renderWith (s.hpf, s.lpf);
            const auto r1 = spectrumOf (out.getReadPointer (0), n, order);
            const double hpf = plainValue (*p, vp::hpf), lpf = plainValue (*p, vp::lpf);
            double maxErr = 0.0;
            int logged = 0;
            for (size_t k = 1; k < r0.size(); ++k)
            {
                const double hz = (double) k * fs / (1 << order);
                if (hz < 20.0 || hz > 20000.0 || std::abs (r0[k]) < 1e-3 * peak0) continue;
                const double expected = p->filterResponseDb (hz);
                if (expected < -40.0) continue;
                const double measured = 20.0 * std::log10 (std::abs (r1[k]) / std::abs (r0[k]));
                maxErr = std::max (maxErr, std::abs (measured - expected));
                if ((k % 64) == 0 && logged++ < 400)
                    csv << (mode == 0 ? "Pre" : "Post") << "," << hpf << "," << lpf << "," << hz << "," << expected << "," << measured << "," << measured - expected << "\n";
            }
            worst = std::max (worst, maxErr);
            MEASURE (juce::String (mode == 0 ? "PRE " : "POST ") + s.what + ": max |measured - expected| (dB)", maxErr);
            CHECK_MSG (maxErr < 0.05, juce::String (s.what) + ": " + juce::String (maxErr, 4) + " dB");
            // the design: 12 dB/oct Butterworth, -3.01 dB at the set cutoff (outside the open zones)
            if (hpf >= 25.0) CHECK_NEAR (p->filterResponseDb (hpf), -3.0103, 0.01);
            if (lpf <= 16000.0) CHECK_NEAR (p->filterResponseDb (lpf), -3.0103, 0.01);
            if (hpf >= 200.0) CHECK_NEAR (p->filterResponseDb (hpf / 4.0), -24.1, 0.3);
            if (lpf <= 5000.0) CHECK (p->filterResponseDb (lpf * 4.0) < -23.0);
        }
        // open end stops: transparent at every frequency
        double openMax = 0;
        for (double hz = 10.0; hz < 0.5 * fs; hz *= 1.05)
            openMax = std::max (openMax, std::abs (pr->filterResponseDb (hz)));
        MEASURE ("open end stops: max |response| 10 Hz - Nyquist (dB)", openMax);
        CHECK (openMax < 1e-3);
    }
    MEASURE ("worst filter error (dB)", worst);
    // the evaluator is continuous through the open zones (no step on the knob)
    double maxStep = 0;
    for (double f = 18.0; f < 30.0; f *= 1.002)
        maxStep = std::max (maxStep, std::abs (vf::hpLpResponseDb (40.0, f * 1.002, 20000.0, fs) - vf::hpLpResponseDb (40.0, f, 20000.0, fs)));
    for (double f = 14000.0; f < 20000.0; f *= 1.002)
        maxStep = std::max (maxStep, std::abs (vf::hpLpResponseDb (12000.0, 20.0, f * 1.002, fs) - vf::hpLpResponseDb (12000.0, 20.0, f, fs)));
    MEASURE ("largest response change per 0.2 % knob step in the open zones (dB)", maxStep);
    CHECK (maxStep < 0.1);
    outputDirectory().getChildFile ("VOID_filter_responses.csv").replaceWithText (csv);
}

TEST ("VOID: FILTER MODE: LOOP shortens the decay outside the pass band every pass, PRE and POST filter once (same response, decay unchanged); FREEZE moves LOOP to POST (CSV)")
{
    const double fs = 48000.0;
    juce::String csv ("mode,rt60_125Hz,rt60_1kHz,rt60_8kHz,level_125Hz_db,level_8kHz_db\n");
    std::map<int, std::array<double, 5>> res;
    std::map<int, juce::AudioBuffer<float>> outs;
    for (int mode : { -1, 0, 1, 2 })
    {
        auto p = makeVoid (fs, 512);
        measurementSetup (*p, aHall, 3.0f);
        if (mode >= 0)
        {
            setParam (*p, vp::filterMode, (float) mode);
            setParam (*p, vp::hpf, 250.0f);
            setParam (*p, vp::lpf, 4000.0f);
        }
        p->prepareToPlay (fs, 512);
        const int n = (int) (3.2 * fs);
        auto out = render (*p, impulse (2, n), 512);
        std::array<double, 5> r { bandRt60 (out, fs, 125.0), bandRt60 (out, fs, 1000.0), bandRt60 (out, fs, 8000.0),
                                  bandPowerDb (out, fs, 125.0, 0, (int) (0.3 * fs)), bandPowerDb (out, fs, 8000.0, 0, (int) (0.3 * fs)) };
        res[mode] = r;
        outs[mode] = std::move (out);
        csv << (mode < 0 ? "open" : (mode == 0 ? "Pre" : (mode == 1 ? "Loop" : "Post"))) << "," << r[0] << "," << r[1] << "," << r[2] << "," << r[3] << "," << r[4] << "\n";
    }
    const auto& o = res[-1];
    for (int mode : { 0, 2 })
    {
        const auto& r = res[mode];
        CHECK_NEAR (r[0] / o[0], 1.0, 0.1); // once: the decay does not change
        CHECK_NEAR (r[2] / o[2], 1.0, 0.1);
        CHECK (r[3] < o[3] - 3.0);           // but the level does
        CHECK (r[4] < o[4] - 3.0);
    }
    const auto& l = res[1];
    MEASURE ("LOOP HPF 250 / LPF 4 k: RT60(125 Hz) re open", l[0] / o[0]);
    MEASURE ("LOOP HPF 250 / LPF 4 k: RT60(1 kHz) re open", l[1] / o[1]);
    MEASURE ("LOOP HPF 250 / LPF 4 k: RT60(8 kHz) re open", l[2] / o[2]);
    CHECK (l[0] < 0.5 * o[0]);              // every pass: the decay itself shortens
    CHECK (l[2] < 0.5 * o[2]);
    CHECK_NEAR (l[1] / o[1], 1.0, 0.1);     // the pass band keeps DECAY
    // PRE and POST: the same response at two places of a linear network
    double d = 0, pk = 0;
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < outs[0].getNumSamples(); ++i)
        {
            d = std::max (d, (double) std::abs (outs[0].getSample (ch, i) - outs[2].getSample (ch, i)));
            pk = std::max (pk, (double) std::abs (outs[0].getSample (ch, i)));
        }
    MEASURE ("PRE vs POST (Clean, no modulation): max difference re peak", d / pk);
    CHECK (d < 1e-4 * pk);
    outputDirectory().getChildFile ("VOID_filter_modes.csv").replaceWithText (csv);

    // FREEZE in LOOP mode: the filters shape the frozen output instead of draining it
    auto frozen = [&] (bool filtered)
    {
        auto p = makeVoid (fs, 512);
        setParam (*p, vp::algorithm, (float) aHall);
        setParam (*p, vp::mix, 100.0f);
        if (filtered)
        {
            setParam (*p, vp::hpf, 400.0f);
            setParam (*p, vp::lpf, 2500.0f);
        }
        p->prepareToPlay (fs, 512);
        const int n = (int) (30.0 * fs);
        auto in = noise (2, n, 0.1f, 8);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = (int) (2.0 * fs); i < n; ++i) in.setSample (ch, i, 0.0f);
        juce::AudioBuffer<float> io (in);
        juce::MidiBuffer midi;
        for (int pos = 0; pos < n; pos += 512)
        {
            if (pos == 512 * 180) setParam (*p, vp::freeze, 1.0f);
            juce::AudioBuffer<float> view (io.getArrayOfWritePointers(), 2, pos, std::min (512, n - pos));
            p->processBlock (view, midi);
        }
        return io;
    };
    const auto io = frozen (true), open = frozen (false);
    const int n = io.getNumSamples();
    const double a = dbOf (stereoRms (io, (int) (10 * fs), (int) (4 * fs))), b = dbOf (stereoRms (io, n - (int) (4 * fs), (int) (4 * fs)));
    MEASURE ("FREEZE with LOOP HPF 400 / LPF 2.5 k: level change 12 s -> 28 s (dB)", b - a);
    CHECK (std::abs (b - a) < 0.5);
    CHECK (a > -40.0);
    // and the frozen output is band limited by them (POST): bands re 1 kHz, filtered vs open
    auto rel = [&] (const juce::AudioBuffer<float>& x, double hz)
    { return bandPowerDb (x, fs, hz, (int) (10 * fs), (int) (4 * fs)) - bandPowerDb (x, fs, 1000.0, (int) (10 * fs), (int) (4 * fs)); };
    MEASURE ("FREEZE with LOOP filters: frozen 8 kHz band re open (dB, relative to 1 kHz)", rel (io, 8000.0) - rel (open, 8000.0));
    MEASURE ("FREEZE with LOOP filters: frozen 125 Hz band re open (dB, relative to 1 kHz)", rel (io, 125.0) - rel (open, 125.0));
    CHECK (rel (io, 8000.0) < rel (open, 8000.0) - 15.0);
    CHECK (rel (io, 125.0) < rel (open, 125.0) - 15.0);
}

namespace
{
    // -3 dB bandwidth of a wet output: the lowest frequency above 2 kHz where the
    // 1/3-octave smoothed long-term spectrum (per Hz) falls 3 dB below its 500 Hz - 2 kHz mean.
    double upperBandwidthHz (const juce::AudioBuffer<float>& b, double fs, int from)
    {
        const int order = 13;
        std::vector<double> acc;
        for (int ch = 0; ch < 2; ++ch)
        {
            const auto s = welchDb (b.getReadPointer (ch) + from, b.getNumSamples() - from, order);
            if (acc.empty()) acc.assign (s.size(), 0.0);
            for (size_t k = 0; k < s.size(); ++k) acc[k] += std::pow (10.0, s[k] / 10.0);
        }
        std::vector<double> db (acc.size());
        for (size_t k = 0; k < acc.size(); ++k) db[k] = 10.0 * std::log10 (std::max (acc[k], 1e-30));
        const double ref = bandDb (db, fs, order, 500.0, 2000.0);
        for (double f = 2000.0; f < 0.49 * fs; f *= 1.02)
            if (bandDb (db, fs, order, f / 1.12, f * 1.12) < ref - 3.0)
                return f;
        return 0.5 * fs;
    }

    // Harmonic analysis of a steady tone: THD (H2..H10 re H1, power) and THD+N
    // (everything but DC and the fundamental, re H1). Hann window, bin-centred tone.
    struct Harmonics { double thd, thdn, h2, h3, h1; };
    Harmonics harmonics (const float* x, int n, double fs, double f0)
    {
        const int order = fftOrderFor (n) - (fftOrderFor (n) > 10 && (1 << fftOrderFor (n)) > n ? 1 : 0);
        const int size = 1 << order;
        juce::dsp::FFT fft (order);
        std::vector<float> buf ((size_t) size * 2, 0.0f);
        for (int i = 0; i < size; ++i)
            buf[(size_t) i] = x[i] * (float) (0.5 - 0.5 * std::cos (2.0 * pi * i / size));
        fft.performFrequencyOnlyForwardTransform (buf.data());
        auto binPower = [&] (double hz)
        {
            const int k = (int) std::lround (hz * size / fs);
            double e = 0;
            for (int j = std::max (1, k - 2); j <= std::min (size / 2 - 1, k + 2); ++j) e += (double) buf[(size_t) j] * buf[(size_t) j];
            return e;
        };
        Harmonics h {};
        h.h1 = binPower (f0);
        double harm = 0, total = 0;
        for (int k = 2; k <= 10 && k * f0 < 0.5 * fs; ++k) harm += binPower (k * f0);
        for (int j = 3; j < size / 2; ++j) total += (double) buf[(size_t) j] * buf[(size_t) j];
        h.thd = std::sqrt (harm / std::max (h.h1, 1e-30));
        h.thdn = std::sqrt (std::max (total - h.h1, 0.0) / std::max (h.h1, 1e-30));
        h.h2 = binPower (2 * f0) / std::max (h.h1, 1e-30);
        h.h3 = binPower (3 * f0) / std::max (h.h1, 1e-30);
        return h;
    }
}

TEST ("VOID: COLOR: CLEAN full bandwidth, DARK band limited, RUIN level-dependent saturation (THD vs level, symmetric, aliasing), small-signal levels matched (addendum 27; CSV)")
{
    const double fs = 48000.0;
    juce::String csv ("algorithm,color,bandwidth_hz,level_1kHz_db\n");
    for (int a = 0; a < 5; ++a)
    {
        double bw[3] {}, lv[3] {};
        for (int c = 0; c < 3; ++c)
        {
            auto p = makeVoid (fs, 512);
            measurementSetup (*p, a, 1.5f);
            setParam (*p, vp::color, (float) c);
            p->prepareToPlay (fs, 512);
            const int n = (int) (5.0 * fs);
            const auto out = render (*p, noise (2, n, 0.01f, 12), 512); // -40 dBFS: RUIN's saturation stays in its linear range
            bw[c] = upperBandwidthHz (out, fs, (int) (1.5 * fs));
            lv[c] = bandPowerDb (out, fs, 1000.0, (int) (1.5 * fs), (int) (3.5 * fs));
            csv << algoName (a) << "," << c << "," << bw[c] << "," << lv[c] << "\n";
        }
        const auto tag = juce::String (algoName (a));
        MEASURE (tag + ": -3 dB bandwidth CLEAN (Hz)", bw[0]);
        MEASURE (tag + ": -3 dB bandwidth DARK (Hz)", bw[1]);
        MEASURE (tag + ": -3 dB bandwidth RUIN (Hz)", bw[2]);
        MEASURE (tag + ": 1 kHz level DARK re CLEAN (dB)", lv[1] - lv[0]);
        MEASURE (tag + ": 1 kHz level RUIN re CLEAN (dB)", lv[2] - lv[0]);
        CHECK_MSG (bw[0] >= 14000.0, tag + ": CLEAN is not full bandwidth");
        CHECK_MSG (bw[1] <= 9000.0, tag + ": DARK is not band limited");
        CHECK_MSG (bw[1] < 0.6 * bw[0], tag);
        CHECK_MSG (std::abs (lv[1] - lv[0]) < 1.5 && std::abs (lv[2] - lv[0]) < 1.5, tag + ": colours not level matched");
    }
    outputDirectory().getChildFile ("VOID_color.csv").replaceWithText (csv);

    // THD and THD+N vs level, per colour (Hall, DECAY 1 s, no modulation): tones
    // at five frequencies, power averaged (a single frequency may sit in a notch
    // of the reverb's response).
    juce::String thdCsv ("color,level_dbfs,thd_pct,thdn_pct,h2_db,h3_db\n");
    const int order = 14, size = 1 << order;
    const double tones[] = { 47.0, 61.0, 83.0, 101.0, 127.0 }; // bins: 137 .. 372 Hz
    std::map<int, std::vector<double>> thdByColor;
    for (int c = 0; c < 3; ++c)
        for (double lvl : { -40.0, -30.0, -20.0, -12.0, -6.0, 0.0 })
        {
            double h1 = 0, hs = 0, hn = 0, h2 = 0, h3 = 0;
            for (double bin : tones)
            {
                const double f0 = bin * fs / size;
                auto p = makeVoid (fs, 512);
                measurementSetup (*p, aHall, 1.0f);
                setParam (*p, vp::color, (float) c);
                p->prepareToPlay (fs, 512);
                const int n = (int) (1.5 * fs) + size;
                const auto out = render (*p, toBuffer (sine (fs, f0, (float) std::pow (10.0, lvl / 20.0), n), 2), 512);
                for (int ch = 0; ch < 2; ++ch)
                {
                    const auto hm = harmonics (out.getReadPointer (ch) + n - size, size, fs, f0);
                    h1 += hm.h1;
                    hs += hm.thd * hm.thd * hm.h1;
                    hn += hm.thdn * hm.thdn * hm.h1;
                    h2 += hm.h2 * hm.h1;
                    h3 += hm.h3 * hm.h1;
                }
            }
            const double thd = std::sqrt (hs / h1), thdn = std::sqrt (hn / h1);
            thdByColor[c].push_back (thd);
            thdCsv << c << "," << lvl << "," << 100.0 * thd << "," << 100.0 * thdn << "," << 10.0 * std::log10 (std::max (h2 / h1, 1e-30)) << ","
                   << 10.0 * std::log10 (std::max (h3 / h1, 1e-30)) << "\n";
            const char* cn[] = { "CLEAN", "DARK", "RUIN" };
            MEASURE (juce::String (cn[c]) + " " + juce::String (lvl) + " dBFS: THD (%)", 100.0 * thd);
            if (c == cRuin)
            {
                MEASURE (juce::String ("RUIN ") + juce::String (lvl) + " dBFS: THD+N (%)", 100.0 * thdn);
                if (lvl >= -12.0)
                    CHECK_MSG (h2 < 0.01 * h3, "RUIN is not symmetric (even harmonics) at " + juce::String (lvl)); // odd transfer curve
            }
            else
                CHECK_MSG (thd < 1e-3, juce::String (cn[c]) + " distorts at " + juce::String (lvl) + " dBFS");
        }
    const auto& r = thdByColor[cRuin];
    for (size_t i = 1; i < r.size(); ++i)
        CHECK_MSG (r[i] > r[i - 1], "RUIN THD does not rise with level"); // level dependent
    CHECK (r.back() > 0.01);       // audible at full level
    CHECK (r.front() < 0.003);     // nearly clean when quiet
    outputDirectory().getChildFile ("VOID_ruin_thd.csv").replaceWithText (thdCsv);

    // aliasing inspection (no oversampling, MODEL_SPEC 8.3): a 7.1 kHz tone at -6 dBFS through RUIN;
    // energy away from the tone's harmonics (below Nyquist) re the fundamental
    {
        const double f0 = 1515.0 * fs / size; // 4.44 kHz, bin centred
        auto p = makeVoid (fs, 512);
        measurementSetup (*p, aHall, 1.0f);
        setParam (*p, vp::color, (float) cRuin);
        p->prepareToPlay (fs, 512);
        const int n = (int) (1.5 * fs) + size;
        const auto out = render (*p, toBuffer (sine (fs, f0, 0.5f, n), 2), 512);
        juce::dsp::FFT fft (order);
        std::vector<float> buf ((size_t) size * 2, 0.0f);
        for (int i = 0; i < size; ++i)
            buf[(size_t) i] = out.getSample (0, n - size + i) * (float) (0.5 - 0.5 * std::cos (2.0 * pi * i / size));
        fft.performFrequencyOnlyForwardTransform (buf.data());
        double fund = 0, alias = 0;
        for (int k = 3; k < size / 2; ++k)
        {
            const double hz = (double) k * fs / size;
            const double e = (double) buf[(size_t) k] * buf[(size_t) k];
            const double ratio = hz / f0;
            if (std::abs (ratio - 1.0) * f0 < 4.0 * fs / size) fund += e;
            else if (std::abs (ratio - std::round (ratio)) * f0 > 4.0 * fs / size) alias += e;
        }
        MEASURE ("RUIN, 4.44 kHz at -6 dBFS: inharmonic (aliased) energy re fundamental (dB)", 10.0 * std::log10 (alias / fund));
        CHECK (10.0 * std::log10 (alias / fund) < -40.0);
    }
}

TEST ("VOID: DUCK: the dry input attenuates the wet (24 dB at 100 %, 12 dB at 50 %), 10 ms attack, recovers after the input stops; quiet input does not duck (CSV)")
{
    const double fs = 48000.0;
    const int n = (int) (2.6 * fs);
    // 1 s of noise at -12 dBFS RMS (peaks well above -20 dBFS), then silence
    auto sig = noise (2, n, 0.25f, 5);
    for (int ch = 0; ch < 2; ++ch)
        for (int i = (int) fs; i < n; ++i) sig.setSample (ch, i, 0.0f);
    auto renderDuck = [&] (float duck, float gainIn, std::vector<double>* attDb)
    {
        auto p = makeVoid (fs, 64);
        setParam (*p, vp::mix, 100.0f);
        setParam (*p, vp::modDepth, 0.0f);
        setParam (*p, vp::duck, duck);
        p->prepareToPlay (fs, 64);
        juce::AudioBuffer<float> io (sig);
        io.applyGain (gainIn);
        juce::MidiBuffer midi;
        for (int pos = 0; pos < n; pos += 64)
        {
            juce::AudioBuffer<float> view (io.getArrayOfWritePointers(), 2, pos, 64);
            p->processBlock (view, midi);
            if (attDb != nullptr) attDb->push_back (p->getEngineForTesting().getDuckAttenuationDb());
        }
        return io;
    };
    std::vector<double> att;
    const auto ref = renderDuck (0.0f, 1.0f, nullptr), full = renderDuck (100.0f, 1.0f, &att), half = renderDuck (50.0f, 1.0f, nullptr);
    // applied gain = ratio of the outputs (the networks are identical; DUCK scales the wet after them)
    auto gainDb = [&] (const juce::AudioBuffer<float>& b, int from, int len)
    { return dbOf (stereoRms (b, from, len) / std::max (stereoRms (ref, from, len), 1e-12)); };
    juce::String csv ("time_ms,applied_gain_db,reported_attenuation_db\n");
    for (int w = 0; w < (int) (1.6 * fs) / 480; ++w)
        csv << w * 10 << "," << gainDb (full, w * 480, 480) << "," << att[(size_t) std::min ((int) att.size() - 1, (w * 480 + 479) / 64)] << "\n";
    outputDirectory().getChildFile ("VOID_duck.csv").replaceWithText (csv);
    const double g100 = gainDb (full, (int) (0.3 * fs), (int) (0.6 * fs)), g50 = gainDb (half, (int) (0.3 * fs), (int) (0.6 * fs));
    MEASURE ("DUCK 100 %: wet gain while playing (dB)", g100);
    MEASURE ("DUCK 50 %: wet gain while playing (dB)", g50);
    CHECK_NEAR (g100, -24.0, 1.0);
    CHECK_NEAR (g50, -12.0, 0.75);
    // the reported attenuation is the applied one
    CHECK_NEAR (-att[(size_t) ((int) (0.6 * fs) / 64)], gainDb (full, (int) (0.6 * fs) - 32, 64), 0.5);
    // attack: most of it within 10 ms x 3
    MEASURE ("DUCK 100 %: wet gain 20-30 ms after the onset (dB)", gainDb (full, (int) (0.020 * fs), (int) (0.010 * fs)));
    CHECK (gainDb (full, (int) (0.020 * fs), (int) (0.010 * fs)) < -15.0);
    // recovery after the input stops (detector 50 ms, release 250 ms)
    // (the detector falls below -20 dBFS ~0.1 s after the input, then the 250 ms release)
    MEASURE ("DUCK 100 %: wet gain 0.8-0.9 s after the input stopped (dB)", gainDb (full, (int) (1.8 * fs), (int) (0.1 * fs)));
    MEASURE ("DUCK 100 %: wet gain 1.4-1.5 s after the input stopped (dB)", gainDb (full, (int) (2.4 * fs), (int) (0.1 * fs)));
    CHECK (gainDb (full, (int) (1.8 * fs), (int) (0.1 * fs)) > -2.5);
    CHECK (gainDb (full, (int) (2.4 * fs), (int) (0.1 * fs)) > -0.5);
    // quiet input (peaks below -45 dBFS) does not duck
    std::vector<double> attQuiet;
    renderDuck (100.0f, 0.002f, &attQuiet);
    CHECK (*std::max_element (attQuiet.begin(), attQuiet.end()) < 0.01);
}

TEST ("VOID: WIDTH 0 = mono wet, WIDTH changes only the side, mono compatibility of every algorithm; mono and mono->stereo layouts")
{
    const double fs = 48000.0;
    const auto sig = noise (2, (int) (1.5 * fs), 0.1f, 6);
    juce::String csv ("algorithm,width_pct,mono_sum_re_stereo_db,lr_correlation\n");
    for (int a = 0; a < 5; ++a)
    {
        std::map<int, juce::AudioBuffer<float>> out;
        for (int w : { 0, 100, 200 })
        {
            auto p = makeVoid (fs, 256);
            setParam (*p, vp::algorithm, (float) a);
            setParam (*p, vp::mix, 100.0f);
            setParam (*p, vp::width, (float) w);
            p->prepareToPlay (fs, 256);
            out[w] = render (*p, sig, 256);
        }
        const auto tag = juce::String (algoName (a));
        // WIDTH 0: L == R, sample for sample
        bool mono = true;
        for (int i = 0; i < out[0].getNumSamples() && mono; ++i)
            mono = sameBits (out[0].getSample (0, i), out[0].getSample (1, i));
        CHECK_MSG (mono, tag + ": WIDTH 0 is not mono");
        // the mid is the same at every WIDTH; the side scales with WIDTH
        double midErr = 0, sideErr = 0, pk = 0;
        for (int i = 0; i < out[100].getNumSamples(); ++i)
        {
            const double m100 = out[100].getSample (0, i) + (double) out[100].getSample (1, i);
            const double s100 = out[100].getSample (0, i) - (double) out[100].getSample (1, i);
            for (int w : { 0, 200 })
            {
                midErr = std::max (midErr, std::abs (out[w].getSample (0, i) + (double) out[w].getSample (1, i) - m100));
                sideErr = std::max (sideErr, std::abs (out[w].getSample (0, i) - (double) out[w].getSample (1, i) - 0.01 * w * s100));
            }
            pk = std::max (pk, std::abs (m100));
        }
        CHECK_MSG (midErr < 1e-5 * pk && sideErr < 1e-5 * pk, tag + ": WIDTH changes more than the side");
        // mono compatibility (natural width): the mono sum keeps the energy of a decorrelated pair
        for (int w : { 100, 200 })
        {
            const auto& b = out[w];
            const int from = (int) (0.3 * fs), len = b.getNumSamples() - from;
            double sl = 0, sr = 0, slr = 0, sm = 0;
            for (int i = from; i < from + len; ++i)
            {
                const double l = b.getSample (0, i), r = b.getSample (1, i);
                sl += l * l; sr += r * r; slr += l * r; sm += 0.25 * (l + r) * (l + r);
            }
            const double monoDb = 10.0 * std::log10 (sm / (0.5 * (sl + sr))), rho = slr / std::sqrt (sl * sr);
            csv << algoName (a) << "," << w << "," << monoDb << "," << rho << "\n";
            MEASURE (tag + " WIDTH " + juce::String (w) + ": mono sum re stereo (dB)", monoDb);
            MEASURE (tag + " WIDTH " + juce::String (w) + ": L/R correlation", rho);
            if (w == 100)
            {
                CHECK_MSG (monoDb > -4.5, tag + ": the mono sum loses too much");
                CHECK_MSG (std::abs (rho) < 0.6, tag + ": L and R not decorrelated");
                // no band cancels in mono
                juce::AudioBuffer<float> m (1, b.getNumSamples()), st (2, b.getNumSamples());
                for (int i = 0; i < b.getNumSamples(); ++i)
                    m.setSample (0, i, 0.5f * (b.getSample (0, i) + b.getSample (1, i)));
                for (double hz : { 125.0, 250.0, 500.0, 1000.0, 2000.0, 4000.0, 8000.0 })
                {
                    const double d = bandPowerDb (m, fs, hz, from, len) - bandPowerDb (b, fs, hz, from, len);
                    CHECK_MSG (d > -8.0, tag + ": mono cancels the " + juce::String (hz) + " Hz band (" + juce::String (d, 1) + " dB)");
                }
            }
        }
    }
    outputDirectory().getChildFile ("VOID_width.csv").replaceWithText (csv);

    // mono -> mono = the mid of mono -> stereo; mono -> stereo = stereo with the input copied
    auto run = [&] (int inCh, int outCh, const juce::AudioBuffer<float>& in)
    {
        auto p = makeVoid (fs, 256, inCh, outCh);
        setParam (*p, vp::algorithm, (float) aVoid);
        setParam (*p, vp::mix, 60.0f);
        p->prepareToPlay (fs, 256);
        return render (*p, in, 256);
    };
    const auto monoIn = toBuffer (sine (fs, 330.0, 0.3f, (int) fs), 1);
    const auto stereoIn = toBuffer (sine (fs, 330.0, 0.3f, (int) fs), 2);
    const auto mm = run (1, 1, monoIn), ms = run (1, 2, stereoIn), ss = run (2, 2, stereoIn);
    CHECK (identical (ms, ss));
    double e = 0;
    for (int i = 0; i < mm.getNumSamples(); ++i)
        e = std::max (e, (double) std::abs (mm.getSample (0, i) - 0.5f * (ms.getSample (0, i) + ms.getSample (1, i))));
    MEASURE ("mono output vs mid of the stereo output: max difference", e);
    CHECK (e < 1e-6);
}

TEST ("VOID: telemetry is real: early/late levels = the components' RMS, silence reads 0, isFrozen, spectrum tap = wet, filterResponseDb follows the running filters, meters")
{
    const double fs = 48000.0;
    for (int a = 0; a < 5; ++a)
    {
        auto p = makeVoid (fs, 512);
        setParam (*p, vp::algorithm, (float) a);
        setParam (*p, vp::mix, 100.0f);
        setParam (*p, vp::duck, 60.0f); // the levels include the DUCK gain
        p->prepareToPlay (fs, 512);
        const auto tag = juce::String (algoName (a));
        // silence: both read 0
        render (*p, silence (2, 4096), 512);
        CHECK_MSG (p->takeEarlyLevel() == 0.0f && p->takeLateLevel() == 0.0f, tag + ": silence does not read 0");
        // no new audio since the last call: the previous value
        CHECK (p->takeEarlyLevel() == 0.0f);
        const auto sig = noise (2, (int) (1.0 * fs), 0.2f, 2);
        // each component soloed: the output is that component; its RMS is the reading
        for (int comp : { 1, 2 })
        {
            auto q = makeVoid (fs, 512);
            setParam (*q, vp::algorithm, (float) a);
            setParam (*q, vp::mix, 100.0f);
            setParam (*q, vp::duck, 60.0f);
            q->prepareToPlay (fs, 512);
            q->getEngineForTesting().setComponentSolo (comp);
            const int n = sig.getNumSamples();
            juce::AudioBuffer<float> io (sig);
            juce::MidiBuffer midi;
            double worst = 0;
            for (int pos = 0; pos < n; pos += 4800)
            {
                juce::AudioBuffer<float> view (io.getArrayOfWritePointers(), 2, pos, std::min (4800, n - pos));
                q->takeEarlyLevel();
                q->takeLateLevel();
                for (int s = 0; s < view.getNumSamples(); s += 512)
                {
                    juce::AudioBuffer<float> sub (view.getArrayOfWritePointers(), 2, s, std::min (512, view.getNumSamples() - s));
                    q->processBlock (sub, midi);
                }
                const double measured = stereoRms (io, pos, view.getNumSamples());
                const double reported = comp == 1 ? q->takeEarlyLevel() : q->takeLateLevel();
                if (measured > 1e-4)
                    worst = std::max (worst, std::abs (reported / measured - 1.0));
            }
            MEASURE (tag + (comp == 1 ? ": early" : ": late") + " level vs soloed component RMS, worst relative error", worst);
            CHECK_MSG (worst < 1e-3, tag + (comp == 1 ? ": early" : ": late"));
        }
        // the full wet carries both: early + late ~ total (power, not correlated exactly)
        render (*p, sig, 512);
        const float el = p->takeEarlyLevel(), ll = p->takeLateLevel();
        MEASURE (tag + ": early / late level with noise (dB)", dbOf (el / std::max (ll, 1e-9f)));
        if (a == aPlate || a == aVoid || a == aRoom || a == aChamber || a == aHall)
            CHECK_MSG (el > 0.0f && ll > 0.0f, tag);
    }

    // isFrozen follows FREEZE
    {
        auto p = makeVoid (fs, 256);
        CHECK (! p->isFrozen());
        setParam (*p, vp::freeze, 1.0f);
        render (*p, silence (2, 256), 256);
        CHECK (p->isFrozen());
        setParam (*p, vp::freeze, 0.0f);
        render (*p, silence (2, 256), 256);
        CHECK (! p->isFrozen());
    }
    // the spectrum tap is the wet output (before MIX/OUTPUT), mono sum
    {
        auto p = makeVoid (fs, 512);
        setParam (*p, vp::mix, 40.0f);
        setParam (*p, vp::output, 6.0f);
        p->prepareToPlay (fs, 512);
        p->spectrum.setActive (true);
        auto q = makeVoid (fs, 512);
        setParam (*q, vp::mix, 100.0f);
        q->prepareToPlay (fs, 512);
        const auto sig = noise (2, 8192, 0.2f, 4);
        render (*p, sig, 512);
        const auto wet = render (*q, sig, 512);
        std::vector<float> pulled (8192, 0.0f);
        const int got = p->spectrum.pull (pulled.data(), 8192);
        CHECK (got > 1000);
        double e = 0;
        for (int i = 0; i < got; ++i)
        {
            const int idx = 8192 - got + i;
            e = std::max (e, (double) std::abs (pulled[(size_t) i] - 0.5f * (wet.getSample (0, idx) + wet.getSample (1, idx))));
        }
        MEASURE ("spectrum tap vs wet (L+R)/2: max difference", e);
        CHECK (e < 1e-7);
    }
    // filterResponseDb: the filters as they run (glided), DROWN included
    {
        auto p = makeVoid (fs, 256);
        p->prepareToPlay (fs, 256);
        CHECK (std::abs (p->filterResponseDb (1000.0)) < 1e-6);
        setParam (*p, vp::hpf, 300.0f);
        setParam (*p, vp::lpf, 5000.0f);
        render (*p, silence (2, 64), 64);
        const double moving = p->filterResponseDb (300.0);
        CHECK (moving > -3.0); // still gliding: the display shows the running filter, not the target
        render (*p, silence (2, (int) (0.3 * fs)), 256);
        CHECK_NEAR (p->filterResponseDb (300.0), vf::hpLpResponseDb (300.0, 300.0, 5000.0, fs), 1e-3);
        CHECK_NEAR (p->filterResponseDb (5000.0), -3.01, 0.05);
        CHECK_NEAR (p->getEngineForTesting().getCurrentHpfHz(), plainValue (*p, vp::hpf), 1e-3);
    }
    // meters: input = the input, output = the output
    {
        auto p = makeVoid (fs, 512);
        p->prepareToPlay (fs, 512);
        const auto sig = noise (2, 4096, 0.2f, 9);
        p->inputMeters[0].takeRms();
        p->outputMeters[1].takeRms();
        const auto out = render (*p, sig, 512);
        CHECK_NEAR (p->inputMeters[0].takeRms(), rms (sig.getReadPointer (0), 4096), 1e-4);
        CHECK_NEAR (p->outputMeters[1].takeRms(), rms (out.getReadPointer (1), 4096), 1e-4);
        CHECK (p->inputMeters[0].takeRms() < 0.0f); // no new audio
    }
}

namespace
{
    // 4th-order Butterworth high-pass at 8 kHz (click detector).
    std::vector<double> clickBand (const float* x, int n, double fs)
    {
        vf::Svf a, b;
        const double g = vf::integratorGain (8000.0, fs);
        a.set (g, 0.7653668647301796);
        b.set (g, 1.8477590650225735);
        std::vector<double> y ((size_t) n);
        for (int i = 0; i < n; ++i)
            y[(size_t) i] = b.process<vf::Svf::highpass> (a.process<vf::Svf::highpass> (x[i]));
        return y;
    }

    double maxAbs (const std::vector<double>& v, int from, int to)
    {
        double m = 0;
        for (int i = std::max (0, from); i < std::min ((int) v.size(), to); ++i) m = std::max (m, std::abs (v[(size_t) i]));
        return m;
    }

    // IR metrics for the "algorithms are distinct" test.
    struct IrMetrics { double earlyFraction, centreMs, peakMs, mixingMs, densityAt50; };
    IrMetrics irMetrics (const juce::AudioBuffer<float>& b, double fs)
    {
        const int n = b.getNumSamples();
        std::vector<double> h ((size_t) n), e ((size_t) n);
        double total = 0, tw = 0;
        for (int i = 0; i < n; ++i)
        {
            h[(size_t) i] = 0.5 * (b.getSample (0, i) + b.getSample (1, i)) * 0 + b.getSample (0, i);
            e[(size_t) i] = 0.5 * ((double) b.getSample (0, i) * b.getSample (0, i) + (double) b.getSample (1, i) * b.getSample (1, i));
            total += e[(size_t) i];
            tw += e[(size_t) i] * i / fs;
        }
        IrMetrics m {};
        double early = 0;
        for (int i = 0; i < (int) (0.05 * fs) && i < n; ++i) early += e[(size_t) i];
        m.earlyFraction = early / total;
        m.centreMs = 1000.0 * tw / total;
        // energy peak of the 10 ms moving average
        const int w = (int) (0.01 * fs);
        double run = 0, best = -1;
        for (int i = 0; i < n; ++i)
        {
            run += e[(size_t) i];
            if (i >= w) run -= e[(size_t) (i - w)];
            if (run > best) { best = run; m.peakMs = 1000.0 * (i - w / 2) / fs; }
        }
        // normalised echo density (Abel & Huang 2006): 20 ms Hann window, fraction of
        // samples beyond one weighted standard deviation, / erfc(1 / sqrt 2)
        const int L = (int) (0.02 * fs);
        std::vector<double> hw ((size_t) L);
        double hs = 0;
        for (int i = 0; i < L; ++i) { hw[(size_t) i] = 0.5 - 0.5 * std::cos (2.0 * pi * (i + 0.5) / L); hs += hw[(size_t) i]; }
        m.mixingMs = -1;
        for (int c = 0; c + L < n; c += (int) (0.001 * fs))
        {
            double var = 0;
            for (int i = 0; i < L; ++i) var += hw[(size_t) i] / hs * h[(size_t) (c + i)] * h[(size_t) (c + i)];
            const double sd = std::sqrt (var);
            double out = 0;
            for (int i = 0; i < L; ++i) out += std::abs (h[(size_t) (c + i)]) > sd ? hw[(size_t) i] / hs : 0.0;
            const double ned = out / 0.31731050786291415;
            const double tMs = 1000.0 * (c + L / 2) / fs;
            if (std::abs (tMs - 50.0) < 0.6) m.densityAt50 = ned;
            if (m.mixingMs < 0 && ned >= 0.9 && tMs > 2.0) m.mixingMs = tMs;
        }
        return m;
    }
}

TEST ("VOID: algorithms are measurably distinct (early fraction, centre time, energy peak, echo density / mixing time; CSV)")
{
    const double fs = 48000.0;
    juce::String csv ("algorithm,early_fraction_50ms,centre_time_ms,energy_peak_ms,mixing_time_ms,ned_at_50ms\n");
    std::vector<IrMetrics> m;
    for (int a = 0; a < 5; ++a)
    {
        auto p = makeVoid (fs, 512);
        measurementSetup (*p, a, 2.0f);
        p->prepareToPlay (fs, 512);
        const auto out = render (*p, impulse (2, (int) (2.5 * fs)), 512);
        m.push_back (irMetrics (out, fs));
        const auto& x = m.back();
        csv << algoName (a) << "," << x.earlyFraction << "," << x.centreMs << "," << x.peakMs << "," << x.mixingMs << "," << x.densityAt50 << "\n";
        const auto tag = juce::String (algoName (a)) + ": ";
        MEASURE (tag + "energy in the first 50 ms (%)", 100.0 * x.earlyFraction);
        MEASURE (tag + "centre time (ms)", x.centreMs);
        MEASURE (tag + "energy peak (ms)", x.peakMs);
        MEASURE (tag + "mixing time, NED >= 0.9 (ms)", x.mixingMs);
        MEASURE (tag + "echo density at 50 ms (NED)", x.densityAt50);
    }
    outputDirectory().getChildFile ("VOID_algorithms.csv").replaceWithText (csv);
    // every pair differs clearly in at least two of the five time-structure metrics
    auto rel = [] (double a, double b) { return std::abs (a - b) / std::max (std::max (std::abs (a), std::abs (b)), 1e-9); };
    for (int a = 0; a < 5; ++a)
        for (int b = a + 1; b < 5; ++b)
        {
            int distinct = 0;
            distinct += rel (m[(size_t) a].earlyFraction, m[(size_t) b].earlyFraction) > 0.25 ? 1 : 0;
            distinct += rel (m[(size_t) a].centreMs, m[(size_t) b].centreMs) > 0.15 ? 1 : 0;
            distinct += std::abs (m[(size_t) a].peakMs - m[(size_t) b].peakMs) > 10.0 ? 1 : 0;
            distinct += rel (m[(size_t) a].mixingMs, m[(size_t) b].mixingMs) > 0.25 ? 1 : 0;
            distinct += rel (m[(size_t) a].densityAt50, m[(size_t) b].densityAt50) > 0.5 ? 1 : 0;
            CHECK_MSG (distinct >= 2, juce::String (algoName (a)) + " vs " + algoName (b) + ": " + juce::String (distinct) + " distinct metrics");
        }
    // the characters the spec names: ROOM early-dominant, VOID slowly building
    CHECK (m[aRoom].earlyFraction == std::max ({ m[0].earlyFraction, m[1].earlyFraction, m[2].earlyFraction, m[3].earlyFraction, m[4].earlyFraction }));
    CHECK (m[aVoid].peakMs == std::max ({ m[0].peakMs, m[1].peakMs, m[2].peakMs, m[3].peakMs, m[4].peakMs }));
    CHECK (m[aVoid].peakMs > 80.0);
    CHECK (m[aChamber].densityAt50 > m[aRoom].densityAt50); // CHAMBER: denser short-to-medium reflections
}

TEST ("VOID: modulation does not click: MOD DEPTH 100 % at 0.05 / 0.5 / 5 Hz, every algorithm and colour (8 kHz high-pass detector)")
{
    const double fs = 48000.0;
    const int n = (int) (4.0 * fs);
    double worst = -200.0;
    for (int a = 0; a < 5; ++a)
        for (int c : { cClean, cRuin })
        {
            // low-level tone (RUIN's saturation stays mild): its harmonics stay far below 8 kHz
            const auto sig = toBuffer (sine (fs, 220.0, c == cRuin ? 0.03f : 0.25f, n), 2);
            auto run = [&] (float depth, float rate)
            {
                auto p = makeVoid (fs, 256);
                setParam (*p, vp::algorithm, (float) a);
                setParam (*p, vp::color, (float) c);
                setParam (*p, vp::mix, 100.0f);
                setParam (*p, vp::decay, 3.0f);
                setParam (*p, vp::modDepth, depth);
                setParam (*p, vp::modRate, rate);
                p->prepareToPlay (fs, 256);
                const auto out = render (*p, sig, 256);
                double pk = 0;
                for (int ch = 0; ch < 2; ++ch)
                    pk = std::max (pk, maxAbs (clickBand (out.getReadPointer (ch), n, fs), (int) (1.0 * fs), n));
                return std::pair { pk, stereoRms (out, (int) (1.0 * fs), n - (int) fs) };
            };
            const auto [still, level] = run (0.0f, 0.5f);
            for (float rate : { 0.05f, 0.5f, 5.0f })
            {
                const auto [moving, lv] = run (100.0f, rate);
                const double db = dbOf (moving / level);
                worst = std::max (worst, db);
                const auto tag = juce::String (algoName (a)) + (c == cRuin ? " Ruin" : " Clean") + " " + juce::String (rate) + " Hz";
                MEASURE (tag + ": 8 kHz+ peak re wet RMS (dB)", db);
                CHECK_MSG (moving < 2.0 * still + 1e-3 * level, tag + ": modulation adds broadband transients");
                juce::ignoreUnused (lv);
            }
        }
    MEASURE ("worst 8 kHz+ peak with modulation re wet RMS (dB)", worst);
}

namespace
{
    // 0.3 s noise burst (-20 dBFS RMS) and a 220 Hz tone, then silence: 2 s.
    juce::AudioBuffer<float> controlSignal (double fs)
    {
        const int n = (int) (2.0 * fs);
        auto b = noise (2, n, 0.1f, 17);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < n; ++i)
            {
                const float v = i < (int) (0.3 * fs) ? b.getSample (ch, i) + 0.1f * (float) std::sin (2.0 * pi * 220.0 * i / fs) : 0.0f;
                b.setSample (ch, i, v);
            }
        return b;
    }

    double crest (const juce::AudioBuffer<float>& b, int from, int len)
    {
        double pk = 0;
        for (int i = from; i < std::min (from + len, b.getNumSamples()); ++i) pk = std::max (pk, (double) std::abs (b.getSample (0, i)));
        return pk / std::max (rmsRange (b, 0, from, len), 1e-12);
    }

    double centreTime (const juce::AudioBuffer<float>& b, double fs)
    {
        double e = 0, t = 0;
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            const double v = (double) b.getSample (0, i) * b.getSample (0, i) + (double) b.getSample (1, i) * b.getSample (1, i);
            e += v;
            t += v * i / fs;
        }
        return t / std::max (e, 1e-30);
    }
}

TEST ("VOID: every control audibly changes the output, each in the specified direction (audio-effect test per visible control)")
{
    const double fs = 48000.0;
    const auto sig = controlSignal (fs);
    using Setup = std::function<void (VoidProcessor&)>;
    auto renderWith = [&] (const Setup& setup, int freezeAt = -1)
    {
        auto p = makeVoid (fs, 256);
        setParam (*p, vp::mix, 100.0f);
        setup (*p);
        p->prepareToPlay (fs, 256);
        juce::AudioBuffer<float> io (sig);
        juce::MidiBuffer midi;
        for (int pos = 0; pos < io.getNumSamples(); pos += 256)
        {
            if (pos == freezeAt) setParam (*p, vp::freeze, 1.0f);
            juce::AudioBuffer<float> view (io.getArrayOfWritePointers(), 2, pos, std::min (256, io.getNumSamples() - pos));
            p->processBlock (view, midi);
        }
        return io;
    };
    auto relDiffDb = [] (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
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
    auto set = [] (const char* id, float v) -> Setup { return [id, v] (VoidProcessor& p) { setParam (p, id, v); }; };
    auto both = [] (Setup a, Setup b) -> Setup { return [a, b] (VoidProcessor& p) { a (p); b (p); }; };
    const Setup none = [] (VoidProcessor&) {};
    const Setup narrow = both (set (vp::hpf, 300.0f), set (vp::lpf, 3000.0f));
    struct Case { const char* id; Setup base, alt; };
    const std::vector<Case> cases {
        { vp::algorithm, none, set (vp::algorithm, (float) aRoom) },
        { vp::color, none, set (vp::color, (float) cDark) },
        { vp::preDelay, none, set (vp::preDelay, 150.0f) },
        { vp::size, none, set (vp::size, 100.0f) },
        { vp::decay, none, set (vp::decay, 8.0f) },
        { vp::diffusion, none, set (vp::diffusion, 0.0f) },
        { vp::drown, none, set (vp::drown, 100.0f) },
        { vp::modRate, set (vp::modDepth, 60.0f), both (set (vp::modDepth, 60.0f), set (vp::modRate, 4.0f)) },
        { vp::modDepth, none, set (vp::modDepth, 100.0f) },
        { vp::width, none, set (vp::width, 200.0f) },
        { vp::hpf, none, set (vp::hpf, 400.0f) },
        { vp::lpf, none, set (vp::lpf, 2000.0f) },
        { vp::filterMode, narrow, both (narrow, set (vp::filterMode, 2.0f)) },
        { vp::duck, none, set (vp::duck, 100.0f) },
        { vp::mix, set (vp::mix, 35.0f), set (vp::mix, 80.0f) },
        { vp::freeze, none, none }, // engaged after the burst (below)
        { vp::output, none, set (vp::output, 6.0f) },
        { vp::bypass, none, set (vp::bypass, 1.0f) },
    };
    CHECK ((int) cases.size() == 18); // every parameter of the registry
    std::map<juce::String, std::pair<juce::AudioBuffer<float>, juce::AudioBuffer<float>>> r;
    for (const auto& c : cases)
    {
        const bool isFreeze = juce::String (c.id) == vp::freeze;
        auto a = renderWith (c.base), b = renderWith (c.alt, isFreeze ? (int) (0.35 * fs) / 256 * 256 : -1);
        const double d = relDiffDb (b, a);
        MEASURE (juce::String (c.id) + ": output change re output (dB)", d);
        CHECK_MSG (d > -30.0, juce::String (c.id) + " does not change the output");
        r[c.id] = { std::move (a), std::move (b) };
    }
    const int tail = (int) (1.0 * fs), tailLen = (int) (0.9 * fs);
    auto tailDb = [&] (const juce::AudioBuffer<float>& b) { return dbOf (stereoRms (b, tail, tailLen)); };
    auto side = [&] (const juce::AudioBuffer<float>& b)
    {
        double s = 0;
        for (int i = 0; i < b.getNumSamples(); ++i) { const double d = 0.5 * (b.getSample (0, i) - b.getSample (1, i)); s += d * d; }
        return std::sqrt (s / b.getNumSamples());
    };
    {
        const auto& [a, b] = r[vp::algorithm];
        CHECK (centreTime (b, fs) < 0.7 * centreTime (a, fs)); // Room is shorter and earlier than Hall
    }
    {
        const auto& [a, b] = r[vp::color];
        MEASURE ("DARK: 8 kHz band re CLEAN (dB)", bandPowerDb (b, fs, 8000.0, 0, (int) fs) - bandPowerDb (a, fs, 8000.0, 0, (int) fs));
        CHECK (bandPowerDb (b, fs, 8000.0, 0, (int) fs) < bandPowerDb (a, fs, 8000.0, 0, (int) fs) - 6.0);
    }
    {
        const auto& [a, b] = r[vp::preDelay];
        CHECK (stereoRms (b, 0, (int) (0.12 * fs)) < 0.3 * stereoRms (a, 0, (int) (0.12 * fs))); // later onset
    }
    // SIZE and DIFFUSION act on the time structure: measured on impulse responses
    auto irWith = [&] (const Setup& setup)
    {
        auto p = makeVoid (fs, 512);
        setParam (*p, vp::mix, 100.0f);
        setParam (*p, vp::modDepth, 0.0f);
        setup (*p);
        p->prepareToPlay (fs, 512);
        return irMetrics (render (*p, impulse (2, (int) (2.0 * fs)), 512), fs);
    };
    {
        const auto a = irWith (none), b = irWith (set (vp::size, 100.0f));
        MEASURE ("SIZE 100: energy peak (ms) re SIZE 50", b.peakMs - a.peakMs);
        MEASURE ("SIZE 100: energy in the first 50 ms re SIZE 50", b.earlyFraction / a.earlyFraction);
        CHECK (b.peakMs > a.peakMs + 5.0);              // larger space: later reflections
    }
    {
        const auto& [a, b] = r[vp::decay];
        CHECK (tailDb (b) > tailDb (a) + 6.0);
    }
    {
        // DIFFUSION 0: the late field builds from discrete echoes (peaky), 100 %: smooth (late component)
        auto irCrest = [&] (const Setup& setup)
        {
            auto p = makeVoid (fs, 512);
            setParam (*p, vp::mix, 100.0f);
            setParam (*p, vp::modDepth, 0.0f);
            setup (*p);
            p->prepareToPlay (fs, 512);
            p->getEngineForTesting().setComponentSolo (2); // the late field (the early reflections do not diffuse)
            return crest (render (*p, impulse (2, (int) (0.4 * fs)), 512), (int) (0.04 * fs), (int) (0.12 * fs));
        };
        const double c0 = irCrest (set (vp::diffusion, 0.0f)), c70 = irCrest (none), c100 = irCrest (set (vp::diffusion, 100.0f));
        MEASURE ("DIFFUSION 0: crest factor of the late response 40-160 ms", c0);
        MEASURE ("DIFFUSION 70: crest factor of the late response 40-160 ms", c70);
        MEASURE ("DIFFUSION 100: crest factor of the late response 40-160 ms", c100);
        CHECK (c0 > 1.5 * c70);
        CHECK (c0 > 1.5 * c100);
    }
    {
        const auto& [a, b] = r[vp::drown];
        CHECK (tailDb (b) > tailDb (a) + 3.0);
        CHECK (side (b) > side (a));
    }
    {
        const auto& [a, b] = r[vp::width];
        CHECK (side (b) > 1.8 * side (a));
    }
    {
        // default FILTER MODE Loop: the late tail loses more every pass (the early reflections are not in the loop)
        const auto& [a, b] = r[vp::hpf];
        MEASURE ("HPF 400 (Loop): 125 Hz band of the tail re HPF 20 (dB)", bandPowerDb (b, fs, 125.0, tail, tailLen) - bandPowerDb (a, fs, 125.0, tail, tailLen));
        CHECK (bandPowerDb (b, fs, 125.0, tail, tailLen) < bandPowerDb (a, fs, 125.0, tail, tailLen) - 10.0);
    }
    {
        const auto& [a, b] = r[vp::lpf];
        MEASURE ("LPF 2 k (Loop): 8 kHz band of the tail re LPF 20 k (dB)", bandPowerDb (b, fs, 8000.0, tail, tailLen) - bandPowerDb (a, fs, 8000.0, tail, tailLen));
        CHECK (bandPowerDb (b, fs, 8000.0, tail, tailLen) < bandPowerDb (a, fs, 8000.0, tail, tailLen) - 10.0);
    }
    {
        // LOOP (default) filters every pass, POST once: POST keeps more of the out-of-band tail
        const auto& [a, b] = r[vp::filterMode];
        CHECK (bandPowerDb (b, fs, 125.0, tail, tailLen) > bandPowerDb (a, fs, 125.0, tail, tailLen) + 6.0);
    }
    {
        const auto& [a, b] = r[vp::duck];
        CHECK (stereoRms (b, (int) (0.1 * fs), (int) (0.15 * fs)) < 0.2 * stereoRms (a, (int) (0.1 * fs), (int) (0.15 * fs)));
    }
    {
        const auto& [a, b] = r[vp::mix];
        CHECK (tailDb (b) > tailDb (a) + 4.0);
    }
    {
        const auto& [a, b] = r[vp::freeze];
        CHECK (tailDb (b) > tailDb (a) + 6.0); // the tail is held
    }
    {
        const auto& [a, b] = r[vp::output];
        CHECK_NEAR (dbOf (stereoRms (b, 0, b.getNumSamples()) / stereoRms (a, 0, a.getNumSamples())), 6.0, 0.02);
    }
    {
        const auto& [a, b] = r[vp::bypass];
        CHECK (identical (b, sig));
        juce::ignoreUnused (a);
    }
}

TEST ("VOID: switching every control (ALGORITHM, COLOR, SIZE, PRE-DELAY, FREEZE and FILTER MODE included) is click-free; rapid automation stays finite (CSV)")
{
    // Click measure: 4th-order high-pass at 8 kHz on the output of a 220 Hz tone
    // (-12 dBFS). The tone and its reverb stay far below 8 kHz; a discontinuity
    // does not. Limit: 2x the worse of the two fixed settings + 2e-4 (-74 dBFS).
    const double fs = 48000.0;
    const int block = 64;
    const int n = (int) (3.0 * fs) / block * block;
    const int s1 = (int) (1.0 * fs) / block * block + 17 * block, s2 = (int) (2.0 * fs) / block * block + 5 * block;
    struct Ctl { const char* id; float a, b; };
    const Ctl ctls[] = {
        { vp::algorithm, (float) aHall, (float) aRoom }, { vp::algorithm, (float) aRoom, (float) aVoid }, { vp::algorithm, (float) aPlate, (float) aChamber },
        { vp::color, (float) cClean, (float) cRuin }, { vp::color, (float) cClean, (float) cDark },
        { vp::preDelay, 0.0f, 250.0f }, { vp::size, 0.0f, 100.0f }, { vp::decay, 0.1f, 60.0f }, { vp::diffusion, 0.0f, 100.0f },
        { vp::drown, 0.0f, 100.0f }, { vp::modRate, 0.01f, 5.0f }, { vp::modDepth, 0.0f, 100.0f }, { vp::width, 0.0f, 200.0f },
        { vp::hpf, 20.0f, 1000.0f }, { vp::lpf, 20000.0f, 1000.0f }, { vp::filterMode, 0.0f, 2.0f }, { vp::filterMode, 1.0f, 0.0f },
        { vp::duck, 0.0f, 100.0f }, { vp::mix, 0.0f, 100.0f }, { vp::freeze, 0.0f, 1.0f }, { vp::output, -12.0f, 12.0f }, { vp::bypass, 0.0f, 1.0f },
    };
    const auto tone = toBuffer (sine (fs, 220.0, 0.25f, n), 2);
    auto run = [&] (const Ctl& c, int mode) // 0: fixed a, 1: fixed b, 2: a -> b at s1, b -> a at s2
    {
        auto p = makeVoid (fs, block);
        setParam (*p, vp::mix, 60.0f);
        setParam (*p, vp::hpf, 80.0f); // keep the filter cases audible
        setParam (*p, vp::lpf, 9000.0f);
        if (juce::String (c.id) == vp::hpf || juce::String (c.id) == vp::lpf) { setParam (*p, vp::hpf, 20.0f); setParam (*p, vp::lpf, 20000.0f); }
        setParam (*p, c.id, mode == 1 ? c.b : c.a);
        p->prepareToPlay (fs, block);
        juce::AudioBuffer<float> io (tone);
        juce::MidiBuffer midi;
        for (int pos = 0; pos < n; pos += block)
        {
            if (mode == 2 && pos == s1) setParam (*p, c.id, c.b);
            if (mode == 2 && pos == s2) setParam (*p, c.id, c.a);
            juce::AudioBuffer<float> view (io.getArrayOfWritePointers(), 2, pos, block);
            p->processBlock (view, midi);
        }
        double pk = 0;
        for (int ch = 0; ch < 2; ++ch)
            pk = std::max (pk, maxAbs (clickBand (io.getReadPointer (ch), n, fs), (int) (0.5 * fs), n));
        return pk;
    };
    juce::String csv ("control,from,to,fixed_a,fixed_b,switching,limit\n");
    for (const auto& c : ctls)
    {
        const double fa = run (c, 0), fb = run (c, 1), sw = run (c, 2);
        const double limit = 2.0 * std::max (fa, fb) + 2e-4;
        csv << c.id << "," << c.a << "," << c.b << "," << fa << "," << fb << "," << sw << "," << limit << "\n";
        const auto tag = juce::String (c.id) + " " + juce::String (c.a) + " <-> " + juce::String (c.b);
        MEASURE (tag + ": 8 kHz+ peak switching / fixed (dB)", dbOf (sw / std::max (std::max (fa, fb), 1e-9)));
        CHECK_MSG (sw < limit, tag + ": click (" + juce::String (sw, 6) + " vs " + juce::String (limit, 6) + ")");
    }
    outputDirectory().getChildFile ("VOID_switching.csv").replaceWithText (csv);

    // rapid automation: every parameter jumps to a random value every block for 10 s
    {
        auto p = makeVoid (fs, 128);
        p->prepareToPlay (fs, 128);
        juce::Random rng (99);
        const auto sig = noise (2, (int) (10.0 * fs), 0.3f, 23);
        juce::AudioBuffer<float> io (sig);
        juce::MidiBuffer midi;
        float peak = 0;
        for (int pos = 0; pos < io.getNumSamples(); pos += 128)
        {
            for (auto* param : p->getParameters())
                if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (param))
                    if (rp->paramID != vp::bypass && rng.nextFloat() < 0.5f)
                        rp->setValueNotifyingHost (rng.nextFloat());
            juce::AudioBuffer<float> view (io.getArrayOfWritePointers(), 2, pos, std::min (128, io.getNumSamples() - pos));
            p->processBlock (view, midi);
            peak = std::max (peak, view.getMagnitude (0, view.getNumSamples()));
        }
        MEASURE ("rapid automation of every parameter, 10 s: output peak", peak);
        CHECK (allFinite (io));
        CHECK (peak < 16.0f); // OUTPUT +12 dB included
    }
}

TEST ("VOID: all sample rates and block sizes, mono / stereo / mono->stereo: finite, no allocation (switches included), zero latency, memory per instance")
{
    for (double fs : { 44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0 })
        for (int block : { 1, 16, 64, 256, 1024, 2048 })
            for (auto [inCh, outCh] : { std::pair { 1, 1 }, std::pair { 2, 2 }, std::pair { 1, 2 } })
            {
                auto p = makeVoid (fs, block, inCh, outCh);
                setParam (*p, vp::algorithm, (float) (block % 5));
                setParam (*p, vp::color, (float) (block % 3));
                setParam (*p, vp::duck, 40.0f);
                setParam (*p, vp::filterMode, (float) (block % 3));
                setParam (*p, vp::mix, 80.0f);
                p->prepareToPlay (fs, block);
                const int n = std::max (block * 4, (int) (0.25 * fs));
                auto in = toBuffer (sine (fs, 220.0, 0.5f, n), inCh);
                render (*p, in, block); // warm-up
                // switch algorithm, colour, filter mode, size, pre-delay and freeze so slots, fades and glides run while counted
                setParam (*p, vp::algorithm, (float) ((block + 2) % 5));
                setParam (*p, vp::color, (float) ((block + 1) % 3));
                setParam (*p, vp::filterMode, (float) ((block + 1) % 3));
                setParam (*p, vp::size, 90.0f);
                setParam (*p, vp::preDelay, 120.0f);
                setParam (*p, vp::freeze, 1.0f);
                juce::AudioBuffer<float> io (std::max (inCh, outCh), n);
                for (int ch = 0; ch < io.getNumChannels(); ++ch)
                    io.copyFrom (ch, 0, in, std::min (ch, inCh - 1), 0, n);
                juce::MidiBuffer midi;
                long allocs;
                {
                    ScopedAllocationCounter counter;
                    for (int pos = 0; pos < n; pos += block)
                    {
                        if (pos == n / 2) setParam (*p, vp::algorithm, (float) ((block + 4) % 5)); // a switch while the old slot rings out
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
            }
    // a host block larger than announced is processed in chunks
    {
        auto p = makeVoid (48000.0, 256);
        const auto sig = noise (2, 4096, 0.1f, 3);
        auto big = render (*p, sig, 4096);
        auto q = makeVoid (48000.0, 256);
        CHECK (identical (big, render (*q, sig, 256)));
    }
    for (double fs : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        auto p = makeVoid (fs, 512);
        MEASURE ("reverb memory per instance (MB) @" + juce::String (fs), p->getEngineForTesting().getMemoryBytes() / 1048576.0);
    }
}

TEST ("VOID: NaN/Inf in the input never reaches the output or poisons the networks")
{
    for (int a = 0; a < 5; ++a)
    {
        auto p = makeVoid (48000.0, 256);
        setParam (*p, vp::algorithm, (float) a);
        setParam (*p, vp::decay, 60.0f);
        setParam (*p, vp::color, (float) cRuin);
        setParam (*p, vp::duck, 50.0f);
        p->prepareToPlay (48000.0, 256);
        auto in = toBuffer (sine (48000.0, 440.0, 0.5f, 9600), 2);
        in.setSample (0, 100, std::numeric_limits<float>::quiet_NaN());
        in.setSample (1, 200, std::numeric_limits<float>::infinity());
        in.setSample (0, 300, -std::numeric_limits<float>::infinity());
        in.setSample (1, 5000, std::numeric_limits<float>::max());
        auto out = render (*p, in, 256);
        CHECK_MSG (allFinite (out), algoName (a));
        auto after = render (*p, toBuffer (sine (48000.0, 440.0, 0.5f, 48000), 2), 256);
        CHECK_MSG (allFinite (after), algoName (a));
        CHECK_MSG (rms (after.getReadPointer (0) + 24000, 24000) > 0.05f, algoName (a));
    }
}

TEST ("VOID: silence gives exact silence; DC is blocked on the wet path; denormal input stays clean; tails decay to nothing")
{
    const double fs = 48000.0;
    for (int a = 0; a < 5; ++a)
    {
        auto p = makeVoid (fs, 512);
        setParam (*p, vp::algorithm, (float) a);
        setParam (*p, vp::color, (float) (a % 3));
        p->prepareToPlay (fs, 512);
        const auto out = render (*p, silence (2, (int) fs), 512);
        CHECK_MSG (out.getMagnitude (0, out.getNumSamples()) == 0.0f, juce::String (algoName (a)) + ": silence is not silent");
        // DC: the wet carries none (network input DC blocked)
        auto q = makeVoid (fs, 512);
        setParam (*q, vp::algorithm, (float) a);
        setParam (*q, vp::mix, 100.0f);
        q->prepareToPlay (fs, 512);
        juce::AudioBuffer<float> dc (2, (int) (6.0 * fs));
        for (int ch = 0; ch < 2; ++ch) juce::FloatVectorOperations::fill (dc.getWritePointer (ch), 0.5f, dc.getNumSamples());
        const auto wet = render (*q, dc, 512);
        double mean = 0;
        for (int i = (int) (4.0 * fs); i < wet.getNumSamples(); ++i) mean += wet.getSample (0, i);
        mean /= (2.0 * fs);
        MEASURE (juce::String (algoName (a)) + ": wet mean with 0.5 DC input (last 2 s)", mean);
        CHECK (std::abs (mean) < 1e-3);
        // denormal input: finite, silent-ish, and not slow
        juce::AudioBuffer<float> den (2, (int) fs);
        for (int ch = 0; ch < 2; ++ch) juce::FloatVectorOperations::fill (den.getWritePointer (ch), 1.0e-40f, den.getNumSamples());
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        const auto dout = render (*q, den, 512);
        const double ms = juce::Time::getMillisecondCounterHiRes() - t0;
        CHECK (allFinite (dout));
        MEASURE (juce::String (algoName (a)) + ": 1 s of denormal input (ms)", ms);
    }
    // tails decay to nothing (Hall 2.5 s: below -150 dBFS after 25 s)
    {
        auto p = makeVoid (fs, 512);
        setParam (*p, vp::mix, 100.0f);
        p->prepareToPlay (fs, 512);
        render (*p, noise (2, (int) fs, 0.3f, 1), 512);
        const auto t = render (*p, silence (2, (int) (25.0 * fs)), 512);
        const double end = stereoRms (t, t.getNumSamples() - (int) fs, (int) fs);
        MEASURE ("Hall 2.5 s: tail level 24-25 s after the input (dBFS)", dbOf (end));
        CHECK (dbOf (end) < -150.0);
    }
}

TEST ("VOID: host Bypass and MIX 0 are bit-exact dry (zero latency), every rate and layout; the Bypass transition is a 20 ms crossfade")
{
    for (double fs : { 44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0 })
        for (auto [inCh, outCh] : { std::pair { 1, 1 }, std::pair { 2, 2 }, std::pair { 1, 2 } })
        {
            auto p = makeVoid (fs, 128, inCh, outCh);
            CHECK (p->getLatencySamples() == 0);
            const auto tag = juce::String (fs) + " " + juce::String (inCh) + "->" + juce::String (outCh);
            juce::AudioBuffer<float> sig (outCh, 8192);
            const auto l = sine (fs, 997.0, 0.5f, 8192), r = sine (fs, 331.0, 0.4f, 8192);
            sig.copyFrom (0, 0, l.data(), 8192);
            if (outCh == 2) sig.copyFrom (1, 0, (inCh == 2 ? r : l).data(), 8192);
            setParam (*p, vp::algorithm, (float) aVoid);
            setParam (*p, vp::color, (float) cRuin);
            setParam (*p, vp::decay, 60.0f);
            setParam (*p, vp::output, 12.0f);
            setParam (*p, vp::bypass, 1.0f);
            p->prepareToPlay (fs, 128);
            auto out = render (*p, sig, 128);
            CHECK_MSG (identical (out, sig), tag + ": Bypass is not bit-exact dry");

            setParam (*p, vp::bypass, 0.0f);
            setParam (*p, vp::output, 0.0f);
            setParam (*p, vp::mix, 0.0f);
            p->prepareToPlay (fs, 128);
            out = render (*p, sig, 128);
            CHECK_MSG (identical (out, sig), tag + ": MIX 0 is not bit-exact dry");
        }

    const double fs = 48000.0;
    auto p = makeVoid (fs, 64);
    setParam (*p, vp::mix, 70.0f);
    p->prepareToPlay (fs, 64);
    const auto sig = toBuffer (sine (fs, 300.0, 0.4f, (int) fs), 2);
    juce::AudioBuffer<float> io (sig);
    juce::MidiBuffer midi;
    const int switchAt = (int) (0.5 * fs) / 64 * 64;
    for (int pos = 0; pos < io.getNumSamples(); pos += 64)
    {
        if (pos == switchAt) setParam (*p, vp::bypass, 1.0f);
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

TEST ("VOID: state round trip restores every parameter and renders identically; foreign and corrupt states are ignored")
{
    const double fs = 48000.0;
    auto a = makeVoid (fs, 256);
    const std::pair<const char*, float> values[] = {
        { vp::algorithm, 2.0f }, { vp::color, 1.0f }, { vp::preDelay, 37.0f }, { vp::size, 63.0f }, { vp::decay, 3.3f }, { vp::diffusion, 44.0f },
        { vp::drown, 22.0f }, { vp::modRate, 1.7f }, { vp::modDepth, 55.0f }, { vp::width, 140.0f }, { vp::hpf, 95.0f }, { vp::lpf, 7700.0f },
        { vp::filterMode, 2.0f }, { vp::duck, 18.0f }, { vp::mix, 47.0f }, { vp::freeze, 0.0f }, { vp::output, -2.5f } };
    CHECK (std::size (values) == 17);
    for (auto [id, v] : values)
        setParam (*a, id, v);
    juce::MemoryBlock state;
    a->getStateInformation (state);
    auto b = makeVoid (fs, 256);
    b->setStateInformation (state.getData(), (int) state.getSize());
    for (auto* param : a->getParameters())
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (param))
            CHECK_MSG (std::abs (r->getValue() - b->apvts.getParameter (r->paramID)->getValue()) < 1e-6f, r->paramID);
    a->prepareToPlay (fs, 256);
    b->prepareToPlay (fs, 256);
    const auto sig = noise (2, 48000, 0.1f, 31);
    CHECK_MSG (identical (render (*a, sig, 256), render (*b, sig, 256)), "restored instance renders differently");
    auto fresh = makeVoid (fs, 256);
    CHECK (! identical (render (*fresh, sig, 256), render (*b, sig, 256)));
    b->setStateInformation ("nonsense", 8);
    CHECK_NEAR (plainValue (*b, vp::mix), 47.0f, 0.01);
    auto xml = juce::AudioProcessor::getXmlFromBinary (state.getData(), (int) state.getSize());
    CHECK (xml != nullptr);
    if (xml != nullptr)
    {
        xml->setAttribute ("plugin", "ECHO");
        juce::MemoryBlock foreign;
        juce::AudioProcessor::copyXmlToBinary (*xml, foreign);
        auto c = makeVoid (fs, 256);
        c->setStateInformation (foreign.getData(), (int) foreign.getSize());
        CHECK_NEAR (plainValue (*c, vp::mix), 35.0f, 0.01);
    }
}

TEST ("VOID: factory presets load, set every parameter, cover every algorithm and colour, render safely and reject other plugins' presets")
{
    VoidProcessor p;
    const auto& list = p.presets.getPresets();
    int factory = 0;
    for (auto& pr : list) factory += pr.isFactory ? 1 : 0;
    MEASURE ("factory presets", factory);
    CHECK (factory >= 14);
    std::set<int> algos, colors, modes;
    std::set<juce::String> names;
    bool infinite = false;
    for (int i = 0; i < (int) list.size(); ++i)
        if (list[(size_t) i].isFactory)
        {
            p.loadPreset (i);
            names.insert (list[(size_t) i].name);
            CHECK_MSG (list[(size_t) i].params.values.size() == 17, list[(size_t) i].name);
            for (auto& [id, v] : list[(size_t) i].params.values)
            {
                auto* r = p.apvts.getParameter (id);
                CHECK_MSG (r != nullptr, id);
                if (r) CHECK_MSG (std::abs (r->convertFrom0to1 (r->getValue()) - v) < 0.06f * std::max (1.0f, std::abs (v) * 0.01f), list[(size_t) i].name + ": " + id);
            }
            CHECK (! p.isPresetModified());
            algos.insert (juce::roundToInt (plainValue (p, vp::algorithm)));
            colors.insert (juce::roundToInt (plainValue (p, vp::color)));
            modes.insert (juce::roundToInt (plainValue (p, vp::filterMode)));
            infinite = infinite || vp::isInfiniteDecay (plainValue (p, vp::decay));
            CHECK_MSG (plainValue (p, vp::freeze) < 0.5f, list[(size_t) i].name + ": a preset must not load frozen");
        }
    CHECK (algos.size() == 5 && colors.size() == 3 && modes.size() == 3 && infinite);
    CHECK ((int) names.size() == factory);
    for (auto* required : { "Small Room", "Dark Hall", "Huge Wash", "Shoegaze Drown", "Eternal Room" })
        CHECK_MSG (names.count (required) == 1, required);

    setParam (p, vp::mix, 13.0f);
    CHECK (p.isPresetModified());
    PresetManager::Preset out;
    CHECK (p.presets.parse (R"({"format":"tleet-preset","version":1,"plugin":"ECHO","name":"x","params":{}})", out).failed());
    CHECK (p.presets.parse (R"({"format":"tleet-preset","version":99,"plugin":"VOID","name":"x","params":{}})", out).failed());
    const auto json = p.stateToClipboardJson();
    setParam (p, vp::mix, 99.0f);
    CHECK (p.pasteStateFromJson (json).wasOk());
    CHECK_NEAR (plainValue (p, vp::mix), 13.0f, 0.06);

    for (int i = 0; i < (int) list.size(); ++i)
    {
        auto q = makeVoid (48000.0, 256);
        q->loadPreset (i);
        q->prepareToPlay (48000.0, 256);
        const auto sig = controlSignal (48000.0);
        const auto o = render (*q, sig, 256);
        double dd = 0, de = 0;
        for (int ch = 0; ch < 2; ++ch)
            for (int k = 0; k < o.getNumSamples(); ++k)
            {
                dd += std::pow ((double) o.getSample (ch, k) - sig.getSample (ch, k), 2.0);
                de += std::pow ((double) sig.getSample (ch, k), 2.0);
            }
        MEASURE (list[(size_t) i].name + ": output - input re input (dB)", 10.0 * std::log10 (dd / de));
        MEASURE (list[(size_t) i].name + ": output peak", o.getMagnitude (0, o.getNumSamples()));
        CHECK_MSG (allFinite (o) && 10.0 * std::log10 (dd / de) > -20.0, list[(size_t) i].name);
        CHECK_MSG (o.getMagnitude (0, o.getNumSamples()) < 1.0f, list[(size_t) i].name + ": too hot");
    }
}

TEST ("VOID: CPU cost per instance at 44.1-192 kHz, every algorithm (measurement)")
{
    for (double fs : { 44100.0, 48000.0, 96000.0, 192000.0 })
        for (int a = 0; a < 5; ++a)
        {
            const int block = 128;
            auto p = makeVoid (fs, block);
            setParam (*p, vp::algorithm, (float) a);
            setParam (*p, vp::modDepth, 50.0f);
            setParam (*p, vp::duck, 30.0f);
            setParam (*p, vp::color, (float) cRuin);
            setParam (*p, vp::filterMode, 1.0f);
            setParam (*p, vp::hpf, 60.0f);
            setParam (*p, vp::lpf, 12000.0f);
            setParam (*p, vp::mix, 50.0f);
            p->prepareToPlay (fs, block);
            const int n = (int) (fs * 2.0);
            auto io = noise (2, n, 0.2f, 3);
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
            const double medianLoad = times[times.size() / 2] * (fs / block) * 100.0;
            const auto tag = juce::String (algoName (a)) + " @" + juce::String (fs) + " Hz, block 128, stereo, RUIN + LOOP filters";
            MEASURE (tag + ": % of one core", ms / 2000.0 * 100.0);
            MEASURE (tag + ": % of one core from the median block", medianLoad);
            if (fs <= 48000.0)
                CHECK_MSG (medianLoad < 10.0, tag);
            CHECK_MSG (medianLoad < 50.0, tag);
        }
}
