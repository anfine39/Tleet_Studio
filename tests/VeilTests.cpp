#include "TestFramework.h"
#include "TestSignals.h"
#include <cstring>
#include <set>
#include <tuple>
#include "../plugins/VEIL/PluginProcessor.h"
#include "../plugins/VEIL/Parameters.h"
#include "../plugins/VEIL/dsp/VeilModel.h"
#include "../plugins/VEIL/dsp/VeilLaw.h"
#include "../plugins/VEIL/dsp/Filters.h"
#include "../plugins/VEIL/dsp/TubeStages.h"

using namespace tleet;
using namespace tleet::test;
using tleet::veil::VeilProcessor;
using tleet::veil::VeilModel;
namespace law = tleet::veil::law;
namespace vp = tleet::veil::params;

namespace
{
    enum CharIdx { cClean = 0, cVeil, cThick };
    enum MeterIdx { mGr = 0, mVu, mInOut };
    constexpr double pi = juce::MathConstants<double>::pi;

    void setParam (VeilProcessor& p, const char* id, float plain)
    {
        auto* r = p.apvts.getParameter (id);
        r->setValueNotifyingHost (r->convertTo0to1 (plain));
    }

    float plainValue (VeilProcessor& p, const char* id)
    {
        auto* r = p.apvts.getParameter (id);
        return r->convertFrom0to1 (r->getValue());
    }

    // THRESHOLD knob value for a threshold in dBFS.
    float knobFor (float thresholdDbfs) { return thresholdDbfs - vp::alignmentDbfs; }

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

    // `x` delayed by `d` samples (zeros in front), same length.
    juce::AudioBuffer<float> delayed (const juce::AudioBuffer<float>& x, int d)
    {
        juce::AudioBuffer<float> y (x.getNumChannels(), x.getNumSamples());
        y.clear();
        for (int ch = 0; ch < x.getNumChannels(); ++ch)
            if (x.getNumSamples() > d)
                y.copyFrom (ch, d, x, ch, 0, x.getNumSamples() - d);
        return y;
    }

    struct GrBlock { float l, r; };

    juce::AudioBuffer<float> renderKeyed (VeilProcessor& p, const juce::AudioBuffer<float>& main,
                                          const juce::AudioBuffer<float>* key, int block,
                                          std::vector<GrBlock>* grPerBlock = nullptr)
    {
        const int nm = main.getNumChannels(), nk = key != nullptr ? key->getNumChannels() : 0, n = main.getNumSamples();
        jassert (key == nullptr || key->getNumSamples() >= n); // a shorter key would be read past its end
        juce::AudioBuffer<float> io (nm + nk, n);
        io.clear();
        for (int ch = 0; ch < nm; ++ch) io.copyFrom (ch, 0, main, ch, 0, n);
        for (int k = 0; k < nk; ++k)    io.copyFrom (nm + k, 0, *key, k, 0, std::min (n, key->getNumSamples()));
        juce::MidiBuffer midi;
        for (int pos = 0; pos < n; pos += block)
        {
            const int len = std::min (block, n - pos);
            juce::AudioBuffer<float> view (io.getArrayOfWritePointers(), nm + nk, pos, len);
            p.processBlock (view, midi);
            if (grPerBlock != nullptr)
                grPerBlock->push_back ({ p.takeGrDb (0), p.takeGrDb (1) });
        }
        juce::AudioBuffer<float> out (nm, n);
        for (int ch = 0; ch < nm; ++ch) out.copyFrom (ch, 0, io, ch, 0, n);
        return out;
    }

    juce::AudioBuffer<float> render (VeilProcessor& p, const juce::AudioBuffer<float>& input, int block,
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

    std::unique_ptr<VeilProcessor> makeVeil (double fs, int block, int channels = 2, int keyChannels = 0)
    {
        auto p = std::make_unique<VeilProcessor>();
        const auto set = channels == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo();
        VeilProcessor::BusesLayout layout;
        layout.inputBuses.add (set);
        layout.inputBuses.add (keyChannels == 0 ? juce::AudioChannelSet::disabled()
                                                : keyChannels == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo());
        layout.outputBuses.add (set);
        CHECK_MSG (p->setBusesLayout (layout), "layout rejected");
        p->setRateAndBufferSizeDetails (fs, block);
        p->prepareToPlay (fs, block);
        return p;
    }

    // The colour stages as linear as they go: Clean, no drive, no harmonics, no saturation.
    void cleanColour (VeilProcessor& p)
    {
        setParam (p, "character", (float) cClean);
        setParam (p, "tubeDrive", 0.0f);
        setParam (p, "harmonics", 0.0f);
        setParam (p, "saturation", 0.0f);
    }

    // What the detector and the gain element receive from the input tube stage
    // for a sine of peak level `levelDbfs` (HARMONICS 0, so the stage is its
    // odd soft limit only; MODEL_SPEC.md 6.1): the detector level (dBFS, sine
    // peak calibrated: pi/2 x full-wave average) and the fundamental (dBFS).
    struct StageLevels { double detectorDb, fundamentalDb; };
    StageLevels inputStageSine (double levelDbfs, int character, double drive01)
    {
        const double d = veil::tube::driveGain (drive01);
        const double vc = veil::tube::inputLimit * veil::tube::voicing (character).headroom;
        const double amp = std::pow (10.0, levelDbfs / 20.0);
        const int m = 8192;
        double avg = 0, fund = 0;
        for (int i = 0; i < m; ++i)
        {
            const double th = 2.0 * pi * (i + 0.5) / m, z = amp * std::sin (th) * d / vc;
            const double y = z / std::sqrt (1.0 + z * z) * vc / d;
            avg += std::abs (y) / m;
            fund += 2.0 * y * std::sin (th) / m;
        }
        return { 20.0 * std::log10 (VeilModel::halfPi * avg), 20.0 * std::log10 (fund) };
    }

    std::vector<float> concat (std::vector<float> a, const std::vector<float>& b)
    {
        a.insert (a.end(), b.begin(), b.end());
        return a;
    }

    // Constant-envelope test signal (+A, -A, ...): its rectified value is A at
    // every sample, so the full-wave detector sees a clean step (model level only).
    std::vector<float> alternating (float amp, int n)
    {
        std::vector<float> v ((size_t) n);
        for (int i = 0; i < n; ++i)
            v[(size_t) i] = (i & 1) ? -amp : amp;
        return v;
    }

    // Amplitude of an alternating signal whose detector level (dBFS, sine-peak calibrated) is `levelDbfs`.
    float ampForLevel (double levelDbfs) { return (float) (std::pow (10.0, levelDbfs / 20.0) / VeilModel::halfPi); }

    // Input level (over threshold, dB) at which the static law gives `grDb`.
    double overForGr (double grDb, double compress)
    {
        double lo = -10.0, hi = 120.0;
        for (int i = 0; i < 200; ++i)
        {
            const double x = 0.5 * (lo + hi);
            (law::staticGainReductionDb (x, 0.0, compress) > grDb ? hi : lo) = x;
        }
        return 0.5 * (lo + hi);
    }

    void configureModel (VeilModel& m, double compress, float thrDbfs, float attackMs, float recoveryMs)
    {
        m.setCompress ((float) compress);
        m.setThresholdDbfs (thrDbfs);
        m.setAttackMs (attackMs);
        m.setRecoveryMs (recoveryMs);
        m.setTubeDrive (0.0f);
        m.setHarmonics (0.0f);
        m.setSaturation (0.0f);
        m.setCharacter (cClean);
        m.setScHpf (20.0f);
        m.setHpf (20.0f);
        m.setMix (1.0f);
        m.setStereoLink (true);
    }

    // Runs the bare model (2x rate signal) one sample at a time; returns channel-0 GR per sample.
    std::vector<double> grTrace (VeilModel& m, std::vector<float> l, std::vector<float> r = {}, std::vector<double>* grR = nullptr)
    {
        std::vector<double> gr;
        gr.reserve (l.size());
        const int nch = r.empty() ? 1 : 2;
        for (size_t i = 0; i < l.size(); ++i)
        {
            float* ch[] = { &l[i], r.empty() ? nullptr : &r[i] };
            m.process (ch, nch, nullptr, 0, 1);
            gr.push_back (m.getCurrentGrDb (0));
            if (grR != nullptr)
                grR->push_back (m.getCurrentGrDb (1));
        }
        return gr;
    }

    // Least-squares slope of ln(y) over [i0, i1).
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

    // Time (s) from `from` at which trace first reaches `level` (linear interpolation).
    double crossing (const std::vector<double>& trace, int from, double level, double fs)
    {
        for (int i = from + 1; i < (int) trace.size(); ++i)
            if (trace[(size_t) i] >= level)
            {
                const double p = trace[(size_t) i - 1], v = trace[(size_t) i];
                const double frac = std::abs (v - p) > 1.0e-15 ? (level - p) / (v - p) : 1.0;
                return (i - 1 - from + frac) / fs;
            }
        return -1.0;
    }

    // Independent reference of the documented continuous-time detector and
    // ballistics (MODEL_SPEC 3), RK4 with many sub-steps per sample:
    //   a1' = (r - a1)/tau_d, a2' = (a1 - a2)/tau_d, L = 20 log10(pi/2 a2),
    //   T = staticGainReductionDb(L, thr, compress), c' = (T - c)/tau (attack if T > c, else recovery)
    // for a constant rectified input r from t = 0 (states from rest).
    std::vector<double> referenceTrace (double r, double thr, double compress, double ta, double tr, double fs, int n)
    {
        const double td = VeilModel::detectorTauS;
        auto deriv = [&] (double a1, double a2, double c, double& d1, double& d2, double& dc)
        {
            d1 = (r - a1) / td;
            d2 = (a1 - a2) / td;
            const double t = a2 > 1.0e-12 ? law::staticGainReductionDb (VeilModel::levelDbOfAverage (a2), thr, compress) : 0.0;
            dc = (t - c) / (t > c ? ta : tr);
        };
        const int sub = 8;
        const double h = 1.0 / (fs * sub);
        std::vector<double> out ((size_t) n);
        double a1 = 0, a2 = 0, c = 0;
        for (int i = 0; i < n; ++i)
        {
            for (int k = 0; k < sub; ++k)
            {
                double k1[3], k2[3], k3[3], k4[3];
                deriv (a1, a2, c, k1[0], k1[1], k1[2]);
                deriv (a1 + 0.5 * h * k1[0], a2 + 0.5 * h * k1[1], c + 0.5 * h * k1[2], k2[0], k2[1], k2[2]);
                deriv (a1 + 0.5 * h * k2[0], a2 + 0.5 * h * k2[1], c + 0.5 * h * k2[2], k3[0], k3[1], k3[2]);
                deriv (a1 + h * k3[0], a2 + h * k3[1], c + h * k3[2], k4[0], k4[1], k4[2]);
                a1 += h / 6.0 * (k1[0] + 2 * k2[0] + 2 * k3[0] + k4[0]);
                a2 += h / 6.0 * (k1[1] + 2 * k2[1] + 2 * k3[1] + k4[1]);
                c  += h / 6.0 * (k1[2] + 2 * k2[2] + 2 * k3[2] + k4[2]);
            }
            out[(size_t) i] = c;
        }
        return out;
    }

    struct Fit { double amp, phase; };
    Fit sineFit (const double* d, int n, double fs, double hz)
    {
        double ss = 0, cc = 0, sc = 0, ys = 0, yc = 0;
        const double w = 2.0 * pi * hz / fs;
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

    std::vector<float> fadedTone (double fs, double hz, float amp, int n, double fadeS = 0.05)
    {
        auto v = sine (fs, hz, amp, n);
        const int fade = (int) (fadeS * fs);
        for (int i = 0; i < fade && i < n; ++i)
            v[(size_t) i] *= 0.5f - 0.5f * (float) std::cos (pi * i / fade);
        return v;
    }

    // ---- harmonic measurements on the processor
    struct Colour
    {
        float drive = vp::tubeDriveDefault, harmonics = vp::harmonicsDefault, saturation = vp::saturationDefault;
        int character = cVeil;
        float thresholdKnob = 20.0f; // +2 dBFS: no GR for the levels used unless set
        float compress = 5.0f;
        float input = 0.0f;
    };

    struct ToneResult { double thd, h2, h3, h5, fundDb, grDb; };

    ToneResult renderTone (const Colour& c, float peakDbfs, double hz = 1000.0, double fs = 48000.0)
    {
        auto p = makeVeil (fs, 480);
        setParam (*p, "tubeDrive", c.drive);
        setParam (*p, "harmonics", c.harmonics);
        setParam (*p, "saturation", c.saturation);
        setParam (*p, "character", (float) c.character);
        setParam (*p, "threshold", c.thresholdKnob);
        setParam (*p, "compress", c.compress);
        setParam (*p, "input", c.input);
        setParam (*p, "attack", 20.0f);
        setParam (*p, "recovery", 2000.0f);
        setParam (*p, "scHpf", 20.0f);
        p->prepareToPlay (fs, 480);
        const float amp = juce::Decibels::decibelsToGain (peakDbfs);
        auto in = toBuffer (sine (fs, hz, amp, (int) (fs * 2.0)), 2);
        std::vector<GrBlock> gr;
        auto out = render (*p, in, 480, &gr);
        const int n = (int) fs, start = out.getNumSamples() - n;
        const float* d = out.getReadPointer (0) + start;
        const double f = toneAmplitude (d, n, fs, hz);
        return { thd (d, n, fs, hz), toneAmplitude (d, n, fs, 2 * hz) / f, toneAmplitude (d, n, fs, 3 * hz) / f,
                 toneAmplitude (d, n, fs, 5 * hz) / f, 20.0 * std::log10 (f / amp), (double) gr.back().l };
    }

    double pct (double ratio) { return 100.0 * ratio; }
    double dbc (double ratio) { return 20.0 * std::log10 (std::max (ratio, 1.0e-15)); }
}

// =====================================================================================
TEST ("VEIL: parameter IDs, ranges, skews, defaults, texts and buses match the registry")
{
    VeilProcessor p;
    const char* ids[] = { "input", "threshold", "attack", "recovery", "compress", "mix", "output", "tubeDrive", "harmonics",
                          "saturation", "dryWet", "hpf", "scHpf", "character", "scExternal", "stereoLink", "meterMode", "bypass" };
    for (auto* id : ids)
        CHECK_MSG (p.apvts.getParameter (id) != nullptr, id);
    CHECK (p.getParameters().size() == 18);
    for (auto* param : p.getParameters())
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (param))
            CHECK_MSG (r->getVersionHint() == 1, r->paramID);

    auto def = [&p] (const char* id) { auto* r = p.apvts.getParameter (id); return r->convertFrom0to1 (r->getDefaultValue()); };
    CHECK_NEAR (def ("input"), 0.0f, 1e-5);
    CHECK_NEAR (def ("threshold"), 0.0f, 1e-5);
    CHECK_NEAR (def ("attack"), 50.0f, 1e-3);
    CHECK_NEAR (def ("recovery"), 400.0f, 1e-2);
    CHECK_NEAR (def ("compress"), 5.0f, 1e-5);
    CHECK_NEAR (def ("mix"), 100.0f, 1e-4);
    CHECK_NEAR (def ("output"), 0.0f, 1e-5);
    CHECK_NEAR (def ("tubeDrive"), 28.0f, 1e-4);
    CHECK_NEAR (def ("harmonics"), 42.0f, 1e-4);
    CHECK_NEAR (def ("saturation"), 18.0f, 1e-4);
    CHECK_NEAR (def ("dryWet"), 100.0f, 1e-4);
    CHECK_NEAR (def ("hpf"), 20.0f, 1e-3);
    CHECK_NEAR (def ("scHpf"), 60.0f, 1e-2);
    CHECK_NEAR (def ("character"), (float) cVeil, 1e-5);
    CHECK_NEAR (def ("scExternal"), 0.0f, 1e-5);
    CHECK_NEAR (def ("stereoLink"), 1.0f, 1e-5);
    CHECK_NEAR (def ("meterMode"), (float) mGr, 1e-5);
    CHECK_NEAR (def ("bypass"), 0.0f, 1e-5);

    auto range = [&p] (const char* id, float lo, float mid, float hi)
    {
        auto* r = p.apvts.getParameter (id);
        CHECK_NEAR (r->convertFrom0to1 (0.0f), lo, 1e-3);
        CHECK_NEAR (r->convertFrom0to1 (0.5f), mid, 1e-2);
        CHECK_NEAR (r->convertFrom0to1 (1.0f), hi, 1e-3);
    };
    range ("input", -20.0f, 0.0f, 20.0f);
    range ("threshold", -20.0f, 0.0f, 20.0f);
    range ("output", -20.0f, 0.0f, 20.0f);
    range ("attack", 10.0f, 100.0f, 1000.0f);   // log, centre 100 ms
    range ("recovery", 50.0f, 400.0f, 2000.0f); // skewed, centre 400 ms
    range ("compress", 0.0f, 5.0f, 10.0f);
    range ("mix", 0.0f, 50.0f, 100.0f);
    range ("dryWet", 0.0f, 50.0f, 100.0f);
    range ("tubeDrive", 0.0f, 50.0f, 100.0f);
    range ("harmonics", 0.0f, 50.0f, 100.0f);
    range ("saturation", 0.0f, 50.0f, 100.0f);
    range ("scHpf", 20.0f, 80.0f, 300.0f);      // skewed, centre 80 Hz
    range ("hpf", 20.0f, 100.0f, 300.0f);       // skewed, centre 100 Hz

    // dB knobs: 0 exactly at the top, 0.1 dB legal values, exact round trip through text.
    for (auto* id : { "input", "threshold", "output" })
    {
        auto* f = dynamic_cast<juce::AudioParameterFloat*> (p.apvts.getParameter (id));
        juce::AudioProcessorParameter* r = f;
        const auto& nr = f->getNormalisableRange();
        CHECK_MSG (nr.convertFrom0to1 (0.5f) == 0.0f && nr.convertTo0to1 (0.0f) == 0.5f, id);
        int bad = 0;
        for (int t = -200; t <= 200; ++t)
        {
            const float v = nr.snapToLegalValue ((float) t * 0.1f);
            const float n = nr.convertTo0to1 (v);
            bad += std::abs (v - (float) t * 0.1f) > 1e-5f ? 1 : 0;
            bad += nr.snapToLegalValue (nr.convertFrom0to1 (n)) != v ? 1 : 0;
            bad += nr.snapToLegalValue (nr.convertFrom0to1 (r->getValueForText (r->getText (n, 64)))) != v ? 1 : 0;
        }
        CHECK_MSG (bad == 0, id);
    }

    // getText: the skin's readouts print these verbatim.
    auto text = [&p] (const char* id, float plain) { auto* r = p.apvts.getParameter (id); return r->getText (r->convertTo0to1 (plain), 64); };
    CHECK (text ("input", 3.4f) == "+3.4 dB");
    CHECK (text ("threshold", -4.6f) == "-4.6 dB");
    CHECK (text ("output", 0.0f) == "0.0 dB");
    CHECK (text ("attack", 120.0f) == "120 ms");
    CHECK (text ("attack", 10.0f) == "10 ms");
    CHECK (text ("attack", 1000.0f) == "1.0 s");
    CHECK (text ("recovery", 420.0f) == "420 ms");
    CHECK (text ("recovery", 1200.0f) == "1.2 s");
    CHECK (text ("recovery", 2000.0f) == "2.0 s");
    CHECK (text ("compress", 6.2f) == "6.2");
    CHECK (text ("mix", 100.0f) == "100 %");
    CHECK (text ("tubeDrive", 28.0f) == "28 %");
    CHECK (text ("saturation", 18.5f) == "18.5 %");
    CHECK (text ("scHpf", 60.0f) == "60 Hz");
    CHECK (text ("scHpf", 20.0f) == "Off");
    CHECK (text ("hpf", 20.0f) == "Off");
    CHECK (text ("hpf", 100.0f) == "100 Hz");
    CHECK (text ("scExternal", 0.0f) == "Int");
    CHECK (text ("scExternal", 1.0f) == "Ext");
    CHECK (text ("stereoLink", 1.0f) == "Linked");
    CHECK (text ("stereoLink", 0.0f) == "Dual Mono");
    auto fromText = [&p] (const char* id, const char* t) { auto* r = p.apvts.getParameter (id); return r->convertFrom0to1 (r->getValueForText (t)); };
    CHECK_NEAR (fromText ("attack", "1.2 s"), 1000.0f, 1e-3); // clamped to the range
    CHECK_NEAR (fromText ("recovery", "1.2 s"), 1200.0f, 0.5);
    CHECK_NEAR (fromText ("recovery", "420 ms"), 420.0f, 0.5);
    CHECK_NEAR (fromText ("scHpf", "Off"), 20.0f, 1e-3);
    CHECK_NEAR (fromText ("scHpf", "150 Hz"), 150.0f, 0.1);
    CHECK_NEAR (fromText ("hpf", "off"), 20.0f, 1e-3);
    for (float ms : { 10.0f, 37.3f, 99.6f, 100.0f, 456.0f, 999.4f })
        CHECK_NEAR (fromText ("attack", text ("attack", ms).toRawUTF8()), std::round (ms), 0.6);

    auto choices = [&p] (const char* id) { return dynamic_cast<juce::AudioParameterChoice*> (p.apvts.getParameter (id))->choices; };
    CHECK (choices ("character") == juce::StringArray ({ "Clean", "Veil", "Thick" }));
    CHECK (choices ("meterMode") == juce::StringArray ({ "GR", "VU", "In/Out" }));

    // calibration constants
    CHECK_NEAR (vp::thresholdToDbfs (0.0f), -18.0f, 1e-6);
    CHECK_NEAR (vp::thresholdToDbfs (-20.0f), -38.0f, 1e-6);
    CHECK_NEAR (vp::thresholdToDbfs (20.0f), 2.0f, 1e-6);
    CHECK_NEAR (vp::vuDb (0.125893f / std::sqrt (2.0f)), 0.0f, 0.001); // a -18 dBFS sine reads 0 VU

    // Buses: main stereo/mono in == out, plus an optional Sidechain input (disabled by default).
    CHECK (p.getBusCount (true) == 2 && p.getBusCount (false) == 1);
    CHECK (p.getBus (true, 1)->getName() == "Sidechain");
    CHECK (! p.getBus (true, 1)->isEnabledByDefault());
    VeilProcessor::BusesLayout l;
    l.inputBuses.add (juce::AudioChannelSet::stereo());
    l.inputBuses.add (juce::AudioChannelSet::mono());
    l.outputBuses.add (juce::AudioChannelSet::stereo());
    CHECK (p.checkBusesLayoutSupported (l));
    l.inputBuses.getReference (1) = juce::AudioChannelSet::create5point1();
    CHECK (! p.checkBusesLayoutSupported (l));
    l.inputBuses.getReference (1) = juce::AudioChannelSet::disabled();
    l.outputBuses.getReference (0) = juce::AudioChannelSet::mono();
    CHECK (! p.checkBusesLayoutSupported (l));
    l.inputBuses.getReference (0) = juce::AudioChannelSet::mono();
    CHECK (p.checkBusesLayoutSupported (l));
}

TEST ("VEIL: all sample rates and block sizes, mono and stereo, with and without a key: finite output, no steady-state allocation")
{
    for (double fs : { 44100.0, 48000.0, 88200.0, 96000.0, 192000.0 })
        for (int block : { 1, 16, 32, 64, 128, 256, 512, 1024, 2048 })
            for (int channels : { 1, 2 })
                for (int keyChannels : { 0, 2 })
                {
                    auto p = makeVeil (fs, block, channels, keyChannels);
                    setParam (*p, "threshold", -15.0f);
                    setParam (*p, "compress", (float) (block % 11));
                    setParam (*p, "attack", 10.0f);
                    setParam (*p, "recovery", 50.0f);
                    setParam (*p, "character", (float) (block % 3));
                    setParam (*p, "tubeDrive", 80.0f);
                    setParam (*p, "harmonics", 70.0f);
                    setParam (*p, "saturation", 60.0f);
                    setParam (*p, "hpf", 60.0f);
                    setParam (*p, "scHpf", 150.0f);
                    setParam (*p, "mix", 70.0f);
                    setParam (*p, "dryWet", 80.0f);
                    setParam (*p, "input", 6.0f);
                    setParam (*p, "stereoLink", block % 2 == 0 ? 1.0f : 0.0f);
                    setParam (*p, "scExternal", keyChannels > 0 ? 1.0f : 0.0f);
                    p->spectrum.setActive (block % 4 == 0);
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
                            p->takeGrDb (0);           // a UI reading the telemetry meanwhile
                            p->takeGrDb (1);
                            p->inputMeters[1].takeRms();
                            p->outputMeters[0].takePeak();
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
                    CHECK_MSG (p->getLatencySamples() > 0 && p->getLatencySamples() < 64, tag);
                }
}

TEST ("VEIL: NaN/Inf in the input or the key never reaches the output or poisons the state")
{
    for (int keyChannels : { 0, 2 })
    {
        auto p = makeVeil (48000.0, 256, 2, keyChannels);
        setParam (*p, "threshold", -15.0f);
        setParam (*p, "character", (float) cThick);
        setParam (*p, "hpf", 80.0f);
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
        const float gr = p->takeGrDb (0);
        MEASURE ("GR after the bad samples (dB), key " + juce::String (keyChannels), gr);
        CHECK (std::isfinite (gr) && gr > 1.0f && gr < 40.0f);
    }
}

TEST ("VEIL: silence gives silence; DC never triggers GR and is blocked by the wet path; denormal input stays clean")
{
    auto p = makeVeil (48000.0, 512);
    setParam (*p, "output", 20.0f);
    setParam (*p, "input", 20.0f);
    setParam (*p, "threshold", -20.0f);
    setParam (*p, "character", (float) cThick);
    p->prepareToPlay (48000.0, 512);
    juce::AudioBuffer<float> silence (2, 48000);
    silence.clear();
    auto out = render (*p, silence, 512);
    CHECK (out.getMagnitude (0, out.getNumSamples()) < 1.0e-6f);
    CHECK (p->takeGrDb (0) == 0.0f && p->takeGrDb (1) == 0.0f);

    // DC: the coupling high-pass (2 Hz, the input transformer) keeps it out of
    // the tube stages and the detector; the dry path (DRY/WET) keeps it.
    auto q = makeVeil (48000.0, 512);
    setParam (*q, "threshold", -20.0f);
    setParam (*q, "scHpf", 20.0f); // SC HPF off: only the coupling protects the detector
    q->prepareToPlay (48000.0, 512);
    juce::AudioBuffer<float> dc (2, 48000 * 6); // the DC step itself is a transient: give the recovery time
    for (int ch = 0; ch < 2; ++ch)
        juce::FloatVectorOperations::fill (dc.getWritePointer (ch), 0.25f, dc.getNumSamples());
    std::vector<GrBlock> gr;
    out = render (*q, dc, 512, &gr);
    MEASURE ("GR from a 0.25 DC input after 6 s (dB)", gr.back().l);
    MEASURE ("output DC after 6 s", out.getSample (0, out.getNumSamples() - 1));
    CHECK (gr.back().l < 0.01f);
    CHECK (std::abs (out.getSample (0, out.getNumSamples() - 1)) < 1.0e-3f);

    // Denormal-level input into a fresh instance with the most gain.
    auto fresh = makeVeil (48000.0, 512);
    setParam (*fresh, "input", 20.0f);
    setParam (*fresh, "output", 20.0f);
    setParam (*fresh, "tubeDrive", 100.0f);
    juce::AudioBuffer<float> tiny (2, 48000);
    for (int i = 0; i < tiny.getNumSamples(); ++i)
        for (int ch = 0; ch < 2; ++ch)
            tiny.setSample (ch, i, (i & 1) ? 1.0e-38f : -1.0e-38f);
    out = render (*fresh, tiny, 512);
    CHECK (allFinite (out));
    CHECK (out.getMagnitude (0, out.getNumSamples()) < 1.0e-6f);
}

TEST ("VEIL: reported latency equals measured impulse delay; Bypass and DRY/WET 0 are bit-exact latency-aligned dry (also with a key)")
{
    for (double fs : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        auto p = makeVeil (fs, 128, 2, 2);
        const int latency = p->getLatencySamples();
        MEASURE ("reported latency (samples) @" + juce::String (fs), latency);
        CHECK (latency > 0);

        cleanColour (*p);
        setParam (*p, "threshold", 20.0f); // no GR for a small impulse
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
        CHECK_NEAR (out.getSample (0, peakAt), 0.25f, 0.02f);

        // DRY/WET = 0: the delayed plugin input, bit-exact, while the wet path compresses hard on a key.
        auto sig = toBuffer (sine (fs, 997.0, 0.5f, 8192), 2);
        auto key = toBuffer (sine (fs, 300.0, 0.9f, 8192), 2);
        setParam (*p, "threshold", -20.0f);
        setParam (*p, "compress", 10.0f);
        setParam (*p, "character", (float) cThick);
        setParam (*p, "tubeDrive", 100.0f);
        setParam (*p, "saturation", 100.0f);
        setParam (*p, "hpf", 200.0f);
        setParam (*p, "output", 12.0f);
        setParam (*p, "scExternal", 1.0f);
        setParam (*p, "dryWet", 0.0f);
        p->prepareToPlay (fs, 128);
        out = renderKeyed (*p, sig, &key, 128);
        CHECK_MSG (identical (out, delayed (sig, latency)), "DRY/WET 0 is not bit-exact delayed dry");

        // Bypass: the delayed input, bit-exact, whatever the other settings.
        setParam (*p, "dryWet", 100.0f);
        setParam (*p, "bypass", 1.0f);
        p->prepareToPlay (fs, 128);
        out = renderKeyed (*p, sig, &key, 128);
        CHECK_MSG (identical (out, delayed (sig, latency)), "Bypass is not bit-exact delayed dry");
    }
}

TEST ("VEIL: static curves: GR vs level for several COMPRESS and THRESHOLD values match the pure law (model exact; processor, CSV)")
{
    // 1) The model's per-sample solver against the pure bisection law, on a
    //    ripple-free constant-envelope signal (exact detector level).
    double maxModelErr = 0;
    for (double c : { 0.0, 2.5, 5.0, 7.5, 10.0 })
        for (float thr : { -38.0f, -18.0f, 2.0f })
            for (double over : { -8.0, -2.0, 0.0, 1.0, 3.0, 6.0, 10.0, 16.0, 24.0, 34.0 })
            {
                VeilModel m;
                configureModel (m, c, thr, 10.0f, 2000.0f);
                m.prepare (48000.0, 2);
                auto sig = alternating (ampForLevel (thr + over), (int) (96000 * 0.6));
                float* ch[] = { sig.data() };
                m.process (ch, 1, nullptr, 0, (int) sig.size());
                const double level = m.getDetectorLevelDb (0);
                const double expected = law::staticGainReductionDb (level, thr, c);
                maxModelErr = std::max ({ maxModelErr, std::abs (m.getControlDb (0) - expected),
                                          std::abs (m.getTargetGrDb (0) - expected) });
            }
    MEASURE ("model: max |GR - pure static law| (dB)", maxModelErr);
    CHECK (maxModelErr < 1.0e-6);

    // 2) The processor on 1 kHz sines: GR telemetry and output level vs the
    //    pure law of the level the detector receives: the input-stage output
    //    (Clean, TUBE DRIVE 0, HARMONICS 0: only its soft limit, +15 dBFS,
    //    which is visible above about -2 dBFS; inputStageSine).
    juce::String csv ("compress,threshold_dbfs,input_dbfs,gr_measured_db,gr_law_db,out_measured_dbfs,out_law_dbfs,error_db\n");
    double maxGrErr = 0, maxOutErr = 0;
    const double fs = 48000.0;
    for (float c : { 0.0f, 3.0f, 5.0f, 7.5f, 10.0f })
        for (float thr : { -30.0f, -18.0f, -6.0f })
            for (float lvl = thr - 8.0f; lvl <= std::min (thr + 26.0f, 6.0f); lvl += 2.0f)
            {
                auto p = makeVeil (fs, 480);
                cleanColour (*p);
                setParam (*p, "threshold", knobFor (thr));
                setParam (*p, "compress", c);
                setParam (*p, "attack", 20.0f);
                setParam (*p, "recovery", 2000.0f);
                setParam (*p, "scHpf", 20.0f);
                p->prepareToPlay (fs, 480);
                const auto in = toBuffer (sine (fs, 1000.0, juce::Decibels::decibelsToGain (lvl), (int) (fs * 0.8)), 2);
                std::vector<GrBlock> gr;
                const auto out = render (*p, in, 480, &gr);
                const int n = 9600;
                const double outDb = 20.0 * std::log10 (toneAmplitude (out.getReadPointer (0) + out.getNumSamples() - n, n, fs, 1000.0));
                const auto st = inputStageSine (lvl, cClean, 0.0);
                const double lawGr = law::staticGainReductionDb (st.detectorDb, thr, c);
                const double lawOut = st.fundamentalDb - lawGr;
                const double grErr = std::abs (gr.back().l - lawGr), outErr = std::abs (outDb - lawOut);
                maxGrErr = std::max (maxGrErr, grErr);
                maxOutErr = std::max (maxOutErr, outErr);
                csv << c << "," << thr << "," << lvl << "," << gr.back().l << "," << lawGr << "," << outDb << "," << lawOut << "," << grErr << "\n";
            }
    outputDirectory().getChildFile ("VEIL_static_curves.csv").replaceWithText (csv);
    MEASURE ("processor: max |GR telemetry - pure law| (dB)", maxGrErr);
    MEASURE ("processor: max |output level - pure law| (dB)", maxOutErr);
    CHECK (maxGrErr < 0.05);
    CHECK (maxOutErr < 0.1);
}

TEST ("VEIL: vari-mu ratio growth: the effective local ratio rises with GR depth at every COMPRESS (CSV)")
{
    juce::String csv ("compress,gr_db,ratio_law,ratio_measured\n");
    const double fs = 48000.0, thr = -24.0;
    double worstRel = 0;
    for (float c : { 0.0f, 2.5f, 5.0f, 7.5f, 10.0f })
    {
        // pure law: strictly increasing with GR
        double prevR = 0, prevGr = -1;
        bool monotonic = true;
        for (double over = 0.5; over < 40.0; over += 0.5)
        {
            const double r = law::localRatio (thr + over, thr, c), g = law::staticGainReductionDb (thr + over, thr, c);
            if (g > 0.2 && prevGr > 0.2)
                monotonic = monotonic && r > prevR;
            prevR = r;
            prevGr = g;
        }
        CHECK_MSG (monotonic, "pure law ratio not increasing at COMPRESS " + juce::String (c));

        // measured on the processor: finite differences of the output level
        std::vector<double> outDb, grDb;
        std::vector<float> levels;
        for (float lvl = (float) thr; lvl <= (float) thr + 26.0f; lvl += 1.0f)
        {
            auto p = makeVeil (fs, 480);
            cleanColour (*p);
            setParam (*p, "threshold", knobFor ((float) thr));
            setParam (*p, "compress", c);
            setParam (*p, "attack", 20.0f);
            setParam (*p, "recovery", 2000.0f);
            p->prepareToPlay (fs, 480);
            std::vector<GrBlock> gr;
            const auto out = render (*p, toBuffer (sine (fs, 1000.0, juce::Decibels::decibelsToGain (lvl), (int) (fs * 0.7)), 1 + 1), 480, &gr);
            const int n = 9600;
            outDb.push_back (20.0 * std::log10 (toneAmplitude (out.getReadPointer (0) + out.getNumSamples() - n, n, fs, 1000.0)));
            grDb.push_back (gr.back().l);
            levels.push_back (lvl);
        }
        double prevMeasured = 0;
        bool measuredRises = true;
        for (size_t i = 1; i < outDb.size(); ++i)
        {
            auto lawOut = [&] (float lvl)
            {
                const auto st = inputStageSine (lvl, cClean, 0.0); // the input-stage limit, as in the static-curve test
                return st.fundamentalDb - law::staticGainReductionDb (st.detectorDb, thr, c);
            };
            const double measured = 1.0 / (outDb[i] - outDb[i - 1]);
            const double expected = 1.0 / (lawOut (levels[i]) - lawOut (levels[i - 1]));
            const double g = 0.5 * (grDb[i] + grDb[i - 1]);
            csv << c << "," << g << "," << expected << "," << measured << "\n";
            if (g > 1.0 && expected < 40.0)
            {
                worstRel = std::max (worstRel, std::abs (measured / expected - 1.0));
                measuredRises = measuredRises && measured > prevMeasured * 0.98;
                prevMeasured = measured;
            }
        }
        CHECK_MSG (measuredRises, "measured ratio not rising at COMPRESS " + juce::String (c));
        const double at3 = law::localRatio (thr + overForGr (3.0, c), thr, c), at10 = law::localRatio (thr + overForGr (10.0, c), thr, c);
        MEASURE ("COMPRESS " + juce::String (c) + ": local ratio at 3 dB GR", at3);
        MEASURE ("COMPRESS " + juce::String (c) + ": local ratio at 10 dB GR", at10);
        CHECK (at10 > at3 * 1.3);
    }
    outputDirectory().getChildFile ("VEIL_ratio_growth.csv").replaceWithText (csv);
    MEASURE ("max relative |measured - law| local ratio (GR > 1 dB, ratio < 40)", worstRel);
    CHECK (worstRel < 0.05);

    // Calibration anchors (RESEARCH.md F2): COMPRESS 10 = the hardware's LIMIT,
    // onset 4:1 rising to ~20:1 at 12 dB of GR; COMPRESS 3 ~ its COMPRESS 1.5:1.
    const auto k10 = law::constantsFor (10.0), k3 = law::constantsFor (3.0);
    MEASURE ("COMPRESS 10: onset ratio above the knee (1 + 0.1151 K)", 1.0 + law::ln10Over20 * k10.k);
    MEASURE ("COMPRESS 3: onset ratio above the knee", 1.0 + law::ln10Over20 * k3.k);
    const double r12 = law::localRatio (overForGr (12.0, 10.0), 0.0, 10.0);
    MEASURE ("COMPRESS 10: local ratio at 12 dB GR", r12);
    CHECK_NEAR (1.0 + law::ln10Over20 * k10.k, 4.0, 1e-9);
    CHECK (r12 > 15.0 && r12 < 25.0);
    CHECK (1.0 + law::ln10Over20 * law::constantsFor (0.0).k < 1.3);
}

TEST ("VEIL: attack and recovery: exact time constants of the GR (dB), at several knob values, vs the documented definition (CSV)")
{
    const double fsOs = 96000.0; // model at 48 kHz x 2
    juce::String csv ("kind,setting_ms,fitted_tau_ms,error_pct,t63_measured_ms,t63_reference_ms\n");
    double worstTau = 0, worstRef = 0;
    const double c = 5.0, thr = -18.0, targetGr = 10.0;
    const double level = thr + overForGr (targetGr, c);

    for (float atk : { 10.0f, 30.0f, 100.0f, 300.0f, 1000.0f })
    {
        VeilModel m;
        configureModel (m, c, (float) thr, atk, 2000.0f);
        m.prepare (48000.0, 2);
        const int n = (int) (fsOs * (0.06 + 5.0 * atk * 0.001));
        const auto gr = grTrace (m, alternating (ampForLevel (level), n));
        const double target = m.getTargetGrDb (0); // 10 dB minus the input stage's tiny limit (Clean, drive 0)
        CHECK_NEAR (target, targetGr, 0.05);
        // tail: (T - GR) decays with exactly tau_a once the detector has settled (> 50 ms)
        std::vector<double> rem (gr.size());
        for (size_t i = 0; i < gr.size(); ++i) rem[i] = target - gr[i];
        const int i0 = (int) (0.05 * fsOs), i1 = std::min ((int) gr.size(), i0 + (int) (2.0 * atk * 0.001 * fsOs));
        const double tau = -1.0 / logSlope (rem, std::max (i0, 1), i1, fsOs) * 1000.0;
        const double err = std::abs (tau / atk - 1.0) * 100.0;
        const double amp = ampForLevel (level), vc = veil::tube::inputLimit * veil::tube::voicing (cClean).headroom;
        const auto ref = referenceTrace (amp / std::sqrt (1.0 + (amp / vc) * (amp / vc)), thr, c, atk * 0.001, 2.0, fsOs, n);
        const double t63 = crossing (gr, 0, 0.632 * target, fsOs) * 1000.0, t63r = crossing (ref, 0, 0.632 * target, fsOs) * 1000.0;
        double maxDev = 0;
        for (size_t i = 0; i < gr.size(); ++i) maxDev = std::max (maxDev, std::abs (gr[i] - ref[i]));
        csv << "attack," << atk << "," << tau << "," << err << "," << t63 << "," << t63r << "\n";
        MEASURE ("attack " + juce::String (atk) + " ms: fitted tail tau (ms)", tau);
        MEASURE ("   time to 63 % of the GR, measured / reference (ms)", t63);
        MEASURE ("   reference", t63r);
        MEASURE ("   max |trace - reference ODE| (dB)", maxDev);
        worstTau = std::max (worstTau, err);
        worstRef = std::max (worstRef, maxDev);
    }

    for (float rec : { 50.0f, 150.0f, 400.0f, 1000.0f, 2000.0f })
    {
        VeilModel m;
        configureModel (m, c, (float) thr, 10.0f, rec);
        m.prepare (48000.0, 2);
        const int on = (int) (fsOs * 0.3), off = (int) (fsOs * (0.08 + 3.0 * rec * 0.001));
        const auto gr = grTrace (m, concat (alternating (ampForLevel (level), on), std::vector<float> ((size_t) off, 0.0f)));
        const double g0 = gr[(size_t) on - 1];
        const int i0 = on + (int) (0.06 * fsOs), i1 = on + (int) (0.06 * fsOs + 2.0 * rec * 0.001 * fsOs);
        const double tau = -1.0 / logSlope (gr, i0, std::min (i1, (int) gr.size()), fsOs) * 1000.0;
        const double err = std::abs (tau / rec - 1.0) * 100.0;
        // time from the stop to 1/e of the GR
        double t37 = -1;
        for (size_t i = (size_t) on; i < gr.size(); ++i)
            if (gr[i] <= g0 * std::exp (-1.0)) { t37 = (double) (i - (size_t) on) / fsOs * 1000.0; break; }
        csv << "recovery," << rec << "," << tau << "," << err << "," << t37 << ",\n";
        MEASURE ("recovery " + juce::String (rec) + " ms: fitted tau (ms)", tau);
        MEASURE ("   time from the stop to 1/e of the GR (ms)", t37);
        worstTau = std::max (worstTau, err);
        CHECK_NEAR (g0, targetGr, 0.05);
    }
    outputDirectory().getChildFile ("VEIL_attack_recovery.csv").replaceWithText (csv);
    MEASURE ("worst |fitted tau / setting - 1| (%)", worstTau);
    MEASURE ("worst |attack trace - reference ODE| (dB)", worstRef);
    CHECK (worstTau < 0.5);
    CHECK (worstRef < 0.05);

    // On the processor (1 kHz sine, 2x rate gain trace): recovery 400 ms and attack 100 ms.
    const double fs = 48000.0;
    auto p = makeVeil (fs, 64);
    cleanColour (*p);
    setParam (*p, "threshold", knobFor ((float) thr));
    setParam (*p, "compress", (float) c);
    setParam (*p, "attack", 100.0f);
    setParam (*p, "recovery", 400.0f);
    p->prepareToPlay (fs, 64);
    const int n = (int) (fs * 2.5);
    auto sig = sine (fs, 1000.0, juce::Decibels::decibelsToGain ((float) level), n);
    for (int i = (int) (fs * 1.2); i < n; ++i) sig[(size_t) i] = 0.0f;
    std::vector<float> gl ((size_t) n * 2 + 64), gr2 ((size_t) n * 2 + 64);
    p->getModelForTesting().setGainTraceForTesting (gl.data(), gr2.data(), (int) gl.size());
    render (*p, toBuffer (sig, 2), 64);
    p->getModelForTesting().setGainTraceForTesting (nullptr, nullptr, 0);
    std::vector<double> grDb (gl.size());
    for (size_t i = 0; i < gl.size(); ++i) grDb[i] = -20.0 * std::log10 ((double) gl[i]);
    const int L2 = 2 * p->getLatencySamples(), stop = (int) (fsOs * 1.2) + L2;
    const double tauRec = -1.0 / logSlope (grDb, stop + (int) (0.06 * fsOs), stop + (int) (0.86 * fsOs), fsOs) * 1000.0;
    std::vector<double> rem (grDb.size());
    for (size_t i = 0; i < grDb.size(); ++i) rem[i] = grDb[(size_t) stop - 200] - grDb[i];
    const double tauAtk = -1.0 / logSlope (rem, L2 + (int) (0.06 * fsOs), L2 + (int) (0.3 * fsOs), fsOs) * 1000.0;
    MEASURE ("processor, 1 kHz: recovery 400 ms fitted (ms)", tauRec);
    MEASURE ("processor, 1 kHz: attack 100 ms fitted tail (ms)", tauAtk);
    CHECK_NEAR (tauRec, 400.0, 4.0);
    CHECK_NEAR (tauAtk, 100.0, 2.0);
}

TEST ("VEIL: stereo link vs dual mono: a signal only in L (or R) leaves the other channel at 0 dB GR in dual mono and follows it exactly when linked (CSV)")
{
    const double fs = 48000.0, thr = -24.0, level = -8.0;
    juce::String csv ("mode,driven,gr_left_db,gr_right_db,att_left_db,att_right_db\n");
    for (bool linked : { false, true })
        for (int driven : { 0, 1 })
        {
            auto p = makeVeil (fs, 256);
            cleanColour (*p);
            setParam (*p, "threshold", knobFor ((float) thr));
            setParam (*p, "attack", 30.0f);
            setParam (*p, "recovery", 300.0f);
            setParam (*p, "stereoLink", linked ? 1.0f : 0.0f);
            p->prepareToPlay (fs, 256);
            const int n = (int) (fs * 1.5);
            const auto loud = sine (fs, 1000.0, juce::Decibels::decibelsToGain ((float) level), n);
            const auto quiet = sine (fs, 3000.0, 0.001f, n); // -60 dBFS: far below threshold on its own
            const auto in = driven == 0 ? stereo (loud, quiet) : stereo (quiet, loud);
            std::vector<GrBlock> gr;
            const auto out = render (*p, in, 256, &gr);
            const int L = p->getLatencySamples(), w0 = n - 9600;
            auto att = [&] (int ch, double hz)
            {
                return 20.0 * std::log10 (toneAmplitude (in.getReadPointer (ch) + w0 - L, 9600, fs, hz)
                                          / toneAmplitude (out.getReadPointer (ch) + w0, 9600, fs, hz));
            };
            const double attL = att (0, driven == 0 ? 1000.0 : 3000.0), attR = att (1, driven == 0 ? 3000.0 : 1000.0);
            const auto& last = gr.back();
            const double expected = law::staticGainReductionDb (level, thr, 5.0);
            csv << (linked ? "linked" : "dual_mono") << "," << (driven == 0 ? "L" : "R") << "," << last.l << "," << last.r << "," << attL << "," << attR << "\n";
            const auto tag = juce::String (linked ? "linked" : "dual mono") + ", signal in " + (driven == 0 ? "L" : "R");
            MEASURE (tag + ": GR L (dB)", last.l);
            MEASURE (tag + ": GR R (dB)", last.r);
            const float drivenGr = driven == 0 ? last.l : last.r, otherGr = driven == 0 ? last.r : last.l;
            CHECK_NEAR (drivenGr, expected, 0.05);
            if (linked)
            {
                bool same = true;
                for (auto& b : gr) same = same && b.l == b.r;
                CHECK_MSG (same, tag + ": L and R GR differ while linked");
            }
            else
            {
                float maxOther = 0;
                for (auto& b : gr) maxOther = std::max (maxOther, driven == 0 ? b.r : b.l);
                CHECK_MSG (maxOther == 0.0f, tag + ": the silent side was reduced");
                CHECK (otherGr == 0.0f);
            }
            // the audio carries the telemetry, per channel
            CHECK_NEAR (attL, last.l, 0.05);
            CHECK_NEAR (attR, last.r, 0.05);
        }
    outputDirectory().getChildFile ("VEIL_stereo_link.csv").replaceWithText (csv);

    // Identical channels: link changes nothing.
    auto grFor = [&] (bool linked)
    {
        auto p = makeVeil (fs, 256);
        setParam (*p, "threshold", knobFor ((float) thr));
        setParam (*p, "stereoLink", linked ? 1.0f : 0.0f);
        p->prepareToPlay (fs, 256);
        std::vector<GrBlock> gr;
        const auto out = render (*p, toBuffer (sine (fs, 1000.0, 0.5f, 48000), 2), 256, &gr);
        return std::make_pair (gr.back().l, out);
    };
    const auto a = grFor (false), b = grFor (true);
    CHECK (a.first == b.first);
    CHECK (identical (a.second, b.second));
}

TEST ("VEIL: per-channel GR telemetry equals each channel's actually applied gain (error trace) and the attenuation of the audio")
{
    const double fs = 48000.0;
    const int block = 64, n = (int) (fs * 3.0);
    std::vector<float> l = sine (fs, 1000.0, 1.0f, n), r = sine (fs, 700.0, 1.0f, n);
    const float levels[] = { 0.05f, 0.3f, 0.6f, 0.15f, 0.9f, 0.4f };
    for (int i = 0; i < n; ++i)
    {
        l[(size_t) i] *= levels[(i / (int) (0.5 * fs)) % 6];
        r[(size_t) i] *= (i / (int) (0.11 * fs)) % 3 == 0 ? 0.8f : 0.02f;
    }
    juce::String trace ("block,ch,telemetry_db,applied_max_db,error_db\n");
    double maxErr = 0;
    for (bool linked : { false, true })
    {
        auto p = makeVeil (fs, block);
        setParam (*p, "threshold", knobFor (-26.0f));
        setParam (*p, "compress", 7.0f);
        setParam (*p, "attack", 10.0f);
        setParam (*p, "recovery", 120.0f);
        setParam (*p, "stereoLink", linked ? 1.0f : 0.0f);
        p->prepareToPlay (fs, block);
        std::vector<float> gL ((size_t) block * 2), gR ((size_t) block * 2);
        std::vector<float> histL, histR;
        auto& model = p->getModelForTesting();
        auto io = stereo (l, r);
        juce::MidiBuffer midi;
        for (int pos = 0; pos < n; pos += block)
        {
            model.setGainTraceForTesting (gL.data(), gR.data(), block * 2);
            juce::AudioBuffer<float> view (io.getArrayOfWritePointers(), 2, pos, block);
            p->processBlock (view, midi);
            CHECK (model.getGainTraceCount() == block * 2);
            histL.insert (histL.end(), gL.begin(), gL.end());
            histR.insert (histR.end(), gR.begin(), gR.end());
            for (int ch = 0; ch < 2; ++ch)
            {
                const auto& g = ch == 0 ? gL : gR;
                double deepest = 0;
                for (float x : g) deepest = std::max (deepest, -20.0 * std::log10 ((double) x));
                const float tel = p->takeGrDb (ch);
                const double err = std::abs (tel - deepest);
                maxErr = std::max (maxErr, err);
                if (! linked)
                    trace << pos / block << "," << ch << "," << tel << "," << deepest << "," << err << "\n";
            }
        }
        model.setGainTraceForTesting (nullptr, nullptr, 0);
        double lr = 0;
        for (size_t i = 0; i < histL.size(); ++i)
            lr = std::max (lr, std::abs (20.0 * std::log10 ((double) histL[i] / (double) histR[i])));
        MEASURE (juce::String (linked ? "linked" : "dual mono") + ": max |GR L - GR R| (dB)", lr);
        if (linked) CHECK (lr < 1.0e-6);
        else        CHECK (lr > 3.0);
        // after a read with no new audio, the latest value is reported
        CHECK_NEAR (p->takeGrDb (0), p->getCurrentGrDb (0), 1e-6);
    }
    outputDirectory().getChildFile ("VEIL_gr_telemetry_error_trace.csv").replaceWithText (trace);
    MEASURE ("max |takeGrDb - deepest applied GR of the block| (dB)", maxErr);
    CHECK (maxErr < 1.0e-3);

    // Audio check: in steady windows the attenuation of the fundamental equals
    // the telemetry. The reference is the same chain with MIX 0 (the coloured,
    // uncompressed signal: input stage only, Clean, SATURATION 0), so the
    // input stage's own soft limit is not counted as gain reduction.
    auto renderAt = [&] (float mixPct, std::vector<GrBlock>* gr)
    {
        auto q = makeVeil (fs, block);
        cleanColour (*q);
        setParam (*q, "threshold", knobFor (-26.0f));
        setParam (*q, "attack", 10.0f);
        setParam (*q, "recovery", 120.0f);
        setParam (*q, "stereoLink", 0.0f);
        setParam (*q, "mix", mixPct);
        q->prepareToPlay (fs, block);
        return render (*q, toBuffer (l, 2), block, gr);
    };
    std::vector<GrBlock> gr;
    const auto out = renderAt (100.0f, &gr), ref = renderAt (0.0f, nullptr);
    const int win = 960;
    double maxAudioErr = 0;
    int windows = 0;
    for (int s = 9600; s + win < n; s += win)
    {
        const int b0 = s / block, b1 = (s + win) / block;
        float tMin = 1e9f, tMax = -1e9f;
        double tMean = 0;
        for (int b = b0; b < b1; ++b) { tMin = std::min (tMin, gr[(size_t) b].l); tMax = std::max (tMax, gr[(size_t) b].l); tMean += gr[(size_t) b].l; }
        tMean /= (b1 - b0);
        if (tMax - tMin > 0.02f || tMean < 0.5)
            continue;
        const double att = 20.0 * std::log10 (toneAmplitude (ref.getReadPointer (0) + s, win, fs, 1000.0) / toneAmplitude (out.getReadPointer (0) + s, win, fs, 1000.0));
        maxAudioErr = std::max (maxAudioErr, std::abs (att - tMean));
        ++windows;
    }
    MEASURE ("max |telemetry - attenuation of the audio| (dB), steady windows", maxAudioErr);
    MEASURE ("steady windows compared", windows);
    CHECK (windows >= 20);
    CHECK (maxAudioErr < 0.05);

    // deep GR: never clipped at a meter range
    auto d = makeVeil (fs, 480);
    setParam (*d, "threshold", -20.0f);
    setParam (*d, "compress", 0.0f);
    setParam (*d, "input", 20.0f);
    d->prepareToPlay (fs, 480);
    render (*d, toBuffer (sine (fs, 1000.0, 0.9f, 48000), 2), 480);
    const float deep = d->takeGrDb (0);
    MEASURE ("GR telemetry, 58 dB over threshold at COMPRESS 0 (dB)", deep);
    CHECK (deep > 20.0f);
}

TEST ("VEIL: METER mode only changes what the editor shows: the audio is bit-identical in GR, VU and In/Out")
{
    const double fs = 48000.0;
    auto renderMode = [&] (int mode)
    {
        auto p = makeVeil (fs, 128);
        setParam (*p, "threshold", knobFor (-26.0f));
        setParam (*p, "stereoLink", 0.0f);
        setParam (*p, "meterMode", (float) mode);
        p->prepareToPlay (fs, 128);
        juce::Random rng (5);
        juce::AudioBuffer<float> in (2, 48000);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < in.getNumSamples(); ++i)
                in.setSample (ch, i, (0.5f + 0.4f * (float) std::sin (i * 0.0007 + ch)) * (rng.nextFloat() - 0.5f));
        auto out = render (*p, in, 128);
        CHECK ((int) p->getMeterMode() == mode);
        return out;
    };
    const auto a = renderMode (mGr), b = renderMode (mVu), c = renderMode (mInOut);
    CHECK_MSG (identical (a, b) && identical (a, c), "METER mode altered the audio");

    // The meter sources: per channel input / output RMS, VU calibration (0 VU = -18 dBFS sine).
    auto p = makeVeil (fs, 256);
    setParam (*p, "bypass", 1.0f);
    p->prepareToPlay (fs, 256);
    render (*p, stereo (sine (fs, 1000.0, juce::Decibels::decibelsToGain (-18.0f), 48000), sine (fs, 1000.0, juce::Decibels::decibelsToGain (-24.0f), 48000)), 256);
    const float vuInL = vp::vuDb (p->inputMeters[0].takeRms()), vuInR = vp::vuDb (p->inputMeters[1].takeRms());
    const float vuOutL = vp::vuDb (p->outputMeters[0].takeRms());
    MEASURE ("input meter L, -18 dBFS sine (VU dB)", vuInL);
    MEASURE ("input meter R, -24 dBFS sine (VU dB)", vuInR);
    CHECK_NEAR (vuInL, 0.0f, 0.02);
    CHECK_NEAR (vuInR, -6.0f, 0.02);
    CHECK_NEAR (vuOutL, 0.0f, 0.05);
}

TEST ("VEIL: SC HPF: sweep matches hpfResponseDb (min, mid, max, musical); Off bypasses; the detector sees it; it reduces pumping (CSV)")
{
    juce::String csv ("rate,cutoff_hz,freq_hz,measured_db,function_db\n");
    for (double fs : { 88200.0, 96000.0, 384000.0 }) // the detector runs at 2x the host rate
        for (float hz : { 25.0f, 60.0f, 80.0f, 150.0f, 300.0f })
        {
            veil::HighPassControl ctl;
            ctl.prepare (fs);
            ctl.setTarget (hz);
            ctl.snap();
            double maxErr = 0;
            for (double f = 10.0; f <= 4000.0; f *= std::pow (2.0, 1.0 / 6.0))
            {
                veil::HighPassState s;
                const int settle = (int) (0.3 * fs), n = (int) (std::max (0.1, 20.0 / f) * fs);
                std::vector<double> y ((size_t) n);
                const double w = 2.0 * pi * f / fs;
                for (int i = 0; i < settle + n; ++i)
                {
                    ctl.advance();
                    const double out = s.process (0.25 * std::sin (w * i), ctl);
                    if (i >= settle) y[(size_t) (i - settle)] = out;
                }
                const double measured = 20.0 * std::log10 (sineFit (y.data(), n, fs, f).amp / 0.25);
                const double expected = veil::hpfResponseDb (f, hz, fs);
                if (expected > -60.0)
                    maxErr = std::max (maxErr, std::abs (measured - expected));
                if (fs == 96000.0)
                    csv << fs << "," << hz << "," << f << "," << measured << "," << expected << "\n";
            }
            MEASURE ("SC HPF " + juce::String (hz) + " Hz @" + juce::String (fs) + ": max |measured - hpfResponseDb| (dB)", maxErr);
            CHECK (maxErr < 0.01);
            CHECK_NEAR (veil::hpfResponseDb (hz, hz, fs), -3.0103, 0.05); // Butterworth: -3 dB at the cutoff
        }
    outputDirectory().getChildFile ("VEIL_sc_hpf_sweep.csv").replaceWithText (csv);

    // Off: bit-transparent (the filter is not computed).
    {
        veil::HighPassControl ctl;
        ctl.prepare (96000.0);
        ctl.setTarget (20.0);
        ctl.snap();
        veil::HighPassState s;
        bool same = true;
        juce::Random rng (9);
        for (int i = 0; i < 10000; ++i)
        {
            ctl.advance();
            const double x = rng.nextDouble() - 0.5;
            same = same && s.process (x, ctl) == x;
        }
        CHECK (same);
        CHECK (veil::hpfResponseDb (5.0, 20.0, 96000.0) == 0.0);
    }

    // The processor's detector sees exactly the filter: the GR for a low tone
    // with the SC HPF at f equals the GR with the SC HPF Off for the same tone
    // at (level + hpfResponseDb). Comparing at the same frequency cancels the
    // rectifier ripple of low tones, which the attack follows to its peaks
    // (MODEL_SPEC.md 2.4); the ripple itself is measured against the static law.
    const double fs = 48000.0;
    auto grFor = [&] (float hz, double tone, double levelDb)
    {
        auto p = makeVeil (fs, 256);
        cleanColour (*p);
        setParam (*p, "threshold", knobFor (-30.0f));
        setParam (*p, "attack", 100.0f);
        setParam (*p, "recovery", 2000.0f);
        setParam (*p, "scHpf", hz);
        p->prepareToPlay (fs, 256);
        std::vector<GrBlock> gr;
        render (*p, toBuffer (fadedTone (fs, tone, juce::Decibels::decibelsToGain ((float) levelDb), (int) (2.0 * fs)), 2), 256, &gr);
        return (double) gr.back().l;
    };
    for (double tone : { 50.0, 120.0 })
    {
        const double off = grFor (20.0f, tone, -10.0), law0 = law::staticGainReductionDb (-10.0, -30.0, 5.0);
        MEASURE ("SC HPF Off, " + juce::String (tone) + " Hz tone: GR - static law (dB, rectifier ripple)", off - law0);
        CHECK (off - law0 > -0.05 && off - law0 < (tone < 60.0 ? 1.5 : 0.4));
    }
    for (float hz : { 60.0f, 80.0f, 150.0f, 300.0f })
        for (double tone : { 50.0, 120.0 })
        {
            const double resp = veil::hpfResponseDb (tone, hz, 2.0 * fs);
            const double got = grFor (hz, tone, -10.0), expected = grFor (20.0f, tone, -10.0 + resp);
            MEASURE ("SC HPF " + juce::String (hz) + ", " + juce::String (tone) + " Hz tone: GR / Off at level + response (dB)", got);
            MEASURE ("   expected", expected);
            CHECK_NEAR (got, expected, 0.05);
        }

    // Pumping: 50 Hz kick-like bursts under a sustained 1 kHz tone.
    const int n = (int) (fs * 4.0);
    std::vector<float> sig = sine (fs, 1000.0, juce::Decibels::decibelsToGain (-18.0f), n);
    for (int k = 0; k < 10; ++k)
    {
        const int start = (int) (fs * (0.2 + 0.4 * k));
        for (int i = 0; i < (int) (0.25 * fs) && start + i < n; ++i)
        {
            const double t = i / fs;
            sig[(size_t) (start + i)] += (float) (0.7 * std::exp (-t / 0.06) * std::sin (2.0 * pi * 50.0 * t));
        }
    }
    auto pumping = [&] (float hpf, double& grSwing)
    {
        auto p = makeVeil (fs, 64);
        cleanColour (*p);
        setParam (*p, "threshold", knobFor (-22.0f));
        setParam (*p, "attack", 10.0f);
        setParam (*p, "recovery", 100.0f);
        setParam (*p, "scHpf", hpf);
        p->prepareToPlay (fs, 64);
        std::vector<GrBlock> gr;
        const auto out = render (*p, toBuffer (sig, 2), 64, &gr);
        const int win = (int) (0.02 * fs);
        double lo = 1e9, hi = -1e9;
        for (int s = (int) (0.6 * fs); s + win < n; s += win / 2)
        {
            const double level = 20.0 * std::log10 (toneAmplitude (out.getReadPointer (0) + s, win, fs, 1000.0));
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
    const double swingOff = pumping (20.0f, grOff), swingOn = pumping (300.0f, grOn);
    MEASURE ("1 kHz tone level swing, SC HPF Off (dB)", swingOff);
    MEASURE ("1 kHz tone level swing, SC HPF 300 Hz (dB)", swingOn);
    MEASURE ("GR swing Off / 300 Hz (dB)", grOff);
    MEASURE ("   300 Hz", grOn);
    MEASURE ("pumping reduction factor", swingOff / std::max (swingOn, 1.0e-3));
    CHECK (swingOff > 4.0);
    CHECK (swingOn < swingOff / 4.0);
    CHECK (grOn < grOff / 4.0);
}

TEST ("VEIL: HPF (audio path): sweep matches hpfResponseDb; Off is bit-transparent; it feeds the internal detector but not the external key (CSV)")
{
    juce::String csv ("rate,cutoff_hz,freq_hz,measured_db,function_db\n");
    // the filter itself, at host rates
    for (double fs : { 44100.0, 48000.0, 192000.0 })
        for (float hz : { 25.0f, 100.0f, 300.0f })
        {
            veil::HighPassControl ctl;
            ctl.prepare (fs);
            ctl.setTarget (hz);
            ctl.snap();
            double maxErr = 0;
            for (double f = 10.0; f <= 4000.0; f *= std::pow (2.0, 1.0 / 6.0))
            {
                veil::HighPassState s;
                const int settle = (int) (0.3 * fs), n = (int) (std::max (0.1, 20.0 / f) * fs);
                std::vector<double> y ((size_t) n);
                const double w = 2.0 * pi * f / fs;
                for (int i = 0; i < settle + n; ++i)
                {
                    ctl.advance();
                    const double out = s.process (0.25 * std::sin (w * i), ctl);
                    if (i >= settle) y[(size_t) (i - settle)] = out;
                }
                const double measured = 20.0 * std::log10 (sineFit (y.data(), n, fs, f).amp / 0.25);
                const double expected = veil::hpfResponseDb (f, hz, fs);
                if (expected > -60.0)
                    maxErr = std::max (maxErr, std::abs (measured - expected));
            }
            MEASURE ("HPF " + juce::String (hz) + " Hz @" + juce::String (fs) + ": max |measured - hpfResponseDb| (dB)", maxErr);
            CHECK (maxErr < 0.01);
        }

    // through the processor: measured sweep = HPF + coupling (2 Hz) response (Clean, no GR)
    const double fs = 48000.0;
    for (float hz : { 20.0f, 60.0f, 100.0f, 300.0f })
    {
        auto p = makeVeil (fs, 512);
        cleanColour (*p);
        setParam (*p, "threshold", 20.0f);
        setParam (*p, "hpf", hz);
        p->prepareToPlay (fs, 512);
        double maxErr = 0;
        for (double f = 20.0; f <= 16000.0; f *= std::pow (2.0, 1.0 / 3.0))
        {
            const int n = (int) (fs * 1.0);
            const auto in = toBuffer (sine (fs, f, 0.1f, n), 2);
            const auto out = render (*p, in, 512);
            const int m = (int) (0.4 * fs);
            std::vector<double> y ((size_t) m);
            for (int i = 0; i < m; ++i) y[(size_t) i] = out.getSample (0, n - m + i);
            const double measured = 20.0 * std::log10 (sineFit (y.data(), m, fs, f).amp / 0.1);
            const double expected = veil::hpfResponseDb (f, hz, p->getHostSampleRate())
                                  + veil::filt::toDb (1.0 - veil::filt::onePoleLowpass (veil::filt::integratorGain (veil::filt::couplingHz, fs), veil::filt::zInv (f, fs)));
            if (expected > -50.0)
                maxErr = std::max (maxErr, std::abs (measured - expected));
            csv << fs << "," << hz << "," << f << "," << measured << "," << expected << "\n";
        }
        MEASURE ("processor HPF " + juce::String (hz) + " Hz: max |measured - (hpfResponseDb + coupling)| (dB)", maxErr);
        CHECK (maxErr < 0.05);
    }
    outputDirectory().getChildFile ("VEIL_hpf_sweep.csv").replaceWithText (csv);

    // Off is bit-transparent relative to the same chain without the filter:
    // preProcess (HPF Off + coupling) == the coupling high-pass alone.
    {
        VeilModel m;
        m.setHpf (20.0f);
        m.prepare (48000.0, 2);
        juce::Random rng (4);
        std::vector<float> x (48000), ref (48000);
        for (size_t i = 0; i < x.size(); ++i) x[i] = ref[i] = rng.nextFloat() - 0.5f;
        veil::OnePole coupling;
        const double cc = veil::OnePole::coeff (veil::filt::couplingHz, 48000.0);
        for (auto& v : ref) v = (float) coupling.highpass ((double) v, cc);
        float* ch[] = { x.data() };
        m.preProcess (ch, 1, (int) x.size());
        bool same = true;
        for (size_t i = 0; i < x.size(); ++i) same = same && sameBits (x[i], ref[i]);
        CHECK_MSG (same, "HPF Off is not bit-transparent");
        // ... and after being switched on and off again (faded out, not computed)
        m.setHpf (150.0f);
        m.preProcess (ch, 1, 4800);
        m.setHpf (20.0f);
        m.preProcess (ch, 1, 4800);
        CHECK (! m.getHpfControl().isActive());
    }

    // The internal detector sees the high-passed signal; the external key does not.
    auto grWith = [&] (float hpf, bool external)
    {
        auto p = makeVeil (fs, 256, 2, external ? 2 : 0);
        cleanColour (*p);
        setParam (*p, "threshold", knobFor (-30.0f));
        setParam (*p, "scHpf", 20.0f);
        setParam (*p, "hpf", hpf);
        setParam (*p, "scExternal", external ? 1.0f : 0.0f);
        p->prepareToPlay (fs, 256);
        const auto tone = toBuffer (fadedTone (fs, 40.0, juce::Decibels::decibelsToGain (-10.0f), (int) (1.5 * fs)), 2);
        std::vector<GrBlock> gr;
        if (external)
            renderKeyed (*p, toBuffer (sine (fs, 500.0, 0.01f, (int) (1.5 * fs)), 2), &tone, 256, &gr);
        else
            render (*p, tone, 256, &gr);
        return gr.back().l;
    };
    const float intOff = grWith (20.0f, false), intOn = grWith (300.0f, false);
    const float extOff = grWith (20.0f, true), extOn = grWith (300.0f, true);
    MEASURE ("40 Hz internal: GR with HPF Off / 300 Hz (dB)", intOff);
    MEASURE ("   300 Hz", intOn);
    MEASURE ("40 Hz external key: GR with HPF Off / 300 Hz (dB)", extOff);
    MEASURE ("    300 Hz", extOn);
    CHECK (intOff > 5.0f && intOn < 0.01f);
    CHECK_NEAR (extOn, extOff, 0.02);
}

TEST ("VEIL: sidechain: internal detection ignores an enabled key; external with a silent key gives no GR")
{
    const double fs = 48000.0;
    const auto main = toBuffer (sine (fs, 500.0, 0.5f, 24000), 2);
    auto loudKey = toBuffer (sine (fs, 200.0, 0.9f, 24000), 2);
    juce::AudioBuffer<float> silentKey (2, 24000);
    silentKey.clear();
    auto setup = [&] (int keyChannels, bool external)
    {
        auto p = makeVeil (fs, 256, 2, keyChannels);
        setParam (*p, "threshold", knobFor (-20.0f));
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
    MEASURE ("external, silent key: max GR (dB)", maxGr);
    CHECK (maxGr < 1.0e-6f);
    std::vector<GrBlock> grInt;
    render (*setup (0, false), main, 256, &grInt);
    MEASURE ("internal: GR for the same main input (dB)", grInt.back().l);
    CHECK (grInt.back().l > 3.0f);
}

TEST ("VEIL: sidechain: a sine key follows the static law; a transient key ducks with the documented timing")
{
    const double fs = 48000.0;
    for (float c : { 2.0f, 5.0f, 9.0f })
    {
        auto p = makeVeil (fs, 64, 2, 2);
        cleanColour (*p);
        setParam (*p, "threshold", knobFor (-20.0f));
        setParam (*p, "compress", c);
        setParam (*p, "attack", 20.0f);
        setParam (*p, "recovery", 100.0f);
        setParam (*p, "scHpf", 20.0f);
        setParam (*p, "scExternal", 1.0f);
        p->prepareToPlay (fs, 64);
        const auto main = toBuffer (sine (fs, 300.0, juce::Decibels::decibelsToGain (-30.0f), 48000), 2);
        const auto key = toBuffer (sine (fs, 1000.0, juce::Decibels::decibelsToGain (-8.0f), 48000), 2);
        std::vector<GrBlock> gr;
        const auto out = renderKeyed (*p, main, &key, 64, &gr);
        const double lawGr = law::staticGainReductionDb (-8.0, -20.0, c);
        const int L = p->getLatencySamples();
        const double att = 20.0 * std::log10 (toneAmplitude (main.getReadPointer (0) + 36000 - L, 9600, fs, 300.0)
                                              / toneAmplitude (out.getReadPointer (0) + 36000, 9600, fs, 300.0));
        MEASURE ("sine key -8 dBFS, COMPRESS " + juce::String (c) + ": GR / law (dB)", gr.back().l);
        MEASURE ("   law", lawGr);
        MEASURE ("   attenuation of the main signal (dB)", att);
        CHECK_NEAR (gr.back().l, lawGr, 0.05);
        CHECK_NEAR (gr.back().r, lawGr, 0.05);
        CHECK_NEAR (att, lawGr, 0.05);
    }

    // transient key: 50 ms bursts every 500 ms; the main tone ducks and recovers with RECOVERY.
    auto q = makeVeil (fs, 64, 2, 2);
    cleanColour (*q);
    setParam (*q, "threshold", knobFor (-20.0f));
    setParam (*q, "attack", 10.0f);
    setParam (*q, "recovery", 200.0f);
    setParam (*q, "compress", 10.0f);
    setParam (*q, "scHpf", 20.0f);
    setParam (*q, "scExternal", 1.0f);
    q->prepareToPlay (fs, 64);
    std::vector<float> k ((size_t) (fs * 2.0), 0.0f);
    for (int b = 0; b < 4; ++b)
        for (int i = 0; i < (int) (0.05 * fs); ++i)
            k[(size_t) ((int) (fs * (0.1 + 0.5 * b)) + i)] = 0.5f * (float) std::sin (2.0 * pi * 1000.0 * i / fs);
    std::vector<GrBlock> grT;
    const auto burstKey = toBuffer (k, 2);
    renderKeyed (*q, toBuffer (sine (fs, 300.0, 0.05f, (int) (fs * 2.0)), 2), &burstKey, 64, &grT);
    const int burstEnd = (int) (fs * 0.15) / 64;
    const double peak = grT[(size_t) burstEnd + 1].l;
    const double after = grT[(size_t) (burstEnd + 1 + (int) (0.2 * fs) / 64)].l;
    MEASURE ("transient key: GR at the end of a burst (dB)", peak);
    MEASURE ("transient key: GR one recovery time later (dB)", after);
    MEASURE ("   ratio (e^-1 = 0.368 expected, plus the rectifier's own decay)", after / peak);
    CHECK (peak > 0.95 * law::staticGainReductionDb (20.0 * std::log10 (0.5), -20.0, 10.0) * (1.0 - std::exp (-5.0)));
    CHECK (after / peak > 0.36 && after / peak < 0.42);
    CHECK (grT[(size_t) ((int) (fs * 0.08) / 64)].l < 1.0e-6f); // nothing before the first burst
}

TEST ("VEIL: sidechain: stereo key channel mapping (key L drives L, key R drives R; linked = both); a mono key drives both")
{
    const double fs = 48000.0;
    const auto main = toBuffer (sine (fs, 300.0, 0.01f, 48000), 2);
    const auto tone = sine (fs, 1000.0, juce::Decibels::decibelsToGain (-6.0f), 48000);
    const std::vector<float> zero (tone.size(), 0.0f);
    auto grWith = [&] (const juce::AudioBuffer<float>& key, bool linked, int mainChannels = 2)
    {
        auto p = makeVeil (fs, 128, mainChannels, key.getNumChannels());
        setParam (*p, "threshold", knobFor (-20.0f));
        setParam (*p, "attack", 20.0f);
        setParam (*p, "recovery", 200.0f);
        setParam (*p, "scHpf", 20.0f);
        setParam (*p, "stereoLink", linked ? 1.0f : 0.0f);
        setParam (*p, "scExternal", 1.0f);
        p->prepareToPlay (fs, 128);
        std::vector<GrBlock> gr;
        renderKeyed (*p, mainChannels == 2 ? main : toBuffer (sine (fs, 300.0, 0.01f, 48000), 1), &key, 128, &gr);
        CHECK (p->isUsingExternalSidechain());
        return gr.back();
    };
    const double c = law::staticGainReductionDb (-6.0, -20.0, 5.0);
    const auto lOnly = grWith (stereo (tone, zero), false), rOnly = grWith (stereo (zero, tone), false);
    const auto lLinked = grWith (stereo (tone, zero), true), monoKey = grWith (toBuffer (tone, 1), false);
    MEASURE ("key L only, dual mono: GR L / R (dB)", lOnly.l);
    MEASURE ("   R", lOnly.r);
    MEASURE ("key R only, dual mono: GR L / R (dB)", rOnly.l);
    MEASURE ("   R ", rOnly.r);
    MEASURE ("key L only, linked: GR L / R (dB)", lLinked.l);
    MEASURE ("   R  ", lLinked.r);
    CHECK_NEAR (lOnly.l, c, 0.05);
    CHECK (lOnly.r == 0.0f);
    CHECK (rOnly.l == 0.0f);
    CHECK_NEAR (rOnly.r, c, 0.05);
    CHECK_NEAR (lLinked.l, c, 0.05);
    CHECK (lLinked.r == lLinked.l);
    CHECK_NEAR (monoKey.l, c, 0.05);
    CHECK_NEAR (monoKey.r, c, 0.05);

    // mono main with a stereo key: the mean of both rectified key channels (L only: -6 dB)
    const auto monoMain = grWith (stereo (tone, zero), false, 1);
    MEASURE ("mono main, stereo key L only: GR (dB)", monoMain.l);
    CHECK_NEAR (monoMain.l, law::staticGainReductionDb (-6.0 - 6.0206, -20.0, 5.0), 0.05);
    CHECK (monoMain.r == monoMain.l);
}

TEST ("VEIL: sidechain: EXT with the bus disabled falls back to the internal key; switching is click-free; the key never leaks; SC HPF acts on the key")
{
    const double fs = 48000.0;
    const auto main = toBuffer (sine (fs, 700.0, 0.5f, 24000), 2);
    auto run = [&] (int keyChannels, bool external)
    {
        auto p = makeVeil (fs, 256, 2, keyChannels);
        setParam (*p, "threshold", knobFor (-20.0f));
        setParam (*p, "scExternal", external ? 1.0f : 0.0f);
        p->prepareToPlay (fs, 256);
        auto key = toBuffer (sine (fs, 100.0, 0.9f, 24000), std::max (1, keyChannels));
        auto out = renderKeyed (*p, main, keyChannels > 0 ? &key : nullptr, 256);
        return std::make_pair (out, p->isUsingExternalSidechain());
    };
    const auto internalRef = run (0, false), fallback = run (0, true);
    CHECK (! internalRef.second && ! fallback.second);
    CHECK_MSG (identical (internalRef.first, fallback.first), "EXT without a bus is not the internal detector");

    // toggling INT/EXT under signal: no click
    auto p = makeVeil (fs, 64, 2, 2);
    setParam (*p, "threshold", knobFor (-20.0f));
    setParam (*p, "attack", 10.0f);
    setParam (*p, "recovery", 100.0f);
    p->prepareToPlay (fs, 64);
    juce::MidiBuffer midi;
    float prev = 0.0f, maxJump = 0.0f;
    bool sawExternal = false;
    for (int b = 0; b < 1500; ++b)
    {
        if (b % 100 == 50)
            setParam (*p, "scExternal", plainValue (*p, "scExternal") > 0.5f ? 0.0f : 1.0f);
        juce::AudioBuffer<float> io (4, 64);
        const auto m = sine (fs, 200.0, 0.3f, 64, 2.0 * pi * 200.0 * b * 64 / fs);
        const auto k = sine (fs, 90.0, 0.9f, 64, 2.0 * pi * 90.0 * b * 64 / fs);
        for (int ch = 0; ch < 2; ++ch) { io.copyFrom (ch, 0, m.data(), 64); io.copyFrom (2 + ch, 0, k.data(), 64); }
        p->processBlock (io, midi);
        sawExternal = sawExternal || p->isUsingExternalSidechain();
        for (int i = 0; i < 64; ++i)
        {
            maxJump = std::max (maxJump, std::abs (io.getSample (0, i) - prev));
            prev = io.getSample (0, i);
        }
    }
    MEASURE ("max sample jump while toggling INT/EXT", maxJump);
    CHECK (sawExternal);
    CHECK (maxJump < 0.05f);

    // no leakage: silent main + loud key = silence; a key tone never appears in the output
    auto q = makeVeil (fs, 128, 2, 2);
    setParam (*q, "threshold", knobFor (-20.0f));
    setParam (*q, "character", (float) cThick);
    setParam (*q, "output", 20.0f);
    setParam (*q, "scExternal", 1.0f);
    q->prepareToPlay (fs, 128);
    juce::AudioBuffer<float> silent (2, 48000);
    silent.clear();
    const auto key = toBuffer (sine (fs, 1000.0, 0.9f, 48000), 2);
    const auto out = renderKeyed (*q, silent, &key, 128);
    MEASURE ("silent main, loud key: output peak", out.getMagnitude (0, out.getNumSamples()));
    CHECK (out.getMagnitude (0, out.getNumSamples()) < 1.0e-7f);
    auto r = makeVeil (fs, 128, 2, 2);
    setParam (*r, "threshold", 20.0f);
    setParam (*r, "scExternal", 1.0f);
    r->prepareToPlay (fs, 128);
    const auto longKey = toBuffer (sine (fs, 1000.0, 0.9f, 96000), 2); // as long as the main input
    const auto o2 = renderKeyed (*r, toBuffer (sine (fs, 300.0, 0.5f, 96000), 2), &longKey, 128);
    const double leak = toneAmplitude (o2.getReadPointer (0) + 48000, 48000, fs, 1000.0);
    MEASURE ("key-frequency component in the output (dBFS)", dbc (leak));
    CHECK (leak < 1.0e-6);

    // SC HPF acts on the external key
    auto grFor = [&] (float hpf)
    {
        auto s = makeVeil (fs, 256, 2, 2);
        setParam (*s, "threshold", knobFor (-30.0f));
        setParam (*s, "attack", 100.0f);
        setParam (*s, "scHpf", hpf);
        setParam (*s, "scExternal", 1.0f);
        s->prepareToPlay (fs, 256);
        const auto keyTone = toBuffer (fadedTone (fs, 60.0, juce::Decibels::decibelsToGain (-10.0f), (int) (1.5 * fs)), 2);
        std::vector<GrBlock> gr;
        renderKeyed (*s, toBuffer (sine (fs, 500.0, 0.01f, (int) (1.5 * fs)), 2), &keyTone, 256, &gr);
        return gr.back().l;
    };
    const float off = grFor (20.0f), on = grFor (300.0f);
    MEASURE ("60 Hz key: GR with SC HPF Off / 300 Hz (dB)", off);
    MEASURE ("   300 Hz", on);
    CHECK (off > 5.0f && on < off - 5.0f);
}

TEST ("VEIL: harmonic spectrum: CHARACTER, level dependence and the gain element's GR-dependent colour (THD tables, CSV)")
{
    juce::String csv ("table,setting,level_dbfs,thd_pct,h2_dbc,h3_dbc,h5_dbc,gr_db\n");
    const float levels[] = { -30.0f, -18.0f, -12.0f, -6.0f, 0.0f };
    double thdAt[3][5] {};
    for (int c = 0; c < 3; ++c)
        for (int li = 0; li < 5; ++li)
        {
            Colour col;
            col.character = c;
            const auto r = renderTone (col, levels[li]);
            thdAt[c][li] = r.thd;
            csv << "character," << vp::characterNames[c] << "," << levels[li] << "," << pct (r.thd) << "," << dbc (r.h2) << "," << dbc (r.h3) << "," << dbc (r.h5) << "," << r.grDb << "\n";
            MEASURE (vp::characterNames[c] + " (defaults 28/42/18) THD % at " + juce::String (levels[li]) + " dBFS", pct (r.thd));
        }
    for (int li = 0; li < 5; ++li)
        CHECK_MSG (thdAt[0][li] < thdAt[1][li] && thdAt[1][li] < thdAt[2][li], "Clean < Veil < Thick fails at " + juce::String (levels[li]) + " dBFS");
    for (int c = 0; c < 3; ++c)
        for (int li = 1; li < 5; ++li)
            CHECK_MSG (thdAt[c][li] > thdAt[c][li - 1], "colour does not rise with level: " + vp::characterNames[c]);
    // calibration anchor: the hardware's < 0.1 % THD at the nominal level (RESEARCH F6): Veil at 0 VU
    CHECK (thdAt[1][1] < 0.002);
    CHECK (thdAt[0][1] < 0.0005);
    CHECK (thdAt[2][4] < 0.08); // bounded: Thick at 0 dBFS with the default colour

    // The gain element: colour grows with the GR (input level fixed, threshold lowered).
    double prev = -1;
    for (float thrKnob : { 20.0f, 4.0f, -2.0f, -8.0f, -14.0f })
    {
        Colour col;
        col.drive = 0.0f;
        col.saturation = 0.0f;
        col.thresholdKnob = thrKnob;
        const auto r = renderTone (col, -6.0f);
        csv << "element_gr,thr_knob " << thrKnob << ",-6," << pct (r.thd) << "," << dbc (r.h2) << "," << dbc (r.h3) << "," << dbc (r.h5) << "," << r.grDb << "\n";
        MEASURE ("element: GR " + juce::String (r.grDb, 1) + " dB: THD %", pct (r.thd));
        CHECK (r.thd > prev);
        prev = r.thd;
    }

    // The CHARACTER's LF weight (output transformer voicing), small-signal.
    const double fs = 48000.0;
    for (int c = 0; c < 3; ++c)
    {
        auto p = makeVeil (fs, 512);
        setParam (*p, "character", (float) c);
        setParam (*p, "threshold", 20.0f);
        p->prepareToPlay (fs, 512);
        auto gainAt = [&] (double hz)
        {
            const auto out = render (*p, toBuffer (sine (fs, hz, 0.05f, 48000), 2), 512);
            return 20.0 * std::log10 (toneAmplitude (out.getReadPointer (0) + 24000, 24000, fs, hz) / 0.05);
        };
        const double lf = gainAt (30.0) - gainAt (1000.0);
        const double expected = veil::shelfResponseDb (30.0, veil::tube::voicing (c).weightDb, veil::filt::weightShelfHz, fs)
                              - veil::shelfResponseDb (1000.0, veil::tube::voicing (c).weightDb, veil::filt::weightShelfHz, fs)
                              + veil::filt::toDb (1.0 - veil::filt::onePoleLowpass (veil::filt::integratorGain (veil::filt::couplingHz, fs), veil::filt::zInv (30.0, fs)));
        MEASURE (vp::characterNames[c] + ": LF weight, 30 Hz re 1 kHz (dB)", lf);
        CHECK_NEAR (lf, expected, 0.05);
    }
    outputDirectory().getChildFile ("VEIL_thd_character.csv").replaceWithText (csv);
}

TEST ("VEIL: TUBE DRIVE, HARMONICS and SATURATION each do their documented job and not the others' (THD tables, CSV)")
{
    juce::String csv ("control,value,level_dbfs,thd_pct,h2_dbc,h3_dbc,h5_dbc,fund_change_db,gr_db\n");
    auto row = [&] (const char* control, float value, float level, const ToneResult& r)
    {
        csv << control << "," << value << "," << level << "," << pct (r.thd) << "," << dbc (r.h2) << "," << dbc (r.h3) << "," << dbc (r.h5) << "," << r.fundDb << "," << r.grDb << "\n";
    };

    // TUBE DRIVE: level into the input stage. THD rises monotonically with drive;
    // the THD-vs-level curve shifts by the drive in dB (THD(L, +12 dB drive) = THD(L + 12, 0));
    // loudness compensated (small-signal level unchanged).
    double prev = -1;
    for (float d : { 0.0f, 25.0f, 50.0f, 75.0f, 100.0f })
    {
        Colour col;
        col.drive = d;
        col.saturation = 0.0f;
        const auto r = renderTone (col, -18.0f);
        row ("tubeDrive", d, -18.0f, r);
        MEASURE ("TUBE DRIVE " + juce::String (d) + " %: THD % at -18 dBFS", pct (r.thd));
        CHECK (r.thd > prev);
        prev = r.thd;
        const auto quiet = renderTone (col, -40.0f);
        MEASURE ("   fundamental level change at -40 dBFS (dB)", quiet.fundDb);
        CHECK (std::abs (quiet.fundDb) < 0.05);
    }
    {
        Colour a, b;
        a.drive = 50.0f; a.saturation = 0.0f;
        b.drive = 0.0f;  b.saturation = 0.0f;
        const auto ra = renderTone (a, -18.0f), rb = renderTone (b, -6.0f);
        MEASURE ("THD at -18 dBFS, drive +12 dB vs -6 dBFS, drive 0 (dB difference)", dbc (ra.thd) - dbc (rb.thd));
        CHECK_NEAR (dbc (ra.thd), dbc (rb.thd), 0.3);
        // drive moves the balance toward the 3rd: H3/H2 rises with drive
        Colour lo, hi;
        lo.drive = 0.0f; lo.saturation = 0.0f;
        hi.drive = 100.0f; hi.saturation = 0.0f;
        const auto rl = renderTone (lo, -18.0f), rh = renderTone (hi, -18.0f);
        MEASURE ("H3/H2 (dB) at drive 0 / 100 %", dbc (rl.h3) - dbc (rl.h2));
        MEASURE ("   100 %", dbc (rh.h3) - dbc (rh.h2));
        CHECK (dbc (rh.h3) - dbc (rh.h2) > dbc (rl.h3) - dbc (rl.h2) + 6.0);
    }

    // HARMONICS: the strength of the 2nd/3rd-order terms. H2 is proportional
    // to HARMONICS (+6 dB per doubling), zero at 0 %; no level change.
    double h2At[4] {};
    const float hs[] = { 0.0f, 21.0f, 42.0f, 84.0f };
    for (int i = 0; i < 4; ++i)
    {
        Colour col;
        col.harmonics = hs[i];
        const auto r = renderTone (col, -12.0f);
        h2At[i] = r.h2;
        row ("harmonics", hs[i], -12.0f, r);
        MEASURE ("HARMONICS " + juce::String (hs[i]) + " %: H2 (dBc) at -12 dBFS", dbc (r.h2));
        MEASURE ("   THD %", pct (r.thd));
        MEASURE ("   fundamental change (dB)", r.fundDb);
        CHECK (std::abs (r.fundDb) < 0.15);
    }
    CHECK (h2At[0] < 1.0e-6);
    CHECK_NEAR (dbc (h2At[2]) - dbc (h2At[1]), 6.02, 0.3);
    CHECK_NEAR (dbc (h2At[3]) - dbc (h2At[2]), 6.02, 0.3);

    // SATURATION: output-stage depth. Odd orders only (H2 unchanged), H3 and
    // H5 rise monotonically; nothing at low level; 0 % bypasses the stage.
    double prevH3 = -1;
    ToneResult s0 {};
    for (float s : { 0.0f, 25.0f, 50.0f, 75.0f, 100.0f })
    {
        Colour col;
        col.saturation = s;
        const auto r = renderTone (col, -6.0f);
        if (s == 0.0f) s0 = r;
        row ("saturation", s, -6.0f, r);
        MEASURE ("SATURATION " + juce::String (s) + " %: H3 (dBc) at -6 dBFS", dbc (r.h3));
        MEASURE ("   H2 (dBc)", dbc (r.h2));
        CHECK (r.h3 > prevH3);
        prevH3 = r.h3;
        CHECK_NEAR (dbc (r.h2), dbc (s0.h2), 1.5); // odd stage: H2 only scaled by the stage's mean slope
        const auto quiet = renderTone (col, -40.0f);
        CHECK (std::abs (quiet.fundDb) < 0.05);
    }

    // SATURATION is continuous at 0: 0.1 % differs from 0 % (stage bypassed)
    // by far less than the stage's own effect at its +24 dBFS ceiling (the
    // stage is blended in below 5 %, MODEL_SPEC.md 6.3). Thick, -6 dBFS.
    {
        auto renderSat = [] (float sat)
        {
            auto p = makeVeil (48000.0, 512);
            setParam (*p, "threshold", 20.0f);
            setParam (*p, "character", (float) cThick);
            setParam (*p, "saturation", sat);
            p->prepareToPlay (48000.0, 512);
            return render (*p, toBuffer (sine (48000.0, 1000.0, juce::Decibels::decibelsToGain (-6.0f), 24000), 2), 512);
        };
        const double step = maxAbsDiff (renderSat (0.0f), renderSat (0.1f));
        MEASURE ("SATURATION 0 % vs 0.1 %: max |difference| at -6 dBFS (continuity)", step);
        CHECK (step < 1.0e-4);
    }

    // None of the colour controls moves the GR (they act after the detector,
    // or are level-compensated before it); INPUT does (into the stage).
    Colour base;
    base.thresholdKnob = -12.0f;
    const double grBase = renderTone (base, -18.0f).grDb;
    MEASURE ("reference GR for the invariance checks (dB)", grBase);
    CHECK (grBase > 4.0);
    for (auto [name, field, value] : { std::tuple { "tubeDrive", 0, 50.0f }, std::tuple { "harmonics", 1, 100.0f },
                                       std::tuple { "saturation", 2, 100.0f }, std::tuple { "character", 3, (float) cThick } })
    {
        Colour col = base;
        if (field == 0) col.drive = value;
        if (field == 1) col.harmonics = value;
        if (field == 2) col.saturation = value;
        if (field == 3) col.character = (int) value;
        const double g = renderTone (col, -18.0f).grDb;
        MEASURE (juce::String (name) + ": GR change (dB)", g - grBase);
        CHECK (std::abs (g - grBase) < (field == 2 ? 1.0e-6 : 0.1));
    }
    Colour hot = base;
    hot.input = 6.0f;
    const auto rh = renderTone (hot, -18.0f), rb = renderTone (base, -18.0f);
    MEASURE ("INPUT +6 dB: GR change (dB)", rh.grDb - rb.grDb);
    MEASURE ("INPUT +6 dB: THD change (dB)", dbc (rh.thd) - dbc (rb.thd));
    CHECK (rh.grDb > rb.grDb + 2.0);
    CHECK (rh.thd > rb.thd);
    outputDirectory().getChildFile ("VEIL_thd_controls.csv").replaceWithText (csv);
}

TEST ("VEIL: aliasing of the nonlinear stages stays bounded with 2x oversampling (and is measurably lower than at 1x)")
{
    // In-band aliases of the harmonics of a high tone: harmonic k at k f folds
    // at the processing rate; anything that lands below 0.45 fs (the band) and
    // is not a true harmonic is aliasing (the downsampler removes 0.5..1 x fs).
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
            if (std::abs (r - std::round (r)) < 1e-6) continue; // a true harmonic
            worst = std::max (worst, toneAmplitude (d, n, rate, a) / fund);
        }
        return dbc (worst);
    };
    // the model alone at host rate x factor, analysed before decimation (ideal decimator)
    auto modelAlias = [&] (double fs, double f, float levelDb, int tier, int factor)
    {
        VeilModel m;
        m.setThresholdDbfs (vp::thresholdToDbfs (20.0f));
        m.setCharacter (tier == 0 ? cVeil : cThick);
        const float c = tier == 0 ? -1.0f : (tier == 1 ? 0.6f : 1.0f);
        m.setTubeDrive (c < 0 ? 0.28f : c);
        m.setHarmonics (c < 0 ? 0.42f : c);
        m.setSaturation (c < 0 ? 0.18f : c);
        m.setScHpf (60.0f);
        m.prepare (fs, factor);
        const double rate = fs * factor;
        const int n = (int) rate;
        auto x = sine (rate, f, juce::Decibels::decibelsToGain (levelDb), 2 * n);
        float* ch[] = { x.data() };
        m.process (ch, 1, nullptr, 0, 2 * n);
        return aliasDb (x.data() + n, n, rate, f, rate, 0.45 * fs);
    };

    // tiers: 0 = defaults at -1 dBFS; 1 = hot (Thick, DRIVE/HARMONICS/SATURATION 60 %)
    // at -3 dBFS; 2 = extreme (Thick, all three 100 %) at -1 dBFS.
    const char* tierName[] = { "default", "hot", "extreme" };
    const float tierLevel[] = { -1.0f, -3.0f, -1.0f };
    juce::String csv ("rate,tone_hz,setting,alias_2x_dbc,alias_1x_dbc,alias_4x_model_dbc\n");
    double worst[3] = { -300, -300, -300 };
    for (double fs : { 44100.0, 48000.0 })
        for (double f : { 5000.0, 7000.0, 9000.0 })
            for (int tier : { 0, 1, 2 })
            {
                auto p = makeVeil (fs, 512);
                setParam (*p, "threshold", 20.0f);
                if (tier > 0)
                {
                    const float v = tier == 1 ? 60.0f : 100.0f;
                    setParam (*p, "character", (float) cThick);
                    setParam (*p, "tubeDrive", v);
                    setParam (*p, "harmonics", v);
                    setParam (*p, "saturation", v);
                }
                p->prepareToPlay (fs, 512);
                const int n = (int) fs;
                const auto out = render (*p, toBuffer (sine (fs, f, juce::Decibels::decibelsToGain (tierLevel[tier]), 2 * n), 2), 512);
                const double a2 = aliasDb (out.getReadPointer (0) + n, n, fs, f, 2.0 * fs, 0.45 * fs);
                const double a1 = modelAlias (fs, f, tierLevel[tier], tier, 1);
                const double a4 = modelAlias (fs, f, tierLevel[tier], tier, 4); // what 4x would give (information)
                csv << fs << "," << f << "," << tierName[tier] << "," << a2 << "," << a1 << "," << a4 << "\n";
                MEASURE (juce::String (fs) + " Hz, " + juce::String (f) + " Hz tone at " + juce::String (tierLevel[tier]) + " dBFS, " + tierName[tier] + ": worst alias 2x (dBc)", a2);
                MEASURE ("   at 1x (dBc)", a1);
                MEASURE ("   model at 4x (dBc)", a4);
                worst[tier] = std::max (worst[tier], a2);
                if (tier > 0)
                    CHECK (a2 < a1 - 10.0);
            }
    outputDirectory().getChildFile ("VEIL_aliasing.csv").replaceWithText (csv);
    MEASURE ("worst alias, default colour (dBc)", worst[0]);
    MEASURE ("worst alias, hot colour (dBc)", worst[1]);
    MEASURE ("worst alias, extreme colour (dBc)", worst[2]);
    CHECK (worst[0] < -90.0);
    CHECK (worst[1] < -55.0);
    CHECK (worst[2] < -25.0); // the +24 dB drive corner at -1 dBFS; MODEL_SPEC.md 6.6 (open item: 4x)
}

TEST ("VEIL: MIX vs DRY/WET: routing per spec 13.6, linear null tests at the extremes, no double-gain anomalies")
{
    const double fs = 48000.0;
    const int n = 24000;
    std::vector<float> sig = sine (fs, 220.0, 0.6f, n);
    for (size_t i = 0; i < sig.size(); ++i)
        sig[i] *= (i / 4000) % 2 == 0 ? 1.0f : 0.2f;
    const auto in = toBuffer (sig, 2);
    auto renderAt = [&] (float mix, float dryWet, float sat, float thrKnob = -10.0f, float out = 0.0f)
    {
        auto p = makeVeil (fs, 128);
        setParam (*p, "threshold", thrKnob);
        setParam (*p, "compress", 8.0f);
        setParam (*p, "attack", 10.0f);
        setParam (*p, "recovery", 100.0f);
        setParam (*p, "character", (float) cThick);
        setParam (*p, "saturation", sat);
        setParam (*p, "mix", mix);
        setParam (*p, "dryWet", dryWet);
        setParam (*p, "output", out);
        p->prepareToPlay (fs, 128);
        return std::make_pair (render (*p, in, 128), p->getLatencySamples());
    };

    // MIX blends the coloured-uncompressed and the compressed signal BEFORE the
    // output stage: with the output stage linear (SATURATION 0) the output is an
    // exact linear blend of MIX 0 and MIX 100.
    const auto m0 = renderAt (0.0f, 100.0f, 0.0f).first, m100 = renderAt (100.0f, 100.0f, 0.0f).first;
    double worst = 0, spread = 0;
    for (float m : { 25.0f, 50.0f, 75.0f })
    {
        const auto o = renderAt (m, 100.0f, 0.0f).first;
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < n; ++i)
            {
                const double e = m0.getSample (ch, i) + (m / 100.0) * (m100.getSample (ch, i) - m0.getSample (ch, i));
                worst = std::max (worst, std::abs (o.getSample (ch, i) - e));
                spread = std::max (spread, (double) std::abs (m100.getSample (ch, i) - m0.getSample (ch, i)));
            }
    }
    MEASURE ("MIX: max |out - linear blend of MIX 0 and 100| (SATURATION 0)", worst);
    MEASURE ("MIX: max |MIX 100 - MIX 0| (the blend is not trivial)", spread);
    CHECK (worst < 2.0e-6);
    CHECK (spread > 0.1);
    // MIX 0 is independent of the compressor (bit-identical for any threshold)
    CHECK (identical (renderAt (0.0f, 100.0f, 18.0f, -20.0f).first, renderAt (0.0f, 100.0f, 18.0f, 20.0f).first));
    // ... and is not the dry signal: the colour is still there (Thick)
    const auto [m0col, L] = renderAt (0.0f, 100.0f, 50.0f);
    CHECK (maxAbsDiff (m0col, delayed (in, L)) > 1.0e-3);
    // with the output stage nonlinear, MIX is NOT a blend of outputs (it sits before the stage)
    const auto s0 = renderAt (0.0f, 100.0f, 100.0f).first, s100 = renderAt (100.0f, 100.0f, 100.0f).first, s50 = renderAt (50.0f, 100.0f, 100.0f).first;
    double devSat = 0;
    for (int i = 0; i < n; ++i)
        devSat = std::max (devSat, (double) std::abs (s50.getSample (0, i) - 0.5f * (s0.getSample (0, i) + s100.getSample (0, i))));
    MEASURE ("MIX 50 vs blend of outputs with SATURATION 100 (proves MIX is before the output stage)", devSat);
    CHECK (devSat > 1.0e-4);

    // DRY/WET blends the latency-aligned plugin input with the whole processed chain (colour included).
    const auto dry = delayed (in, L);
    const auto w100 = renderAt (100.0f, 100.0f, 50.0f).first;
    double worstDw = 0;
    for (float dw : { 0.0f, 30.0f, 70.0f })
    {
        const auto o = renderAt (100.0f, dw, 50.0f).first;
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < n; ++i)
            {
                const double e = dry.getSample (ch, i) + (dw / 100.0) * (w100.getSample (ch, i) - dry.getSample (ch, i));
                worstDw = std::max (worstDw, std::abs (o.getSample (ch, i) - e));
            }
    }
    MEASURE ("DRY/WET: max |out - linear blend of dry and processed|", worstDw);
    CHECK (worstDw < 2.0e-6);
    CHECK (identical (renderAt (100.0f, 0.0f, 50.0f).first, dry));

    // OUTPUT acts on the processed path only, once: DRY/WET 100 + OUTPUT +6 dB = processed x 2;
    // DRY/WET 0 ignores OUTPUT.
    const auto o6 = renderAt (100.0f, 100.0f, 50.0f, -10.0f, 6.0f).first;
    double worstOut = 0;
    const double g6 = std::pow (10.0, 6.0 / 20.0);
    for (int i = 0; i < n; ++i)
        worstOut = std::max (worstOut, std::abs (o6.getSample (0, i) - g6 * w100.getSample (0, i)));
    MEASURE ("OUTPUT +6 dB: max |out - 10^(6/20) x processed|", worstOut);
    CHECK (worstOut < 1.0e-5);
    CHECK (identical (renderAt (100.0f, 0.0f, 50.0f, -10.0f, 6.0f).first, dry));

    // No double-gain anomalies: below threshold with the colour neutral, every
    // MIX x DRY/WET combination is unity (no comb filtering, no +6 dB, no dip).
    double worstUnity = 0;
    for (float m : { 0.0f, 50.0f, 100.0f })
        for (float dw : { 0.0f, 50.0f, 100.0f })
        {
            auto p = makeVeil (fs, 128);
            cleanColour (*p);
            setParam (*p, "threshold", 20.0f);
            setParam (*p, "mix", m);
            setParam (*p, "dryWet", dw);
            p->prepareToPlay (fs, 128);
            const auto s = toBuffer (sine (fs, 3000.0, juce::Decibels::decibelsToGain (-24.0f), 24000), 2);
            const auto o = render (*p, s, 128);
            const double change = 20.0 * std::log10 (toneAmplitude (o.getReadPointer (0) + 12000, 12000, fs, 3000.0)
                                                     / toneAmplitude (s.getReadPointer (0) + 12000, 12000, fs, 3000.0));
            worstUnity = std::max (worstUnity, std::abs (change));
        }
    MEASURE ("below threshold: worst level change over MIX x DRY/WET in {0, 50, 100} (dB)", worstUnity);
    CHECK (worstUnity < 0.01);

    // Under compression, MIX 50 / DRY/WET 50 lies between the extremes (no anomaly).
    const auto mm = renderAt (50.0f, 50.0f, 0.0f).first;
    const float lvl = rms (mm.getReadPointer (0) + 12000, 12000);
    const float lo = std::min (rms (m100.getReadPointer (0) + 12000, 12000), rms (dry.getReadPointer (0) + 12000, 12000));
    const float hi = std::max (rms (m0.getReadPointer (0) + 12000, 12000), rms (dry.getReadPointer (0) + 12000, 12000));
    CHECK (lvl >= lo * 0.999f && lvl <= hi * 1.001f);
}

TEST ("VEIL: spectrum tap: inactive costs nothing and delivers nothing; active delivers exactly the final output (also in bypass)")
{
    const double fs = 48000.0;
    auto p = makeVeil (fs, 256);
    setParam (*p, "threshold", knobFor (-24.0f));
    p->prepareToPlay (fs, 256);
    CHECK_NEAR (p->spectrum.getSampleRate(), fs, 1e-9);
    const auto in = stereo (sine (fs, 440.0, 0.5f, 16384), sine (fs, 660.0, 0.3f, 16384));
    render (*p, in, 256);
    std::vector<float> pulled (16384);
    CHECK (p->spectrum.pull (pulled.data(), 16384) == 0); // inactive: nothing was written

    for (bool bypass : { false, true })
    {
        setParam (*p, "bypass", bypass ? 1.0f : 0.0f);
        p->prepareToPlay (fs, 256);
        p->spectrum.setActive (true);
        const auto out = render (*p, in, 256);
        const int got = p->spectrum.pull (pulled.data(), 16384);
        CHECK (got == 16384);
        bool exact = true;
        for (int i = 0; i < got; ++i)
            exact = exact && sameBits (pulled[(size_t) i], 0.5f * (out.getSample (0, i) + out.getSample (1, i)));
        CHECK_MSG (exact, bypass ? "tap is not the output (bypass)" : "tap is not the output");
        p->spectrum.setActive (false);
    }

    // The tap never changes the audio, and costs nothing measurable when inactive.
    auto renderTap = [&] (bool active, double& seconds)
    {
        auto q = makeVeil (fs, 128);
        q->spectrum.setActive (active);
        juce::AudioBuffer<float> io (in);
        juce::MidiBuffer midi;
        const auto t0 = juce::Time::getHighResolutionTicks();
        for (int pos = 0; pos < io.getNumSamples(); pos += 128)
        {
            juce::AudioBuffer<float> view (io.getArrayOfWritePointers(), 2, pos, 128);
            q->processBlock (view, midi);
        }
        seconds = juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - t0);
        return io;
    };
    double tOff, tOn;
    CHECK (identical (renderTap (false, tOff), renderTap (true, tOn)));
    MEASURE ("processing time, tap off / on (ms)", tOff * 1000.0);
    MEASURE ("   on", tOn * 1000.0);
    // the inactive tap's whole cost is one atomic load per block
    dsp::SpectrumTap tap;
    std::vector<float> big (1 << 16, 0.25f);
    const auto t0 = juce::Time::getHighResolutionTicks();
    for (int k = 0; k < 1000; ++k)
        tap.push (big.data(), big.data(), (int) big.size());
    const double inactiveNs = juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - t0) * 1.0e9 / 1000.0;
    MEASURE ("inactive tap: ns per push of 65536 samples", inactiveNs);
    CHECK (inactiveNs < 2000.0);
    CHECK (tap.pull (big.data(), 100) == 0);
}

TEST ("VEIL: switching CHARACTER, link, filters and every continuous control under compression is click-free; rapid automation stays finite")
{
    // Click measure: the residual of the 100 Hz sine predictor,
    // r = y[n] - 2 cos(w) y[n-1] + y[n-2], which is 0 for a steady 100 Hz sine
    // of any amplitude, tiny for its low harmonics and for smooth gain changes
    // (a 20 ms ramp's kink gives ~1e-5), and equals the size of a step. Each
    // switching run must stay below 2x the worst fixed setting it switches
    // between, or 1e-4 (a -80 dBFS step on a -10..-20 dBFS signal), whichever
    // is larger.
    const double fs = 48000.0;
    const int block = 64, blocks = 1600, period = 50;
    // which: -1 = every control at once, else only control `which` switches
    auto apply = [] (VeilProcessor& p, int k, int which)
    {
        auto on = [which] (int i) { return which < 0 || which == i; };
        if (on (0)) setParam (p, "character", (float) (k % 3));
        if (on (1)) setParam (p, "stereoLink", (float) (k % 2));
        if (on (2)) setParam (p, "hpf", k % 2 == 0 ? 20.0f : 150.0f);
        if (on (3)) setParam (p, "scHpf", k % 3 == 0 ? 20.0f : 250.0f);
        if (on (4)) setParam (p, "compress", (float) (k % 11));
        if (on (5)) setParam (p, "tubeDrive", k % 2 == 0 ? 0.0f : 60.0f);
        if (on (6)) setParam (p, "saturation", k % 2 == 0 ? 0.0f : 60.0f);
        if (on (7)) setParam (p, "mix", k % 2 == 0 ? 30.0f : 100.0f);
        if (on (8)) setParam (p, "dryWet", k % 2 == 0 ? 100.0f : 60.0f);
    };
    const char* names[] = { "character", "stereoLink", "hpf", "scHpf", "compress", "tubeDrive", "saturation", "mix", "dryWet" };
    auto run = [&] (int fixedK, int which, int numBlocks)
    {
        auto p = makeVeil (fs, block);
        setParam (*p, "threshold", knobFor (-30.0f));
        setParam (*p, "attack", 20.0f);
        setParam (*p, "recovery", 200.0f);
        if (fixedK >= 0)
            apply (*p, fixedK, -1);
        p->prepareToPlay (fs, block);
        juce::MidiBuffer midi;
        const float c2 = (float) (2.0 * std::cos (2.0 * pi * 100.0 / fs));
        float h[2] {}, maxR = 0;
        for (int b = 0; b < numBlocks; ++b)
        {
            if (fixedK < 0 && b % period == 0)
                apply (*p, b / period, which);
            auto buf = toBuffer (sine (fs, 100.0, 0.3f, block, 2.0 * pi * 100.0 * b * block / fs), 2);
            p->processBlock (buf, midi);
            for (int i = 0; i < block; ++i)
            {
                const float y = buf.getSample (0, i);
                if (b > (fixedK < 0 ? 20 : numBlocks / 2)) // fixed settings: measure the settled half
                    maxR = std::max (maxR, std::abs (y - c2 * h[0] + h[1]));
                h[1] = h[0]; h[0] = y;
            }
        }
        return maxR;
    };
    float baseline = 0;
    for (int k = 0; k < blocks / period; ++k)
        baseline = std::max (baseline, run (k, -1, 400));
    MEASURE ("max |sine residual|, worst fixed setting", baseline);
    const float limit = std::max (2.0f * baseline, 1.0e-4f);
    for (int w = 0; w < 9; ++w)
    {
        const float one = run (-1, w, blocks);
        MEASURE (juce::String ("max |sine residual| switching only ") + names[w], one);
        CHECK_MSG (one < limit, juce::String ("click when switching ") + names[w]);
    }
    const float switched = run (-1, -1, blocks);
    MEASURE ("max |sine residual| switching everything every 67 ms", switched);
    CHECK (switched < limit);

    auto p = makeVeil (fs, 64, 2, 2);
    juce::Random rng (42);
    float prevSample = 0.0f, maxJump = 0.0f;
    bool finite = true;
    juce::MidiBuffer midi;
    for (int b = 0; b < 3000; ++b)
    {
        setParam (*p, "input", -6.0f + rng.nextFloat() * 12.0f);
        setParam (*p, "threshold", -20.0f + rng.nextFloat() * 40.0f);
        setParam (*p, "attack", 10.0f + rng.nextFloat() * 990.0f);
        setParam (*p, "recovery", 50.0f + rng.nextFloat() * 1950.0f);
        setParam (*p, "compress", rng.nextFloat() * 10.0f);
        setParam (*p, "mix", rng.nextFloat() * 100.0f);
        setParam (*p, "output", -6.0f + rng.nextFloat() * 12.0f);
        setParam (*p, "tubeDrive", rng.nextFloat() * 100.0f);
        setParam (*p, "harmonics", rng.nextFloat() * 100.0f);
        setParam (*p, "saturation", rng.nextFloat() * 100.0f);
        setParam (*p, "dryWet", rng.nextFloat() * 100.0f);
        setParam (*p, "hpf", 20.0f + rng.nextFloat() * 280.0f);
        setParam (*p, "scHpf", 20.0f + rng.nextFloat() * 280.0f);
        setParam (*p, "character", (float) rng.nextInt (3));
        setParam (*p, "stereoLink", rng.nextBool() ? 1.0f : 0.0f);
        setParam (*p, "meterMode", (float) rng.nextInt (3));
        setParam (*p, "scExternal", rng.nextBool() ? 1.0f : 0.0f);
        juce::AudioBuffer<float> io (4, 64);
        const auto m = sine (fs, 200.0, 0.3f, 64, 2.0 * pi * 200.0 * b * 64 / fs);
        const auto k = sine (fs, 77.0, 0.8f, 64, 2.0 * pi * 77.0 * b * 64 / fs);
        for (int ch = 0; ch < 2; ++ch) { io.copyFrom (ch, 0, m.data(), 64); io.copyFrom (2 + ch, 0, k.data(), 64); }
        p->processBlock (io, midi);
        for (int i = 0; i < 64; ++i)
        {
            finite = finite && std::isfinite (io.getSample (0, i));
            maxJump = std::max (maxJump, std::abs (io.getSample (0, i) - prevSample));
            prevSample = io.getSample (0, i);
        }
    }
    MEASURE ("rapid automation: max sample-to-sample jump", maxJump);
    CHECK (finite);
    CHECK (maxJump < 0.08f);
}

TEST ("VEIL: the editor (open, resized, painted, meters and spectrum read) does not alter the audio")
{
    const double fs = 48000.0;
    auto renderWith = [&] (bool withEditor)
    {
        auto p = makeVeil (fs, 256);
        setParam (*p, "threshold", knobFor (-24.0f));
        setParam (*p, "stereoLink", 0.0f);
        p->prepareToPlay (fs, 256);
        std::unique_ptr<juce::AudioProcessorEditor> ed;
        if (withEditor)
        {
            ed.reset (p->createEditor());
            CHECK (ed != nullptr);
            ed->setSize (900, 700);
            p->setUiProperty ("scale", 1.5);
            p->spectrum.setActive (true);
        }
        juce::Random rng (3);
        juce::AudioBuffer<float> in (2, 48000);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < in.getNumSamples(); ++i)
                in.setSample (ch, i, 0.4f * (float) std::sin (i * 0.05 + ch) + (rng.nextFloat() - 0.5f) * 0.2f);
        juce::AudioBuffer<float> out (in);
        juce::MidiBuffer midi;
        std::vector<float> scratch (8192);
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
                p->takeGrDb (0);
                p->takeGrDb (1);
                p->inputMeters[0].takeRms();
                p->outputMeters[1].takePeak();
                p->spectrum.pull (scratch.data(), 8192);
                setParam (*p, "meterMode", (float) ((pos / 4096) % 3));
            }
        }
        ed.reset();
        return out;
    };
    CHECK_MSG (identical (renderWith (false), renderWith (true)), "the editor altered the audio");
}

TEST ("VEIL: state round trip restores every parameter and UI settings and renders identically")
{
    const double fs = 48000.0;
    auto a = makeVeil (fs, 256);
    setParam (*a, "input", 3.4f);
    setParam (*a, "threshold", -7.5f);
    setParam (*a, "attack", 120.0f);
    setParam (*a, "recovery", 650.0f);
    setParam (*a, "compress", 6.2f);
    setParam (*a, "mix", 72.0f);
    setParam (*a, "output", -3.5f);
    setParam (*a, "tubeDrive", 61.0f);
    setParam (*a, "harmonics", 33.0f);
    setParam (*a, "saturation", 47.0f);
    setParam (*a, "dryWet", 88.0f);
    setParam (*a, "hpf", 45.0f);
    setParam (*a, "scHpf", 133.0f);
    setParam (*a, "character", (float) cThick);
    setParam (*a, "scExternal", 1.0f);
    setParam (*a, "stereoLink", 0.0f);
    setParam (*a, "meterMode", (float) mInOut);
    a->setUiProperty ("scale", 1.25);

    juce::MemoryBlock state;
    a->getStateInformation (state);
    auto b = makeVeil (fs, 256);
    b->setStateInformation (state.getData(), (int) state.getSize());
    for (auto* param : a->getParameters())
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (param))
            CHECK_MSG (std::abs (r->getValue() - b->apvts.getParameter (r->paramID)->getValue()) < 1e-6f, r->paramID);
    CHECK_NEAR ((double) b->getUiProperty ("scale", 1.0), 1.25, 1e-9);

    a->prepareToPlay (fs, 256);
    b->prepareToPlay (fs, 256);
    const auto sig = toBuffer (sine (fs, 300.0, 0.4f, 48000), 2);
    CHECK_MSG (identical (render (*a, sig, 256), render (*b, sig, 256)), "restored instance renders differently");
    auto fresh = makeVeil (fs, 256);
    CHECK (! identical (render (*fresh, sig, 256), render (*b, sig, 256)));

    b->setStateInformation ("nonsense", 8);
    CHECK_NEAR (plainValue (*b, "mix"), 72.0f, 0.01);
}

TEST ("VEIL: factory presets load, set every parameter and reject other plugins' presets")
{
    VeilProcessor p;
    const auto& list = p.presets.getPresets();
    int factory = 0;
    for (auto& pr : list) factory += pr.isFactory ? 1 : 0;
    MEASURE ("factory presets", factory);
    CHECK (factory >= 10);

    std::set<int> charSeen, linkSeen;
    std::set<juce::String> names;
    for (int i = 0; i < (int) list.size(); ++i)
        if (list[(size_t) i].isFactory)
        {
            p.loadPreset (i);
            names.insert (list[(size_t) i].name);
            CHECK (list[(size_t) i].params.values.size() == 17);
            for (auto& [id, v] : list[(size_t) i].params.values)
            {
                auto* r = p.apvts.getParameter (id);
                CHECK_MSG (r != nullptr, id);
                if (r) CHECK_MSG (std::abs (r->convertFrom0to1 (r->getValue()) - v) < 0.06f, list[(size_t) i].name + ": " + id);
            }
            CHECK (! p.isPresetModified());
            CHECK (plainValue (p, "scExternal") < 0.5f); // factory presets never switch the routing on
            charSeen.insert (juce::roundToInt (plainValue (p, "character")));
            linkSeen.insert (juce::roundToInt (plainValue (p, "stereoLink")));
        }
    CHECK (charSeen.size() == 3 && linkSeen.size() == 2);
    CHECK ((int) names.size() == factory);

    setParam (p, "mix", 13.0f);
    CHECK (p.isPresetModified());
    PresetManager::Preset out;
    CHECK (p.presets.parse (R"({"format":"tleet-preset","version":1,"plugin":"WEIGHT","name":"x","params":{}})", out).failed());
    CHECK (p.presets.parse (R"({"format":"tleet-preset","version":99,"plugin":"VEIL","name":"x","params":{}})", out).failed());
    const auto json = p.stateToClipboardJson();
    setParam (p, "mix", 99.0f);
    CHECK (p.pasteStateFromJson (json).wasOk());
    CHECK_NEAR (plainValue (p, "mix"), 13.0f, 0.06);

    // every factory preset renders finite audio
    for (int i = 0; i < (int) list.size(); ++i)
    {
        auto q = makeVeil (48000.0, 256);
        q->loadPreset (i);
        q->prepareToPlay (48000.0, 256);
        const auto o = render (*q, toBuffer (sine (48000.0, 200.0, 0.5f, 24000), 2), 256);
        CHECK_MSG (allFinite (o) && rms (o.getReadPointer (0), 24000) > 0.01f, list[(size_t) i].name);
    }
}

TEST ("VEIL: CPU cost per instance (measurement)")
{
    for (double fs : { 44100.0, 48000.0, 96000.0, 192000.0 })
        for (int keyChannels : { 0, 2 })
        {
            auto p = makeVeil (fs, 128, 2, keyChannels);
            setParam (*p, "threshold", knobFor (-24.0f));
            setParam (*p, "hpf", 40.0f);
            setParam (*p, "scHpf", 80.0f);
            setParam (*p, "stereoLink", 0.0f);
            setParam (*p, "scExternal", keyChannels > 0 ? 1.0f : 0.0f);
            setParam (*p, "mix", 80.0f);
            p->prepareToPlay (fs, 128);
            juce::Random rng (1);
            const int n = (int) (fs * 5.0);
            juce::AudioBuffer<float> io (2 + keyChannels, n);
            for (int ch = 0; ch < 2 + keyChannels; ++ch)
                for (int i = 0; i < n; ++i)
                    io.setSample (ch, i, 0.5f * (float) std::sin (i * (ch < 2 ? 0.031 : 0.007)) * (rng.nextFloat() * 0.6f + 0.4f));
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
            const double load = ms / 5000.0 * 100.0;
            // the median block time is robust against other processes on a shared machine
            const double medianLoad = times[times.size() / 2] * (fs / 128.0) * 100.0;
            const auto tag = juce::String (fs) + " Hz, block 128, key " + juce::String (keyChannels);
            MEASURE (tag + ": % of one core", load);
            MEASURE (tag + ": % of one core from the median block", medianLoad);
            MEASURE (tag + ": p99 block time (us)", times[(size_t) (0.99 * (double) times.size())] * 1e6);
            MEASURE (tag + ": max block time (us)", times.back() * 1e6);
            CHECK (medianLoad < (fs > 100000.0 ? 25.0 : 12.0));
        }
}
