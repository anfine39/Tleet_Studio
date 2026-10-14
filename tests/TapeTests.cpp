#include "TestFramework.h"
#include "TestSignals.h"
#include "../plugins/TAPE/PluginProcessor.h"
#include "../plugins/TAPE/Parameters.h"

using namespace tleet;
using namespace tleet::test;
using tleet::tape::TapeProcessor;

TEST ("TAPE: cpu probe")
{
    for (double fs : { 48000.0, 96000.0, 192000.0 })
    {
        TapeProcessor p;
        p.setRateAndBufferSizeDetails (fs, 128);
        p.prepareToPlay (fs, 128);
        const int n = (int) fs * 2 / 128 * 128;
        juce::AudioBuffer<float> b (2, n);
        auto s = sine (fs, 1000.0, 0.3f, n);
        b.copyFrom (0, 0, s.data(), n);
        b.copyFrom (1, 0, s.data(), n);
        juce::MidiBuffer m;
        std::vector<double> t;
        for (int pos = 0; pos < n; pos += 128)
        {
            juce::AudioBuffer<float> v (b.getArrayOfWritePointers(), 2, pos, 128);
            auto t0 = juce::Time::getHighResolutionTicks();
            p.processBlock (v, m);
            t.push_back (juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - t0));
        }
        std::sort (t.begin(), t.end());
        MEASURE ("cpu % " + juce::String (fs), t[t.size() / 2] * fs / 128 * 100);
    }
}
#include "../plugins/TAPE/dsp/Hysteresis.h"
TEST ("TAPE: ja probe")
{
    tleet::tape::HysteresisSolver s; s.reset();
    tleet::tape::JAParams p;
    const int n = 384000;
    double acc = 0;
    auto t0 = juce::Time::getHighResolutionTicks();
    for (int i = 0; i < n; ++i) acc += s.process<4> (2.0 * std::sin (i * 0.0327), p);
    MEASURE ("ja ms per 384k steps", juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - t0) * 1000);
    MEASURE ("acc", acc);
    t0 = juce::Time::getHighResolutionTicks();
    (void) tleet::tape::HysteresisTables::get();
    MEASURE ("tables ms", juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - t0) * 1000);
}

TEST ("TAPE: rk probe")
{
    for (double c : { 0.15, 0.5, 0.88, 0.95 })
        for (double hz : { 1000.0, 5000.0, 15000.0 })
            for (double a : { 0.1, 1.0, 5.0 })
            {
                tleet::tape::HysteresisSolver s2, s4, sr; s2.reset(); s4.reset(); sr.reset();
                tleet::tape::JAParams p { 1.227, 0.02545, c };
                double e2 = 0, e4 = 0, r = 0, yr = 0;
                for (int i = 0; i < 19200; ++i)
                {
                    const double h = a * std::sin (2 * 3.14159265358979 * hz * i / 192000.0);
                    const double y2 = s2.process<2> (h, p), y4 = s4.process<4> (h, p);
                    for (int k = 1; k <= 16; ++k) yr = sr.process<4> (a * std::sin (2 * 3.14159265358979 * hz * (i - 1 + k / 16.0) / 192000.0), p);
                    if (i > 9600) { e2 += (y2 - yr) * (y2 - yr); e4 += (y4 - yr) * (y4 - yr); r += yr * yr; }
                }
                MEASURE ("c " + juce::String (c) + " hz " + juce::String (hz) + " a " + juce::String (a) + " err rk2", 10 * std::log10 (e2 / r + 1e-30));
                MEASURE ("    rk4", 10 * std::log10 (e4 / r + 1e-30));
            }
}
