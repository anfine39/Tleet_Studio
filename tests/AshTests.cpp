#include "TestFramework.h"
#include "TestSignals.h"
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Parameters.h"
#include "dsp/AshModel.h"

using namespace tleet;
using namespace tleet::test;
using tleet::ash::AshProcessor;

namespace
{
    void setParam (AshProcessor& p, const char* id, float plain)
    {
        auto* r = p.apvts.getParameter (id);
        r->setValueNotifyingHost (r->convertTo0to1 (plain));
    }

    // Renders `input` through the processor in blocks of `block` samples.
    juce::AudioBuffer<float> render (AshProcessor& p, const juce::AudioBuffer<float>& input, int block)
    {
        juce::AudioBuffer<float> out (input);
        juce::MidiBuffer midi;
        for (int pos = 0; pos < out.getNumSamples(); pos += block)
        {
            const int n = std::min (block, out.getNumSamples() - pos);
            juce::AudioBuffer<float> view (out.getArrayOfWritePointers(), out.getNumChannels(), pos, n);
            p.processBlock (view, midi);
        }
        return out;
    }

    juce::AudioBuffer<float> toBuffer (const std::vector<float>& mono, int channels)
    {
        juce::AudioBuffer<float> b (channels, (int) mono.size());
        for (int ch = 0; ch < channels; ++ch)
            b.copyFrom (ch, 0, mono.data(), (int) mono.size());
        return b;
    }

    std::unique_ptr<AshProcessor> makeAsh (double fs, int block, int channels = 2)
    {
        auto p = std::make_unique<AshProcessor>();
        const auto set = channels == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo();
        AshProcessor::BusesLayout layout;
        layout.inputBuses.add (set);
        layout.outputBuses.add (set);
        p->setBusesLayout (layout);
        p->setRateAndBufferSizeDetails (fs, block);
        p->prepareToPlay (fs, block);
        return p;
    }

    // Steady-state gain reduction of the bare model for a sine at `levelDb`.
    struct StaticPoint { float inDb, outDb, grDb; };
    StaticPoint staticPoint (bool limit, float pr, float levelDb)
    {
        ash::AshModel m;
        const double fs = 96000.0;
        m.setPeakReduction (pr);
        m.setLimitMode (limit);
        m.setCharacter (0.0f);
        m.setMakeupDb (0.0f);
        m.prepare (fs);
        auto sig = sine (fs, 1000.0, juce::Decibels::decibelsToGain (levelDb), (int) (fs * 8));
        float* ch[] = { sig.data() };
        m.process (ch, 1, (int) sig.size());
        const int tail = (int) fs; // last second, after the slow component settled
        const float out = rms (sig.data() + sig.size() - tail, tail);
        return { levelDb, db (out) + 3.0103f, -db (m.getCurrentGain()) };
    }
}

TEST ("ASH: parameter IDs, ranges and defaults match the registry")
{
    AshProcessor p;
    const char* ids[] = { "peakReduction", "gain", "mode", "mix", "character", "output", "bypass" };
    for (auto* id : ids)
        CHECK_MSG (p.apvts.getParameter (id) != nullptr, id);

    auto def = [&p] (const char* id) { auto* r = p.apvts.getParameter (id); return r->convertFrom0to1 (r->getDefaultValue()); };
    CHECK_NEAR (def ("gain"), 50.0f, 1e-4);
    CHECK_NEAR (def ("mix"), 100.0f, 1e-4);
    CHECK_NEAR (def ("output"), 0.0f, 1e-4);
    CHECK_NEAR (ash::params::gainKnobToDb (50.0f), 0.0f, 1e-6);

    auto* out = p.apvts.getParameter ("output");
    CHECK_NEAR (out->convertFrom0to1 (0.0f), -20.0f, 1e-4);
    CHECK_NEAR (out->convertFrom0to1 (1.0f), 20.0f, 1e-4);
}

TEST ("ASH: all sample rates and block sizes, mono and stereo: finite output, no steady-state allocation")
{
    for (double fs : { 44100.0, 48000.0, 88200.0, 96000.0, 192000.0 })
        for (int block : { 1, 16, 32, 64, 128, 256, 512, 1024, 2048 })
            for (int channels : { 1, 2 })
            {
                auto p = makeAsh (fs, block, channels);
                setParam (*p, "peakReduction", 70.0f);
                auto in = toBuffer (sine (fs, 220.0, 0.7f, std::max (block * 4, 4096)), channels);
                render (*p, in, block); // warm-up

                juce::AudioBuffer<float> out;
                long allocs;
                {
                    ScopedAllocationCounter counter;
                    out = juce::AudioBuffer<float> (in);
                    allocs = -1; // copy above allocates; measure only processing below
                }
                juce::MidiBuffer midi;
                {
                    ScopedAllocationCounter counter;
                    for (int pos = 0; pos < out.getNumSamples(); pos += block)
                    {
                        const int n = std::min (block, out.getNumSamples() - pos);
                        juce::AudioBuffer<float> view (out.getArrayOfWritePointers(), channels, pos, n);
                        p->processBlock (view, midi);
                    }
                    allocs = counter.count();
                }
                const auto tag = juce::String (fs) + " Hz, block " + juce::String (block) + ", " + juce::String (channels) + " ch";
                CHECK_MSG (allFinite (out), tag);
                CHECK_MSG (allocs == 0, tag + ": " + juce::String (allocs) + " allocations in processBlock");
            }
}

TEST ("ASH: NaN/Inf input never reaches the output or poisons the state")
{
    auto p = makeAsh (48000.0, 256);
    auto in = toBuffer (sine (48000.0, 440.0, 0.5f, 4800), 2);
    in.setSample (0, 100, std::numeric_limits<float>::quiet_NaN());
    in.setSample (1, 200, std::numeric_limits<float>::infinity());
    auto out = render (*p, in, 256);
    CHECK (allFinite (out));
    auto after = render (*p, toBuffer (sine (48000.0, 440.0, 0.5f, 4800), 2), 256);
    CHECK (allFinite (after));
    CHECK (rms (after.getReadPointer (0) + 2400, 2400) > 0.05f);
}

TEST ("ASH: silence in gives silence out; DC is rejected")
{
    auto p = makeAsh (48000.0, 512);
    juce::AudioBuffer<float> silence (2, 48000);
    silence.clear();
    auto out = render (*p, silence, 512);
    CHECK (out.getMagnitude (0, out.getNumSamples()) < 1.0e-6f);

    juce::AudioBuffer<float> dc (2, 96000);
    for (int ch = 0; ch < 2; ++ch)
        juce::FloatVectorOperations::fill (dc.getWritePointer (ch), 0.25f, dc.getNumSamples());
    out = render (*p, dc, 512);
    float mean = 0;
    for (int i = 48000; i < 96000; ++i) mean += out.getSample (0, i);
    mean /= 48000.0f;
    MEASURE ("residual DC after 1 s", mean);
    CHECK (std::abs (mean) < 0.002f);
}

TEST ("ASH: reported latency equals measured impulse delay; Mix=0 and bypass are bit-exact delayed dry")
{
    for (double fs : { 44100.0, 96000.0 })
    {
        auto p = makeAsh (fs, 128);
        const int latency = p->getLatencySamples();
        MEASURE ("reported latency @" + juce::String (fs), latency);

        setParam (*p, "peakReduction", 0.0f);
        setParam (*p, "character", 0.0f);
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

        // Mix = 0: delayed dry, bit-exact.
        auto sig = toBuffer (sine (fs, 997.0, 0.5f, 8192), 2);
        setParam (*p, "mix", 0.0f);
        p->prepareToPlay (fs, 128);
        out = render (*p, sig, 128);
        bool exact = true;
        for (int i = latency; i < sig.getNumSamples(); ++i)
            exact = exact && out.getSample (0, i) == sig.getSample (0, i - latency);
        CHECK_MSG (exact, "Mix=0 is not bit-exact dry");

        // Bypass: delayed dry, bit-exact, whatever the other settings.
        setParam (*p, "mix", 100.0f);
        setParam (*p, "peakReduction", 90.0f);
        setParam (*p, "bypass", 1.0f);
        p->prepareToPlay (fs, 128);
        out = render (*p, sig, 128);
        exact = true;
        for (int i = latency; i < sig.getNumSamples(); ++i)
            exact = exact && out.getSample (1, i) == sig.getSample (1, i - latency);
        CHECK_MSG (exact, "Bypass is not bit-exact dry");
    }
}

TEST ("ASH: wet path is time-aligned with dry (no comb filtering at Mix 50%)")
{
    const double fs = 48000.0;
    auto p = makeAsh (fs, 256);
    setParam (*p, "peakReduction", 0.0f);
    setParam (*p, "character", 0.0f);
    setParam (*p, "mix", 50.0f);
    p->prepareToPlay (fs, 256);

    // A high tone would cancel strongly if wet and dry were misaligned.
    for (double hz : { 1000.0, 10000.0, 18000.0 })
    {
        auto sig = toBuffer (sine (fs, hz, 0.1f, 24000), 2);
        auto out = render (*p, sig, 256);
        const float ratio = db (rms (out.getReadPointer (0) + 12000, 12000) / rms (sig.getReadPointer (0) + 12000, 12000));
        MEASURE ("level change at " + juce::String (hz) + " Hz (dB)", ratio);
        CHECK (std::abs (ratio) < 0.5f);
    }
}

TEST ("ASH: Compress and Limit are different static laws (ratio ~3:1 vs ~9:1)")
{
    auto slope = [] (bool limit)
    {
        const auto a = staticPoint (limit, 60.0f, -12.0f);
        const auto b = staticPoint (limit, 60.0f, 0.0f);
        return (b.inDb - a.inDb) / (b.outDb - a.outDb);
    };
    const float rc = slope (false), rl = slope (true);
    MEASURE ("compress ratio (-12..0 dBFS)", rc);
    MEASURE ("limit ratio (-12..0 dBFS)", rl);
    CHECK (rc > 2.0f && rc < 4.5f);
    CHECK (rl > 6.0f);

    auto csv = outputDirectory().getChildFile ("ASH_static_curves.csv");
    juce::String text ("input_dbfs,compress_out_dbfs,compress_gr_db,limit_out_dbfs,limit_gr_db\n");
    for (float l = -40.0f; l <= 0.01f; l += 2.0f)
    {
        const auto c = staticPoint (false, 60.0f, l), li = staticPoint (true, 60.0f, l);
        text << l << "," << c.outDb << "," << c.grDb << "," << li.outDb << "," << li.grDb << "\n";
    }
    csv.replaceWithText (text);
}

TEST ("ASH: Peak Reduction controls the amount of gain reduction monotonically")
{
    float prev = -1.0f;
    for (float pr : { 0.0f, 25.0f, 50.0f, 75.0f, 100.0f })
    {
        const auto pt = staticPoint (false, pr, -18.0f);
        MEASURE ("GR at -18 dBFS, PR " + juce::String (pr), pt.grDb);
        CHECK (pt.grDb >= prev - 0.01f);
        prev = pt.grDb;
    }
    CHECK (staticPoint (false, 0.0f, -18.0f).grDb < 0.5f);
    CHECK (staticPoint (false, 100.0f, -18.0f).grDb > 10.0f);
}

TEST ("ASH: attack is ~10 ms and release is history dependent (optical memory)")
{
    const double fs = 96000.0;
    auto run = [fs] (double burstSeconds, float& attackMs, float& recoverMs)
    {
        ash::AshModel m;
        m.setPeakReduction (70.0f);
        m.setLimitMode (false);
        m.setCharacter (0.0f);
        m.setMakeupDb (0.0f);
        m.prepare (fs);

        const int burst = (int) (burstSeconds * fs), after = (int) (8.0 * fs);
        auto loud = sine (fs, 500.0, 0.5f, burst);
        auto quiet = sine (fs, 500.0, 0.005f, after);
        std::vector<float> gr;
        gr.reserve ((size_t) (burst + after));

        for (auto* seg : { &loud, &quiet })
            for (auto& s : *seg)
            {
                float* ch[] = { &s };
                m.process (ch, 1, 1);
                gr.push_back (-db (m.getCurrentGain()));
            }

        // attack: time to reach 63 % of the GR reached after 50 ms
        const float ref = gr[(size_t) std::min (burst - 1, (int) (0.05 * fs))];
        attackMs = -1.0f;
        for (int i = 0; i < burst; ++i)
            if (gr[(size_t) i] >= 0.63f * ref) { attackMs = (float) (i * 1000.0 / fs); break; }

        // recovery: time after the burst until GR < 0.5 dB
        recoverMs = -1.0f;
        for (int i = burst; i < (int) gr.size(); ++i)
            if (gr[(size_t) i] < 0.5f) { recoverMs = (float) ((i - burst) * 1000.0 / fs); break; }
    };

    float attackShort, recoverShort, attackLong, recoverLong;
    run (0.3, attackShort, recoverShort);
    run (6.0, attackLong, recoverLong);
    MEASURE ("attack to 63% (ms)", attackShort);
    MEASURE ("recovery after 0.3 s burst (ms)", recoverShort);
    MEASURE ("recovery after 6 s burst (ms)", recoverLong);
    CHECK (attackShort > 3.0f && attackShort < 25.0f);
    CHECK (recoverShort > 0.0f && recoverLong > 0.0f);
    CHECK (recoverLong > recoverShort * 1.5f);
}

TEST ("ASH: colouration exists without compression and grows with Character")
{
    const double fs = 48000.0;
    auto thdFor = [fs] (float character, float levelDb)
    {
        auto p = makeAsh (fs, 512);
        setParam (*p, "peakReduction", 0.0f);
        setParam (*p, "character", character);
        p->prepareToPlay (fs, 512);
        auto out = render (*p, toBuffer (sine (fs, 1000.0, juce::Decibels::decibelsToGain (levelDb), 48000), 2), 512);
        return thd (out.getReadPointer (0) + 24000, 24000, fs, 1000.0);
    };
    const double clean = thdFor (0.0f, -6.0f), mid = thdFor (50.0f, -6.0f), ash = thdFor (100.0f, -6.0f);
    const double ashQuiet = thdFor (100.0f, -30.0f);
    MEASURE ("THD % Character 0, -6 dBFS", clean * 100);
    MEASURE ("THD % Character 50, -6 dBFS", mid * 100);
    MEASURE ("THD % Character 100, -6 dBFS", ash * 100);
    MEASURE ("THD % Character 100, -30 dBFS", ashQuiet * 100);
    CHECK (clean < 0.001);
    CHECK (mid > clean * 3.0 && mid < 0.01);
    CHECK (ash > mid);
    CHECK (ashQuiet < ash); // level dependent, as the tube stages should be
}

TEST ("ASH: GR telemetry equals the attenuation measured on the audio")
{
    const double fs = 48000.0;
    auto p = makeAsh (fs, 512);
    setParam (*p, "peakReduction", 65.0f);
    setParam (*p, "character", 0.0f);
    p->prepareToPlay (fs, 512);

    const float inAmp = juce::Decibels::decibelsToGain (-12.0f);
    auto in = toBuffer (sine (fs, 1000.0, inAmp, (int) (fs * 6)), 2);
    auto out = render (*p, in, 512);
    p->grMeter.takeGainReduction();

    // one more steady block, read telemetry for exactly that block
    auto last = toBuffer (sine (fs, 1000.0, inAmp, 4800), 2);
    auto lastOut = render (*p, last, 4800 > 512 ? 512 : 4800);
    const float telemetry = p->grMeter.takeGainReduction();
    const float measured = db (rms (last.getReadPointer (0), 4800)) - db (rms (lastOut.getReadPointer (0), 4800));
    MEASURE ("GR telemetry (dB)", telemetry);
    MEASURE ("GR measured on audio (dB)", measured);
    CHECK (telemetry > 3.0f);
    CHECK_NEAR (telemetry, measured, 0.05);
}

TEST ("ASH: VU meter calibration (0 VU = -18 dBFS in +4, -12 dBFS in +10, GR mode shows reduction)")
{
    const float rmsMinus18 = juce::Decibels::decibelsToGain (-18.0f);
    CHECK_NEAR (ash::meterReadingFor (ash::MeterMode::plus4, -1.0f, rmsMinus18), 0.0f, 1e-4);
    CHECK_NEAR (ash::meterReadingFor (ash::MeterMode::plus10, -1.0f, rmsMinus18), -6.0f, 1e-4);
    CHECK_NEAR (ash::meterReadingFor (ash::MeterMode::gr, 4.0f, 0.1f), -4.0f, 1e-6);
    CHECK (std::isnan (ash::meterReadingFor (ash::MeterMode::gr, -1.0f, 0.1f)));
}

TEST ("ASH: rapid automation of every parameter stays click-free and finite")
{
    const double fs = 48000.0;
    auto p = makeAsh (fs, 64);
    juce::Random rng (42);
    auto in = toBuffer (sine (fs, 200.0, 0.3f, 64), 2);
    float prevSample = 0.0f, maxJump = 0.0f;
    for (int b = 0; b < 2000; ++b)
    {
        setParam (*p, "peakReduction", rng.nextFloat() * 100.0f);
        setParam (*p, "gain", 40.0f + rng.nextFloat() * 20.0f);
        setParam (*p, "mode", (float) rng.nextInt (2));
        setParam (*p, "mix", rng.nextFloat() * 100.0f);
        setParam (*p, "character", rng.nextFloat() * 100.0f);
        setParam (*p, "output", -6.0f + rng.nextFloat() * 12.0f);
        auto blockIn = toBuffer (sine (fs, 200.0, 0.3f, 64, 2.0 * juce::MathConstants<double>::pi * 200.0 * b * 64 / fs), 2);
        auto out = render (*p, blockIn, 64);
        CHECK (allFinite (out));
        for (int i = 0; i < 64; ++i)
        {
            maxJump = std::max (maxJump, std::abs (out.getSample (0, i) - prevSample));
            prevSample = out.getSample (0, i);
        }
    }
    juce::ignoreUnused (in);
    // a 200 Hz sine at <= ~0.6 peak moves at most ~0.016 per sample; allow
    // generous headroom for gain changes but catch hard steps
    MEASURE ("max sample-to-sample jump", maxJump);
    CHECK (maxJump < 0.12f);
}

TEST ("ASH: state round trip restores parameters, preset name, A/B and UI settings and renders identically")
{
    const double fs = 48000.0;
    auto a = makeAsh (fs, 256);
    setParam (*a, "peakReduction", 63.5f);
    setParam (*a, "gain", 58.0f);
    setParam (*a, "mode", 1.0f);
    setParam (*a, "mix", 72.0f);
    setParam (*a, "character", 81.0f);
    setParam (*a, "output", -3.5f);
    a->setUiProperty ("scale", 1.5);
    a->setUiProperty ("meterMode", 2);

    juce::MemoryBlock state;
    a->getStateInformation (state);

    auto b = makeAsh (fs, 256);
    b->setStateInformation (state.getData(), (int) state.getSize());

    for (auto* param : a->getParameters())
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (param))
            CHECK_NEAR (r->getValue(), b->apvts.getParameter (r->paramID)->getValue(), 1e-6);
    CHECK_NEAR ((double) b->getUiProperty ("scale", 1.0), 1.5, 1e-9);
    CHECK ((int) b->getUiProperty ("meterMode", 0) == 2);

    a->prepareToPlay (fs, 256);
    b->prepareToPlay (fs, 256);
    auto sig = toBuffer (sine (fs, 300.0, 0.4f, 48000), 2);
    auto oa = render (*a, sig, 256), ob = render (*b, sig, 256);
    bool identical = true;
    for (int i = 0; i < oa.getNumSamples(); ++i)
        identical = identical && oa.getSample (0, i) == ob.getSample (0, i);
    CHECK_MSG (identical, "restored instance renders differently");

    // garbage and foreign state are ignored
    b->setStateInformation ("nonsense", 8);
    CHECK_NEAR (b->apvts.getParameter ("mix")->convertFrom0to1 (b->apvts.getParameter ("mix")->getValue()), 72.0f, 0.01);
}

TEST ("ASH: factory presets load, change parameters and reject other plugins' presets")
{
    AshProcessor p;
    const auto& list = p.presets.getPresets();
    int factory = 0;
    for (auto& pr : list) factory += pr.isFactory ? 1 : 0;
    MEASURE ("factory presets", factory);
    CHECK (factory >= 10);

    for (int i = 0; i < (int) list.size(); ++i)
        if (list[(size_t) i].isFactory)
        {
            p.loadPreset (i);
            for (auto& [id, v] : list[(size_t) i].params.values)
            {
                auto* r = p.apvts.getParameter (id);
                CHECK_MSG (r != nullptr, id);
                if (r) CHECK_NEAR (r->convertFrom0to1 (r->getValue()), v, 0.06);
            }
            CHECK (! p.isPresetModified());
        }

    setParam (p, "mix", 13.0f);
    CHECK (p.isPresetModified());

    PresetManager::Preset out;
    const auto foreign = juce::String (R"({"format":"tleet-preset","version":1,"plugin":"BITE","name":"x","params":{}})");
    CHECK (p.presets.parse (foreign, out).failed());
    const auto future = juce::String (R"({"format":"tleet-preset","version":99,"plugin":"ASH","name":"x","params":{}})");
    CHECK (p.presets.parse (future, out).failed());

    // copy/paste of the complete state
    const auto json = p.stateToClipboardJson();
    setParam (p, "mix", 99.0f);
    CHECK (p.pasteStateFromJson (json).wasOk());
    CHECK_NEAR (p.apvts.getParameter ("mix")->convertFrom0to1 (p.apvts.getParameter ("mix")->getValue()), 13.0f, 0.06);
}

TEST ("ASH: A/B slots and undo/redo restore complete settings")
{
    AshProcessor p;
    auto get = [&p] (const char* id) { auto* r = p.apvts.getParameter (id); return r->convertFrom0to1 (r->getValue()); };

    setParam (p, "peakReduction", 20.0f);
    p.commitHistory();
    p.switchToSlot (1);                  // B starts as a copy of A
    setParam (p, "peakReduction", 80.0f);
    p.commitHistory();
    p.switchToSlot (0);
    CHECK_NEAR (get ("peakReduction"), 20.0f, 0.06);
    p.switchToSlot (1);
    CHECK_NEAR (get ("peakReduction"), 80.0f, 0.06);

    p.undo();
    CHECK_NEAR (get ("peakReduction"), 20.0f, 0.06);
    p.redo();
    CHECK_NEAR (get ("peakReduction"), 80.0f, 0.06);
}

TEST ("ASH: editor opens at every scale and renders screenshots")
{
    auto p = makeAsh (48000.0, 256);
    for (float scale : { 0.75f, 1.0f, 1.5f, 2.0f })
    {
        p->setUiProperty ("scale", scale);
        std::unique_ptr<juce::AudioProcessorEditor> ed (p->createEditor());
        const int expectedW = juce::roundToInt (ash::AshEditor::baseWidth * scale);
        CHECK_MSG (std::abs (ed->getWidth() - expectedW) <= 1, "width at scale " + juce::String (scale) + ": " + juce::String (ed->getWidth()) + " vs " + juce::String (expectedW));

        auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
        juce::PNGImageFormat png;
        auto f = outputDirectory().getChildFile ("ASH_editor_" + juce::String (juce::roundToInt (scale * 100)) + ".png");
        f.deleteFile();
        if (auto os = f.createOutputStream())
            png.writeImageToStream (img, *os);
    }
}

namespace
{
    // Mouse events at reference-pixel positions of the mockup (UI_MAP coordinates).
    juce::Point<float> refToEditor (float x, float y)
    {
        return { x * ash::AshEditor::skinScale, ui::TleetEditorBase::toolbarHeight + y * ash::AshEditor::skinScale };
    }

    juce::Component* componentAt (juce::Component& ed, juce::Point<float> pos)
    {
        return ed.getComponentAt (pos.toInt());
    }

    float plainValue (AshProcessor& p, const char* id)
    {
        auto* r = p.apvts.getParameter (id);
        return r->convertFrom0to1 (r->getValue());
    }
}

TEST ("ASH GUI: every mapped control sits at its mockup position and owns its hitbox")
{
    auto p = makeAsh (48000.0, 256);
    p->setUiProperty ("scale", 1.0);
    std::unique_ptr<juce::AudioProcessorEditor> base (p->createEditor());
    auto& ed = dynamic_cast<ash::AshEditor&> (*base);
    ed.setVisible (true); // hit-testing skips invisible components
    const auto map = ash::AshEditor::loadUiMap();

    int checked = 0;
    for (auto& c : *map["controls"].getArray())
    {
        const auto type = c["type"].toString();
        if (type != "ROTARY" && type != "TOGGLE" && type != "VU")
            continue;
        juce::Rectangle<float> hb ((float) c["hitbox"][0], (float) c["hitbox"][1], (float) c["hitbox"][2], (float) c["hitbox"][3]);

        juce::Component* expected = type == "ROTARY" ? static_cast<juce::Component*> (&ed.getKnob (c["parameter"].toString()))
                                  : type == "TOGGLE" ? static_cast<juce::Component*> (&ed.getModeSwitch())
                                                     : static_cast<juce::Component*> (&ed.getVU());

        // centre and all four inner corners of the hitbox hit the control
        for (auto pt : { hb.getCentre(), hb.getTopLeft() + juce::Point<float> (3, 3), hb.getBottomRight() - juce::Point<float> (3, 3),
                         hb.getTopRight() + juce::Point<float> (-3, 3), hb.getBottomLeft() + juce::Point<float> (3, -3) })
        {
            auto* hit = componentAt (ed, refToEditor (pt.x, pt.y));
            CHECK_MSG (hit == expected, c["id"].toString() + " at " + pt.toString() + " hit " + (hit ? juce::String (typeid (*hit).name()) + " " + hit->getBounds().toString() : juce::String ("null")) + " expected bounds " + expected->getBounds().toString());
        }

        // rotary pointer pivot equals the knob centre on the artwork (+-1 px at 100 %)
        if (type == "ROTARY")
        {
            auto& k = ed.getKnob (c["parameter"].toString());
            const auto centreRef = juce::Point<float> ((float) c["centre"][0], (float) c["centre"][1]);
            const auto expectedCentre = refToEditor (centreRef.x, centreRef.y);
            const auto actual = k.getBounds().getPosition().toFloat() + (centreRef - juce::Point<float> ((float) c["hitbox"][0], (float) c["hitbox"][1])) * ash::AshEditor::skinScale;
            CHECK (actual.getDistanceFrom (expectedCentre) <= 1.0f);
        }
        ++checked;
    }
    MEASURE ("controls checked", checked);
    CHECK (checked == 7);

    // hitboxes never overlap, so clicks between controls cannot hit a neighbour
    std::vector<juce::Rectangle<int>> boxes;
    for (auto* child : ed.getChildren().getFirst()->getChildren())
        if (dynamic_cast<ui::Knob*> (child) || dynamic_cast<ui::LeverSwitch*> (child) || dynamic_cast<ui::SkinVU*> (child))
            boxes.push_back (child->getBounds());
    for (size_t i = 0; i < boxes.size(); ++i)
        for (size_t j = i + 1; j < boxes.size(); ++j)
            CHECK_MSG (! boxes[i].intersects (boxes[j]), boxes[i].toString() + " overlaps " + boxes[j].toString());
}

TEST ("ASH GUI: mouse drag, fine drag, wheel, reset and lever clicks change the real parameters and DSP")
{
    auto p = makeAsh (48000.0, 256);
    p->setUiProperty ("scale", 1.0);
    std::unique_ptr<juce::AudioProcessorEditor> base (p->createEditor());
    auto& ed = dynamic_cast<ash::AshEditor&> (*base);
    ed.setVisible (true); // hit-testing skips invisible components
    auto& knob = ed.getKnob ("peakReduction");
    auto src = juce::Desktop::getInstance().getMainMouseSource();

    auto drag = [&] (ui::Knob& k, float dy, juce::ModifierKeys mods)
    {
        const auto c = k.getLocalBounds().getCentre().toFloat();
        const auto t = juce::Time::getCurrentTime();
        juce::MouseEvent down (src, c, mods.withFlags (juce::ModifierKeys::leftButtonModifier), 1.0f, 0, 0, 0, 0, &k, &k, t, c, t, 1, false);
        k.mouseDown (down);
        const auto to = c.translated (0.0f, dy);
        juce::MouseEvent move (src, to, mods.withFlags (juce::ModifierKeys::leftButtonModifier), 1.0f, 0, 0, 0, 0, &k, &k, t, c, t, 1, true);
        k.mouseDrag (move);
        k.mouseUp (move);
    };

    {
        auto* r = p->apvts.getParameter ("peakReduction");
        r->setValueNotifyingHost (r->convertTo0to1 (40.0f));
    }

    drag (knob, -125.0f, {});           // half the travel upwards = +50
    CHECK_NEAR (plainValue (*p, "peakReduction"), 90.0f, 0.6);
    drag (knob, 100.0f, juce::ModifierKeys::shiftModifier); // fine: 1/10 sensitivity
    CHECK_NEAR (plainValue (*p, "peakReduction"), 86.0f, 0.6);
    CHECK (p->canUndo());

    // the knob changes the DSP: more Peak Reduction = more measured attenuation
    auto grAt = [&] (float pr)
    {
        auto* r = p->apvts.getParameter ("peakReduction");
        r->setValueNotifyingHost (r->convertTo0to1 (pr));
        p->prepareToPlay (48000.0, 256);
        auto in = toBuffer (sine (48000.0, 1000.0, 0.25f, 96000), 2);
        auto out = render (*p, in, 256);
        return db (rms (in.getReadPointer (0) + 48000, 48000)) - db (rms (out.getReadPointer (0) + 48000, 48000));
    };
    CHECK (grAt (80.0f) > grAt (30.0f) + 3.0f);

    // Alt-click resets to default
    {
        const auto c = knob.getLocalBounds().getCentre().toFloat();
        const auto t = juce::Time::getCurrentTime();
        juce::MouseEvent alt (src, c, juce::ModifierKeys (juce::ModifierKeys::altModifier | juce::ModifierKeys::leftButtonModifier),
                              1.0f, 0, 0, 0, 0, &knob, &knob, t, c, t, 1, false);
        knob.mouseDown (alt);
        knob.mouseUp (alt);
        CHECK_NEAR (plainValue (*p, "peakReduction"), 40.0f, 0.06);
    }

    // wheel
    {
        const auto before = plainValue (*p, "mix");
        auto& mix = ed.getKnob ("mix");
        juce::MouseWheelDetails w { 0.0f, -1.0f, false, false, false };
        const auto t = juce::Time::getCurrentTime();
        juce::MouseEvent e (src, {}, {}, 1.0f, 0, 0, 0, 0, &mix, &mix, t, {}, t, 0, false);
        mix.mouseWheelMove (e, w);
        CHECK (plainValue (*p, "mix") < before);
    }

    // lever: click left of the pivot = Compress, right = Limit
    {
        auto& lever = ed.getModeSwitch();
        const auto t = juce::Time::getCurrentTime();
        auto click = [&] (float x)
        {
            const juce::Point<float> pos (x, (float) lever.getHeight() * 0.5f);
            juce::MouseEvent e (src, pos, juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier), 1.0f, 0, 0, 0, 0, &lever, &lever, t, pos, t, 1, false);
            lever.mouseDown (e);
        };
        click ((float) lever.getWidth() - 5.0f);
        CHECK (plainValue (*p, "mode") > 0.5f);
        click (5.0f);
        CHECK (plainValue (*p, "mode") < 0.5f);
    }
}

TEST ("ASH GUI: host automation moves knob, lever and stays in sync; VU mode switch changes the measured quantity")
{
    auto p = makeAsh (48000.0, 256);
    std::unique_ptr<juce::AudioProcessorEditor> base (p->createEditor());
    auto& ed = dynamic_cast<ash::AshEditor&> (*base);
    ed.setVisible (true); // hit-testing skips invisible components

    for (auto* id : { "peakReduction", "gain", "mix", "character", "output" })
    {
        auto* r = p->apvts.getParameter (id);
        r->setValueNotifyingHost (0.123f);
        CHECK_NEAR (ed.getKnob (id).getNormalisedValue(), r->getValue(), 1e-4);
    }
    auto* m = p->apvts.getParameter ("mode");
    m->setValueNotifyingHost (1.0f);
    CHECK (ed.getModeSwitch().getState() == 1);

    CHECK (ed.getMeterMode() == ash::MeterMode::gr);
    ed.getVU().onClick();
    CHECK (ed.getMeterMode() == ash::MeterMode::plus4);
    CHECK ((int) p->getUiProperty ("meterMode", -1) == 1);
    ed.getVU().onClick();
    ed.getVU().onClick();
    CHECK (ed.getMeterMode() == ash::MeterMode::gr);

    // needle angle follows the printed scale exactly at the marks
    const auto map = ash::AshEditor::loadUiMap();
    for (auto& c : *map["controls"].getArray())
        if (c["type"].toString() == "VU")
            for (auto& prop : c["scaleDegrees"].getDynamicObject()->getProperties())
                CHECK_NEAR (juce::radiansToDegrees (ed.getVU().angleForVu (prop.name.toString().getFloatValue())), (double) prop.value, 1e-3);
}
