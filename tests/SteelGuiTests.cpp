#include "TestFramework.h"
#include "../plugins/STEEL/PluginProcessor.h"
#include "../plugins/STEEL/PluginEditor.h"
#include "../plugins/STEEL/Parameters.h"

// GUI tests for STEEL (plugins/STEEL/GUI_TEST_PLAN.md). Every control is
// checked against UI_MAP.json and against the real parameter / DSP state.

using namespace tleet;
using namespace tleet::test;
using tleet::steel::SteelProcessor;
using tleet::steel::SteelEditor;
using tleet::steel::LoudnessPanel;
namespace sp = tleet::steel::params;

namespace
{
    constexpr double sr = 48000.0;
    constexpr int block = 256;
    constexpr float frame = 1.0f / 30.0f;

    std::unique_ptr<SteelProcessor> makeSteelForGui()
    {
        auto p = std::make_unique<SteelProcessor>();
        p->setRateAndBufferSizeDetails (sr, block);
        p->prepareToPlay (sr, block);
        return p;
    }

    void setPlain (SteelProcessor& p, const char* id, float plain)
    {
        auto* r = p.apvts.getParameter (id);
        r->setValueNotifyingHost (r->convertTo0to1 (plain));
    }

    void setIndex (SteelProcessor& p, const char* id, int index) { setPlain (p, id, (float) index); }

    float plainOf (SteelProcessor& p, const char* id)
    {
        auto* r = p.apvts.getParameter (id);
        return r->convertFrom0to1 (r->getValue());
    }

    int indexOf (SteelProcessor& p, const char* id) { return juce::roundToInt (plainOf (p, id)); }

    // A fixed programme: kick-like bursts on a 45 Hz bass, a bright noise
    // layer that differs left / right, and a high sine near fs/4 for the
    // inter-sample peaks. Loud enough to make the limiter work at INPUT 0.
    // `clock` carries the programme on across calls (screenshots).
    std::vector<float> render (SteelProcessor& p, double seconds = 1.0, long* clock = nullptr)
    {
        juce::AudioBuffer<float> buf (2, block);
        juce::MidiBuffer midi;
        juce::Random rng (11 + (clock != nullptr ? *clock : 0));
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
                const double beat = std::fmod (t, 0.25);
                const float kick = (float) (std::exp (-beat * 30.0) * std::sin (twoPi * (50.0 + 80.0 * std::exp (-beat * 40.0)) * beat));
                const float bass = 0.35f * (float) std::sin (twoPi * 45.0 * t);
                const float hi = 0.25f * (float) std::sin (twoPi * 11025.0 * t + 0.785);
                const float nl = 0.3f * (rng.nextFloat() * 2.0f - 1.0f), nr = 0.3f * (rng.nextFloat() * 2.0f - 1.0f);
                buf.setSample (0, i, 0.9f * kick + bass + hi + nl);
                buf.setSample (1, i, 0.5f * kick + bass + 0.4f * hi + nr);
            }
            p.processBlock (buf, midi);
            out.insert (out.end(), buf.getReadPointer (0), buf.getReadPointer (0) + block);
            out.insert (out.end(), buf.getReadPointer (1), buf.getReadPointer (1) + block);
        }
        return out;
    }

    // `seconds` of a steady sine (or noise when hz <= 0) of `peak` through the plugin.
    void feed (SteelProcessor& p, float peak, double seconds, double hz = 1000.0)
    {
        static double phase = 0.0;
        static juce::Random rng (5);
        juce::AudioBuffer<float> buf (2, block);
        juce::MidiBuffer midi;
        for (int b = 0; b < juce::jmax (1, (int) (seconds * sr / block)); ++b)
        {
            for (int i = 0; i < block; ++i)
            {
                float s;
                if (hz > 0.0)
                {
                    s = peak * (float) std::sin (phase);
                    phase += 2.0 * juce::MathConstants<double>::pi * hz / sr;
                }
                else
                    s = peak * (rng.nextFloat() * 2.0f - 1.0f);
                buf.setSample (0, i, s);
                buf.setSample (1, i, s);
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
        return { x * SteelEditor::skinScale, ui::TleetEditorBase::toolbarHeight + y * SteelEditor::skinScale };
    }

    juce::Rectangle<float> refToEditor (juce::Rectangle<float> r)
    {
        const auto tl = refToEditor (r.getX(), r.getY());
        return { tl.x, tl.y, r.getWidth() * SteelEditor::skinScale, r.getHeight() * SteelEditor::skinScale };
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

    float brightnessAt (const juce::Image& img, juce::Rectangle<float> editorRect)
    {
        const auto r = editorRect.toNearestInt().getIntersection (img.getBounds());
        double sum = 0; int n = 0;
        for (int y = r.getY(); y < r.getBottom(); ++y)
            for (int x = r.getX(); x < r.getRight(); ++x)
            {
                const auto c = img.getPixelAt (x, y);
                sum += c.getRed() + c.getGreen() + c.getBlue();
                ++n;
            }
        return n > 0 ? (float) (sum / n) : 0.0f;
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
    void clickAt (SteelEditor& ed, juce::Point<float> refPoint)
    {
        auto* hit = ed.getComponentAt (refToEditor (refPoint.x, refPoint.y).toInt());
        CHECK_MSG (hit != nullptr, "nothing at " + refPoint.toString());
        if (hit != nullptr)
            click (*hit, ed, refPoint);
    }

    const char* const knobIds[] = { "input", "ceiling", "release", "lookahead", "stereoLink", "softClipAmount", "output" };
    const char* const toggleIds[] = { "truePeak", "softClip", "delta", "levelMatch" };

    void frames (SteelProcessor& p, SteelEditor& ed, int n, float peak, double hz = 1000.0)
    {
        for (int i = 0; i < n; ++i)
        {
            feed (p, peak, frame, hz);
            ed.refresh (frame);
        }
    }
}

TEST ("STEEL GUI: editor opens at every scale; screenshots of default and representative states")
{
    auto p = makeSteelForGui();
    for (float scale : { 0.75f, 1.0f, 1.5f, 2.0f })
    {
        p->setUiProperty ("scale", scale);
        std::unique_ptr<juce::AudioProcessorEditor> ed (p->createEditor());
        CHECK (dynamic_cast<SteelEditor*> (ed.get()) != nullptr);
        const int expectedW = juce::roundToInt (SteelEditor::baseWidth * scale);
        CHECK_MSG (std::abs (ed->getWidth() - expectedW) <= 1, "width at scale " + juce::String (scale));
        saveSnapshot (*ed, "STEEL_editor_" + juce::String (juce::roundToInt (scale * 100)));
    }

    p->setUiProperty ("scale", 1.0);
    std::unique_ptr<juce::AudioProcessorEditor> base (p->createEditor());
    auto& ed = dynamic_cast<SteelEditor&> (*base);
    ed.setVisible (true);
    struct State { const char* name; std::function<void()> apply; };
    const State states[] = {
        { "steel_loud", [&] { setPlain (*p, sp::input, 8.0f); setPlain (*p, sp::ceiling, -1.0f); } },
        { "clean_manual_softclip", [&] { setIndex (*p, sp::mode, 0); setIndex (*p, sp::releaseMode, 1); setPlain (*p, sp::release, 420.0f);
                                         setPlain (*p, sp::softClip, 1.0f); setPlain (*p, sp::softClipAmount, 60.0f);
                                         setIndex (*p, sp::subFilter, 2); setIndex (*p, sp::oversampling, 3); setPlain (*p, sp::input, 4.0f); } },
        { "punch_delta", [&] { setIndex (*p, sp::mode, 1); setPlain (*p, sp::delta, 1.0f); setPlain (*p, sp::stereoLink, 40.0f);
                               setPlain (*p, sp::lookahead, 1.2f); setPlain (*p, sp::input, 10.0f); } },
        { "dense_levelmatch_tp_off", [&] { setIndex (*p, sp::mode, 2); setPlain (*p, sp::delta, 0.0f); setPlain (*p, sp::levelMatch, 1.0f);
                                           setPlain (*p, sp::truePeak, 0.0f); setPlain (*p, sp::ceiling, -0.3f); setPlain (*p, sp::output, 2.0f); } },
    };
    for (auto& st : states)
    {
        st.apply();
        ed.getSpectrum().setQuality (ui::SpectrumDisplay::Quality::normal);
        long clock = 0;
        for (int i = 0; i < 90; ++i)
        {
            render (*p, frame, &clock);
            ed.refresh (frame);
            ed.getSpectrum().processPending();
        }
        saveSnapshot (ed, juce::String ("STEEL_state_") + st.name);
    }
}

TEST ("STEEL GUI: every mapped control sits at its mockup position and owns its click area")
{
    auto p = makeSteelForGui();
    p->setUiProperty ("scale", 1.0);
    std::unique_ptr<juce::AudioProcessorEditor> base (p->createEditor());
    auto& ed = dynamic_cast<SteelEditor&> (*base);
    ed.setVisible (true);
    const auto map = SteelEditor::loadUiMap();

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

    int knobsChecked = 0, lampsChecked = 0, togglesChecked = 0, displays = 0;
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
            checkArea (id, rectOf (c["hitbox"]), &ed.getToggle (id), 4.0f);
            ++togglesChecked;
        }
        else if (type == "HISTORY")
        {
            checkArea ("history", rectOf (c["hitbox"]), &ed.getHistory(), 3.0f);
            ++displays;
        }
        else if (type == "BAR_METERS")
        {
            checkArea ("meters IN", rectOf (c["hitboxIn"]), &ed.getMeters (false), 3.0f);
            checkArea ("meters OUT", rectOf (c["hitboxOut"]), &ed.getMeters (true), 3.0f);
            ++displays;
        }
        else if (type == "READOUT_PANEL")
        {
            checkArea ("loudness", rectOf (c["hitbox"]), &ed.getLoudness(), 3.0f);
            ++displays;
        }
        else if (type == "SPECTRUM")
        {
            checkArea ("spectrum", rectOf (c["screen"]), &ed.getSpectrum(), 4.0f);
            ++displays;
        }
    }
    MEASURE ("knob click areas checked", knobsChecked);
    MEASURE ("button click areas checked", lampsChecked);
    CHECK (knobsChecked == 7);
    CHECK (lampsChecked == 4 + 2 + 4 + 4);
    CHECK (togglesChecked == 4);
    CHECK (displays == 4);

    for (size_t i = 0; i < areas.size(); ++i)
        for (size_t j = i + 1; j < areas.size(); ++j)
        {
            const auto a = areas[i].first.upToFirstOccurrenceOf (" ", false, false);
            const auto b = areas[j].first.upToFirstOccurrenceOf (" ", false, false);
            if (! a.equalsIgnoreCase (b))
                CHECK_MSG (! areas[i].second.intersects (areas[j].second), areas[i].first + " overlaps " + areas[j].first);
        }
}

TEST ("STEEL GUI: every scale number and dot sits where the knob really has that value")
{
    auto p = makeSteelForGui();
    std::unique_ptr<juce::AudioProcessorEditor> base (p->createEditor());
    auto& ed = dynamic_cast<SteelEditor&> (*base);
    const auto map = SteelEditor::loadUiMap();

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
        const int expected = (int) c["stepDots"]["count"];
        CHECK_MSG ((int) ds.size() == expected, id + " dots " + juce::String ((int) ds.size()));
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
    CHECK (labels == 3 + 2 * 4);
    CHECK (dots == 13 * 2 + 11 * 5);

    auto texts = [&] (const char* id)
    {
        juce::StringArray s;
        for (auto& l : ed.getScaleLabels (id)) s.add (l.text);
        return s.joinIntoString (" ");
    };
    CHECK_MSG (texts ("release") == "20 250 1K", texts ("release"));
    CHECK_MSG (texts ("lookahead") == "0 10", texts ("lookahead"));
    CHECK_MSG (texts ("output") == "-12 +12", texts ("output"));
    for (auto* id : { "stereoLink", "softClipAmount" })
        CHECK_MSG (texts (id) == "0 100", texts (id));

    // RELEASE: 250 ms is printed at the top of the knob, so the knob centre is 250 ms
    for (auto& l : ed.getScaleLabels ("release"))
        if (l.text == "250")
        {
            MEASURE ("RELEASE 250 ms label angle (deg)", l.angleDegrees);
            CHECK (std::abs (l.angleDegrees) < 0.5f);
        }

    // the readout windows print the parameter's own text
    for (auto* id : knobIds)
    {
        auto* r = p->apvts.getParameter (id);
        r->setValueNotifyingHost (0.42f);
        CHECK_MSG (ed.getKnob (id).getReadoutText().isNotEmpty(), id);
    }
}

TEST ("STEEL GUI: every control drives its real parameter and follows automation")
{
    auto p = makeSteelForGui();
    p->setUiProperty ("scale", 1.0);
    std::unique_ptr<juce::AudioProcessorEditor> base (p->createEditor());
    auto& ed = dynamic_cast<SteelEditor&> (*base);
    ed.setVisible (true);
    const auto map = SteelEditor::loadUiMap();

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

    // MODE, RELEASE MODE, OVERSAMPLING, SUB FILTER: a click selects; automation lights the right button
    for (auto* id : { "mode", "releaseMode", "oversampling", "subFilter" })
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

    // toggles: a click flips the parameter; the lamp follows automation
    for (auto* id : toggleIds)
    {
        const auto& c = control (map, id);
        auto& t = ed.getToggle (id);
        const bool before = plainOf (*p, id) > 0.5f;
        clickAt (ed, rectOf (c["body"]).getCentre());
        CHECK_MSG ((plainOf (*p, id) > 0.5f) != before && t.isOn() != before, juce::String (id) + " click");
        clickAt (ed, rectOf (c["body"]).getCentre());
        CHECK_MSG ((plainOf (*p, id) > 0.5f) == before && t.isOn() == before, juce::String (id) + " click back");
        setPlain (*p, id, 1.0f);
        CHECK (t.isOn());
        setPlain (*p, id, 0.0f);
        CHECK (! t.isOn());
    }
}

TEST ("STEEL GUI: meters, history, loudness and spectrum show the real DSP state")
{
    auto p = makeSteelForGui();
    p->setUiProperty ("scale", 1.0);
    std::unique_ptr<juce::AudioProcessorEditor> base (p->createEditor());
    auto& ed = dynamic_cast<SteelEditor&> (*base);
    ed.setVisible (true);
    const auto map = SteelEditor::loadUiMap();

    // IN / OUT: a 1 kHz sine at -6 dBFS (below the ceiling) reads -6 on all eight bars
    {
        const float peak = std::pow (10.0f, -6.0f / 20.0f);
        frames (*p, ed, 20, peak);
        for (bool out : { false, true })
            for (int i = 0; i < 4; ++i)
            {
                const float db = ed.getMeters (out).getDisplayedDb (i);
                MEASURE (juce::String (out ? "OUT" : "IN") + " bar " + juce::String (i) + " at a -6 dBFS sine (dB)", db);
                CHECK_NEAR (db, -6.0f, 0.25);
            }
        const auto& c = control (map, "meters");
        const float y = ed.getMeters (false).yForDb (-6.0f) / SteelEditor::skinScale + rectOf (c["hitboxIn"]).getY();
        CHECK_NEAR (y, (float) c["scale"]["-6"], 0.01);
        frames (*p, ed, 90, 0.0f);
        CHECK (ed.getMeters (false).getDisplayedDb (0) < -48.0f);
        CHECK (ed.getMeters (true).getDisplayedDb (3) < -48.0f);
    }

    // history: slices arrive at the processor's rate; with the limiter working, the
    // trace is the applied gain reduction and the output never exceeds the ceiling
    {
        auto& h = ed.getHistory();
        const int before = h.getNumFilled();
        setPlain (*p, sp::input, 12.0f);
        setPlain (*p, sp::ceiling, -1.0f);
        frames (*p, ed, 30, 0.9f, 200.0); // one second
        const int added = h.getNumFilled() - before;
        MEASURE ("history slices per second", added);
        CHECK (std::abs (added - (int) std::round (1.0 / SteelProcessor::historySliceSeconds)) <= 2);
        float deepest = 0.0f, loudest = -200.0f;
        for (int a = 0; a < 20; ++a)
        {
            deepest = std::min (deepest, h.getSlice (a).grDb);
            loudest = std::max (loudest, h.getSlice (a).peakDb);
        }
        MEASURE ("history: deepest GR in the last slices (dB)", deepest);
        MEASURE ("history: loudest output peak in the last slices (dBFS)", loudest);
        MEASURE ("processor GR now (dB)", p->getCurrentGainReductionDb());
        CHECK (deepest < -6.0f);
        CHECK_NEAR (deepest, p->getCurrentGainReductionDb(), 1.0);
        CHECK (loudest <= -1.0f + 0.05f);
        CHECK_NEAR (h.yForGrDb (-6.0f) - h.yForGrDb (0.0f), h.yForDb (-6.0f) - h.yForDb (0.0f), 1e-3);
        CHECK_NEAR (h.getCeilingDb(), -1.0f, 1e-4);
        // the loudness panel's GR is the deepest applied GR of the last 0.3 s
        CHECK (ed.getLoudness().getText (LoudnessPanel::gainReduction) == LoudnessPanel::format (LoudnessPanel::gainReduction, h.getDeepestGrDb (0.3)));
        // warnings shown == the processor's own flags
        CHECK (h.isTruePeakOverShown() == p->isTruePeakOver());
        CHECK (h.isHeavyLimitingShown() == p->isHeavyLimiting());
        setPlain (*p, sp::input, 0.0f);
    }

    // loudness: a stereo 1 kHz sine at -23 dBFS reads -23.0 LUFS; a click on the panel resets it
    {
        p->resetLoudness();
        const float peak = std::pow (10.0f, -23.0f / 20.0f);
        frames (*p, ed, 120, peak); // 4 s
        const auto& L = ed.getLoudness();
        MEASURE ("LUFS-I shown", L.getText (LoudnessPanel::lufsI).getFloatValue());
        MEASURE ("LUFS-S shown", L.getText (LoudnessPanel::lufsS).getFloatValue());
        MEASURE ("TRUE PEAK shown", L.getText (LoudnessPanel::truePeak).getFloatValue());
        CHECK (L.getText (LoudnessPanel::lufsS) == "-23.0");
        CHECK (L.getText (LoudnessPanel::lufsI) == "-23.0");
        CHECK (L.getText (LoudnessPanel::truePeak) == "-23.0");
        const auto& c = control (map, "loudness");
        clickAt (ed, rectOf (c["hitbox"]).getCentre());
        ed.refresh (frame);
        CHECK_MSG (ed.getLoudness().getText (LoudnessPanel::lufsI) == "--", ed.getLoudness().getText (LoudnessPanel::lufsI));
    }

    // the lit TRUE PEAK / DELTA buttons are lit on screen (pixels)
    {
        const auto& c = control (map, "delta");
        const auto face = refToEditor (rectOf (c["body"]).reduced (10.0f));
        setPlain (*p, sp::delta, 1.0f);
        const float on = brightnessAt (snapshot (ed), face);
        setPlain (*p, sp::delta, 0.0f);
        const float off = brightnessAt (snapshot (ed), face);
        MEASURE ("DELTA lit / dark brightness", on / juce::jmax (1.0f, off));
        CHECK (on > off * 1.3f);
    }

    // spectrum: the output spectrum of a 1 kHz sine peaks at 1 kHz
    {
        auto& s = ed.getSpectrum();
        s.setQuality (ui::SpectrumDisplay::Quality::normal);
        for (int i = 0; i < 40; ++i)
        {
            feed (*p, 0.25f, frame, 1000.0);
            s.processPending();
        }
        const float at1k = s.getFrameDb (1000.0f), at100 = s.getFrameDb (100.0f), at10k = s.getFrameDb (10000.0f);
        MEASURE ("spectrum at 1 kHz (dB)", at1k);
        CHECK (at1k > at100 + 20.0f);
        CHECK (at1k > at10k + 20.0f);
    }
}

TEST ("STEEL GUI: no dead controls: moving each control from the editor changes the sound")
{
    struct Case { const char* id; float from, to; };
    const Case cases[] = {
        { sp::input, 0, 6 }, { sp::ceiling, -1, -4 }, { sp::mode, 0, 3 }, { sp::releaseMode, 0, 1 }, { sp::release, 40, 600 },
        { sp::lookahead, 0.5f, 6 }, { sp::truePeak, 1, 0 }, { sp::oversampling, 1, 3 }, { sp::stereoLink, 100, 0 },
        { sp::softClip, 0, 1 }, { sp::softClipAmount, 10, 90 }, { sp::subFilter, 0, 3 }, { sp::delta, 0, 1 },
        { sp::levelMatch, 0, 1 }, { sp::output, 0, -6 },
    };
    for (auto& cs : cases)
    {
        std::array<std::vector<float>, 2> out;
        for (int k = 0; k < 2; ++k)
        {
            auto q = makeSteelForGui();
            std::unique_ptr<juce::AudioProcessorEditor> b (q->createEditor());
            auto& e = dynamic_cast<SteelEditor&> (*b);
            const auto id = juce::String (cs.id);
            if (id == sp::release)
                setIndex (*q, sp::releaseMode, 1);
            if (id == sp::softClipAmount)
                setPlain (*q, sp::softClip, 1.0f);
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
