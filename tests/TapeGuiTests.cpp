#include "TestFramework.h"
#include "../plugins/TAPE/PluginProcessor.h"
#include "../plugins/TAPE/PluginEditor.h"
#include "../plugins/TAPE/Parameters.h"

// GUI tests for TAPE (plugins/TAPE/GUI_TEST_PLAN.md). Every control is
// checked against UI_MAP.json and against the real parameter / DSP state.

using namespace tleet;
using namespace tleet::test;
using tleet::tape::TapeProcessor;
using tleet::tape::TapeEditor;
using tleet::tape::TapeReel;
using tleet::tape::SaturationBar;
namespace tp = tleet::tape::params;

namespace
{
    constexpr double sr = 48000.0;
    constexpr int block = 256;
    constexpr float frame = 1.0f / 30.0f;

    std::unique_ptr<TapeProcessor> makeTapeForGui()
    {
        auto p = std::make_unique<TapeProcessor>();
        p->setRateAndBufferSizeDetails (sr, block);
        p->prepareToPlay (sr, block);
        return p;
    }

    void setPlain (TapeProcessor& p, const char* id, float plain)
    {
        auto* r = p.apvts.getParameter (id);
        r->setValueNotifyingHost (r->convertTo0to1 (plain));
    }

    void setIndex (TapeProcessor& p, const char* id, int index) { setPlain (p, id, (float) index); }

    float plainOf (TapeProcessor& p, const char* id)
    {
        auto* r = p.apvts.getParameter (id);
        return r->convertFrom0to1 (r->getValue());
    }

    int indexOf (TapeProcessor& p, const char* id) { return juce::roundToInt (plainOf (p, id)); }

    // A fixed programme around -14 dBFS RMS: a bass line, a kick, a bright
    // noise layer and a mid sine that differ left / right.
    std::vector<float> render (TapeProcessor& p, double seconds = 1.0, long* clock = nullptr, float gain = 1.0f)
    {
        juce::AudioBuffer<float> buf (2, block);
        juce::MidiBuffer midi;
        juce::Random rng (17 + (clock != nullptr ? *clock : 0));
        std::vector<float> out;
        const int blocks = juce::jmax (1, (int) (seconds * sr / block));
        long local = 0;
        long& n = clock != nullptr ? *clock : local;
        const double twoPi = 2.0 * juce::MathConstants<double>::pi;
        for (int b = 0; b < blocks; ++b)
        {
            for (int i = 0; i < block; ++i, ++n)
            {
                const double t = (double) n / sr;
                const double beat = std::fmod (t, 0.5);
                const float kick = (float) (std::exp (-beat * 25.0) * std::sin (twoPi * (55.0 + 70.0 * std::exp (-beat * 40.0)) * beat));
                const float bass = 0.18f * (float) std::sin (twoPi * 82.4 * t);
                const float mid = 0.12f * (float) std::sin (twoPi * 660.0 * t);
                const float nl = 0.06f * (rng.nextFloat() * 2.0f - 1.0f), nr = 0.06f * (rng.nextFloat() * 2.0f - 1.0f);
                buf.setSample (0, i, gain * (0.3f * kick + bass + mid + nl));
                buf.setSample (1, i, gain * (0.2f * kick + bass + 0.3f * mid + nr));
            }
            p.processBlock (buf, midi);
            out.insert (out.end(), buf.getReadPointer (0), buf.getReadPointer (0) + block);
            out.insert (out.end(), buf.getReadPointer (1), buf.getReadPointer (1) + block);
        }
        return out;
    }

    // `seconds` of a sine of `peak` (left) and `peakR` (right; < 0 = same as left).
    void feed (TapeProcessor& p, float peak, double seconds, double hz = 1000.0, float peakR = -1.0f)
    {
        static double phase = 0.0;
        juce::AudioBuffer<float> buf (2, block);
        juce::MidiBuffer midi;
        for (int b = 0; b < juce::jmax (1, (int) std::lround (seconds * sr / block)); ++b)
        {
            for (int i = 0; i < block; ++i)
            {
                const float s = (float) std::sin (phase);
                phase += 2.0 * juce::MathConstants<double>::pi * hz / sr;
                buf.setSample (0, i, peak * s);
                buf.setSample (1, i, (peakR < 0.0f ? peak : peakR) * s);
            }
            p.processBlock (buf, midi);
        }
    }

    double rmsDifferenceDb (const std::vector<float>& a, const std::vector<float>& b)
    {
        double d = 0, e = 0;
        for (size_t i = 0; i < juce::jmin (a.size(), b.size()); ++i)
        {
            d += (double) (a[i] - b[i]) * (a[i] - b[i]);
            e += (double) a[i] * a[i];
        }
        return 10.0 * std::log10 ((d + 1e-30) / (e + 1e-30));
    }

    juce::Point<float> refToEditor (float x, float y)
    {
        return { x * TapeEditor::skinScale, ui::TleetEditorBase::toolbarHeight + y * TapeEditor::skinScale };
    }

    juce::Rectangle<float> rectOf (const juce::var& v) { return { (float) v[0], (float) v[1], (float) v[2], (float) v[3] }; }
    juce::Point<float> pointOf (const juce::var& v) { return { (float) v[0], (float) v[1] }; }

    const juce::var& control (const juce::var& map, const juce::String& id)
    {
        for (auto& c : *map["controls"].getArray())
            if (c["id"].toString() == id)
                return c;
        static juce::var none;
        return none;
    }

    juce::Image snapshot (juce::Component& ed) { return ed.createComponentSnapshot (ed.getLocalBounds(), true, 1.0f); }

    void saveSnapshot (juce::Component& ed, const juce::String& name)
    {
        auto img = snapshot (ed);
        juce::PNGImageFormat png;
        auto f = outputDirectory().getChildFile (name + ".png");
        f.deleteFile();
        if (auto os = f.createOutputStream())
            png.writeImageToStream (img, *os);
    }

    void click (juce::Component& target, juce::Component& editor, juce::Point<float> refPoint)
    {
        auto src = juce::Desktop::getInstance().getMainMouseSource();
        const auto local = target.getLocalPoint (&editor, refToEditor (refPoint.x, refPoint.y));
        const auto t = juce::Time::getCurrentTime();
        juce::MouseEvent e (src, local, juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier), 1.0f, 0, 0, 0, 0,
                            &target, &target, t, local, t, 1, false);
        auto& c = static_cast<juce::Component&> (target); // Button hides these as protected
        c.mouseDown (e);
        c.mouseUp (e);
    }

    // clicks the component that really sits at the point, as the mouse would
    void clickAt (TapeEditor& ed, juce::Point<float> refPoint)
    {
        auto* hit = ed.getComponentAt (refToEditor (refPoint.x, refPoint.y).toInt());
        CHECK_MSG (hit != nullptr, "nothing at " + refPoint.toString());
        if (hit != nullptr)
            click (*hit, ed, refPoint);
    }

    const char* const knobIds[] = { "input", "output", "bias", "saturation", "headBump", "lf", "hf",
                                    "age", "wow", "flutter", "noiseAmount", "crosstalk", "mix" };

    // One UI frame of audio: the processor runs, then the editor and the
    // meters (their own timers do not run in the test runner).
    void frames (TapeProcessor& p, TapeEditor& ed, int n, float peak, double hz = 1000.0, float peakR = -1.0f)
    {
        for (int i = 0; i < n; ++i)
        {
            feed (p, peak, frame, hz, peakR);
            ed.refresh (frame);
            for (int m = 0; m < 2; ++m)
                for (int ch = 0; ch < 2; ++ch)
                    ed.getVu (m == 1, ch).tick (frame);
        }
    }

    float dbToGain (float db) { return std::pow (10.0f, db / 20.0f); }
}

TEST ("TAPE GUI: editor opens at every scale; screenshots of default and representative states")
{
    auto p = makeTapeForGui();
    for (float scale : { 0.75f, 1.0f, 1.5f, 2.0f })
    {
        p->setUiProperty ("scale", scale);
        std::unique_ptr<juce::AudioProcessorEditor> ed (p->createEditor());
        CHECK (dynamic_cast<TapeEditor*> (ed.get()) != nullptr);
        const int expectedW = juce::roundToInt (TapeEditor::baseWidth * scale);
        CHECK_MSG (std::abs (ed->getWidth() - expectedW) <= 1, "width at scale " + juce::String (scale));
        saveSnapshot (*ed, "TAPE_editor_" + juce::String (juce::roundToInt (scale * 100)));
    }

    p->setUiProperty ("scale", 1.0);
    std::unique_ptr<juce::AudioProcessorEditor> base (p->createEditor());
    auto& ed = dynamic_cast<TapeEditor&> (*base);
    ed.setVisible (true);
    struct State { const char* name; std::function<void()> apply; };
    const State states[] = {
        { "default_running", [&] {} },
        { "hot_driven", [&] { setIndex (*p, tp::tapeType, 2); setIndex (*p, tp::calibration, 3); setPlain (*p, tp::input, 12.0f);
                              setPlain (*p, tp::saturation, 85.0f); } },
        { "slow_worn_noise", [&] { setIndex (*p, tp::tapeType, 0); setIndex (*p, tp::speed, 0); setPlain (*p, tp::age, 80.0f);
                                   setPlain (*p, tp::wow, 40.0f); setPlain (*p, tp::flutter, 30.0f); setPlain (*p, tp::noise, 1.0f);
                                   setPlain (*p, tp::noiseAmount, 60.0f); setPlain (*p, tp::input, 4.0f); } },
        { "fast_eq_mix", [&] { setIndex (*p, tp::speed, 2); setPlain (*p, tp::age, 0.0f); setPlain (*p, tp::lf, -60.0f);
                               setPlain (*p, tp::hf, 70.0f); setPlain (*p, tp::bias, -50.0f); setPlain (*p, tp::headBump, 90.0f);
                               setPlain (*p, tp::mix, 70.0f); setPlain (*p, tp::autoGain, 0.0f); setPlain (*p, tp::output, -3.0f);
                               setPlain (*p, tp::noise, 0.0f); setPlain (*p, tp::crosstalk, 50.0f); } },
    };
    long clock = 0;
    for (auto& st : states)
    {
        st.apply();
        ed.getSpectrum().setQuality (ui::SpectrumDisplay::Quality::normal);
        for (int i = 0; i < 60; ++i)
        {
            render (*p, frame, &clock, 2.0f);
            ed.refresh (frame);
            for (int m = 0; m < 2; ++m)
                for (int ch = 0; ch < 2; ++ch)
                    ed.getVu (m == 1, ch).tick (frame);
            ed.getSpectrum().processPending();
        }
        saveSnapshot (ed, juce::String ("TAPE_state_") + st.name);
    }
}

TEST ("TAPE GUI: every mapped control sits at its mockup position and owns its click area")
{
    auto p = makeTapeForGui();
    p->setUiProperty ("scale", 1.0);
    std::unique_ptr<juce::AudioProcessorEditor> base (p->createEditor());
    auto& ed = dynamic_cast<TapeEditor&> (*base);
    ed.setVisible (true);
    const auto map = TapeEditor::loadUiMap();

    std::vector<std::pair<juce::String, juce::Rectangle<float>>> areas;
    auto checkArea = [&] (const juce::String& name, juce::Rectangle<float> hb, juce::Component* target, float inset)
    {
        areas.push_back ({ name, hb });
        const auto in = hb.reduced (inset);
        for (auto pt : { in.getCentre(), in.getTopLeft(), in.getBottomRight(), in.getTopRight(), in.getBottomLeft() })
        {
            auto* hit = ed.getComponentAt (refToEditor (pt.x, pt.y).toInt());
            CHECK_MSG (hit == target, name + " at " + pt.toString() + " hit " + (hit ? hit->getName() + " " + hit->getBounds().toString() : juce::String ("null")));
        }
    };

    int knobsChecked = 0, lampsChecked = 0, toggles = 0, displays = 0, reels = 0;
    for (auto& c : *map["controls"].getArray())
    {
        const auto type = c["type"].toString();
        const auto id = c["id"].toString();
        if (type == "ROTARY")
        {
            auto& knob = ed.getKnob (id);
            checkArea (id, rectOf (c["hitbox"]), &knob, 3.0f);
            checkArea (id + " readout", rectOf (c["readout"]), &knob, 2.0f);
            const auto centre = pointOf (c["centre"]);
            const auto inEditor = ed.getLocalArea (&knob, knob.getLocalBounds()).toFloat();
            CHECK_MSG (inEditor.reduced (8.0f).contains (refToEditor (centre.x, centre.y)), id + " centre outside its component");
            ++knobsChecked;
        }
        else if (type == "BUTTON_GROUP")
        {
            auto& group = ed.getLampGroup (id);
            int i = 0;
            for (auto& b : *c["buttons"].getArray())
            {
                checkArea (id + " " + b["label"].toString(), rectOf (b["hitbox"]), &group.getLamp (i++), 3.0f);
                ++lampsChecked;
            }
        }
        else if (type == "TOGGLE")
        {
            checkArea (id, rectOf (c["hitbox"]), &ed.getAutoGain(), 4.0f);
            ++toggles;
        }
        else if (type == "VU")
        {
            // the L needle's component takes the mouse (tooltip); R and the cap let it through
            const auto face = rectOf (c["face"]);
            checkArea (id, face.withHeight (face.getHeight() - 22.0f), &ed.getVu (id == "vuRepro", 0), 3.0f);
            ++displays;
        }
        else if (type == "LED_BAR")
        {
            checkArea (id, rectOf (c["hitbox"]), &ed.getSaturationBar(), 3.0f);
            ++displays;
        }
        else if (type == "SPECTRUM")
        {
            checkArea (id, rectOf (c["screen"]), &ed.getSpectrum(), 4.0f);
            ++displays;
        }
        else if (type == "REEL")
        {
            // reels never take the mouse: whatever is under them stays reachable
            auto& reel = ed.getReel (id == "reelLeft" ? 0 : 1);
            const auto centre = pointOf (c["flange"]);
            CHECK (ed.getComponentAt (refToEditor (centre.x, centre.y + 60.0f).toInt()) != &reel);
            const auto inEditor = ed.getLocalArea (&reel, reel.getLocalBounds()).toFloat();
            CHECK_NEAR (inEditor.getCentreX(), refToEditor (centre.x, centre.y).x, 1.0);
            CHECK_NEAR (inEditor.getCentreY(), refToEditor (centre.x, centre.y).y, 1.0);
            ++reels;
        }
    }
    MEASURE ("knob click areas checked", knobsChecked);
    MEASURE ("button click areas checked", lampsChecked);
    CHECK (knobsChecked == 13);
    CHECK (lampsChecked == 3 + 3 + 4 + 2);
    CHECK (toggles == 1);
    CHECK (displays == 4);
    CHECK (reels == 2);

    for (size_t i = 0; i < areas.size(); ++i)
        for (size_t j = i + 1; j < areas.size(); ++j)
        {
            const auto a = areas[i].first.upToFirstOccurrenceOf (" ", false, false);
            const auto b = areas[j].first.upToFirstOccurrenceOf (" ", false, false);
            if (! a.equalsIgnoreCase (b))
                CHECK_MSG (! areas[i].second.intersects (areas[j].second), areas[i].first + " overlaps " + areas[j].first);
        }
}

TEST ("TAPE GUI: every scale number and dot sits where the knob really has that value")
{
    auto p = makeTapeForGui();
    std::unique_ptr<juce::AudioProcessorEditor> base (p->createEditor());
    auto& ed = dynamic_cast<TapeEditor&> (*base);
    const auto map = TapeEditor::loadUiMap();

    float worstLabel = 0.0f, worstPointer = 0.0f, worstDot = 0.0f;
    int labels = 0, dots = 0;
    for (auto& c : *map["controls"].getArray())
    {
        if (c["type"].toString() != "ROTARY")
            continue;
        const auto id = c["id"].toString();
        auto* param = p->apvts.getParameter (c["parameter"].toString());
        auto& knob = ed.getKnob (id);
        const juce::Point<float> sc ((float) c["scaleCentre"][0], (float) c["scaleCentre"][1]);
        const float t0 = (float) c["travelDegrees"][0], t1 = (float) c["travelDegrees"][1];
        auto pointerAt = [&] (float plain)
        {
            param->setValueNotifyingHost (param->convertTo0to1 (plain));
            return t0 + knob.getNormalisedValue() * (t1 - t0);
        };

        for (auto& l : ed.getScaleLabels (id))
        {
            const auto mid = l.box.getCentre().translated (0.0f, -l.box.getHeight() * 0.07f);
            const float printed = juce::radiansToDegrees (std::atan2 (mid.x - sc.x, -(mid.y - sc.y)));
            const float err = std::abs (printed - l.angleDegrees);
            worstLabel = juce::jmax (worstLabel, err);
            CHECK_MSG (err <= 1.0f, id + " '" + l.text + "' printed at " + juce::String (printed, 2) + " deg, value at " + juce::String (l.angleDegrees, 2));
            const float pointer = pointerAt (l.value);
            worstPointer = juce::jmax (worstPointer, std::abs (pointer - l.angleDegrees));
            CHECK_MSG (std::abs (pointer - l.angleDegrees) <= 0.05f, id + " '" + l.text + "' pointer " + juce::String (pointer, 3));
            ++labels;
        }

        const auto& ds = ed.getStepDots (id);
        CHECK_MSG ((int) ds.size() == (int) c["stepDots"]["count"], id + " dots " + juce::String ((int) ds.size()));
        for (int i = 0; i < (int) ds.size(); ++i)
        {
            const float t = (float) i / (float) juce::jmax (1, (int) ds.size() - 1);
            const float pointer = pointerAt (param->convertFrom0to1 (t));
            const auto& d = ds[(size_t) i];
            const float at = juce::radiansToDegrees (std::atan2 (d.centre.x - sc.x, -(d.centre.y - sc.y)));
            worstDot = juce::jmax (worstDot, std::abs (at - pointer));
            CHECK_MSG (std::abs (at - pointer) <= 0.05f, id + " dot " + juce::String (i));
            ++dots;
        }
    }
    MEASURE ("labels checked", labels);
    MEASURE ("live dots checked", dots);
    MEASURE ("worst label angle error (deg)", worstLabel);
    MEASURE ("worst pointer-vs-label error (deg)", worstPointer);
    MEASURE ("worst pointer-vs-dot error (deg)", worstDot);
    CHECK (labels == 13 * 2);
    CHECK (dots == 13 * 11);

    auto texts = [&] (const char* id)
    {
        juce::StringArray s;
        for (auto& l : ed.getScaleLabels (id)) s.add (l.text);
        return s.joinIntoString (" ");
    };
    CHECK_MSG (texts ("input") == "-12 +24", texts ("input"));
    CHECK_MSG (texts ("output") == "-24 +24", texts ("output"));
    for (auto* id : { "bias", "lf", "hf" })
        CHECK_MSG (texts (id) == "-100 +100", texts (id));
    // MIX: the mockup printed "10" at the left end; the knob starts at 0
    for (auto* id : { "saturation", "headBump", "age", "wow", "flutter", "noiseAmount", "crosstalk", "mix" })
        CHECK_MSG (texts (id) == "0 100", texts (id));

    // OUTPUT: 0 dB at the top of the knob (its law is centred)
    setPlain (*p, tp::output, 0.0f);
    CHECK_NEAR (ed.getKnob ("output").getNormalisedValue(), 0.5f, 1e-4);

    // the readout windows print the parameter's own text
    setPlain (*p, tp::input, 6.4f);
    CHECK_MSG (ed.getKnob ("input").getReadoutText() == "+6.4 dB", ed.getKnob ("input").getReadoutText());
    setPlain (*p, tp::saturation, 42.0f);
    CHECK_MSG (ed.getKnob ("saturation").getReadoutText() == "42 %", ed.getKnob ("saturation").getReadoutText());
    setPlain (*p, tp::lf, -12.0f);
    CHECK_MSG (ed.getKnob ("lf").getReadoutText() == "-12", ed.getKnob ("lf").getReadoutText());
    setPlain (*p, tp::hf, 18.0f);
    CHECK_MSG (ed.getKnob ("hf").getReadoutText() == "+18", ed.getKnob ("hf").getReadoutText());
    for (auto* id : knobIds)
    {
        p->apvts.getParameter (id)->setValueNotifyingHost (0.42f);
        CHECK_MSG (ed.getKnob (id).getReadoutText().isNotEmpty(), id);
    }
}

TEST ("TAPE GUI: every control drives its real parameter and follows automation")
{
    auto p = makeTapeForGui();
    p->setUiProperty ("scale", 1.0);
    std::unique_ptr<juce::AudioProcessorEditor> base (p->createEditor());
    auto& ed = dynamic_cast<TapeEditor&> (*base);
    ed.setVisible (true);
    const auto map = TapeEditor::loadUiMap();

    for (auto* id : knobIds)
    {
        auto* r = p->apvts.getParameter (id);
        auto& k = ed.getKnob (id);
        for (float n : { 0.0f, 0.37f, 1.0f })
        {
            r->setValueNotifyingHost (n);
            CHECK_MSG (std::abs (k.getNormalisedValue() - r->getValue()) < 1e-5f, juce::String (id) + " follows automation");
        }
        r->setValueNotifyingHost (0.0f);
        k.keyPressed (juce::KeyPress (juce::KeyPress::upKey));
        CHECK_MSG (r->getValue() > 0.0f, juce::String (id) + " up key");
    }

    // TAPE TYPE, TAPE SPEED, CALIBRATION, NOISE: a click selects; automation lights the right button
    for (auto* id : { "tapeType", "speed", "calibration", "noise" })
    {
        const auto& c = control (map, id);
        auto& group = ed.getLampGroup (id);
        CHECK_MSG (group.getNumLamps() == p->apvts.getParameter (id)->getNumSteps(), juce::String (id) + ": one button per real choice");
        for (int i = 0; i < group.getNumLamps(); ++i)
        {
            clickAt (ed, rectOf (c["buttons"][i]["hitbox"]).getCentre());
            CHECK_MSG (indexOf (*p, id) == i, juce::String (id) + " click " + juce::String (i));
            for (int j = 0; j < group.getNumLamps(); ++j)
                CHECK (group.getLamp (j).getToggleState() == (j == i));
        }
        setIndex (*p, id, 1);
        CHECK (group.getLamp (1).getToggleState() && ! group.getLamp (0).getToggleState());
    }
    // the buttons print the parameter's own choices, in order
    CHECK (ed.getLampGroup ("speed").getLamp (0).getName() == tp::speedNames[0]);
    CHECK (ed.getLampGroup ("calibration").getLamp (3).getName() == tp::calibrationNames[3]);

    // AUTO GAIN: a click flips the parameter; the lamp follows automation
    {
        const auto& c = control (map, "autoGain");
        auto& t = ed.getAutoGain();
        const bool before = plainOf (*p, tp::autoGain) > 0.5f;
        CHECK (before); // on by default
        clickAt (ed, rectOf (c["body"]).getCentre());
        CHECK ((plainOf (*p, tp::autoGain) > 0.5f) != before && t.isOn() != before);
        clickAt (ed, rectOf (c["body"]).getCentre());
        CHECK ((plainOf (*p, tp::autoGain) > 0.5f) == before && t.isOn() == before);
        setPlain (*p, tp::autoGain, 0.0f);
        CHECK (! t.isOn());
        setPlain (*p, tp::autoGain, 1.0f);
        CHECK (t.isOn());
    }
}

TEST ("TAPE GUI: VU needles, saturation bar, response, spectrum and reels show the real state")
{
    auto p = makeTapeForGui();
    p->setUiProperty ("scale", 1.0);
    std::unique_ptr<juce::AudioProcessorEditor> base (p->createEditor());
    auto& ed = dynamic_cast<TapeEditor&> (*base);
    ed.setVisible (true);
    const auto map = TapeEditor::loadUiMap();

    // ---- VU: a -18 dBFS 1 kHz sine is 0 VU at the RECORD point (INPUT 0);
    // each needle points at the printed 0 mark
    {
        frames (*p, ed, 75, dbToGain (-18.0f));
        for (int ch = 0; ch < 2; ++ch)
        {
            const float vu = ed.getVu (false, ch).getNeedleVu();
            MEASURE ("RECORD needle " + juce::String (ch == 0 ? "L" : "R") + " at a -18 dBFS sine (VU)", vu);
            CHECK_NEAR (vu, 0.0f, 0.3);
        }
        const auto& c = control (map, "vuRecord");
        CHECK_NEAR (juce::radiansToDegrees (ed.getVu (false, 0).angleForVu (0.0f)), (float) c["scaleDegrees"]["0"], 1e-3);
        CHECK_NEAR (juce::radiansToDegrees (ed.getVu (false, 0).angleForVu (-7.0f)), (float) c["scaleDegrees"]["-7"], 1e-3);
        const float repro = ed.getVu (true, 0).getNeedleVu();
        MEASURE ("REPRO needle L at a -18 dBFS 1 kHz sine, defaults (VU)", repro);
        CHECK (repro > -3.0f && repro < 3.0f);

        // INPUT +6 dB: RECORD reads +6 dB more (clipped to the pin above +3)
        setPlain (*p, tp::input, 2.0f);
        frames (*p, ed, 75, dbToGain (-18.0f));
        CHECK_NEAR (ed.getVu (false, 0).getNeedleVu(), 2.0f, 0.3);
        setPlain (*p, tp::input, 0.0f);

        // L and R are separate: the right channel silent -> its needles fall to the pin
        frames (*p, ed, 90, dbToGain (-18.0f), 1000.0, 0.0f);
        CHECK_NEAR (ed.getVu (false, 0).getNeedleVu(), 0.0f, 0.3);
        CHECK (ed.getVu (false, 1).getNeedleVu() < -19.0f);
        CHECK (ed.getVu (true, 1).getNeedleVu() < -15.0f); // crosstalk and hiss stay far below
    }

    // ---- SATURATION: dark in silence; lights with drive, more with more drive;
    // the number of lit segments is round(24 x the processor's activity)
    {
        frames (*p, ed, 30, 0.0f);
        CHECK (ed.getSaturationBar().getLitSegments() == 0);
        int lit[3] {};
        const float inputs[3] { 0.0f, 12.0f, 24.0f };
        for (int k = 0; k < 3; ++k)
        {
            setPlain (*p, tp::input, inputs[k]);
            frames (*p, ed, 30, dbToGain (-12.0f), 100.0);
            lit[k] = ed.getSaturationBar().getLitSegments();
            MEASURE ("SATURATION segments lit, 100 Hz at -12 dBFS, INPUT +" + juce::String ((int) inputs[k]) + " dB", lit[k]);
        }
        CHECK (lit[1] > lit[0] && lit[2] > lit[1]);
        CHECK (lit[2] > SaturationBar::numSegments / 2);
        CHECK (SaturationBar::litFor (0.5f) == 12 && SaturationBar::litFor (1.0f) == 24 && SaturationBar::litFor (0.0f) == 0);
        // instant rise, then a fall-back of the display only
        setPlain (*p, tp::input, 0.0f);
        frames (*p, ed, 1, 0.0f);
        CHECK (ed.getSaturationBar().getLitSegments() > 0);
        frames (*p, ed, 45, 0.0f);
        CHECK (ed.getSaturationBar().getLitSegments() == 0);
    }

    // ---- TAPE CONDITION line: exactly the processor's response; it follows the controls
    {
        auto& rc = ed.getResponseCurve();
        auto check = [&] (const juce::String& what)
        {
            ed.refresh (frame);
            float worst = 0.0f;
            const auto& hz = rc.getFrequencies();
            const auto& db = rc.getResponseDb();
            for (size_t i = 0; i < hz.size(); i += 7)
                worst = juce::jmax (worst, std::abs (db[i] - (float) p->tapeResponseDb (hz[i])));
            MEASURE ("response line vs processor, " + what + " (dB)", worst);
            CHECK (worst < 1e-3f);
        };
        check ("defaults");
        const float bump15 = *std::max_element (rc.getResponseDb().begin(), rc.getResponseDb().begin() + 40);
        setIndex (*p, tp::speed, 0);
        setPlain (*p, tp::headBump, 100.0f);
        frames (*p, ed, 10, 0.0f); // the speed glides
        check ("7.5 IPS, HEAD BUMP 100");
        setPlain (*p, tp::hf, -100.0f);
        frames (*p, ed, 2, 0.0f);
        check ("HF -100");
        const auto& db = rc.getResponseDb();
        CHECK (db.back() < db[db.size() / 2] - 3.0f);
        MEASURE ("LF peak of the line at 15 IPS, defaults (dB)", bump15);
        // the curve's 0 dB sits on the dashed line, +12 dB at the top end of its range
        CHECK (rc.yForResponseDb (12.0f) < rc.yForResponseDb (0.0f));
        setIndex (*p, tp::speed, 1);
        setPlain (*p, tp::headBump, 36.0f);
        setPlain (*p, tp::hf, 0.0f);
    }

    // ---- TAPE CONDITION spectrum: the output's real spectrum (MIX 0: the output is the dry sine)
    {
        setPlain (*p, tp::mix, 0.0f);
        ed.getSpectrum().setQuality (ui::SpectrumDisplay::Quality::high);
        frames (*p, ed, 45, dbToGain (-12.0f), 1000.0);
        ed.getSpectrum().processPending();
        const float shown = ed.getSpectrum().getFrameDb (1000.0f);
        MEASURE ("spectrum at 1 kHz for a -12 dBFS sine, MIX 0 (dB)", shown);
        CHECK_NEAR (shown, -12.0f, 0.6);
        setPlain (*p, tp::mix, 100.0f);
    }

    // ---- reels: still without signal (no playhead in the test host); while
    // signal runs, they turn counter-clockwise at IPS / (2 pi x 3.5 in) turns per second
    {
        frames (*p, ed, 30, 0.0f); // longer than the 0.5 s hold
        const float a0 = ed.getReel (0).getAngle();
        frames (*p, ed, 10, 0.0f);
        CHECK (juce::exactlyEqual (ed.getReel (0).getAngle(), a0));
        CHECK (! p->isTransportRunning());

        frames (*p, ed, 2, dbToGain (-30.0f));
        CHECK (p->isTransportRunning());
        for (int speed = 0; speed < 3; ++speed)
        {
            setIndex (*p, tp::speed, speed);
            frames (*p, ed, 10, dbToGain (-30.0f)); // the speed glides over 100 ms
            const float before = ed.getReel (1).getAngle();
            feed (*p, dbToGain (-30.0f), 0.5);
            ed.refresh (0.5f);
            float turned = before - ed.getReel (1).getAngle();
            if (turned < 0.0f)
                turned += juce::MathConstants<float>::twoPi;
            const double ips = tp::speedIps[speed];
            const double expected = std::fmod (juce::MathConstants<double>::twoPi * TapeReel::turnsPerSecond (ips) * 0.5,
                                               juce::MathConstants<double>::twoPi);
            MEASURE ("reel turn in 0.5 s at " + tp::speedNames[speed] + " IPS (rad)", turned);
            CHECK_NEAR (turned, expected, 1e-3);
        }
        CHECK_NEAR (TapeReel::turnsPerSecond (15.0), 15.0 / (juce::MathConstants<double>::twoPi * 3.5), 1e-9);
    }
}

TEST ("TAPE GUI: the reels are drawn turned, under a fixed light, only inside the window")
{
    auto p = makeTapeForGui();
    p->setUiProperty ("scale", 1.0);
    std::unique_ptr<juce::AudioProcessorEditor> base (p->createEditor());
    auto& ed = dynamic_cast<TapeEditor&> (*base);
    ed.setVisible (true);
    const auto map = TapeEditor::loadUiMap();
    const auto& c = control (map, "reelLeft");
    const auto centre = pointOf (c["flange"]);
    auto pixel = [&] (const juce::Image& img, float rx, float ry)
    {
        const auto pt = refToEditor (rx, ry).toInt();
        return img.getPixelAt (pt.x, pt.y);
    };

    auto& reel = ed.getReel (0);
    reel.setAngle (0.0f);
    const auto a = snapshot (ed);
    reel.setAngle (juce::degreesToRadians (60.0f)); // three windows: 60 degrees swaps window and metal
    const auto b = snapshot (ed);
    reel.setAngle (juce::degreesToRadians (120.0f)); // one window pitch: the same picture (bar the label)
    const auto d = snapshot (ed);

    // above the transport window's top edge nothing of the reel shows, at any angle
    double above = 0, inside = 0, pitch = 0;
    int nAbove = 0, nInside = 0;
    for (float x = centre.x - 120.0f; x <= centre.x + 120.0f; x += 4.0f)
    {
        for (float y = 140.0f; y < 182.0f; y += 3.0f)
        {
            above += std::abs (pixel (a, x, y).getBrightness() - pixel (b, x, y).getBrightness());
            ++nAbove;
        }
        for (float y = centre.y + 20.0f; y < centre.y + 130.0f; y += 4.0f)
        {
            inside += std::abs (pixel (a, x, y).getBrightness() - pixel (b, x, y).getBrightness());
            pitch += std::abs (pixel (a, x, y).getBrightness() - pixel (d, x, y).getBrightness());
            ++nInside;
        }
    }
    MEASURE ("mean change above the window when the reel turns", above / nAbove);
    MEASURE ("mean change on the reel, 60 degrees", inside / nInside);
    MEASURE ("mean change on the reel, 120 degrees (one window pitch)", pitch / nInside);
    CHECK (above / nAbove < 1e-6);
    CHECK (inside / nInside > 0.05);
    CHECK (pitch / nInside < 0.5 * inside / nInside);
    saveSnapshot (ed, "TAPE_reel_60deg");
}

TEST ("TAPE GUI: no dead controls: moving each control from the editor changes the sound")
{
    struct Case { const char* id; float from, to; };
    const Case cases[] = {
        { tp::input, 0, 8 }, { tp::output, 0, -6 }, { tp::tapeType, 0, 2 }, { tp::speed, 1, 0 }, { tp::calibration, 1, 3 },
        { tp::bias, 0, -60 }, { tp::saturation, 42, 90 }, { tp::headBump, 36, 100 }, { tp::lf, 0, 60 }, { tp::hf, 0, -60 },
        { tp::age, 18, 80 }, { tp::wow, 6, 60 }, { tp::flutter, 4, 60 }, { tp::noise, 0, 1 }, { tp::noiseAmount, 12, 80 },
        { tp::crosstalk, 8, 80 }, { tp::mix, 100, 40 }, { tp::autoGain, 1, 0 },
    };
    for (auto& cs : cases)
    {
        std::array<std::vector<float>, 2> out;
        for (int k = 0; k < 2; ++k)
        {
            auto q = makeTapeForGui();
            std::unique_ptr<juce::AudioProcessorEditor> b (q->createEditor());
            auto& e = dynamic_cast<TapeEditor&> (*b);
            const auto id = juce::String (cs.id);
            if (id == tp::noiseAmount)
                setPlain (*q, tp::noise, 1.0f);
            if (id == tp::noise) // the default 12 % hiss sits right at -60 dB
                setPlain (*q, tp::noiseAmount, 50.0f);
            if (id == tp::autoGain) // AUTO GAIN undoes INPUT's level change
                setPlain (*q, tp::input, 8.0f);
            const float v = k == 0 ? cs.from : cs.to;
            auto* param = q->apvts.getParameter (cs.id);
            bool isKnob = false;
            for (auto* kid : knobIds)
                isKnob = isKnob || id == kid;
            if (isKnob)
                e.getKnob (id).setNormalisedFromUser (param->convertTo0to1 (v), true);
            else
                setPlain (*q, cs.id, v);
            CHECK_NEAR (param->convertFrom0to1 (param->getValue()), v, 1e-2 * juce::jmax (1.0f, std::abs (v)));
            out[(size_t) k] = render (*q);
        }
        const double diff = rmsDifferenceDb (out[0], out[1]);
        MEASURE (juce::String (cs.id) + ": change of the output (dB rel. signal)", diff);
        CHECK_MSG (diff > -60.0, juce::String (cs.id) + " changed the sound by only " + juce::String (diff, 1) + " dB");
    }
}
