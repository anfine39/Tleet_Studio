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

    // Pink-ish noise (Paul Kellet's economy filter), deterministic.
    std::vector<float> pinkNoise (int n, float rmsAmp, int seed)
    {
        juce::Random rng (seed);
        std::vector<float> v ((size_t) n);
        double b0 = 0, b1 = 0, b2 = 0;
        for (int i = 0; i < n; ++i)
        {
            const double w = 2.0 * rng.nextDouble() - 1.0;
            b0 = 0.99765 * b0 + w * 0.0990460;
            b1 = 0.96300 * b1 + w * 0.2965164;
            b2 = 0.57000 * b2 + w * 1.0526913;
            v[(size_t) i] = (float) (b0 + b1 + b2 + w * 0.1848);
        }
        const float r = rms (v.data(), n);
        for (auto& s : v) s *= rmsAmp / r;
        return v;
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
    double referenceTruePeak (const float* x, int n, int factor = 16)
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
            for (int i = 0; i < win / 2; ++i)
            {
                b[(size_t) i] = a[(size_t) i] * (double) factor;
                b[(size_t) (N - i - 1)] = a[(size_t) (win - i - 1)] * (double) factor;
            }
            // Nyquist bin split equally (real signal)
            b[(size_t) (win / 2)] = a[(size_t) (win / 2)] * (0.5 * factor);
            b[(size_t) (N - win / 2)] = a[(size_t) (win / 2)] * (0.5 * factor);
            b[(size_t) (N - win / 2 + 0)] = b[(size_t) (N - win / 2)];
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

    double referenceTruePeak (const juce::AudioBuffer<float>& b, int from = 0)
    {
        double m = 0.0;
        for (int ch = 0; ch < b.getNumChannels(); ++ch)
            m = std::max (m, referenceTruePeak (b.getReadPointer (ch) + from, b.getNumSamples() - from));
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
                CHECK_NEAR (best, 0.25f, 1e-4); // below the ceiling: the impulse passes unchanged in height
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
