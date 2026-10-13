#include "TestFramework.h"
#include "TestSignals.h"
#include <cstring>
#include <functional>
#include <map>
#include <set>
#include <tuple>
#include "../plugins/SHAPE/PluginProcessor.h"
#include "../plugins/SHAPE/Parameters.h"
#include "../plugins/SHAPE/dsp/ShapeModel.h"
#include "../plugins/SHAPE/dsp/PassiveNetwork.h"
#include "../plugins/SHAPE/dsp/AnalogStages.h"

using namespace tleet;
using namespace tleet::test;
using tleet::shape::ShapeProcessor;
using tleet::shape::ShapeModel;
using tleet::shape::EqSettings;
namespace sp = tleet::shape::params;
namespace snet = tleet::shape::net;

namespace
{
    enum CharIdx { cClean = 0, cOriginal, cBurn };
    constexpr double pi = juce::MathConstants<double>::pi;
    const char* charName[] = { "Clean", "Original", "Burn" };

    void setParam (ShapeProcessor& p, const char* id, float plain)
    {
        auto* r = p.apvts.getParameter (id);
        r->setValueNotifyingHost (r->convertTo0to1 (plain));
    }

    float plainValue (ShapeProcessor& p, const char* id)
    {
        auto* r = p.apvts.getParameter (id);
        return r->convertFrom0to1 (r->getValue());
    }

    void applyEq (ShapeProcessor& p, const EqSettings& s)
    {
        setParam (p, sp::lfFreq, (float) s.lfIndex);
        setParam (p, sp::lfBoost, (float) s.lfBoost);
        setParam (p, sp::lfAtten, (float) s.lfAtten);
        setParam (p, sp::hfFreq, (float) s.hfIndex);
        setParam (p, sp::hfBoost, (float) s.hfBoost);
        setParam (p, sp::hfBandwidth, (float) s.hfBandwidth);
        setParam (p, sp::hfAttenFreq, (float) s.attenIndex);
        setParam (p, sp::hfAtten, (float) s.hfAtten);
        setParam (p, sp::eqIn, s.eqIn ? 1.0f : 0.0f);
    }

    juce::String describe (const EqSettings& s)
    {
        return "LF " + sp::lfFreqNames[s.lfIndex] + " B" + juce::String (s.lfBoost, 1) + " A" + juce::String (s.lfAtten, 1)
             + " | HF " + sp::hfFreqNames[s.hfIndex] + " B" + juce::String (s.hfBoost, 1) + " BW" + juce::String (s.hfBandwidth, 1)
             + " | ATT " + sp::hfAttenFreqNames[s.attenIndex] + " " + juce::String (s.hfAtten, 1) + (s.eqIn ? "" : " | EQ OUT");
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

    double maxAbsDiff (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
    {
        double m = 0;
        for (int ch = 0; ch < a.getNumChannels(); ++ch)
            for (int i = 0; i < a.getNumSamples(); ++i)
                m = std::max (m, (double) std::abs (a.getSample (ch, i) - b.getSample (ch, i)));
        return m;
    }

    juce::AudioBuffer<float> delayed (const juce::AudioBuffer<float>& x, int d)
    {
        juce::AudioBuffer<float> y (x.getNumChannels(), x.getNumSamples());
        y.clear();
        for (int ch = 0; ch < x.getNumChannels(); ++ch)
            if (x.getNumSamples() > d)
                y.copyFrom (ch, d, x, ch, 0, x.getNumSamples() - d);
        return y;
    }

    juce::AudioBuffer<float> render (ShapeProcessor& p, const juce::AudioBuffer<float>& input, int block)
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

    std::unique_ptr<ShapeProcessor> makeShape (double fs, int block, int channels = 2)
    {
        auto p = std::make_unique<ShapeProcessor>();
        const auto set = channels == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo();
        ShapeProcessor::BusesLayout layout;
        layout.inputBuses.add (set);
        layout.outputBuses.add (set);
        CHECK_MSG (p->setBusesLayout (layout), "layout rejected");
        p->setRateAndBufferSizeDetails (fs, block);
        p->prepareToPlay (fs, block);
        return p;
    }

    // Colour as linear as it goes: Clean, DRIVE 0.
    void cleanColour (ShapeProcessor& p)
    {
        setParam (p, sp::character, (float) cClean);
        setParam (p, sp::drive, 0.0f);
    }

    double dbc (double ratio) { return 20.0 * std::log10 (std::max (ratio, 1.0e-15)); }
    double pct (double ratio) { return 100.0 * ratio; }

    // ---- multitone frequency response measurement -------------------------------
    // Tones sit exactly on DFT bins of the analysis window (df = fs / n), so each
    // bin reads one tone (no leakage); amplitudes -40 dBFS, fixed random phases.
    struct Multitone
    {
        double fs = 48000.0;
        int window = 12000, warmup = 36000;
        std::vector<double> hz;
        std::vector<double> phase;

        Multitone (double rate, double topHz) : fs (rate)
        {
            window = (int) std::round (fs / 4.0);                 // df = 4 Hz
            warmup = (int) std::round (fs * 0.6);
            const double df = fs / window;
            const double nominal[] = { 20, 25, 30, 40, 50, 60, 80, 100, 125, 160, 200, 250, 315, 400, 500, 630, 800, 1000,
                                       1250, 1600, 2000, 2500, 3000, 3500, 4000, 5000, 6300, 8000, 9000, 10000, 11000, 12000,
                                       13500, 15000, 16000, 17000, 18000, 19000, 20000, 22000, 26000, 30000, 36000, 42000 };
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

        // Tone amplitudes (linear) of channel 0, window ending `latency` samples
        // before the end is not needed: the signal is periodic in the window.
        std::vector<double> amplitudes (const juce::AudioBuffer<float>& out, int channel = 0) const
        {
            const float* d = out.getReadPointer (channel) + (out.getNumSamples() - window);
            std::vector<double> a;
            for (double f : hz)
                a.push_back (toneAmplitude (d, window, fs, f));
            return a;
        }
    };

    struct SweepResult { double maxErrDb = 0, atHz = 0, measuredDb = 0, expectedDb = 0; };

    // Measured response (dB) of the processor at `s` relative to the same
    // processor with EQ IN Out, compared with eqResponseDb. Appends CSV rows.
    SweepResult measureAgainstEvaluator (double fs, const EqSettings& s, const Multitone& mt,
                                         const std::vector<double>& reference, juce::String* csv,
                                         int character = cClean, float drive = 0.0f)
    {
        auto p = makeShape (fs, 256);
        setParam (*p, sp::character, (float) character);
        setParam (*p, sp::drive, drive);
        applyEq (*p, s);
        p->prepareToPlay (fs, 256);
        const auto out = render (*p, mt.signal (2), 256);
        const auto a = mt.amplitudes (out);
        SweepResult r;
        for (size_t k = 0; k < mt.hz.size(); ++k)
        {
            const double measured = 20.0 * std::log10 (a[k] / reference[k]);
            const double expected = p->getEqResponseDb (mt.hz[k]);
            const double err = measured - expected;
            if (csv != nullptr)
                *csv << fs << ",\"" << describe (s) << "\"," << mt.hz[k] << "," << expected << "," << measured << "," << err << ","
                     << shape::analogResponseDb (mt.hz[k], s) << "\n";
            if (std::abs (err) > std::abs (r.maxErrDb))
                r = { err, mt.hz[k], measured, expected };
        }
        return r;
    }

    std::vector<double> referenceAmplitudes (double fs, const Multitone& mt, int character = cClean, float drive = 0.0f)
    {
        auto p = makeShape (fs, 256);
        setParam (*p, sp::character, (float) character);
        setParam (*p, sp::drive, drive);
        setParam (*p, sp::eqIn, 0.0f);
        p->prepareToPlay (fs, 256);
        return mt.amplitudes (render (*p, mt.signal (2), 256));
    }

    // The settings matrix of the curve tests (addendum 12 / 26).
    std::vector<EqSettings> curveMatrix()
    {
        std::vector<EqSettings> m;
        for (int lf = 0; lf < 4; ++lf)
        {
            for (double b : { 2.5, 5.0, 7.5, 10.0 })
            {
                EqSettings s; s.lfIndex = lf; s.lfBoost = b; m.push_back (s);
            }
            for (double a : { 2.5, 5.0, 7.5, 10.0 })
            {
                EqSettings s; s.lfIndex = lf; s.lfAtten = a; m.push_back (s);
            }
            for (auto [b, a] : { std::pair { 5.0, 5.0 }, { 10.0, 10.0 }, { 10.0, 5.0 }, { 5.0, 10.0 }, { 8.0, 3.0 } })
            {
                EqSettings s; s.lfIndex = lf; s.lfBoost = b; s.lfAtten = a; m.push_back (s);
            }
        }
        for (int hf = 0; hf < 7; ++hf)
            for (double bw : { 0.0, 5.0, 10.0 })
                for (double b : { 5.0, 10.0 })
                {
                    EqSettings s; s.hfIndex = hf; s.hfBoost = b; s.hfBandwidth = bw; m.push_back (s);
                }
        for (int at = 0; at < 3; ++at)
            for (double a : { 5.0, 10.0 })
            {
                EqSettings s; s.attenIndex = at; s.hfAtten = a; m.push_back (s);
            }
        // combined
        EqSettings c1; c1.lfIndex = 1; c1.lfBoost = 7; c1.lfAtten = 4; c1.hfIndex = 3; c1.hfBoost = 6; c1.hfBandwidth = 3; c1.attenIndex = 2; c1.hfAtten = 4;
        EqSettings c2; c2.lfIndex = 3; c2.lfBoost = 10; c2.lfAtten = 10; c2.hfIndex = 6; c2.hfBoost = 10; c2.hfBandwidth = 10; c2.attenIndex = 0; c2.hfAtten = 10;
        EqSettings c3; c3.lfIndex = 0; c3.lfBoost = 4; c3.hfIndex = 0; c3.hfBoost = 10; c3.hfBandwidth = 0; c3.attenIndex = 1; c3.hfAtten = 7;
        EqSettings c4; c4.lfIndex = 2; c4.lfAtten = 6; c4.hfIndex = 5; c4.hfBoost = 3; c4.hfBandwidth = 7; c4.attenIndex = 2; c4.hfAtten = 2;
        for (auto& c : { c1, c2, c3, c4 })
            m.push_back (c);
        return m;
    }

    // Grid helpers for the pure evaluator
    std::vector<double> logGrid (double lo, double hi, int n)
    {
        std::vector<double> v;
        for (int i = 0; i < n; ++i)
            v.push_back (lo * std::pow (hi / lo, (double) i / (n - 1)));
        return v;
    }

    // ---- tone measurements on the processor -----------------------------------
    struct ToneResult { double thd, thdN, h2, h3, fundDb; };

    ToneResult renderTone (int character, float drivePct, float peakDbfs, double hz, double fs = 48000.0,
                           const EqSettings& eq = {})
    {
        auto p = makeShape (fs, 480);
        setParam (*p, sp::character, (float) character);
        setParam (*p, sp::drive, drivePct);
        applyEq (*p, eq);
        p->prepareToPlay (fs, 480);
        const float amp = juce::Decibels::decibelsToGain (peakDbfs);
        auto out = render (*p, toBuffer (sine (fs, hz, amp, (int) (fs * 1.0)), 2), 480);
        const int n = (int) (fs / 2), start = out.getNumSamples() - n;
        const float* d = out.getReadPointer (0) + start;
        const double f = toneAmplitude (d, n, fs, hz);
        // THD+N: everything but the fundamental (least-squares fit removed), 20 Hz .. 20 kHz band is implicit (host rate).
        double ss = 0, cc = 0, sc = 0, ys = 0, yc = 0, tot = 0;
        const double w = 2.0 * pi * hz / fs;
        for (int i = 0; i < n; ++i)
        {
            const double s = std::sin (w * i), c = std::cos (w * i);
            ss += s * s; cc += c * c; sc += s * c; ys += d[i] * s; yc += d[i] * c;
        }
        const double det = ss * cc - sc * sc;
        const double a = (ys * cc - yc * sc) / det, b = (yc * ss - ys * sc) / det;
        double mean = 0;
        for (int i = 0; i < n; ++i) mean += d[i];
        mean /= n;
        for (int i = 0; i < n; ++i)
        {
            const double r = d[i] - mean - a * std::sin (w * i) - b * std::cos (w * i);
            tot += r * r;
        }
        const double fundRms = std::sqrt (0.5 * (a * a + b * b));
        return { thd (d, n, fs, hz), std::sqrt (tot / n) / fundRms, toneAmplitude (d, n, fs, 2 * hz) / f,
                 toneAmplitude (d, n, fs, 3 * hz) / f, 20.0 * std::log10 (f / amp) };
    }
}

// =====================================================================================
TEST ("SHAPE: parameter IDs, ranges, defaults, texts, choice strings and buses match the registry")
{
    ShapeProcessor p;
    const char* ids[] = { "lfFreq", "lfBoost", "lfAtten", "hfFreq", "hfBoost", "hfBandwidth", "hfAttenFreq", "hfAtten",
                          "drive", "character", "mix", "output", "eqIn", "bypass" };
    for (auto* id : ids)
        CHECK_MSG (p.apvts.getParameter (id) != nullptr, id);
    CHECK (p.getParameters().size() == 14);
    for (auto* param : p.getParameters())
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (param))
            CHECK_MSG (r->getVersionHint() == 1, r->paramID);

    auto def = [&p] (const char* id) { auto* r = p.apvts.getParameter (id); return r->convertFrom0to1 (r->getDefaultValue()); };
    CHECK_NEAR (def ("lfFreq"), 2.0f, 1e-5);      // 60 Hz
    CHECK_NEAR (def ("lfBoost"), 0.0f, 1e-6);
    CHECK_NEAR (def ("lfAtten"), 0.0f, 1e-6);
    CHECK_NEAR (def ("hfFreq"), 4.0f, 1e-5);      // 10 kHz
    CHECK_NEAR (def ("hfBoost"), 0.0f, 1e-6);
    CHECK_NEAR (def ("hfBandwidth"), 5.0f, 1e-5);
    CHECK_NEAR (def ("hfAttenFreq"), 2.0f, 1e-5); // 20 kHz
    CHECK_NEAR (def ("hfAtten"), 0.0f, 1e-6);
    CHECK_NEAR (def ("drive"), 25.0f, 1e-4);
    CHECK_NEAR (def ("character"), (float) cOriginal, 1e-5);
    CHECK_NEAR (def ("mix"), 100.0f, 1e-4);
    CHECK_NEAR (def ("output"), 0.0f, 1e-6);
    CHECK_NEAR (def ("eqIn"), 1.0f, 1e-6);
    CHECK_NEAR (def ("bypass"), 0.0f, 1e-6);

    auto range = [&p] (const char* id, float lo, float mid, float hi)
    {
        auto* r = p.apvts.getParameter (id);
        CHECK_NEAR (r->convertFrom0to1 (0.0f), lo, 1e-4);
        CHECK_NEAR (r->convertFrom0to1 (0.5f), mid, 1e-3);
        CHECK_NEAR (r->convertFrom0to1 (1.0f), hi, 1e-4);
    };
    for (auto* id : { "lfBoost", "lfAtten", "hfBoost", "hfBandwidth", "hfAtten" })
        range (id, 0.0f, 5.0f, 10.0f);
    range ("drive", 0.0f, 50.0f, 100.0f);
    range ("mix", 0.0f, 50.0f, 100.0f);
    range ("output", -20.0f, 0.0f, 20.0f);

    // OUTPUT: 0 dB exactly at the knob centre, 0.1 dB grid, exact text round trip.
    {
        auto* f = dynamic_cast<juce::AudioParameterFloat*> (p.apvts.getParameter ("output"));
        juce::AudioProcessorParameter* r = f;
        const auto& nr = f->getNormalisableRange();
        CHECK (nr.convertFrom0to1 (0.5f) == 0.0f && nr.convertTo0to1 (0.0f) == 0.5f);
        int bad = 0;
        for (int t = -200; t <= 200; ++t)
        {
            const float v = nr.snapToLegalValue ((float) t * 0.1f);
            const float n = nr.convertTo0to1 (v);
            bad += nr.snapToLegalValue (nr.convertFrom0to1 (r->getValueForText (r->getText (n, 64)))) != v ? 1 : 0;
        }
        CHECK (bad == 0);
    }

    auto text = [&p] (const char* id, float plain) { auto* r = p.apvts.getParameter (id); return r->getText (r->convertTo0to1 (plain), 64); };
    CHECK (text ("lfBoost", 6.5f) == "6.5");
    CHECK (text ("lfAtten", 0.0f) == "0.0");
    CHECK (text ("hfBoost", 10.0f) == "10.0");
    CHECK (text ("hfBandwidth", 5.0f) == "5.0");
    CHECK (text ("hfAtten", 3.25f) == "3.3" || text ("hfAtten", 3.25f) == "3.2");
    CHECK (text ("drive", 25.0f) == "25 %");
    CHECK (text ("mix", 72.5f) == "72.5 %");
    CHECK (text ("output", 0.0f) == "0.0 dB");
    CHECK (text ("output", 3.4f) == "+3.4 dB");
    CHECK (text ("output", -12.0f) == "-12.0 dB");
    CHECK (text ("lfFreq", 0.0f) == "20 Hz");
    CHECK (text ("lfFreq", 3.0f) == "100 Hz");
    CHECK (text ("hfFreq", 6.0f) == "16 kHz");
    CHECK (text ("hfAttenFreq", 1.0f) == "10 kHz");
    CHECK (text ("character", 2.0f) == "Burn");
    CHECK (text ("eqIn", 1.0f) == "In");
    CHECK (text ("eqIn", 0.0f) == "Out");
    auto fromText = [&p] (const char* id, const char* t) { auto* r = p.apvts.getParameter (id); return r->convertFrom0to1 (r->getValueForText (t)); };
    CHECK_NEAR (fromText ("hfBandwidth", "Sharp"), 0.0f, 1e-5);
    CHECK_NEAR (fromText ("hfBandwidth", "broad"), 10.0f, 1e-5);
    CHECK_NEAR (fromText ("hfBandwidth", "3.5"), 3.5f, 1e-4);
    CHECK_NEAR (fromText ("lfBoost", "7.2"), 7.2f, 1e-4);
    CHECK_NEAR (fromText ("output", "-6 dB"), -6.0f, 1e-4);
    CHECK_NEAR (fromText ("eqIn", "Out"), 0.0f, 1e-6);
    CHECK_NEAR (fromText ("eqIn", "In"), 1.0f, 1e-6);

    // Discrete selectors expose only the hardware's real positions (addendum 4).
    auto choices = [&p] (const char* id) { return dynamic_cast<juce::AudioParameterChoice*> (p.apvts.getParameter (id))->choices; };
    CHECK (choices ("lfFreq") == juce::StringArray ({ "20 Hz", "30 Hz", "60 Hz", "100 Hz" }));
    CHECK (choices ("hfFreq") == juce::StringArray ({ "3 kHz", "4 kHz", "5 kHz", "8 kHz", "10 kHz", "12 kHz", "16 kHz" }));
    CHECK (choices ("hfAttenFreq") == juce::StringArray ({ "5 kHz", "10 kHz", "20 kHz" }));
    CHECK (choices ("character") == juce::StringArray ({ "Clean", "Original", "Burn" }));
    CHECK (choices ("lfFreq") == sp::lfFreqNames && choices ("hfFreq") == sp::hfFreqNames
           && choices ("hfAttenFreq") == sp::hfAttenFreqNames && choices ("character") == sp::characterNames);
    CHECK (p.getBypassParameter() == p.apvts.getParameter ("bypass"));

    // Buses: main stereo/mono in == out, no sidechain.
    CHECK (p.getBusCount (true) == 1 && p.getBusCount (false) == 1);
    ShapeProcessor::BusesLayout l;
    l.inputBuses.add (juce::AudioChannelSet::stereo());
    l.outputBuses.add (juce::AudioChannelSet::stereo());
    CHECK (p.checkBusesLayoutSupported (l));
    l.outputBuses.getReference (0) = juce::AudioChannelSet::mono();
    CHECK (! p.checkBusesLayoutSupported (l));
    l.inputBuses.getReference (0) = juce::AudioChannelSet::mono();
    CHECK (p.checkBusesLayoutSupported (l));
    l.inputBuses.getReference (0) = juce::AudioChannelSet::create5point1();
    l.outputBuses.getReference (0) = juce::AudioChannelSet::create5point1();
    CHECK (! p.checkBusesLayoutSupported (l));
}

TEST ("SHAPE: all sample rates and block sizes, mono and stereo: finite output, no steady-state allocation, latency per rate")
{
    for (double fs : { 44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0 })
        for (int block : { 1, 16, 32, 64, 128, 256, 512, 1024, 2048 })
            for (int channels : { 1, 2 })
            {
                auto p = makeShape (fs, block, channels);
                setParam (*p, sp::lfFreq, (float) (block % 4));
                setParam (*p, sp::lfBoost, 7.0f);
                setParam (*p, sp::lfAtten, 4.0f);
                setParam (*p, sp::hfFreq, (float) (block % 7));
                setParam (*p, sp::hfBoost, 6.0f);
                setParam (*p, sp::hfBandwidth, 2.0f);
                setParam (*p, sp::hfAttenFreq, (float) (block % 3));
                setParam (*p, sp::hfAtten, 3.0f);
                setParam (*p, sp::drive, 70.0f);
                setParam (*p, sp::character, (float) (block % 3));
                setParam (*p, sp::mix, 80.0f);
                setParam (*p, sp::output, 3.0f);
                const int n = std::max (block * 4, 4096);
                auto in = toBuffer (sine (fs, 220.0, 0.7f, n), channels);
                render (*p, in, block); // warm-up
                // move controls so the glides and the coefficient ramps run during the counted pass
                setParam (*p, sp::lfBoost, 2.0f);
                setParam (*p, sp::hfFreq, (float) ((block + 3) % 7));
                setParam (*p, sp::eqIn, 0.0f);

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
                CHECK_MSG (rms (io.getReadPointer (0), n) > 0.05f, tag + ": no output");
                const int expectedFactor = fs < 80000.0 ? 4 : (fs < 160000.0 ? 2 : 1);
                CHECK_MSG (p->getOversamplingFactor() == expectedFactor, tag);
                CHECK_MSG (std::abs (p->getInternalSampleRate() - fs * expectedFactor) < 1e-6, tag);
                CHECK_MSG (expectedFactor == 1 ? p->getLatencySamples() == 0 : (p->getLatencySamples() > 0 && p->getLatencySamples() < 128), tag);
            }
}

TEST ("SHAPE: NaN/Inf in the input never reaches the output or poisons the state")
{
    auto p = makeShape (48000.0, 256);
    setParam (*p, sp::lfBoost, 10.0f);
    setParam (*p, sp::hfBoost, 10.0f);
    setParam (*p, sp::hfAtten, 5.0f);
    setParam (*p, sp::character, (float) cBurn);
    auto in = toBuffer (sine (48000.0, 440.0, 0.5f, 4800), 2);
    in.setSample (0, 100, std::numeric_limits<float>::quiet_NaN());
    in.setSample (1, 200, std::numeric_limits<float>::infinity());
    in.setSample (0, 300, -std::numeric_limits<float>::infinity());
    auto out = render (*p, in, 256);
    CHECK (allFinite (out));
    auto after = render (*p, toBuffer (sine (48000.0, 440.0, 0.5f, 9600), 2), 256);
    CHECK (allFinite (after));
    MEASURE ("rms after the bad samples", rms (after.getReadPointer (0) + 4800, 4800));
    CHECK (rms (after.getReadPointer (0) + 4800, 4800) > 0.1f);
}

TEST ("SHAPE: silence gives silence; DC is blocked by the transformers on the wet path; denormal input stays clean")
{
    auto p = makeShape (48000.0, 512);
    setParam (*p, sp::output, 20.0f);
    setParam (*p, sp::drive, 100.0f);
    setParam (*p, sp::character, (float) cBurn);
    setParam (*p, sp::lfBoost, 10.0f);
    setParam (*p, sp::hfBoost, 10.0f);
    p->prepareToPlay (48000.0, 512);
    juce::AudioBuffer<float> silence (2, 48000);
    silence.clear();
    auto out = render (*p, silence, 512);
    CHECK (out.getMagnitude (0, out.getNumSamples()) < 1.0e-7f);

    auto q = makeShape (48000.0, 512);
    juce::AudioBuffer<float> dc (2, 48000 * 4);
    for (int ch = 0; ch < 2; ++ch)
        juce::FloatVectorOperations::fill (dc.getWritePointer (ch), 0.25f, dc.getNumSamples());
    out = render (*q, dc, 512);
    MEASURE ("output DC after 4 s of 0.25 DC in", out.getSample (0, out.getNumSamples() - 1));
    CHECK (std::abs (out.getSample (0, out.getNumSamples() - 1)) < 1.0e-3f);

    auto fresh = makeShape (48000.0, 512);
    setParam (*fresh, sp::output, 20.0f);
    setParam (*fresh, sp::drive, 100.0f);
    juce::AudioBuffer<float> tiny (2, 48000);
    for (int i = 0; i < tiny.getNumSamples(); ++i)
        for (int ch = 0; ch < 2; ++ch)
            tiny.setSample (ch, i, (i & 1) ? 1.0e-38f : -1.0e-38f);
    out = render (*fresh, tiny, 512);
    CHECK (allFinite (out));
    CHECK (out.getMagnitude (0, out.getNumSamples()) < 1.0e-6f);
}

TEST ("SHAPE: reported latency equals measured impulse delay; host Bypass and MIX 0 are bit-exact latency-aligned dry")
{
    for (double fs : { 44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0 })
    {
        auto p = makeShape (fs, 128);
        const int latency = p->getLatencySamples();
        MEASURE ("reported latency (samples) @" + juce::String (fs), latency);
        MEASURE ("reported latency (ms) @" + juce::String (fs), 1000.0 * latency / fs);

        cleanColour (*p);
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
        // the half-band FIR chain (up + down) spreads the impulse over a few samples: the peak is lower than 0.25
        MEASURE ("    impulse peak amplitude (input 0.25)", out.getSample (0, peakAt));
        CHECK (out.getSample (0, peakAt) > 0.15f && out.getSample (0, peakAt) <= 0.26f);

        // Host bypass: delayed input, bit-exact, whatever the other settings.
        const auto sig = toBuffer (sine (fs, 997.0, 0.5f, 8192), 2);
        setParam (*p, sp::lfBoost, 10.0f);
        setParam (*p, sp::hfBoost, 8.0f);
        setParam (*p, sp::character, (float) cBurn);
        setParam (*p, sp::drive, 100.0f);
        setParam (*p, sp::output, 12.0f);
        setParam (*p, sp::bypass, 1.0f);
        p->prepareToPlay (fs, 128);
        out = render (*p, sig, 128);
        CHECK_MSG (identical (out, delayed (sig, latency)), "Bypass is not bit-exact delayed dry");

        // MIX 0 with OUTPUT 0 dB: delayed input, bit-exact.
        setParam (*p, sp::bypass, 0.0f);
        setParam (*p, sp::output, 0.0f);
        setParam (*p, sp::mix, 0.0f);
        p->prepareToPlay (fs, 128);
        out = render (*p, sig, 128);
        CHECK_MSG (identical (out, delayed (sig, latency)), "MIX 0 is not bit-exact delayed dry");
    }
}

TEST ("SHAPE: the network as run equals eqResponseDb exactly (model sweep at the internal rate, every rate)")
{
    // The bare network (analog stages off) driven at the internal rate: the
    // evaluator is the transfer function of the very coefficients that run.
    double worst = 0;
    juce::String csv ("host_rate,setting,hz,evaluator_db,measured_db,error_db\n");
    for (double host : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        const double fs = shape::internalRateFor (host);
        Multitone mt (fs, 0.45 * host);
        const auto x = mt.signal (1, 0.01f);
        for (const auto& s : curveMatrix())
        {
            ShapeModel m;
            m.setEq (s);
            m.setCharacter (cClean);
            m.setDrive (0.0f);
            m.setStagesEnabledForTesting (false);
            m.prepare (host, 1 << shape::oversamplingFactorLog2For (host));
            juce::AudioBuffer<float> io (x);
            float* ch[] = { io.getWritePointer (0) };
            m.process (ch, 1, io.getNumSamples());
            const auto a = mt.amplitudes (io);
            for (size_t k = 0; k < mt.hz.size(); ++k)
            {
                const double measured = 20.0 * std::log10 (a[k] / 0.01);
                const double expected = shape::eqResponseDb (mt.hz[k], s, host);
                worst = std::max (worst, std::abs (measured - expected));
                if (host == 48000.0)
                    csv << host << ",\"" << describe (s) << "\"," << mt.hz[k] << "," << expected << "," << measured << "," << measured - expected << "\n";
            }
        }
    }
    outputDirectory().getChildFile ("SHAPE_network_vs_evaluator.csv").replaceWithText (csv);
    MEASURE ("max |network sweep - eqResponseDb| (dB), 101 settings x 4 rates", worst);
    CHECK (worst < 1.0e-4);
}

TEST ("SHAPE: every curve family through the processor matches eqResponseDb (LF x boost/atten, HF x bandwidth, atten, combined; CSV)")
{
    juce::String csv ("host_rate,setting,hz,evaluator_db,measured_db,error_db,analog_db\n");
    juce::String summary ("host_rate,setting,max_error_db,at_hz\n");
    for (double fs : { 44100.0, 48000.0, 96000.0 })
    {
        Multitone mt (fs, std::min (20000.0, 0.45 * fs));
        const auto ref = referenceAmplitudes (fs, mt);
        double worst = 0;
        for (const auto& s : curveMatrix())
        {
            const auto r = measureAgainstEvaluator (fs, s, mt, ref, fs == 48000.0 ? &csv : nullptr);
            summary << fs << ",\"" << describe (s) << "\"," << r.maxErrDb << "," << r.atHz << "\n";
            worst = std::max (worst, std::abs (r.maxErrDb));
            CHECK_MSG (std::abs (r.maxErrDb) < 0.01, describe (s) + " @ " + juce::String (fs) + ": " + juce::String (r.maxErrDb, 5)
                                                     + " dB at " + juce::String (r.atHz) + " Hz (expected " + juce::String (r.expectedDb, 3) + ")");
        }
        MEASURE ("max |processor sweep - eqResponseDb| (dB) @" + juce::String (fs), worst);
    }
    outputDirectory().getChildFile ("SHAPE_curves.csv").replaceWithText (csv);
    outputDirectory().getChildFile ("SHAPE_curve_errors.csv").replaceWithText (summary);
}

TEST ("SHAPE: EQ changes during playback (automation) land on the evaluator's curve: knobs, BANDWIDTH and every selector")
{
    // Prepared flat, then every EQ control is moved while audio runs. After the
    // glides the multitone response must equal eqResponseDb for the new settings,
    // i.e. the running coefficients follow the parameters (not only prepareToPlay).
    const double fs = 48000.0;
    Multitone mt (fs, 20000.0);
    const auto ref = referenceAmplitudes (fs, mt);
    EqSettings a; a.lfIndex = 3; a.lfBoost = 8; a.lfAtten = 6; a.hfIndex = 6; a.hfBoost = 7; a.hfBandwidth = 2; a.attenIndex = 0; a.hfAtten = 5;
    EqSettings b; b.lfIndex = 0; b.lfBoost = 3; b.lfAtten = 9; b.hfIndex = 1; b.hfBoost = 10; b.hfBandwidth = 9; b.attenIndex = 1; b.hfAtten = 8;
    double worst = 0;
    for (const auto& s : { a, b })
    {
        auto p = makeShape (fs, 256);
        cleanColour (*p);
        p->prepareToPlay (fs, 256);
        const auto sig = mt.signal (2);
        render (*p, sig, 256);  // run flat first
        applyEq (*p, s);        // automation while playing
        const auto out = render (*p, sig, 256);
        const auto amp = mt.amplitudes (out);
        double err = 0;
        for (size_t k = 0; k < mt.hz.size(); ++k)
            err = std::max (err, std::abs (20.0 * std::log10 (amp[k] / ref[k]) - p->getEqResponseDb (mt.hz[k])));
        MEASURE ("after automation to " + describe (s) + ": max |processor - eqResponseDb| (dB)", err);
        worst = std::max (worst, err);
    }
    CHECK (worst < 0.01);
}

TEST ("SHAPE: control ranges equal the published maxima; per-position boost/atten table (analog and as run)")
{
    // The calibration points (MODEL_SPEC 2.3): analog prototype.
    EqSettings s;
    s.lfBoost = 10; s.lfIndex = 2;
    CHECK_NEAR (shape::analogResponseDb (60.0, s), 13.5, 0.01);
    s = {}; s.lfAtten = 10; s.lfIndex = 2;
    CHECK_NEAR (shape::analogResponseDb (60.0, s), -17.5, 0.01);
    s = {}; s.hfAtten = 10; s.attenIndex = 1;
    CHECK_NEAR (shape::analogResponseDb (10000.0, s), -16.0, 0.01);
    for (int hf = 0; hf < 7; ++hf)
    {
        s = {}; s.hfIndex = hf; s.hfBoost = 10; s.hfBandwidth = 0;
        CHECK_NEAR (shape::analogResponseDb (sp::hfFreqHz[hf], s), 18.0, 0.01);
        CHECK_NEAR (shape::eqResponseDb (sp::hfFreqHz[hf], s, 44100.0), 18.0, 0.01); // the resonance is pre-warped exactly
        s.hfBandwidth = 10;
        CHECK_NEAR (shape::analogResponseDb (sp::hfFreqHz[hf], s), 13.5, 0.01);
        CHECK_NEAR (shape::eqResponseDb (sp::hfFreqHz[hf], s, 44100.0), 13.5, 0.01);
    }

    juce::String csv ("section,position,knob,analog_at_position_db,digital_44k1_db,digital_48k_db,digital_96k_db,plateau_db\n");
    for (int lf = 0; lf < 4; ++lf)
        for (int which = 0; which < 2; ++which)
        {
            s = {}; s.lfIndex = lf;
            (which == 0 ? s.lfBoost : s.lfAtten) = 10;
            const double f = sp::lfFreqHz[lf];
            const double a = shape::analogResponseDb (f, s);
            csv << (which == 0 ? "LF boost," : "LF atten,") << f << ",10," << a << "," << shape::eqResponseDb (f, s, 44100.0) << ","
                << shape::eqResponseDb (f, s, 48000.0) << "," << shape::eqResponseDb (f, s, 96000.0) << "," << shape::analogResponseDb (1.0, s) << "\n";
            MEASURE (juce::String (which == 0 ? "LF boost 10 at " : "LF atten 10 at ") + juce::String (f) + " Hz position, at the position (dB)", a);
            if (which == 0) CHECK (a > 11.5 && a < 14.0);
            else            CHECK (a < -16.0 && a > -18.5);
        }
    for (int at = 0; at < 3; ++at)
    {
        s = {}; s.attenIndex = at; s.hfAtten = 10;
        const double f = sp::hfAttenFreqHz[at];
        const double a = shape::analogResponseDb (f, s);
        csv << "HF atten," << f << ",10," << a << "," << (f < 20000.0 ? shape::eqResponseDb (f, s, 44100.0) : 0.0) << ","
            << (f < 20000.0 ? shape::eqResponseDb (f, s, 48000.0) : shape::eqResponseDb (19999.0, s, 48000.0)) << ","
            << shape::eqResponseDb (f, s, 96000.0) << "," << shape::analogResponseDb (1.0e6, s) << "\n";
        MEASURE ("HF atten 10 at " + juce::String (f) + " Hz position, at the position (dB)", a);
        CHECK (a < -15.5 && a > -16.6);
    }
    for (int hf = 0; hf < 7; ++hf)
        for (double bw : { 0.0, 5.0, 10.0 })
        {
            s = {}; s.hfIndex = hf; s.hfBoost = 10; s.hfBandwidth = bw;
            const double f = sp::hfFreqHz[hf];
            csv << "HF boost BW " << bw << "," << f << ",10," << shape::analogResponseDb (f, s) << "," << shape::eqResponseDb (f, s, 44100.0) << ","
                << shape::eqResponseDb (f, s, 48000.0) << "," << shape::eqResponseDb (f, s, 96000.0) << ",\n";
        }
    // knob law (MODEL_SPEC 2.4): knob 5 = 40 % of the full range in dB at the reference position
    juce::String knobs ("knob,lf_boost_60_db,lf_atten_60_db,hf_boost_10k_bw5_db,hf_atten_10k_db\n");
    for (int k = 0; k <= 10; ++k)
    {
        EqSettings b; b.lfBoost = k;
        EqSettings a; a.lfAtten = k;
        EqSettings h; h.hfBoost = k;
        EqSettings t; t.attenIndex = 1; t.hfAtten = k;
        knobs << k << "," << shape::analogResponseDb (60.0, b) << "," << shape::analogResponseDb (60.0, a) << ","
              << shape::analogResponseDb (10000.0, h) << "," << shape::analogResponseDb (10000.0, t) << "\n";
    }
    EqSettings b5; b5.lfBoost = 5;
    CHECK_NEAR (shape::analogResponseDb (60.0, b5), 0.4 * 13.5, 0.01);
    outputDirectory().getChildFile ("SHAPE_ranges.csv").replaceWithText (csv);
    outputDirectory().getChildFile ("SHAPE_knob_law.csv").replaceWithText (knobs);

    // The flat network is flat and the EQ IN Out response is 0 dB.
    EqSettings flat;
    double maxFlat = 0;
    for (double f : logGrid (10.0, 21000.0, 200))
        maxFlat = std::max ({ maxFlat, std::abs (shape::eqResponseDb (f, flat, 44100.0)), std::abs (shape::analogResponseDb (f, flat)) });
    MEASURE ("max |flat network response| (dB)", maxFlat);
    CHECK (maxFlat < 1.0e-9);
}

TEST ("SHAPE: LF boost + atten at the same setting do not cancel: boost below, dip above (all positions; processor-measured; CSV)")
{
    juce::String csv ("position,boost,atten,hz,response_db\n");
    const double fs = 48000.0;
    Multitone mt (fs, 20000.0);
    const auto ref = referenceAmplitudes (fs, mt);
    for (int lf = 0; lf < 4; ++lf)
        for (double knob : { 5.0, 10.0 })
        {
            EqSettings both; both.lfIndex = lf; both.lfBoost = knob; both.lfAtten = knob;
            EqSettings boost = both; boost.lfAtten = 0;
            EqSettings atten = both; atten.lfBoost = 0;
            const auto grid = logGrid (5.0, 20000.0, 400);
            double peak = -100, peakHz = 0, dip = 100, dipHz = 0, b3 = 0, a3 = 0;
            for (double f : grid)
            {
                const double r = shape::eqResponseDb (f, both, fs);
                csv << sp::lfFreqHz[lf] << "," << knob << "," << knob << "," << f << "," << r << "\n";
                if (r > peak) { peak = r; peakHz = f; }
                if (r < dip) { dip = r; dipHz = f; }
                // turnover: highest frequency where the boost alone is >= 3 dB / the atten alone <= -3 dB
                if (shape::eqResponseDb (f, boost, fs) >= 3.0) b3 = f;
                if (shape::eqResponseDb (f, atten, fs) <= -3.0) a3 = f;
            }
            const auto tag = sp::lfFreqNames[lf] + " boost = atten = " + juce::String (knob, 0);
            MEASURE (tag + ": peak (dB)", peak);
            MEASURE (tag + ": peak at (Hz)", peakHz);
            MEASURE (tag + ": dip (dB)", dip);
            MEASURE (tag + ": dip at (Hz)", dipHz);
            MEASURE (tag + ": boost -3 dB turnover (Hz)", b3);
            MEASURE (tag + ": atten -3 dB turnover (Hz)", a3);
            CHECK_MSG (peakHz < dipHz, tag);              // boost below, dip above
            CHECK_MSG (a3 > 1.5 * b3, tag);              // the atten acts higher than the boost
            if (knob == 10.0)
            {
                CHECK_MSG (peak > 10.0 && dip < -4.0, tag);
                // at the selected frequency the pair still boosts (boost > atten there)
                CHECK_MSG (shape::eqResponseDb (sp::lfFreqHz[lf], both, fs) > 6.0, tag);
            }
            // The processor really does it (not only the evaluator).
            const auto r = measureAgainstEvaluator (fs, both, mt, ref, nullptr);
            CHECK_MSG (std::abs (r.maxErrDb) < 0.01, tag + ": processor differs from the evaluator by " + juce::String (r.maxErrDb));
        }
    outputDirectory().getChildFile ("SHAPE_lf_boost_atten.csv").replaceWithText (csv);
}

TEST ("SHAPE: HF BANDWIDTH sweeps the boost from Sharp to Broad: Q falls monotonically, peak stays at the selected frequency (CSV)")
{
    juce::String csv ("hf_hz,bandwidth,peak_db,peak_hz,q_half_peak_db,q_minus3db\n");
    for (int hf = 0; hf < 7; ++hf)
    {
        double prevQ = 1.0e9;
        for (int bw = 0; bw <= 10; ++bw)
        {
            EqSettings s; s.hfIndex = hf; s.hfBoost = 10; s.hfBandwidth = bw;
            // Q from the analog prototype (the hardware's curve) and the run response at 96 kHz
            auto qOf = [&] (auto response)
            {
                const auto grid = logGrid (300.0, 40000.0, 4000);
                double peak = -100, peakHz = 0;
                for (double f : grid)
                    if (const double r = response (f); r > peak) { peak = r; peakHz = f; }
                // Q from the width where the boost is at least half its dB peak, and the
                // conventional width 3 dB below the peak
                double lo = 0, hi = 0, lo3 = 0, hi3 = 0;
                for (double f : grid)
                {
                    const double r = response (f);
                    if (r >= 0.5 * peak) { if (lo <= 0) lo = f; hi = f; }
                    if (r >= peak - 3.0) { if (lo3 <= 0) lo3 = f; hi3 = f; }
                }
                return std::tuple { peak, peakHz, peakHz / (hi - lo), peakHz / (hi3 - lo3) };
            };
            const auto [peak, peakHz, q, q3] = qOf ([&] (double f) { return shape::analogResponseDb (f, s); });
            csv << sp::hfFreqHz[hf] << "," << bw << "," << peak << "," << peakHz << "," << q << "," << q3 << "\n";
            CHECK_MSG (q < prevQ, "Q not falling at " + sp::hfFreqNames[hf] + " bandwidth " + juce::String (bw));
            CHECK_NEAR (peakHz / sp::hfFreqHz[hf], 1.0, 0.01);
            prevQ = q;
            if (bw == 0 || bw == 10)
            {
                const juce::String where = sp::hfFreqNames[hf] + (bw == 0 ? " Sharp" : " Broad");
                MEASURE (where + ": Q from the half-peak-dB width", q);
                MEASURE (where + ": Q from the -3 dB width", q3);
            }
            if (hf == 4)
            {
                const auto [dpeak, dpeakHz, dq, dq3] = qOf ([&] (double f) { return f < 43000.0 ? shape::eqResponseDb (f, s, 96000.0) : -100.0; });
                CHECK_NEAR (dpeak, peak, 0.01);
                CHECK_NEAR (dq / q, 1.0, 0.03);
                CHECK_NEAR (dq3 / q3, 1.0, 0.03);
            }
        }
    }
    outputDirectory().getChildFile ("SHAPE_hf_bandwidth.csv").replaceWithText (csv);
}

TEST ("SHAPE: digital vs analog prototype: documented deviation at 44.1 / 48 / 96 / 192 kHz (CSV)")
{
    juce::String csv ("host_rate,setting,max_dev_db_to_10k,max_dev_db_to_20k,at_hz\n");
    for (double host : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        double worst10 = 0, worst20 = 0, worstLf = 0;
        for (const auto& s : curveMatrix())
        {
            double w10 = 0, w20 = 0, at = 0;
            for (double f : logGrid (20.0, std::min (20000.0, 0.45 * host), 500))
            {
                const double d = std::abs (shape::eqResponseDb (f, s, host) - shape::analogResponseDb (f, s));
                if (f <= 10000.0) w10 = std::max (w10, d);
                if (d > w20) { w20 = d; at = f; }
                if (f <= 1000.0) worstLf = std::max (worstLf, d);
            }
            csv << host << ",\"" << describe (s) << "\"," << w10 << "," << w20 << "," << at << "\n";
            worst10 = std::max (worst10, w10);
            worst20 = std::max (worst20, w20);
        }
        MEASURE ("max |digital - analog| up to 1 kHz (dB) @" + juce::String (host), worstLf);
        MEASURE ("max |digital - analog| up to 10 kHz (dB) @" + juce::String (host), worst10);
        MEASURE ("max |digital - analog| up to 20 kHz (dB) @" + juce::String (host), worst20);
        // documented in MODEL_SPEC 4.4: the HF resonator's Z0 pre-warp (gamma) trades a few
        // hundredths of a dB on the LF skirts for an exact peak frequency and height
        CHECK (worstLf < 0.03);
        CHECK (worst10 < 0.2);
        CHECK (worst20 < 0.3);
    }
    outputDirectory().getChildFile ("SHAPE_digital_vs_analog.csv").replaceWithText (csv);
}

TEST ("SHAPE: EQ IN Out removes the curves but keeps the transformer/amplifier colour; host Bypass is fully dry")
{
    const double fs = 48000.0;
    EqSettings big; big.lfBoost = 10; big.lfAtten = 4; big.hfBoost = 10; big.hfBandwidth = 2; big.hfAtten = 6; big.attenIndex = 1;

    // 1) curves: with EQ IN Out, a heavy setting measures flat against the flat setting.
    Multitone mt (fs, 20000.0);
    auto measure = [&] (const EqSettings& s, int character)
    {
        auto p = makeShape (fs, 256);
        setParam (*p, sp::character, (float) character);
        applyEq (*p, s);
        p->prepareToPlay (fs, 256);
        return mt.amplitudes (render (*p, mt.signal (2), 256));
    };
    EqSettings flat;
    EqSettings bigOut = big; bigOut.eqIn = false;
    const auto aFlat = measure (flat, cOriginal), aOut = measure (bigOut, cOriginal), aIn = measure (big, cOriginal);
    double devOut = 0, devIn = 0;
    for (size_t k = 0; k < mt.hz.size(); ++k)
    {
        devOut = std::max (devOut, std::abs (20.0 * std::log10 (aOut[k] / aFlat[k])));
        devIn = std::max (devIn, std::abs (20.0 * std::log10 (aIn[k] / aFlat[k])));
    }
    MEASURE ("EQ IN Out with heavy settings: max deviation from the flat network (dB)", devOut);
    MEASURE ("EQ IN In with the same settings: max deviation (dB)", devIn);
    CHECK (devOut < 0.001);
    CHECK (devIn > 10.0);

    // 2) colour: with EQ IN Out the analog stages still distort exactly as with a flat EQ in.
    EqSettings outFlat; outFlat.eqIn = false;
    const auto tIn = renderTone (cOriginal, 25.0f, -6.0f, 1000.0, fs, flat);
    const auto tOut = renderTone (cOriginal, 25.0f, -6.0f, 1000.0, fs, outFlat);
    const auto tOutLf = renderTone (cOriginal, 25.0f, -6.0f, 40.0, fs, outFlat);
    MEASURE ("THD 1 kHz -6 dBFS, EQ flat In (%)", pct (tIn.thd));
    MEASURE ("THD 1 kHz -6 dBFS, EQ Out (%)", pct (tOut.thd));
    MEASURE ("THD 40 Hz -6 dBFS, EQ Out (%)", pct (tOutLf.thd));
    CHECK (tOut.thd > 0.0005);
    CHECK_NEAR (tOut.thd / tIn.thd, 1.0, 0.01);
    CHECK (tOutLf.thd > tOut.thd); // the transformers' LF colour stays

    // 3) host Bypass: bit-exact delayed dry; EQ IN Out is not.
    auto p = makeShape (fs, 256);
    applyEq (*p, bigOut);
    p->prepareToPlay (fs, 256);
    const auto sig = toBuffer (sine (fs, 60.0, 0.5f, 48000), 2);
    const auto eqOut = render (*p, sig, 256);
    const int latency = p->getLatencySamples();
    const double diff = maxAbsDiff (eqOut, delayed (sig, latency));
    MEASURE ("EQ IN Out: max |output - delayed dry|", diff);
    CHECK (diff > 1.0e-4);
    setParam (*p, sp::bypass, 1.0f);
    p->prepareToPlay (fs, 256);
    CHECK_MSG (identical (render (*p, sig, 256), delayed (sig, latency)), "host bypass not bit-exact");

    // 4) EQ IN toggling is click-free and lands on the right curve.
    auto q = makeShape (fs, 64);
    applyEq (*q, big);
    setParam (*q, sp::character, (float) cClean);
    setParam (*q, sp::drive, 0.0f);
    q->prepareToPlay (fs, 64);
    juce::MidiBuffer midi;
    const double c2 = 2.0 * std::cos (2.0 * pi * 100.0 / fs);
    double h[4] {}, maxR = 0;
    for (int b = 0; b < 1500; ++b)
    {
        if (b % 150 == 0 && b > 0)
            setParam (*q, sp::eqIn, (b / 150) % 2 == 0 ? 1.0f : 0.0f);
        auto buf = toBuffer (sine (fs, 100.0, 0.2f, 64, 2.0 * pi * 100.0 * b * 64 / fs), 2);
        q->processBlock (buf, midi);
        for (int i = 0; i < 64; ++i)
        {
            const double y = buf.getSample (0, i);
            const double r1 = y - c2 * h[0] + h[1];
            const double r2 = r1 - c2 * h[2] + h[3];
            h[1] = h[0]; h[0] = y; h[3] = h[2]; h[2] = r1;
            if (b > 20) maxR = std::max (maxR, std::abs (r2));
        }
    }
    MEASURE ("EQ IN toggling: max |double sine-predictor residual|", maxR);
    CHECK (maxR < 1.0e-4);
}

TEST ("SHAPE: harmonic spectrum, THD and THD+N vs level per CHARACTER and DRIVE (stepped sine 1 kHz and 50 Hz; CSV)")
{
    juce::String csv ("character,drive_pct,hz,level_dbfs,thd_pct,thdn_pct,h2_dbc,h3_dbc,gain_db\n");
    const float levels[] = { -40.0f, -30.0f, -18.0f, -12.0f, -8.0f, -3.0f, 0.0f, 3.0f, 6.0f };
    std::map<std::tuple<int, int, int>, ToneResult> res;
    auto R = [&res] (int c, int f, int li) -> ToneResult& { return res[std::make_tuple (c, f, li)]; };
    for (int c : { cClean, cOriginal, cBurn })
        for (double hz : { 1000.0, 50.0 })
            for (int li = 0; li < 9; ++li)
            {
                const auto r = renderTone (c, 0.0f, levels[li], hz);
                R (c, hz > 100 ? 1 : 0, li) = r;
                csv << charName[c] << ",0," << hz << "," << levels[li] << "," << pct (r.thd) << "," << pct (r.thdN) << "," << dbc (r.h2) << ","
                    << dbc (r.h3) << "," << r.fundDb << "\n";
            }
    // table to the log
    for (int c : { cClean, cOriginal, cBurn })
        for (int li : { 1, 2, 4, 6, 8 })
        {
            const auto& r = R (c, 1, li);
            MEASURE (juce::String (charName[c]) + " DRIVE 0, 1 kHz " + juce::String (levels[li], 0) + " dBFS: THD %", pct (r.thd));
            MEASURE ("    THD+N %", pct (r.thdN));
            MEASURE ("    H2 (dBc)", dbc (r.h2));
            MEASURE ("    H3 (dBc)", dbc (r.h3));
            MEASURE ("    50 Hz THD %", pct (R (c, 0, li).thd));
        }
    for (int f = 0; f < 2; ++f)
        for (int li = 0; li < 9; ++li)
        {
            // Clean < Original < Burn at every level
            CHECK_MSG (R (cClean, f, li).thd < R (cOriginal, f, li).thd, "Clean vs Original at " + juce::String (levels[li]));
            CHECK_MSG (R (cOriginal, f, li).thd < R (cBurn, f, li).thd, "Original vs Burn at " + juce::String (levels[li]));
            if (li > 0) // level-dependent: THD rises with level
                for (int c : { cClean, cOriginal, cBurn })
                    CHECK_MSG (R (c, f, li).thd > R (c, f, li - 1).thd, juce::String (charName[c]) + " THD not rising at " + juce::String (levels[li]));
        }
    // Calibration anchors (MODEL_SPEC 5.5): Original at +10 dBu (-8 dBFS) <= 0.15 % at 1 kHz (S1),
    // third-harmonic tendency at nominal (S7), LF transformer colour, Clean far cleaner.
    const auto& nominal = R (cOriginal, 1, 4);
    CHECK (nominal.thd < 0.0015 && nominal.thd > 0.0005);
    CHECK (nominal.h3 > nominal.h2);
    CHECK (R (cOriginal, 0, 4).thd > 2.0 * nominal.thd);
    CHECK (R (cClean, 1, 4).thd < 0.2 * nominal.thd);
    CHECK (R (cOriginal, 1, 8).thd > 0.02); // +24 dBu: audibly crunchy
    // EQ curves preserved by every CHARACTER: small-signal gain within 0.01 dB
    for (int c : { cClean, cOriginal, cBurn })
        CHECK_NEAR (R (c, 1, 0).fundDb, 0.0, 0.01);

    // DRIVE: level into the stages, compensated (gain unchanged); THD rises monotonically.
    double prev = 0;
    for (float d : { 0.0f, 25.0f, 50.0f, 75.0f, 100.0f })
    {
        const auto r = renderTone (cOriginal, d, -18.0f, 1000.0);
        const auto small = renderTone (cOriginal, d, -50.0f, 1000.0);
        csv << "Original," << d << ",1000,-18," << pct (r.thd) << "," << pct (r.thdN) << "," << dbc (r.h2) << "," << dbc (r.h3) << "," << r.fundDb << "\n";
        MEASURE ("Original DRIVE " + juce::String (d, 0) + " %, 1 kHz -18 dBFS: THD %", pct (r.thd));
        MEASURE ("    small-signal gain (dB)", small.fundDb);
        CHECK (r.thd > prev);
        CHECK_NEAR (small.fundDb, 0.0, 0.01);
        prev = r.thd;
    }
    // DRIVE +12 dB at -18 dBFS = DRIVE 0 at -6 dBFS (the drive is the level)
    const auto d50 = renderTone (cOriginal, 50.0f, -18.0f, 1000.0);
    const auto l6 = renderTone (cOriginal, 0.0f, -6.0f, 1000.0);
    CHECK_NEAR (dbc (d50.thd), dbc (l6.thd), 0.05);
    outputDirectory().getChildFile ("SHAPE_thd.csv").replaceWithText (csv);
}

TEST ("SHAPE: symmetry: even/odd harmonic balance per CHARACTER; LF boost drives the output stages harder (level dependence through the EQ)")
{
    for (int c : { cClean, cOriginal, cBurn })
    {
        const auto r = renderTone (c, 25.0f, -6.0f, 1000.0);
        MEASURE (juce::String (charName[c]) + ": H2 - H3 at 1 kHz -6 dBFS, DRIVE 25 % (dB)", dbc (r.h2) - dbc (r.h3));
    }
    // Burn is the most asymmetric (largest H2 relative to H3)
    const auto o = renderTone (cOriginal, 25.0f, -6.0f, 1000.0), b = renderTone (cBurn, 25.0f, -6.0f, 1000.0);
    CHECK (b.h2 > 2.0 * o.h2);
    // An LF boost raises the level into the amplifier and output transformer: more THD on a 40 Hz tone.
    EqSettings boost; boost.lfIndex = 2; boost.lfBoost = 10;
    const auto flat = renderTone (cOriginal, 25.0f, -24.0f, 40.0);
    const auto boosted = renderTone (cOriginal, 25.0f, -24.0f, 40.0, 48000.0, boost);
    MEASURE ("40 Hz THD at -24 dBFS, flat (%)", pct (flat.thd));
    MEASURE ("40 Hz THD at -24 dBFS, LF boost 10 (%)", pct (boosted.thd));
    MEASURE ("    gain of the boosted tone (dB)", boosted.fundDb);
    CHECK (boosted.fundDb > 11.0);
    CHECK (boosted.thd > 3.0 * flat.thd); // the boost comes before the amplifier and output transformer
}

TEST ("SHAPE: aliasing of the analog stages: oversampled path vs the same model at 1x (CSV)")
{
    auto aliasDb = [] (const float* d, int n, double rate, double f, double foldRate, double band)
    {
        double worst = 0;
        const double fund = toneAmplitude (d, n, rate, f);
        for (int k = 2; k <= 80; ++k)
        {
            double a = std::fmod (k * f, foldRate);
            if (a > foldRate * 0.5) a = foldRate - a;
            if (a > band || a < 20.0) continue;
            const double r = a / f;
            if (std::abs (r - std::round (r)) < 1e-6) continue;
            worst = std::max (worst, toneAmplitude (d, n, rate, a) / fund);
        }
        return dbc (worst);
    };
    juce::String csv ("rate,tone_hz,setting,alias_oversampled_dbc,alias_1x_dbc\n");
    double worst[3] = { -300, -300, -300 };
    const char* tierName[] = { "defaults", "Original DRIVE 100", "Burn DRIVE 100" };
    for (double fs : { 44100.0, 48000.0 })
        for (double f : { 5000.0, 7000.0, 9000.0 })
            for (int tier : { 0, 1, 2 })
            {
                const int character = tier == 2 ? cBurn : cOriginal;
                const float drive = tier == 0 ? 25.0f : 100.0f;
                const float level = -1.0f;
                auto p = makeShape (fs, 512);
                setParam (*p, sp::character, (float) character);
                setParam (*p, sp::drive, drive);
                p->prepareToPlay (fs, 512);
                const int n = (int) fs;
                const auto out = render (*p, toBuffer (sine (fs, f, juce::Decibels::decibelsToGain (level), 2 * n), 2), 512);
                const double aOs = aliasDb (out.getReadPointer (0) + n, n, fs, f, fs * p->getOversamplingFactor(), 0.45 * fs);
                ShapeModel m;
                m.setCharacter (character);
                m.setDrive (drive * 0.01f);
                m.prepare (fs, 1);
                auto x = sine (fs, f, juce::Decibels::decibelsToGain (level), 2 * n);
                float* ch[] = { x.data() };
                m.process (ch, 1, 2 * n);
                const double a1 = aliasDb (x.data() + n, n, fs, f, fs, 0.45 * fs);
                csv << fs << "," << f << ",\"" << tierName[tier] << "\"," << aOs << "," << a1 << "\n";
                MEASURE (juce::String (fs) + " Hz, " + juce::String (f) + " Hz at -1 dBFS, " + tierName[tier] + ": worst alias (dBc)", aOs);
                MEASURE ("    same model at 1x (dBc)", a1);
                worst[tier] = std::max (worst[tier], aOs);
                CHECK (aOs < a1 - 10.0);
            }
    outputDirectory().getChildFile ("SHAPE_aliasing.csv").replaceWithText (csv);
    for (int t = 0; t < 3; ++t)
        MEASURE (juce::String ("worst alias, ") + tierName[t] + " (dBc)", worst[t]);
    // defaults: below the noise of any converter; the extreme corners (+24 / +30 dB into
    // the stages at -1 dBFS, heavily clipped) are limited by the 4x oversampling (MODEL_SPEC 5.7)
    CHECK (worst[0] < -90.0);
    CHECK (worst[1] < -60.0);
    CHECK (worst[2] < -30.0);
}

TEST ("SHAPE: every control changes the audio as specified (audio-effect test per visible control)")
{
    const double fs = 48000.0;
    auto gainAt = [&] (std::function<void (ShapeProcessor&)> setup, double hz, float level = -30.0f)
    {
        auto p = makeShape (fs, 256);
        cleanColour (*p);
        setup (*p);
        p->prepareToPlay (fs, 256);
        const float amp = juce::Decibels::decibelsToGain (level);
        auto out = render (*p, toBuffer (sine (fs, hz, amp, 24000), 2), 256);
        return 20.0 * std::log10 (toneAmplitude (out.getReadPointer (0) + 12000, 12000, fs, hz) / amp);
    };
    auto none = [] (ShapeProcessor&) {};
    const double base60 = gainAt (none, 60.0), base10k = gainAt (none, 10000.0);
    CHECK_NEAR (base60, 0.0, 0.05);
    CHECK_NEAR (base10k, 0.0, 0.05);
    const double lfB = gainAt ([] (ShapeProcessor& p) { setParam (p, sp::lfBoost, 10.0f); }, 60.0);
    const double lfA = gainAt ([] (ShapeProcessor& p) { setParam (p, sp::lfAtten, 10.0f); }, 60.0);
    const double lfF = gainAt ([] (ShapeProcessor& p) { setParam (p, sp::lfBoost, 10.0f); setParam (p, sp::lfFreq, 0.0f); }, 60.0);
    const double hfB = gainAt ([] (ShapeProcessor& p) { setParam (p, sp::hfBoost, 10.0f); }, 10000.0);
    const double hfF = gainAt ([] (ShapeProcessor& p) { setParam (p, sp::hfBoost, 10.0f); setParam (p, sp::hfFreq, 0.0f); }, 10000.0);
    const double hfBW = gainAt ([] (ShapeProcessor& p) { setParam (p, sp::hfBoost, 10.0f); setParam (p, sp::hfBandwidth, 0.0f); }, 10000.0);
    const double hfBWoff = gainAt ([] (ShapeProcessor& p) { setParam (p, sp::hfBoost, 10.0f); setParam (p, sp::hfBandwidth, 0.0f); }, 7000.0);
    const double hfBWbroad = gainAt ([] (ShapeProcessor& p) { setParam (p, sp::hfBoost, 10.0f); setParam (p, sp::hfBandwidth, 10.0f); }, 7000.0);
    const double hfBWbroadPk = gainAt ([] (ShapeProcessor& p) { setParam (p, sp::hfBoost, 10.0f); setParam (p, sp::hfBandwidth, 10.0f); }, 10000.0);
    const double hfBWsharp4k = gainAt ([] (ShapeProcessor& p) { setParam (p, sp::hfBoost, 10.0f); setParam (p, sp::hfBandwidth, 0.0f); }, 4000.0);
    const double hfBWbroad4k = gainAt ([] (ShapeProcessor& p) { setParam (p, sp::hfBoost, 10.0f); setParam (p, sp::hfBandwidth, 10.0f); }, 4000.0);
    const double att = gainAt ([] (ShapeProcessor& p) { setParam (p, sp::hfAtten, 10.0f); setParam (p, sp::hfAttenFreq, 0.0f); }, 3000.0);
    const double attF = gainAt ([] (ShapeProcessor& p) { setParam (p, sp::hfAtten, 10.0f); setParam (p, sp::hfAttenFreq, 2.0f); }, 3000.0);
    const double eqOff = gainAt ([] (ShapeProcessor& p) { setParam (p, sp::lfBoost, 10.0f); setParam (p, sp::eqIn, 0.0f); }, 60.0);
    const double out6 = gainAt ([] (ShapeProcessor& p) { setParam (p, sp::output, 6.0f); }, 1000.0);
    const double mixHalf = gainAt ([] (ShapeProcessor& p) { setParam (p, sp::hfBoost, 10.0f); setParam (p, sp::mix, 50.0f); }, 10000.0);
    MEASURE ("LF BOOST 10 at 60 Hz (dB)", lfB);
    MEASURE ("LF ATTEN 10 at 60 Hz (dB)", lfA);
    MEASURE ("LF FREQ 20 Hz, BOOST 10, at 60 Hz (dB)", lfF);
    MEASURE ("HF BOOST 10 (10 kHz) at 10 kHz (dB)", hfB);
    MEASURE ("HF FREQ 3 kHz, BOOST 10, at 10 kHz (dB)", hfF);
    MEASURE ("BANDWIDTH Sharp: at 10 kHz / at 7 kHz (dB)", hfBW);
    MEASURE ("    at 7 kHz Sharp vs Broad (dB)", hfBWoff - hfBWbroad);
    MEASURE ("BANDWIDTH Broad: at 10 kHz (dB)", hfBWbroadPk);
    MEASURE ("    4 kHz re peak, Sharp / Broad (dB)", hfBWsharp4k - hfBW);
    MEASURE ("    4 kHz re peak, Broad (dB)", hfBWbroad4k - hfBWbroadPk);
    MEASURE ("ATTEN SEL 5 kHz, ATTEN 10, at 3 kHz (dB)", att);
    MEASURE ("ATTEN SEL 20 kHz, ATTEN 10, at 3 kHz (dB)", attF);
    MEASURE ("EQ IN Out with LF BOOST 10 at 60 Hz (dB)", eqOff);
    MEASURE ("OUTPUT +6 (dB)", out6);
    MEASURE ("MIX 50 % with HF BOOST 10, at 10 kHz (dB)", mixHalf);
    CHECK_NEAR (lfB, 13.5, 0.1);
    CHECK_NEAR (lfA, -17.5, 0.1);
    CHECK (lfF < lfB - 3.0);
    CHECK (hfB > 15.0 && hfF < hfB - 8.0);
    CHECK_NEAR (hfBW, 18.0, 0.1);
    // Broad: lower peak, wider skirt (relative to its own peak)
    CHECK (hfBWbroadPk < hfBW - 3.0);
    CHECK (hfBWbroad4k - hfBWbroadPk > hfBWsharp4k - hfBW + 2.0);
    CHECK (att < -10.0 && attF > att + 8.0);
    CHECK_NEAR (eqOff, 0.0, 0.05);
    CHECK_NEAR (out6, 6.0, 0.01);
    // MIX 50 %: the latency-aligned dry and the wet add in phase at 10 kHz (the network is near zero phase at its peak).
    CHECK (mixHalf > 8.0 && mixHalf < hfB);
    // DRIVE and CHARACTER: THD (detailed in the THD test)
    const auto d0 = renderTone (cOriginal, 0.0f, -12.0f, 1000.0), d100 = renderTone (cOriginal, 100.0f, -12.0f, 1000.0);
    const auto cc = renderTone (cClean, 25.0f, -12.0f, 1000.0), cb = renderTone (cBurn, 25.0f, -12.0f, 1000.0);
    CHECK (d100.thd > 10.0 * d0.thd);
    CHECK (cb.thd > 10.0 * cc.thd);
}

TEST ("SHAPE: switching every control (selectors included) is click-free; rapid automation stays finite")
{
    // Click measure: the twice-applied sine predictor of the test tone,
    // r2 = A(z) A(z) y with A(z) = 1 - 2 cos(w) z^-1 + z^-2. It is 0 for a steady
    // sine and for a sine whose amplitude/phase glide linearly, tiny for smooth
    // glides, and equals the size of a step. Limit: 2x the worst fixed setting,
    // or 1e-4 (a -80 dBFS step), whichever is larger.
    const double fs = 48000.0;
    const int block = 64, blocks = 1600, period = 53;
    struct Ctl { const char* id; float a, b; double toneHz; };
    const Ctl ctls[] = {
        { sp::lfFreq, 0.0f, 3.0f, 100.0 }, { sp::lfBoost, 0.0f, 10.0f, 100.0 }, { sp::lfAtten, 0.0f, 10.0f, 100.0 },
        { sp::hfFreq, 0.0f, 6.0f, 3000.0 }, { sp::hfBoost, 0.0f, 10.0f, 3000.0 }, { sp::hfBandwidth, 0.0f, 10.0f, 3000.0 },
        { sp::hfAttenFreq, 0.0f, 2.0f, 3000.0 }, { sp::hfAtten, 0.0f, 10.0f, 3000.0 }, { sp::drive, 0.0f, 100.0f, 100.0 },
        { sp::character, 0.0f, 2.0f, 100.0 }, { sp::mix, 30.0f, 100.0f, 100.0 }, { sp::output, -12.0f, 6.0f, 100.0 },
        { sp::eqIn, 0.0f, 1.0f, 100.0 } };
    auto run = [&] (int which, int fixed, double toneHz)
    {
        auto p = makeShape (fs, block);
        cleanColour (*p);
        setParam (*p, sp::lfBoost, 6.0f);
        setParam (*p, sp::lfAtten, 3.0f);
        setParam (*p, sp::hfBoost, 6.0f);
        setParam (*p, sp::hfAtten, 4.0f);
        setParam (*p, sp::hfAttenFreq, 0.0f);
        const auto& c = ctls[which];
        if (fixed >= 0)
            setParam (*p, c.id, fixed == 0 ? c.a : c.b);
        p->prepareToPlay (fs, block);
        juce::MidiBuffer midi;
        const double c2 = 2.0 * std::cos (2.0 * pi * toneHz / fs);
        double h[4] {}, maxR = 0;
        const int n = fixed >= 0 ? 400 : blocks;
        for (int b = 0; b < n; ++b)
        {
            if (fixed < 0 && b % period == 0 && b > 0)
                setParam (*p, c.id, (b / period) % 2 == 0 ? c.a : c.b);
            auto buf = toBuffer (sine (fs, toneHz, 0.1f, block, 2.0 * pi * toneHz * b * block / fs), 2);
            p->processBlock (buf, midi);
            for (int i = 0; i < block; ++i)
            {
                const double y = buf.getSample (0, i);
                const double r1 = y - c2 * h[0] + h[1];
                const double r2 = r1 - c2 * h[2] + h[3];
                h[1] = h[0]; h[0] = y; h[3] = h[2]; h[2] = r1;
                if (b > (fixed >= 0 ? n / 2 : 40)) maxR = std::max (maxR, std::abs (r2));
            }
        }
        return maxR;
    };
    for (int w = 0; w < (int) std::size (ctls); ++w)
    {
        const double base = std::max (run (w, 0, ctls[w].toneHz), run (w, 1, ctls[w].toneHz));
        const double sw = run (w, -1, ctls[w].toneHz);
        const double limit = std::max (2.0 * base, 1.0e-4);
        MEASURE (juce::String ("switching ") + ctls[w].id + ": max |r2| (fixed settings: " + juce::String (base, 8) + ")", sw);
        CHECK_MSG (sw < limit, juce::String ("click when switching ") + ctls[w].id);
    }

    auto p = makeShape (fs, 64);
    juce::Random rng (42);
    float prevSample = 0.0f, maxJump = 0.0f;
    bool finite = true;
    juce::MidiBuffer midi;
    for (int b = 0; b < 3000; ++b)
    {
        setParam (*p, sp::lfFreq, (float) rng.nextInt (4));
        setParam (*p, sp::lfBoost, rng.nextFloat() * 10.0f);
        setParam (*p, sp::lfAtten, rng.nextFloat() * 10.0f);
        setParam (*p, sp::hfFreq, (float) rng.nextInt (7));
        setParam (*p, sp::hfBoost, rng.nextFloat() * 10.0f);
        setParam (*p, sp::hfBandwidth, rng.nextFloat() * 10.0f);
        setParam (*p, sp::hfAttenFreq, (float) rng.nextInt (3));
        setParam (*p, sp::hfAtten, rng.nextFloat() * 10.0f);
        setParam (*p, sp::drive, rng.nextFloat() * 100.0f);
        setParam (*p, sp::character, (float) rng.nextInt (3));
        setParam (*p, sp::mix, rng.nextFloat() * 100.0f);
        setParam (*p, sp::output, -6.0f + rng.nextFloat() * 12.0f);
        setParam (*p, sp::eqIn, rng.nextBool() ? 1.0f : 0.0f);
        auto buf = toBuffer (sine (fs, 200.0, 0.3f, 64, 2.0 * pi * 200.0 * b * 64 / fs), 2);
        p->processBlock (buf, midi);
        for (int i = 0; i < 64; ++i)
        {
            finite = finite && std::isfinite (buf.getSample (0, i));
            maxJump = std::max (maxJump, std::abs (buf.getSample (0, i) - prevSample));
            prevSample = buf.getSample (0, i);
        }
    }
    MEASURE ("rapid automation: max sample-to-sample jump", maxJump);
    CHECK (finite);
    CHECK (maxJump < 0.25f);
}

TEST ("SHAPE: mono and stereo: channels are independent and mono matches stereo's left channel")
{
    const double fs = 48000.0;
    auto st = makeShape (fs, 256, 2);
    auto mo = makeShape (fs, 256, 1);
    for (auto* p : { st.get(), mo.get() })
    {
        setParam (*p, sp::lfBoost, 7.0f);
        setParam (*p, sp::hfBoost, 5.0f);
        setParam (*p, sp::character, (float) cBurn);
        p->prepareToPlay (fs, 256);
    }
    juce::AudioBuffer<float> in (2, 24000);
    const auto l = sine (fs, 300.0, 0.5f, 24000);
    in.copyFrom (0, 0, l.data(), 24000);
    in.clear (1, 0, 24000);
    const auto o = render (*st, in, 256);
    CHECK (o.getMagnitude (1, 0, 24000) == 0.0f);
    juce::AudioBuffer<float> monoIn (1, 24000);
    monoIn.copyFrom (0, 0, l.data(), 24000);
    const auto om = render (*mo, monoIn, 256);
    double d = 0;
    for (int i = 0; i < 24000; ++i)
        d = std::max (d, (double) std::abs (om.getSample (0, i) - o.getSample (0, i)));
    MEASURE ("max |mono - stereo left|", d);
    CHECK (d < 1.0e-6);
}

TEST ("SHAPE: state round trip restores every parameter and renders identically")
{
    const double fs = 48000.0;
    auto a = makeShape (fs, 256);
    setParam (*a, sp::lfFreq, 1.0f);
    setParam (*a, sp::lfBoost, 6.5f);
    setParam (*a, sp::lfAtten, 3.2f);
    setParam (*a, sp::hfFreq, 5.0f);
    setParam (*a, sp::hfBoost, 4.4f);
    setParam (*a, sp::hfBandwidth, 7.7f);
    setParam (*a, sp::hfAttenFreq, 0.0f);
    setParam (*a, sp::hfAtten, 2.1f);
    setParam (*a, sp::drive, 61.0f);
    setParam (*a, sp::character, (float) cBurn);
    setParam (*a, sp::mix, 72.0f);
    setParam (*a, sp::output, -3.5f);
    setParam (*a, sp::eqIn, 0.0f);
    a->setUiProperty ("scale", 1.25);

    juce::MemoryBlock state;
    a->getStateInformation (state);
    auto b = makeShape (fs, 256);
    b->setStateInformation (state.getData(), (int) state.getSize());
    for (auto* param : a->getParameters())
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (param))
            CHECK_MSG (std::abs (r->getValue() - b->apvts.getParameter (r->paramID)->getValue()) < 1e-6f, r->paramID);
    CHECK_NEAR ((double) b->getUiProperty ("scale", 1.0), 1.25, 1e-9);

    a->prepareToPlay (fs, 256);
    b->prepareToPlay (fs, 256);
    const auto sig = toBuffer (sine (fs, 300.0, 0.4f, 48000), 2);
    CHECK_MSG (identical (render (*a, sig, 256), render (*b, sig, 256)), "restored instance renders differently");
    auto fresh = makeShape (fs, 256);
    CHECK (! identical (render (*fresh, sig, 256), render (*b, sig, 256)));

    b->setStateInformation ("nonsense", 8);
    CHECK_NEAR (plainValue (*b, sp::mix), 72.0f, 0.01);
}

TEST ("SHAPE: factory presets load, set every parameter and reject other plugins' presets")
{
    ShapeProcessor p;
    const auto& list = p.presets.getPresets();
    int factory = 0;
    for (auto& pr : list) factory += pr.isFactory ? 1 : 0;
    MEASURE ("factory presets", factory);
    CHECK (factory >= 8 && factory <= 12);

    std::set<int> charSeen, eqInSeen;
    std::set<juce::String> names;
    for (int i = 0; i < (int) list.size(); ++i)
        if (list[(size_t) i].isFactory)
        {
            p.loadPreset (i);
            names.insert (list[(size_t) i].name);
            CHECK (list[(size_t) i].params.values.size() == 13);
            for (auto& [id, v] : list[(size_t) i].params.values)
            {
                auto* r = p.apvts.getParameter (id);
                CHECK_MSG (r != nullptr, id);
                if (r) CHECK_MSG (std::abs (r->convertFrom0to1 (r->getValue()) - v) < 0.06f, list[(size_t) i].name + ": " + id);
            }
            CHECK (! p.isPresetModified());
            charSeen.insert (juce::roundToInt (plainValue (p, sp::character)));
            eqInSeen.insert (juce::roundToInt (plainValue (p, sp::eqIn)));
        }
    CHECK (charSeen.size() == 3 && eqInSeen.size() == 2);
    CHECK ((int) names.size() == factory);

    setParam (p, sp::mix, 13.0f);
    CHECK (p.isPresetModified());
    PresetManager::Preset out;
    CHECK (p.presets.parse (R"({"format":"tleet-preset","version":1,"plugin":"VEIL","name":"x","params":{}})", out).failed());
    CHECK (p.presets.parse (R"({"format":"tleet-preset","version":99,"plugin":"SHAPE","name":"x","params":{}})", out).failed());
    const auto json = p.stateToClipboardJson();
    setParam (p, sp::mix, 99.0f);
    CHECK (p.pasteStateFromJson (json).wasOk());
    CHECK_NEAR (plainValue (p, sp::mix), 13.0f, 0.06);

    for (int i = 0; i < (int) list.size(); ++i)
    {
        auto q = makeShape (48000.0, 256);
        q->loadPreset (i);
        q->prepareToPlay (48000.0, 256);
        const auto o = render (*q, toBuffer (sine (48000.0, 200.0, 0.5f, 24000), 2), 256);
        CHECK_MSG (allFinite (o) && rms (o.getReadPointer (0), 24000) > 0.05f, list[(size_t) i].name);
    }
}

TEST ("SHAPE: CPU cost per instance (measurement)")
{
    for (double fs : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        auto p = makeShape (fs, 128);
        setParam (*p, sp::lfBoost, 6.0f);
        setParam (*p, sp::lfAtten, 3.0f);
        setParam (*p, sp::hfBoost, 5.0f);
        setParam (*p, sp::hfAtten, 2.0f);
        setParam (*p, sp::mix, 80.0f);
        p->prepareToPlay (fs, 128);
        juce::Random rng (1);
        const int n = (int) (fs * 5.0);
        juce::AudioBuffer<float> io (2, n);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < n; ++i)
                io.setSample (ch, i, 0.5f * (float) std::sin (i * 0.031) * (rng.nextFloat() * 0.6f + 0.4f));
        juce::MidiBuffer midi;
        std::vector<double> times;
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        for (int pos = 0; pos + 128 <= n; pos += 128)
        {
            juce::AudioBuffer<float> view (io.getArrayOfWritePointers(), 2, pos, 128);
            const auto b0 = juce::Time::getHighResolutionTicks();
            p->processBlock (view, midi);
            times.push_back (juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - b0));
        }
        const double ms = juce::Time::getMillisecondCounterHiRes() - t0;
        std::sort (times.begin(), times.end());
        const double load = ms / 5000.0 * 100.0;
        const double medianLoad = times[times.size() / 2] * (fs / 128.0) * 100.0;
        const auto tag = juce::String (fs) + " Hz, block 128, stereo";
        MEASURE (tag + ": % of one core", load);
        MEASURE (tag + ": % of one core from the median block", medianLoad);
        MEASURE (tag + ": p99 block time (us)", times[(size_t) (0.99 * (double) times.size())] * 1e6);
        MEASURE (tag + ": max block time (us)", times.back() * 1e6);
        CHECK (medianLoad < 10.0);
    }
}
