#include "TestFramework.h"
#include "TestSignals.h"
#include <cstring>
#include <complex>
#include <set>
#include <tuple>
#include "../plugins/WEIGHT/PluginProcessor.h"
#include "../plugins/WEIGHT/Parameters.h"
#include "../plugins/WEIGHT/dsp/WeightModel.h"
#include "../plugins/WEIGHT/dsp/DetectorFilters.h"
#include "../plugins/WEIGHT/dsp/OutputStage.h"
#include "../plugins/WEIGHT/dsp/GrTelemetry.h"

using namespace tleet;
using namespace tleet::test;
using tleet::weight::WeightProcessor;
using tleet::weight::WeightModel;
using tleet::weight::GrTelemetry;

namespace
{
    enum RatioIdx { r1_5 = 0, r2, r3, r4, r6, r10, rInf };
    enum AttackIdx { a30u = 0, a100u, a300u, a1, a3, a10, a30 };
    enum ReleaseIdx { rel50 = 0, rel100, rel200, rel500, rel1s, rel2s };
    enum ThrustIdx { tClean = 0, tNeutral, tHard };
    enum DetIdx { ff = 0, fb };
    enum ToneIdx { toneOld = 0, toneNeutral, toneNew };
    enum GrModeIdx { gmPeak = 0, gmRms, gmAverage };

    constexpr double aes = WeightModel::aesOffsetDb;

    void setParam (WeightProcessor& p, const char* id, float plain)
    {
        auto* r = p.apvts.getParameter (id);
        r->setValueNotifyingHost (r->convertTo0to1 (plain));
    }

    float plainValue (WeightProcessor& p, const char* id)
    {
        auto* r = p.apvts.getParameter (id);
        return r->convertFrom0to1 (r->getValue());
    }

    // Threshold knob value for a detector threshold in dBFS RMS (AES17).
    float knobFor (float thresholdDbfs) { return thresholdDbfs - weight::params::thresholdReferenceDbfs; }

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

    juce::String name (int ratio) { return weight::params::ratioNames[ratio].replace (juce::String::fromUTF8 ("\xe2\x88\x9e"), "inf"); }
    juce::String topoName (int t) { return t == fb ? "FB" : "FF"; }

    struct GrBlock { float l, r; };

    // Renders main (+ optional key) through the processor in blocks; returns
    // the main output. Optionally records the per-channel peak GR per block.
    juce::AudioBuffer<float> renderKeyed (WeightProcessor& p, const juce::AudioBuffer<float>& main,
                                          const juce::AudioBuffer<float>* key, int block,
                                          std::vector<GrBlock>* grPerBlock = nullptr)
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
                grPerBlock->push_back ({ p.grLeft.takePeakDb(), p.grRight.takePeakDb() });
        }
        juce::AudioBuffer<float> out (nm, n);
        for (int ch = 0; ch < nm; ++ch) out.copyFrom (ch, 0, io, ch, 0, n);
        return out;
    }

    juce::AudioBuffer<float> render (WeightProcessor& p, const juce::AudioBuffer<float>& input, int block,
                                     std::vector<GrBlock>* grPerBlock = nullptr)
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

    std::unique_ptr<WeightProcessor> makeWeight (double fs, int block, int channels = 2, int keyChannels = 0)
    {
        auto p = std::make_unique<WeightProcessor>();
        const auto set = channels == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo();
        WeightProcessor::BusesLayout layout;
        layout.inputBuses.add (set);
        layout.inputBuses.add (keyChannels == 0 ? juce::AudioChannelSet::disabled()
                                                : keyChannels == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo());
        layout.outputBuses.add (set);
        CHECK_MSG (p->setBusesLayout (layout), "layout rejected");
        p->setRateAndBufferSizeDetails (fs, block);
        p->prepareToPlay (fs, block);
        return p;
    }

    // Constant-envelope test signal (+A, -A, ...): its mean square is A^2 at
    // every sample, so the RMS detector sees a clean step (and the SC HPF and
    // a Clean THRUST pass Nyquist with unity gain).
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

    // Amplitude of an alternating signal whose AES17 detector level is `levelDbfs`.
    float ampForLevel (double levelDbfs) { return (float) std::pow (10.0, (levelDbfs - aes) / 20.0); }

    WeightModel makeModel (double fs, int ratio, int attack, int release, float thresholdDbfs = -20.0f,
                           int topology = ff, int thrust = tClean, float hpf = 20.0f, float link = 1.0f, int tone = toneNeutral)
    {
        WeightModel m;
        m.setRatio (ratio);
        m.setAttack (attack);
        m.setRelease (release);
        m.setThresholdDbfs (thresholdDbfs);
        m.setTopology (topology);
        m.setThrust (thrust);
        m.setScHpf (hpf);
        m.setLink (link);
        m.setTone (tone);
        m.setMakeupDb (0.0f);
        m.prepare (fs);
        return m;
    }

    // Runs the bare model on `l` (and `r` if given); returns channel-0 GR (dB) per sample.
    std::vector<double> grTrace (WeightModel& m, std::vector<float> l, std::vector<float> r = {}, std::vector<double>* grR = nullptr)
    {
        std::vector<double> gr;
        gr.reserve (l.size());
        const int nch = r.empty() ? 1 : 2;
        for (size_t i = 0; i < l.size(); ++i)
        {
            float* ch[] = { &l[i], r.empty() ? nullptr : &r[i] };
            m.process (ch, nch, nullptr, 0, 1);
            gr.push_back (m.getGainReductionDb (0));
            if (grR != nullptr)
                grR->push_back (m.getGainReductionDb (1));
        }
        return gr;
    }

    double steadyGrOfModel (int ratio, double levelDbfs, double thresholdDbfs, int topology,
                            int attack = a30u, int release = rel2s, double fs = 48000.0)
    {
        auto m = makeModel (fs, ratio, attack, release, (float) thresholdDbfs, topology);
        std::vector<float> sig = alternating (ampForLevel (levelDbfs), (int) (fs * 0.8));
        float* ch[] = { sig.data() };
        m.process (ch, 1, nullptr, 0, (int) sig.size());
        return m.getGainReductionDb (0);
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

    // Least-squares slope of ln(y) over [i0, i1): the decay rate of an exponential.
    double logSlope (const std::vector<double>& y, int i0, int i1, double fs)
    {
        double sx = 0, sy = 0, sxx = 0, sxy = 0;
        int n = 0;
        for (int i = i0; i < i1; ++i)
        {
            if (! (y[(size_t) i] > 0.0))
                break;
            const double x = i / fs, v = std::log (y[(size_t) i]);
            sx += x; sy += v; sxx += x * x; sxy += x * v; ++n;
        }
        return (n * sxy - sx * sy) / (n * sxx - sx * sx);
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

    // Raised-cosine fade-in (avoids ringing the SC HPF / THRUST on an abrupt start).
    std::vector<float> fadedTone (double fs, double hz, float amp, int n, double fadeS = 0.05)
    {
        auto v = sine (fs, hz, amp, n);
        const int fade = (int) (fadeS * fs);
        for (int i = 0; i < fade && i < n; ++i)
            v[(size_t) i] *= 0.5f - 0.5f * (float) std::cos (juce::MathConstants<double>::pi * i / fade);
        return v;
    }

    // Independent reference of the documented continuous-time detector
    // (MODEL_SPEC 3): RK4 on
    //   ms' = (p - ms) / tau_rms,   p = A^2 (FF) or A^2 g^2 (FB), g = 10^(-c/20)
    //   c'  = (T(L(ms)) - c) / tau,  tau = attack if T > c else release
    // for a mono constant-envelope input of level `levelDbfs` starting at
    // sample 0 from rest (ms = ms0, c = c0), many sub-steps per sample.
    std::vector<double> referenceTrace (int ratio, int topology, double levelDbfs, double thrDbfs, int attack, int release,
                                        double fs, int n, double ms0 = 0.0, double c0 = 0.0)
    {
        const double ta = WeightModel::attackSeconds (attack), tr = WeightModel::releaseSeconds (release);
        const double trms = WeightModel::rmsTimeSeconds, s = WeightModel::ratioSlope (ratio);
        const double a2 = levelDbfs < -300.0 ? 0.0 : std::pow (10.0, (levelDbfs - aes) / 10.0);
        auto deriv = [&] (double ms, double c, double& dms, double& dc)
        {
            const double g2 = topology == fb ? std::exp (-2.0 * WeightModel::ln10Over20 * c) : 1.0;
            dms = (a2 * g2 - ms) / trms;
            const double t = WeightModel::gainComputerDb (WeightModel::levelDbOfMeanSquare (std::max (0.0, ms)), thrDbfs, s);
            dc = (t - c) / (t > c ? ta : tr);
        };
        const int sub = juce::jlimit (16, 2048, (int) std::ceil (64.0 / (std::min (ta, trms) * fs)));
        const double h = 1.0 / (fs * sub);
        std::vector<double> out ((size_t) n);
        double ms = ms0, c = c0;
        for (int i = 0; i < n; ++i)
        {
            for (int k = 0; k < sub; ++k)
            {
                double k1m, k1c, k2m, k2c, k3m, k3c, k4m, k4c;
                deriv (ms, c, k1m, k1c);
                deriv (ms + 0.5 * h * k1m, c + 0.5 * h * k1c, k2m, k2c);
                deriv (ms + 0.5 * h * k2m, c + 0.5 * h * k2c, k3m, k3c);
                deriv (ms + h * k3m, c + h * k3c, k4m, k4c);
                ms += h / 6.0 * (k1m + 2 * k2m + 2 * k3m + k4m);
                c += h / 6.0 * (k1c + 2 * k2c + 2 * k3c + k4c);
            }
            out[(size_t) i] = c;
        }
        return out;
    }

    // Processor render of a 1 kHz sine: THD of the last second, final telemetry.
    struct ToneResult { double thd, h2, h3; float grDb, outDb; };
    ToneResult renderTone (int toneIdx, float peakDbfs, float thresholdDbfs, int ratio = r4, float makeup = 0.0f, double hz = 1000.0)
    {
        const double fs = 48000.0;
        auto p = makeWeight (fs, 480);
        setParam (*p, "tone", (float) toneIdx);
        setParam (*p, "threshold", knobFor (thresholdDbfs));
        setParam (*p, "ratio", (float) ratio);
        setParam (*p, "attack", (float) a30);
        setParam (*p, "release", (float) rel2s);
        setParam (*p, "makeup", makeup);
        p->prepareToPlay (fs, 480);
        auto in = toBuffer (sine (fs, hz, juce::Decibels::decibelsToGain (peakDbfs), (int) (fs * 3.0)), 2);
        std::vector<GrBlock> gr;
        auto out = render (*p, in, 480, &gr);
        const int n = 48000, start = out.getNumSamples() - n;
        const float* d = out.getReadPointer (0) + start;
        const double f = toneAmplitude (d, n, fs, hz);
        return { thd (d, n, fs, hz), toneAmplitude (d, n, fs, hz * 2) / f, toneAmplitude (d, n, fs, hz * 3) / f,
                 gr.back().l, db (rms (d, n)) };
    }
}

// =====================================================================================
TEST ("WEIGHT: parameter IDs, ranges, steps and defaults match the registry")
{
    WeightProcessor p;
    const char* ids[] = { "threshold", "attack", "release", "ratio", "makeup", "thrust", "detector", "tone",
                          "scHpf", "scExternal", "mix", "output", "link", "grMode", "bypass" };
    for (auto* id : ids)
        CHECK_MSG (p.apvts.getParameter (id) != nullptr, id);
    CHECK (p.getParameters().size() == 15);
    for (auto* param : p.getParameters())
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (param))
            CHECK_MSG (r->getVersionHint() == 1, r->paramID);

    auto def = [&p] (const char* id) { auto* r = p.apvts.getParameter (id); return r->convertFrom0to1 (r->getDefaultValue()); };
    CHECK_NEAR (def ("threshold"), 0.0f, 1e-4);
    CHECK_NEAR (def ("attack"), (float) a10, 1e-4);
    CHECK_NEAR (def ("release"), (float) rel200, 1e-4);
    CHECK_NEAR (def ("ratio"), (float) r2, 1e-4);
    CHECK_NEAR (def ("makeup"), 0.0f, 1e-4);
    CHECK_NEAR (def ("thrust"), (float) tClean, 1e-4);
    CHECK_NEAR (def ("detector"), (float) ff, 1e-4);
    CHECK_NEAR (def ("tone"), (float) toneNeutral, 1e-4);
    CHECK_NEAR (def ("scHpf"), 20.0f, 1e-3);
    CHECK_NEAR (def ("scExternal"), 0.0f, 1e-4);
    CHECK_NEAR (def ("mix"), 100.0f, 1e-4);
    CHECK_NEAR (def ("output"), 0.0f, 1e-4);
    CHECK_NEAR (def ("link"), 100.0f, 1e-4);
    CHECK_NEAR (def ("grMode"), (float) gmRms, 1e-4);
    CHECK_NEAR (def ("bypass"), 0.0f, 1e-4);

    auto range = [&p] (const char* id, float lo, float hi)
    {
        auto* r = p.apvts.getParameter (id);
        CHECK_NEAR (r->convertFrom0to1 (0.0f), lo, 1e-3);
        CHECK_NEAR (r->convertFrom0to1 (1.0f), hi, 1e-3);
    };
    range ("threshold", -20.0f, 10.0f);
    range ("makeup", -12.0f, 24.0f);
    range ("scHpf", 20.0f, 240.0f);
    range ("mix", 0.0f, 100.0f);
    range ("output", -12.0f, 12.0f);
    range ("link", 0.0f, 100.0f);

    // THRESHOLD and MAKEUP: piecewise linear, 0 dB at the knob centre, 0.1 dB
    // legal values, exact round trip (as GLUE).
    for (auto [id, lo, hi] : { std::tuple { "threshold", -20.0f, 10.0f }, std::tuple { "makeup", -12.0f, 24.0f } })
    {
        auto* f = dynamic_cast<juce::AudioParameterFloat*> (p.apvts.getParameter (id));
        juce::AudioProcessorParameter* r = f;
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
            r->setValueNotifyingHost (n);
            bad += f->get() != v ? 1 : 0;
            badText += nr.snapToLegalValue (nr.convertFrom0to1 (r->getValueForText (r->getText (n, 64)))) != v ? 1 : 0;
        }
        CHECK_MSG (bad == 0 && badSnap == 0 && badMono == 0 && badText == 0, id);
        CHECK_NEAR (nr.snapToLegalValue (-3.04f), -3.0f, 1e-6);
        CHECK (nr.snapToLegalValue (hi + 5.0f) == hi && nr.snapToLegalValue (lo - 5.0f) == lo);
        r->setValueNotifyingHost (r->getDefaultValue());
    }

    // SC HPF is logarithmic; the skin's 20 60 120 240 marks are where the log law puts them.
    auto* hpf = p.apvts.getParameter ("scHpf");
    CHECK_NEAR (hpf->convertFrom0to1 (0.5f), std::sqrt (20.0f * 240.0f), 0.05);
    for (float mark : { 20.0f, 60.0f, 120.0f, 240.0f })
        MEASURE ("SC HPF skin mark " + juce::String (mark) + " Hz at knob position", hpf->convertTo0to1 (mark));
    // Threshold calibration: knob 0 dB = -18 dBFS RMS (EBU R68: 0 dBu = -18 dBFS).
    CHECK_NEAR (weight::params::thresholdToDbfs (0.0f), -18.0f, 1e-6);
    CHECK_NEAR (weight::params::thresholdToDbfs (10.0f), -8.0f, 1e-6);
    CHECK_NEAR (weight::params::thresholdToDbfs (-20.0f), -38.0f, 1e-6);

    auto choices = [&p] (const char* id) { return dynamic_cast<juce::AudioParameterChoice*> (p.apvts.getParameter (id))->choices; };
    CHECK (choices ("attack") == juce::StringArray ({ "0.03 ms", "0.1 ms", "0.3 ms", "1 ms", "3 ms", "10 ms", "30 ms" }));
    CHECK (choices ("release") == juce::StringArray ({ "0.05 s", "0.1 s", "0.2 s", "0.5 s", "1 s", "2 s" }));
    juce::StringArray ratioLabels { "1.5", "2", "3", "4", "6", "10" };
    ratioLabels.add (juce::String::fromUTF8 ("\xe2\x88\x9e"));
    CHECK (choices ("ratio") == ratioLabels);
    CHECK (choices ("thrust") == juce::StringArray ({ "Clean", "Neutral", "Hard" }));
    CHECK (choices ("detector") == juce::StringArray ({ "Feed Forward", "Feed Back" }));
    CHECK (choices ("tone") == juce::StringArray ({ "Old", "Neutral", "New" }));
    CHECK (choices ("grMode") == juce::StringArray ({ "Peak", "RMS", "Average" }));

    // The model's tables agree with the labels.
    const double atk[] = { 30e-6, 100e-6, 300e-6, 1e-3, 3e-3, 10e-3, 30e-3 };
    for (int i = 0; i < WeightModel::numAttacks; ++i)
        CHECK_NEAR (WeightModel::attackSeconds (i), atk[i], 1e-12);
    const double rel[] = { 0.05, 0.1, 0.2, 0.5, 1.0, 2.0 };
    for (int i = 0; i < WeightModel::numReleases; ++i)
        CHECK_NEAR (WeightModel::releaseSeconds (i), rel[i], 1e-12);
    const double ratios[] = { 1.5, 2, 3, 4, 6, 10 };
    for (int i = 0; i < 6; ++i)
        CHECK_NEAR (WeightModel::ratioValue (i), ratios[i], 1e-12);
    CHECK (std::isinf (WeightModel::ratioValue (rInf)) && WeightModel::ratioSlope (rInf) == 1.0);

    // Buses: main stereo/mono in == out, plus an optional Sidechain input (disabled by default).
    CHECK (p.getBusCount (true) == 2 && p.getBusCount (false) == 1);
    CHECK (p.getBus (true, 1)->getName() == "Sidechain");
    CHECK (! p.getBus (true, 1)->isEnabledByDefault());
    WeightProcessor::BusesLayout l;
    l.inputBuses.add (juce::AudioChannelSet::stereo());
    l.inputBuses.add (juce::AudioChannelSet::mono());
    l.outputBuses.add (juce::AudioChannelSet::stereo());
    CHECK (p.checkBusesLayoutSupported (l));
    l.inputBuses.getReference (1) = juce::AudioChannelSet::create5point1();
    CHECK (! p.checkBusesLayoutSupported (l));
    l.inputBuses.getReference (1) = juce::AudioChannelSet::disabled();
    l.outputBuses.getReference (0) = juce::AudioChannelSet::mono();
    CHECK (! p.checkBusesLayoutSupported (l)); // stereo in, mono out
    l.inputBuses.getReference (0) = juce::AudioChannelSet::mono();
    CHECK (p.checkBusesLayoutSupported (l));   // mono == mono
}

TEST ("WEIGHT: all sample rates and block sizes, mono and stereo, with and without a key: finite output, no steady-state allocation")
{
    for (double fs : { 44100.0, 48000.0, 88200.0, 96000.0, 192000.0 })
        for (int block : { 1, 16, 32, 64, 128, 256, 512, 1024, 2048 })
            for (int channels : { 1, 2 })
                for (int keyChannels : { 0, 2 })
                {
                    auto p = makeWeight (fs, block, channels, keyChannels);
                    setParam (*p, "threshold", -20.0f);
                    setParam (*p, "ratio", (float) (keyChannels > 0 ? rInf : r10));
                    setParam (*p, "attack", (float) a30u);
                    setParam (*p, "release", (float) rel50);
                    setParam (*p, "detector", (float) (block % 3 == 0 ? fb : ff));
                    setParam (*p, "thrust", (float) (block % 2 == 0 ? tHard : tNeutral));
                    setParam (*p, "tone", (float) (block % 4 == 0 ? toneOld : toneNew));
                    setParam (*p, "scHpf", 150.0f);
                    setParam (*p, "makeup", 24.0f);
                    setParam (*p, "mix", 70.0f);
                    setParam (*p, "link", 60.0f);
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
                            p->grLeft.take();          // a UI reading the telemetry meanwhile
                            p->takeGrDb (1);
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
                    CHECK_MSG (p->getLatencySamples() == 0, tag);
                }
}

TEST ("WEIGHT: NaN/Inf in the input or the key never reaches the output or poisons the state")
{
    for (int keyChannels : { 0, 2 })
        for (int topo : { ff, fb })
        {
            auto p = makeWeight (48000.0, 256, 2, keyChannels);
            setParam (*p, "threshold", -15.0f);
            setParam (*p, "detector", (float) topo);
            setParam (*p, "thrust", (float) tHard);
            setParam (*p, "tone", (float) toneOld);
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
            const auto r = p->grLeft.take();
            MEASURE ("GR after the bad samples (dB), key " + juce::String (keyChannels) + ", " + topoName (topo), r.peakDb);
            CHECK (std::isfinite (r.peakDb) && std::isfinite (r.rmsDb) && std::isfinite (r.averageDb));
            CHECK (r.peakDb > 1.0f && r.peakDb < 30.0f);
        }
}

TEST ("WEIGHT: silence gives silence; DC never triggers GR and passes the VCA untouched; denormal input stays clean")
{
    auto p = makeWeight (48000.0, 512);
    setParam (*p, "makeup", 24.0f);
    setParam (*p, "threshold", -20.0f);
    setParam (*p, "tone", (float) toneOld);
    juce::AudioBuffer<float> silence (2, 48000);
    silence.clear();
    auto out = render (*p, silence, 512);
    CHECK (out.getMagnitude (0, out.getNumSamples()) < 1.0e-6f);
    CHECK (p->grLeft.takePeakDb() == 0.0f && p->grRight.takePeakDb() == 0.0f);

    // DC: the SC HPF (always at least 20 Hz, 12 dB/oct) keeps it out of the
    // detector, in both topologies; the VCA passes it at unity gain (as a
    // DC-coupled VCA would; the hardware's transformers are not modelled as
    // a high-pass, MODEL_SPEC 6).
    for (int topo : { ff, fb })
    {
        auto q = makeWeight (48000.0, 512);
        setParam (*q, "threshold", -20.0f);
        setParam (*q, "detector", (float) topo);
        setParam (*q, "thrust", (float) tHard);
        q->prepareToPlay (48000.0, 512);
        juce::AudioBuffer<float> dc (2, 48000 * 3);
        for (int ch = 0; ch < 2; ++ch)
            juce::FloatVectorOperations::fill (dc.getWritePointer (ch), 0.25f, dc.getNumSamples());
        std::vector<GrBlock> gr;
        out = render (*q, dc, 512, &gr);
        MEASURE ("GR from a 0.25 DC input after 3 s (dB), " + topoName (topo), gr.back().l);
        MEASURE ("output DC after 3 s, " + topoName (topo), out.getSample (0, out.getNumSamples() - 1));
        CHECK (gr.back().l < 0.01f);
        CHECK_NEAR (out.getSample (0, out.getNumSamples() - 1), 0.25f, 1.0e-3);
    }

    // Denormal-level input into a fresh instance with the most gain.
    auto fresh = makeWeight (48000.0, 512);
    setParam (*fresh, "makeup", 24.0f);
    setParam (*fresh, "output", 12.0f);
    setParam (*fresh, "tone", (float) toneNew);
    juce::AudioBuffer<float> tiny (2, 48000);
    for (int i = 0; i < tiny.getNumSamples(); ++i)
        for (int ch = 0; ch < 2; ++ch)
            tiny.setSample (ch, i, (i & 1) ? 1.0e-38f : -1.0e-38f);
    out = render (*fresh, tiny, 512);
    CHECK (allFinite (out));
    CHECK (out.getMagnitude (0, out.getNumSamples()) < 1.0e-6f);
}

TEST ("WEIGHT: reported latency equals measured impulse delay (0); Mix 0 and Bypass are bit-exact dry (also with a key)")
{
    for (double fs : { 44100.0, 48000.0, 96000.0 })
    {
        auto p = makeWeight (fs, 128, 2, 2);
        const int latency = p->getLatencySamples();
        MEASURE ("reported latency @" + juce::String (fs), latency);
        CHECK (latency == 0);

        setParam (*p, "threshold", 10.0f); // a linear path for a small impulse
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
        CHECK (out.getSample (0, 100) == 0.25f); // Neutral tone, no GR: the impulse is untouched

        // Mix = 0: dry, bit-exact, while the wet path compresses hard on an external key.
        auto sig = toBuffer (sine (fs, 997.0, 0.5f, 8192), 2);
        auto key = toBuffer (sine (fs, 300.0, 0.9f, 8192), 2);
        setParam (*p, "threshold", -20.0f);
        setParam (*p, "ratio", (float) rInf);
        setParam (*p, "tone", (float) toneOld);
        setParam (*p, "thrust", (float) tHard);
        setParam (*p, "makeup", 12.0f);
        setParam (*p, "scExternal", 1.0f);
        setParam (*p, "mix", 0.0f);
        p->prepareToPlay (fs, 128);
        out = renderKeyed (*p, sig, &key, 128);
        CHECK_MSG (identical (out, sig), "Mix=0 is not bit-exact dry");

        // Bypass: dry, bit-exact, whatever the other settings (Output trim included).
        setParam (*p, "mix", 100.0f);
        setParam (*p, "output", 7.0f);
        setParam (*p, "bypass", 1.0f);
        p->prepareToPlay (fs, 128);
        out = renderKeyed (*p, sig, &key, 128);
        CHECK_MSG (identical (out, sig), "Bypass is not bit-exact dry");
    }
}

TEST ("WEIGHT: Mix is a linear blend: out(m) = dry + m (wet - dry) (null test); Output trims after Mix")
{
    const double fs = 48000.0;
    auto renderAt = [&] (float mix)
    {
        auto p = makeWeight (fs, 128);
        setParam (*p, "threshold", -15.0f);
        setParam (*p, "ratio", (float) r10);
        setParam (*p, "attack", (float) a1);
        setParam (*p, "release", (float) rel100);
        setParam (*p, "tone", (float) toneOld);
        setParam (*p, "makeup", 6.0f);
        setParam (*p, "mix", mix);
        p->prepareToPlay (fs, 128);
        std::vector<float> sig = sine (fs, 220.0, 0.6f, 24000);
        for (size_t i = 0; i < sig.size(); ++i)
            sig[i] *= (i / 4000) % 2 == 0 ? 1.0f : 0.2f;
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

    // Output acts on the blend (dry included): Mix 0 + Output -6 dB = dry - 6 dB.
    auto q = makeWeight (fs, 256);
    setParam (*q, "mix", 0.0f);
    setParam (*q, "output", -6.0f);
    q->prepareToPlay (fs, 256);
    auto sig = toBuffer (sine (fs, 500.0, 0.3f, 24000), 2);
    auto out = render (*q, sig, 256);
    const float change = db (rms (out.getReadPointer (0) + 12000, 12000) / rms (sig.getReadPointer (0) + 12000, 12000));
    MEASURE ("Mix 0 + Output -6 dB: level change (dB)", change);
    CHECK_NEAR (change, -6.0f, 0.01);
}

TEST ("WEIGHT: static curves: GR vs level for every ratio, threshold and topology, knee included, match MODEL_SPEC (CSV)")
{
    juce::String csv ("topology,ratio,threshold_dbfs,level_dbfs_rms,gr_model_db,gr_law_db,error_db\n");
    double maxErr = 0;
    for (int topo : { ff, fb })
        for (int r = 0; r < WeightModel::numRatios; ++r)
            for (double thr : { -38.0, -28.0, -18.0, -8.0 })
                for (double level = thr - 10.0; level <= std::min (thr + 30.0, 3.0); level += 1.0)
                {
                    const double model = steadyGrOfModel (r, level, thr, topo);
                    const double law = WeightModel::staticGainReductionDb (level, thr, r, topo);
                    maxErr = std::max (maxErr, std::abs (model - law));
                    csv << topoName (topo) << "," << name (r) << "," << thr << "," << level << "," << model << "," << law << "," << model - law << "\n";
                }
    outputDirectory().getChildFile ("WEIGHT_static_curves.csv").replaceWithText (csv);
    MEASURE ("max |model - static law| over both topologies, all ratios, thresholds and levels (dB)", maxErr);
    CHECK (maxErr < 1.0e-3);

    // Ratio labels, independent of the law code: output rise from 10 to 20 dB
    // above threshold (above the knee). FF: the label; FB: 2 - 1/label.
    const double thr = -20.0;
    for (int r = 0; r < WeightModel::numRatios; ++r)
        for (int topo : { ff, fb })
        {
            const double y10 = (thr + 10.0) - steadyGrOfModel (r, thr + 10.0, thr, topo);
            const double y20 = (thr + 20.0) - steadyGrOfModel (r, thr + 20.0, thr, topo);
            const double measured = 10.0 / (y20 - y10);
            const double label = WeightModel::ratioValue (r);
            const double expected = topo == ff ? label : 2.0 - 1.0 / label;
            MEASURE (topoName (topo) + " ratio " + name (r) + ": measured ratio 10..20 dB over threshold",
                     std::isfinite (measured) ? measured : 1.0e9);
            if (std::isinf (expected))
                CHECK (std::abs (y20 - y10) < 1.0e-4); // a true limiter above the knee
            else
                CHECK_NEAR (measured, expected, 1.0e-3 * expected);
        }

    // Knee (fixed MED, 6 dB wide, quadratic): GR = 0 at threshold - 3 dB,
    // S x 0.75 dB at threshold, S x 3 dB at threshold + 3 dB (FF).
    for (int r : { r2, r4, rInf })
    {
        const double s = WeightModel::ratioSlope (r);
        const double below = steadyGrOfModel (r, thr - 3.0, thr, ff), at = steadyGrOfModel (r, thr, thr, ff),
                     top = steadyGrOfModel (r, thr + 3.0, thr, ff), mid = steadyGrOfModel (r, thr + 1.5, thr, ff);
        MEASURE ("FF " + name (r) + ": GR at threshold -3 / 0 / +1.5 / +3 dB", below);
        MEASURE ("   at threshold", at);
        MEASURE ("   at +1.5 dB", mid);
        MEASURE ("   at +3 dB", top);
        CHECK (below < 1.0e-6);
        CHECK_NEAR (at, s * 0.75, 1.0e-4);
        CHECK_NEAR (mid, s * 4.5 * 4.5 / 12.0, 1.0e-4);
        CHECK_NEAR (top, s * 3.0, 1.0e-4);
        // the local slope is continuous through the knee: no corner
        const double d1 = steadyGrOfModel (r, thr + 2.9, thr, ff) - steadyGrOfModel (r, thr + 2.8, thr, ff);
        const double d2 = steadyGrOfModel (r, thr + 3.1, thr, ff) - steadyGrOfModel (r, thr + 3.0, thr, ff);
        CHECK_NEAR (d1, d2, 0.02 * s);
    }

    // Through the processor (1 kHz sine, AES17 level = peak dBFS, both topologies).
    double maxProcErr = 0;
    juce::String pcsv ("topology,ratio,level_dbfs,telemetry_db,law_db\n");
    for (int topo : { ff, fb })
        for (int r : { r1_5, r4, r10, rInf })
            for (float level : { -26.0f, -20.0f, -14.0f, -6.0f })
            {
                auto p = makeWeight (48000.0, 480);
                setParam (*p, "threshold", knobFor (-20.0f));
                setParam (*p, "ratio", (float) r);
                setParam (*p, "attack", (float) a30);
                setParam (*p, "release", (float) rel50);
                setParam (*p, "detector", (float) topo);
                p->prepareToPlay (48000.0, 480);
                std::vector<GrBlock> gr;
                render (*p, toBuffer (sine (48000.0, 1000.0, juce::Decibels::decibelsToGain (level), 96000), 2), 480, &gr);
                const double law = WeightModel::staticGainReductionDb (level, -20.0, r, topo);
                const double tel = p->grLeft.getAverageDb();
                maxProcErr = std::max (maxProcErr, std::abs (tel - law));
                pcsv << topoName (topo) << "," << name (r) << "," << level << "," << tel << "," << law << "\n";
            }
    outputDirectory().getChildFile ("WEIGHT_static_curves_processor.csv").replaceWithText (pcsv);
    MEASURE ("processor: max |300 ms average GR telemetry - static law| (1 kHz sine) (dB)", maxProcErr);
    CHECK (maxProcErr < 0.05);
}

TEST ("WEIGHT: attack per step follows the documented RMS + ballistics law in both topologies; FF tail = the label (CSV)")
{
    // Bare model at 384 kHz (the reference ODE is continuous time), step from
    // silence to 15 dB above threshold (constant envelope), release 2 s.
    const double fs = 384000.0;
    const double thr = -20.0, over = 15.0;
    juce::String csv ("topology,ratio,attack_label_ms,t63_model_ms,t63_reference_ms,error_pct,final_gr_db\n");
    for (int topo : { ff, fb })
        for (int r : { r2, r4, rInf })
            for (int atk = 0; atk < WeightModel::numAttacks; ++atk)
            {
                const double ta = WeightModel::attackSeconds (atk);
                const int pre = 64, n = (int) (fs * std::max (0.06, ta * 10.0));
                auto m = makeModel (fs, r, atk, rel2s, (float) thr, topo);
                const auto sig = concat (alternating (0.0f, pre), alternating (ampForLevel (thr + over), n));
                const auto gr = grTrace (m, sig);
                const std::vector<double> modelTrace (gr.begin() + pre, gr.end());
                const auto ref = referenceTrace (r, topo, thr + over, thr, atk, rel2s, fs, n);
                const double finalModel = modelTrace.back(), finalRef = ref.back();
                const double tModel = crossing (modelTrace, 0, 0.632 * finalModel, true, fs);
                const double tRef = crossing (ref, 0, 0.632 * finalRef, true, fs);
                const double errPct = 100.0 * (tModel - tRef) / tRef;
                csv << topoName (topo) << "," << name (r) << "," << ta * 1000.0 << "," << tModel * 1000.0 << "," << tRef * 1000.0
                    << "," << errPct << "," << finalModel << "\n";
                const auto tag = topoName (topo) + " " + name (r) + ":1, attack " + weight::params::attackNames[atk];
                MEASURE (tag + ": GR 63 % rise (ms), model", tModel * 1000.0);
                // FF settles on the static law; FB may overshoot (the RMS lag sits
                // inside the loop) and then creeps back at the release speed,
                // exactly as the reference does.
                if (topo == ff)
                    CHECK_NEAR (finalModel, WeightModel::staticGainReductionDb (thr + over, thr, r, topo), 0.01);
                CHECK_NEAR (finalModel, finalRef, 0.01);
                CHECK_MSG (std::abs (tModel - tRef) <= 0.03 * tRef + 1.0 / fs, tag + ": " + juce::String (errPct, 2) + " %");
            }
    outputDirectory().getChildFile ("WEIGHT_attack_steps.csv").replaceWithText (csv);

    // FF: once the RMS detector has settled (> 10 tau_rms), the remaining
    // approach is the ballistic capacitor alone: an exponential with exactly
    // the label's time constant (labels slower than the RMS detector).
    for (int atk : { a10, a30 })
    {
        const double hostFs = 48000.0;
        auto m = makeModel (hostFs, r4, atk, rel2s, (float) thr, ff);
        const int n = (int) (hostFs * 0.3);
        const auto gr = grTrace (m, alternating (ampForLevel (thr + over), n));
        const double target = WeightModel::staticGainReductionDb (thr + over, thr, r4, ff);
        std::vector<double> gap;
        for (double v : gr) gap.push_back (target - v);
        const int i0 = (int) (12.0 * WeightModel::rmsTimeSeconds * hostFs);
        int i1 = i0;
        while (i1 < n && gap[(size_t) i1] > 1.0e-4 * target) ++i1;
        const double tau = -1.0 / logSlope (gap, i0, i1, hostFs);
        MEASURE ("FF attack " + weight::params::attackNames[atk] + ": fitted tail time constant (ms)", tau * 1000.0);
        CHECK_NEAR (tau, WeightModel::attackSeconds (atk), 0.005 * WeightModel::attackSeconds (atk));
    }

    // Through the processor and the telemetry: Attack reaches the DSP.
    auto rise = [] (int atk)
    {
        const double hostFs = 48000.0;
        auto p = makeWeight (hostFs, 16);
        setParam (*p, "attack", (float) atk);
        setParam (*p, "release", (float) rel2s);
        setParam (*p, "threshold", knobFor (-30.0f));
        p->prepareToPlay (hostFs, 16);
        auto sig = concat (std::vector<float> (4800, 0.0f), sine (hostFs, 2000.0, 0.5f, 19200));
        std::vector<GrBlock> gr;
        render (*p, toBuffer (sig, 2), 16, &gr);
        std::vector<double> g;
        for (auto& b : gr) g.push_back (b.l);
        return crossing (g, 4800 / 16, 0.632 * g.back(), true, hostFs / 16.0) * 1000.0;
    };
    const double fast = rise (a30u), slow = rise (a30);
    MEASURE ("processor: GR 63 % rise, Attack 0.03 ms (ms, 16-sample resolution)", fast);
    MEASURE ("processor: GR 63 % rise, Attack 30 ms (ms)", slow);
    CHECK (slow > 10.0 * std::max (fast, 0.34));
}

TEST ("WEIGHT: release per step: the GR decays with exactly the label's time constant in both topologies (CSV)")
{
    const double fs = 48000.0, thr = -20.0;
    juce::String csv ("topology,ratio,release,label_ms,fitted_tau_ms,t37_model_ms,t37_reference_ms\n");
    for (int topo : { ff, fb })
        for (int r : { r1_5, r4, rInf })
            for (int rel = 0; rel < WeightModel::numReleases; ++rel)
            {
                auto m = makeModel (fs, r, a1, rel, (float) thr, topo);
                const int burst = (int) (0.3 * fs);
                const double label = WeightModel::releaseSeconds (rel);
                const int after = (int) (label * 4.0 * fs);
                const auto gr = grTrace (m, concat (alternating (ampForLevel (thr + 15.0), burst), alternating (0.0f, after)));
                const double start = gr[(size_t) burst - 1];
                // once the detector level is below the knee the target is 0: pure capacitor discharge
                const int i0 = burst + (int) (0.05 * fs), i1 = burst + (int) (label * 3.0 * fs);
                std::vector<double> tail (gr.begin(), gr.end());
                const double tau = -1.0 / logSlope (tail, i0, i1, fs);
                const double t37 = crossing (gr, burst, 0.36788 * start, false, fs);
                // reference: the ODE started from the model's state at the end of the burst
                const auto ref = referenceTrace (r, topo, -1000.0, thr, a1, rel, fs, after, std::pow (10.0, (thr + 15.0 - aes) / 10.0)
                                                     * (topo == fb ? std::exp (-2.0 * WeightModel::ln10Over20 * start) : 1.0), start);
                const double t37ref = crossing (ref, 0, 0.36788 * start, false, fs);
                csv << topoName (topo) << "," << name (r) << "," << weight::params::releaseNames[rel] << "," << label * 1000 << ","
                    << tau * 1000 << "," << t37 * 1000 << "," << t37ref * 1000 << "\n";
                const auto tag = topoName (topo) + " " + name (r) + ":1 release " + weight::params::releaseNames[rel];
                MEASURE (tag + ": fitted decay time constant (ms)", tau * 1000.0);
                MEASURE (tag + ": recovery to 37 % (ms)", t37 * 1000.0);
                CHECK (start > 3.0);
                CHECK_MSG (std::abs (tau - label) <= 1.0e-3 * label, tag);
                CHECK_MSG (std::abs (t37 - t37ref) <= 0.01 * t37ref + 2.0 / fs, tag);
                // the RMS detector's own decay (5 ms) adds a short hold before the release proper
                CHECK (t37 > label && t37 < label + 0.03);
            }
    outputDirectory().getChildFile ("WEIGHT_release_steps.csv").replaceWithText (csv);

    // Through the processor: telemetry recovery per step.
    auto recovery = [] (int rel)
    {
        const double hostFs = 48000.0;
        auto p = makeWeight (hostFs, 64);
        setParam (*p, "release", (float) rel);
        setParam (*p, "attack", (float) a30u);
        setParam (*p, "threshold", knobFor (-30.0f));
        p->prepareToPlay (hostFs, 64);
        const int burst = (int) (0.5 * hostFs) / 64 * 64;
        auto sig = sine (hostFs, 1000.0, 0.5f, burst);
        sig.resize ((size_t) (burst + 10.0 * hostFs), 0.0f);
        std::vector<GrBlock> gr;
        render (*p, toBuffer (sig, 2), 64, &gr);
        const int endBlock = burst / 64;
        const float start = gr[(size_t) endBlock - 1].l;
        for (int b = endBlock; b < (int) gr.size(); ++b)
            if (gr[(size_t) b].l <= 0.36788f * start)
                return (b - endBlock + 1) * 64.0 / hostFs;
        return -1.0;
    };
    for (int rel = 0; rel < WeightModel::numReleases; ++rel)
    {
        const double t = recovery (rel), label = WeightModel::releaseSeconds (rel);
        MEASURE ("processor: telemetry recovery to 37 %, release " + weight::params::releaseNames[rel] + " (ms)", t * 1000.0);
        CHECK (t > label && t < label + 0.03);
    }
}

TEST ("WEIGHT: OLD (feedback) vs NEW (feed-forward): softer effective ratio, softer knee, faster loop timing, as documented (CSV)")
{
    const double thr = -20.0;
    juce::String csv ("ratio,label,ff_measured,fb_measured,fb_documented\n");
    for (int r = 0; r < WeightModel::numRatios; ++r)
    {
        auto meas = [&] (int topo)
        {
            const double y10 = (thr + 10.0) - steadyGrOfModel (r, thr + 10.0, thr, topo);
            const double y30 = (thr + 30.0) - steadyGrOfModel (r, thr + 30.0, thr, topo);
            return 20.0 / std::max (1.0e-9, y30 - y10);
        };
        const double rf = meas (ff), rb = meas (fb);
        csv << name (r) << "," << WeightModel::ratioValue (r) << "," << rf << "," << rb << "," << WeightModel::effectiveRatio (r, fb) << "\n";
        MEASURE ("ratio " + name (r) + ": effective ratio FF / FB", rf > 1e6 ? 1e6 : rf);
        MEASURE ("   FB", rb);
        CHECK_NEAR (rb, WeightModel::effectiveRatio (r, fb), 1.0e-3 * rb);
        CHECK (rb < rf);
    }
    outputDirectory().getChildFile ("WEIGHT_old_vs_new_ratio.csv").replaceWithText (csv);

    // GR for the same input: OLD compresses less at every overshoot.
    for (double over : { 0.0, 6.0, 12.0, 24.0 })
    {
        const double gf = steadyGrOfModel (r4, thr + over, thr, ff), gb = steadyGrOfModel (r4, thr + over, thr, fb);
        MEASURE ("4:1 at threshold +" + juce::String (over) + " dB: GR FF / FB (dB)", gf);
        MEASURE ("   FB", gb);
        CHECK (gb < gf || (over == 0.0 && gb <= gf));
    }

    // Timing: near equilibrium the feedback loop shortens the attack. Linear
    // analysis (MODEL_SPEC 2.4): with u the RMS level and c the GR,
    //   u' = (L - c - u) / tau_rms,  c' = (S (u - thr) - c) / tau_a
    // whose slow eigenvalue is the tail of a small upward step. FF: 1/tau_a.
    const double hostFs = 96000.0;
    for (int r : { r2, rInf })
    {
        const double s = WeightModel::ratioSlope (r), ta = WeightModel::attackSeconds (a30), tr = WeightModel::rmsTimeSeconds;
        const double trace = -(1.0 / tr + 1.0 / ta), det = (1.0 + s) / (tr * ta);
        const double disc = trace * trace - 4.0 * det;
        CHECK (disc > 0.0);
        const double slow = 0.5 * (trace + std::sqrt (disc));
        auto tail = [&] (int topo)
        {
            auto m = makeModel (hostFs, r, a30, rel2s, (float) thr, topo);
            const int n1 = (int) (1.0 * hostFs), n2 = (int) (0.6 * hostFs);
            const auto gr = grTrace (m, concat (alternating (ampForLevel (thr + 10.0), n1), alternating (ampForLevel (thr + 11.0), n2)));
            // the settled value (20+ time constants later) is the target
            const double target = gr.back();
            CHECK_NEAR (target, WeightModel::staticGainReductionDb (thr + 11.0, thr, r, topo), 1.0e-4);
            std::vector<double> gap;
            for (double v : gr) gap.push_back (target - v);
            const int i0 = n1 + (int) (0.06 * hostFs);
            int i1 = i0;
            while (i1 < (int) gap.size() && gap[(size_t) i1] > 1.0e-6) ++i1;
            return -1.0 / logSlope (gap, i0, i1, hostFs);
        };
        const double tff = tail (ff), tfb = tail (fb);
        MEASURE (name (r) + ":1 attack 30 ms, +1 dB step: tail time constant FF (ms)", tff * 1000.0);
        MEASURE (name (r) + ":1 attack 30 ms, +1 dB step: tail time constant FB (ms)", tfb * 1000.0);
        MEASURE (name (r) + ":1 documented FB tail (ms)", -1000.0 / slow);
        CHECK_NEAR (tff, ta, 0.01 * ta);
        CHECK_NEAR (tfb, -1.0 / slow, 0.02 * (-1.0 / slow));
        CHECK (tfb < 0.8 * tff);
    }

    // Audible: on the same programme, NEW reduces more and the outputs differ clearly.
    auto renderTopo = [] (int topo)
    {
        auto p = makeWeight (48000.0, 256);
        setParam (*p, "threshold", knobFor (-26.0f));
        setParam (*p, "ratio", (float) r6);
        setParam (*p, "attack", (float) a1);
        setParam (*p, "release", (float) rel100);
        setParam (*p, "detector", (float) topo);
        p->prepareToPlay (48000.0, 256);
        std::vector<float> sig = sine (48000.0, 330.0, 0.5f, 48000);
        for (size_t i = 0; i < sig.size(); ++i)
            sig[i] *= (i / 6000) % 2 == 0 ? 1.0f : 0.25f;
        std::vector<GrBlock> gr;
        auto out = render (*p, toBuffer (sig, 2), 256, &gr);
        double mean = 0;
        for (auto& g : gr) mean += g.l;
        return std::make_pair (out, mean / (double) gr.size());
    };
    const auto a = renderTopo (ff), b = renderTopo (fb);
    double diff = 0;
    for (int i = 0; i < a.first.getNumSamples(); ++i)
        diff = std::max (diff, (double) std::abs (a.first.getSample (0, i) - b.first.getSample (0, i)));
    MEASURE ("programme: mean GR FF / FB (dB)", a.second);
    MEASURE ("   FB", b.second);
    MEASURE ("programme: max |out FF - out FB|", diff);
    CHECK (a.second > b.second + 2.0);
    CHECK (diff > 0.05);
}

TEST ("WEIGHT: THRUST weighting: filter sweep matches thrustResponseDb and the documented curves (CSV)")
{
    using namespace tleet::weight;
    juce::String summary ("case,points,max_abs_error_db,max_dev_from_ideal_db\n");
    for (double fs : { 44100.0, 48000.0, 96000.0, 192000.0 })
        for (int mode : { tClean, tNeutral, tHard })
            for (float hpf : { 20.0f, 120.0f })
            {
                DetectorFilterControl ctl;
                ctl.prepare (fs);
                ctl.setTargets (hpf, mode);
                ctl.snap();
                const auto tag = weight::params::thrustNames[mode] + "_hpf" + juce::String ((int) hpf) + "_" + juce::String ((int) fs);
                juce::String csv ("hz,expected_db,measured_db,error_db,ideal_thrust_db\n");
                double maxErr = 0, maxIdeal = 0;
                int points = 0;
                for (double hz = 10.0; hz <= std::min (20000.0, 0.45 * fs); hz *= std::pow (2.0, 1.0 / 6.0))
                {
                    DetectorFilter f;
                    const int settle = (int) (0.3 * fs), n = (int) (std::max (0.1, 20.0 / hz) * fs);
                    std::vector<double> y ((size_t) n);
                    const double w = 2.0 * juce::MathConstants<double>::pi * hz / fs;
                    for (int i = 0; i < settle + n; ++i)
                    {
                        const double out = f.process (0.25 * std::sin (w * i), ctl);
                        if (i >= settle)
                            y[(size_t) (i - settle)] = out;
                    }
                    const double measured = 20.0 * std::log10 (sineFit (y.data(), n, fs, hz).amp / 0.25);
                    const double expected = detectorResponseDb (hz, hpf, mode, fs);
                    const double ideal = detector::thrustIdealDb (hz, mode);
                    csv << hz << "," << expected << "," << measured << "," << measured - expected << "," << ideal << "\n";
                    if (expected > -60.0)
                    {
                        maxErr = std::max (maxErr, std::abs (measured - expected));
                        ++points;
                    }
                    if (hz >= 20.0)
                        maxIdeal = std::max (maxIdeal, std::abs (thrustResponseDb (hz, mode, fs) - ideal));
                }
                if (hpf < 21.0f)
                    outputDirectory().getChildFile ("WEIGHT_thrust_" + tag + ".csv").replaceWithText (csv);
                summary << tag << "," << points << "," << maxErr << "," << maxIdeal << "\n";
                MEASURE (tag + ": max |measured - detectorResponseDb| (dB)", maxErr);
                MEASURE (tag + ": max |thrustResponseDb - documented curve| 20 Hz..20 kHz (dB)", maxIdeal);
                CHECK_MSG (maxErr < 0.01, tag);
                CHECK (points > 30);
                // documented curves: Hard within 0.75 dB of the 10 dB/decade line,
                // Neutral within 1.5 dB of the piecewise curve (its corners are
                // necessarily rounded by a half-order slope); Clean is flat.
                CHECK_MSG (maxIdeal < (mode == tHard ? 0.75 : mode == tNeutral ? 1.5 : 1.0e-9), tag);
            }
    outputDirectory().getChildFile ("WEIGHT_thrust_summary.csv").replaceWithText (summary);

    // Documented anchors (API manual: LOUD down 15 dB at 20 Hz, up 15 dB at 20 kHz, 10 dB/decade).
    const double fs = 96000.0;
    MEASURE ("Hard: 20 Hz (dB)", thrustResponseDb (20.0, tHard, fs));
    MEASURE ("Hard: 200 Hz (dB)", thrustResponseDb (200.0, tHard, fs));
    MEASURE ("Hard: 2 kHz (dB)", thrustResponseDb (2000.0, tHard, fs));
    MEASURE ("Hard: 20 kHz (dB)", thrustResponseDb (20000.0, tHard, fs));
    MEASURE ("Neutral: 20 Hz (dB)", thrustResponseDb (20.0, tNeutral, fs));
    MEASURE ("Neutral: 1 kHz (dB)", thrustResponseDb (1000.0, tNeutral, fs));
    MEASURE ("Neutral: 20 kHz (dB)", thrustResponseDb (20000.0, tNeutral, fs));
    CHECK_NEAR (thrustResponseDb (20.0, tHard, fs), -15.0, 0.5);
    CHECK_NEAR (thrustResponseDb (20000.0, tHard, fs), 15.0, 0.5);
    CHECK_NEAR (thrustResponseDb (std::sqrt (20.0 * 20000.0), tHard, fs), 0.0, 1.0e-9);
    CHECK_NEAR (thrustResponseDb (2000.0, tHard, fs) - thrustResponseDb (200.0, tHard, fs), 10.0, 0.5);
    CHECK_NEAR (thrustResponseDb (std::sqrt (200.0 * 3000.0), tNeutral, fs), 0.0, 1.0e-9);
    CHECK (std::abs (thrustResponseDb (500.0, tNeutral, fs)) < 0.6 && std::abs (thrustResponseDb (1500.0, tNeutral, fs)) < 0.6);
    CHECK (thrustResponseDb (30.0, tNeutral, fs) < -6.0 && thrustResponseDb (15000.0, tNeutral, fs) > 5.0);
    CHECK (thrustResponseDb (30.0, tNeutral, fs) > thrustResponseDb (30.0, tHard, fs) + 3.0);
    // SC HPF anchors: 12 dB/oct Butterworth (-3 dB at the cutoff); 20 Hz only touches subsonics.
    CHECK_NEAR (scHpfResponseDb (120.0, 120.0, fs), -3.0103, 0.01);
    CHECK_NEAR (scHpfResponseDb (30.0, 120.0, fs), -24.1, 0.2);
    MEASURE ("SC HPF 20 Hz: response at 40 Hz (dB)", scHpfResponseDb (40.0, 20.0, fs));
    CHECK (scHpfResponseDb (40.0, 20.0, fs) > -0.3);
}

TEST ("WEIGHT: the processor's detector sees exactly the THRUST + SC HPF response; THRUST reduces low-frequency-driven GR")
{
    // GR for a tone at level L equals the static law at L + response(f).
    struct Case { double hz; int thrust; float hpf, level; int topo; };
    const Case cases[] = { { 60.0, tHard, 20.0f, -6.0f, ff }, { 100.0, tNeutral, 20.0f, -8.0f, ff }, { 1000.0, tHard, 20.0f, -12.0f, ff },
                           { 8000.0, tHard, 20.0f, -24.0f, ff }, { 8000.0, tNeutral, 20.0f, -22.0f, fb }, { 60.0, tClean, 120.0f, -4.0f, ff },
                           { 150.0, tHard, 240.0f, 0.0f, fb }, { 3000.0, tClean, 20.0f, -14.0f, fb } };
    double maxErr = 0;
    for (const auto& c : cases)
    {
        const double fs = 48000.0;
        auto p = makeWeight (fs, 480);
        setParam (*p, "threshold", knobFor (-20.0f));
        setParam (*p, "ratio", (float) r4);
        setParam (*p, "attack", (float) a30);
        setParam (*p, "release", (float) rel50);
        setParam (*p, "thrust", (float) c.thrust);
        setParam (*p, "scHpf", c.hpf);
        setParam (*p, "detector", (float) c.topo);
        p->prepareToPlay (fs, 480);
        render (*p, toBuffer (fadedTone (fs, c.hz, juce::Decibels::decibelsToGain (c.level), (int) (2.0 * fs)), 2), 480);
        const double resp = weight::detectorResponseDb (c.hz, plainValue (*p, "scHpf"), c.thrust, p->getDetectorSampleRate());
        const double expected = WeightModel::staticGainReductionDb (c.level + resp, -20.0, r4, c.topo);
        const double got = p->grLeft.getAverageDb();
        MEASURE (juce::String (c.hz) + " Hz at " + juce::String (c.level) + " dBFS, " + weight::params::thrustNames[c.thrust] + ", HPF "
                     + juce::String (c.hpf) + ", " + topoName (c.topo) + ": GR (300 ms average) / expected (dB)", got);
        MEASURE ("   expected from the response (" + juce::String (resp, 2) + " dB)", expected);
        maxErr = std::max (maxErr, std::abs (got - expected));
    }
    MEASURE ("max |GR - static law at the filtered level| (dB)", maxErr);
    // low tones carry the RMS detector's 2f ripple (5 ms): a few hundredths of a dB
    CHECK (maxErr < 0.25);

    // THRUST reduces low-frequency-driven GR: a loud 50 Hz bass under a
    // -20 dBFS 2 kHz tone. The 2 kHz tone's own GR stays similar.
    auto grOf = [] (int thrust, bool withBass)
    {
        const double fs = 48000.0;
        auto p = makeWeight (fs, 480);
        setParam (*p, "threshold", knobFor (-24.0f));
        setParam (*p, "ratio", (float) r4);
        setParam (*p, "attack", (float) a30);
        setParam (*p, "release", (float) rel500);
        setParam (*p, "thrust", (float) thrust);
        p->prepareToPlay (fs, 480);
        auto sig = fadedTone (fs, 2000.0, 0.1f, (int) (2.0 * fs));
        if (withBass)
        {
            const auto bass = fadedTone (fs, 50.0, 0.5f, (int) (2.0 * fs));
            for (size_t i = 0; i < sig.size(); ++i) sig[i] += bass[i];
        }
        render (*p, toBuffer (sig, 2), 480);
        return p->grLeft.getAverageDb();
    };
    const float cleanBass = grOf (tClean, true), neutralBass = grOf (tNeutral, true), hardBass = grOf (tHard, true);
    const float cleanTone = grOf (tClean, false), hardTone = grOf (tHard, false);
    MEASURE ("50 Hz bass + 2 kHz tone: GR Clean / Neutral / Hard (dB)", cleanBass);
    MEASURE ("   Neutral", neutralBass);
    MEASURE ("   Hard", hardBass);
    MEASURE ("2 kHz tone alone: GR Clean / Hard (dB)", cleanTone);
    MEASURE ("   Hard", hardTone);
    CHECK (neutralBass < cleanBass - 3.0f);
    CHECK (hardBass < neutralBass - 1.0f);
    // Hard weights 2 kHz up by thrustResponseDb(2 kHz) (+5.2 dB): at 4:1 the
    // tone alone gets S x that much more GR (above the knee).
    const double hardAt2k = weight::thrustResponseDb (2000.0, tHard, 48000.0);
    MEASURE ("2 kHz tone alone: GR Hard - Clean (dB) / expected", hardTone - cleanTone);
    MEASURE ("   expected", 0.75 * hardAt2k);
    CHECK_NEAR (hardTone - cleanTone, 0.75 * hardAt2k, 0.1);

    // THRUST is detector-only: without GR the audio is bit-identical whatever it is.
    auto renderThrust = [] (int thrust)
    {
        auto p = makeWeight (48000.0, 256);
        setParam (*p, "threshold", 10.0f);
        setParam (*p, "thrust", (float) thrust);
        p->prepareToPlay (48000.0, 256);
        juce::Random rng (7);
        juce::AudioBuffer<float> noise (2, 48000);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < noise.getNumSamples(); ++i)
                noise.setSample (ch, i, (rng.nextFloat() * 2.0f - 1.0f) * 0.05f);
        return render (*p, noise, 256);
    };
    CHECK_MSG (identical (renderThrust (tClean), renderThrust (tHard)), "THRUST changed the audio path");
}

TEST ("WEIGHT: SC HPF: the sweep matches scHpfResponseDb; it reduces low-frequency pumping (kick-like 50 Hz bursts)")
{
    for (double fs : { 44100.0, 96000.0 })
        for (float hpf : { 20.0f, 60.0f, 120.0f, 240.0f })
        {
            weight::DetectorFilterControl ctl;
            ctl.prepare (fs);
            ctl.setTargets (hpf, tClean);
            ctl.snap();
            double maxErr = 0;
            for (double hz = 10.0; hz <= 2000.0; hz *= std::pow (2.0, 1.0 / 6.0))
            {
                weight::DetectorFilter f;
                const int settle = (int) (0.3 * fs), n = (int) (std::max (0.1, 20.0 / hz) * fs);
                std::vector<double> y ((size_t) n);
                const double w = 2.0 * juce::MathConstants<double>::pi * hz / fs;
                for (int i = 0; i < settle + n; ++i)
                {
                    const double out = f.process (0.25 * std::sin (w * i), ctl);
                    if (i >= settle) y[(size_t) (i - settle)] = out;
                }
                const double measured = 20.0 * std::log10 (sineFit (y.data(), n, fs, hz).amp / 0.25);
                const double expected = weight::scHpfResponseDb (hz, hpf, fs);
                if (expected > -60.0)
                    maxErr = std::max (maxErr, std::abs (measured - expected));
            }
            MEASURE ("SC HPF " + juce::String (hpf) + " Hz @" + juce::String (fs) + ": max |measured - scHpfResponseDb| (dB)", maxErr);
            CHECK (maxErr < 0.01);
        }

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
        auto p = makeWeight (fs, 64);
        setParam (*p, "threshold", knobFor (-22.0f));
        setParam (*p, "ratio", (float) r4);
        setParam (*p, "attack", (float) a1);
        setParam (*p, "release", (float) rel100);
        setParam (*p, "scHpf", hpf);
        p->prepareToPlay (fs, 64);
        std::vector<GrBlock> gr;
        const auto out = render (*p, toBuffer (sig, 2), 64, &gr);
        const int win = (int) (0.02 * fs);
        double lo = 1e9, hi = -1e9;
        for (int s = (int) (0.6 * fs); s + win < n; s += win / 2)
        {
            double re = 0, im = 0;
            for (int i = 0; i < win; ++i)
            {
                const double ph = 2.0 * juce::MathConstants<double>::pi * 1000.0 * (s + i) / fs;
                re += out.getSample (0, s + i) * std::cos (ph);
                im += out.getSample (0, s + i) * std::sin (ph);
            }
            const double level = 20.0 * std::log10 (2.0 * std::sqrt (re * re + im * im) / win);
            lo = std::min (lo, level);
            hi = std::max (hi, level);
        }
        double gLo = 1e9, gHi = -1e9;
        for (size_t b = (size_t) (0.6 * fs / 64); b < gr.size(); ++b)
        {
            gLo = std::min (gLo, (double) gr[b].l);
            gHi = std::max (gHi, (double) gr[b].l);
        }
        grSwing = gHi - gLo;
        return hi - lo;
    };
    double grOff, grOn;
    const double swingOff = pumping (20.0f, grOff), swingOn = pumping (240.0f, grOn);
    MEASURE ("1 kHz tone level swing, SC HPF 20 Hz (dB)", swingOff);
    MEASURE ("1 kHz tone level swing, SC HPF 240 Hz (dB)", swingOn);
    MEASURE ("GR swing, SC HPF 20 Hz (dB)", grOff);
    MEASURE ("GR swing, SC HPF 240 Hz (dB)", grOn);
    MEASURE ("pumping reduction factor", swingOff / std::max (swingOn, 1.0e-3));
    CHECK (swingOff > 4.0);
    CHECK (swingOn < swingOff / 4.0);
    CHECK (grOn < grOff / 4.0);
}

TEST ("WEIGHT: stereo-link sweep: a signal only in L reduces R from 0 (link 0) to L's GR (link 100), as documented (CSV)")
{
    // Documented law (MODEL_SPEC 5): cv_L = c_L + (k/2)(c_R - c_L), cv_R = c_R + (k/2)(c_L - c_R).
    // With R silent (c_R = 0): cv_R = (k/2) c_L and cv_L = (1 - k/2) c_L.
    // FF: c_L is the static law of L's level, whatever the link. FB: L's
    // detector reads its own (linked) output, so c_L solves c = T(level - (1 - k/2) c).
    const double fs = 48000.0, thr = -20.0, level = -6.0;
    juce::String csv ("topology,link_pct,gr_left_db,gr_right_db,expected_left,expected_right,att_left_db,att_right_db\n");
    for (int topo : { ff, fb })
        for (float link : { 0.0f, 25.0f, 50.0f, 75.0f, 100.0f })
        {
            auto p = makeWeight (fs, 256);
            setParam (*p, "threshold", knobFor ((float) thr));
            setParam (*p, "ratio", (float) r4);
            setParam (*p, "attack", (float) a30);
            setParam (*p, "release", (float) rel50);
            setParam (*p, "detector", (float) topo);
            setParam (*p, "link", link);
            p->prepareToPlay (fs, 256);
            const int n = (int) (fs * 2.0);
            const auto l = sine (fs, 1000.0, juce::Decibels::decibelsToGain ((float) level), n);
            const auto r = sine (fs, 3000.0, 0.001f, n); // -60 dBFS: far below threshold on its own
            const auto in = stereo (l, r);
            const auto out = render (*p, in, 256);
            const double k = link * 0.01, h = 0.5 * k, s = WeightModel::ratioSlope (r4);
            double cL;
            if (topo == ff)
                cL = WeightModel::staticGainReductionDb (level, thr, r4, ff);
            else
            {
                double lo = 0, hi = 40;
                for (int it = 0; it < 200; ++it)
                {
                    const double c = 0.5 * (lo + hi);
                    (c - WeightModel::gainComputerDb (level - (1.0 - h) * c, thr, s) > 0 ? hi : lo) = c;
                }
                cL = 0.5 * (lo + hi);
            }
            const double expL = (1.0 - h) * cL, expR = h * cL;
            const float gl = p->grLeft.getAverageDb(), gr = p->grRight.getAverageDb();
            const int w0 = (int) (1.5 * fs), wn = (int) (0.4 * fs);
            const float attL = db (rms (in.getReadPointer (0) + w0, wn)) - db (rms (out.getReadPointer (0) + w0, wn));
            const float attR = db (rms (in.getReadPointer (1) + w0, wn)) - db (rms (out.getReadPointer (1) + w0, wn));
            csv << topoName (topo) << "," << link << "," << gl << "," << gr << "," << expL << "," << expR << "," << attL << "," << attR << "\n";
            const auto tag = topoName (topo) + " link " + juce::String (link) + " %";
            MEASURE (tag + ": GR L / R (dB)", gl);
            MEASURE ("   R", gr);
            CHECK_NEAR (gl, expL, 0.03);
            CHECK_NEAR (gr, expR, 0.03);
            CHECK_NEAR (attL, gl, 0.03); // the audio carries exactly the telemetry, per channel
            CHECK_NEAR (attR, gr, 0.03);
            if (link == 0.0f)
                CHECK (gr < 1.0e-6f);
            if (link == 100.0f)
                CHECK_NEAR (gr, gl, 1.0e-4);
        }
    outputDirectory().getChildFile ("WEIGHT_stereo_link_sweep.csv").replaceWithText (csv);

    // Identical channels: the link changes nothing (normalised sum).
    auto grFor = [&] (float link)
    {
        auto p = makeWeight (fs, 256);
        setParam (*p, "threshold", knobFor ((float) thr));
        setParam (*p, "link", link);
        p->prepareToPlay (fs, 256);
        render (*p, toBuffer (sine (fs, 1000.0, 0.5f, 48000), 2), 256);
        return p->grLeft.getAverageDb();
    };
    MEASURE ("L = R: GR at link 0 / 100 (dB)", grFor (0.0f));
    CHECK_NEAR (grFor (0.0f), grFor (100.0f), 1.0e-4);
}

TEST ("WEIGHT: per-channel GR telemetry equals each channel's applied gain (error trace); Peak / RMS / Average follow their formulas")
{
    const double fs = 48000.0;
    const int block = 64;
    const int n = (int) (fs * 3.0);
    // Deterministic asymmetric stereo programme: level steps on L, bursts on R.
    std::vector<float> l = sine (fs, 1000.0, 1.0f, n), r = sine (fs, 700.0, 1.0f, n);
    const float levels[] = { 0.05f, 0.3f, 0.6f, 0.15f, 0.9f, 0.4f };
    for (int i = 0; i < n; ++i)
    {
        l[(size_t) i] *= levels[(i / (int) (0.5 * fs)) % 6];
        r[(size_t) i] *= (i / (int) (0.11 * fs)) % 3 == 0 ? 0.8f : 0.02f;
    }

    juce::String trace ("block,ch,peak_tel,peak_applied,rms_tel,rms_formula,avg_tel,avg_formula\n");
    double maxPeak = 0, maxRms = 0, maxAvg = 0, maxLR = 0;
    for (int topo : { ff, fb })
        for (float link : { 0.0f, 50.0f, 100.0f })
        {
            auto p = makeWeight (fs, block);
            setParam (*p, "threshold", knobFor (-24.0f));
            setParam (*p, "ratio", (float) r4);
            setParam (*p, "attack", (float) a1);
            setParam (*p, "release", (float) rel100);
            setParam (*p, "detector", (float) topo);
            setParam (*p, "link", link);
            p->prepareToPlay (fs, block);
            const int W = GrTelemetry::windowLength (fs);
            CHECK (p->grLeft.getWindowLength() == W && W == 14400);

            std::vector<float> appliedL ((size_t) block), appliedR ((size_t) block);
            std::vector<float> histL, histR; // every applied gain since the start
            auto& model = p->getModelForTesting();
            auto io = stereo (l, r);
            juce::MidiBuffer midi;
            for (int pos = 0; pos < n; pos += block)
            {
                model.setGainTraceForTesting (appliedL.data(), appliedR.data(), block);
                juce::AudioBuffer<float> view (io.getArrayOfWritePointers(), 2, pos, block);
                p->processBlock (view, midi);
                CHECK (model.getGainTraceCount() == block);
                histL.insert (histL.end(), appliedL.begin(), appliedL.end());
                histR.insert (histR.end(), appliedR.begin(), appliedR.end());

                for (int ch = 0; ch < 2; ++ch)
                {
                    const auto& hist = ch == 0 ? histL : histR;
                    const auto& applied = ch == 0 ? appliedL : appliedR;
                    // formulas, from the applied gains alone (missing history = g of 1)
                    double peak = 0, sumDb = 0, sumG2 = 0;
                    for (float g : applied) peak = std::max (peak, -20.0 * std::log10 ((double) g));
                    for (int k = 0; k < W; ++k)
                    {
                        const int idx = (int) hist.size() - W + k;
                        const double g = idx >= 0 ? (double) hist[(size_t) idx] : 1.0;
                        sumDb += -20.0 * std::log10 (g);
                        sumG2 += g * g;
                    }
                    const double rmsF = -10.0 * std::log10 (sumG2 / W), avgF = sumDb / W;
                    // the GUI path: the mode-selected readings
                    const float tPeak = p->takeGrDb (ch, WeightProcessor::GrMode::peak);
                    const float tRms = p->takeGrDb (ch, WeightProcessor::GrMode::rms);
                    const float tAvg = p->takeGrDb (ch, WeightProcessor::GrMode::average);
                    maxPeak = std::max (maxPeak, std::abs (tPeak - peak));
                    maxRms = std::max (maxRms, std::abs (tRms - rmsF));
                    maxAvg = std::max (maxAvg, std::abs (tAvg - avgF));
                    if (topo == ff && link == 50.0f)
                        trace << pos / block << "," << ch << "," << tPeak << "," << peak << "," << tRms << "," << rmsF << "," << tAvg << "," << avgF << "\n";
                }
            }
            model.setGainTraceForTesting (nullptr, nullptr, 0);

            // L and R are reduced differently below 100 % link: the telemetry shows it.
            double lr = 0;
            for (size_t i = 0; i < histL.size(); ++i)
                lr = std::max (lr, std::abs (20.0 * std::log10 ((double) histL[i] / (double) histR[i])));
            MEASURE (topoName (topo) + " link " + juce::String (link) + " %: max |GR L - GR R| (dB)", lr);
            if (link < 100.0f)
                CHECK (lr > 1.0);
            else
                CHECK (lr < 1.0e-5);
            maxLR = std::max (maxLR, lr);

            // Audio check: in steady windows (per channel), the attenuation of the
            // output equals that channel's telemetry.
            if (link == 0.0f && topo == ff)
            {
                auto q = makeWeight (fs, block);
                setParam (*q, "threshold", knobFor (-24.0f));
                setParam (*q, "ratio", (float) r4);
                setParam (*q, "attack", (float) a1);
                setParam (*q, "release", (float) rel100);
                setParam (*q, "link", 0.0f);
                q->prepareToPlay (fs, block);
                const auto in = toBuffer (l, 2);
                std::vector<GrBlock> gr;
                const auto out = render (*q, in, block, &gr);
                const int win = 960;
                double maxAudioErr = 0;
                int windows = 0;
                for (int s = 4800; s + win < n; s += win)
                {
                    const int b0 = s / block, b1 = (s + win) / block;
                    float tMin = 1e9f, tMax = -1e9f;
                    double tMean = 0;
                    for (int b = b0; b < b1; ++b)
                    {
                        tMin = std::min (tMin, gr[(size_t) b].l);
                        tMax = std::max (tMax, gr[(size_t) b].l);
                        tMean += gr[(size_t) b].l;
                    }
                    tMean /= (b1 - b0);
                    if (tMax - tMin > 0.02f || tMean < 0.5)
                        continue;
                    const double att = db (rms (l.data() + s, win)) - db (rms (out.getReadPointer (0) + s, win));
                    maxAudioErr = std::max (maxAudioErr, std::abs (att - tMean));
                    ++windows;
                }
                MEASURE ("max |telemetry - attenuation on the audio| (dB), steady windows", maxAudioErr);
                MEASURE ("steady windows compared", windows);
                CHECK (windows >= 20);
                CHECK (maxAudioErr < 0.05);
            }
        }
    outputDirectory().getChildFile ("WEIGHT_gr_telemetry_error_trace.csv").replaceWithText (trace);
    MEASURE ("max |Peak telemetry - deepest applied GR of the block| (dB)", maxPeak);
    MEASURE ("max |RMS telemetry - formula| (dB)", maxRms);
    MEASURE ("max |Average telemetry - formula| (dB)", maxAvg);
    CHECK (maxPeak < 1.0e-4);
    CHECK (maxRms < 1.0e-4);
    CHECK (maxAvg < 1.0e-4);

    // Deterministic analytic case, on the telemetry object itself: a GR that
    // is 0 dB for half the window and D dB for the other half.
    {
        GrTelemetry t;
        t.prepare (1000.0); // W = 300
        const float D = 12.0f, g = (float) std::pow (10.0, -D / 20.0);
        for (int i = 0; i < 150; ++i) t.push (0.0f, 1.0f);
        for (int i = 0; i < 150; ++i) t.push (D, g);
        t.publish();
        const auto rd = t.take();
        MEASURE ("half 0 dB / half 12 dB: Peak / RMS / Average (dB)", rd.peakDb);
        MEASURE ("   RMS", rd.rmsDb);
        MEASURE ("   Average", rd.averageDb);
        CHECK_NEAR (rd.peakDb, 12.0, 1e-6);
        CHECK_NEAR (rd.averageDb, 6.0, 1e-5);
        CHECK_NEAR (rd.rmsDb, -10.0 * std::log10 (0.5 * (1.0 + (double) g * g)), 1e-5); // 2.94 dB
        // the peak hold resets on read; with no new audio it reports the latest value
        CHECK_NEAR (t.takePeakDb(), 12.0, 1e-6);
        for (int i = 0; i < 300; ++i) t.push (3.0f, (float) std::pow (10.0, -3.0 / 20.0));
        t.publish();
        CHECK_NEAR (t.getAverageDb(), 3.0, 1e-5);
        CHECK_NEAR (t.getRmsDb(), 3.0, 1e-5);
        CHECK_NEAR (t.takePeakDb(), 3.0, 1e-6);
        // never clipped at a meter range
        for (int i = 0; i < 300; ++i) t.push (45.0f, (float) std::pow (10.0, -45.0 / 20.0));
        t.publish();
        CHECK_NEAR (t.getAverageDb(), 45.0, 1e-4);
        CHECK_NEAR (t.getRmsDb(), 45.0, 1e-3);
    }

    // Beyond the 20 dB the meters show, the telemetry stays honest.
    auto p = makeWeight (fs, 480);
    setParam (*p, "threshold", knobFor (-38.0f));
    setParam (*p, "ratio", (float) rInf);
    setParam (*p, "attack", (float) a30u);
    p->prepareToPlay (fs, 480);
    render (*p, toBuffer (sine (fs, 1000.0, 0.9f, 48000), 2), 480);
    const auto rd = p->grLeft.take();
    MEASURE ("GR telemetry for 37 dB of overshoot at infinity (dB): peak", rd.peakDb);
    CHECK (rd.peakDb > 35.0f && rd.averageDb > 35.0f && rd.rmsDb > 35.0f);
}

TEST ("WEIGHT: GR MODE only changes what the meters read: the audio is bit-identical in Peak, RMS and Average")
{
    const double fs = 48000.0;
    auto renderMode = [&] (int mode, std::vector<float>& reading)
    {
        auto p = makeWeight (fs, 128);
        setParam (*p, "threshold", knobFor (-26.0f));
        setParam (*p, "ratio", (float) r6);
        setParam (*p, "attack", (float) a300u);
        setParam (*p, "release", (float) rel100);
        setParam (*p, "link", 40.0f);
        setParam (*p, "grMode", (float) mode);
        p->prepareToPlay (fs, 128);
        juce::Random rng (5);
        juce::AudioBuffer<float> in (2, 48000);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < in.getNumSamples(); ++i)
                in.setSample (ch, i, (0.5f + 0.4f * (float) std::sin (i * 0.0007 + ch)) * (rng.nextFloat() - 0.5f));
        juce::AudioBuffer<float> out (in);
        juce::MidiBuffer midi;
        for (int pos = 0; pos < out.getNumSamples(); pos += 128)
        {
            juce::AudioBuffer<float> view (out.getArrayOfWritePointers(), 2, pos, 128);
            p->processBlock (view, midi);
            if (pos % 1024 == 0)
                reading.push_back (p->takeGrDb (0)); // the mode-selected reading
        }
        CHECK ((int) p->getGrMode() == mode);
        return out;
    };
    std::vector<float> rPeak, rRms, rAvg;
    const auto a = renderMode (gmPeak, rPeak), b = renderMode (gmRms, rRms), c = renderMode (gmAverage, rAvg);
    CHECK_MSG (identical (a, b) && identical (a, c), "GR MODE altered the audio");
    // ... while the readings differ (the selector works)
    double dPR = 0, dRA = 0;
    for (size_t i = 0; i < rPeak.size(); ++i)
    {
        dPR = std::max (dPR, (double) std::abs (rPeak[i] - rRms[i]));
        dRA = std::max (dRA, (double) std::abs (rRms[i] - rAvg[i]));
    }
    MEASURE ("max |Peak - RMS reading| (dB)", dPR);
    MEASURE ("max |RMS - Average reading| (dB)", dRA);
    CHECK (dPR > 0.5 && dRA > 0.01);
}

TEST ("WEIGHT: sidechain: internal detection ignores an enabled key; external with a silent key gives no GR")
{
    const double fs = 48000.0;
    const auto main = toBuffer (sine (fs, 500.0, 0.5f, 24000), 2);
    auto loudKey = toBuffer (sine (fs, 200.0, 0.9f, 24000), 2);
    juce::AudioBuffer<float> silentKey (2, 24000);
    silentKey.clear();

    for (int topo : { ff, fb })
    {
        auto setup = [&] (int keyChannels, bool external)
        {
            auto p = makeWeight (fs, 256, 2, keyChannels);
            setParam (*p, "threshold", knobFor (-20.0f));
            setParam (*p, "detector", (float) topo);
            setParam (*p, "scExternal", external ? 1.0f : 0.0f);
            p->prepareToPlay (fs, 256);
            return p;
        };
        auto noBus = setup (0, false);
        auto withKey = setup (2, false);
        const auto ref = render (*noBus, main, 256);
        const auto internal = renderKeyed (*withKey, main, &loudKey, 256);
        CHECK_MSG (identical (ref, internal), "an unselected key altered the output");
        CHECK (! withKey->isUsingExternalSidechain());

        auto ext = setup (2, true);
        std::vector<GrBlock> gr;
        const auto out = renderKeyed (*ext, main, &silentKey, 256, &gr);
        CHECK (ext->isUsingExternalSidechain());
        float maxGr = 0;
        for (auto& g : gr) maxGr = std::max ({ maxGr, g.l, g.r });
        MEASURE ("external, silent key: max GR (dB), " + topoName (topo), maxGr);
        CHECK (maxGr < 1.0e-6f);
        std::vector<GrBlock> grInt;
        render (*setup (0, false), main, 256, &grInt);
        MEASURE ("internal: GR for the same main input (dB), " + topoName (topo), grInt.back().l);
        CHECK (grInt.back().l > 3.0f);
        CHECK_MSG (identical (out, main), "external silent key: the main signal was altered");
    }
}

TEST ("WEIGHT: sidechain: a sine key follows the static law (feed-forward in both DETECTOR positions); a transient key ducks with the documented timing")
{
    const double fs = 48000.0;
    for (int topo : { ff, fb })
    {
        auto p = makeWeight (fs, 64, 2, 2);
        setParam (*p, "threshold", knobFor (-20.0f));
        setParam (*p, "attack", (float) a30);
        setParam (*p, "release", (float) rel50);
        setParam (*p, "detector", (float) topo);
        setParam (*p, "scExternal", 1.0f);
        p->prepareToPlay (fs, 64);
        const auto main = toBuffer (sine (fs, 300.0, juce::Decibels::decibelsToGain (-30.0f), 48000), 2);
        const auto key = toBuffer (sine (fs, 1000.0, juce::Decibels::decibelsToGain (-8.0f), 48000), 2);
        const auto out = renderKeyed (*p, main, &key, 64);
        // The key replaces the feed, so the loop through the VCA is gone: the
        // hardware's side-chain only works in NEW (feed-forward).
        const double law = WeightModel::staticGainReductionDb (-8.0, -20.0, r2, ff);
        MEASURE ("sine key at -8 dBFS, DETECTOR " + topoName (topo) + ": GR (dB)", p->grLeft.getAverageDb());
        MEASURE ("static feed-forward law at -8 dBFS (dB)", law);
        CHECK_NEAR (p->grLeft.getAverageDb(), law, 0.05);
        CHECK_NEAR (p->grRight.getAverageDb(), law, 0.05);
        const float att = db (rms (main.getReadPointer (0) + 36000, 9600)) - db (rms (out.getReadPointer (0) + 36000, 9600));
        MEASURE ("main signal attenuation (dB)", att);
        CHECK_NEAR (att, law, 0.05);
    }

    // transient key: 50 ms bursts every 500 ms; the main tone ducks and recovers with the release
    auto q = makeWeight (fs, 64, 2, 2);
    setParam (*q, "threshold", knobFor (-20.0f));
    setParam (*q, "attack", (float) a30u);
    setParam (*q, "release", (float) rel200);
    setParam (*q, "ratio", (float) rInf);
    setParam (*q, "scExternal", 1.0f);
    q->prepareToPlay (fs, 64);
    std::vector<float> k ((size_t) (fs * 2.0), 0.0f);
    for (int b = 0; b < 4; ++b)
        for (int i = 0; i < (int) (0.05 * fs); ++i)
            k[(size_t) ((int) (fs * (0.1 + 0.5 * b)) + i)] = 0.5f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 1000.0 * i / fs);
    std::vector<GrBlock> grT;
    const auto burstKey = toBuffer (k, 2);
    renderKeyed (*q, toBuffer (sine (fs, 300.0, 0.05f, (int) (fs * 2.0)), 2), &burstKey, 64, &grT);
    const int burstEndBlock = (int) (fs * 0.15) / 64;
    const double peak = grT[(size_t) burstEndBlock - 1].l;
    const double after = grT[(size_t) (burstEndBlock + (int) (0.2 * fs) / 64)].l;
    MEASURE ("transient key: GR at the end of a burst (dB)", peak);
    MEASURE ("transient key: GR 0.2 s later (dB)", after);
    CHECK_NEAR (peak, WeightModel::staticGainReductionDb (20.0 * std::log10 (0.5), -20.0, rInf, ff), 0.2);
    // e^-1 after the release time, plus the short hold of the RMS detector's own decay
    CHECK (after / peak > 0.3679 && after / peak < 0.45);
    CHECK (grT[(size_t) ((int) (fs * 0.08) / 64)].l < 1.0e-6f); // nothing before the first burst
}

TEST ("WEIGHT: sidechain: stereo key channel mapping (key L drives L, key R drives R, linked as set); a mono key drives both")
{
    const double fs = 48000.0;
    const auto main = toBuffer (sine (fs, 300.0, 0.01f, 48000), 2);
    const auto tone = sine (fs, 1000.0, juce::Decibels::decibelsToGain (-6.0f), 48000);
    const std::vector<float> zero (tone.size(), 0.0f);
    auto grWith = [&] (const juce::AudioBuffer<float>& key, float link)
    {
        auto p = makeWeight (fs, 128, 2, key.getNumChannels());
        setParam (*p, "threshold", knobFor (-20.0f));
        setParam (*p, "ratio", (float) r4);
        setParam (*p, "attack", (float) a30);
        setParam (*p, "release", (float) rel50);
        setParam (*p, "link", link);
        setParam (*p, "scExternal", 1.0f);
        p->prepareToPlay (fs, 128);
        renderKeyed (*p, main, &key, 128);
        CHECK (p->isUsingExternalSidechain());
        return std::make_pair (p->grLeft.getAverageDb(), p->grRight.getAverageDb());
    };
    const double c = WeightModel::staticGainReductionDb (-6.0, -20.0, r4, ff);
    const auto lOnly0 = grWith (stereo (tone, zero), 0.0f), rOnly0 = grWith (stereo (zero, tone), 0.0f);
    const auto lOnly100 = grWith (stereo (tone, zero), 100.0f), both = grWith (stereo (tone, tone), 0.0f);
    const auto monoKey = grWith (toBuffer (tone, 1), 0.0f);
    MEASURE ("key L only, link 0: GR L / R (dB)", lOnly0.first);
    MEASURE ("   R", lOnly0.second);
    MEASURE ("key R only, link 0: GR L / R (dB)", rOnly0.first);
    MEASURE ("   R ", rOnly0.second);
    MEASURE ("key L only, link 100: GR L / R (dB)", lOnly100.first);
    MEASURE ("   R  ", lOnly100.second);
    CHECK_NEAR (lOnly0.first, c, 0.05);
    CHECK (lOnly0.second < 1.0e-5f);
    CHECK (rOnly0.first < 1.0e-5f);
    CHECK_NEAR (rOnly0.second, c, 0.05);
    CHECK_NEAR (lOnly100.first, 0.5 * c, 0.05);
    CHECK_NEAR (lOnly100.second, 0.5 * c, 0.05);
    CHECK_NEAR (both.first, c, 0.05);
    CHECK_NEAR (both.second, c, 0.05);
    CHECK_NEAR (monoKey.first, c, 0.05);
    CHECK_NEAR (monoKey.second, c, 0.05);

    // mono main with a stereo key: the mean power of both key channels
    auto p = makeWeight (fs, 128, 1, 2);
    setParam (*p, "threshold", knobFor (-20.0f));
    setParam (*p, "ratio", (float) r4);
    setParam (*p, "attack", (float) a30);
    setParam (*p, "release", (float) rel50);
    setParam (*p, "scExternal", 1.0f);
    p->prepareToPlay (fs, 128);
    const auto key = stereo (tone, zero);
    renderKeyed (*p, toBuffer (sine (fs, 300.0, 0.01f, 48000), 1), &key, 128);
    MEASURE ("mono main, stereo key L only: GR (dB)", p->grLeft.getAverageDb());
    CHECK_NEAR (p->grLeft.getAverageDb(), WeightModel::staticGainReductionDb (-6.0 - 3.0103, -20.0, r4, ff), 0.05);
    CHECK_NEAR (p->grRight.getAverageDb(), p->grLeft.getAverageDb(), 1.0e-6); // mono: both meters show the channel
}

TEST ("WEIGHT: sidechain: External SC with the bus disabled falls back to the internal detector; switching is click-free")
{
    const double fs = 48000.0;
    const auto main = toBuffer (sine (fs, 700.0, 0.5f, 24000), 2);
    for (int topo : { ff, fb })
    {
        auto run = [&] (int keyChannels, bool external)
        {
            auto p = makeWeight (fs, 256, 2, keyChannels);
            setParam (*p, "threshold", knobFor (-20.0f));
            setParam (*p, "detector", (float) topo);
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
    }

    auto p = makeWeight (fs, 64, 2, 2);
    setParam (*p, "threshold", knobFor (-20.0f));
    setParam (*p, "attack", (float) a1);
    setParam (*p, "release", (float) rel50);
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
    CHECK (maxJump < 0.05f);
}

TEST ("WEIGHT: sidechain: the key never leaks into the output; SC HPF and THRUST act on the key too")
{
    const double fs = 48000.0;
    auto p = makeWeight (fs, 128, 2, 2);
    setParam (*p, "threshold", knobFor (-20.0f));
    setParam (*p, "tone", (float) toneOld);
    setParam (*p, "makeup", 24.0f);
    setParam (*p, "scExternal", 1.0f);
    p->prepareToPlay (fs, 128);
    juce::AudioBuffer<float> silent (2, 48000);
    silent.clear();
    const auto key = toBuffer (sine (fs, 1000.0, 0.9f, 48000), 2);
    const auto out = renderKeyed (*p, silent, &key, 128);
    MEASURE ("silent main, loud key: output peak", out.getMagnitude (0, out.getNumSamples()));
    CHECK (out.getMagnitude (0, out.getNumSamples()) < 1.0e-7f);

    auto q = makeWeight (fs, 128, 2, 2);
    setParam (*q, "threshold", 10.0f);
    setParam (*q, "scExternal", 1.0f);
    q->prepareToPlay (fs, 128);
    const auto main = toBuffer (sine (fs, 300.0, 0.5f, 96000), 2);
    const auto quietKey = toBuffer (sine (fs, 1234.0, 0.2f, 96000), 2);
    const auto o2 = renderKeyed (*q, main, &quietKey, 128);
    const double leak = toneAmplitude (o2.getReadPointer (0) + 48000, 48000, fs, 1234.0);
    MEASURE ("key-frequency component in the output (dBFS)", 20.0 * std::log10 (leak + 1e-15));
    CHECK (leak < 1.0e-6);

    auto grFor = [&] (double keyHz, float hpf, int thrust)
    {
        auto r = makeWeight (fs, 256, 2, 2);
        setParam (*r, "threshold", knobFor (-20.0f));
        setParam (*r, "ratio", (float) r4);
        setParam (*r, "attack", (float) a30);
        setParam (*r, "release", (float) rel50);
        setParam (*r, "scHpf", hpf);
        setParam (*r, "thrust", (float) thrust);
        setParam (*r, "scExternal", 1.0f);
        r->prepareToPlay (fs, 256);
        const auto keyTone = toBuffer (fadedTone (fs, keyHz, juce::Decibels::decibelsToGain (-8.0f), (int) (1.5 * fs)), 2);
        renderKeyed (*r, toBuffer (sine (fs, 500.0, 0.01f, (int) (1.5 * fs)), 2), &keyTone, 256);
        const double resp = weight::detectorResponseDb (keyHz, hpf, thrust, r->getDetectorSampleRate());
        const double expected = WeightModel::staticGainReductionDb (-8.0 + resp, -20.0, r4, ff);
        MEASURE ("key " + juce::String (keyHz) + " Hz, HPF " + juce::String (hpf) + ", THRUST " + weight::params::thrustNames[thrust]
                     + ": GR / expected (dB)", r->grLeft.getAverageDb());
        MEASURE ("   expected", expected);
        CHECK_NEAR (r->grLeft.getAverageDb(), expected, 0.2);
        return r->grLeft.getAverageDb();
    };
    const float low20 = grFor (60.0, 20.0f, tClean), low240 = grFor (60.0, 240.0f, tClean);
    const float lowHard = grFor (60.0, 20.0f, tHard), hiHard = grFor (8000.0, 20.0f, tHard), hiClean = grFor (8000.0, 20.0f, tClean);
    CHECK (low240 < low20 - 3.0f);
    CHECK (lowHard < low20 - 3.0f);
    CHECK (hiHard > hiClean + 3.0f);
}

TEST ("WEIGHT: TONE: Neutral is bit-exact clean; Old and New are measurable, bounded, output stage only (GR unchanged) (CSV)")
{
    // Neutral, no GR: the output is the input, bit for bit.
    {
        auto p = makeWeight (48000.0, 256);
        setParam (*p, "threshold", 10.0f);
        p->prepareToPlay (48000.0, 256);
        juce::Random rng (11);
        juce::AudioBuffer<float> in (2, 48000);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < in.getNumSamples(); ++i)
                in.setSample (ch, i, (rng.nextFloat() * 2.0f - 1.0f) * 0.2f);
        CHECK_MSG (identical (render (*p, in, 256), in), "Neutral without GR is not the identity");
    }

    // THD vs level of the output stage alone (no GR in the way).
    juce::String csv ("tone,level_dbfs,thd_pct,h2_pct,h3_pct\n");
    double thdAt[3][4] {};
    const float levels[] = { -30.0f, -20.0f, -10.0f, 0.0f };
    for (int t : { toneOld, toneNeutral, toneNew })
        for (int li = 0; li < 4; ++li)
        {
            const double fs = 48000.0;
            weight::OutputStage st;
            st.setTone (t);
            st.prepare (fs);
            auto x = sine (fs, 1000.0, juce::Decibels::decibelsToGain (levels[li]), (int) (fs * 1.5));
            for (auto& v : x) { st.advance(); v = st.process (v); }
            const float* d = x.data() + (int) (0.5 * fs);
            const int n = (int) fs;
            const double f = toneAmplitude (d, n, fs, 1000.0);
            const double h2 = toneAmplitude (d, n, fs, 2000.0) / f, h3 = toneAmplitude (d, n, fs, 3000.0) / f;
            thdAt[t][li] = thd (d, n, fs, 1000.0);
            csv << weight::params::toneNames[t] << "," << levels[li] << "," << thdAt[t][li] * 100 << "," << h2 * 100 << "," << h3 * 100 << "\n";
            MEASURE (weight::params::toneNames[t] + ": THD % at " + juce::String (levels[li]) + " dBFS", thdAt[t][li] * 100);
            if (t == toneOld && li == 3) { CHECK (h2 > 2.0 * h3); }
            if (t == toneNew && li == 3) { CHECK (h3 > 2.0 * h2); }
        }
    outputDirectory().getChildFile ("WEIGHT_tone_thd.csv").replaceWithText (csv);
    for (int li = 0; li < 4; ++li)
        CHECK (thdAt[toneNeutral][li] < 1.0e-7);
    for (int t : { toneOld, toneNew })
    {
        CHECK (thdAt[t][3] > 1.0e-3 && thdAt[t][3] < 0.01);       // 0.1 .. 1 % at 0 dBFS
        CHECK (thdAt[t][1] > 1.0e-5 && thdAt[t][1] < 1.0e-3);     // subtle at -20 dBFS
        CHECK (thdAt[t][3] > thdAt[t][2] && thdAt[t][2] > thdAt[t][1]); // grows with level
    }

    // Small-signal response of the stage matches toneResponseDb (shelves).
    for (int t : { toneOld, toneNew })
    {
        double maxErr = 0;
        for (double hz : { 20.0, 40.0, 60.0, 100.0, 300.0, 1000.0, 3000.0, 8000.0, 12000.0, 16000.0, 20000.0 })
        {
            const double fs = 48000.0;
            weight::OutputStage st;
            st.setTone (t);
            st.prepare (fs);
            const int settle = (int) (0.5 * fs), n = (int) (std::max (0.1, 20.0 / hz) * fs);
            std::vector<double> y ((size_t) n);
            const double w = 2.0 * juce::MathConstants<double>::pi * hz / fs;
            for (int i = 0; i < settle + n; ++i)
            {
                st.advance();
                const float o = st.process (0.001f * (float) std::sin (w * i));
                if (i >= settle) y[(size_t) (i - settle)] = o;
            }
            const double measured = 20.0 * std::log10 (sineFit (y.data(), n, fs, hz).amp / 0.001);
            maxErr = std::max (maxErr, std::abs (measured - weight::toneResponseDb (hz, t, fs)));
        }
        MEASURE (weight::params::toneNames[t] + ": max |measured - toneResponseDb| (dB)", maxErr);
        CHECK (maxErr < 0.01);
    }
    MEASURE ("Old: response at 30 Hz / 15 kHz (dB)", weight::toneResponseDb (30.0, toneOld, 48000.0));
    MEASURE ("   15 kHz", weight::toneResponseDb (15000.0, toneOld, 48000.0));
    MEASURE ("New: response at 30 Hz / 15 kHz (dB)", weight::toneResponseDb (30.0, toneNew, 48000.0));
    MEASURE ("   15 kHz ", weight::toneResponseDb (15000.0, toneNew, 48000.0));

    // Output stage only: the GR is bit-identical whatever TONE is.
    auto grTraceFor = [] (int t)
    {
        auto p = makeWeight (48000.0, 128);
        setParam (*p, "threshold", knobFor (-24.0f));
        setParam (*p, "tone", (float) t);
        setParam (*p, "detector", (float) fb); // even with the detector after the VCA
        setParam (*p, "makeup", 12.0f);
        p->prepareToPlay (48000.0, 128);
        std::vector<GrBlock> gr;
        render (*p, toBuffer (sine (48000.0, 500.0, 0.6f, 48000), 2), 128, &gr);
        std::vector<float> v;
        for (auto& g : gr) v.push_back (g.l);
        return v;
    };
    const auto gOld = grTraceFor (toneOld), gNeu = grTraceFor (toneNeutral), gNew = grTraceFor (toneNew);
    CHECK (gNeu.back() > 3.0f);
    CHECK_MSG (gOld == gNeu && gNew == gNeu, "TONE changed the gain reduction");

    // With GR, the processor's THD per tone (1 kHz, -6 dBFS, 4:1, threshold -16 dBFS).
    const auto rOld = renderTone (toneOld, -6.0f, -16.0f), rNeu = renderTone (toneNeutral, -6.0f, -16.0f), rNew = renderTone (toneNew, -6.0f, -16.0f);
    MEASURE ("processor under GR: THD % Old / Neutral / New", rOld.thd * 100);
    MEASURE ("   Neutral", rNeu.thd * 100);
    MEASURE ("   New", rNew.thd * 100);
    CHECK (rNeu.thd < 1.0e-4);
    CHECK (rOld.thd > 5.0 * rNeu.thd && rNew.thd > 5.0 * rNeu.thd);

    // Aliasing inspection (1x, no oversampling): 15 kHz at 0 dBFS, 48 kHz.
    // The 3rd harmonic (45 kHz) folds to 3 kHz.
    for (int t : { toneOld, toneNew })
    {
        weight::OutputStage st;
        st.setTone (t);
        st.prepare (48000.0);
        auto x = sine (48000.0, 15000.0, 1.0f, 96000);
        for (auto& v : x) { st.advance(); v = st.process (v); }
        const double fund = toneAmplitude (x.data() + 48000, 48000, 48000.0, 15000.0);
        const double alias = toneAmplitude (x.data() + 48000, 48000, 48000.0, 3000.0);
        MEASURE (weight::params::toneNames[t] + ", 15 kHz at 0 dBFS: alias at 3 kHz (dBc)", 20.0 * std::log10 (alias / fund + 1e-12));
        CHECK (alias / fund < std::pow (10.0, -50.0 / 20.0));
    }
}

TEST ("WEIGHT: Makeup is an exact gain after the detector (both topologies): the output moves by the setting, the GR does not")
{
    for (int topo : { ff, fb })
    {
        auto run = [&] (float mk, float thr, float level)
        {
            auto p = makeWeight (48000.0, 480);
            setParam (*p, "threshold", knobFor (thr));
            setParam (*p, "detector", (float) topo);
            setParam (*p, "attack", (float) a30);
            setParam (*p, "release", (float) rel50);
            setParam (*p, "makeup", mk);
            p->prepareToPlay (48000.0, 480);
            const auto in = toBuffer (sine (48000.0, 1000.0, juce::Decibels::decibelsToGain (level), 96000), 2);
            const auto out = render (*p, in, 480);
            return std::make_pair (p->grLeft.getAverageDb(), db (rms (out.getReadPointer (0) + 48000, 48000)) - db (rms (in.getReadPointer (0) + 48000, 48000)));
        };
        for (float mk : { -12.0f, -4.5f, 0.0f, 6.0f, 24.0f })
        {
            const auto r = run (mk, -8.0f, -30.0f);
            CHECK (r.first == 0.0f);
            CHECK_NEAR (r.second, mk, 0.01);
        }
        const auto a = run (0.0f, -30.0f, -12.0f), b = run (12.0f, -30.0f, -12.0f);
        MEASURE (topoName (topo) + ": GR at makeup 0 / +12 dB (dB)", a.first);
        MEASURE ("   makeup +12", b.first);
        CHECK (a.first > 3.0f);
        CHECK_NEAR (b.first, a.first, 1.0e-4);
        CHECK_NEAR (b.second - a.second, 12.0, 0.01);
    }
}

TEST ("WEIGHT: ratio, attack, release, THRUST, DETECTOR, TONE, link and SC HPF switching under compression is click-free")
{
    const double fs = 48000.0;
    const int block = 64, blocks = 1600;
    auto run = [&] (bool switching, int fixedRatio, int fixedTopo)
    {
        auto p = makeWeight (fs, block);
        setParam (*p, "threshold", knobFor (-30.0f));
        setParam (*p, "attack", (float) a1);
        setParam (*p, "release", (float) rel100);
        setParam (*p, "ratio", (float) fixedRatio);
        setParam (*p, "detector", (float) fixedTopo);
        p->prepareToPlay (fs, block);
        juce::MidiBuffer midi;
        float y1 = 0, y2 = 0, maxCurv = 0;
        const int ratioSeq[] = { r2, rInf, r1_5, r10, r4, r6, r3 };
        for (int b = 0; b < blocks; ++b)
        {
            if (switching && b % 50 == 0)
            {
                const int k = b / 50;
                setParam (*p, "ratio", (float) ratioSeq[k % 7]);
                setParam (*p, "thrust", (float) (k % 3));
                setParam (*p, "tone", (float) ((k + 1) % 3));
                setParam (*p, "detector", (float) (k % 2));
                setParam (*p, "release", (float) (k % 6));
                setParam (*p, "attack", (float) (3 + k % 4));
                setParam (*p, "link", k % 2 == 0 ? 0.0f : 100.0f);
                setParam (*p, "scHpf", k % 2 == 0 ? 20.0f : 240.0f);
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
    for (int r = 0; r < WeightModel::numRatios; ++r)
        for (int t : { ff, fb })
            baseline = std::max (baseline, run (false, r, t));
    const float switched = run (true, r4, ff);
    MEASURE ("max |2nd difference| over all fixed ratio/topology settings", baseline);
    MEASURE ("max |2nd difference| while switching", switched);
    MEASURE ("equivalent instant gain step while switching (dB)", switched / (0.3 * 0.1151));
    CHECK (switched < 2.0f * baseline);
}

TEST ("WEIGHT: rapid automation of every parameter stays finite and click-free")
{
    const double fs = 48000.0;
    auto p = makeWeight (fs, 64, 2, 2);
    juce::Random rng (42);
    float prevSample = 0.0f, maxJump = 0.0f;
    juce::MidiBuffer midi;
    for (int b = 0; b < 3000; ++b)
    {
        setParam (*p, "threshold", -20.0f + rng.nextFloat() * 30.0f);
        setParam (*p, "attack", (float) rng.nextInt (7));
        setParam (*p, "release", (float) rng.nextInt (6));
        setParam (*p, "ratio", (float) rng.nextInt (7));
        setParam (*p, "makeup", -6.0f + rng.nextFloat() * 12.0f);
        setParam (*p, "thrust", (float) rng.nextInt (3));
        setParam (*p, "detector", (float) rng.nextInt (2));
        setParam (*p, "tone", (float) rng.nextInt (3));
        setParam (*p, "scHpf", 20.0f + rng.nextFloat() * 220.0f);
        setParam (*p, "mix", rng.nextFloat() * 100.0f);
        setParam (*p, "output", -6.0f + rng.nextFloat() * 12.0f);
        setParam (*p, "link", rng.nextFloat() * 100.0f);
        setParam (*p, "grMode", (float) rng.nextInt (3));
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
    MEASURE ("max sample-to-sample jump", maxJump);
    CHECK (maxJump < 0.06f);
}

TEST ("WEIGHT: the editor (open, resized, UI properties, telemetry reads) does not alter the audio")
{
    const double fs = 48000.0;
    auto renderWith = [&] (bool withEditor)
    {
        auto p = makeWeight (fs, 256);
        setParam (*p, "threshold", knobFor (-24.0f));
        setParam (*p, "thrust", (float) tNeutral);
        setParam (*p, "link", 70.0f);
        p->prepareToPlay (fs, 256);
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
                p->grLeft.take();
                p->takeGrDb (1);
                p->inputMeter.takeRms();
                p->outputMeter.takePeak();
            }
        }
        ed.reset();
        return out;
    };
    CHECK_MSG (identical (renderWith (false), renderWith (true)), "the editor altered the audio");
}

TEST ("WEIGHT: state round trip restores every parameter and UI settings and renders identically")
{
    const double fs = 48000.0;
    auto a = makeWeight (fs, 256);
    setParam (*a, "threshold", -7.5f);
    setParam (*a, "attack", (float) a3);
    setParam (*a, "release", (float) rel500);
    setParam (*a, "ratio", (float) r6);
    setParam (*a, "makeup", 4.5f);
    setParam (*a, "thrust", (float) tHard);
    setParam (*a, "detector", (float) fb);
    setParam (*a, "tone", (float) toneNew);
    setParam (*a, "scHpf", 133.0f);
    setParam (*a, "mix", 72.0f);
    setParam (*a, "output", -3.5f);
    setParam (*a, "link", 65.0f);
    setParam (*a, "grMode", (float) gmAverage);
    setParam (*a, "scExternal", 1.0f);
    a->setUiProperty ("scale", 1.25);

    juce::MemoryBlock state;
    a->getStateInformation (state);
    auto b = makeWeight (fs, 256);
    b->setStateInformation (state.getData(), (int) state.getSize());
    for (auto* param : a->getParameters())
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (param))
            CHECK_NEAR (r->getValue(), b->apvts.getParameter (r->paramID)->getValue(), 1e-6);
    CHECK_NEAR ((double) b->getUiProperty ("scale", 1.0), 1.25, 1e-9);

    a->prepareToPlay (fs, 256);
    b->prepareToPlay (fs, 256);
    const auto sig = toBuffer (sine (fs, 300.0, 0.4f, 48000), 2);
    CHECK_MSG (identical (render (*a, sig, 256), render (*b, sig, 256)), "restored instance renders differently");
    auto fresh = makeWeight (fs, 256);
    CHECK (! identical (render (*fresh, sig, 256), render (*b, sig, 256)));

    b->setStateInformation ("nonsense", 8);
    CHECK_NEAR (plainValue (*b, "mix"), 72.0f, 0.01);
}

TEST ("WEIGHT: factory presets load, set every parameter and reject other plugins' presets")
{
    WeightProcessor p;
    const auto& list = p.presets.getPresets();
    int factory = 0;
    for (auto& pr : list) factory += pr.isFactory ? 1 : 0;
    MEASURE ("factory presets", factory);
    CHECK (factory >= 10);

    std::set<int> ratiosSeen, thrustSeen, topoSeen, toneSeen;
    for (int i = 0; i < (int) list.size(); ++i)
        if (list[(size_t) i].isFactory)
        {
            p.loadPreset (i);
            CHECK (list[(size_t) i].params.values.size() == 14);
            for (auto& [id, v] : list[(size_t) i].params.values)
            {
                auto* r = p.apvts.getParameter (id);
                CHECK_MSG (r != nullptr, id);
                if (r) CHECK_NEAR (r->convertFrom0to1 (r->getValue()), v, 0.06);
            }
            CHECK (! p.isPresetModified());
            CHECK (plainValue (p, "scExternal") < 0.5f); // factory presets never switch the routing on
            ratiosSeen.insert (juce::roundToInt (plainValue (p, "ratio")));
            thrustSeen.insert (juce::roundToInt (plainValue (p, "thrust")));
            topoSeen.insert (juce::roundToInt (plainValue (p, "detector")));
            toneSeen.insert (juce::roundToInt (plainValue (p, "tone")));
        }
    CHECK (ratiosSeen.size() >= 5 && thrustSeen.size() == 3 && topoSeen.size() == 2 && toneSeen.size() == 3);

    setParam (p, "mix", 13.0f);
    CHECK (p.isPresetModified());
    PresetManager::Preset out;
    CHECK (p.presets.parse (R"({"format":"tleet-preset","version":1,"plugin":"GLUE","name":"x","params":{}})", out).failed());
    CHECK (p.presets.parse (R"({"format":"tleet-preset","version":99,"plugin":"WEIGHT","name":"x","params":{}})", out).failed());
    const auto json = p.stateToClipboardJson();
    setParam (p, "mix", 99.0f);
    CHECK (p.pasteStateFromJson (json).wasOk());
    CHECK_NEAR (plainValue (p, "mix"), 13.0f, 0.06);
}

TEST ("WEIGHT: CPU cost per instance (measurement)")
{
    for (double fs : { 44100.0, 48000.0, 96000.0, 192000.0 })
        for (int keyChannels : { 0, 2 })
        {
            auto p = makeWeight (fs, 128, 2, keyChannels);
            setParam (*p, "threshold", knobFor (-24.0f));
            setParam (*p, "thrust", (float) tHard);
            setParam (*p, "tone", (float) toneOld);
            setParam (*p, "scHpf", 80.0f);
            setParam (*p, "link", 70.0f);
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
            // per-block timings
            juce::AudioBuffer<float> io (2 + keyChannels, n);
            for (int ch = 0; ch < 2; ++ch) io.copyFrom (ch, 0, in, ch, 0, n);
            for (int k = 0; k < keyChannels; ++k) io.copyFrom (2 + k, 0, key, k, 0, n);
            juce::MidiBuffer midi;
            std::vector<double> times;
            const auto t0 = juce::Time::getMillisecondCounterHiRes();
            for (int pos = 0; pos + 128 <= n; pos += 128)
            {
                juce::AudioBuffer<float> view (io.getArrayOfWritePointers(), 2 + keyChannels, pos, 128);
                const auto b0 = juce::Time::getHighResolutionTicks();
                p->processBlock (view, midi);
                times.push_back (juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - b0));
            }
            const double ms = juce::Time::getMillisecondCounterHiRes() - t0;
            std::sort (times.begin(), times.end());
            const double load = ms / 10000.0 * 100.0;
            const auto tag = juce::String (fs) + " Hz, block 128, key " + juce::String (keyChannels);
            MEASURE (tag + ": % of one core", load);
            MEASURE (tag + ": p99 block time (us)", times[(size_t) (0.99 * (double) times.size())] * 1e6);
            MEASURE (tag + ": max block time (us)", times.back() * 1e6);
            CHECK (load < (fs > 100000.0 ? 20.0 : 10.0));
        }
}
