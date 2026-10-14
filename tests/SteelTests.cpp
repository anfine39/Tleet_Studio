#include "TestFramework.h"
#include "TestSignals.h"
#include <juce_dsp/juce_dsp.h>
#include <complex>
#include <cstring>
#include <functional>
#include <map>
#include <set>
#include "../plugins/STEEL/PluginProcessor.h"
#include "../plugins/STEEL/Parameters.h"
#include "../plugins/STEEL/dsp/SteelEngine.h"

// Exact floating-point comparisons are deliberate here (bit-exact paths).
#if defined(__clang__) || defined(__GNUC__)
 #pragma GCC diagnostic ignored "-Wfloat-equal"
#endif

using namespace tleet;
using namespace tleet::test;
using tleet::steel::SteelProcessor;
namespace sp = tleet::steel::params;
namespace st = tleet::steel;

namespace
{
    constexpr double pi = juce::MathConstants<double>::pi;
    const char* modeName (int m) { static const char* n[] = { "Clean", "Punch", "Dense", "Steel" }; return n[m]; }
    const char* osName (int o) { static const char* n[] = { "1x", "2x", "4x", "8x" }; return n[o]; }

    void setParam (SteelProcessor& p, const char* id, float plain)
    {
        auto* r = p.apvts.getParameter (id);
        r->setValueNotifyingHost (r->convertTo0to1 (plain));
    }

    float plainValue (SteelProcessor& p, const char* id)
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

    juce::AudioBuffer<float> render (SteelProcessor& p, const juce::AudioBuffer<float>& input, int block)
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

    std::unique_ptr<SteelProcessor> makeSteel (double fs, int block, int channels = 2)
    {
        auto p = std::make_unique<SteelProcessor>();
        SteelProcessor::BusesLayout layout;
        layout.inputBuses.add (channels == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo());
        layout.outputBuses.add (channels == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo());
        CHECK_MSG (p->setBusesLayout (layout), "layout rejected");
        p->setRateAndBufferSizeDetails (fs, block);
        p->prepareToPlay (fs, block);
        return p;
    }

    // Music-like programme: kick (pitched decaying sine), snare (noise burst),
    // hats (HF noise), bass line, a sustained chord with HF content. Stereo,
    // decorrelated, crest factor typical of a mix. Peaks around `peak`.
    juce::AudioBuffer<float> musicLike (double fs, double seconds, float peak, int seed = 11)
    {
        const int n = (int) (seconds * fs);
        juce::AudioBuffer<float> b (2, n);
        b.clear();
        juce::Random rng (seed);
        const double beat = 60.0 / 124.0;
        for (int ch = 0; ch < 2; ++ch)
        {
            auto* d = b.getWritePointer (ch);
            double phB = 0, phC1 = 0, phC2 = 0, phC3 = 0;
            for (int i = 0; i < n; ++i)
            {
                const double t = i / fs;
                const double tb = std::fmod (t, beat);
                const int beatIdx = (int) (t / beat);
                // kick
                double s = 0.9 * std::exp (-tb * 18.0) * std::sin (2 * pi * (48.0 * tb + 90.0 * (1.0 - std::exp (-tb * 30.0)) / 30.0));
                // snare on 2 and 4
                if (beatIdx % 2 == 1)
                    s += 0.5 * std::exp (-tb * 25.0) * (2.0 * rng.nextDouble() - 1.0);
                // hats on eighths
                const double te = std::fmod (t, beat * 0.5);
                s += 0.18 * std::exp (-te * 80.0) * (2.0 * rng.nextDouble() - 1.0) * (i % 2 == 0 ? 1.0 : -1.0);
                // bass
                const double fb = (beatIdx / 4) % 2 == 0 ? 55.0 : 73.4;
                phB += 2 * pi * fb / fs;
                s += 0.35 * std::sin (phB) + 0.1 * std::sin (3 * phB);
                // chord with bright partials
                phC1 += 2 * pi * (261.6 * (ch == 0 ? 1.0 : 1.002)) / fs;
                phC2 += 2 * pi * 329.6 / fs;
                phC3 += 2 * pi * 392.0 / fs;
                s += 0.08 * (std::sin (phC1) + std::sin (phC2) + std::sin (phC3)) + 0.03 * std::sin (17.0 * phC1) + 0.02 * std::sin (29.0 * phC3);
                d[i] = (float) s;
            }
        }
        // fades so the circular reference interpolation sees clean edges
        const int fade = (int) (0.01 * fs);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < fade; ++i)
            {
                const float g = (float) i / fade;
                b.getWritePointer (ch)[i] *= g;
                b.getWritePointer (ch)[n - 1 - i] *= g;
            }
        const float m = b.getMagnitude (0, n);
        b.applyGain (peak / m);
        return b;
    }

    // ---- independent true-peak reference -----------------------------------------
    // Ideal band-limited interpolation by FFT zero padding (16x), double precision,
    // then a parabola through every local maximum. Written for the tests from
    // scratch (no JUCE, no plugin code). The signal is treated as periodic, so the
    // test signals start and end in silence.
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

    // Returns the true peak (linear) of x[0..n). Long signals are processed in
    // overlapping windows of 8192 samples (4096 hop, Hann^2-free: rectangular core
    // with 2048 samples of context on each side) so memory stays small.
    double referenceTruePeak (const float* x, int n, int factor = 16, double bandEdge = 0.49)
    {
        const int core = 4096, ctx = 2048, win = core + 2 * ctx; // 8192
        double best = 0.0;
        for (int s = 0; s < n; s += core)
        {
            std::vector<std::complex<double>> a ((size_t) win);
            for (int i = 0; i < win; ++i)
            {
                const int k = s - ctx + i;
                a[(size_t) i] = (k >= 0 && k < n) ? (double) x[k] : 0.0;
            }
            fft (a, false);
            const int N = win * factor;
            std::vector<std::complex<double>> b ((size_t) N);
            // ideal reconstruction of the band below bandEdge * fs (bins above are dropped;
            // with bandEdge 0.5 the Nyquist bin is split equally, the exact periodic sinc)
            const int keep = std::min (win / 2, (int) std::floor (bandEdge * win + 1.0e-9) + 1);
            for (int i = 1; i < keep; ++i)
            {
                b[(size_t) i] = a[(size_t) i] * (double) factor;
                b[(size_t) (N - i)] = a[(size_t) (win - i)] * (double) factor;
            }
            b[0] = a[0] * (double) factor;
            if (bandEdge >= 0.5)
            {
                b[(size_t) (win / 2)] = a[(size_t) (win / 2)] * (0.5 * factor);
                b[(size_t) (N - win / 2)] = a[(size_t) (win / 2)] * (0.5 * factor);
            }
            fft (b, true);
            const int from = ctx * factor, to = std::min (win, ctx + core + 1) * factor;
            for (int m = from; m < to && m < N - 1; ++m)
            {
                const double y0 = std::abs (b[(size_t) m].real());
                if (y0 <= best)
                    continue;
                double peak = y0;
                const double ym = b[(size_t) (m - 1)].real(), yc = b[(size_t) m].real(), yp = b[(size_t) (m + 1)].real();
                const double sgn = yc >= 0 ? 1.0 : -1.0;
                const double a0 = sgn * ym, a1 = sgn * yc, a2 = sgn * yp;
                const double den = a0 - 2 * a1 + a2;
                if (a1 >= a0 && a1 >= a2 && den < 0)
                {
                    const double p = 0.5 * (a0 - a2) / den;
                    peak = std::max (peak, a1 - 0.25 * (a0 - a2) * p);
                }
                best = std::max (best, peak);
            }
        }
        return best;
    }

    // The tests' true-peak reference: 16x polyphase interpolation with a Kaiser-windowed
    // sinc (beta 10, cut-off fs/2, +-128 host samples per phase), double precision, plus
    // a parabola through every local maximum. Designed here independently of the
    // plugin (which uses a half-band cascade). Flat within 0.01 dB to 0.48 fs, -6 dB
    // at fs/2, like the reconstruction filter of a high-quality DAC. Intervals whose
    // two neighbouring samples are both below `gate` are skipped (no programme in these
    // tests has inter-sample peaks 9 dB above its samples).
    struct SincReference
    {
        static constexpr int half = 128, phases = 16;
        std::vector<double> h; // [phase][2 * half]
        SincReference()
        {
            h.resize ((size_t) (phases * 2 * half));
            auto i0 = [] (double x) { double s = 1, t = 1; for (int k = 1; k < 200; ++k) { t *= (x / (2 * k)) * (x / (2 * k)); s += t; if (t < 1e-18 * s) break; } return s; };
            const double beta = 10.0;
            for (int p = 0; p < phases; ++p)
                for (int j = 0; j < 2 * half; ++j)
                {
                    const double x = (j - half + 1) - (double) p / phases; // tap j multiplies x[i - half + 1 + j]
                    const double r = x / (half + 0.5);
                    const double w = std::abs (r) < 1 ? i0 (beta * std::sqrt (1 - r * r)) / i0 (beta) : 0.0;
                    h[(size_t) (p * 2 * half + j)] = (std::abs (x) < 1e-12 ? 1.0 : std::sin (pi * x) / (pi * x)) * w;
                }
        }
        static const SincReference& get() { static const SincReference r; return r; }

        double peak (const float* x, int n, double gate) const
        {
            double best = 0.0;
            std::vector<double> win ((size_t) (2 * half));
            double prev2 = 0, prev1 = 0;
            for (int i = 0; i < n; ++i)
            {
                const bool active = std::abs (x[i]) >= gate || (i + 1 < n && std::abs (x[i + 1]) >= gate);
                if (! active) { prev2 = prev1 = 0; continue; }
                for (int j = 0; j < 2 * half; ++j)
                {
                    const int k = i - half + 1 + j;
                    win[(size_t) j] = (k >= 0 && k < n) ? x[k] : 0.0;
                }
                for (int p = 0; p < phases; ++p)
                {
                    const double* c = h.data() + p * 2 * half;
                    double acc = 0;
                    for (int j = 0; j < 2 * half; ++j)
                        acc += c[j] * win[(size_t) j];
                    // parabola through the previous two and this point
                    const double a0 = std::abs (prev2), a1 = std::abs (prev1), a2 = std::abs (acc);
                    best = std::max (best, a2);
                    if (a1 >= a0 && a1 >= a2 && a1 > 0)
                    {
                        const double sg = prev1 >= 0 ? 1.0 : -1.0;
                        const double y0 = sg * prev2, y1 = sg * prev1, y2 = sg * acc, den = y0 - 2 * y1 + y2;
                        if (den < 0)
                        {
                            const double q = 0.5 * (y0 - y2) / den;
                            if (std::abs (q) <= 1) best = std::max (best, y1 - 0.25 * (y0 - y2) * q);
                        }
                    }
                    prev2 = prev1;
                    prev1 = acc;
                }
            }
            return best;
        }
    };

    double sincTruePeak (const juce::AudioBuffer<float>& b, double gate)
    {
        double m = 0.0;
        for (int ch = 0; ch < b.getNumChannels(); ++ch)
            m = std::max (m, SincReference::get().peak (b.getReadPointer (ch), b.getNumSamples(), gate));
        return m;
    }

    double referenceTruePeak (const juce::AudioBuffer<float>& b, int from = 0, double bandEdge = 0.49)
    {
        double m = 0.0;
        for (int ch = 0; ch < b.getNumChannels(); ++ch)
            m = std::max (m, referenceTruePeak (b.getReadPointer (ch) + from, b.getNumSamples() - from, 16, bandEdge));
        return m;
    }

    double samplePeak (const juce::AudioBuffer<float>& b, int from = 0)
    {
        double m = 0.0;
        for (int ch = 0; ch < b.getNumChannels(); ++ch)
            for (int i = from; i < b.getNumSamples(); ++i)
                m = std::max (m, (double) std::abs (b.getSample (ch, i)));
        return m;
    }

    // A sine whose analytic true peak is `amp` (Tech 3341-style vectors).
    std::vector<float> tpSine (double fs, double hz, float amp, double seconds, double phaseDeg)
    {
        auto v = sine (fs, hz, amp, (int) (seconds * fs), phaseDeg * pi / 180.0);
        const int fade = (int) (0.02 * fs);
        for (int i = 0; i < fade; ++i)
        {
            const float g = 0.5f - 0.5f * (float) std::cos (pi * i / fade);
            v[(size_t) i] *= g;
            v[v.size() - 1 - (size_t) i] *= g;
        }
        return v;
    }

    juce::File outFile (const juce::String& name) { return outputDirectory().getChildFile (name); }
}

// =====================================================================================
TEST ("STEEL: parameter IDs, ranges, defaults, texts, choice strings and buses match the registry")
{
    SteelProcessor p;
    struct Expect { const char* id; float lo, hi, def; };
    const Expect floats[] = {
        { sp::input, -12.0f, 24.0f, 0.0f }, { sp::ceiling, -12.0f, 0.0f, -1.0f }, { sp::release, 20.0f, 1000.0f, 180.0f },
        { sp::lookahead, 0.0f, 10.0f, 3.0f }, { sp::stereoLink, 0.0f, 100.0f, 100.0f }, { sp::softClipAmount, 0.0f, 100.0f, 30.0f },
        { sp::output, -12.0f, 12.0f, 0.0f } };
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
    choice (sp::mode, { "Clean", "Punch", "Dense", "Steel" }, 3);
    choice (sp::releaseMode, { "Auto", "Manual" }, 0);
    choice (sp::oversampling, { "1x", "2x", "4x", "8x" }, 2);
    choice (sp::subFilter, { "Off", "20", "25", "30" }, 0);
    for (auto [id, def] : { std::pair { sp::truePeak, true }, std::pair { sp::softClip, false }, std::pair { sp::delta, false },
                            std::pair { sp::levelMatch, false }, std::pair { sp::bypass, false } })
    {
        auto* b = dynamic_cast<juce::AudioParameterBool*> (p.apvts.getParameter (id));
        CHECK_MSG (b != nullptr, id);
        if (b) CHECK_MSG (b->get() == def, id);
    }
    CHECK (p.getParameters().size() == 16);
    CHECK (p.getBypassParameter() == p.apvts.getParameter (sp::bypass));

    auto text = [&] (const char* id, float v) { auto* r = p.apvts.getParameter (id); return r->getText (r->convertTo0to1 (v), 64); };
    CHECK (text (sp::input, 6.4f) == "+6.4 dB");
    CHECK (text (sp::input, 0.0f) == "0.0 dB");
    CHECK (text (sp::output, -4.5f) == "-4.5 dB");
    CHECK (text (sp::ceiling, -1.0f) == "-1.0 dBTP");
    CHECK (text (sp::ceiling, 0.0f) == "0.0 dBTP");
    setParam (p, sp::truePeak, 0.0f);
    CHECK (text (sp::ceiling, -1.0f) == "-1.0 dBFS");
    setParam (p, sp::truePeak, 1.0f);
    CHECK (text (sp::release, 180.0f) == "180 ms");
    CHECK (text (sp::release, 20.0f) == "20 ms");
    CHECK (text (sp::lookahead, 3.0f) == "3.0 ms");
    CHECK (text (sp::lookahead, 0.0f) == "0.0 ms");
    CHECK (text (sp::stereoLink, 100.0f) == "100 %");
    CHECK (text (sp::softClipAmount, 28.0f) == "28 %");
    // RELEASE knob centre = 250 ms (the skin prints 20 / 250 / 1K)
    CHECK_NEAR (p.apvts.getParameter (sp::release)->convertFrom0to1 (0.5f), 250.0f, 0.01f);
    // OUTPUT knob centre = 0 dB
    CHECK_NEAR (p.apvts.getParameter (sp::output)->convertFrom0to1 (0.5f), 0.0f, 1e-6f);
    // 0.1 dB steps
    CHECK_NEAR (p.apvts.getParameter (sp::input)->convertFrom0to1 (p.apvts.getParameter (sp::input)->convertTo0to1 (3.4567f)), 3.5f, 1e-5);
    CHECK_NEAR (p.apvts.getParameter (sp::ceiling)->convertFrom0to1 (p.apvts.getParameter (sp::ceiling)->convertTo0to1 (-0.333f)), -0.3f, 1e-5);
    // text entry
    CHECK_NEAR (p.apvts.getParameter (sp::release)->getValueForText ("0.5 s"), p.apvts.getParameter (sp::release)->convertTo0to1 (500.0f), 1e-5);
    CHECK_NEAR (p.apvts.getParameter (sp::ceiling)->convertFrom0to1 (p.apvts.getParameter (sp::ceiling)->getValueForText ("-0.3 dBTP")), -0.3f, 1e-5);

    // buses: mono and stereo, nothing else
    SteelProcessor::BusesLayout l;
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

TEST ("STEEL: oversampling and true-peak filters: passband flat to 20 kHz at 44.1 kHz, images rejected, exact integer latencies")
{
    const auto& s = st::HalfbandSet::get();
    double worstPass = 0.0, worstStop = -300.0;
    // stage 1 at 2 fs: pass 0..20 kHz of 44.1 kHz -> 0..0.2268; stop from 24.1 kHz -> 0.2732
    for (double f = 0.0; f <= 0.2268; f += 0.0005)
        worstPass = std::max (worstPass, std::abs (dbOf (s.stage[0].magnitude (f))));
    for (double f = 0.2732; f <= 0.5; f += 0.0005)
        worstStop = std::max (worstStop, dbOf (s.stage[0].magnitude (f)));
    MEASURE ("stage 1 passband ripple 0-20 kHz @44.1 (dB)", worstPass);
    MEASURE ("stage 1 stopband from 24.1 kHz @44.1 (dB)", worstStop);
    CHECK (worstPass < 0.001);
    CHECK (worstStop < -85.0);
    for (int k = 1; k < 3; ++k)
    {
        // stage k+1 at 2^(k+1) fs: pass 0..20 kHz, stop where aliases would land below 24.1 kHz
        const double rate = 44100.0 * (1 << (k + 1));
        double wp = 0.0, ws = -300.0;
        for (double f = 0.0; f <= 20000.0 / rate; f += 0.0005)
            wp = std::max (wp, std::abs (dbOf (s.stage[k].magnitude (f))));
        for (double f = (rate / 2 - 24100.0) / rate; f <= 0.5; f += 0.0005)
            ws = std::max (ws, dbOf (s.stage[k].magnitude (f)));
        MEASURE ("stage " + juce::String (k + 1) + " passband ripple (dB)", wp);
        MEASURE ("stage " + juce::String (k + 1) + " stopband (dB)", ws);
        CHECK (wp < 0.001);
        CHECK (ws < -85.0);
    }
    const int expected[] = { 0, 63, 72, 75 };
    for (int o = 0; o < 4; ++o)
    {
        MEASURE (juce::String ("oversampling round trip latency ") + osName (o) + " (host samples)", st::OversamplingLatency::hostSamples (o));
        CHECK (st::OversamplingLatency::hostSamples (o) == expected[o]);
    }
}

TEST ("STEEL: reported latency equals the measured impulse delay for every LOOKAHEAD and OVERSAMPLING, 44.1-192 kHz, and follows changes (CSV)")
{
    juce::String csv = "fs,oversampling,lookahead_ms,reported,measured\n";
    for (double fs : { 44100.0, 48000.0, 96000.0, 192000.0 })
        for (int o = 0; o < 4; ++o)
            for (float la : { 0.0f, 0.1f, 1.0f, 3.0f, 5.5f, 10.0f })
            {
                auto p = makeSteel (fs, 256);
                setParam (*p, sp::oversampling, (float) o);
                setParam (*p, sp::lookahead, la);
                p->prepareToPlay (fs, 256);
                const int reported = p->getLatencySamples();
                const int n = 8192;
                auto in = silence (2, n);
                in.setSample (0, 1000, 0.25f);
                in.setSample (1, 1000, 0.25f);
                const auto out = render (*p, in, 256);
                int arg = 0;
                float best = 0.0f;
                for (int i = 0; i < n; ++i)
                    if (std::abs (out.getSample (0, i)) > best) { best = std::abs (out.getSample (0, i)); arg = i; }
                const int measured = arg - 1000;
                csv << fs << "," << osName (o) << "," << la << "," << reported << "," << measured << "\n";
                const int expect = st::OversamplingLatency::hostSamples (o) + st::SteelEngine::lookaheadSamples (la, fs)
                                   + p->getEngineForTesting().ceilingStage().latency();
                CHECK_MSG (reported == measured && reported == expect,
                           juce::String (fs) + " " + osName (o) + " " + juce::String (la) + " ms: reported " + juce::String (reported) + " measured " + juce::String (measured));
                // below the ceiling nothing is limited: at 1x the impulse passes exactly; the
                // oversampling filters band-limit it to 20 kHz (peak 0.983 of the original)
                if (o == 0) CHECK_NEAR (best, 0.25f, 1e-6);
                else CHECK (best > 0.24f && best <= 0.25f);
            }
    outFile ("STEEL_latency.csv").replaceWithText (csv);

    // changes while running: latency follows, and the steady-state impulse delay matches
    const double fs = 48000.0;
    auto p = makeSteel (fs, 128);
    const auto before = p->getLatencySamples();
    setParam (*p, sp::lookahead, 7.0f);
    setParam (*p, sp::oversampling, 3.0f);
    auto warm = render (*p, silence (2, 128), 128);
    const int after = p->getLatencySamples();
    CHECK (after == 75 + 336 + p->getEngineForTesting().ceilingStage().latency());
    CHECK (after != before);
    render (*p, silence (2, 48000), 128); // let the slot crossfade finish
    auto in = silence (2, 8192);
    in.setSample (0, 2000, 0.25f);
    const auto out = render (*p, in, 128);
    int arg = 0;
    for (int i = 0; i < 8192; ++i)
        if (std::abs (out.getSample (0, i)) > std::abs (out.getSample (0, arg))) arg = i;
    CHECK (arg - 2000 == after);
}


namespace
{
    // ---- ceiling-compliance signals (all start and end in silence) ----------------
    juce::AudioBuffer<float> ceilingSignal (int kind, double fs, double seconds)
    {
        const int n = (int) (seconds * fs);
        auto fadeEdges = [&] (juce::AudioBuffer<float>& b)
        {
            const int f = (int) (0.01 * fs);
            for (int ch = 0; ch < b.getNumChannels(); ++ch)
                for (int i = 0; i < f; ++i)
                {
                    const float g = (float) i / f;
                    b.getWritePointer (ch)[i] *= g;
                    b.getWritePointer (ch)[b.getNumSamples() - 1 - i] *= g;
                }
        };
        switch (kind)
        {
            case 0: return musicLike (fs, seconds, 1.0f);
            case 1: // naive square, 0 dBFS (Gibbs overshoot on reconstruction)
            {
                juce::AudioBuffer<float> b (2, n);
                for (int i = 0; i < n; ++i)
                {
                    const float v = std::fmod (i * 97.0 / fs, 1.0) < 0.5 ? 1.0f : -1.0f;
                    b.setSample (0, i, v);
                    b.setSample (1, i, -v);
                }
                fadeEdges (b);
                return b;
            }
            case 2: // fs/4 sine at 45 degrees: samples at 0.707, true peak 1.0 (+3 dB over the sample peak)
                return toBuffer (tpSine (fs, fs / 4.0, 1.0f, seconds, 45.0), 2);
            case 3: // alternating +1 +1 -1 -1 (fs/4 square): true peak +3 dB over the samples
            {
                juce::AudioBuffer<float> b (2, n);
                for (int i = 0; i < n; ++i)
                {
                    const float v = (i / 2) % 2 == 0 ? 1.0f : -1.0f;
                    b.setSample (0, i, v);
                    b.setSample (1, i, (i / 3) % 2 == 0 ? 1.0f : -1.0f); // fs/6 square
                }
                fadeEdges (b);
                return b;
            }
            case 4: // fs/6 sine at 60 degrees and an HF 19 kHz-ish tone (0.4 fs), stereo
            {
                auto l = tpSine (fs, fs / 6.0, 1.0f, seconds, 60.0);
                auto r = tpSine (fs, 0.4 * fs, 1.0f, seconds, 30.0);
                juce::AudioBuffer<float> b (2, n);
                b.copyFrom (0, 0, l.data(), n);
                b.copyFrom (1, 0, r.data(), n);
                return b;
            }
            default: // full-band white noise, +6 dB crest, loud
            {
                auto b = noise (2, n, 0.5f, 5);
                fadeEdges (b);
                return b;
            }
        }
    }
    const char* signalName (int k) { static const char* n[] = { "music", "square97Hz", "fs/4@45deg", "alt+1+1-1-1", "fs/6@60+0.4fs", "noise" }; return n[k]; }

    struct CeilingResult { double sp, tp, meterTp; };

    // Renders a ceiling-compliance signal and returns the output's sample peak, its true
    // peak by the independent reference (SincReference) and the plugin's own max TP reading.
    CeilingResult runCeiling (double fs, int block, int mode, int os, float la, int kind, float inputDb, float ceilingDb,
                              bool truePeak, double seconds, juce::AudioBuffer<float>* keep = nullptr, int channels = 2)
    {
        auto p = makeSteel (fs, block, channels);
        setParam (*p, sp::mode, (float) mode);
        setParam (*p, sp::oversampling, (float) os);
        setParam (*p, sp::lookahead, la);
        setParam (*p, sp::input, inputDb);
        setParam (*p, sp::ceiling, ceilingDb);
        setParam (*p, sp::truePeak, truePeak ? 1.0f : 0.0f);
        p->prepareToPlay (fs, block);
        auto in = ceilingSignal (kind, fs, seconds);
        // room for the latency at the end
        juce::AudioBuffer<float> padded (channels, in.getNumSamples() + 2048);
        padded.clear();
        for (int ch = 0; ch < channels; ++ch)
            padded.copyFrom (ch, 0, in, ch, 0, in.getNumSamples());
        auto out = render (*p, padded, block);
        CHECK (allFinite (out));
        const auto r = p->getLoudness();
        if (keep) *keep = out;
        return { samplePeak (out), sincTruePeak (out, 0.3), r.maxTruePeakDbtp };
    }

    // Allowed true-peak excess over the ceiling by the independent reference (MODEL_SPEC 6.3):
    // programme and test vectors 0.05 dB; full-band noise and naive squares (energy up to
    // fs/2, where the reconstruction itself is definition-dependent) 0.1 dB.
    double tpTolerance (int kind) { return kind == 1 || kind == 3 || kind == 5 ? 0.1 : 0.05; }
}

TEST ("STEEL: CEILING holds at 48 kHz for every MODE, OVERSAMPLING and LOOKAHEAD: true peak by an independent reference, sample peak exact with TRUE PEAK off (CSV)")
{
    const double fs = 48000.0;
    const float ceil = -1.0f;
    juce::String csv = "mode,oversampling,lookahead_ms,signal,true_peak_on,input_db,out_sample_peak_dbfs,out_true_peak_ref_dbtp,meter_max_tp_dbtp\n";
    double worstMusic = -100.0, worstHard = -100.0, worstSp = -100.0, worstMeterErr = 0.0;
    auto run = [&] (int m, int o, float la, int kind, bool tp, float inDb)
    {
        const auto r = runCeiling (fs, 512, m, o, la, kind, inDb, ceil, tp, kind == 0 ? 1.0 : 0.6);
        const double spDb = dbOf (r.sp), tpDb = dbOf (r.tp);
        csv << modeName (m) << "," << osName (o) << "," << la << "," << signalName (kind) << "," << (tp ? 1 : 0) << "," << inDb << ","
            << juce::String (spDb, 4) << "," << juce::String (tpDb, 4) << "," << juce::String (r.meterTp, 4) << "\n";
        const auto tag = juce::String (modeName (m)) + " " + osName (o) + " LA " + juce::String (la) + " " + signalName (kind) + (tp ? " TP" : " SP");
        if (tp)
        {
            double& worst = tpTolerance (kind) > 0.06 ? worstHard : worstMusic;
            worst = std::max (worst, tpDb - ceil);
            CHECK_MSG (tpDb <= ceil + tpTolerance (kind), tag + ": true peak " + juce::String (tpDb, 4) + " dBTP");
            worstMeterErr = std::max (worstMeterErr, std::abs (r.meterTp - tpDb));
            CHECK_MSG (std::abs (r.meterTp - tpDb) < 0.1, tag + ": meter " + juce::String (r.meterTp, 4) + " vs reference " + juce::String (tpDb, 4));
        }
        worstSp = std::max (worstSp, spDb - ceil);
        CHECK_MSG (spDb <= ceil + 1.0e-4, tag + ": sample peak " + juce::String (spDb, 6) + " dBFS");
    };
    for (int m = 0; m < 4; ++m)
        for (int o = 0; o < 4; ++o)
            for (float la : { 0.0f, 1.0f, 3.0f, 10.0f })
                run (m, o, la, 0, true, 9.0f);
    for (int o = 0; o < 4; ++o)
        for (float la : { 0.0f, 3.0f })
            for (int kind = 1; kind < 6; ++kind)
                for (bool tp : { true, false })
                    run (3, o, la, kind, tp, 6.0f);
    for (int m = 0; m < 3; ++m)
        for (int kind : { 2, 5 })
            run (m, 2, 3.0f, kind, true, 6.0f);
    for (int o = 0; o < 4; ++o)
        run (3, o, 3.0f, 0, false, 9.0f);
    MEASURE ("worst true-peak excess, programme and vectors (dB)", worstMusic);
    MEASURE ("worst true-peak excess, full-band noise and squares (dB)", worstHard);
    MEASURE ("worst sample-peak excess (dB)", worstSp);
    MEASURE ("worst |plugin max TP - reference| (dB)", worstMeterErr);
    outFile ("STEEL_ceiling_48k.csv").replaceWithText (csv);
}

TEST ("STEEL: CEILING holds at 44.1-192 kHz, block sizes 1-2048, mono and stereo, and for CEILING settings -12..0 dB (CSV)")
{
    juce::String csv = "fs,block,channels,oversampling,signal,ceiling_db,out_true_peak_ref_dbtp,out_sample_peak_dbfs\n";
    auto run = [&] (double fs, int block, int channels, int o, int kind, float ceil, double seconds)
    {
        const auto r = runCeiling (fs, block, 3, o, 3.0f, kind, 8.0f, ceil, true, seconds, nullptr, channels);
        const double tpDb = dbOf (r.tp);
        csv << fs << "," << block << "," << channels << "," << osName (o) << "," << signalName (kind) << "," << ceil << ","
            << juce::String (tpDb, 4) << "," << juce::String (dbOf (r.sp), 4) << "\n";
        CHECK_MSG (tpDb <= ceil + tpTolerance (kind) && dbOf (r.sp) <= ceil + 1.0e-4,
                   juce::String (fs) + " Hz block " + juce::String (block) + " " + juce::String (channels) + " ch " + osName (o) + " "
                       + signalName (kind) + " ceiling " + juce::String (ceil) + ": " + juce::String (tpDb, 4) + " dBTP");
    };
    for (double fs : { 44100.0, 88200.0, 96000.0, 176400.0, 192000.0 })
        for (int o : { 0, 2 })
            for (int kind : { 0, 2 })
                run (fs, 512, 2, o, kind, -1.0f, 0.6);
    for (int block : { 1, 64, 2048 })
        run (48000.0, block, 2, 2, 0, -1.0f, 0.6);
    for (int o : { 0, 3 })
        for (int kind : { 0, 4 })
            run (48000.0, 333, 1, o, kind, -1.0f, 0.6);
    for (float ceil : { -12.0f, -6.0f, -0.3f, 0.0f })
        run (48000.0, 512, 2, 2, 0, ceil, 0.6);
    outFile ("STEEL_ceiling_rates.csv").replaceWithText (csv);
}

// =====================================================================================
namespace
{
    // A metering instance: host Bypass on (the output is the delayed input, bit-exact),
    // 1x, LOOKAHEAD 0, so loudness and true-peak readings see the test signal itself.
    std::unique_ptr<SteelProcessor> makeMeter (double fs, int block, int channels = 2)
    {
        auto p = makeSteel (fs, block, channels);
        setParam (*p, sp::bypass, 1.0f);
        setParam (*p, sp::oversampling, 0.0f);
        setParam (*p, sp::lookahead, 0.0f);
        p->prepareToPlay (fs, block);
        return p;
    }

    // 1 kHz sine (phase-continuous) through level segments {dBFS, seconds}, every channel equal.
    juce::AudioBuffer<float> toneSegments (double fs, std::vector<std::pair<double, double>> segs, int channels = 2, double hz = 1000.0)
    {
        int n = 0;
        for (auto& sgm : segs) n += (int) std::lround (sgm.second * fs);
        juce::AudioBuffer<float> b (channels, n);
        int pos = 0;
        for (auto& sgm : segs)
        {
            const int len = (int) std::lround (sgm.second * fs);
            const double a = std::pow (10.0, sgm.first / 20.0);
            for (int i = 0; i < len; ++i, ++pos)
                for (int ch = 0; ch < channels; ++ch)
                    b.setSample (ch, pos, (float) (a * std::sin (2.0 * pi * hz * pos / fs)));
        }
        return b;
    }

    // Renders in 100 ms blocks and returns the readings after every block.
    std::vector<st::LoudnessReadings> loudnessRun (double fs, const juce::AudioBuffer<float>& in)
    {
        const int hop = (int) std::lround (0.1 * fs);
        auto p = makeMeter (fs, hop, in.getNumChannels());
        std::vector<st::LoudnessReadings> r;
        juce::AudioBuffer<float> io (in);
        juce::MidiBuffer midi;
        for (int pos = 0; pos + hop <= io.getNumSamples(); pos += hop)
        {
            juce::AudioBuffer<float> view (io.getArrayOfWritePointers(), io.getNumChannels(), pos, hop);
            p->processBlock (view, midi);
            r.push_back (p->getLoudness());
        }
        return r;
    }
}

TEST ("STEEL: true-peak meters (input after INPUT, output) read sines within 0.2 dB of the analytic peak at 44.1-192 kHz, and agree with the reference on programme (CSV)")
{
    juce::String csv = "fs,signal,expected_out_dbtp,out_meter_dbtp,expected_in_dbtp,in_meter_dbtp,loudness_max_tp,loudness_max_sp\n";
    double worst = 0.0;
    for (double fs : { 44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0 })
    {
        struct Sig { double hz, phase; juce::String name; };
        std::vector<Sig> sigs { { 997.0, 0.0, "997 Hz" }, { 5000.0, 30.0, "5 kHz" }, { 10000.0, 15.0, "10 kHz" },
                                { std::min (20000.0, 0.45 * fs), 0.0, "20 kHz or 0.45 fs" } };
        if (fs < 60000.0)
        {
            sigs.push_back ({ fs / 4.0, 45.0, "fs/4 at 45 deg" });
            sigs.push_back ({ fs / 6.0, 60.0, "fs/6 at 60 deg" });
            sigs.push_back ({ fs / 4.0, 0.0, "fs/4 at 0 deg" });
        }
        for (auto& sg : sigs)
        {
            auto p = makeSteel (fs, 512);
            setParam (*p, sp::bypass, 1.0f);
            setParam (*p, sp::oversampling, 0.0f);
            setParam (*p, sp::input, 6.0f);
            p->prepareToPlay (fs, 512);
            const auto in = toBuffer (tpSine (fs, sg.hz, 0.5f, 0.5, sg.phase), 2);
            const auto out = render (*p, in, 512);
            const float outTp = std::max (p->takeOutputTruePeakDb (0), p->takeOutputTruePeakDb (1));
            const float inTp = std::max (p->takeInputTruePeakDb (0), p->takeInputTruePeakDb (1));
            const auto r = p->getLoudness();
            const double expOut = 20.0 * std::log10 (0.5), expIn = expOut + 20.0 * std::log10 (juce::Decibels::decibelsToGain (6.0f));
            csv << fs << "," << sg.name << "," << juce::String (expOut, 4) << "," << juce::String (outTp, 4) << "," << juce::String (expIn, 4) << ","
                << juce::String (inTp, 4) << "," << juce::String (r.maxTruePeakDbtp, 4) << "," << juce::String (r.maxSamplePeakDbfs, 4) << "\n";
            const auto tag = juce::String (fs) + " Hz " + sg.name;
            CHECK_MSG (std::abs (outTp - expOut) <= 0.2, tag + ": output " + juce::String (outTp, 4));
            CHECK_MSG (std::abs (inTp - expIn) <= 0.2, tag + ": input " + juce::String (inTp, 4));
            CHECK_MSG (std::abs (r.maxTruePeakDbtp - outTp) < 1.0e-4, tag + ": loudness max TP = output meter");
            CHECK_MSG (std::abs (r.maxSamplePeakDbfs - dbOf (samplePeak (out))) < 1.0e-4, tag + ": max sample peak");
            CHECK_MSG (outTp >= r.maxSamplePeakDbfs - 1.0e-4, tag + ": true peak below the sample peak");
            worst = std::max ({ worst, std::abs (outTp - expOut), std::abs (inTp - expIn) });
            // a take reads only what arrived since the previous one (here: silence after the tail)
            render (*p, silence (2, 4096), 512);
            p->takeOutputTruePeakDb (0);
            p->takeInputTruePeakDb (0);
            render (*p, silence (2, 4096), 512);
            CHECK_MSG (p->takeOutputTruePeakDb (0) == -120.0f && p->takeInputTruePeakDb (0) == -120.0f, tag + ": take() resets");
        }
    }
    MEASURE ("worst true-peak meter error on sines, 44.1-192 kHz (dB)", worst);
    CHECK (worst < 0.02); // design target (MODEL_SPEC 6.2); BS.1770 itself asks for 0.2 dB
    outFile ("STEEL_truepeak_meter.csv").replaceWithText (csv);

    // isolated band-limited pulses at fractional positions: a single peak, so the reading cannot
    // benefit from a grid point landing near some other cycle's peak (as a long sine does)
    double worstPulse = 0.0;
    for (double fs : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        const double bw = std::min (0.8, 40000.0 / fs); // sinc bandwidth: 0.4 fs, at most 20 kHz
        for (double frac : { 0.0, 0.0625, 0.125, 0.1875, 0.25, 0.3125, 0.4, 0.5 })
        {
            auto p = makeMeter (fs, 512);
            juce::AudioBuffer<float> b (2, 4096);
            for (int i = 0; i < 4096; ++i)
            {
                const double t = i - 2000.0 - frac, r = t / 400.0;
                const double w = std::abs (r) < 1.0 ? std::pow (std::cos (0.5 * pi * r), 2.0) : 0.0;
                const double v = 0.5 * (std::abs (t) < 1e-12 ? 1.0 : std::sin (pi * bw * t) / (pi * bw * t)) * w;
                b.setSample (0, i, (float) v);
                b.setSample (1, i, (float) v);
            }
            render (*p, b, 512);
            const double err = std::abs (p->takeOutputTruePeakDb (0) - 20.0 * std::log10 (0.5));
            worstPulse = std::max (worstPulse, err);
            CHECK_MSG (err < 0.02, juce::String (fs) + " Hz pulse at +" + juce::String (frac) + ": error " + juce::String (err, 4) + " dB");
        }
    }
    MEASURE ("worst true-peak meter error on isolated band-limited pulses (dB)", worstPulse);
    // the fs/4 at 45 deg vector reads +3 dB over its samples (the classic inter-sample case)
    {
        auto p = makeMeter (48000.0, 512);
        render (*p, toBuffer (tpSine (48000.0, 12000.0, 1.0f, 0.5, 45.0), 2), 512);
        const auto r = p->getLoudness();
        CHECK_NEAR (r.maxSamplePeakDbfs, -3.01, 0.02);
        CHECK_NEAR (r.maxTruePeakDbtp, 0.0, 0.05);
    }
    // programme: the meters agree with the independent reference
    double worstProg = 0.0;
    for (double fs : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        auto p = makeMeter (fs, 512);
        const auto in = musicLike (fs, 2.0, 0.9f, 23);
        const auto out = render (*p, in, 512);
        const double ref = dbOf (sincTruePeak (out, 0.3));
        const float meter = std::max (p->takeOutputTruePeakDb (0), p->takeOutputTruePeakDb (1));
        const float meterIn = std::max (p->takeInputTruePeakDb (0), p->takeInputTruePeakDb (1));
        worstProg = std::max (worstProg, std::abs (meter - ref));
        CHECK_MSG (std::abs (meter - ref) < 0.1, juce::String (fs) + ": meter " + juce::String (meter, 4) + " reference " + juce::String (ref, 4));
        CHECK_NEAR (meterIn, meter, 1.0e-4); // bypassed: input and output are the same signal
    }
    MEASURE ("worst true-peak meter vs reference on programme (dB)", worstProg);

    // mono: both indices carry the single channel
    {
        auto p = makeMeter (48000.0, 512, 1);
        render (*p, toBuffer (tpSine (48000.0, 997.0, 0.5f, 0.3, 0.0), 1), 512);
        CHECK_NEAR (p->takeOutputTruePeakDb (1), -6.02, 0.05);
        CHECK_NEAR (p->takeInputTruePeakDb (1), -6.02, 0.05);
    }
}

TEST ("STEEL: K-weighting = the BS.1770-4 coefficients at 48 kHz, the same response at 44.1-192 kHz, and the filter does what its response says")
{
    st::Biquad sh, hp;
    st::KWeighting::design (48000.0, sh, hp);
    // ITU-R BS.1770-4 Annex 1, tables 1 and 2
    CHECK_NEAR (sh.b0, 1.53512485958697, 2.0e-7);
    CHECK_NEAR (sh.b1, -2.69169618940638, 2.0e-7);
    CHECK_NEAR (sh.b2, 1.19839281085285, 2.0e-7);
    CHECK_NEAR (sh.a1, -1.69065929318241, 2.0e-7);
    CHECK_NEAR (sh.a2, 0.73248077421585, 2.0e-7);
    CHECK_NEAR (hp.b0, 1.0, 1e-12);
    CHECK_NEAR (hp.b1, -2.0, 1e-12);
    CHECK_NEAR (hp.b2, 1.0, 1e-12);
    CHECK_NEAR (hp.a1, -1.99004745483398, 2.0e-7);
    CHECK_NEAR (hp.a2, 0.99007225036621, 2.0e-7);

    st::KWeighting k48;
    k48.prepare (48000.0);
    juce::String csv = "hz,k48_db,k44_db,k96_db,k192_db\n";
    double worst = 0.0;
    for (double f = 10.0; f <= 20000.0; f *= std::pow (2.0, 1.0 / 6.0))
    {
        const double ref = dbOf (k48.magnitude (f, 48000.0));
        csv << juce::String (f, 1) << "," << juce::String (ref, 4);
        for (double fs : { 44100.0, 96000.0, 192000.0 })
        {
            st::KWeighting k;
            k.prepare (fs);
            const double d = dbOf (k.magnitude (f, fs));
            csv << "," << juce::String (d, 4);
            if (f <= 10000.0)
            {
                worst = std::max (worst, std::abs (d - ref));
                CHECK_MSG (std::abs (d - ref) < 0.05, juce::String (fs) + " Hz at " + juce::String (f, 1) + " Hz: " + juce::String (d - ref, 4));
            }
        }
        csv << "\n";
    }
    MEASURE ("worst K-weighting difference to 48 kHz, 10 Hz-10 kHz, 44.1-192 kHz (dB)", worst);
    outFile ("STEEL_kweighting.csv").replaceWithText (csv);
    // known points of the curve (48 kHz): the shelf is +4 dB at the top, the high-pass -3 dB near 38 Hz... ~ -1.4 dB at 40 Hz
    CHECK_NEAR (dbOf (k48.magnitude (997.0, 48000.0)), 0.691, 0.02);
    CHECK_NEAR (dbOf (k48.magnitude (20000.0, 48000.0)), 4.0, 0.1);
    CHECK (dbOf (k48.magnitude (20.0, 48000.0)) < -5.0);
    // the running filter has the response it reports
    for (double f : { 25.0, 100.0, 997.0, 3000.0, 12000.0 })
    {
        st::KWeighting k;
        k.prepare (48000.0);
        const auto x = sine (48000.0, f, 0.5f, 96000);
        std::vector<float> y (x.size());
        for (size_t i = 0; i < x.size(); ++i)
            y[i] = (float) k.process (x[i]);
        const int n = (int) (std::floor (48000.0 / f) * f) > 0 ? 48000 : 48000;
        const double g = toneAmplitude (y.data() + 48000, n, 48000.0, f) / toneAmplitude (x.data() + 48000, n, 48000.0, f);
        CHECK_NEAR (dbOf (g), dbOf (k.magnitude (f, 48000.0)), 0.01);
    }
}

TEST ("STEEL: loudness meter passes EBU Tech 3341 (I, S, M cases 1-5, 9, 12) at 44.1-192 kHz within 0.1 LU; mono is one channel")
{
    struct Case { const char* name; std::vector<std::pair<double, double>> segs; double expect; };
    const std::vector<Case> cases {
        { "case 1", { { -23.0, 20.0 } }, -23.0 },
        { "case 2", { { -33.0, 20.0 } }, -33.0 },
        { "case 3", { { -36.0, 10.0 }, { -23.0, 60.0 }, { -36.0, 10.0 } }, -23.0 },
        { "case 4", { { -72.0, 10.0 }, { -36.0, 10.0 }, { -23.0, 60.0 }, { -36.0, 10.0 }, { -72.0, 10.0 } }, -23.0 },
        { "case 5", { { -26.0, 20.0 }, { -20.0, 20.1 }, { -26.0, 20.0 } }, -23.0 } };
    juce::String csv = "fs,case,expected,integrated,short_term_end,momentary_end\n";
    for (double fs : { 48000.0, 44100.0, 96000.0, 192000.0 })
        for (auto& c : cases)
        {
            if (fs != 48000.0 && c.segs.size() > 1) continue; // long cases at 48 kHz only
            const auto r = loudnessRun (fs, toneSegments (fs, c.segs));
            const auto& last = r.back();
            csv << fs << "," << c.name << "," << c.expect << "," << juce::String (last.integratedLufs, 3) << ","
                << juce::String (last.shortTermLufs, 3) << "," << juce::String (last.momentaryLufs, 3) << "\n";
            const auto tag = juce::String (fs) + " " + c.name;
            CHECK_MSG (std::abs (last.integratedLufs - c.expect) <= 0.1, tag + ": I = " + juce::String (last.integratedLufs, 3));
            if (c.segs.size() == 1)
            {
                CHECK_MSG (std::abs (last.shortTermLufs - c.expect) <= 0.1, tag + ": S = " + juce::String (last.shortTermLufs, 3));
                CHECK_MSG (std::abs (last.momentaryLufs - c.expect) <= 0.1, tag + ": M = " + juce::String (last.momentaryLufs, 3));
                CHECK_NEAR (last.loudnessRangeLu, 0.0, 0.1);
                CHECK_NEAR (last.measuredSeconds, c.segs[0].second, 0.1);
            }
        }
    for (double fs : { 48000.0, 44100.0, 96000.0 })
    {
        // case 9: S constant at -23 for 1.34 s at -20 / 1.66 s at -30 dBFS
        std::vector<std::pair<double, double>> s9, s12;
        for (int k = 0; k < 8; ++k) { s9.push_back ({ -20.0, 1.34 }); s9.push_back ({ -30.0, 1.66 }); }
        for (int k = 0; k < 25; ++k) { s12.push_back ({ -20.0, 0.18 }); s12.push_back ({ -30.0, 0.22 }); }
        const auto r9 = loudnessRun (fs, toneSegments (fs, s9));
        double worstS = 0.0, worstM = 0.0;
        for (size_t i = 35; i < r9.size(); ++i)
            worstS = std::max (worstS, std::abs (r9[i].shortTermLufs + 23.0));
        const auto r12 = loudnessRun (fs, toneSegments (fs, s12));
        for (size_t i = 6; i < r12.size(); ++i)
            worstM = std::max (worstM, std::abs (r12[i].momentaryLufs + 23.0));
        MEASURE ("Tech 3341 case 9 worst |S + 23| @" + juce::String (fs), worstS);
        MEASURE ("Tech 3341 case 12 worst |M + 23| @" + juce::String (fs), worstM);
        CHECK (worstS <= 0.1);
        CHECK (worstM <= 0.1);
        csv << fs << ",case 9 worst |S+23|," << juce::String (worstS, 4) << "\n" << fs << ",case 12 worst |M+23|," << juce::String (worstM, 4) << "\n";
    }
    // mono bus: one channel, G = 1 (a -23 dBFS mono tone is -26 LUFS, not dual mono)
    {
        const auto r = loudnessRun (48000.0, toneSegments (48000.0, { { -23.0, 10.0 } }, 1));
        CHECK_NEAR (r.back().integratedLufs, -26.01, 0.1);
    }
    // silence: no reading
    {
        const auto r = loudnessRun (48000.0, silence (2, 48000 * 3));
        CHECK (! std::isfinite (r.back().integratedLufs) && ! std::isfinite (r.back().momentaryLufs) && r.back().loudnessRangeLu == 0.0f);
    }
    outFile ("STEEL_tech3341.csv").replaceWithText (csv);
}

TEST ("STEEL: loudness range passes EBU Tech 3342 cases 1-4 within 1 LU, and matches an independent computation on a continuous level ramp within 0.1 LU")
{
    struct Case { const char* name; std::vector<std::pair<double, double>> segs; double expect; };
    const std::vector<Case> cases {
        { "case 1", { { -20.0, 20.0 }, { -30.0, 20.0 } }, 10.0 },
        { "case 2", { { -20.0, 20.0 }, { -15.0, 20.0 } }, 5.0 },
        { "case 3", { { -40.0, 20.0 }, { -20.0, 20.0 } }, 20.0 },
        { "case 4", { { -50.0, 20.0 }, { -35.0, 20.0 }, { -20.0, 20.0 }, { -35.0, 20.0 }, { -50.0, 20.0 } }, 15.0 } };
    juce::String csv = "case,expected_lu,lra_lu,integrated_lufs\n";
    for (auto& c : cases)
    {
        const auto r = loudnessRun (48000.0, toneSegments (48000.0, c.segs));
        csv << c.name << "," << c.expect << "," << juce::String (r.back().loudnessRangeLu, 3) << "," << juce::String (r.back().integratedLufs, 3) << "\n";
        MEASURE (juce::String ("Tech 3342 ") + c.name + " LRA (LU)", r.back().loudnessRangeLu);
        CHECK_MSG (std::abs (r.back().loudnessRangeLu - c.expect) <= 1.0, juce::String (c.name) + ": " + juce::String (r.back().loudnessRangeLu, 3));
    }

    // a continuous loudness distribution: a 60 s level ramp from -40 to -10 dBFS. The reference
    // computes the short-term values of the ramp analytically and applies the Tech 3342 statistics
    // with a plain sort, independently of the meter's histogram
    {
        const double fs = 48000.0, T = 60.0;
        const int n = (int) (T * fs);
        juce::AudioBuffer<float> in (2, n);
        for (int i = 0; i < n; ++i)
        {
            const double a = std::pow (10.0, (-40.0 + 30.0 * i / n) / 20.0);
            const float v = (float) (a * std::sin (2.0 * pi * 1000.0 * i / fs));
            in.setSample (0, i, v);
            in.setSample (1, i, v);
        }
        const auto r = loudnessRun (fs, in);
        std::vector<double> sv;
        const double dt = 0.001;
        for (int k = 30; k <= (int) std::lround (T * 10.0); ++k)
        {
            const double t1 = 0.1 * k, t0 = t1 - 3.0;
            double e = 0.0;
            for (double t = t0 + 0.5 * dt; t < t1; t += dt)
                e += std::pow (10.0, (-40.0 + 30.0 * t / T) / 10.0) * dt;
            sv.push_back (10.0 * std::log10 (e / 3.0));
        }
        double pm = 0.0;
        for (auto v : sv) pm += std::pow (10.0, v / 10.0);
        const double relGate = 10.0 * std::log10 (pm / (double) sv.size()) - 20.0;
        std::vector<double> gated;
        for (auto v : sv) if (v > -70.0 && v > relGate) gated.push_back (v);
        std::sort (gated.begin(), gated.end());
        const auto c = (double) gated.size();
        const double expect = gated[(size_t) std::llround (0.95 * (c - 1))] - gated[(size_t) std::llround (0.10 * (c - 1))];
        MEASURE ("LRA of a 60 s -40..-10 dBFS ramp: meter (LU)", r.back().loudnessRangeLu);
        MEASURE ("LRA of a 60 s -40..-10 dBFS ramp: reference (LU)", expect);
        CHECK_NEAR (r.back().loudnessRangeLu, expect, 0.1);
        csv << "level ramp," << juce::String (expect, 3) << "," << juce::String (r.back().loudnessRangeLu, 3) << "," << juce::String (r.back().integratedLufs, 3) << "\n";
    }
    outFile ("STEEL_tech3342.csv").replaceWithText (csv);
}

TEST ("STEEL: gain-reduction telemetry is the gain actually applied (output / aligned input at 1x), history slices match the output, GR <= 0, current and take readings")
{
    const double fs = 48000.0;
    const int n = 144000; // 3 s = 300 slices
    auto music = musicLike (fs, 3.0, 1.0f, 5);
    juce::AudioBuffer<float> in (2, n);
    in.copyFrom (0, 0, music, 0, 0, n);
    in.copyFrom (1, 0, music, 0, 0, n); // equal channels: the linked gain is each channel's own
    {
        auto p = makeSteel (fs, 256);
        setParam (*p, sp::oversampling, 0.0f);
        setParam (*p, sp::input, 10.0f);
        p->prepareToPlay (fs, 256);
        std::vector<float> cap;
        cap.reserve ((size_t) n + 16);
        p->setGainCaptureForTesting (&cap);
        const auto out = render (*p, in, 256);
        p->setGainCaptureForTesting (nullptr);
        CHECK (cap.size() == (size_t) n);
        const int L = p->getLatencySamples();
        const float G = juce::Decibels::decibelsToGain (10.0f);
        double worstRel = 0.0;
        float minCap = 1.0f;
        for (int i = L; i < n && i < (int) cap.size(); ++i)
        {
            const float u = in.getSample (0, i - L) * G;
            minCap = std::min (minCap, cap[(size_t) i]);
            if (std::abs (u) > 1.0e-3f)
                worstRel = std::max (worstRel, std::abs ((double) out.getSample (0, i) / u - cap[(size_t) i]) / cap[(size_t) i]);
        }
        MEASURE ("1x: worst |output / aligned input - reported gain| (relative)", worstRel);
        MEASURE ("1x: deepest gain reduction on the test programme (dB)", dbOf (minCap));
        CHECK (worstRel < 1.0e-5);
        CHECK (minCap < 0.6f);
    }
    {
        auto p = makeSteel (fs, 256);
        setParam (*p, sp::input, 8.0f);
        p->prepareToPlay (fs, 256);
        std::vector<SteelProcessor::HistorySlice> slices (4096);
        CHECK (p->popHistory (slices.data(), 4096) == 0);
        CHECK (p->getCurrentGainReductionDb() == 0.0f);
        std::vector<float> cap;
        cap.reserve ((size_t) n + 16);
        p->setGainCaptureForTesting (&cap);
        const auto out = render (*p, in, 256);
        p->setGainCaptureForTesting (nullptr);
        const int got = p->popHistory (slices.data(), 4096);
        CHECK (got == n / 480);
        CHECK (p->popHistory (slices.data(), 4096) == 0);
        double worstPk = 0.0, worstRms = 0.0, worstGr = 0.0;
        float deepest = 0.0f;
        for (int k = 0; k < got; ++k)
        {
            float pk = 0.0f, g = 1.0f;
            double sq = 0.0;
            for (int i = k * 480; i < (k + 1) * 480; ++i)
            {
                for (int ch = 0; ch < 2; ++ch)
                {
                    pk = std::max (pk, std::abs (out.getSample (ch, i)));
                    sq += 0.5 * (double) out.getSample (ch, i) * out.getSample (ch, i);
                }
                g = std::min (g, cap[(size_t) i]);
            }
            const auto& sl = slices[(size_t) k];
            const double pkDb = pk > 0 ? std::max (-120.0, dbOf (pk)) : -120.0;
            const double rmsDb = sq > 0 ? std::max (-120.0, 10.0 * std::log10 (sq / 480.0)) : -120.0;
            const double grDb = g < 1.0f ? dbOf (g) : 0.0;
            worstPk = std::max (worstPk, std::abs (sl.outPeakDb - pkDb));
            worstRms = std::max (worstRms, std::abs (sl.outRmsDb - rmsDb));
            worstGr = std::max (worstGr, std::abs (sl.grDb - grDb));
            CHECK (sl.grDb <= 0.0f);
            deepest = std::min (deepest, sl.grDb);
        }
        MEASURE ("history: worst slice peak error (dB)", worstPk);
        MEASURE ("history: worst slice RMS error (dB)", worstRms);
        MEASURE ("history: worst slice GR error (dB)", worstGr);
        CHECK (worstPk < 1.0e-3 && worstRms < 1.0e-2 && worstGr < 1.0e-3);
        CHECK (deepest < -3.0f);
        if (got > 0)
        {
            CHECK_NEAR (p->getCurrentGainReductionDb(), slices[(size_t) got - 1].grDb, 1.0e-6);
            CHECK_NEAR (std::min (p->getCurrentGainReductionDb (0), p->getCurrentGainReductionDb (1)), p->getCurrentGainReductionDb(), 1.0e-6);
        }
        CHECK_NEAR (p->takeGainReductionDb(), deepest, 1.0e-3);
        CHECK (p->takeGainReductionDb() == 0.0f); // nothing since the previous take
        render (*p, silence (2, 48000 * 12), 256);
        p->takeGainReductionDb();
        render (*p, silence (2, 4800), 256);
        CHECK (p->takeGainReductionDb() > -0.001f); // recovered
        CHECK (p->getCurrentGainReductionDb() > -0.001f);
        // the FIFO keeps the newest half when the reader is away (editor closed)
        render (*p, silence (2, 48000 * 30), 512);
        const int kept = p->popHistory (slices.data(), 4096);
        CHECK (kept == st::HistoryFifo::capacity / 2);
    }
    {
        // below the ceiling nothing is reduced: GR exactly 0 everywhere
        auto p = makeSteel (fs, 256);
        setParam (*p, sp::input, -12.0f);
        p->prepareToPlay (fs, 256);
        render (*p, in, 256);
        std::vector<SteelProcessor::HistorySlice> slices (4096);
        const int got = p->popHistory (slices.data(), 4096);
        bool allZero = got == n / 480;
        for (int k = 0; k < got; ++k)
            allZero = allZero && slices[(size_t) k].grDb == 0.0f;
        CHECK (allZero);
        CHECK (p->takeGainReductionDb() == 0.0f);
    }
}

// =====================================================================================
namespace
{
    struct Capture
    {
        std::vector<float> gain; // applied gain per output sample (deeper channel)
        juce::AudioBuffer<float> out;
        int latency = 0;
    };

    Capture runCapture (SteelProcessor& p, const juce::AudioBuffer<float>& in, int block = 256)
    {
        Capture c;
        c.gain.reserve ((size_t) in.getNumSamples() + 16);
        p.setGainCaptureForTesting (&c.gain);
        c.out = render (p, in, block);
        p.setGainCaptureForTesting (nullptr);
        c.latency = p.getLatencySamples();
        return c;
    }

    // silence `pre` s, a sine of `amp` for `len` s (starting at a zero crossing), silence `post` s
    juce::AudioBuffer<float> burst (double fs, double pre, double len, double post, float amp, double hz = 1000.0, int channels = 2)
    {
        const int a = (int) std::lround (pre * fs), b = (int) std::lround (len * fs), c = (int) std::lround (post * fs);
        juce::AudioBuffer<float> buf (channels, a + b + c);
        buf.clear();
        for (int i = 0; i < b; ++i)
            for (int ch = 0; ch < channels; ++ch)
                buf.setSample (ch, a + i, amp * (float) std::sin (2.0 * pi * hz * i / fs));
        return buf;
    }

    // release time constant from a gain capture: from the last sample at the deepest
    // gain, the time until 1 - g has fallen to 1/e of its value there (ms)
    double releaseTauMs (const std::vector<float>& g, double fs)
    {
        float gmin = 1.0f;
        for (auto v : g) gmin = std::min (gmin, v);
        int i0 = 0;
        for (int i = 0; i < (int) g.size(); ++i)
            if (g[(size_t) i] <= gmin + 1.0e-4f * (1.0f - gmin)) i0 = i;
        const double target = (1.0 - g[(size_t) i0]) / std::exp (1.0);
        for (int i = i0; i < (int) g.size(); ++i)
            if (1.0 - g[(size_t) i] <= target)
                return 1000.0 * (i - i0) / fs;
        return 1.0e9;
    }

    std::unique_ptr<SteelProcessor> makeConfigured (double fs, std::initializer_list<std::pair<const char*, float>> settings, int block = 256, int channels = 2)
    {
        auto p = makeSteel (fs, block, channels);
        for (auto [id, v] : settings)
            setParam (*p, id, v);
        p->prepareToPlay (fs, block);
        return p;
    }

    double meanGrDb (const std::vector<float>& g, int from = 0)
    {
        double s = 0.0;
        for (int i = from; i < (int) g.size(); ++i)
            s += dbOf (g[(size_t) i]);
        return s / std::max (1, (int) g.size() - from);
    }
}

TEST ("STEEL: the four MODEs are measurably distinct: attack onset, AUTO release, soft knee, LF distortion, programme loudness and crest (CSV)")
{
    const double fs = 48000.0;
    const int laHost = st::SteelEngine::lookaheadSamples (5.0, fs);
    juce::String csv = "mode,attack_lead_samples,attack_fraction_x_lookahead,auto_release_tau_ms,spec_fast_ms,knee_gr_at_minus1_db,thd_50hz_6db_over_pct,programme_lufs_i,programme_crest_db\n";
    double lead[4] {}, tau[4] {}, knee[4] {}, thdv[4] {}, lufsI[4] {}, crest[4] {};
    for (int m = 0; m < 4; ++m)
    {
        const auto& spec = st::modeSpec (m);
        {
            auto p = makeConfigured (fs, { { sp::mode, (float) m }, { sp::lookahead, 5.0f }, { sp::ceiling, -6.0f } });
            const auto c = runCapture (*p, burst (fs, 0.2, 0.05, 1.5, 1.0f));
            int onset = 0;
            while (onset < (int) c.gain.size() && c.gain[(size_t) onset] >= 0.97f) ++onset;
            lead[m] = (int) std::lround (0.2 * fs) + c.latency - onset;
            tau[m] = releaseTauMs (c.gain, fs);
        }
        {
            auto p = makeConfigured (fs, { { sp::mode, (float) m }, { sp::lookahead, 5.0f }, { sp::ceiling, -6.0f } });
            const auto c = runCapture (*p, toBuffer (sine (fs, 1000.0, juce::Decibels::decibelsToGain (-7.0f), 48000), 2));
            float g = 1.0f;
            for (int i = 24000; i < 48000; ++i) g = std::min (g, c.gain[(size_t) i]);
            knee[m] = dbOf (g);
        }
        {
            auto p = makeConfigured (fs, { { sp::mode, (float) m }, { sp::ceiling, -6.0f } });
            const auto c = runCapture (*p, toBuffer (sine (fs, 50.0, 1.0f, 3 * 48000), 2));
            thdv[m] = 100.0 * thd (c.out.getReadPointer (0) + 2 * 48000, 48000, fs, 50.0);
        }
        {
            auto p = makeConfigured (fs, { { sp::mode, (float) m }, { sp::input, 8.0f } });
            const auto c = runCapture (*p, musicLike (fs, 4.0, 1.0f, 3));
            lufsI[m] = p->getLoudness().integratedLufs;
            double sq = 0.0, pk = 0.0;
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < c.out.getNumSamples(); ++i)
                {
                    sq += (double) c.out.getSample (ch, i) * c.out.getSample (ch, i);
                    pk = std::max (pk, (double) std::abs (c.out.getSample (ch, i)));
                }
            crest[m] = dbOf (pk) - 10.0 * std::log10 (sq / (2.0 * c.out.getNumSamples()));
        }
        csv << modeName (m) << "," << lead[m] << "," << juce::String (spec.attackFraction * laHost, 1) << "," << juce::String (tau[m], 2) << ","
            << spec.autoFastMs << "," << juce::String (knee[m], 4) << "," << juce::String (thdv[m], 4) << "," << juce::String (lufsI[m], 3) << ","
            << juce::String (crest[m], 3) << "\n";
        MEASURE (juce::String (modeName (m)) + ": attack lead (samples)", lead[m]);
        MEASURE (juce::String (modeName (m)) + ": AUTO release tau after an isolated peak (ms)", tau[m]);
        MEASURE (juce::String (modeName (m)) + ": GR on a sine 1 dB below the ceiling (dB)", knee[m]);
        MEASURE (juce::String (modeName (m)) + ": THD at 50 Hz, 6 dB over (%)", thdv[m]);
        MEASURE (juce::String (modeName (m)) + ": programme LUFS-I", lufsI[m]);
        MEASURE (juce::String (modeName (m)) + ": programme crest (dB)", crest[m]);
        CHECK_MSG (std::abs (tau[m] - spec.autoFastMs) <= 0.2 * spec.autoFastMs, juce::String (modeName (m)) + ": tau " + juce::String (tau[m], 2));
        // the gain starts to move attackFraction x LOOKAHEAD before the peak; the 0.26 dB
        // threshold is crossed a little later on the S-shaped ramp
        CHECK_MSG (lead[m] <= spec.attackFraction * laHost + 2 && lead[m] >= 0.6 * spec.attackFraction * laHost,
                   juce::String (modeName (m)) + ": lead " + juce::String (lead[m]));
    }
    outFile ("STEEL_modes.csv").replaceWithText (csv);
    CHECK (lead[0] > lead[3] && lead[3] > lead[1]);                 // CLEAN > STEEL > PUNCH
    CHECK (tau[1] < tau[2] && tau[2] < tau[3] && tau[3] < tau[0]);  // PUNCH < DENSE < STEEL < CLEAN
    // only DENSE's 3 dB knee reaches 1 dB below the ceiling: GR = (0.5 dB)^2 / (2 x 3 dB) (Giannoulis et al.)
    CHECK_NEAR (knee[2], -0.25 / 6.0, 0.005);
    CHECK (knee[0] == 0.0 && knee[1] == 0.0 && knee[3] == 0.0);
    CHECK (thdv[0] < thdv[1]);                                     // CLEAN is the cleanest at LF, PUNCH the dirtiest
    for (int a = 0; a < 4; ++a)
        for (int b = a + 1; b < 4; ++b)
            CHECK_MSG (std::max (std::abs (lufsI[a] - lufsI[b]), std::abs (crest[a] - crest[b])) >= 0.1,
                       juce::String (modeName (a)) + " vs " + modeName (b) + ": same programme result");
}

namespace
{
    // 60 Hz bed at 0 dBFS with a 4 ms 3 kHz transient every 0.5 s (peak +6 dB over the bed)
    juce::AudioBuffer<float> pumpSignal (double fs, double seconds)
    {
        const int n = (int) (seconds * fs);
        juce::AudioBuffer<float> b (2, n);
        const int period = (int) (0.5 * fs), len = (int) (0.004 * fs);
        for (int i = 0; i < n; ++i)
        {
            double v = std::sin (2.0 * pi * 60.0 * i / fs);
            const int k = i % period;
            if (k < len)
                v += 1.0 * std::sin (pi * k / len) * std::sin (2.0 * pi * 3000.0 * i / fs);
            b.setSample (0, i, (float) v);
            b.setSample (1, i, (float) v);
        }
        return b;
    }

    struct Pumping { double pumpDb, rippleDb; };

    // pump: how much deeper the GR is 20-300 ms after each transient than in the 100 ms before it;
    // ripple: the GR's standard deviation within those 100 ms (LF waveform distortion)
    Pumping measurePumping (const Capture& c, double fs, double fromSeconds)
    {
        const int period = (int) (0.5 * fs);
        double pump = 0.0, ripple = 0.0;
        int count = 0;
        for (int t = period * (int) std::ceil (fromSeconds / 0.5); t + period < (int) c.gain.size() - c.latency; t += period)
        {
            const int o = t + c.latency;
            double before = 0.0, after = 0.0, sq = 0.0;
            const int nb = (int) (0.1 * fs), na0 = (int) (0.02 * fs), na1 = (int) (0.3 * fs);
            for (int i = o - nb; i < o; ++i) { const double d = dbOf (c.gain[(size_t) i]); before += d; sq += d * d; }
            for (int i = o + na0; i < o + na1; ++i) after += dbOf (c.gain[(size_t) i]);
            before /= nb;
            after /= (na1 - na0);
            pump += before - after;
            ripple += std::sqrt (std::max (0.0, sq / nb - before * before));
            ++count;
        }
        return { pump / std::max (1, count), ripple / std::max (1, count) };
    }
}

TEST ("STEEL: AUTO release does not pump on transients over a sustained bed, without the LF distortion of a fast release; it is programme-dependent (CSV)")
{
    const double fs = 48000.0;
    const auto sig = pumpSignal (fs, 8.0);
    juce::String csv = "mode,release,pump_db,ripple_db\n";
    for (int m = 0; m < 4; ++m)
    {
        auto run = [&] (bool autoRel, float ms)
        {
            auto p = makeConfigured (fs, { { sp::mode, (float) m }, { sp::ceiling, -6.0f }, { sp::releaseMode, autoRel ? 0.0f : 1.0f }, { sp::release, ms } });
            const auto c = runCapture (*p, sig);
            const auto r = measurePumping (c, fs, 3.0);
            csv << modeName (m) << "," << (autoRel ? juce::String ("AUTO") : juce::String (ms) + " ms") << "," << juce::String (r.pumpDb, 3) << "," << juce::String (r.rippleDb, 3) << "\n";
            return r;
        };
        const auto a = run (true, 180.0f), fast = run (false, 20.0f), slow = run (false, 1000.0f);
        const auto tag = juce::String (modeName (m));
        MEASURE (tag + " AUTO: pumping after transients (dB)", a.pumpDb);
        MEASURE (tag + " AUTO: GR ripple on the bed (dB)", a.rippleDb);
        MEASURE (tag + " MANUAL 20 ms: GR ripple on the bed (dB)", fast.rippleDb);
        MEASURE (tag + " MANUAL 1000 ms: pumping after transients (dB)", slow.pumpDb);
        CHECK_MSG (a.pumpDb < 0.5 * slow.pumpDb, tag + ": AUTO pumps like a slow release");
        // PUNCH and DENSE ride faster on purpose (30 / 50 ms fast branch): less ripple than
        // 20 ms; CLEAN and STEEL at most half of it
        CHECK_MSG (a.rippleDb < (m == 1 || m == 2 ? 0.9 : 0.5) * fast.rippleDb, tag + ": AUTO ripples like a fast release");
        CHECK_MSG (a.pumpDb < 1.0, tag + ": AUTO pumping " + juce::String (a.pumpDb, 3) + " dB");
    }
    outFile ("STEEL_auto_release.csv").replaceWithText (csv);

    // programme dependency, shown by the effective release: isolated peaks recover at the mode's
    // fast time, sustained limiting moves it towards the slow one
    {
        auto p = makeConfigured (fs, { { sp::ceiling, -6.0f } });
        juce::AudioBuffer<float> sparse (2, 4 * 48000);
        sparse.clear();
        for (int k = 0; k < 4; ++k)
        {
            const auto b = burst (fs, 0.0, 0.01, 0.0, 1.0f);
            for (int ch = 0; ch < 2; ++ch)
                sparse.copyFrom (ch, k * 48000, b, ch, 0, b.getNumSamples());
        }
        render (*p, sparse, 256);
        const double eSparse = p->getEffectiveReleaseMs();
        render (*p, toBuffer (sine (fs, 60.0, 1.0f, 3 * 48000), 2), 256);
        const double eDense = p->getEffectiveReleaseMs();
        MEASURE ("STEEL AUTO effective release, isolated peaks (ms)", eSparse);
        MEASURE ("STEEL AUTO effective release, sustained limiting (ms)", eDense);
        // sustained: the slow branch holds autoSlowDepth of the GR, so the effective release is
        // fast^(1 - depth) x slow^depth; isolated peaks leave mostly the fast branch's work
        const auto& spec = st::modeSpec (3);
        const double sustained = std::pow ((double) spec.autoFastMs, 1.0 - spec.autoSlowDepth) * std::pow ((double) spec.autoSlowReleaseMs, (double) spec.autoSlowDepth);
        CHECK_NEAR (eDense, sustained, 0.05 * sustained);
        CHECK (eSparse < 0.75 * eDense);
        CHECK (eSparse > spec.autoFastMs && eSparse < 0.5 * spec.autoSlowReleaseMs);
        setParam (*p, sp::releaseMode, 1.0f);
        setParam (*p, sp::release, 333.0f);
        render (*p, silence (2, 256), 256);
        CHECK_NEAR (p->getEffectiveReleaseMs(), 333.0, 0.5);
    }
}

TEST ("STEEL: MANUAL release time constant = the RELEASE knob (20 ms-1 s, measured); AUTO ignores the knob (bit-exact) (CSV)")
{
    const double fs = 48000.0;
    juce::String csv = "release_ms,oversampling,measured_tau_ms\n";
    double worst = 0.0;
    for (float ms : { 20.0f, 50.0f, 180.0f, 500.0f, 1000.0f })
        for (int o : { 0, 2, 3 })
        {
            auto p = makeConfigured (fs, { { sp::releaseMode, 1.0f }, { sp::release, ms }, { sp::ceiling, -6.0f }, { sp::oversampling, (float) o } });
            const auto c = runCapture (*p, burst (fs, 0.1, 0.2, 5.0 * ms * 0.001 + 0.2, 1.0f));
            const double tau = releaseTauMs (c.gain, fs);
            csv << ms << "," << osName (o) << "," << juce::String (tau, 3) << "\n";
            worst = std::max (worst, std::abs (tau - ms) / ms);
            CHECK_MSG (std::abs (tau - ms) <= 0.05 * ms + 1.0, juce::String (ms) + " ms " + osName (o) + ": measured " + juce::String (tau, 3));
        }
    MEASURE ("worst relative error of the MANUAL release time constant", worst);
    outFile ("STEEL_manual_release.csv").replaceWithText (csv);

    const auto prog = musicLike (fs, 2.0, 1.0f, 9);
    auto a = makeConfigured (fs, { { sp::input, 8.0f }, { sp::release, 20.0f } });
    auto b = makeConfigured (fs, { { sp::input, 8.0f }, { sp::release, 1000.0f } });
    CHECK (identical (render (*a, prog, 256), render (*b, prog, 256)));
    auto c = makeConfigured (fs, { { sp::input, 8.0f }, { sp::release, 1000.0f }, { sp::releaseMode, 1.0f } });
    CHECK (! identical (render (*a, prog, 256), render (*c, prog, 256)));
}

TEST ("STEEL: STEREO LINK: 100 % = one gain for both channels, 0 % = independent, 50 % = halfway (linear gain)")
{
    const double fs = 48000.0;
    juce::AudioBuffer<float> in (2, 48000);
    const auto l = sine (fs, 1000.0, 1.0f, 48000), r = sine (fs, 1000.0, 0.1f, 48000);
    in.copyFrom (0, 0, l.data(), 48000);
    in.copyFrom (1, 0, r.data(), 48000);
    for (int o : { 0, 2 })
    {
        float grL[3] {}, grR[3] {};
        double rmsR[3] {};
        const float links[3] = { 0.0f, 50.0f, 100.0f };
        for (int k = 0; k < 3; ++k)
        {
            auto p = makeConfigured (fs, { { sp::stereoLink, links[k] }, { sp::ceiling, -6.0f }, { sp::oversampling, (float) o } });
            const auto out = render (*p, in, 256);
            grL[k] = p->getCurrentGainReductionDb (0);
            grR[k] = p->getCurrentGainReductionDb (1);
            rmsR[k] = rms (out.getReadPointer (1) + 24000, 24000);
            MEASURE (juce::String (osName (o)) + " link " + juce::String (links[k]) + ": GR left / right (dB)", grL[k]);
            MEASURE (juce::String (osName (o)) + " link " + juce::String (links[k]) + ": GR right (dB)", grR[k]);
        }
        const double rmsIn = rms (r.data() + 24000, 24000);
        CHECK_NEAR (grL[0], -6.0, 0.1);
        CHECK (grR[0] == 0.0f);
        CHECK_NEAR (dbOf (rmsR[0] / rmsIn), 0.0, 0.01);
        CHECK_NEAR (grR[2], grL[2], 0.01);
        CHECK_NEAR (dbOf (rmsR[2] / rmsIn), grL[2], 0.1);
        const double expect = dbOf (1.0 + 0.5 * (std::pow (10.0, grL[1] / 20.0) - 1.0));
        CHECK_NEAR (grR[1], expect, 0.1);
        CHECK_NEAR (grL[1], grL[0], 0.05); // the louder channel is not affected by the link
    }
}

TEST ("STEEL: SOFT CLIP takes peak load off the limiter by AMOUNT, scaled per MODE; the ceiling still holds; oversampling removes its aliasing (CSV)")
{
    const double fs = 48000.0;
    const auto prog = musicLike (fs, 3.0, 1.0f, 13);
    auto grFor = [&] (int mode, bool clip, float amount, juce::AudioBuffer<float>* keep = nullptr)
    {
        auto p = makeConfigured (fs, { { sp::mode, (float) mode }, { sp::input, 8.0f }, { sp::softClip, clip ? 1.0f : 0.0f }, { sp::softClipAmount, amount } });
        const auto c = runCapture (*p, prog);
        if (keep) *keep = c.out;
        return meanGrDb (c.gain, 48000);
    };
    juce::AudioBuffer<float> out100;
    const double off = grFor (3, false, 30.0f), a0 = grFor (3, true, 0.0f), a30 = grFor (3, true, 30.0f), a100 = grFor (3, true, 100.0f, &out100);
    MEASURE ("STEEL mean limiter GR, SOFT CLIP off (dB)", off);
    MEASURE ("STEEL mean limiter GR, SOFT CLIP 0 % (dB)", a0);
    MEASURE ("STEEL mean limiter GR, SOFT CLIP 30 % (dB)", a30);
    MEASURE ("STEEL mean limiter GR, SOFT CLIP 100 % (dB)", a100);
    CHECK (off <= a0 && a0 < a30 && a30 < a100);
    CHECK (a100 - off > 1.0);
    CHECK (dbOf (sincTruePeak (out100, 0.3)) <= -1.0 + 0.05);

    // MODE scale: a sine 1 dB below the ceiling is never limited (no knee reaches it except
    // DENSE's, which only rides it), so its distortion is the clipper's alone. At AMOUNT 60 %
    // the effective amounts are 0.30 / 0.48 / 0.90 / 0.60 (CLEAN / PUNCH / DENSE / STEEL):
    // CLEAN's knee is still above the sine, the others clip it progressively harder.
    double thdm[4] {};
    for (int m = 0; m < 4; ++m)
    {
        auto p = makeConfigured (fs, { { sp::mode, (float) m }, { sp::softClip, 1.0f }, { sp::softClipAmount, 60.0f } });
        const auto out = render (*p, toBuffer (sine (fs, 1000.0, juce::Decibels::decibelsToGain (-2.0f), 48000), 2), 256);
        thdm[m] = 100.0 * thd (out.getReadPointer (0) + 24000, 24000, fs, 1000.0);
        MEASURE (juce::String (modeName (m)) + ": SOFT CLIP 60 % THD on a sine 1 dB below the ceiling (%)", thdm[m]);
    }
    CHECK (thdm[0] < 0.01);
    CHECK (thdm[0] < thdm[1] && thdm[1] < thdm[3] && thdm[3] < thdm[2]);

    // aliasing: 7 kHz at 6 dB over the ceiling, AMOUNT 100 %: odd harmonics above fs/2 fold back to
    // 13, 1, 15 and 19 kHz unless the clipper runs oversampled
    juce::String csv = "oversampling,alias_db_re_fundamental,thd_pct\n";
    double alias[4] {};
    for (int o = 0; o < 4; ++o)
    {
        auto p = makeConfigured (fs, { { sp::oversampling, (float) o }, { sp::softClip, 1.0f }, { sp::softClipAmount, 100.0f }, { sp::ceiling, -6.0f } });
        const auto out = render (*p, toBuffer (sine (fs, 7000.0, 1.0f, 48000), 2), 256);
        const float* y = out.getReadPointer (0) + 24000;
        const double f0 = toneAmplitude (y, 24000, fs, 7000.0);
        double e = 0.0;
        for (double hz : { 1000.0, 13000.0, 15000.0, 19000.0, 5000.0, 9000.0 })
            e += std::pow (toneAmplitude (y, 24000, fs, hz), 2.0);
        alias[o] = 10.0 * std::log10 (e / (f0 * f0) + 1.0e-30);
        csv << osName (o) << "," << juce::String (alias[o], 2) << "," << juce::String (100.0 * thd (y, 24000, fs, 7000.0), 3) << "\n";
        MEASURE (juce::String ("SOFT CLIP aliasing at ") + osName (o) + " (dB re fundamental)", alias[o]);
    }
    outFile ("STEEL_softclip_aliasing.csv").replaceWithText (csv);
    CHECK (alias[2] < alias[0] - 20.0);
    CHECK (alias[3] < alias[0] - 20.0);
    CHECK (alias[2] < -60.0);
}

TEST ("STEEL: SUB FILTER: 24 dB/oct Butterworth high-pass at 20 / 25 / 30 Hz (-3 dB at the cutoff, -24 dB an octave below), passes the rest; Off is bit-exact (CSV)")
{
    const double fs = 48000.0;
    juce::String csv = "cutoff_hz,hz,measured_db,model_db,butterworth_db\n";
    for (int idx = 1; idx <= 3; ++idx)
    {
        const double fc = sp::subFilterHz[idx];
        for (double f : { fc / 2.0, fc, 2.0 * fc, 100.0, 1000.0 })
        {
            auto p = makeConfigured (fs, { { sp::subFilter, (float) idx }, { sp::oversampling, 0.0f }, { sp::lookahead, 0.0f } });
            const int period = (int) std::lround (fs / f);
            const int n = (int) (2.0 * fs) + 20 * period;
            const auto x = sine (fs, f, 0.1f, n);
            const auto out = render (*p, toBuffer (x, 2), 512);
            const int len = 10 * period * std::max (1, (int) std::ceil (fs / (10.0 * period)));
            const int from = n - len - 1;
            const double g = toneAmplitude (out.getReadPointer (0) + from, len, fs, f) / 0.1;
            st::SubFilter model;
            model.prepare (fs);
            model.setCutoff (fc);
            const double bw = -10.0 * std::log10 (1.0 + std::pow (fc / f, 8.0));
            csv << fc << "," << juce::String (f, 2) << "," << juce::String (dbOf (g), 3) << "," << juce::String (model.magnitudeDb (f), 3) << "," << juce::String (bw, 3) << "\n";
            CHECK_MSG (std::abs (dbOf (g) - bw) <= (f < fc ? 0.3 : 0.1), juce::String (fc) + " Hz at " + juce::String (f) + ": " + juce::String (dbOf (g), 3));
            CHECK_NEAR (dbOf (g), model.magnitudeDb (f), 0.05);
        }
    }
    outFile ("STEEL_subfilter.csv").replaceWithText (csv);

    // Off: exact identity (1x, below the ceiling: output = delayed input, bit for bit)
    auto p = makeConfigured (fs, { { sp::oversampling, 0.0f } });
    const auto prog = musicLike (fs, 1.0, 0.5f, 4);
    const auto out = render (*p, prog, 256);
    const int L = p->getLatencySamples();
    bool exact = true;
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < out.getNumSamples(); ++i)
        {
            const bool same = sameBits (out.getSample (ch, i), i >= L ? prog.getSample (ch, i - L) : 0.0f);
            if (! same && exact)
                std::printf ("first mismatch ch %d i %d out %.9g in %.9g L %d\n", ch, i, out.getSample (ch, i), i >= L ? prog.getSample (ch, i - L) : 0.0f, L);
            exact = exact && same;
        }
    CHECK (exact);
}

TEST ("STEEL: the tests' own true-peak reference is validated: analytic sine peaks, and the ideal (FFT) reconstruction on programme")
{
    double worstSine = 0.0;
    for (double fs : { 44100.0, 48000.0, 96000.0 })
        for (auto [hz, ph] : { std::pair { 997.0, 0.0 }, std::pair { fs / 4.0, 45.0 }, std::pair { fs / 6.0, 60.0 }, std::pair { 0.45 * fs, 10.0 } })
        {
            const auto b = toBuffer (tpSine (fs, hz, 0.5f, 0.2, ph), 1);
            worstSine = std::max (worstSine, std::abs (dbOf (sincTruePeak (b, 0.1)) - dbOf (0.5)));
        }
    MEASURE ("reference: worst error on sines up to 0.45 fs (dB)", worstSine);
    CHECK (worstSine < 0.01);
    // programme band-limited below 0.45 fs: the windowed-sinc reference = ideal interpolation
    const double fs = 48000.0;
    auto prog = musicLike (fs, 1.0, 1.0f, 77);
    auto p = makeConfigured (fs, { { sp::oversampling, 3.0f }, { sp::input, 6.0f } }); // band-limited by the 8x filters, limited
    const auto out = render (*p, prog, 512);
    const double ideal = dbOf (referenceTruePeak (out, 0, 0.5)), ref = dbOf (sincTruePeak (out, 0.3));
    MEASURE ("reference vs FFT-ideal reconstruction on limited programme (dB)", ref - ideal);
    CHECK (std::abs (ref - ideal) < 0.03); // the reference is -6 dB at fs/2 (DAC-like); the ideal is not
}

TEST ("STEEL: DELTA outputs exactly what the clipper, limiter and ceiling stage removed, at the input level, without OUTPUT or LEVEL MATCH; silent when nothing is limited")
{
    const double fs = 48000.0;
    const auto prog = musicLike (fs, 2.0, 1.0f, 17);
    const float G = juce::Decibels::decibelsToGain (9.0f);
    auto normal = makeConfigured (fs, { { sp::oversampling, 0.0f }, { sp::input, 9.0f }, { sp::softClip, 1.0f } });
    auto delta = makeConfigured (fs, { { sp::oversampling, 0.0f }, { sp::input, 9.0f }, { sp::softClip, 1.0f }, { sp::delta, 1.0f } });
    const auto y = render (*normal, prog, 256), d = render (*delta, prog, 256);
    const int L = normal->getLatencySamples();
    double worst = 0.0, energy = 0.0;
    for (int ch = 0; ch < 2; ++ch)
        for (int i = L; i < prog.getNumSamples(); ++i)
        {
            const float u = prog.getSample (ch, i - L) * G;
            const double expect = ((double) u - y.getSample (ch, i)) / G;
            worst = std::max (worst, std::abs (d.getSample (ch, i) - expect));
            energy += expect * expect;
        }
    writeWav (outFile ("STEEL_programme_in.wav"), prog, fs);
    writeWav (outFile ("STEEL_programme_out_input9_softclip_1x.wav"), y, fs);
    writeWav (outFile ("STEEL_programme_delta.wav"), d, fs);
    MEASURE ("DELTA: worst |output - (u - y) / INPUT| at 1x", worst);
    CHECK (worst < 1.0e-6);
    CHECK (energy > 1.0);
    // OUTPUT and LEVEL MATCH do not scale the difference signal
    auto delta2 = makeConfigured (fs, { { sp::oversampling, 0.0f }, { sp::input, 9.0f }, { sp::softClip, 1.0f }, { sp::delta, 1.0f },
                                        { sp::output, -7.0f }, { sp::levelMatch, 1.0f } });
    CHECK (identical (d, render (*delta2, prog, 256)));
    // nothing limited: DELTA is digital silence (1x) or the band-limiting residue only (4x)
    auto quiet = makeConfigured (fs, { { sp::oversampling, 0.0f }, { sp::input, -12.0f }, { sp::delta, 1.0f } });
    CHECK (render (*quiet, prog, 256).getMagnitude (0, prog.getNumSamples()) == 0.0f);
    auto quiet4 = makeConfigured (fs, { { sp::input, -12.0f }, { sp::delta, 1.0f } });
    const auto q4 = render (*quiet4, prog, 256);
    const double resid = dbOf (rms (q4.getReadPointer (0) + 4800, prog.getNumSamples() - 4800) / rms (prog.getReadPointer (0), prog.getNumSamples()));
    MEASURE ("DELTA at 4x with nothing limited: residue re input (dB)", resid);
    CHECK (resid < -30.0);
    // INPUT +24 dB: the difference stays bounded by the input (no explosion)
    auto hot = makeConfigured (fs, { { sp::input, 24.0f }, { sp::delta, 1.0f } });
    const auto h = render (*hot, prog, 256);
    CHECK (allFinite (h));
    MEASURE ("DELTA at INPUT +24 dB: output peak / input peak", h.getMagnitude (0, h.getNumSamples()) / prog.getMagnitude (0, prog.getNumSamples()));
    CHECK (h.getMagnitude (0, h.getNumSamples()) <= prog.getMagnitude (0, prog.getNumSamples()) * 1.1f);
}

TEST ("STEEL: LEVEL MATCH brings the output back to the input loudness (3 s K-weighted), never boosts, and reports its gain")
{
    const double fs = 48000.0;
    const auto prog = musicLike (fs, 12.0, 0.5f, 21);
    auto lufsOf = [&] (const juce::AudioBuffer<float>& b, int from)
    {
        st::LoudnessMeter m;
        m.prepare (fs, 2);
        const float* ptr[2] { b.getReadPointer (0) + from, b.getReadPointer (1) + from };
        m.process (ptr, nullptr, b.getNumSamples() - from);
        return (double) m.read().integratedLufs;
    };
    const int from = (int) (6.0 * fs);
    const double inL = lufsOf (prog, from);
    for (float drive : { 6.0f, 12.0f })
    {
        auto off = makeConfigured (fs, { { sp::input, drive } });
        auto on = makeConfigured (fs, { { sp::input, drive }, { sp::levelMatch, 1.0f } });
        const auto a = render (*off, prog, 512), b = render (*on, prog, 512);
        const double offL = lufsOf (a, from), onL = lufsOf (b, from);
        MEASURE ("LEVEL MATCH at INPUT +" + juce::String (drive) + ": output loudness off / on re input (LU)", offL - inL);
        MEASURE ("LEVEL MATCH at INPUT +" + juce::String (drive) + ": on re input (LU)", onL - inL);
        CHECK (offL - inL > 2.0);
        CHECK_NEAR (onL, inL, 0.5);
        CHECK_NEAR (on->getLevelMatchGainDb(), inL - offL, 0.5);
        CHECK (off->getLevelMatchGainDb() == 0.0f);
    }
    // quieter than the input (INPUT -12, OUTPUT -6): no boost, output identical to LEVEL MATCH off
    auto off = makeConfigured (fs, { { sp::input, -12.0f }, { sp::output, -6.0f } });
    auto on = makeConfigured (fs, { { sp::input, -12.0f }, { sp::output, -6.0f }, { sp::levelMatch, 1.0f } });
    CHECK (identical (render (*off, prog, 512), render (*on, prog, 512)));
    CHECK (on->getLevelMatchGainDb() == 0.0f);
}

TEST ("STEEL: warnings: TP OVERS = output true peak above CEILING + 0.1 dB within the last 2 s; HEAVY LIMITING = mean GR over 2 s >= 4 dB; both exact")
{
    const double fs = 48000.0;
    {
        // TRUE PEAK off at 1x: inter-sample overs pass, the warning shows and holds 2.0 s
        auto p = makeConfigured (fs, { { sp::truePeak, 0.0f }, { sp::oversampling, 0.0f }, { sp::input, 6.0f } }, 480);
        CHECK (! p->isTruePeakOver());
        render (*p, toBuffer (tpSine (fs, 12000.0, 1.0f, 0.5, 45.0), 2), 480);
        CHECK (p->isTruePeakOver());
        CHECK (p->getLoudness().maxTruePeakDbtp > -1.0f + 2.0f);
        render (*p, silence (2, (int) (1.9 * fs)), 480);
        CHECK (p->isTruePeakOver());
        render (*p, silence (2, (int) (0.2 * fs)), 480);
        CHECK (! p->isTruePeakOver());
    }
    {
        // TRUE PEAK on: hot programme and the inter-sample vectors never trigger it
        for (int kind : { 0, 2, 4 })
        {
            auto p = makeConfigured (fs, { { sp::input, 9.0f } }, 480);
            bool any = false;
            const auto sig = ceilingSignal (kind, fs, 2.0);
            juce::AudioBuffer<float> io (sig);
            juce::MidiBuffer midi;
            for (int pos = 0; pos + 480 <= io.getNumSamples(); pos += 480)
            {
                juce::AudioBuffer<float> view (io.getArrayOfWritePointers(), 2, pos, 480);
                p->processBlock (view, midi);
                any = any || p->isTruePeakOver();
            }
            CHECK_MSG (! any, signalName (kind));
        }
    }
    {
        // HEAVY LIMITING: a 60 Hz tone whose level rises from 0 to 9 dB over the ceiling over 12 s;
        // after every slice the flag equals (mean per-sample GR of the last 2 s <= -4 dB)
        const int n = (int) (12.0 * fs);
        juce::AudioBuffer<float> in (2, n);
        for (int i = 0; i < n; ++i)
        {
            const float a = juce::Decibels::decibelsToGain (-6.0f + 9.0f * (float) i / n);
            in.setSample (0, i, a * (float) std::sin (2.0 * pi * 60.0 * i / fs));
            in.setSample (1, i, in.getSample (0, i));
        }
        auto p = makeConfigured (fs, { { sp::ceiling, -6.0f } }, 480);
        std::vector<float> cap;
        cap.reserve ((size_t) n + 16);
        p->setGainCaptureForTesting (&cap);
        juce::MidiBuffer midi;
        int mismatches = 0, onAt = -1, checked = 0;
        double sum = 0.0;
        std::vector<double> sliceSum;
        for (int pos = 0; pos + 480 <= n; pos += 480)
        {
            juce::AudioBuffer<float> view (in.getArrayOfWritePointers(), 2, pos, 480);
            p->processBlock (view, midi);
            double ssum = 0.0;
            for (int i = pos; i < pos + 480; ++i)
                ssum += cap[(size_t) i] < 1.0f ? dbOf (cap[(size_t) i]) : 0.0;
            sliceSum.push_back (ssum);
            sum += ssum;
            if (sliceSum.size() > 200)
                sum -= sliceSum[sliceSum.size() - 201];
            const double mean = sum / (200.0 * 480.0); // the window starts with 2 s of zeros
            const bool expect = mean <= -4.0;
            if (std::abs (mean + 4.0) > 1.0e-3)
            {
                ++checked;
                if (expect != p->isHeavyLimiting()) ++mismatches;
            }
            if (onAt < 0 && p->isHeavyLimiting()) onAt = pos;
        }
        p->setGainCaptureForTesting (nullptr);
        MEASURE ("HEAVY LIMITING switched on at (s)", onAt / fs);
        CHECK (checked > 1000);
        CHECK (mismatches == 0);
        CHECK (onAt > 0);
        render (*p, silence (2, (int) (2.1 * fs)), 480);
        CHECK (! p->isHeavyLimiting());
    }
}

namespace
{
    // 4th-order Butterworth high-pass at 8 kHz (two RBJ biquads): the click detector
    std::vector<double> clickBand (const float* x, int n, double fs)
    {
        st::Biquad a, b;
        auto design = [fs] (st::Biquad& q, double f0, double Q)
        {
            const double w = 2.0 * pi * f0 / fs, al = std::sin (w) / (2.0 * Q), c = std::cos (w), a0 = 1.0 + al;
            q.b0 = (1.0 + c) / 2.0 / a0; q.b1 = -(1.0 + c) / a0; q.b2 = (1.0 + c) / 2.0 / a0;
            q.a1 = -2.0 * c / a0; q.a2 = (1.0 - al) / a0;
        };
        design (a, 8000.0, 0.5411961);
        design (b, 8000.0, 1.3065630);
        std::vector<double> y ((size_t) n);
        for (int i = 0; i < n; ++i)
            y[(size_t) i] = b.process (a.process (x[i]));
        return y;
    }

    double maxAbs (const std::vector<double>& v, int from, int to)
    {
        double m = 0.0;
        for (int i = std::max (0, from); i < std::min ((int) v.size(), to); ++i)
            m = std::max (m, std::abs (v[(size_t) i]));
        return m;
    }
}

TEST ("STEEL: switching or jumping every control is click-free (MODE, OVERSAMPLING, LOOKAHEAD, TRUE PEAK, SUB, DELTA, Bypass included); rapid automation stays finite and under the ceiling (CSV)")
{
    // Click measure: 8 kHz 4th-order high-pass of the output of a 220 Hz tone that is being
    // limited (L 1.0 / R 0.5 with INPUT +3 dB, CEILING -1). Limit: 2x the worse of the two
    // fixed settings + 2e-4 (-74 dBFS).
    const double fs = 48000.0;
    const int block = 64;
    const int n = (int) (3.0 * fs) / block * block;
    const int s1 = (int) (1.0 * fs) / block * block + 17 * block, s2 = (int) (2.0 * fs) / block * block + 5 * block;
    struct Ctl { const char* id; float a, b; std::vector<std::pair<const char*, float>> base; };
    const std::vector<Ctl> ctls {
        { sp::input, -12.0f, 12.0f, {} }, { sp::input, 0.0f, 3.0f, {} },
        { sp::ceiling, 0.0f, -12.0f, {} }, { sp::ceiling, -1.0f, -3.0f, {} },
        { sp::mode, 0.0f, 3.0f, {} }, { sp::mode, 1.0f, 2.0f, {} },
        { sp::releaseMode, 0.0f, 1.0f, {} }, { sp::release, 20.0f, 1000.0f, { { sp::releaseMode, 1.0f } } },
        { sp::lookahead, 0.0f, 10.0f, {} }, { sp::lookahead, 3.0f, 1.3f, {} },
        { sp::truePeak, 1.0f, 0.0f, {} }, { sp::truePeak, 1.0f, 0.0f, { { sp::oversampling, 0.0f } } },
        { sp::oversampling, 0.0f, 3.0f, {} }, { sp::oversampling, 2.0f, 1.0f, {} },
        { sp::stereoLink, 0.0f, 100.0f, {} }, { sp::softClip, 0.0f, 1.0f, {} },
        { sp::softClipAmount, 0.0f, 100.0f, { { sp::softClip, 1.0f } } },
        { sp::subFilter, 0.0f, 3.0f, {} }, { sp::subFilter, 1.0f, 3.0f, {} },
        { sp::delta, 0.0f, 1.0f, {} }, { sp::levelMatch, 0.0f, 1.0f, {} },
        { sp::output, -12.0f, 12.0f, {} }, { sp::bypass, 0.0f, 1.0f, {} } };
    juce::AudioBuffer<float> tone (2, n);
    {
        const auto l = sine (fs, 220.0, 1.0f, n);
        tone.copyFrom (0, 0, l.data(), n);
        tone.copyFrom (1, 0, l.data(), n);
        tone.applyGain (1, 0, n, 0.5f);
    }
    auto run = [&] (const Ctl& c, int mode) // 0: fixed a, 1: fixed b, 2: a -> b at s1, b -> a at s2
    {
        auto p = makeSteel (fs, block);
        setParam (*p, sp::input, 3.0f);
        for (auto [id, v] : c.base) setParam (*p, id, v);
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
        CHECK (allFinite (io));
        double pk = 0.0;
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
        const auto tag = juce::String (c.id) + " " + juce::String (c.a) + " <-> " + juce::String (c.b) + (c.base.empty() ? "" : " (variant)");
        MEASURE (tag + ": 8 kHz+ peak switching (dBFS)", dbOf (sw));
        CHECK_MSG (sw < limit, tag + ": click (" + juce::String (sw, 6) + " vs " + juce::String (limit, 6) + ")");
    }
    outFile ("STEEL_switching.csv").replaceWithText (csv);

    // rapid automation: every parameter (Bypass and DELTA aside) jumps every block for 10 s
    {
        auto p = makeSteel (fs, 128);
        juce::Random rng (99);
        const auto sig = musicLike (fs, 10.0, 1.0f, 23);
        juce::AudioBuffer<float> io (sig);
        juce::MidiBuffer midi;
        float worstOver = -100.0f;
        for (int pos = 0; pos < io.getNumSamples(); pos += 128)
        {
            for (auto* param : p->getParameters())
                if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (param))
                    if (rp->paramID != sp::bypass && rp->paramID != sp::delta && rp->paramID != sp::output && rng.nextFloat() < 0.5f)
                        rp->setValueNotifyingHost (rng.nextFloat());
            juce::AudioBuffer<float> view (io.getArrayOfWritePointers(), 2, pos, std::min (128, io.getNumSamples() - pos));
            p->processBlock (view, midi);
            // CEILING moves: compare with the lowest ceiling of the recent past (it ramps over 20 ms)
            worstOver = std::max (worstOver, juce::Decibels::gainToDecibels (view.getMagnitude (0, view.getNumSamples())));
        }
        MEASURE ("rapid automation of every parameter, 10 s: output sample peak (dBFS)", worstOver);
        CHECK (allFinite (io));
        CHECK (worstOver <= 0.0f + 1.0e-4f); // CEILING <= 0 dB and OUTPUT at 0
    }
}

TEST ("STEEL: NaN / Inf / absurd input never reaches the output or poisons the state; silence gives exact silence; denormals stay clean")
{
    const double fs = 48000.0;
    for (int o : { 0, 2, 3 })
    {
        auto prog = musicLike (fs, 1.0, 0.8f, 31);
        auto bad = prog, zeroed = prog;
        const float nasties[] = { std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity() };
        for (int k = 0; k < 30; ++k)
        {
            const int i = 1000 + k * 1499;
            bad.setSample (k % 2, i, nasties[k % 3]);
            zeroed.setSample (k % 2, i, 0.0f);
        }
        auto a = makeConfigured (fs, { { sp::oversampling, (float) o }, { sp::input, 6.0f }, { sp::subFilter, 2.0f }, { sp::levelMatch, 1.0f } });
        auto b = makeConfigured (fs, { { sp::oversampling, (float) o }, { sp::input, 6.0f }, { sp::subFilter, 2.0f }, { sp::levelMatch, 1.0f } });
        const auto ya = render (*a, bad, 256), yb = render (*b, zeroed, 256);
        CHECK (allFinite (ya));
        CHECK_MSG (identical (ya, yb), juce::String (osName (o)) + ": a non-finite sample acts as a zero");
        const auto r = a->getLoudness();
        CHECK (std::isfinite (r.integratedLufs) && std::isfinite (r.maxTruePeakDbtp));
        CHECK (std::isfinite (a->getCurrentGainReductionDb()) && std::isfinite (a->takeOutputTruePeakDb (0)) && std::isfinite (a->takeInputTruePeakDb (0)));

        // absurd but finite: +200 dBFS samples are limited like any other peak
        auto hugeIn = prog;
        hugeIn.setSample (0, 5000, 1.0e10f);
        hugeIn.setSample (1, 7000, -3.0e38f);
        auto c = makeConfigured (fs, { { sp::oversampling, (float) o } });
        const auto yc = render (*c, hugeIn, 256);
        CHECK (allFinite (yc));
        CHECK (dbOf (samplePeak (yc)) <= -1.0 + 1.0e-4);
        // bypassed, the dry path passes them (sanitised only for non-finite values)
    }
    // silence in, silence out (exactly), GR 0, meters at the floor
    {
        auto p = makeConfigured (fs, {});
        const auto out = render (*p, silence (2, 48000), 256);
        CHECK (out.getMagnitude (0, 48000) == 0.0f);
        CHECK (p->takeGainReductionDb() == 0.0f);
        CHECK (p->takeOutputTruePeakDb (0) == -120.0f);
    }
    // denormal input: output finite and below -300 dBFS-equivalent, no state blow-up
    {
        auto p = makeConfigured (fs, { { sp::subFilter, 3.0f }, { sp::softClip, 1.0f } });
        juce::AudioBuffer<float> den (2, 48000);
        for (int i = 0; i < 48000; ++i)
            for (int ch = 0; ch < 2; ++ch)
                den.setSample (ch, i, (i % 2 ? 1.0f : -1.0f) * 1.0e-41f);
        const auto out = render (*p, den, 256);
        CHECK (allFinite (out));
        CHECK (out.getMagnitude (0, 48000) < 1.0e-30f);
        const auto after = render (*p, toBuffer (sine (fs, 1000.0, 0.5f, 9600), 2), 256);
        CHECK (allFinite (after) && after.getMagnitude (0, 9600) > 0.4f);
    }
}

TEST ("STEEL: all sample rates and block sizes, mono and stereo: finite, no allocation (slot crossfades and switches included), latency as reported; oversize host blocks")
{
    for (double fs : { 44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0 })
        for (int block : { 1, 16, 64, 256, 1024, 2048 })
            for (int channels : { 1, 2 })
            {
                auto p = makeSteel (fs, block, channels);
                setParam (*p, sp::mode, (float) (block % 4));
                setParam (*p, sp::input, 8.0f);
                setParam (*p, sp::oversampling, (float) (block % 3 + 1));
                setParam (*p, sp::subFilter, 1.0f);
                p->prepareToPlay (fs, block);
                const int n = std::max (block * 4, (int) (0.3 * fs));
                const auto in = musicLike (fs, (double) n / fs + 0.01, 1.0f, 3);
                juce::AudioBuffer<float> warm (channels, n);
                for (int ch = 0; ch < channels; ++ch) warm.copyFrom (ch, 0, in, ch, 0, n);
                render (*p, warm, block);
                // switches while counted: MODE, OVERSAMPLING, LOOKAHEAD start a slot crossfade
                setParam (*p, sp::mode, (float) ((block + 1) % 4));
                setParam (*p, sp::oversampling, (float) (block % 3));
                setParam (*p, sp::lookahead, 7.5f);
                setParam (*p, sp::softClip, 1.0f);
                setParam (*p, sp::truePeak, 0.0f);
                setParam (*p, sp::subFilter, 3.0f);
                setParam (*p, sp::delta, 1.0f);
                setParam (*p, sp::levelMatch, 1.0f);
                setParam (*p, sp::releaseMode, 1.0f);
                setParam (*p, sp::stereoLink, 30.0f);
                juce::AudioBuffer<float> io (warm);
                juce::MidiBuffer midi;
                long allocs;
                {
                    ScopedAllocationCounter counter;
                    for (int pos = 0; pos < n; pos += block)
                    {
                        if (pos >= n / 2 && pos < n / 2 + block)
                        {
                            setParam (*p, sp::oversampling, 3.0f); // a switch while the previous one may still fade
                            setParam (*p, sp::delta, 0.0f);
                            setParam (*p, sp::bypass, 1.0f);
                        }
                        juce::AudioBuffer<float> view (io.getArrayOfWritePointers(), channels, pos, std::min (block, n - pos));
                        p->processBlock (view, midi);
                    }
                    allocs = counter.count();
                }
                const auto tag = juce::String (fs) + " Hz, block " + juce::String (block) + ", " + juce::String (channels) + " ch";
                CHECK_MSG (allFinite (io), tag);
                CHECK_MSG (allocs == 0, tag + ": " + juce::String (allocs) + " allocations in processBlock");
                CHECK_MSG (p->getLatencySamples() == p->getEngineForTesting().latency(), tag);
                CHECK_MSG (p->getLatencySamples() == st::OversamplingLatency::hostSamples (3) + st::SteelEngine::lookaheadSamples (7.5, fs)
                                                         + p->getEngineForTesting().ceilingStage().latency(), tag);
            }
    // a host block larger than announced is processed in chunks, identically
    {
        auto p = makeSteel (48000.0, 256);
        const auto sig = musicLike (48000.0, 0.5, 1.0f, 3);
        auto big = render (*p, sig, 4096);
        auto q = makeSteel (48000.0, 256);
        CHECK (identical (big, render (*q, sig, 256)));
        auto r = makeSteel (48000.0, 256);
        CHECK (identical (big, render (*r, sig, 77)));
    }
}

TEST ("STEEL: host Bypass is bit-exact delayed dry at the reported latency (every OVERSAMPLING / LOOKAHEAD, mono and stereo); the transition is a 20 ms crossfade")
{
    for (double fs : { 44100.0, 96000.0 })
        for (int o = 0; o < 4; ++o)
            for (float la : { 0.0f, 3.0f, 10.0f })
                for (int channels : { 1, 2 })
                {
                    auto p = makeSteel (fs, 256, channels);
                    setParam (*p, sp::oversampling, (float) o);
                    setParam (*p, sp::lookahead, la);
                    setParam (*p, sp::input, 12.0f);
                    setParam (*p, sp::bypass, 1.0f);
                    p->prepareToPlay (fs, 256);
                    const auto m = musicLike (fs, 0.5, 1.0f, 8);
                    juce::AudioBuffer<float> in (channels, m.getNumSamples());
                    for (int ch = 0; ch < channels; ++ch) in.copyFrom (ch, 0, m, ch, 0, m.getNumSamples());
                    const auto out = render (*p, in, 256);
                    const int L = p->getLatencySamples();
                    bool exact = true;
                    for (int ch = 0; ch < channels; ++ch)
                        for (int i = 0; i < in.getNumSamples(); ++i)
                            exact = exact && sameBits (out.getSample (ch, i), i >= L ? in.getSample (ch, i - L) : 0.0f);
                    CHECK_MSG (exact, juce::String (fs) + " " + osName (o) + " LA " + juce::String (la) + " " + juce::String (channels) + " ch");
                }
    // transition: processed until the switch, a 20 ms linear crossfade, then exact dry
    const double fs = 48000.0;
    const auto prog = musicLike (fs, 1.0, 1.0f, 12);
    auto ref = makeConfigured (fs, { { sp::input, 9.0f } });
    const auto processed = render (*ref, prog, 240);
    auto p = makeConfigured (fs, { { sp::input, 9.0f } });
    juce::AudioBuffer<float> io (prog);
    juce::MidiBuffer midi;
    const int sw = 24000, L = p->getLatencySamples(), fade = (int) std::lround (0.02 * fs);
    for (int pos = 0; pos < io.getNumSamples(); pos += 240)
    {
        if (pos == sw) setParam (*p, sp::bypass, 1.0f);
        juce::AudioBuffer<float> view (io.getArrayOfWritePointers(), 2, pos, 240);
        p->processBlock (view, midi);
    }
    bool before = true, after = true, within = true;
    double worstFade = 0.0;
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < io.getNumSamples(); ++i)
        {
            const float dry = i >= L ? prog.getSample (ch, i - L) : 0.0f;
            if (i < sw) before = before && sameBits (io.getSample (ch, i), processed.getSample (ch, i));
            else if (i >= sw + fade) after = after && sameBits (io.getSample (ch, i), dry);
            else
            {
                const float w = (float) (i - sw + 1) / (float) fade;
                const float expect = processed.getSample (ch, i) + (dry - processed.getSample (ch, i)) * w;
                worstFade = std::max (worstFade, (double) std::abs (io.getSample (ch, i) - expect));
                within = within && std::abs (io.getSample (ch, i) - expect) < 1.0e-5f;
            }
        }
    MEASURE ("Bypass crossfade: worst deviation from a 20 ms linear crossfade", worstFade);
    CHECK (before && within && after);
}

TEST ("STEEL: state round trip restores every parameter and renders identically; foreign and corrupt states are ignored")
{
    const double fs = 48000.0;
    auto a = makeSteel (fs, 256);
    const std::pair<const char*, float> values[] = {
        { sp::input, 4.2f }, { sp::ceiling, -0.7f }, { sp::mode, 1.0f }, { sp::releaseMode, 1.0f }, { sp::release, 420.0f }, { sp::lookahead, 4.4f },
        { sp::truePeak, 0.0f }, { sp::oversampling, 3.0f }, { sp::stereoLink, 63.0f }, { sp::softClip, 1.0f }, { sp::softClipAmount, 77.0f },
        { sp::subFilter, 2.0f }, { sp::delta, 0.0f }, { sp::levelMatch, 1.0f }, { sp::output, -1.5f } };
    CHECK (std::size (values) == 15);
    for (auto [id, v] : values)
        setParam (*a, id, v);
    juce::MemoryBlock state;
    a->getStateInformation (state);
    auto b = makeSteel (fs, 256);
    b->setStateInformation (state.getData(), (int) state.getSize());
    for (auto* param : a->getParameters())
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (param))
            CHECK_MSG (std::abs (r->getValue() - b->apvts.getParameter (r->paramID)->getValue()) < 1e-6f, r->paramID);
    a->prepareToPlay (fs, 256);
    b->prepareToPlay (fs, 256);
    CHECK (a->getLatencySamples() == b->getLatencySamples());
    const auto sig = musicLike (fs, 1.0, 1.0f, 31);
    CHECK_MSG (identical (render (*a, sig, 256), render (*b, sig, 256)), "restored instance renders differently");
    auto fresh = makeSteel (fs, 256);
    CHECK (! identical (render (*fresh, sig, 256), render (*b, sig, 256)));
    b->setStateInformation ("nonsense", 8);
    CHECK_NEAR (plainValue (*b, sp::stereoLink), 63.0f, 0.01);
    auto xml = juce::AudioProcessor::getXmlFromBinary (state.getData(), (int) state.getSize());
    CHECK (xml != nullptr);
    if (xml != nullptr)
    {
        xml->setAttribute ("plugin", "VOID");
        juce::MemoryBlock foreign;
        juce::AudioProcessor::copyXmlToBinary (*xml, foreign);
        auto c = makeSteel (fs, 256);
        c->setStateInformation (foreign.getData(), (int) foreign.getSize());
        CHECK_NEAR (plainValue (*c, sp::stereoLink), 100.0f, 0.01);
    }
}

TEST ("STEEL: factory presets load, set every parameter, cover every MODE, render under their ceiling, and reject other plugins' presets")
{
    SteelProcessor p;
    const auto& list = p.presets.getPresets();
    int factory = 0;
    for (auto& pr : list) factory += pr.isFactory ? 1 : 0;
    MEASURE ("factory presets", factory);
    CHECK (factory >= 12);
    std::set<int> modes, oss, releaseModes;
    std::set<juce::String> names;
    for (int i = 0; i < (int) list.size(); ++i)
        if (list[(size_t) i].isFactory)
        {
            p.loadPreset (i);
            names.insert (list[(size_t) i].name);
            CHECK_MSG (list[(size_t) i].params.values.size() == 15, list[(size_t) i].name);
            for (auto& [id, v] : list[(size_t) i].params.values)
            {
                auto* r = p.apvts.getParameter (id);
                CHECK_MSG (r != nullptr, id);
                if (r) CHECK_MSG (std::abs (r->convertFrom0to1 (r->getValue()) - v) < 0.06f * std::max (1.0f, std::abs (v) * 0.01f), list[(size_t) i].name + ": " + id);
            }
            CHECK (! p.isPresetModified());
            modes.insert (juce::roundToInt (plainValue (p, sp::mode)));
            oss.insert (juce::roundToInt (plainValue (p, sp::oversampling)));
            releaseModes.insert (juce::roundToInt (plainValue (p, sp::releaseMode)));
            CHECK_MSG (plainValue (p, sp::bypass) < 0.5f && plainValue (p, sp::delta) < 0.5f, list[(size_t) i].name + ": a preset must not load bypassed or in DELTA");
        }
    CHECK (modes.size() == 4 && releaseModes.size() == 2 && oss.size() >= 3);
    CHECK ((int) names.size() == factory);

    setParam (p, sp::stereoLink, 13.0f);
    CHECK (p.isPresetModified());
    PresetManager::Preset out;
    CHECK (p.presets.parse (R"({"format":"tleet-preset","version":1,"plugin":"VOID","name":"x","params":{}})", out).failed());
    CHECK (p.presets.parse (R"({"format":"tleet-preset","version":99,"plugin":"STEEL","name":"x","params":{}})", out).failed());
    const auto json = p.stateToClipboardJson();
    setParam (p, sp::stereoLink, 99.0f);
    CHECK (p.pasteStateFromJson (json).wasOk());
    CHECK_NEAR (plainValue (p, sp::stereoLink), 13.0f, 0.06);

    juce::String csv = "preset,mode,ceiling_db,out_true_peak_ref_dbtp,out_lufs_i\n";
    for (int i = 0; i < (int) list.size(); ++i)
    {
        auto q = makeSteel (48000.0, 256);
        q->loadPreset (i);
        q->prepareToPlay (48000.0, 256);
        const auto sig = musicLike (48000.0, 2.0, 0.7f, 41);
        const auto o = render (*q, sig, 256);
        const float ceil = plainValue (*q, sp::ceiling) + plainValue (*q, sp::output) + (plainValue (*q, sp::levelMatch) > 0.5f ? 0.0f : 0.0f);
        const double tp = dbOf (sincTruePeak (o, 0.3 * juce::Decibels::decibelsToGain (std::min (0.0f, ceil))));
        csv << list[(size_t) i].name << "," << modeName (juce::roundToInt (plainValue (*q, sp::mode))) << "," << plainValue (*q, sp::ceiling) << ","
            << juce::String (tp, 3) << "," << juce::String (q->getLoudness().integratedLufs, 2) << "\n";
        CHECK_MSG (allFinite (o), list[(size_t) i].name);
        const double tol = plainValue (*q, sp::truePeak) > 0.5f ? 0.05 : 3.5; // sample-peak presets allow inter-sample overs
        CHECK_MSG (tp <= ceil + tol, list[(size_t) i].name + ": " + juce::String (tp, 3) + " dBTP over " + juce::String (ceil));
    }
    outFile ("STEEL_presets.csv").replaceWithText (csv);
}

TEST ("STEEL: CPU cost per instance at 44.1-192 kHz for every OVERSAMPLING (measurement)")
{
    juce::String csv = "fs,oversampling,percent_core_median_block\n";
    for (double fs : { 44100.0, 48000.0, 96000.0, 192000.0 })
        for (int o = 0; o < 4; ++o)
        {
            const int block = 128;
            auto p = makeConfigured (fs, { { sp::oversampling, (float) o }, { sp::input, 6.0f }, { sp::softClip, 1.0f }, { sp::subFilter, 1.0f },
                                           { sp::levelMatch, 1.0f } }, block);
            p->outputSpectrum.setActive (true);
            const int n = (int) (fs * 2.0);
            auto io = musicLike (fs, 2.0, 1.0f, 3);
            juce::MidiBuffer midi;
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
            const double load = times[times.size() / 2] * (fs / block) * 100.0;
            const auto tag = juce::String (osName (o)) + " @" + juce::String (fs) + " Hz, block 128, stereo, everything on";
            MEASURE (tag + ": % of one core (median block)", load);
            csv << fs << "," << osName (o) << "," << juce::String (load, 2) << "\n";
            if (fs <= 48000.0)
                CHECK_MSG (load < 20.0, tag);
            CHECK_MSG (load < 60.0, tag);
        }
    outFile ("STEEL_cpu.csv").replaceWithText (csv);
}

TEST ("STEEL: telemetry is real: IN meters after INPUT and before SUB FILTER, OUT meters on the final output, spectrum tap, loudness reset, latency change notifies the host")
{
    const double fs = 48000.0;
    {
        // a 15 Hz tone: SUB 30 removes 24 dB of it, but the IN meter reads it after INPUT (+6 dB)
        auto p = makeConfigured (fs, { { sp::input, 6.0f }, { sp::subFilter, 3.0f }, { sp::ceiling, 0.0f } });
        const auto x = sine (fs, 15.0, 0.25f, 96000);
        render (*p, toBuffer (x, 2), 512);
        p->inputMeters[0].takePeak(); p->outputMeters[0].takePeak();
        render (*p, toBuffer (sine (fs, 15.0, 0.25f, 48000, 2.0 * pi * 15.0 * 96000 / fs), 2), 512);
        const float g = juce::Decibels::decibelsToGain (6.0f);
        CHECK_NEAR (p->inputMeters[0].takePeak(), 0.25f * g, 1.0e-3);
        CHECK_NEAR (p->inputMeters[1].takeRms(), 0.25f * g / std::sqrt (2.0f), 2.0e-3);
        CHECK_NEAR (dbOf (p->outputMeters[0].takePeak() / (0.25f * g)), -24.1, 0.5);
        CHECK_NEAR (p->takeInputTruePeakDb (0), dbOf (0.25 * g), 0.01);
    }
    {
        // OUT meters include OUTPUT; mono feeds both indices
        auto p = makeConfigured (fs, { { sp::output, -6.0f }, { sp::oversampling, 0.0f } }, 512, 1);
        render (*p, toBuffer (sine (fs, 1000.0, 0.5f, 48000), 1), 512);
        p->outputMeters[1].takePeak();
        render (*p, toBuffer (sine (fs, 1000.0, 0.5f, 48000), 1), 512);
        CHECK_NEAR (p->outputMeters[1].takePeak(), 0.5f * juce::Decibels::decibelsToGain (-6.0f), 1.0e-3);
    }
    {
        // spectrum tap: inactive until the editor asks, then the final output (L + R) / 2
        auto p = makeConfigured (fs, { { sp::oversampling, 0.0f } });
        std::vector<float> buf (8192);
        render (*p, toBuffer (sine (fs, 1000.0, 0.5f, 9600), 2), 512);
        CHECK (p->outputSpectrum.pull (buf.data(), 8192) == 0);
        p->outputSpectrum.setActive (true);
        const auto out = render (*p, toBuffer (sine (fs, 1000.0, 0.5f, 9600), 2), 512);
        const int got = p->outputSpectrum.pull (buf.data(), 8192);
        CHECK (got > 4096);
        bool same = got > 0;
        for (int i = 0; i < got; ++i)
            same = same && std::abs (buf[(size_t) i] - 0.5f * (out.getSample (0, 9600 - got + i) + out.getSample (1, 9600 - got + i))) < 1.0e-6f;
        CHECK (same);
    }
    {
        // loudness reset from another thread: readings vanish at once, measuring restarts
        auto p = makeConfigured (fs, {});
        render (*p, musicLike (fs, 3.0, 0.8f, 2), 512);
        CHECK (std::isfinite (p->getLoudness().integratedLufs));
        render (*p, silence (2, 4800), 480); // the latency tail leaves the plugin
        p->resetLoudness();
        CHECK (! std::isfinite (p->getLoudness().integratedLufs) && ! std::isfinite (p->getLoudness().maxTruePeakDbtp));
        render (*p, silence (2, 4800), 480);
        const auto r = p->getLoudness();
        CHECK (! std::isfinite (r.integratedLufs) && ! std::isfinite (r.maxTruePeakDbtp));
        CHECK_NEAR (r.measuredSeconds, 0.1, 1.0e-6);
    }
    {
        // a LOOKAHEAD / OVERSAMPLING change reaches the host as a latency change
        struct L : juce::AudioProcessorListener
        {
            int latencyChanges = 0;
            void audioProcessorParameterChanged (juce::AudioProcessor*, int, float) override {}
            void audioProcessorChanged (juce::AudioProcessor*, const ChangeDetails& d) override { latencyChanges += d.latencyChanged ? 1 : 0; }
        } listener;
        auto p = makeConfigured (fs, {});
        p->addListener (&listener);
        const int before = p->getLatencySamples();
        setParam (*p, sp::lookahead, 5.0f);
        render (*p, silence (2, 512), 512);
        CHECK (listener.latencyChanges == 1);
        CHECK (p->getLatencySamples() == before + st::SteelEngine::lookaheadSamples (5.0, fs) - st::SteelEngine::lookaheadSamples (3.0, fs));
        render (*p, silence (2, 512), 512);
        CHECK (listener.latencyChanges == 1); // no repeat while unchanged
        setParam (*p, sp::oversampling, 0.0f);
        render (*p, silence (2, 512), 512);
        CHECK (listener.latencyChanges == 2);
        p->removeListener (&listener);
    }
}

TEST ("STEEL: every control audibly changes the output, each in the specified direction")
{
    const double fs = 48000.0;
    const auto prog = musicLike (fs, 3.0, 1.0f, 55);
    struct Result { juce::AudioBuffer<float> out; double meanGr; float tpDb; double rmsDb; double lfDb; };
    auto run = [&] (std::initializer_list<std::pair<const char*, float>> settings)
    {
        auto p = makeConfigured (fs, settings);
        auto c = runCapture (*p, prog);
        Result r;
        r.meanGr = meanGrDb (c.gain, 48000);
        r.tpDb = std::max (p->takeOutputTruePeakDb (0), p->takeOutputTruePeakDb (1));
        r.rmsDb = dbOf (rms (c.out.getReadPointer (0) + 48000, prog.getNumSamples() - 48000));
        // LF energy below 40 Hz (Goertzel at 25 Hz as a probe)
        r.lfDb = dbOf (toneAmplitude (c.out.getReadPointer (0) + 48000, 96000, fs, 25.0));
        r.out = std::move (c.out);
        return r;
    };
    const auto base = run ({ { sp::input, 6.0f } });
    writeWav (outFile ("STEEL_programme_out_default_input6.wav"), base.out, fs);
    auto differs = [&] (const Result& r) { return ! identical (r.out, base.out); };
    {
        const auto r = run ({ { sp::input, 10.0f } });
        CHECK (differs (r) && r.meanGr < base.meanGr - 0.5);
    }
    {
        const auto r = run ({ { sp::input, 6.0f }, { sp::ceiling, -6.0f } });
        CHECK (differs (r) && r.tpDb < base.tpDb - 4.5f);
    }
    for (int m : { 0, 1, 2 })
        CHECK_MSG (differs (run ({ { sp::input, 6.0f }, { sp::mode, (float) m } })), modeName (m));
    {
        const auto fast = run ({ { sp::input, 6.0f }, { sp::releaseMode, 1.0f }, { sp::release, 20.0f } });
        const auto slow = run ({ { sp::input, 6.0f }, { sp::releaseMode, 1.0f }, { sp::release, 1000.0f } });
        CHECK (differs (fast) && differs (slow) && slow.meanGr < fast.meanGr - 1.0);
    }
    CHECK (differs (run ({ { sp::input, 6.0f }, { sp::lookahead, 0.5f } })));
    {
        const auto r = run ({ { sp::input, 6.0f }, { sp::truePeak, 0.0f }, { sp::oversampling, 0.0f } });
        const auto t = run ({ { sp::input, 6.0f }, { sp::oversampling, 0.0f } });
        CHECK (differs (r) && sincTruePeak (r.out, 0.3) > sincTruePeak (t.out, 0.3) * 1.01);
    }
    CHECK (differs (run ({ { sp::input, 6.0f }, { sp::oversampling, 3.0f } })));
    CHECK (differs (run ({ { sp::input, 6.0f }, { sp::stereoLink, 0.0f } })));
    {
        const auto on = run ({ { sp::input, 6.0f }, { sp::softClip, 1.0f } });
        const auto more = run ({ { sp::input, 6.0f }, { sp::softClip, 1.0f }, { sp::softClipAmount, 90.0f } });
        CHECK (differs (on) && on.meanGr > base.meanGr + 0.2 && more.meanGr > on.meanGr + 0.2);
    }
    {
        const auto r = run ({ { sp::input, 6.0f }, { sp::subFilter, 3.0f } });
        CHECK (differs (r) && r.lfDb < base.lfDb - 3.0);
    }
    {
        const auto r = run ({ { sp::input, 6.0f }, { sp::delta, 1.0f } });
        CHECK (differs (r) && r.rmsDb < base.rmsDb - 6.0);
    }
    {
        const auto r = run ({ { sp::input, 6.0f }, { sp::levelMatch, 1.0f } });
        CHECK (differs (r) && r.rmsDb < base.rmsDb - 2.0);
    }
    {
        const auto r = run ({ { sp::input, 6.0f }, { sp::output, -6.0f } });
        CHECK (differs (r));
        CHECK_NEAR (r.rmsDb - base.rmsDb, -6.0, 0.01);
        CHECK (r.meanGr == base.meanGr); // OUTPUT is after the limiter
    }
    {
        const auto r = run ({ { sp::input, 6.0f }, { sp::bypass, 1.0f } });
        CHECK (differs (r) && r.tpDb > base.tpDb + 0.5f); // the unlimited dry input
    }
}
