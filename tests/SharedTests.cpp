#include "TestFramework.h"
#include "state/StateHistory.h"
#include "dsp/VUBallistics.h"
#include "dsp/LatencyAlignedDry.h"
#include "dsp/MeterSource.h"

using namespace tleet;

TEST ("shared: undo/redo history walks snapshots and truncates on new edits")
{
    StateHistory h;
    ParamSnapshot a, b, c, d;
    a.values["x"] = 0.0f;
    b.values["x"] = 1.0f;
    c.values["x"] = 2.0f;
    d.values["x"] = 3.0f;

    h.resetTo (a);
    CHECK (! h.canUndo());
    CHECK (h.commit (b));
    CHECK (h.commit (c));
    CHECK (! h.commit (c)); // identical state is not a new step

    auto* u = h.undo();
    CHECK (u != nullptr && u->values.at ("x") == 1.0f);
    CHECK (h.canRedo());
    CHECK (h.commit (d)); // new edit drops the redo branch
    CHECK (! h.canRedo());
    u = h.undo();
    CHECK (u != nullptr && u->values.at ("x") == 1.0f);
    u = h.undo();
    CHECK (u != nullptr && u->values.at ("x") == 0.0f);
    CHECK (h.undo() == nullptr);
}

TEST ("shared: VU ballistics reach 99% in ~300 ms with small overshoot")
{
    dsp::VUBallistics vu;
    vu.reset (0.0f);
    float t99 = -1.0f, peak = 0.0f;
    for (int i = 1; i <= 1000; ++i)
    {
        const float x = vu.process (1.0f, 0.001f);
        peak = std::max (peak, x);
        if (t99 < 0 && x >= 0.99f)
            t99 = (float) i * 0.001f;
    }
    MEASURE ("time to 99% (s)", t99);
    MEASURE ("overshoot (%)", (peak - 1.0f) * 100.0f);
    CHECK (t99 > 0.2f && t99 < 0.4f);
    CHECK (peak > 1.0f && peak < 1.025f);
}

TEST ("shared: latency-aligned dry path delays exactly by the requested samples")
{
    for (int latency : { 0, 1, 7, 63 })
        for (int block : { 1, 16, 64 })
        {
            dsp::LatencyAlignedDry d;
            d.prepare (1, block, latency);
            std::vector<float> in ((size_t) block), out;
            for (int pos = 0; pos < 400; pos += block)
            {
                for (int i = 0; i < block; ++i) in[(size_t) i] = (float) (pos + i + 1);
                const float* ptr = in.data();
                d.push (&ptr, 1, block);
                for (int i = 0; i < block; ++i) out.push_back (d.getDry (0)[i]);
            }
            bool ok = true;
            for (size_t i = 0; i < out.size(); ++i)
            {
                const float expected = (int) i - latency >= 0 ? (float) ((int) i - latency + 1) : 0.0f;
                ok = ok && out[i] == expected;
            }
            CHECK_MSG (ok, "latency " + juce::String (latency) + " block " + juce::String (block));
        }
}

TEST ("shared: meter source hands over peak, RMS and gain reduction exactly once")
{
    dsp::MeterSource m;
    CHECK (m.takeRms() < 0.0f);
    m.pushLevel (4.0f, 4, 1.0f);   // four samples of amplitude 1
    m.pushGainReduction (3.0f);
    m.pushGainReduction (6.0f);
    CHECK_NEAR (m.takeRms(), 1.0f, 1e-6);
    CHECK_NEAR (m.takePeak(), 1.0f, 1e-6);
    CHECK_NEAR (m.takeGainReduction(), 6.0f, 1e-6);
    CHECK (m.takeGainReduction() < 0.0f);
}
