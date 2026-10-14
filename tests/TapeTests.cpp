#include "TestFramework.h"
#include "TestSignals.h"
#include <complex>
#include <cstring>
#include <functional>
#include <map>
#include <set>
#include "../plugins/TAPE/PluginProcessor.h"
#include "../plugins/TAPE/Parameters.h"
#include "../plugins/TAPE/dsp/TapeEngine.h"

// Exact floating-point comparisons are deliberate here (bit-exact paths).
#if defined(__clang__) || defined(__GNUC__)
 #pragma GCC diagnostic ignored "-Wfloat-equal"
#endif

using namespace tleet;
using namespace tleet::test;
using tleet::tape::TapeProcessor;
using tleet::tape::TapeEngine;
using tleet::tape::JAParams;
using tleet::tape::HysteresisSolver;
namespace tp = tleet::tape::params;
namespace tmod = tleet::tape::model;

namespace
{
    constexpr double pi = juce::MathConstants<double>::pi;
    const double zeroVu = std::pow (10.0, -18.0 / 20.0); // 0 VU sine peak (independent of the plugin's constant)
    using Setting = std::pair<const char*, float>;

    void setParam (TapeProcessor& p, const char* id, float plain)
    {
        auto* r = p.apvts.getParameter (id);
        r->setValueNotifyingHost (r->convertTo0to1 (plain));
    }

    float plainValue (TapeProcessor& p, const char* id)
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

    juce::AudioBuffer<float> render (TapeProcessor& p, const juce::AudioBuffer<float>& input, int block)
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

    std::unique_ptr<TapeProcessor> makeTape (double fs, int block, int channels = 2, int osOverride = -1)
    {
        auto p = std::make_unique<TapeProcessor>();
        TapeProcessor::BusesLayout layout;
        layout.inputBuses.add (channels == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo());
        layout.outputBuses.add (channels == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo());
        CHECK_MSG (p->setBusesLayout (layout), "layout rejected");
        p->setOversamplingOverrideForTesting (osOverride);
        p->setRateAndBufferSizeDetails (fs, block);
        p->prepareToPlay (fs, block);
        return p;
    }

    // A clean measuring machine: no transport modulation, no AGE, no noise, no
    // crosstalk, AUTO GAIN off. Everything else at its default unless given.
    const std::vector<Setting> cleanMachine { { tp::age, 0.0f }, { tp::wow, 0.0f }, { tp::flutter, 0.0f }, { tp::noise, 0.0f },
                                              { tp::crosstalk, 0.0f }, { tp::autoGain, 0.0f } };

    std::unique_ptr<TapeProcessor> makeConfigured (double fs, const std::vector<Setting>& settings, int block = 256, int channels = 2,
                                                   bool clean = true, int osOverride = -1)
    {
        auto p = makeTape (fs, block, channels, osOverride);
        if (clean)
            for (auto [id, v] : cleanMachine)
                setParam (*p, id, v);
        for (auto [id, v] : settings)
            setParam (*p, id, v);
        p->prepareToPlay (fs, block); // start settled at the configuration
        return p;
    }

    juce::File outFile (const juce::String& name) { return outputDirectory().getChildFile (name); }

    // ---- FFT (radix 2, double), written for the tests
    void fft (std::vector<std::complex<double>>& a, bool inverse)
    {
        const size_t n = a.size();
        for (size_t i = 1, j = 0; i < n; ++i)
        {
            size_t bit = n >> 1;
            for (; j & bit; bit >>= 1)
                j ^= bit;
            j ^= bit;
            if (i < j)
                std::swap (a[i], a[j]);
        }
        for (size_t len = 2; len <= n; len <<= 1)
        {
            const double ang = 2 * pi / (double) len * (inverse ? 1 : -1);
            const std::complex<double> wl (std::cos (ang), std::sin (ang));
            for (size_t i = 0; i < n; i += len)
            {
                std::complex<double> w (1.0);
                for (size_t k = 0; k < len / 2; ++k)
                {
                    const auto u = a[i + k], v = a[i + k + len / 2] * w;
                    a[i + k] = u + v;
                    a[i + k + len / 2] = u - v;
                    w *= wl;
                }
            }
        }
        if (inverse)
            for (auto& v : a)
                v /= (double) n;
    }

    // ---- small-signal frequency response by exponential-sweep deconvolution (Farina):
    // a log sweep at -20 VU through the plugin, H = Y / X, |H| averaged over +-1/24 octave.
    struct Response
    {
        double fs = 48000.0;
        std::vector<double> mag2; // |H|^2 per bin
        int nfft = 0;
        double db (double hz) const
        {
            const double lo = hz * std::pow (2.0, -1.0 / 48.0), hi = hz * std::pow (2.0, 1.0 / 48.0);
            int b0 = (int) std::floor (lo * nfft / fs), b1 = (int) std::ceil (hi * nfft / fs);
            b0 = std::max (1, b0);
            b1 = std::min (nfft / 2 - 1, std::max (b1, b0));
            double s = 0.0;
            for (int b = b0; b <= b1; ++b) s += mag2[(size_t) b];
            return 10.0 * std::log10 (s / (b1 - b0 + 1));
        }
    };

    Response measureResponse (TapeProcessor& p, double fs, int channel = 0, double levelVu = -20.0, int inputChannel = -1)
    {
        const int len = fs > 120000.0 ? (1 << 18) : (fs > 60000.0 ? (1 << 17) : (1 << 16));
        const int lead = (int) (0.05 * fs), total = len + lead + (int) (0.3 * fs);
        const double f1 = 5.0, f2 = 0.48 * fs, T = (double) len / fs, k = std::log (f2 / f1);
        const double amp = zeroVu * std::pow (10.0, levelVu / 20.0);
        std::vector<float> x ((size_t) total, 0.0f);
        for (int i = 0; i < len; ++i)
        {
            const double t = (double) i / fs;
            double w = 1.0;
            const int fade = (int) (0.01 * fs);
            if (i < fade) w = 0.5 - 0.5 * std::cos (pi * i / fade);
            if (i > len - fade) w = 0.5 - 0.5 * std::cos (pi * (len - i) / fade);
            x[(size_t) (lead + i)] = (float) (amp * w * std::sin (2 * pi * f1 * T / k * (std::exp (t * k / T) - 1.0)));
        }
        const int chans = p.getMainBusNumInputChannels();
        juce::AudioBuffer<float> in (chans, total);
        in.clear();
        for (int ch = 0; ch < chans; ++ch)
            if (inputChannel < 0 || inputChannel == ch)
                in.copyFrom (ch, 0, x.data(), total);
        const auto out = render (p, in, 512);
        const int nfft = juce::nextPowerOfTwo (total) * 2;
        std::vector<std::complex<double>> X ((size_t) nfft), Y ((size_t) nfft);
        for (int i = 0; i < total; ++i)
        {
            X[(size_t) i] = x[(size_t) i];
            Y[(size_t) i] = out.getSample (channel, i);
        }
        fft (X, false);
        fft (Y, false);
        Response r;
        r.fs = fs;
        r.nfft = nfft;
        r.mag2.resize ((size_t) nfft / 2);
        for (int b = 0; b < nfft / 2; ++b)
        {
            const double xm = std::norm (X[(size_t) b]);
            r.mag2[(size_t) b] = xm > 1e-20 ? std::norm (Y[(size_t) b]) / xm : 0.0;
        }
        return r;
    }

    // ---- stepped sine: harmonics by Goertzel over whole periods of the settled tail
    struct Harmonics
    {
        double fundamental = 0.0;          // amplitude
        std::array<double, 10> h {};       // h[k] = amplitude of harmonic k (k >= 2)
        double thd = 0.0, thdN = 0.0, h2 = 0.0, h3 = 0.0;
        double outRms = 0.0;
    };

    Harmonics analyseTone (const float* d, int n, double fs, double hz)
    {
        Harmonics r;
        r.fundamental = toneAmplitude (d, n, fs, hz);
        double hs = 0.0;
        for (int k = 2; k <= 9 && k * hz < 0.5 * fs; ++k)
        {
            r.h[(size_t) k] = toneAmplitude (d, n, fs, hz * k);
            hs += r.h[(size_t) k] * r.h[(size_t) k];
        }
        r.thd = std::sqrt (hs) / r.fundamental;
        r.h2 = r.h[2] / r.fundamental;
        r.h3 = r.h[3] / r.fundamental;
        // THD+N: residual after removing the fitted fundamental (and DC), re the fundamental's RMS
        double re = 0, im = 0, mean = 0;
        for (int i = 0; i < n; ++i)
        {
            re += d[i] * std::cos (2 * pi * hz * i / fs);
            im += d[i] * std::sin (2 * pi * hz * i / fs);
            mean += d[i];
        }
        re *= 2.0 / n; im *= 2.0 / n; mean /= n;
        double res = 0.0, tot = 0.0;
        for (int i = 0; i < n; ++i)
        {
            const double fit = mean + re * std::cos (2 * pi * hz * i / fs) + im * std::sin (2 * pi * hz * i / fs);
            res += (d[i] - fit) * (d[i] - fit);
            tot += (double) d[i] * d[i];
        }
        r.thdN = std::sqrt (res / n) / (r.fundamental / std::sqrt (2.0));
        r.outRms = std::sqrt (tot / n);
        return r;
    }

    // Renders a 1 kHz (or hz) sine at levelVu (re the plugin's 0 VU) for `seconds` and analyses the last half.
    Harmonics toneThrough (TapeProcessor& p, double fs, double levelVu, double hz = 1000.0, double seconds = 0.4, int channel = 0)
    {
        const int n = (int) (seconds * fs);
        const auto in = toBuffer (sine (fs, hz, (float) (zeroVu * std::pow (10.0, levelVu / 20.0)), n), p.getMainBusNumInputChannels());
        const auto out = render (p, in, 256);
        const int per = (int) std::lround (fs / hz);
        const int len = (n / 2) / per * per;
        return analyseTone (out.getReadPointer (channel) + n - len, len, fs, hz);
    }

    // Click measure: 4th-order 8 kHz Butterworth high-pass (RBJ biquads), peak after `from`.
    std::vector<float> clickBand (const float* x, int n, double fs)
    {
        std::vector<float> y (x, x + n);
        for (double q : { 0.5412, 1.3066 })
        {
            const double w0 = 2 * pi * 8000.0 / fs, cs = std::cos (w0), al = std::sin (w0) / (2 * q);
            const double b0 = (1 + cs) / 2, b1 = -(1 + cs), b2 = (1 + cs) / 2, a0 = 1 + al, a1 = -2 * cs, a2 = 1 - al;
            double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
            for (auto& v : y)
            {
                const double o = (b0 * v + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2) / a0;
                x2 = x1; x1 = v; y2 = y1; y1 = o;
                v = (float) o;
            }
        }
        return y;
    }

    double maxAbs (const std::vector<float>& v, int from, int to)
    {
        double m = 0.0;
        for (int i = std::max (0, from); i < std::min ((int) v.size(), to); ++i)
            m = std::max (m, (double) std::abs (v[(size_t) i]));
        return m;
    }

    // Music-like programme (kick, snare, hats, bass, a bright chord), stereo, peaks at `peak`.
    juce::AudioBuffer<float> musicLike (double fs, double seconds, float peak, int seed = 11)
    {
        const int n = (int) (seconds * fs);
        juce::AudioBuffer<float> b (2, n);
        b.clear();
        juce::Random rng (seed);
        const double beat = 60.0 / 118.0;
        for (int ch = 0; ch < 2; ++ch)
        {
            auto* d = b.getWritePointer (ch);
            double phB = 0, phC1 = 0, phC2 = 0;
            for (int i = 0; i < n; ++i)
            {
                const double t = i / fs, tb = std::fmod (t, beat);
                const int beatIdx = (int) (t / beat);
                double s = 0.9 * std::exp (-tb * 18.0) * std::sin (2 * pi * (48.0 * tb + 90.0 * (1.0 - std::exp (-tb * 30.0)) / 30.0));
                if (beatIdx % 2 == 1) s += 0.5 * std::exp (-tb * 25.0) * (2.0 * rng.nextDouble() - 1.0);
                const double te = std::fmod (t, beat * 0.5);
                s += 0.18 * std::exp (-te * 80.0) * (2.0 * rng.nextDouble() - 1.0);
                phB += 2 * pi * ((beatIdx / 4) % 2 == 0 ? 55.0 : 73.4) / fs;
                s += 0.35 * std::sin (phB) + 0.1 * std::sin (3 * phB);
                phC1 += 2 * pi * (261.6 * (ch == 0 ? 1.0 : 1.002)) / fs;
                phC2 += 2 * pi * 392.0 / fs;
                s += 0.08 * (std::sin (phC1) + std::sin (phC2)) + 0.03 * std::sin (17.0 * phC1);
                d[i] = (float) s;
            }
        }
        b.applyGain (peak / b.getMagnitude (0, n));
        return b;
    }

    struct FakePlayHead : juce::AudioPlayHead
    {
        bool playing = false;
        juce::Optional<PositionInfo> getPosition() const override
        {
            PositionInfo info;
            info.setIsPlaying (playing);
            return info;
        }
    };

    const char* speedName (int s) { static const char* n[] = { "7.5", "15", "30" }; return n[s]; }
    const char* typeName (int t) { static const char* n[] = { "A", "B", "C" }; return n[t]; }
}

// =====================================================================================

TEST ("TAPE: parameter IDs, ranges, defaults, texts, choice strings and buses match the registry")
{
    TapeProcessor p;
    struct Expect { const char* id; float lo, hi, def; };
    const Expect floats[] = {
        { tp::input, -12.0f, 24.0f, 0.0f }, { tp::output, -24.0f, 24.0f, 0.0f }, { tp::bias, -100.0f, 100.0f, 0.0f },
        { tp::saturation, 0.0f, 100.0f, 42.0f }, { tp::headBump, 0.0f, 100.0f, 36.0f }, { tp::lf, -100.0f, 100.0f, 0.0f },
        { tp::hf, -100.0f, 100.0f, 0.0f }, { tp::age, 0.0f, 100.0f, 18.0f }, { tp::wow, 0.0f, 100.0f, 6.0f },
        { tp::flutter, 0.0f, 100.0f, 4.0f }, { tp::noiseAmount, 0.0f, 100.0f, 12.0f }, { tp::crosstalk, 0.0f, 100.0f, 8.0f },
        { tp::mix, 0.0f, 100.0f, 100.0f } };
    for (auto& e : floats)
    {
        auto* r = dynamic_cast<juce::AudioParameterFloat*> (p.apvts.getParameter (e.id));
        CHECK_MSG (r != nullptr, e.id);
        if (r == nullptr) continue;
        CHECK_NEAR (r->range.start, e.lo, 1e-6);
        CHECK_NEAR (r->range.end, e.hi, 1e-6);
        CHECK_NEAR (r->convertFrom0to1 (static_cast<juce::AudioProcessorParameter*> (r)->getDefaultValue()), e.def, 1e-4);
    }
    auto choice = [&] (const char* id, juce::StringArray names, int def)
    {
        auto* c = dynamic_cast<juce::AudioParameterChoice*> (p.apvts.getParameter (id));
        CHECK_MSG (c != nullptr, id);
        if (c == nullptr) return;
        CHECK_MSG (c->choices == names, id);
        CHECK_MSG (c->getIndex() == def, id);
    };
    choice (tp::tapeType, { "A", "B", "C" }, 0);
    choice (tp::speed, { "7.5", "15", "30" }, 1);
    choice (tp::calibration, { "-3", "0", "+3", "+6" }, 1);
    for (auto [id, def] : { std::pair { tp::noise, false }, std::pair { tp::autoGain, true }, std::pair { tp::bypass, false } })
    {
        auto* b = dynamic_cast<juce::AudioParameterBool*> (p.apvts.getParameter (id));
        CHECK_MSG (b != nullptr, id);
        if (b) CHECK_MSG (b->get() == def, id);
    }
    CHECK (p.getParameters().size() == 19);
    CHECK (p.getBypassParameter() == p.apvts.getParameter (tp::bypass));

    auto text = [&] (const char* id, float v) { auto* r = p.apvts.getParameter (id); return r->getText (r->convertTo0to1 (v), 64); };
    CHECK (text (tp::input, 6.4f) == "+6.4 dB");
    CHECK (text (tp::input, 0.0f) == "0.0 dB");
    CHECK (text (tp::output, 0.0f) == "0.0 dB");
    CHECK (text (tp::output, -4.5f) == "-4.5 dB");
    CHECK (text (tp::bias, 0.0f) == "0 %");
    CHECK (text (tp::bias, 25.0f) == "+25 %");
    CHECK (text (tp::bias, -40.0f) == "-40 %");
    CHECK (text (tp::saturation, 42.0f) == "42 %");
    CHECK (text (tp::headBump, 36.0f) == "36 %");
    CHECK (text (tp::lf, -12.0f) == "-12");
    CHECK (text (tp::hf, 18.0f) == "+18");
    CHECK (text (tp::hf, 0.0f) == "0");
    CHECK (text (tp::mix, 100.0f) == "100 %");
    CHECK (text (tp::speed, 1.0f) == "15 IPS");
    CHECK (text (tp::speed, 0.0f) == "7.5 IPS");
    CHECK (text (tp::speed, 2.0f) == "30 IPS");
    CHECK (text (tp::calibration, 2.0f) == "+3 dB");
    CHECK (text (tp::calibration, 0.0f) == "-3 dB");
    CHECK (text (tp::calibration, 1.0f) == "0 dB");
    CHECK (text (tp::tapeType, 2.0f) == "C");
    // text entry
    auto* sp = p.apvts.getParameter (tp::speed);
    CHECK_NEAR (sp->convertFrom0to1 (sp->getValueForText ("30 IPS")), 2.0f, 1e-6);
    CHECK_NEAR (sp->convertFrom0to1 (sp->getValueForText ("7.5")), 0.0f, 1e-6);
    auto* cal = p.apvts.getParameter (tp::calibration);
    CHECK_NEAR (cal->convertFrom0to1 (cal->getValueForText ("+6 dB")), 3.0f, 1e-6);
    CHECK_NEAR (cal->convertFrom0to1 (cal->getValueForText ("-3")), 0.0f, 1e-6);
    auto* tt = p.apvts.getParameter (tp::tapeType);
    CHECK_NEAR (tt->convertFrom0to1 (tt->getValueForText ("b")), 1.0f, 1e-6);
    // OUTPUT knob centre = 0 dB; 0.1 dB steps
    CHECK_NEAR (p.apvts.getParameter (tp::output)->convertFrom0to1 (0.5f), 0.0f, 1e-6f);
    CHECK_NEAR (p.apvts.getParameter (tp::input)->convertFrom0to1 (p.apvts.getParameter (tp::input)->convertTo0to1 (3.4567f)), 3.5f, 1e-5);

    TapeProcessor::BusesLayout l;
    l.inputBuses.add (juce::AudioChannelSet::mono());
    l.outputBuses.add (juce::AudioChannelSet::stereo());
    CHECK (! p.checkBusesLayoutSupported (l));
    l.inputBuses.clear(); l.outputBuses.clear();
    l.inputBuses.add (juce::AudioChannelSet::mono());
    l.outputBuses.add (juce::AudioChannelSet::mono());
    CHECK (p.checkBusesLayoutSupported (l));
    l.inputBuses.clear(); l.outputBuses.clear();
    l.inputBuses.add (juce::AudioChannelSet::create5point1());
    l.outputBuses.add (juce::AudioChannelSet::create5point1());
    CHECK (! p.checkBusesLayoutSupported (l));
}

TEST ("TAPE: reported latency equals the measured impulse delay at 44.1-192 kHz, mono and stereo; composition = oversampler + transport centre + loss FIR (CSV)")
{
    juce::String csv ("fs,channels,oversampling,reported,measured,ms\n");
    for (double fs : { 44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0 })
        for (int channels : { 1, 2 })
        {
            auto p = makeConfigured (fs, { { tp::headBump, 0.0f } }, 256, channels);
            const int L = p->getLatencySamples();
            const int n = L + (int) (0.1 * fs);
            juce::AudioBuffer<float> in (channels, n);
            in.clear();
            for (int ch = 0; ch < channels; ++ch)
                in.setSample (ch, 0, 0.02f);
            const auto out = render (*p, in, 256);
            int at = 0;
            float best = 0.0f;
            for (int i = 0; i < n; ++i)
                if (std::abs (out.getSample (0, i)) > best) { best = std::abs (out.getSample (0, i)); at = i; }
            const int osf = p->getEngineForTesting().oversamplingFactor();
            const int expected = tleet::tape::Oversampler::latencyHostSamples (osf == 4 ? 2 : (osf == 2 ? 1 : 0))
                                 + (int) std::lround (0.0025 * fs) + tleet::tape::LossFirDesigner::tapsForRate (fs) / 2;
            csv << fs << "," << channels << "," << osf << "," << L << "," << at << "," << (1000.0 * L / fs) << "\n";
            const auto tag = juce::String (fs) + " Hz " + juce::String (channels) + " ch";
            CHECK_MSG (at == L, tag + ": measured " + juce::String (at) + " vs reported " + juce::String (L));
            CHECK_MSG (L == expected, tag);
            CHECK_MSG (osf == (fs < 60000.0 ? 4 : (fs < 120000.0 ? 2 : 1)), tag);
            if (channels == 2 && fs == 48000.0)
                MEASURE ("latency at 48 kHz (samples)", L);
        }
    outFile ("TAPE_latency.csv").replaceWithText (csv);

    // the machine's impulse response per speed at 48 kHz (clean machine, -40 dBFS impulse), one channel per speed
    const double fs = 48000.0;
    const int n = (int) (0.1 * fs);
    juce::AudioBuffer<float> irs (3, n);
    for (int s = 0; s < 3; ++s)
    {
        auto p = makeConfigured (fs, { { tp::speed, (float) s } }, 256, 1);
        juce::AudioBuffer<float> in (1, n);
        in.clear();
        in.setSample (0, 0, 0.01f);
        const auto out = render (*p, in, 256);
        irs.copyFrom (s, 0, out, 0, 0, n);
        irs.applyGain (s, 0, n, 100.0f);
    }
    CHECK (allFinite (irs));
    writeWav (outFile ("TAPE_impulse_responses_7.5_15_30ips.wav"), irs, fs);
}

TEST ("TAPE: frequency response for each IPS (small signal): head bump frequency and height follow speed and HEAD BUMP, HF extension follows speed, A800 response windows met (CSV)")
{
    const double fs = 96000.0;
    const std::vector<double> freqs { 10, 12.5, 16, 20, 25, 31.5, 40, 50, 63, 80, 100, 125, 160, 200, 250, 315, 400, 500, 630, 800, 1000, 1250, 1600, 2000,
                                      2500, 3150, 4000, 5000, 6300, 8000, 10000, 12500, 16000, 18000, 20000, 22000, 25000, 30000 };
    juce::String csv ("ips,head_bump_pct,freq_hz,response_db\n");
    double bumpHz[3] {}, bumpDb[3] {}, hf1[3] {}, hf3[3] {};
    for (int s = 0; s < 3; ++s)
        for (float bump : { 0.0f, 18.0f, 36.0f, 100.0f })
        {
            auto p = makeConfigured (fs, { { tp::speed, (float) s }, { tp::headBump, bump } });
            const auto r = measureResponse (*p, fs);
            const double ref = r.db (1000.0);
            for (double f : freqs)
                csv << speedName (s) << "," << bump << "," << f << "," << juce::String (r.db (f) - ref, 3) << "\n";
            // bump: the maximum between 15 and 200 Hz (1/24-octave scan)
            double bestF = 0, best = -100;
            for (double f = 15.0; f < 250.0; f *= std::pow (2.0, 1.0 / 24.0))
                if (r.db (f) - ref > best) { best = r.db (f) - ref; bestF = f; }
            const auto tag = juce::String (speedName (s)) + " IPS, HEAD BUMP " + juce::String (bump) + " %";
            if (bump == 36.0f)
            {
                bumpHz[s] = bestF; bumpDb[s] = best;
                MEASURE (tag + ": bump frequency (Hz)", bestF);
                MEASURE (tag + ": bump height (dB re 1 kHz)", best);
                // HF extension: -1 dB and -3 dB points above 2 kHz
                for (double f = 2000.0; f < 0.45 * fs; f *= std::pow (2.0, 1.0 / 48.0))
                {
                    if (hf1[s] == 0 && r.db (f) - ref < -1.0) hf1[s] = f;
                    if (hf3[s] == 0 && r.db (f) - ref < -3.0) { hf3[s] = f; break; }
                }
                if (hf1[s] == 0) hf1[s] = 0.45 * fs; // flatter than -1 dB up to the measuring limit
                if (hf3[s] == 0) hf3[s] = 0.45 * fs;
                MEASURE (tag + ": -1 dB point (Hz)", hf1[s]);
                MEASURE (tag + ": -3 dB point (Hz)", hf3[s]);
                // A800 published response (R1): +-2 dB windows 30 Hz-15 kHz (7.5), 30 Hz-20 kHz (15), 50 Hz-20 kHz (30)
                const double lo = s == 2 ? 50.0 : 30.0, hi = s == 0 ? 15000.0 : 20000.0;
                double worst = 0.0;
                for (double f = lo; f <= hi; f *= std::pow (2.0, 1.0 / 12.0))
                    worst = std::max (worst, std::abs (r.db (f) - ref));
                MEASURE (tag + ": worst deviation inside the A800 +-2 dB window (dB)", worst);
                CHECK_MSG (worst <= 2.0, tag + ": outside the published +-2 dB window");
                // and +-1 dB from 60 Hz to 12 / 18 / 20 kHz
                double worst1 = 0.0;
                for (double f = 60.0; f <= (s == 0 ? 12000.0 : (s == 1 ? 18000.0 : 20000.0)); f *= std::pow (2.0, 1.0 / 12.0))
                    worst1 = std::max (worst1, std::abs (r.db (f) - ref));
                MEASURE (tag + ": worst deviation inside the A800 +-1 dB window (dB)", worst1);
                CHECK_MSG (worst1 <= 2.0, tag); // the bump itself is up to +2.3 dB at 30 IPS by design (HEAD BUMP 36 %)
            }
            if (bump == 18.0f)
            {
                // HEAD BUMP 18 % = the A800 as published: +-1 dB 60 Hz - 12 / 18 / 20 kHz (owner decision D8)
                double worst1 = 0.0;
                for (double f = 60.0; f <= (s == 0 ? 12000.0 : (s == 1 ? 18000.0 : 20000.0)); f *= std::pow (2.0, 1.0 / 12.0))
                    worst1 = std::max (worst1, std::abs (r.db (f) - ref));
                MEASURE (tag + ": worst deviation inside the A800 +-1 dB window (dB)", worst1);
                CHECK_MSG (worst1 <= 1.0, tag);
            }
            if (bump == 0.0f)
            {
                double flat = 0.0;
                for (double f = 100.0; f <= 5000.0; f *= 1.2)
                    flat = std::max (flat, std::abs (r.db (f) - ref));
                CHECK_MSG (flat < 0.3, tag + ": HEAD BUMP 0 is flat 100 Hz - 5 kHz");
            }
            if (bump == 100.0f)
            {
                MEASURE (tag + ": bump height (dB)", best);
                CHECK_MSG (best > 3.0, tag);
            }
        }
    outFile ("TAPE_frequency_response.csv").replaceWithText (csv);
    // bump frequency proportional to speed (one wavelength): 27 / 54 / 109 Hz
    CHECK_NEAR (bumpHz[1] / bumpHz[0], 2.0, 0.15);
    CHECK_NEAR (bumpHz[2] / bumpHz[1], 2.0, 0.15);
    CHECK_NEAR (bumpHz[1], 54.4, 6.0);
    CHECK (bumpDb[2] > bumpDb[1] && bumpDb[1] > bumpDb[0]); // the bump grows with speed
    CHECK (hf3[2] > hf3[1] && hf3[1] > hf3[0]);             // HF extension grows with speed (30 IPS: beyond 43 kHz)
    CHECK (hf1[0] >= 12000.0 && hf1[1] >= 18000.0);
}

TEST ("TAPE: LF and HF repro trims: +-6 dB shelves at 100 Hz and at the EQ turnover, linear in the control, neutral at 0, mid-band untouched")
{
    const double fs = 48000.0;
    auto flat = makeConfigured (fs, { { tp::headBump, 0.0f } });
    const auto r0 = measureResponse (*flat, fs);
    juce::String csv ("control,value,freq_hz,delta_db\n");
    for (auto [id, v] : { std::pair { tp::lf, 100.0f }, std::pair { tp::lf, -100.0f }, std::pair { tp::lf, 50.0f },
                          std::pair { tp::hf, 100.0f }, std::pair { tp::hf, -100.0f }, std::pair { tp::hf, 50.0f } })
    {
        auto p = makeConfigured (fs, { { tp::headBump, 0.0f }, { id, v } });
        const auto r = measureResponse (*p, fs);
        auto delta = [&] (double f) { return r.db (f) - r0.db (f); };
        for (double f : { 20.0, 30.0, 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 4550.0, 8000.0, 12000.0, 16000.0, 20000.0 })
            csv << id << "," << v << "," << f << "," << juce::String (delta (f), 3) << "\n";
        const double expect = v * 0.06;
        const auto tag = juce::String (id) + " " + juce::String (v);
        if (juce::String (id) == tp::lf)
        {
            MEASURE (tag + ": change at 25 Hz (dB)", delta (25.0));
            CHECK_MSG (std::abs (delta (25.0) - expect) < 0.12 * std::abs (expect) + 0.1, tag);
            CHECK_MSG (std::abs (delta (100.0) - 0.5 * expect) < 0.12 * std::abs (expect), tag + ": half at the corner");
            CHECK_MSG (std::abs (delta (2000.0)) < 0.1 && std::abs (delta (10000.0)) < 0.1, tag + ": no HF change");
        }
        else
        {
            MEASURE (tag + ": change at 16 kHz (dB)", delta (16000.0));
            CHECK_MSG (std::abs (delta (16000.0) - expect) < 0.12 * std::abs (expect) + 0.1, tag);
            CHECK_MSG (std::abs (delta (4547.0) - 0.5 * expect) < 0.12 * std::abs (expect), tag + ": half at the 35 us turnover");
            CHECK_MSG (std::abs (delta (100.0)) < 0.1 && std::abs (delta (500.0)) < 0.2, tag + ": no LF change");
        }
        // the UI evaluator shows the same
        for (double f : { 25.0, 1000.0, 16000.0 })
            CHECK_NEAR (p->tapeResponseDb (f) - flat->tapeResponseDb (f), delta (f), 0.15);
    }
    outFile ("TAPE_trims.csv").replaceWithText (csv);
}

TEST ("TAPE: BIAS sweep: under-bias = more treble, more distortion, less sensitivity; over-bias = darker and cleaner; +30 % overbias costs 5 / 2.5 / 1.25 dB at 10 kHz at 7.5 / 15 / 30 IPS (CSV)")
{
    const double fs = 48000.0;
    juce::String csv ("ips,bias_pct,hf10k_re_1k_db,sens_1k_db,thd_0vu_pct,thd_plus6vu_pct,h3_plus6vu_pct\n");
    for (int s = 0; s < 3; ++s)
    {
        std::map<int, double> hf, sens, thd0, thd6, abs10k;
        double ref1k = 0.0, refHf = 0.0;
        for (int b : { -80, -60, -30, -15, 0, 15, 30, 60, 100 })
        {
            auto p = makeConfigured (fs, { { tp::speed, (float) s }, { tp::bias, (float) b }, { tp::headBump, 0.0f } });
            const auto r = measureResponse (*p, fs);
            if (b == 0) { ref1k = r.db (1000.0); refHf = r.db (10000.0); }
            abs10k[b] = r.db (10000.0);
            hf[b] = r.db (10000.0) - r.db (1000.0);
            sens[b] = r.db (1000.0);
            p->reset();
            thd0[b] = toneThrough (*p, fs, 0.0).thd;
            const auto h6 = toneThrough (*p, fs, 6.0);
            thd6[b] = h6.thd;
            csv << speedName (s) << "," << b << "," << juce::String (hf[b], 3) << "," << juce::String (sens[b], 3) << ","
                << juce::String (100 * thd0[b], 4) << "," << juce::String (100 * h6.thd, 4) << "," << juce::String (100 * h6.h3, 4) << "\n";
        }
        const auto tag = juce::String (speedName (s)) + " IPS";
        // "overbias" is read on the 10 kHz output itself (R2): its drop from the optimum
        const double over30 = refHf - abs10k[30];
        MEASURE (tag + ": 10 kHz loss for +30 % overbias (dB)", over30);
        CHECK_NEAR (over30, s == 0 ? 5.0 : (s == 1 ? 2.5 : 1.25), s == 0 ? 0.6 : 0.35);
        // monotonic: more bias = darker
        CHECK_MSG (hf[-60] > hf[-30] && hf[-30] > hf[0] && hf[0] > hf[30] && hf[30] > hf[60] && hf[60] > hf[100], tag + ": treble falls with bias");
        // under-bias: more distortion and a sensitivity loss; over-bias: less distortion
        CHECK_MSG (thd6[-60] > 1.5 * thd6[0] && thd6[-30] > thd6[0], tag + ": under-bias distorts more");
        CHECK_MSG (thd6[60] < thd6[0] && thd6[100] < thd6[30], tag + ": over-bias distorts less");
        CHECK_MSG (sens[-60] - ref1k < -1.0 && sens[-30] - ref1k < -0.2, tag + ": under-bias loses sensitivity");
        CHECK_MSG (std::abs (sens[30] - ref1k) < 0.3, tag + ": over-bias sensitivity loss is small at 1 kHz");
        if (s == 1)
        {
            MEASURE ("15 IPS: THD at +6 VU, BIAS -60 / 0 / +60 (%)", 100 * thd6[-60]);
            MEASURE ("15 IPS: THD at +6 VU, BIAS 0 (%)", 100 * thd6[0]);
            MEASURE ("15 IPS: THD at +6 VU, BIAS +60 (%)", 100 * thd6[60]);
            MEASURE ("15 IPS: sensitivity at BIAS -60 re optimum (dB)", sens[-60] - ref1k);
        }
    }
    outFile ("TAPE_bias_sweep.csv").replaceWithText (csv);
}

TEST ("TAPE: THD and THD+N vs level for every CALIBRATION and TAPE TYPE: MOL (H3 = 3 %) lands at the type's MOL minus CAL; distortion is odd (H3) dominated and grows with level (CSV)")
{
    const double fs = 48000.0;
    juce::String csv ("type,cal_db,level_vu,thd_pct,thdn_pct,h2_pct,h3_pct,h5_pct\n");
    juce::String molCsv ("type,cal_db,mol_expected_vu,mol_measured_vu\n");
    for (int t = 0; t < 3; ++t)
        for (int cal = 0; cal < 4; ++cal)
        {
            auto p = makeConfigured (fs, { { tp::tapeType, (float) t }, { tp::calibration, (float) cal } });
            const double calDb = TapeEngine::calibrationDb[cal];
            const double expected = tmod::typeSpec (t).molDb - calDb;
            std::vector<std::pair<double, double>> h3;
            double prevThd = 0.0;
            bool rising = true;
            for (double lv = -24.0; lv <= 24.0; lv += 1.0)
            {
                const bool listed = std::fmod (lv + 24.0, 3.0) == 0.0;
                const bool nearMol = std::abs (lv - expected) <= 3.0;
                if (! listed && ! nearMol) continue;
                p->reset();
                const auto h = toneThrough (*p, fs, lv, 1000.0, 0.3);
                h3.push_back ({ lv, h.h3 });
                if (listed)
                {
                    csv << typeName (t) << "," << calDb << "," << lv << "," << juce::String (100 * h.thd, 4) << "," << juce::String (100 * h.thdN, 4) << ","
                        << juce::String (100 * h.h2, 5) << "," << juce::String (100 * h.h3, 4) << "," << juce::String (100 * h.h[5] / h.fundamental, 4) << "\n";
                    if (lv >= -6.0 && lv <= 15.0)
                    {
                        if (h.thd < prevThd * 0.98) rising = false;
                        prevThd = h.thd;
                        CHECK_MSG (h.h3 > 5.0 * h.h2, juce::String ("odd-order dominated: ") + typeName (t) + " " + juce::String (lv));
                        CHECK_MSG (h.thdN < 1.2 * h.thd + 1.0e-4, "THD+N ~ THD with NOISE off");
                    }
                }
            }
            // MOL: interpolate the 3 % crossing of H3 (log domain)
            double mol = 99.0;
            std::sort (h3.begin(), h3.end());
            for (size_t i = 1; i < h3.size(); ++i)
                if (h3[i - 1].second < 0.03 && h3[i].second >= 0.03)
                {
                    const double a = std::log (h3[i - 1].second), b = std::log (h3[i].second), c = std::log (0.03);
                    mol = h3[i - 1].first + (c - a) / (b - a) * (h3[i].first - h3[i - 1].first);
                    break;
                }
            const auto tag = juce::String ("type ") + typeName (t) + ", CAL " + juce::String (calDb) + " dB";
            MEASURE (tag + ": MOL (VU)", mol);
            molCsv << typeName (t) << "," << calDb << "," << expected << "," << juce::String (mol, 3) << "\n";
            CHECK_MSG (std::abs (mol - expected) <= 0.7, tag + ": MOL " + juce::String (mol) + " vs " + juce::String (expected));
            CHECK_MSG (rising, tag + ": THD rises with level");
        }
    outFile ("TAPE_thd_vs_level.csv").replaceWithText (csv);
    outFile ("TAPE_mol.csv").replaceWithText (molCsv);
}

TEST ("TAPE: harmonic spectrum: symmetric (odd only) when the heads are clean, even orders from head magnetisation with AGE, CAL trades headroom for drive, SATURATION is level-neutral (CSV)")
{
    const double fs = 48000.0;
    juce::String csv ("case,h2_db,h3_db,h4_db,h5_db,h6_db,h7_db,h9_db,fund_db\n");
    auto row = [&] (const juce::String& name, const Harmonics& h)
    {
        csv << name << "," << juce::String (dbOf (h.h[2] / h.fundamental), 2);
        for (int k : { 3, 4, 5, 6, 7, 9 })
            csv << "," << juce::String (dbOf (h.h[(size_t) k] / h.fundamental), 2);
        csv << "," << juce::String (dbOf (h.fundamental / zeroVu), 3) << "\n";
    };
    for (int t = 0; t < 3; ++t)
    {
        auto p = makeConfigured (fs, { { tp::tapeType, (float) t } });
        const auto h = toneThrough (*p, fs, 6.0);
        row (juce::String ("type ") + typeName (t) + " +6 VU AGE 0", h);
        // clean heads: the model is odd-symmetric: no even harmonics at all
        CHECK_MSG (h.h[2] / h.fundamental < 1.0e-5 && h.h[4] / h.fundamental < 1.0e-5, juce::String ("even orders at AGE 0, type ") + typeName (t));
        CHECK (h.h3 > 0.002);
        // symmetry of the transfer: an inverted input gives an exactly inverted output
        auto q = makeConfigured (fs, { { tp::tapeType, (float) t } });
        const int n = 9600;
        auto x = toBuffer (sine (fs, 1000.0, (float) (zeroVu * 2.0), n), 2);
        auto xi = x;
        xi.applyGain (-1.0f);
        p->reset();
        const auto y = render (*p, x, 256);
        const auto yi = render (*q, xi, 256);
        double err = 0.0, ref = 0.0;
        for (int i = 0; i < n; ++i)
        {
            err = std::max (err, (double) std::abs (y.getSample (0, i) + yi.getSample (0, i)));
            ref = std::max (ref, (double) std::abs (y.getSample (0, i)));
        }
        CHECK_MSG (err < 1.0e-5 * ref, "odd symmetry of the transfer");
    }
    for (float age : { 50.0f, 100.0f })
    {
        auto p = makeConfigured (fs, { { tp::age, age } });
        const auto h = toneThrough (*p, fs, 6.0);
        row ("type A +6 VU AGE " + juce::String (age), h);
        MEASURE ("H2 at +6 VU, AGE " + juce::String (age) + " % (dBc)", dbOf (h.h2));
        CHECK (h.h2 > 1.0e-3);   // even orders appear (head magnetisation)
        CHECK (h.h2 < h.h3);     // but tape stays odd-dominated
    }
    // CAL: +6 puts 6 dB more flux on tape for 0 VU: more distortion at the same VU, same small-signal level
    {
        auto c0 = makeConfigured (fs, { { tp::calibration, 1.0f } });
        auto c6 = makeConfigured (fs, { { tp::calibration, 3.0f } });
        auto cm3 = makeConfigured (fs, { { tp::calibration, 0.0f } });
        const auto h0 = toneThrough (*c0, fs, 3.0), h6 = toneThrough (*c6, fs, 3.0), hm3 = toneThrough (*cm3, fs, 3.0);
        row ("type A +3 VU CAL 0", h0);
        row ("type A +3 VU CAL +6", h6);
        row ("type A +3 VU CAL -3", hm3);
        CHECK (h6.h3 > 1.6 * h0.h3 && h0.h3 > 1.3 * hm3.h3);
        c0->reset(); c6->reset();
        const auto s0 = toneThrough (*c0, fs, -30.0), s6 = toneThrough (*c6, fs, -30.0);
        CHECK_NEAR (dbOf (s6.fundamental / s0.fundamental), 0.0, 0.1); // repro make-up: operating level stays at 0 VU
    }
    // SATURATION: more drive, same small-signal level
    {
        auto lo = makeConfigured (fs, { { tp::saturation, 10.0f } });
        auto hi = makeConfigured (fs, { { tp::saturation, 90.0f } });
        const auto hl = toneThrough (*lo, fs, 0.0), hh = toneThrough (*hi, fs, 0.0);
        row ("type A 0 VU SAT 10", hl);
        row ("type A 0 VU SAT 90", hh);
        MEASURE ("THD at 0 VU, SATURATION 10 % (%)", 100 * hl.thd);
        MEASURE ("THD at 0 VU, SATURATION 90 % (%)", 100 * hh.thd);
        CHECK (hh.thd > 10.0 * hl.thd);
        lo->reset(); hi->reset();
        const auto sl = toneThrough (*lo, fs, -30.0), sh = toneThrough (*hi, fs, -30.0);
        CHECK_NEAR (dbOf (sh.fundamental / sl.fundamental), 0.0, 0.25);
    }
    outFile ("TAPE_harmonics.csv").replaceWithText (csv);
}

TEST ("TAPE: Jiles-Atherton physics: at c -> 1 the layer follows the anhysteretic curve L(h + alpha m) (the alpha feedback is exact); below it, a symmetric loop with remanence and coercivity that shrink as bias (c) rises")
{
    const auto& tb = tmod::typeSpec (1);
    // anhysteretic limit
    {
        JAParams pr { tb.kPrime, tb.alphaPrime, 0.99999 };
        HysteresisSolver j;
        double worst = 0.0;
        const int steps = 40000;
        for (int i = 1; i <= steps; ++i)
        {
            const double h = 4.0 * i / steps;
            const double m = j.process<4> (h, pr);
            double man = 0.0;
            for (int it = 0; it < 200; ++it)
            {
                double L, Lp;
                tleet::tape::ja::langevin (h + tb.alphaPrime * man, L, Lp);
                man = L;
            }
            if (i > steps / 20) worst = std::max (worst, std::abs (m - man) / man);
        }
        MEASURE ("J-A at c = 0.99999: worst relative distance from the anhysteretic curve", worst);
        CHECK (worst < 1.0e-4); // the irreversible share is 1e-5; without the alpha feedback the error is ~1e-2
    }
    // loops
    double area[2] {}, rem[2] {}, coer[2] {};
    int idx = 0;
    for (double c : { 0.6, tb.cOpt })
    {
        JAParams pr { tb.kPrime, tb.alphaPrime, c };
        HysteresisSolver j;
        const int per = 2000;
        std::vector<double> hs, ms;
        for (int i = 0; i < 4 * per; ++i)
        {
            const double h = 3.0 * std::sin (2 * pi * i / per);
            const double m = j.process<4> (h, pr);
            if (i >= 3 * per) { hs.push_back (h); ms.push_back (m); }
        }
        double A = 0.0;
        for (size_t i = 1; i < hs.size(); ++i)
        {
            A += 0.5 * (ms[i] + ms[i - 1]) * (hs[i] - hs[i - 1]);
            if (hs[i - 1] > 0 && hs[i] <= 0 && ms[i] > 0) rem[idx] = ms[i];                  // descending through h = 0
            if (ms[i - 1] > 0 && ms[i] <= 0 && hs[i] < 0) coer[idx] = -hs[i];                 // descending through m = 0
        }
        area[idx] = std::abs (A);
        // odd symmetry of the steady loop: m(t + T/2) = -m(t)
        double sym = 0.0;
        for (size_t i = 0; i < (size_t) per / 2; ++i)
            sym = std::max (sym, std::abs (ms[i] + ms[i + (size_t) per / 2]));
        CHECK (sym < 1.0e-6);
        MEASURE ("J-A loop c = " + juce::String (c) + ": remanence m_r", rem[idx]);
        MEASURE ("J-A loop c = " + juce::String (c) + ": coercive field h_c", coer[idx]);
        MEASURE ("J-A loop c = " + juce::String (c) + ": loop area", area[idx]);
        ++idx;
    }
    CHECK (rem[0] > 0.0 && coer[0] > 0.0 && rem[1] > 0.0 && coer[1] > 0.0);
    CHECK (area[1] < 0.6 * area[0]);
    CHECK (coer[1] < coer[0]);
}

TEST ("TAPE: TAPE SPEED sets the HF headroom: the record pre-emphasis (70 / 35 / 17.5 us) drives treble harder at low speed, so 10 kHz compresses 3 dB earlier per halving of speed; 1 kHz headroom barely moves (CSV)")
{
    const double fs = 48000.0;
    juce::String csv ("ips,freq_hz,level_vu,compression_db\n");
    double p1[3] {}, p1k[3] {};
    for (int s = 0; s < 3; ++s)
        for (double hz : { 10000.0, 1000.0 })
        {
            auto p = makeConfigured (fs, { { tp::speed, (float) s } });
            const double ref = dbOf (toneThrough (*p, fs, -24.0, hz, 0.2).fundamental / zeroVu) + 24.0;
            double point = 99.0;
            for (double lv = -18.0; lv <= 24.0; lv += 0.5)
            {
                p->reset();
                const double c = ref + lv - dbOf (toneThrough (*p, fs, lv, hz, 0.2).fundamental / zeroVu);
                if (std::fmod (lv, 3.0) == 0.0) csv << speedName (s) << "," << hz << "," << lv << "," << juce::String (c, 3) << "\n";
                if (c >= 1.0) { point = lv; break; }
            }
            (hz == 1000.0 ? p1k : p1)[s] = point;
            MEASURE (juce::String (speedName (s)) + " IPS: 1 dB compression of a " + juce::String (hz / 1000.0) + " kHz tone (VU)", point);
        }
    CHECK (p1[1] - p1[0] > 2.0 && p1[1] - p1[0] < 4.5);
    CHECK (p1[2] - p1[1] > 2.0 && p1[2] - p1[1] < 4.5);
    CHECK (std::abs (p1k[2] - p1k[0]) < 1.0);
    outFile ("TAPE_hf_headroom.csv").replaceWithText (csv);
}

TEST ("TAPE: aliasing: the fixed 176.4 / 192 kHz top rate keeps a hot 6.1 kHz tone's aliases far below 1x and 2x processing; RK4 at the top rate matches a 16x reference (CSV)")
{
    const double fs = 48000.0, hz = 6100.0;
    juce::String csv ("oversampling,level_vu,alias_dbc,harmonics_dbc\n");
    double alias[3] {};
    for (int o = 0; o < 3; ++o)
    {
        auto p = makeConfigured (fs, { { tp::saturation, 70.0f } }, 256, 2, true, o);
        CHECK (p->getEngineForTesting().oversamplingFactor() == (1 << o));
        const int nfft = 1 << 15, n = nfft + 9600;
        const auto in = toBuffer (sine (fs, hz, (float) (zeroVu * std::pow (10.0, 9.0 / 20.0)), n), 1);
        juce::AudioBuffer<float> st (2, n);
        st.copyFrom (0, 0, in, 0, 0, n);
        st.copyFrom (1, 0, in, 0, 0, n);
        const auto out = render (*p, st, 256);
        std::vector<std::complex<double>> X ((size_t) nfft);
        for (int i = 0; i < nfft; ++i)
            X[(size_t) i] = out.getSample (0, n - nfft + i) * (0.5 - 0.5 * std::cos (2 * pi * i / nfft)) ;
        fft (X, false);
        double fund = 0, harm = 0, rest = 0;
        for (int b = 3; b < nfft / 2; ++b)
        {
            const double f = (double) b * fs / nfft, pw = std::norm (X[(size_t) b]);
            const double k = f / hz, near = std::abs (k - std::round (k)) * hz;
            if (near < 30.0 && std::round (k) == 1.0) fund += pw;
            else if (near < 30.0) harm += pw;
            else if (f > 20.0) rest += pw;
        }
        alias[o] = 10 * std::log10 (rest / fund);
        const auto tag = juce::String (1 << o) + "x";
        MEASURE ("alias energy re fundamental, 6.1 kHz at +9 VU, SAT 70, " + tag + " (dBc)", alias[o]);
        csv << (1 << o) << ",9," << juce::String (alias[o], 2) << "," << juce::String (10 * std::log10 (harm / fund), 2) << "\n";
    }
    CHECK (alias[2] < alias[0] - 20.0);
    CHECK (alias[2] < alias[1] - 6.0);
    CHECK (alias[2] < -75.0);
    outFile ("TAPE_aliasing.csv").replaceWithText (csv);

    // solver accuracy: RK4 and RK2 at 192 kHz against RK4 at 16 x 192 kHz, a hot 1 kHz field (type B, optimum bias)
    juce::String sc ("solver,amplitude,rms_error_db\n");
    for (double amp : { 0.5, 2.0, 6.0 })
    {
        JAParams pr { tmod::typeSpec (1).kPrime, tmod::typeSpec (1).alphaPrime, tmod::typeSpec (1).cOpt };
        HysteresisSolver ref, r4, r2;
        const double top = 192000.0;
        double e4 = 0, e2 = 0, sr = 0;
        const int steps = (int) top / 100; // 10 ms
        double hPrev = 0.0;
        for (int i = 1; i <= steps; ++i)
        {
            const double h = amp * std::sin (2 * pi * 1000.0 * i / top);
            for (int s = 1; s <= 16; ++s)
                ref.process<4> (hPrev + (h - hPrev) * s / 16.0, pr);
            hPrev = h;
            const double a = r4.process<4> (h, pr), b = r2.process<2> (h, pr), m = ref.magnetisation();
            if (i > steps / 2) { e4 += (a - m) * (a - m); e2 += (b - m) * (b - m); sr += m * m; }
        }
        const double d4 = 10 * std::log10 (e4 / sr), d2 = 10 * std::log10 (e2 / sr);
        sc << "rk4," << amp << "," << d4 << "\nrk2," << amp << "," << d2 << "\n";
        MEASURE ("JA solver error at 192 kHz, field " + juce::String (amp) + ": RK4 (dB re signal)", d4);
        MEASURE ("JA solver error at 192 kHz, field " + juce::String (amp) + ": RK2 (dB re signal)", d2);
        CHECK (d4 < -100.0);
        if (amp >= 2.0)
            CHECK (d4 < d2 - 30.0); // hot fields: RK2 is not enough
    }
    outFile ("TAPE_solver_accuracy.csv").replaceWithText (sc);
}

// ------------------------------------------------------------------ transport, noise, crosstalk, AGE
namespace
{
    // Fractional speed deviation of a played-back carrier (DIN 45507 / IEC 60386 method):
    // quadrature demodulation of the carrier, phase derivative, decimated to 1 kHz.
    std::vector<double> speedDeviation (const float* y, int n, double fs, double carrier)
    {
        const int dec = (int) std::lround (fs / 1000.0);
        std::vector<double> phase;
        double i1 = 0, i2 = 0, q1 = 0, q2 = 0, i3 = 0, q3 = 0, i4 = 0, q4 = 0;
        const double a = 1.0 - std::exp (-2 * pi * 250.0 / fs);
        for (int k = 0; k < n; ++k)
        {
            const double w = 2 * pi * carrier * k / fs;
            i1 += a * (y[k] * std::cos (w) - i1); q1 += a * (-y[k] * std::sin (w) - q1);
            i2 += a * (i1 - i2); q2 += a * (q1 - q2);
            i3 += a * (i2 - i3); q3 += a * (q2 - q3);
            i4 += a * (i3 - i4); q4 += a * (q3 - q4);
            if (k % dec == 0)
                phase.push_back (std::atan2 (q4, i4));
        }
        for (size_t k = 1; k < phase.size(); ++k)
            while (phase[k] - phase[k - 1] > pi) phase[k] -= 2 * pi;
        for (size_t k = 1; k < phase.size(); ++k)
            while (phase[k] - phase[k - 1] < -pi) phase[k] += 2 * pi;
        std::vector<double> dev;
        const double rate = fs / dec;
        for (size_t k = 1; k < phase.size(); ++k)
            dev.push_back ((phase[k] - phase[k - 1]) * rate / (2 * pi) / carrier);
        return dev;
    }

    // IEC 60386 / DIN 45507 / AES6 weighting, fitted exactly to the standard's points (R13):
    // 0.0296 at 0.2 Hz, 0.5 at 0.8 Hz, 1 (the peak) at 4 Hz, 0.508 at 20 Hz:
    // 2nd-order high-pass 0.4345 Hz Q 0.7507, 1st-order high-pass 1.6658 Hz, 1st-order low-pass 9.6728 Hz.
    // Returns the 2-sigma value: the weighted deviation exceeded (either polarity) 5 % of the time.
    double weightedWowFlutter (const std::vector<double>& dev, double rate, int skip)
    {
        struct Bq { double b0, b1, b2, a1, a2, x1 = 0, x2 = 0, y1 = 0, y2 = 0;
                    double run (double x) { const double y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2; x2 = x1; x1 = x; y2 = y1; y1 = y; return y; }
                    std::complex<double> at (double f, double r) const { const auto z = std::polar (1.0, -2 * pi * f / r);
                        return (b0 + b1 * z + b2 * z * z) / (1.0 + a1 * z + a2 * z * z); } };
        auto hp2 = [&] (double f, double q) { const double K = std::tan (pi * f / rate), n = 1 + K / q + K * K;
                                              return Bq { 1 / n, -2 / n, 1 / n, 2 * (K * K - 1) / n, (1 - K / q + K * K) / n }; };
        auto hp1 = [&] (double f) { const double K = std::tan (pi * f / rate); return Bq { 1 / (1 + K), -1 / (1 + K), 0, (K - 1) / (1 + K), 0 }; };
        auto lp1 = [&] (double f) { const double K = std::tan (pi * f / rate); return Bq { K / (1 + K), K / (1 + K), 0, (K - 1) / (1 + K), 0 }; };
        Bq f[3] { hp2 (0.43454, 0.75071), hp1 (1.66584), lp1 (9.67278) };
        const double norm = 1.0 / std::abs (f[0].at (4.0, rate) * f[1].at (4.0, rate) * f[2].at (4.0, rate));
        std::vector<double> w;
        for (size_t k = 0; k < dev.size(); ++k)
        {
            const double y = f[2].run (f[1].run (f[0].run (dev[k]))) * norm;
            if ((int) k >= skip) w.push_back (std::abs (y));
        }
        if (w.empty()) return 0.0;
        std::sort (w.begin(), w.end());
        return w[(size_t) (0.95 * (double) (w.size() - 1))];
    }

    double lineAmplitude (const std::vector<double>& dev, double rate, double hz, int skip)
    {
        const int n = (int) dev.size() - skip;
        double re = 0, im = 0;
        for (int k = 0; k < n; ++k)
        {
            re += dev[(size_t) (k + skip)] * std::cos (2 * pi * hz * k / rate);
            im += dev[(size_t) (k + skip)] * std::sin (2 * pi * hz * k / rate);
        }
        return 2.0 * std::sqrt (re * re + im * im) / n;
    }

    // Power (dB re (0 VU RMS)^2) of a channel in [lo, hi] Hz, Welch average of Hann FFTs. weightA = A-weighting.
    double bandPowerDb (const float* d, int n, double fs, double lo, double hi, bool weightA = false)
    {
        const int N = 1 << 14;
        double acc = 0.0;
        int segs = 0;
        double wsum = 0.0;
        // 4-term Blackman-Harris (-92 dB side lobes): a tone's skirt stays out of neighbouring bands
        auto win = [N] (int i) { const double x = 2 * pi * i / N; return 0.35875 - 0.48829 * std::cos (x) + 0.14128 * std::cos (2 * x) - 0.01168 * std::cos (3 * x); };
        for (int i = 0; i < N; ++i) { const double w = win (i); wsum += w * w; }
        for (int s = 0; s + N <= n; s += N / 2)
        {
            std::vector<std::complex<double>> X ((size_t) N);
            for (int i = 0; i < N; ++i)
                X[(size_t) i] = d[s + i] * win (i);
            fft (X, false);
            for (int b = 1; b < N / 2; ++b)
            {
                const double f = (double) b * fs / N;
                if (f < lo || f > hi) continue;
                double g = 1.0;
                if (weightA)
                {
                    const double f2 = f * f;
                    const double ra = 12194.0 * 12194.0 * f2 * f2 / ((f2 + 20.6 * 20.6) * std::sqrt ((f2 + 107.7 * 107.7) * (f2 + 737.9 * 737.9)) * (f2 + 12194.0 * 12194.0));
                    g = ra * ra * std::pow (10.0, 2.0 / 10.0);
                }
                acc += 2.0 * std::norm (X[(size_t) b]) * g / (wsum * N);
            }
            ++segs;
        }
        const double zr = tmod::zeroVuRms;
        return 10.0 * std::log10 (std::max (acc / std::max (segs, 1), 1.0e-30) / (zr * zr));
    }

    double tonePhase (const float* d, int n, double fs, double hz)
    {
        double re = 0, im = 0;
        for (int i = 0; i < n; ++i)
        {
            re += d[i] * std::cos (2 * pi * hz * i / fs);
            im -= d[i] * std::sin (2 * pi * hz * i / fs);
        }
        return std::atan2 (im, re);
    }
}

TEST ("TAPE: wow and flutter: weighted 2-sigma per speed meets the A800 spec at the defaults, rotating-part lines scale with speed, depth follows WOW / FLUTTER, zero at 0 (CSV spectrum)")
{
    const double fs = 48000.0, carrier = 3150.0, seconds = 16.0;
    const int n = (int) (seconds * fs), skip = 2000; // 2 s settle at 1 kHz
    const auto x = toBuffer (sine (fs, carrier, (float) (zeroVu * 0.3), n), 2);
    juce::String csv ("ips,setting,line_hz,peak_deviation_pct\n");
    juce::String spec ("ips,freq_hz,deviation_pct_rms\n");
    // zero: no deviation at all
    {
        auto p = makeConfigured (fs, {});
        const auto y = render (*p, toBuffer (sine (fs, carrier, (float) (zeroVu * 0.3), (int) fs * 3), 2), 512);
        const auto dev = speedDeviation (y.getReadPointer (0), y.getNumSamples(), fs, carrier);
        double mx = 0;
        for (size_t k = 500; k < dev.size(); ++k) mx = std::max (mx, std::abs (dev[k]));
        MEASURE ("residual speed deviation at WOW = FLUTTER = AGE = 0 (%)", 100 * mx);
        CHECK (mx < 2.0e-6);
        CHECK (p->getEngineForTesting().transportDeviationSeconds() == 0.0);
    }
    double wf[3] {}, capHz[3] {};
    for (int s = 0; s < 3; ++s)
    {
        const double v = tmod::speedIps[s] * 0.0254;
        const double fSup = v / (2 * pi * 0.110), fTak = v / (2 * pi * 0.075), fCap = v / (pi * 0.028), fRol = v / (pi * 0.015);
        capHz[s] = fCap;
        // defaults (AGE 18 %, WOW 6 %, FLUTTER 4 %)
        auto p = makeConfigured (fs, { { tp::speed, (float) s } }, 512, 2, false);
        setParam (*p, tp::noise, 0.0f); setParam (*p, tp::crosstalk, 0.0f);
        p->prepareToPlay (fs, 512);
        const auto y = render (*p, x, 512);
        const auto dev = speedDeviation (y.getReadPointer (0), n, fs, carrier);
        wf[s] = weightedWowFlutter (dev, 1000.0, skip);
        const double spec800 = s == 0 ? 0.0006 : 0.0004;
        MEASURE (juce::String (speedName (s)) + " IPS defaults: weighted W&F 2-sigma (%)", 100 * wf[s]);
        CHECK_MSG (wf[s] <= spec800, juce::String (speedName (s)) + " IPS: A800 weighted W&F spec");
        CHECK_MSG (wf[s] > 0.25 * spec800, "the defaults are an audible-in-principle machine, not a perfect one");
        // deviation spectrum (rms per 1/2 Hz bin)
        {
            const int N = 8192;
            std::vector<std::complex<double>> X ((size_t) N);
            for (int k = 0; k < N; ++k)
                X[(size_t) k] = dev[(size_t) (skip + k)] * (0.5 - 0.5 * std::cos (2 * pi * k / N));
            fft (X, false);
            for (int b = 1; b < N / 2 && b * 1000.0 / N <= 50.0; ++b)
                spec << speedName (s) << "," << juce::String (b * 1000.0 / N, 3) << "," << juce::String (100.0 * 2.0 * std::abs (X[(size_t) b]) / (0.5 * N) / std::sqrt (2.0), 6) << "\n";
        }
        // the lines: FLUTTER 100 % alone -> capstan 0.30 % and roller 0.10 % peak (x1.5 at 7.5 IPS); WOW 100 % -> reels 0.25 / 0.10 %
        auto q = makeConfigured (fs, { { tp::speed, (float) s }, { tp::flutter, 100.0f } }, 512);
        const auto yq = render (*q, x, 512);
        const auto dq = speedDeviation (yq.getReadPointer (0), n, fs, carrier);
        const double fl = s == 0 ? 1.5 : 1.0;
        const double cap = lineAmplitude (dq, 1000.0, fCap, skip), rol = lineAmplitude (dq, 1000.0, fRol, skip);
        csv << speedName (s) << ",FLUTTER 100," << fCap << "," << 100 * cap << "\n" << speedName (s) << ",FLUTTER 100," << fRol << "," << 100 * rol << "\n";
        CHECK_NEAR (cap, 0.0030 * fl, 0.0003 * fl);
        CHECK_NEAR (rol, 0.0010 * fl, 0.00015 * fl);
        auto w = makeConfigured (fs, { { tp::speed, (float) s }, { tp::wow, 50.0f } }, 512);
        const auto yw = render (*w, x, 512);
        const auto dw = speedDeviation (yw.getReadPointer (0), n, fs, carrier);
        const double sup = lineAmplitude (dw, 1000.0, fSup, skip), tak = lineAmplitude (dw, 1000.0, fTak, skip);
        csv << speedName (s) << ",WOW 50," << fSup << "," << 100 * sup << "\n" << speedName (s) << ",WOW 50," << fTak << "," << 100 * tak << "\n";
        MEASURE (juce::String (speedName (s)) + " IPS WOW 50 %: supply reel line at " + juce::String (fSup, 2) + " Hz (%)", 100 * sup);
        // reels are slow: the 14 s window resolves them only roughly at 7.5 IPS
        CHECK_NEAR (sup, 0.00125, 0.00125 * (s == 0 ? 0.35 : 0.2));
        CHECK (wf[s] > 0.0);
        // the left and right channels share the transport
        const auto dr = speedDeviation (yq.getReadPointer (1), n, fs, carrier);
        double diff = 0;
        for (size_t k = (size_t) skip; k < dq.size(); ++k) diff = std::max (diff, std::abs (dr[k] - dq[k]));
        CHECK (diff < 1.0e-5);
    }
    CHECK_NEAR (capHz[1], 4.33, 0.01);
    CHECK_NEAR (capHz[2] / capHz[0], 4.0, 1e-9);
    outFile ("TAPE_wow_flutter_lines.csv").replaceWithText (csv);
    outFile ("TAPE_wow_flutter_spectrum.csv").replaceWithText (spec);
}

TEST ("TAPE: NOISE: off is silent, on = the A800 S/N per speed re the CAL operating level, TAPE TYPE offsets, AMOUNT law (+9 dB / doubling), AGE +6 dB, modulation noise under signal (CSV)")
{
    const double fs = 48000.0;
    const int n = (int) (3.0 * fs);
    const auto quiet = silence (2, n);
    juce::String csv ("type,ips,cal_db,amount_pct,age_pct,noise_unweighted_db_re_0vu,expected_db,a_weighted_db\n");
    auto noiseDb = [&] (std::vector<Setting> s, double* aw = nullptr)
    {
        s.push_back ({ tp::noise, 1.0f });
        auto p = makeConfigured (fs, s);
        const auto y = render (*p, quiet, 512);
        const int from = (int) fs;
        if (aw != nullptr) *aw = bandPowerDb (y.getReadPointer (0) + from, n - from, fs, 20.0, 20000.0, true);
        return bandPowerDb (y.getReadPointer (0) + from, n - from, fs, 20.0, 20000.0);
    };
    // off: silence after the settle, with every other control at its default
    {
        auto p = makeConfigured (fs, {}, 512, 2, false);
        const auto y = render (*p, quiet, 512);
        double mx = 0;
        for (int ch = 0; ch < 2; ++ch)
            for (int i = n / 2; i < n; ++i) mx = std::max (mx, (double) std::abs (y.getSample (ch, i)));
        MEASURE ("NOISE off, defaults, silent input: peak output (dBFS)", dbOf (mx));
        CHECK (mx < 1.0e-6);
    }
    for (int t = 0; t < 3; ++t)
        for (int s = 0; s < 3; ++s)
            for (int cal = 0; cal < 4; ++cal)
            {
                if (t != 1 && cal != 1 && s != 1) continue;
                double aw = 0;
                const double got = noiseDb ({ { tp::tapeType, (float) t }, { tp::speed, (float) s }, { tp::calibration, (float) cal } }, &aw);
                const double expected = -(tmod::snrDb (tmod::speedIps[s]) + tmod::typeSpec (t).snrOffsetDb) - TapeEngine::calibrationDb[cal];
                csv << typeName (t) << "," << speedName (s) << "," << TapeEngine::calibrationDb[cal] << ",12,0," << juce::String (got, 2) << ","
                    << juce::String (expected, 2) << "," << juce::String (aw, 2) << "\n";
                const auto tag = juce::String ("type ") + typeName (t) + ", " + speedName (s) + " IPS, CAL " + juce::String (TapeEngine::calibrationDb[cal]);
                CHECK_MSG (std::abs (got - expected) <= 1.0, tag + ": " + juce::String (got) + " vs " + juce::String (expected));
                if (t == 1 && cal == 1)
                {
                    MEASURE (juce::String (speedName (s)) + " IPS, type B, CAL 0: noise re 0 VU, unweighted (dB)", got);
                    MEASURE (juce::String (speedName (s)) + " IPS, type B, CAL 0: noise re 0 VU, A-weighted (dB)", aw);
                    // re the A800's 510 nWb/m reference (R1): 66 / 66 / 70 dB
                    MEASURE (juce::String (speedName (s)) + " IPS: S/N re 510 nWb/m (dB)", -got + 8.81);
                }
            }
    // AMOUNT law
    const double base = noiseDb ({});
    const double dbl = noiseDb ({ { tp::noiseAmount, 24.0f } }), half = noiseDb ({ { tp::noiseAmount, 6.0f } });
    const double full = noiseDb ({ { tp::noiseAmount, 100.0f } });
    MEASURE ("NOISE AMOUNT 100 %: noise re 0 VU (dB)", full);
    CHECK_NEAR (dbl - base, 9.03, 0.3);
    CHECK_NEAR (base - half, 9.03, 0.3);
    CHECK_NEAR (full - base, 30.0 * std::log10 (100.0 / 12.0), 0.5);
    CHECK (noiseDb ({ { tp::noiseAmount, 0.0f } }) < -150.0);
    // AGE: +6 dB in the band where its own spacing loss is small (500 Hz - 2 kHz)
    {
        auto band = [&] (float age)
        {
            auto p = makeConfigured (fs, { { tp::noise, 1.0f }, { tp::age, age }, { tp::wow, 0.0f }, { tp::flutter, 0.0f } });
            const auto y = render (*p, quiet, 512);
            return bandPowerDb (y.getReadPointer (0) + (int) fs, n - (int) fs, fs, 500.0, 2000.0);
        };
        const double d = band (100.0f) - band (0.0f);
        MEASURE ("AGE 100 %: noise increase 0.5-2 kHz (dB)", d);
        CHECK_NEAR (d, 6.0 - 0.3, 0.6); // 6 dB minus the 0.3 dB spacing loss at 1 kHz
    }
    // modulation noise: the floor next to a +6 VU tone rises (noise rides on the signal)
    {
        auto p = makeConfigured (fs, { { tp::noise, 1.0f } });
        const auto y = render (*p, toBuffer (sine (fs, 1000.0, (float) (zeroVu * 2.0), n), 2), 512);
        auto q = makeConfigured (fs, { { tp::noise, 1.0f } });
        const auto z = render (*q, quiet, 512);
        const double withTone = bandPowerDb (y.getReadPointer (0) + (int) fs, n - (int) fs, fs, 1100.0, 1400.0);
        const double without = bandPowerDb (z.getReadPointer (0) + (int) fs, n - (int) fs, fs, 1100.0, 1400.0);
        MEASURE ("modulation noise: floor 1.1-1.4 kHz rises next to a +6 VU 1 kHz tone (dB)", withTone - without);
        CHECK (withTone - without > 1.0);
        // and it is what NOISE adds: no such floor with NOISE off
        auto r = makeConfigured (fs, {});
        const auto u = render (*r, toBuffer (sine (fs, 1000.0, (float) (zeroVu * 2.0), n), 2), 512);
        const double off = bandPowerDb (u.getReadPointer (0) + (int) fs, n - (int) fs, fs, 1100.0, 1400.0);
        MEASURE ("NOISE off: floor 1.1-1.4 kHz next to a +6 VU 1 kHz tone (dB re 0 VU)", off);
        CHECK (off < without - 20.0);
    }
    outFile ("TAPE_noise.csv").replaceWithText (csv);
}

TEST ("TAPE: CROSSTALK matrix: frequency dependent (fringing, long wavelengths), >= 40 dB separation at 100 Hz - 12 kHz at the default 8 % for every speed, -53 dB / -20 dB at 1 kHz, exactly none at 0, symmetric (CSV)")
{
    const double fs = 48000.0;
    juce::String csv ("ips,crosstalk_pct,direction,freq_hz,crosstalk_db\n");
    for (int s = 0; s < 3; ++s)
        for (float x : { 0.0f, 8.0f, 50.0f, 100.0f })
        {
            auto p = makeConfigured (fs, { { tp::speed, (float) s }, { tp::crosstalk, x }, { tp::headBump, 0.0f } });
            const auto direct = measureResponse (*p, fs, 0, -20.0, 0);
            p->reset();
            const auto lToR = measureResponse (*p, fs, 1, -20.0, 0);
            p->reset();
            const auto rToL = measureResponse (*p, fs, 0, -20.0, 1);
            const auto tag = juce::String (speedName (s)) + " IPS CROSSTALK " + juce::String (x) + " %";
            double worst = -300;
            for (double f : { 31.5, 63.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0, 12000.0, 16000.0 })
            {
                const double c = lToR.db (f) - direct.db (f), c2 = rToL.db (f) - direct.db (f);
                csv << speedName (s) << "," << x << ",L>R," << f << "," << juce::String (c, 2) << "\n";
                csv << speedName (s) << "," << x << ",R>L," << f << "," << juce::String (c2, 2) << "\n";
                if (x > 0.0f) CHECK_MSG (std::abs (c - c2) < 0.1, tag + ": symmetric");
                if (f >= 100.0 && f <= 12000.0) worst = std::max (worst, c);
            }
            if (x == 0.0f)
                CHECK_MSG (worst < -200.0, tag + ": none");
            if (x == 8.0f)
            {
                MEASURE (tag + ": worst crosstalk 100 Hz - 12 kHz (dB)", worst);
                CHECK_MSG (worst <= -40.0, tag);
                CHECK_MSG (lToR.db (10000.0) - direct.db (10000.0) < lToR.db (100.0) - direct.db (100.0) - 15.0, tag + ": falls with frequency");
                if (s == 1) CHECK_NEAR (lToR.db (1000.0) - direct.db (1000.0), -52.9, 0.5);
            }
            if (x == 100.0f && s == 1)
                CHECK_NEAR (lToR.db (1000.0) - direct.db (1000.0), -20.0, 0.5);
        }
    // a mono instance has nothing to cross
    auto m = makeConfigured (fs, { { tp::crosstalk, 100.0f } }, 256, 1);
    const auto y = render (*m, toBuffer (sine (fs, 1000.0, (float) zeroVu, 9600), 1), 256);
    CHECK (allFinite (y));
    outFile ("TAPE_crosstalk.csv").replaceWithText (csv);
}

TEST ("TAPE: AGE: one macro for HF loss (wear spacing, right head worse), wow and flutter, noise, L/R level and timing mismatch, wear variation and head magnetisation; 0 = a factory-new machine")
{
    const double fs = 48000.0;
    // HF loss and L/R level by tones, read as band power (+-150 Hz) so that the old machine's
    // flutter sidebands and wear variation are counted, averaged over 8 s
    auto toneDb = [&] (float age, double hz, int ch)
    {
        auto p = makeConfigured (fs, { { tp::headBump, 0.0f }, { tp::age, age } });
        const int n = (int) (9.0 * fs);
        const auto y = render (*p, toBuffer (sine (fs, hz, (float) (zeroVu * 0.1), n), 2), 512);
        return bandPowerDb (y.getReadPointer (ch) + (int) fs, n - (int) fs, fs, hz - 150.0, hz + 150.0);
    };
    const double l1f = toneDb (0.0f, 1000.0, 0), l10f = toneDb (0.0f, 10000.0, 0), r1f = toneDb (0.0f, 1000.0, 1), r10f = toneDb (0.0f, 10000.0, 1);
    const double l1o = toneDb (100.0f, 1000.0, 0), l10o = toneDb (100.0f, 10000.0, 0), r1o = toneDb (100.0f, 1000.0, 1), r10o = toneDb (100.0f, 10000.0, 1);
    const double lossL = (l10f - l1f) - (l10o - l1o), lossR = (r10f - r1f) - (r10o - r1o);
    MEASURE ("AGE 100 %: 10 kHz loss re 1 kHz, left (dB)", lossL);
    MEASURE ("AGE 100 %: 10 kHz loss re 1 kHz, right (dB)", lossR);
    const double lam = 0.381 / 10000.0, lam1 = 0.381 / 1000.0;
    CHECK_NEAR (lossL, 54.6 * 2.0e-6 * (1 / lam - 1 / lam1), 0.3);
    CHECK_NEAR (lossR, 54.6 * 2.5e-6 * (1 / lam - 1 / lam1), 0.3);
    // L/R level mismatch at 1 kHz (+-0.25 dB gain, plus the right head's extra spacing)
    const double mis = l1o - r1o;
    MEASURE ("AGE 100 %: L - R level at 1 kHz (dB)", mis);
    CHECK_NEAR (mis, 0.5 + 54.6 * 0.5e-6 / lam1, 0.1);
    CHECK_NEAR (l1f - r1f, 0.0, 0.001);
    // the UI evaluator shows the same per channel
    {
        auto p = makeConfigured (fs, { { tp::headBump, 0.0f }, { tp::age, 100.0f } });
        render (*p, silence (2, 4096), 512);
        auto f = makeConfigured (fs, { { tp::headBump, 0.0f } });
        render (*f, silence (2, 4096), 512);
        CHECK_NEAR ((p->tapeResponseDb (1000.0, 0) - p->tapeResponseDb (1000.0, 1)), mis, 0.1);
        CHECK_NEAR ((f->tapeResponseDb (10000.0, 0) - f->tapeResponseDb (1000.0, 0)) - (p->tapeResponseDb (10000.0, 0) - p->tapeResponseDb (1000.0, 0)), lossL, 0.3);
    }
    // timing (azimuth): right 20 us later than left
    {
        auto p = makeConfigured (fs, { { tp::age, 100.0f } });
        const int n = (int) (0.5 * fs);
        const auto y = render (*p, toBuffer (sine (fs, 5000.0, (float) (zeroVu * 0.3), n), 2), 256);
        const int len = n / 2;
        double d = tonePhase (y.getReadPointer (0) + n - len, len, fs, 5000.0) - tonePhase (y.getReadPointer (1) + n - len, len, fs, 5000.0);
        while (d > pi) d -= 2 * pi;
        while (d < -pi) d += 2 * pi;
        // right extra spacing adds no phase (linear-phase FIR), so the phase is the azimuth delay
        const double us = d / (2 * pi * 5000.0) * 1.0e6;
        MEASURE ("AGE 100 %: right channel delay re left (us)", us);
        CHECK_NEAR (us, 20.0, 1.5);
        auto q = makeConfigured (fs, {});
        const auto z = render (*q, toBuffer (sine (fs, 5000.0, (float) (zeroVu * 0.3), n), 2), 256);
        CHECK (identical (z, z)); // trivially true; the real check:
        double maxDiff = 0;
        for (int i = 0; i < n; ++i) maxDiff = std::max (maxDiff, (double) std::abs (z.getSample (0, i) - z.getSample (1, i)));
        CHECK_MSG (maxDiff == 0.0, "AGE 0: L and R identical for identical input");
    }
    // wear variation: the 10 kHz envelope fluctuates far more than the 300 Hz one
    auto envelopeSpread = [&] (float age, double hz)
    {
        auto p = makeConfigured (fs, { { tp::age, age }, { tp::headBump, 0.0f } });
        const int n = (int) (6.0 * fs);
        const auto y = render (*p, toBuffer (sine (fs, hz, (float) (zeroVu * 0.3), n), 2), 512);
        std::vector<double> env;
        const int w = (int) (0.04 * fs);
        for (int s = (int) fs; s + w <= n; s += w)
            env.push_back (rms (y.getReadPointer (0) + s, w));
        double m = 0, v = 0;
        for (auto e : env) m += e;
        m /= (double) env.size();
        for (auto e : env) v += (e - m) * (e - m);
        return std::sqrt (v / (double) env.size()) / m;
    };
    const double hf100 = envelopeSpread (100.0f, 10000.0), lf100 = envelopeSpread (100.0f, 300.0), hf0 = envelopeSpread (0.0f, 10000.0);
    MEASURE ("AGE 100 %: wear variation of a 10 kHz tone (envelope RMS, %)", 100 * hf100);
    MEASURE ("AGE 100 %: wear variation of a 300 Hz tone (envelope RMS, %)", 100 * lf100);
    CHECK (hf100 > 0.03 && hf100 < 0.2);
    CHECK (hf100 > 3.0 * lf100);
    CHECK (hf0 < 0.002);
    // wow and flutter from AGE alone
    {
        auto p = makeConfigured (fs, { { tp::age, 100.0f } });
        const int n = (int) (10.0 * fs);
        const auto y = render (*p, toBuffer (sine (fs, 3150.0, (float) (zeroVu * 0.3), n), 2), 512);
        const double wf = weightedWowFlutter (speedDeviation (y.getReadPointer (0), n, fs, 3150.0), 1000.0, 2000);
        MEASURE ("AGE 100 %, WOW = FLUTTER = 0: weighted W&F 2-sigma (%)", 100 * wf);
        CHECK (wf > 0.0004 && wf < 0.004);
    }
}

// ------------------------------------------------------------------ dry paths, AUTO GAIN, telemetry
TEST ("TAPE: MIX 0 is a latency-aligned bit-exact null of the input; host Bypass is bit-exact delayed dry (from the start and after its 20 ms crossfade); both crossfades are click-free")
{
    for (double fs : { 44100.0, 48000.0, 96000.0, 192000.0 })
        for (int channels : { 1, 2 })
        {
            const auto tag = juce::String (fs) + " Hz " + juce::String (channels) + " ch";
            const int n = (int) (0.5 * fs);
            juce::AudioBuffer<float> x (channels, n);
            const auto prog = musicLike (fs, 0.5, 0.9f);
            for (int ch = 0; ch < channels; ++ch)
                x.copyFrom (ch, 0, prog, ch, 0, n);
            // MIX 0 from the start, everything else on (NOISE on, AGE, wow, crosstalk)
            for (const char* which : { tp::mix, tp::bypass })
            {
                auto p = makeTape (fs, 256, channels);
                setParam (*p, tp::noise, 1.0f);
                setParam (*p, tp::age, 70.0f);
                setParam (*p, tp::input, 9.0f);
                setParam (*p, which, juce::String (which) == tp::mix ? 0.0f : 1.0f);
                p->prepareToPlay (fs, 256);
                const int L = p->getLatencySamples();
                const auto y = render (*p, x, 256);
                bool exact = true;
                for (int ch = 0; ch < channels; ++ch)
                    for (int i = 0; i < n; ++i)
                    {
                        const float want = i < L ? 0.0f : x.getSample (ch, i - L);
                        if (! sameBits (y.getSample (ch, i), want)) exact = false;
                    }
                CHECK_MSG (exact, tag + ": " + which + " is bit-exact delayed dry");
            }
            // switching during playback: bit-exact once the ramps are done
            for (const char* which : { tp::mix, tp::bypass })
            {
                auto p = makeTape (fs, 256, channels);
                const int L = p->getLatencySamples();
                const int half = n / 2;
                juce::AudioBuffer<float> a (x);
                juce::MidiBuffer midi;
                juce::AudioBuffer<float> first (a.getArrayOfWritePointers(), channels, 0, half);
                p->processBlock (first, midi);
                setParam (*p, which, juce::String (which) == tp::mix ? 0.0f : 1.0f);
                for (int pos = half; pos < n; pos += 256)
                {
                    juce::AudioBuffer<float> v (a.getArrayOfWritePointers(), channels, pos, std::min (256, n - pos));
                    p->processBlock (v, midi);
                }
                bool exact = true;
                for (int ch = 0; ch < channels; ++ch)
                    for (int i = half + (int) (0.1 * fs); i < n; ++i)
                        if (! sameBits (a.getSample (ch, i), x.getSample (ch, i - L))) exact = false;
                CHECK_MSG (exact, tag + ": " + which + " switched on is exact after its ramp");
            }
        }
    // click-free crossfades with a full-scale-ish tone (AUTO GAIN keeps wet and dry level-matched)
    const double fs = 48000.0;
    const int n = 188 * 256;
    const auto x = toBuffer (sine (fs, 220.0, (float) zeroVu, n), 2);
    for (const char* which : { tp::mix, tp::bypass })
    {
        auto p = makeTape (fs, 256, 2);
        auto ref = makeTape (fs, 256, 2);
        juce::AudioBuffer<float> a (x), b (x);
        juce::MidiBuffer midi;
        for (int pos = 0; pos < n; pos += 256)
        {
            if (pos == n / 2) setParam (*p, which, juce::String (which) == tp::mix ? 0.0f : 1.0f); // n / 2 = 94 blocks
            juce::AudioBuffer<float> v (a.getArrayOfWritePointers(), 2, pos, 256);
            p->processBlock (v, midi);
            juce::AudioBuffer<float> w (b.getArrayOfWritePointers(), 2, pos, 256);
            ref->processBlock (w, midi);
        }
        const auto hp = clickBand (a.getReadPointer (0), n, fs), hr = clickBand (b.getReadPointer (0), n, fs);
        const double around = maxAbs (hp, n / 2 - 100, n / 2 + (int) (0.08 * fs)), steady = maxAbs (hr, n / 2 - 100, n / 2 + (int) (0.08 * fs));
        MEASURE (juce::String (which) + " switch: 8 kHz+ energy during the crossfade (dBFS)", dbOf (around));
        CHECK_MSG (around < std::max (2.0 * steady, 1.0e-4), juce::String (which) + ": click-free crossfade");
    }
}

TEST ("TAPE: AUTO GAIN: a 0 VU 1 kHz tone comes out at its input level for every INPUT, CAL, SATURATION, TYPE, SPEED and BIAS; equals -INPUT + the machine's measured loss; off follows INPUT; getAutoGainDb reports it (CSV)")
{
    const double fs = 48000.0;
    juce::String csv ("input_db,cal_db,sat_pct,type,ips,bias_pct,auto_gain_db,out_re_in_db,off_out_re_in_db\n");
    double worst = 0.0;
    int cases = 0;
    for (float in : { -12.0f, 0.0f, 6.0f, 12.0f, 24.0f })
        for (int cal = 0; cal < 4; ++cal)
            for (float sat : { 0.0f, 42.0f, 100.0f })
                for (int t = 0; t < 3; ++t)
                    for (int s = 0; s < 3; ++s)
                        for (float bias : { -50.0f, 0.0f, 50.0f })
                        {
                            // a spread subset of the grid (all INPUT x CAL, rotating the rest)
                            const int idx = (int) (in + 12) + 7 * cal + 13 * (int) sat + 3 * t + 5 * s + (int) bias;
                            if (idx % 7 != 0 && ! (sat == 42.0f && t == 0 && s == 1 && bias == 0.0f)) continue;
                            std::vector<Setting> st { { tp::input, in }, { tp::calibration, (float) cal }, { tp::saturation, sat }, { tp::tapeType, (float) t },
                                                      { tp::speed, (float) s }, { tp::bias, bias }, { tp::autoGain, 1.0f } };
                            auto p = makeConfigured (fs, st);
                            const auto h = toneThrough (*p, fs, 0.0, 1000.0, 0.3);
                            const double out = dbOf (h.fundamental / zeroVu);
                            const double ag = p->getAutoGainDb();
                            st.back().second = 0.0f;
                            auto q = makeConfigured (fs, st);
                            const auto ho = toneThrough (*q, fs, 0.0, 1000.0, 0.3);
                            const double off = dbOf (ho.fundamental / zeroVu);
                            csv << in << "," << TapeEngine::calibrationDb[cal] << "," << sat << "," << typeName (t) << "," << speedName (s) << "," << bias << ","
                                << juce::String (ag, 3) << "," << juce::String (out, 3) << "," << juce::String (off, 3) << "\n";
                            const auto tag = "INPUT " + juce::String (in) + " CAL " + juce::String (cal) + " SAT " + juce::String (sat) + " type " + typeName (t)
                                             + " " + speedName (s) + " IPS BIAS " + juce::String (bias);
                            worst = std::max (worst, std::abs (out));
                            ++cases;
                            // within 0.3 dB; 0.5 dB when INPUT drives the record amplifier into its limit
                            CHECK_MSG (std::abs (out) <= (in > 12.0f ? 0.5 : 0.3), tag + ": " + juce::String (out) + " dB");
                            CHECK_MSG (std::abs ((out - off) - ag) < 0.02, tag + ": AUTO GAIN is a pure gain = getAutoGainDb");
                            // off: the machine's level follows INPUT (within its own compression)
                            CHECK_MSG (off <= in + 0.3, tag);
                            if (in <= 0.0f && sat <= 42.0f && bias >= 0.0f) CHECK_MSG (std::abs (off - in) < 1.0, tag);
                        }
    MEASURE ("AUTO GAIN: worst 0 VU in -> out deviation over " + juce::String (cases) + " settings (dB)", worst);
    auto q = makeConfigured (fs, { { tp::autoGain, 0.0f } });
    CHECK (q->getAutoGainDb() == 0.0f);

    // AUTO GAIN is aligned with the audio (delayed by the latency): an INPUT move under a steady
    // tone leaves the output level steady (2 ms RMS windows)
    for (float to : { 12.0f, -12.0f })
    {
        auto p = makeConfigured (fs, { { tp::autoGain, 1.0f } });
        const int blocks = 80, n = blocks * 256;
        auto io = toBuffer (sine (fs, 1000.0, (float) zeroVu, n), 2);
        juce::MidiBuffer midi;
        for (int k = 0; k < blocks; ++k)
        {
            if (k == 40) setParam (*p, tp::input, to);
            juce::AudioBuffer<float> v (io.getArrayOfWritePointers(), 2, k * 256, 256);
            p->processBlock (v, midi);
        }
        const int w = 96;
        double lo = 1e9, hi = -1e9;
        for (int s0 = 20 * 256; s0 + w <= n; s0 += w)
        {
            const double db = dbOf (rms (io.getReadPointer (0) + s0, w) / tmod::zeroVuRms);
            lo = std::min (lo, db);
            hi = std::max (hi, db);
        }
        MEASURE ("AUTO GAIN: output level spread while INPUT moves 0 -> " + juce::String (to) + " dB (dB)", hi - lo);
        CHECK (hi - lo < 0.5);
    }
    outFile ("TAPE_auto_gain.csv").replaceWithText (csv);
}

TEST ("TAPE: RECORD and REPRO VU are real: RECORD = the signal after INPUT (0 VU = -18 dBFS sine), independent of CAL; REPRO = the repro output before AUTO GAIN; per channel; NaN when no audio")
{
    const double fs = 48000.0;
    const int n = (int) (0.5 * fs);
    for (float in : { 0.0f, 6.0f, -10.0f })
        for (int cal : { 0, 3 })
        {
            auto p = makeConfigured (fs, { { tp::input, in }, { tp::calibration, (float) cal } });
            render (*p, toBuffer (sine (fs, 1000.0, (float) zeroVu, n), 2), 256);
            (void) p->takeRecordVuDb(); (void) p->takeReproVuDb();
            for (int ch = 0; ch < 2; ++ch) { (void) p->takeRecordVuDb (ch); (void) p->takeReproVuDb (ch); }
            const auto y = render (*p, toBuffer (sine (fs, 1000.0, (float) zeroVu, n), 2), 256);
            const auto tag = "INPUT " + juce::String (in) + " CAL " + juce::String (cal);
            CHECK_NEAR (p->takeRecordVuDb(), in, 0.05);
            CHECK_NEAR (p->takeRecordVuDb (0), in, 0.05);
            CHECK_NEAR (p->takeRecordVuDb (1), in, 0.05);
            // REPRO = output / AUTO GAIN (MIX 100, OUTPUT 0)
            const double outDb = dbOf (rms (y.getReadPointer (0), n) / tmod::zeroVuRms);
            const double repro = p->takeReproVuDb();
            MEASURE (tag + ": REPRO VU (dB)", repro);
            CHECK_MSG (std::abs (repro - (outDb - p->getAutoGainDb())) < 0.05, tag);
            CHECK (std::isnan (p->takeRecordVuDb()));
            CHECK (std::isnan (p->takeReproVuDb()));
        }
    // AUTO GAIN off: the REPRO VU reads exactly the plugin output; left-only input lights only the left meters
    {
        auto p = makeConfigured (fs, {});
        juce::AudioBuffer<float> x (2, n);
        x.clear();
        x.copyFrom (0, 0, sine (fs, 1000.0, (float) zeroVu, n).data(), n);
        const auto y = render (*p, x, 256);
        (void) y;
        CHECK_NEAR (p->takeRecordVuDb (0), 0.0, 0.05);
        CHECK (p->takeRecordVuDb (1) <= -79.9f);
        CHECK (p->takeReproVuDb (1) <= -79.9f);
        const auto z = render (*p, x, 256);
        CHECK_NEAR (p->takeReproVuDb (0), dbOf (rms (z.getReadPointer (0), n) / tmod::zeroVuRms), 0.05);
    }
    // mono: both meters carry the one channel
    {
        auto p = makeConfigured (fs, {}, 256, 1);
        render (*p, toBuffer (sine (fs, 1000.0, (float) zeroVu, n), 1), 256);
        CHECK_NEAR (p->takeRecordVuDb (0), 0.0, 0.05);
        CHECK_NEAR (p->takeRecordVuDb (1), 0.0, 0.05);
    }
}

TEST ("TAPE: SATURATION telemetry: knee depth of the magnetic layer, ~0 when quiet, rises with level, SATURATION and CAL, near 1 when slammed; read-and-reset; 24 segments")
{
    const double fs = 48000.0;
    auto activity = [&] (std::vector<Setting> s, double vu)
    {
        auto p = makeConfigured (fs, s);
        render (*p, toBuffer (sine (fs, 1000.0, (float) (zeroVu * std::pow (10.0, vu / 20.0)), 9600), 2), 256);
        (void) p->getSaturationActivity();
        render (*p, toBuffer (sine (fs, 1000.0, (float) (zeroVu * std::pow (10.0, vu / 20.0)), 4800), 2), 256);
        const float a = p->getSaturationActivity();
        CHECK (p->getSaturationActivity() == 0.0f); // read-and-reset
        return (double) a;
    };
    juce::String csv ("level_vu,sat_pct,activity,segments_lit\n");
    double prev = -1.0;
    for (double vu : { -40.0, -20.0, -10.0, 0.0, 6.0, 12.0, 18.0 })
    {
        const double a = activity ({}, vu);
        csv << vu << ",42," << juce::String (a, 4) << "," << juce::roundToInt (24.0 * a) << "\n";
        CHECK (a > prev || (a == 0.0 && prev == 0.0));
        prev = a;
        MEASURE ("SAT activity at " + juce::String (vu) + " VU (segments of 24)", juce::roundToInt (24.0 * a));
    }
    CHECK (activity ({}, -40.0) < 0.01);
    CHECK (activity ({}, 0.0) > 0.02 && activity ({}, 0.0) < 0.1);   // 1 segment at 0 VU
    CHECK (activity ({}, 9.0) > 0.4 && activity ({}, 9.0) < 0.6);    // about half at MOL
    CHECK (activity ({ { tp::saturation, 100.0f } }, 0.0) > activity ({}, 0.0));
    CHECK (activity ({ { tp::saturation, 0.0f } }, 0.0) < activity ({}, 0.0));
    CHECK (activity ({ { tp::calibration, 3.0f } }, 0.0) > activity ({}, 0.0));
    CHECK (activity ({ { tp::saturation, 100.0f }, { tp::input, 24.0f } }, 6.0) > 0.85);
    outFile ("TAPE_saturation_activity.csv").replaceWithText (csv);
}

TEST ("TAPE: transport telemetry: reels follow the host playhead, or input signal without one; tape speed glides to the selected IPS")
{
    const double fs = 48000.0;
    auto p = makeConfigured (fs, {});
    FakePlayHead head;
    p->setPlayHead (&head);
    render (*p, silence (2, 2048), 256);
    CHECK (! p->isTransportRunning());
    head.playing = true;
    render (*p, silence (2, 256), 256);
    CHECK (p->isTransportRunning());
    head.playing = false;
    render (*p, toBuffer (sine (fs, 1000.0, 0.5f, 256), 2), 256);
    CHECK (! p->isTransportRunning()); // the host says stopped, whatever the input
    p->setPlayHead (nullptr);
    render (*p, toBuffer (sine (fs, 1000.0, 0.01f, 256), 2), 256);
    CHECK (p->isTransportRunning()); // fallback: signal above -60 dBFS
    render (*p, silence (2, (int) (0.4 * fs)), 256);
    CHECK (p->isTransportRunning()); // held 0.5 s
    render (*p, silence (2, (int) (0.2 * fs)), 256);
    CHECK (! p->isTransportRunning());
    render (*p, toBuffer (sine (fs, 1000.0, 0.0005f, 4800), 2), 256); // -66 dBFS: below the threshold
    CHECK (! p->isTransportRunning());

    CHECK_NEAR (p->getTapeSpeedIps(), 15.0, 1e-9);
    setParam (*p, tp::speed, 2.0f);
    render (*p, silence (2, (int) (0.05 * fs)), 256);
    const double mid = p->getTapeSpeedIps();
    MEASURE ("tape speed 50 ms into a 15 -> 30 IPS change (IPS)", mid);
    CHECK (mid > 16.0 && mid < 29.0);
    render (*p, silence (2, (int) (0.1 * fs)), 256);
    CHECK_NEAR (p->getTapeSpeedIps(), 30.0, 1e-9);
    setParam (*p, tp::speed, 0.0f);
    render (*p, silence (2, (int) (0.2 * fs)), 256);
    CHECK_NEAR (p->getTapeSpeedIps(), 7.5, 1e-9);
}

// ------------------------------------------------------------------ robustness, switching, state, presets, CPU
TEST ("TAPE: every control switches click-free (TYPE, SPEED, CAL, NOISE, AUTO GAIN and every knob, both directions); rapid random automation stays finite and bounded")
{
    const double fs = 48000.0;
    const int blocks = 160, n = blocks * 256, at = 80 * 256;
    const auto x = toBuffer (sine (fs, 220.0, (float) (zeroVu * 0.5), n), 2);
    struct Change { const char* id; float from, to; };
    const Change changes[] = {
        { tp::input, 0, 18 }, { tp::input, 18, -12 }, { tp::output, 0, -20 }, { tp::output, -20, 12 }, { tp::tapeType, 0, 2 }, { tp::tapeType, 2, 1 },
        { tp::speed, 1, 2 }, { tp::speed, 1, 0 }, { tp::speed, 0, 2 }, { tp::calibration, 1, 3 }, { tp::calibration, 3, 0 }, { tp::bias, 0, 80 },
        { tp::bias, 0, -80 }, { tp::saturation, 42, 100 }, { tp::saturation, 42, 0 }, { tp::headBump, 36, 100 }, { tp::headBump, 36, 0 },
        { tp::lf, 0, 100 }, { tp::lf, 0, -100 }, { tp::hf, 0, 100 }, { tp::hf, 0, -100 }, { tp::age, 18, 100 }, { tp::age, 100, 0 },
        { tp::wow, 6, 100 }, { tp::wow, 100, 0 }, { tp::flutter, 4, 100 }, { tp::flutter, 100, 0 }, { tp::noise, 0, 1 }, { tp::noise, 1, 0 },
        { tp::noiseAmount, 12, 100 }, { tp::crosstalk, 8, 100 }, { tp::crosstalk, 100, 0 }, { tp::mix, 100, 40 }, { tp::mix, 0, 100 },
        { tp::autoGain, 1, 0 }, { tp::autoGain, 0, 1 } };
    juce::String csv ("control,from,to,hf_peak_during_dbfs,hf_peak_steady_dbfs\n");
    for (auto& c : changes)
    {
        auto setup = [&] (float v)
        {
            auto p = makeTape (fs, 256, 2);
            if (juce::String (c.id) == tp::noiseAmount) setParam (*p, tp::noise, 1.0f);
            setParam (*p, c.id, v);
            p->prepareToPlay (fs, 256);
            return p;
        };
        auto p = setup (c.from), before = setup (c.from), after = setup (c.to);
        juce::AudioBuffer<float> a (x), b (x), d (x);
        juce::MidiBuffer midi;
        for (int k = 0; k < blocks; ++k)
        {
            if (k == 80) setParam (*p, c.id, c.to);
            for (auto [proc, buf] : { std::pair { p.get(), &a }, std::pair { before.get(), &b }, std::pair { after.get(), &d } })
            {
                juce::AudioBuffer<float> v (buf->getArrayOfWritePointers(), 2, k * 256, 256);
                proc->processBlock (v, midi);
            }
        }
        double during = 0, steady = 0;
        for (int ch = 0; ch < 2; ++ch)
        {
            const auto hp = clickBand (a.getReadPointer (ch), n, fs);
            const auto hb = clickBand (b.getReadPointer (ch), n, fs), hd = clickBand (d.getReadPointer (ch), n, fs);
            during = std::max (during, maxAbs (hp, at, at + (int) (0.25 * fs)));
            steady = std::max ({ steady, maxAbs (hb, at, at + (int) (0.25 * fs)), maxAbs (hd, at, at + (int) (0.25 * fs)) });
        }
        const auto tag = juce::String (c.id) + " " + juce::String (c.from) + " -> " + juce::String (c.to);
        csv << c.id << "," << c.from << "," << c.to << "," << juce::String (dbOf (during), 1) << "," << juce::String (dbOf (steady), 1) << "\n";
        CHECK_MSG (during < std::max (2.0 * steady, 1.0e-4), tag + ": " + juce::String (dbOf (during), 1) + " dBFS vs steady " + juce::String (dbOf (steady), 1));
        CHECK_MSG (allFinite (a), tag);
    }
    outFile ("TAPE_switching.csv").replaceWithText (csv);

    // rapid random automation of everything (every 64 samples), music at full scale
    auto p = makeTape (fs, 64, 2);
    auto io = musicLike (fs, 3.0, 0.99f);
    juce::Random rng (7);
    juce::MidiBuffer midi;
    float peak = 0.0f;
    long allocations = 0;
    for (int pos = 0; pos + 64 <= io.getNumSamples(); pos += 64)
    {
        for (auto* r : p->getParameters())
            if (r != p->getBypassParameter() && rng.nextFloat() < 0.3f)
                r->setValueNotifyingHost (rng.nextFloat());
        juce::AudioBuffer<float> v (io.getArrayOfWritePointers(), 2, pos, 64);
        {
            ScopedAllocationCounter count;
            p->processBlock (v, midi);
            allocations += count.count();
        }
        peak = std::max (peak, v.getMagnitude (0, 64));
    }
    MEASURE ("random automation: output peak (dBFS)", dbOf (peak));
    CHECK (allFinite (io));
    CHECK (peak < 64.0f); // OUTPUT +24 dB and AUTO GAIN up to +40 dB on a full-scale input are allowed to be loud, never unbounded
    CHECK (allocations == 0);
}

TEST ("TAPE: NaN, Inf, huge and denormal input never reach the output; the machine recovers; silence stays exactly silent with NOISE off")
{
    const double fs = 48000.0;
    auto p = makeTape (fs, 256, 2);
    auto ref = makeTape (fs, 256, 2);
    auto x = musicLike (fs, 1.0, 0.5f);
    auto clean = x;
    const float bad[] = { std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity() };
    for (int i = 0; i < 3; ++i)
    {
        x.setSample (0, 1000 + i * 37, bad[i]);
        x.setSample (1, 2000 + i * 41, bad[i]);
        clean.setSample (0, 1000 + i * 37, 0.0f);
        clean.setSample (1, 2000 + i * 41, 0.0f);
    }
    for (int i = 5000; i < 5100; ++i) { x.setSample (0, i, 1.0e-41f); clean.setSample (0, i, 1.0e-41f); }
    const auto y = render (*p, x, 256);
    const auto yr = render (*ref, clean, 256);
    CHECK (allFinite (y));
    CHECK (identical (y, yr)); // non-finite samples are taken as silence, nothing else changes
    // huge (finite) input: bounded by the record amplifier and the tape
    auto h = makeTape (fs, 256, 2);
    setParam (*h, tp::autoGain, 0.0f);
    juce::AudioBuffer<float> big (2, 9600);
    for (int i = 0; i < 9600; ++i) { big.setSample (0, i, (i % 2) ? 1.0e30f : -1.0e30f); big.setSample (1, i, 3.0e38f); }
    const auto yb = render (*h, big, 256);
    CHECK (allFinite (yb));
    CHECK (yb.getMagnitude (0, 9600) < 100.0f);
    const auto after = render (*h, toBuffer (sine (fs, 1000.0, (float) zeroVu, 9600), 2), 256);
    CHECK (allFinite (after));
    CHECK (toneAmplitude (after.getReadPointer (0) + 4800, 4800, fs, 1000.0) > 0.5 * zeroVu);
    // after a signal, silence decays to exact zeros (no denormals, no residue) with NOISE off
    auto s = makeTape (fs, 256, 2);
    render (*s, musicLike (fs, 0.5, 0.9f), 256);
    const auto tail = render (*s, silence (2, (int) (3.0 * fs)), 256);
    int subnormals = 0;
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < tail.getNumSamples(); ++i)
            subnormals += std::fpclassify (tail.getSample (ch, i)) == FP_SUBNORMAL ? 1 : 0;
    CHECK (subnormals == 0);
    CHECK (tail.getMagnitude (0, (int) (2.0 * fs), (int) fs) < 1.0e-6f);
}

TEST ("TAPE: all sample rates x block sizes 1-2048 x mono / stereo: unity at 0 VU with AUTO GAIN, finite, zero allocations, bit-identical across block sizes (and host blocks larger than announced)")
{
    juce::String csv ("fs,channels,block,out_re_in_db\n");
    for (double fs : { 44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0 })
        for (int channels : { 1, 2 })
        {
            const int n = (int) (0.3 * fs);
            juce::AudioBuffer<float> x (channels, n);
            const auto prog = musicLike (fs, 0.3, 0.7f);
            for (int ch = 0; ch < channels; ++ch) x.copyFrom (ch, 0, prog, ch, 0, n);
            juce::AudioBuffer<float> first;
            for (int block : { 1, 7, 64, 333, 2048 })
            {
                auto p = makeTape (fs, block, channels);
                juce::AudioBuffer<float> io (x);
                juce::MidiBuffer midi;
                long allocations = 0;
                for (int pos = 0; pos < n; pos += block)
                {
                    juce::AudioBuffer<float> v (io.getArrayOfWritePointers(), channels, pos, std::min (block, n - pos));
                    ScopedAllocationCounter count;
                    p->processBlock (v, midi);
                    allocations += count.count();
                }
                const auto tag = juce::String (fs) + " Hz " + juce::String (channels) + " ch block " + juce::String (block);
                CHECK_MSG (allocations == 0, tag + ": " + juce::String (allocations) + " allocations");
                CHECK_MSG (allFinite (io), tag);
                if (first.getNumSamples() == 0) first = io;
                else CHECK_MSG (identical (first, io), tag + ": differs from block 1");
                if (block == 333)
                {
                    auto t = makeTape (fs, block, channels);
                    const auto y = render (*t, toBuffer (sine (fs, 1000.0, (float) zeroVu, n), channels), block);
                    const double g = dbOf (toneAmplitude (y.getReadPointer (channels - 1) + n / 2, n / 2, fs, 1000.0) / zeroVu);
                    csv << fs << "," << channels << "," << block << "," << juce::String (g, 3) << "\n";
                    CHECK_MSG (std::abs (g) < 0.35, tag + ": " + juce::String (g));
                }
            }
            // a host block larger than prepareToPlay announced
            auto p = makeTape (fs, 64, channels);
            const auto y = render (*p, x, 1000);
            CHECK (identical (first, y));
        }
    outFile ("TAPE_rates_blocks.csv").replaceWithText (csv);
}

TEST ("TAPE: state round trip restores every parameter and the same sound; factory presets load, cover every TYPE, SPEED and CAL, and render sanely (CSV)")
{
    const double fs = 48000.0;
    {
        TapeProcessor a;
        const std::vector<Setting> s { { tp::input, 4.5f }, { tp::output, -3.2f }, { tp::tapeType, 2.0f }, { tp::speed, 0.0f }, { tp::calibration, 3.0f },
                                       { tp::bias, -22.5f }, { tp::saturation, 77.0f }, { tp::headBump, 61.0f }, { tp::lf, -33.0f }, { tp::hf, 12.0f },
                                       { tp::age, 64.0f }, { tp::wow, 21.0f }, { tp::flutter, 17.0f }, { tp::noise, 1.0f }, { tp::noiseAmount, 30.0f },
                                       { tp::crosstalk, 44.0f }, { tp::mix, 73.0f }, { tp::autoGain, 0.0f } };
        for (auto [id, v] : s) setParam (a, id, v);
        juce::MemoryBlock mb;
        a.getStateInformation (mb);
        TapeProcessor b;
        b.setStateInformation (mb.getData(), (int) mb.getSize());
        for (auto [id, v] : s)
            CHECK_NEAR (plainValue (b, id), plainValue (a, id), 1e-5);
        auto pa = makeTape (fs, 256), pb = makeTape (fs, 256);
        pa->setStateInformation (mb.getData(), (int) mb.getSize());
        pb->setStateInformation (mb.getData(), (int) mb.getSize());
        pa->prepareToPlay (fs, 256);
        pb->prepareToPlay (fs, 256);
        const auto prog = musicLike (fs, 0.5, 0.8f);
        CHECK (identical (render (*pa, prog, 256), render (*pb, prog, 256)));
    }
    TapeProcessor p;
    const auto& list = p.presets.getPresets();
    int factory = 0;
    for (auto& pr : list) factory += pr.isFactory ? 1 : 0;
    MEASURE ("factory presets", factory);
    CHECK (factory >= 12);
    std::set<int> types, speeds, cals;
    std::set<juce::String> names;
    juce::String csv ("preset,type,ips,cal,out_rms_re_in_db,peak_dbfs\n");
    for (int i = 0; i < (int) list.size(); ++i)
        if (list[(size_t) i].isFactory)
        {
            p.loadPreset (i);
            names.insert (list[(size_t) i].name);
            CHECK_MSG (list[(size_t) i].params.values.size() == 18, list[(size_t) i].name);
            for (auto& [id, v] : list[(size_t) i].params.values)
            {
                auto* r = p.apvts.getParameter (id);
                CHECK_MSG (r != nullptr, id);
                if (r) CHECK_MSG (std::abs (r->convertFrom0to1 (r->getValue()) - v) < 0.06f, list[(size_t) i].name + ": " + id);
            }
            CHECK (! p.isPresetModified());
            CHECK_MSG (plainValue (p, tp::bypass) < 0.5f, list[(size_t) i].name);
            types.insert (juce::roundToInt (plainValue (p, tp::tapeType)));
            speeds.insert (juce::roundToInt (plainValue (p, tp::speed)));
            cals.insert (juce::roundToInt (plainValue (p, tp::calibration)));
            auto q = makeTape (fs, 256);
            q->loadPreset (i);
            q->prepareToPlay (fs, 256);
            const auto prog = musicLike (fs, 1.0, 0.25f);
            const auto y = render (*q, prog, 256);
            const double g = dbOf (rms (y.getReadPointer (0) + 4800, y.getNumSamples() - 4800) / rms (prog.getReadPointer (0), prog.getNumSamples()));
            csv << list[(size_t) i].name << "," << plainValue (p, tp::tapeType) << "," << speedName (juce::roundToInt (plainValue (p, tp::speed))) << ","
                << plainValue (p, tp::calibration) << "," << juce::String (g, 2) << "," << juce::String (dbOf (y.getMagnitude (0, y.getNumSamples())), 2) << "\n";
            CHECK_MSG (allFinite (y), list[(size_t) i].name);
            CHECK_MSG (std::abs (g) < 6.0, list[(size_t) i].name + ": level " + juce::String (g));
        }
    CHECK (types.size() == 3 && speeds.size() == 3 && cals.size() == 4);
    CHECK ((int) names.size() == factory);
    setParam (p, tp::hf, 13.0f);
    CHECK (p.isPresetModified());
    PresetManager::Preset out;
    CHECK (p.presets.parse (R"({"format":"tleet-preset","version":1,"plugin":"STEEL","name":"x","params":{}})", out).failed());
    outFile ("TAPE_presets.csv").replaceWithText (csv);
}

TEST ("TAPE: every control measurably changes the output; the TAPE CONDITION response evaluator equals the measured sweep; the spectrum tap carries the output")
{
    const double fs = 48000.0;
    const auto prog = musicLike (fs, 1.0, 0.3f);
    struct Change { const char* id; float to; };
    const Change changes[] = { { tp::input, 6 }, { tp::output, -2 }, { tp::tapeType, 2 }, { tp::speed, 2 }, { tp::calibration, 3 }, { tp::bias, 30 },
                               { tp::saturation, 60 }, { tp::headBump, 70 }, { tp::lf, 30 }, { tp::hf, 30 }, { tp::age, 40 }, { tp::wow, 30 },
                               { tp::flutter, 30 }, { tp::noise, 1 }, { tp::noiseAmount, 30 }, { tp::crosstalk, 40 }, { tp::mix, 70 }, { tp::autoGain, 0 } };
    auto base = makeTape (fs, 256);
    setParam (*base, tp::noise, 1.0f); // so that NOISE AMOUNT acts; NOISE itself is compared from off
    base->prepareToPlay (fs, 256);
    const auto y0 = render (*base, prog, 256);
    auto off = makeTape (fs, 256);
    const auto yOff = render (*off, prog, 256);
    for (auto& c : changes)
    {
        auto p = makeTape (fs, 256);
        const bool noiseCase = juce::String (c.id) == tp::noise;
        if (! noiseCase) setParam (*p, tp::noise, 1.0f);
        setParam (*p, c.id, c.to);
        p->prepareToPlay (fs, 256);
        const auto y = render (*p, prog, 256);
        const auto& ref = noiseCase ? yOff : y0;
        double d = 0, s = 0;
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 4800; i < y.getNumSamples(); ++i)
            {
                d += (y.getSample (ch, i) - ref.getSample (ch, i)) * (y.getSample (ch, i) - ref.getSample (ch, i));
                s += ref.getSample (ch, i) * ref.getSample (ch, i);
            }
        const double ratio = 10 * std::log10 (d / s);
        MEASURE (juce::String (c.id) + " -> " + juce::String (c.to) + ": change (dB re signal)", ratio);
        CHECK_MSG (ratio > -65.0, c.id); // NOISE on adds the machine's hiss: -53 dB re this programme
    }

    // the evaluator vs the measured small-signal sweep (absolute level, AUTO GAIN off)
    juce::String csv ("case,freq_hz,measured_db,evaluator_db\n");
    const std::vector<std::pair<juce::String, std::vector<Setting>>> cases {
        { "defaults clean", {} }, { "7.5 IPS", { { tp::speed, 0.0f } } }, { "30 IPS bump 100", { { tp::speed, 2.0f }, { tp::headBump, 100.0f } } },
        { "bias -50", { { tp::bias, -50.0f } } }, { "bias +60", { { tp::bias, 60.0f } } }, { "LF -50 HF +80", { { tp::lf, -50.0f }, { tp::hf, 80.0f } } },
        { "type C cal +6", { { tp::tapeType, 2.0f }, { tp::calibration, 3.0f } } } };
    for (auto& [name, st] : cases)
    {
        auto p = makeConfigured (fs, st);
        const auto r = measureResponse (*p, fs, 0, -30.0);
        double worst = 0;
        for (double f = 25.0; f <= 18000.0; f *= std::pow (2.0, 1.0 / 6.0))
        {
            const double m = r.db (f), e = p->tapeResponseDb (f, 0);
            csv << name << "," << juce::String (f, 1) << "," << juce::String (m, 3) << "," << juce::String (e, 3) << "\n";
            worst = std::max (worst, std::abs (m - e));
        }
        MEASURE ("response evaluator vs measured, " + name + ": worst (dB)", worst);
        CHECK_MSG (worst < 0.25, name);
    }
    outFile ("TAPE_response_evaluator.csv").replaceWithText (csv);

    // spectrum tap: off by default, then the output's tone
    auto p = makeTape (fs, 256);
    CHECK (! p->outputSpectrum.isActive());
    p->outputSpectrum.setActive (true);
    render (*p, toBuffer (sine (fs, 1000.0, (float) zeroVu, 8192), 2), 256);
    std::vector<float> buf (8192);
    const int got = p->outputSpectrum.pull (buf.data(), 8192);
    CHECK (got >= 4096);
    CHECK_NEAR (toneAmplitude (buf.data() + got - 4096, 4096, fs, 1000.0), zeroVu, 0.05 * zeroVu);
}

TEST ("TAPE: CPU cost per stereo instance at 44.1-192 kHz, everything on (measurement, CSV)")
{
    juce::String csv ("fs,oversampling,percent_core_median_block\n");
    for (double fs : { 44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0 })
    {
        const int block = 128;
        auto p = makeConfigured (fs, { { tp::noise, 1.0f }, { tp::age, 60.0f }, { tp::input, 6.0f } }, block, 2, false);
        p->outputSpectrum.setActive (true);
        const auto prog = musicLike (fs, 2.0, 0.9f, 3);
        const int n = prog.getNumSamples();
        juce::MidiBuffer midi;
        double load = 1.0e9;
        for (int run = 0; run < 5; ++run) // the best of five medians (shared machines are noisy)
        {
            auto io = prog;
            std::vector<double> times;
            for (int pos = 0; pos + block <= n; pos += block)
            {
                juce::AudioBuffer<float> view (io.getArrayOfWritePointers(), 2, pos, block);
                const auto b0 = juce::Time::getHighResolutionTicks();
                p->processBlock (view, midi);
                times.push_back (juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - b0));
                if (pos % 4096 == 0) { float tmp[4096]; p->outputSpectrum.pull (tmp, 4096); }
            }
            std::sort (times.begin(), times.end());
            load = std::min (load, times[times.size() / 2] * (fs / block) * 100.0);
        }
        const int osf = p->getEngineForTesting().oversamplingFactor();
        MEASURE ("@" + juce::String (fs) + " Hz (" + juce::String (osf) + "x), block 128, stereo: % of one core (median block)", load);
        csv << fs << "," << osf << "," << juce::String (load, 2) << "\n";
        // the brief: well under 10 % of one core at 48 kHz stereo; higher rates run the
        // host-rate stages (loss FIR, repro EQ) at their own rate on top of the same top-rate cost
        CHECK_MSG (load < (fs <= 48000.0 ? 10.0 : 20.0), juce::String (fs));
    }
    outFile ("TAPE_cpu.csv").replaceWithText (csv);
}
