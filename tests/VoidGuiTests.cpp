#include "TestFramework.h"
#include "../plugins/VOID/PluginProcessor.h"
#include "../plugins/VOID/PluginEditor.h"
#include "../plugins/VOID/Parameters.h"

// GUI tests for VOID (plugins/VOID/GUI_TEST_PLAN.md). Every control is
// checked against UI_MAP.json and against the real parameter / DSP state.

using namespace tleet;
using namespace tleet::test;
using tleet::voidrev::VoidProcessor;
using tleet::voidrev::VoidEditor;
namespace vp = tleet::voidrev::params;

namespace
{
    constexpr double sr = 48000.0;
    constexpr int block = 256;
    constexpr float frame = 1.0f / 30.0f;

    std::unique_ptr<VoidProcessor> makeVoidForGui()
    {
        auto p = std::make_unique<VoidProcessor>();
        p->setRateAndBufferSizeDetails (sr, block);
        p->prepareToPlay (sr, block);
        return p;
    }

    void setPlain (VoidProcessor& p, const char* id, float plain)
    {
        auto* r = p.apvts.getParameter (id);
        r->setValueNotifyingHost (r->convertTo0to1 (plain));
    }

    void setIndex (VoidProcessor& p, const char* id, int index) { setPlain (p, id, (float) index); }

    float plainOf (VoidProcessor& p, const char* id)
    {
        auto* r = p.apvts.getParameter (id);
        return r->convertFrom0to1 (r->getValue());
    }

    int indexOf (VoidProcessor& p, const char* id) { return juce::roundToInt (plainOf (p, id)); }

    // A fixed test signal: a noise burst, then a quiet steady sine, so the
    // early reflections, the tail and the dry path all carry signal. The
    // output of the whole run, left channel.
    std::vector<float> render (VoidProcessor& p, double seconds = 1.2)
    {
        juce::AudioBuffer<float> buf (2, block);
        juce::MidiBuffer midi;
        juce::Random rng (11);
        std::vector<float> out;
        const int blocks = (int) (seconds * sr / block);
        long n = 0;
        for (int b = 0; b < blocks; ++b)
        {
            for (int i = 0; i < block; ++i, ++n)
            {
                const double t = (double) n / sr;
                const float noise = t < 0.08 ? 0.3f * (rng.nextFloat() * 2.0f - 1.0f) : 0.0f;
                const float sine = t > 0.5 ? 0.06f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 330.0 * t) : 0.0f;
                buf.setSample (0, i, noise + sine);
                buf.setSample (1, i, 0.8f * noise + sine);
            }
            p.processBlock (buf, midi);
            out.insert (out.end(), buf.getReadPointer (0), buf.getReadPointer (0) + block);
        }
        return out;
    }

    // `seconds` of a steady sine (or noise when hz <= 0) of `peak` through the plugin.
    void feed (VoidProcessor& p, float peak, double seconds, double hz = 1000.0)
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
        return { x * VoidEditor::skinScale, ui::TleetEditorBase::toolbarHeight + y * VoidEditor::skinScale };
    }

    juce::Rectangle<float> refToEditor (juce::Rectangle<float> r)
    {
        const auto tl = refToEditor (r.getX(), r.getY());
        return { tl.x, tl.y, r.getWidth() * VoidEditor::skinScale, r.getHeight() * VoidEditor::skinScale };
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
        return n > 0 ? (float) (sum / n / 3.0) : 0.0f;
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
    void clickAt (VoidEditor& ed, juce::Point<float> refPoint)
    {
        auto* hit = ed.getComponentAt (refToEditor (refPoint.x, refPoint.y).toInt());
        CHECK_MSG (hit != nullptr, "nothing at " + refPoint.toString());
        if (hit != nullptr)
            click (*hit, ed, refPoint);
    }

    const char* const continuousKnobs[] = { "preDelay", "size", "decay", "diffusion", "modRate", "modDepth", "width",
                                            "hpf", "lpf", "duck", "mix", "output" };

    void frames (VoidProcessor& p, VoidEditor& ed, int n, float peak, double hz = 1000.0)
    {
        for (int i = 0; i < n; ++i)
        {
            feed (p, peak, frame, hz);
            ed.refresh (frame);
        }
    }
}

TEST ("VOID GUI: editor opens at every scale; screenshots of default and representative states")
{
    auto p = makeVoidForGui();
    for (float scale : { 0.75f, 1.0f, 1.5f, 2.0f })
    {
        p->setUiProperty ("scale", scale);
        std::unique_ptr<juce::AudioProcessorEditor> ed (p->createEditor());
        CHECK (dynamic_cast<VoidEditor*> (ed.get()) != nullptr);
        const int expectedW = juce::roundToInt (VoidEditor::baseWidth * scale);
        CHECK_MSG (std::abs (ed->getWidth() - expectedW) <= 1, "width at scale " + juce::String (scale));
        saveSnapshot (*ed, "VOID_editor_" + juce::String (juce::roundToInt (scale * 100)));
    }

    p->setUiProperty ("scale", 1.0);
    std::unique_ptr<juce::AudioProcessorEditor> base (p->createEditor());
    auto& ed = dynamic_cast<VoidEditor&> (*base);
    ed.setVisible (true);
    struct State { const char* name; std::function<void()> apply; };
    const State states[] = {
        { "hall_playing", [&] { setPlain (*p, vp::mix, 60.0f); } },
        { "plate_dark_prefilter", [&] { setIndex (*p, vp::algorithm, 1); setIndex (*p, vp::color, 1); setIndex (*p, vp::filterMode, 0);
                                        setPlain (*p, vp::hpf, 180.0f); setPlain (*p, vp::lpf, 6500.0f); setPlain (*p, vp::decay, 1.6f);
                                        setPlain (*p, vp::preDelay, 42.0f); } },
        { "void_ruin_drown", [&] { setIndex (*p, vp::algorithm, 4); setIndex (*p, vp::color, 2); setIndex (*p, vp::filterMode, 1);
                                   setPlain (*p, vp::drown, 70.0f); setPlain (*p, vp::decay, vp::decayMaxS); setPlain (*p, vp::width, 160.0f);
                                   setPlain (*p, vp::mix, 100.0f); setPlain (*p, vp::output, -3.0f); } },
        { "frozen", [&] { setIndex (*p, vp::algorithm, 3); setPlain (*p, vp::freeze, 1.0f); setPlain (*p, vp::drown, 0.0f);
                          setPlain (*p, vp::decay, 8.0f); setIndex (*p, vp::color, 0); } },
    };
    for (auto& st : states)
    {
        st.apply();
        ed.getSpectrum().setQuality (ui::SpectrumDisplay::Quality::normal);
        for (int i = 0; i < 24; ++i)
        {
            feed (*p, i < 6 ? 0.25f : 0.0f, frame, -1.0);
            ed.refresh (frame);
            ed.getSpectrum().processPending();
        }
        saveSnapshot (ed, juce::String ("VOID_state_") + st.name);
    }
}

TEST ("VOID GUI: every mapped control sits at its mockup position and owns its click area")
{
    auto p = makeVoidForGui();
    p->setUiProperty ("scale", 1.0);
    std::unique_ptr<juce::AudioProcessorEditor> base (p->createEditor());
    auto& ed = dynamic_cast<VoidEditor&> (*base);
    ed.setVisible (true);
    const auto map = VoidEditor::loadUiMap();

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

    int knobsChecked = 0, lampsChecked = 0;
    for (auto& c : *map["controls"].getArray())
    {
        const auto type = c["type"].toString();
        const auto id = c["id"].toString();
        if (type.startsWith ("ROTARY") || type == "ARC_KNOB")
        {
            const auto hb = rectOf (c["hitbox"]);
            auto& knob = ed.getKnob (id);
            checkArea (id, hb, &knob, 3.0f);
            if (c.hasProperty ("readout"))
                checkArea (id + " readout", rectOf (c["readout"]), &knob, 2.0f);
            // the knob's centre is where the mockup has it
            const auto centre = pointOf (c["centre"]);
            const auto compCentre = ed.getLocalPoint (&knob, juce::Point<float>()) * -1.0f;
            juce::ignoreUnused (compCentre);
            const auto bounds = knob.getBoundsInParent().toFloat();
            const auto expected = refToEditor (centre.x, centre.y) - juce::Point<float> (0.0f, (float) ui::TleetEditorBase::toolbarHeight);
            CHECK_MSG (bounds.contains (expected), id + " centre outside its component");
            ++knobsChecked;
        }
        else if (type == "LAMP_LIST")
        {
            auto& group = ed.getLampGroup ("algorithmLamps");
            for (int i = 0; i < group.getNumLamps(); ++i)
            {
                checkArea ("lamp " + c["labels"][i].toString(), rectOf (c["hitboxes"][i]), &group.getLamp (i), 3.0f);
                ++lampsChecked;
            }
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
            checkArea ("FREEZE", rectOf (c["hitbox"]), &ed.getFreeze(), 4.0f);
        else if (type == "SPECTRUM")
            checkArea ("spectrum", rectOf (c["screen"]), &ed.getSpectrum(), 4.0f);
        else if (type == "BAR_METERS")
            checkArea ("meters", rectOf (c["hitbox"]), &ed.getMeters(), 3.0f);
        else if (type == "FILTER_GRAPH")
            checkArea ("filter graph", rectOf (c["plot"]), &ed.getFilterGraph(), 3.0f);
        else if (type == "LEVEL_BARS")
        {
            checkArea ("EARLY", rectOf (c["early"]["track"]), &ed.getLevelBar (0), 0.0f);
            checkArea ("LATE", rectOf (c["late"]["track"]), &ed.getLevelBar (1), 0.0f);
        }
        else if (type == "PRESET_BAR")
            for (auto* part : { "prev", "name", "next", "save", "delete" })
                checkArea (juce::String ("preset ") + part, rectOf (c[part]), &ed.getPresetBar(), 3.0f);
    }
    MEASURE ("knob click areas checked", knobsChecked);
    MEASURE ("lamp click areas checked", lampsChecked);
    CHECK (knobsChecked == 2 + 12 + 1);
    CHECK (lampsChecked == 5 + 3 + 3);

    for (size_t i = 0; i < areas.size(); ++i)
        for (size_t j = i + 1; j < areas.size(); ++j)
        {
            const auto a = areas[i].first.upToFirstOccurrenceOf (" ", false, false);
            const auto b = areas[j].first.upToFirstOccurrenceOf (" ", false, false);
            if (! a.equalsIgnoreCase (b))
                CHECK_MSG (! areas[i].second.intersects (areas[j].second), areas[i].first + " overlaps " + areas[j].first);
        }
}

TEST ("VOID GUI: every scale number and dot sits where the knob really has that value")
{
    auto p = makeVoidForGui();
    std::unique_ptr<juce::AudioProcessorEditor> base (p->createEditor());
    auto& ed = dynamic_cast<VoidEditor&> (*base);
    const auto map = VoidEditor::loadUiMap();

    float worstLabel = 0.0f, worstPointer = 0.0f, worstDot = 0.0f;
    int labels = 0, dots = 0;
    for (auto& c : *map["controls"].getArray())
    {
        if (! c["type"].toString().startsWith ("ROTARY"))
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
        const int expected = c["stepDots"].hasProperty ("count") ? (int) c["stepDots"]["count"] : param->getNumSteps();
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
    CHECK (labels == 24);
    CHECK (dots == 5 + 3 + 11 * 12);

    auto texts = [&] (const char* id)
    {
        juce::StringArray s;
        for (auto& l : ed.getScaleLabels (id)) s.add (l.text);
        return s.joinIntoString (" ");
    };
    CHECK_MSG (texts ("preDelay") == "0 250", texts ("preDelay"));
    CHECK_MSG (texts ("decay") == juce::String ("0.1 ") + juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x9e")), texts ("decay"));
    CHECK_MSG (texts ("modRate") == "0.01 5", texts ("modRate"));
    CHECK_MSG (texts ("width") == "0 200", texts ("width"));
    CHECK_MSG (texts ("hpf") == "20 1K", texts ("hpf"));
    CHECK_MSG (texts ("lpf") == "1K 20K", texts ("lpf"));
    CHECK_MSG (texts ("output") == "-12 +12", texts ("output"));
    for (auto* id : { "size", "diffusion", "modDepth", "duck", "mix" })
        CHECK_MSG (texts (id) == "0 100", texts (id));

    // the readout windows print the parameter's own text
    setPlain (*p, vp::decay, vp::decayMaxS);
    CHECK (ed.getKnob ("decay").getReadoutText() == juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x9e")));
    setPlain (*p, vp::decay, 8.4f);
    CHECK_MSG (ed.getKnob ("decay").getReadoutText() == "8.40 s", ed.getKnob ("decay").getReadoutText());
    setPlain (*p, vp::lpf, 8500.0f);
    CHECK_MSG (ed.getKnob ("lpf").getReadoutText() == "8.5 kHz", ed.getKnob ("lpf").getReadoutText());
    setPlain (*p, vp::preDelay, 42.0f);
    CHECK_MSG (ed.getKnob ("preDelay").getReadoutText() == "42 ms", ed.getKnob ("preDelay").getReadoutText());
}

TEST ("VOID GUI: every control drives its real parameter and follows automation")
{
    auto p = makeVoidForGui();
    p->setUiProperty ("scale", 1.0);
    std::unique_ptr<juce::AudioProcessorEditor> base (p->createEditor());
    auto& ed = dynamic_cast<VoidEditor&> (*base);
    ed.setVisible (true);
    const auto map = VoidEditor::loadUiMap();

    for (auto* id : continuousKnobs)
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

    // DROWN: the knob and the lit arc follow the parameter
    {
        auto& d = ed.getDrown();
        const auto& c = control (map, "drown");
        for (float v : { 0.0f, 25.0f, 100.0f })
        {
            setPlain (*p, vp::drown, v);
            const float expected = (float) c["travelDegrees"][0] + v / 100.0f * ((float) c["travelDegrees"][1] - (float) c["travelDegrees"][0]);
            CHECK_NEAR (d.getLitAngleDegrees(), expected, 0.01);
        }
        setPlain (*p, vp::drown, 0.0f);
        d.keyPressed (juce::KeyPress (juce::KeyPress::upKey));
        CHECK (plainOf (*p, vp::drown) > 0.0f);
    }

    // ALGORITHM: the lamps select; knob, lamps and parameter stay together
    {
        const auto& c = control (map, "algorithmLamps");
        auto& lamps = ed.getLampGroup ("algorithmLamps");
        for (int i = 0; i < vp::numAlgorithms; ++i)
        {
            clickAt (ed, rectOf (c["hitboxes"][i]).getCentre());
            CHECK_MSG (indexOf (*p, vp::algorithm) == i, "ALGORITHM lamp " + juce::String (i));
            for (int j = 0; j < vp::numAlgorithms; ++j)
                CHECK (lamps.getLamp (j).getToggleState() == (j == i));
            CHECK_NEAR (ed.getKnob ("algorithm").getNormalisedValue(), (float) i / (float) (vp::numAlgorithms - 1), 1e-5);
        }
        auto& k = ed.getKnob ("algorithm");
        k.setNormalisedFromUser (0.5f, true);
        CHECK (indexOf (*p, vp::algorithm) == 2 && lamps.getLamp (2).getToggleState());
    }

    // COLOR and FILTER MODE: a click selects; automation lights the right button
    for (auto* id : { "color", "filterMode" })
    {
        const auto& c = control (map, id);
        auto& group = ed.getLampGroup (id);
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
    // the COLOR knob is the same setting as the COLOR buttons
    setIndex (*p, vp::color, 2);
    CHECK_NEAR (ed.getKnob ("colorKnob").getNormalisedValue(), 1.0f, 1e-5);
    ed.getKnob ("colorKnob").setNormalisedFromUser (0.0f, true);
    CHECK (indexOf (*p, vp::color) == 0 && ed.getLampGroup ("color").getLamp (0).getToggleState());

    // FREEZE: a click toggles the parameter
    {
        const auto& c = control (map, "freeze");
        clickAt (ed, rectOf (c["face"]).getCentre());
        CHECK (plainOf (*p, vp::freeze) > 0.5f && ed.getFreeze().isOn());
        clickAt (ed, rectOf (c["face"]).getCentre());
        CHECK (plainOf (*p, vp::freeze) < 0.5f && ! ed.getFreeze().isOn());
    }

    // preset bar: previous / next step through the presets; the name shows the current one
    {
        const auto& c = control (map, "presetBar");
        const int count = (int) p->presets.getPresets().size();
        CHECK (count >= 14);
        p->loadPreset (0);
        ed.refresh (frame);
        CHECK_MSG (ed.getPresetBar().getText() == p->presets.getCurrentName(), ed.getPresetBar().getText());
        clickAt (ed, rectOf (c["next"]).getCentre());
        CHECK (p->presets.getCurrentIndex() == 1);
        ed.refresh (frame);
        CHECK (ed.getPresetBar().getText() == p->presets.getPresets()[1].name);
        clickAt (ed, rectOf (c["prev"]).getCentre());
        CHECK (p->presets.getCurrentIndex() == 0);
        // a factory preset cannot be deleted: the delete icon is dimmed and does nothing
        ed.refresh (frame);
        CHECK (! ed.getPresetBar().isDeleteEnabled());
        // an edit marks the preset as modified
        setPlain (*p, vp::mix, 77.0f);
        ed.refresh (frame);
        CHECK (ed.getPresetBar().getText().endsWith (" *"));
    }
}

TEST ("VOID GUI: lamps, displays and meters show the real DSP state")
{
    auto p = makeVoidForGui();
    p->setUiProperty ("scale", 1.0);
    std::unique_ptr<juce::AudioProcessorEditor> base (p->createEditor());
    auto& ed = dynamic_cast<VoidEditor&> (*base);
    ed.setVisible (true);
    const auto map = VoidEditor::loadUiMap();

    // IN / OUT: a sine at -6 dBFS peak reads -6 on the IN bars, at the printed -6 mark
    {
        setPlain (*p, vp::mix, 0.0f);
        const float peak = std::pow (10.0f, -6.0f / 20.0f);
        frames (*p, ed, 20, peak);
        for (int i = 0; i < 4; ++i)
        {
            MEASURE ("meter " + juce::String (i) + " at a -6 dBFS sine (dB)", ed.getMeters().getDisplayedDb (i));
            CHECK_NEAR (ed.getMeters().getDisplayedDb (i), -6.0f, 0.2);
        }
        const auto& c = control (map, "meters");
        const float y = ed.getMeters().yForDb (-6.0f) / VoidEditor::skinScale + rectOf (c["hitbox"]).getY();
        CHECK_NEAR (y, (float) c["scale"]["-6"], 0.01);
        // silence: the bars fall back
        frames (*p, ed, 60, 0.0f);
        CHECK (ed.getMeters().getDisplayedDb (0) < -48.0f);
    }

    // EARLY / LATE: they read the processor's own levels; a burst lights them, silence lets them fall
    {
        setPlain (*p, vp::mix, 50.0f);
        setPlain (*p, vp::decay, 3.0f);
        frames (*p, ed, 3, 0.3f, -1.0);
        const float early = ed.getLevelBar (0).getDisplayedDb(), late = ed.getLevelBar (1).getDisplayedDb();
        MEASURE ("EARLY after a noise burst (dBFS)", early);
        MEASURE ("LATE after a noise burst (dBFS)", late);
        CHECK (early > -40.0f);
        CHECK (late > -40.0f);
        frames (*p, ed, 150, 0.0f);
        CHECK (ed.getLevelBar (0).getDisplayedDb() < early - 20.0f);
        CHECK (ed.getLevelBar (0).litWidthFor (0.0f) > ed.getLevelBar (0).litWidthFor (-24.0f));
        CHECK (ed.getLevelBar (0).litWidthFor (-60.0f) <= 0.0f);
    }

    // FREEZE: the lamp is the real state (the parameter, which is what the DSP runs)
    {
        const auto& c = control (map, "freeze");
        setPlain (*p, vp::freeze, 1.0f);
        feed (*p, 0.0f, 0.02);
        CHECK (p->isFrozen() && ed.getFreeze().isOn());
        const float on = brightnessAt (snapshot (ed), refToEditor (rectOf (c["face"]).reduced (12.0f)));
        setPlain (*p, vp::freeze, 0.0f);
        feed (*p, 0.0f, 0.02);
        CHECK (! p->isFrozen() && ! ed.getFreeze().isOn());
        const float off = brightnessAt (snapshot (ed), refToEditor (rectOf (c["face"]).reduced (12.0f)));
        MEASURE ("FREEZE face lit / dark brightness", on / juce::jmax (1.0f, off));
        CHECK (on > off * 1.5f);
    }

    // ALGORITHM / COLOR / FILTER MODE lamps: the lit one is the selected one (pixels)
    {
        const auto& c = control (map, "algorithmLamps");
        auto lampAt = [&] (int i) { return juce::Rectangle<float> (8.0f, 8.0f).withCentre ({ (float) c["lampX"], (float) c["rows"][i] }); };
        setIndex (*p, vp::algorithm, 3);
        const auto img = snapshot (ed);
        const float lit = brightnessAt (img, refToEditor (lampAt (3)));
        const float dark = brightnessAt (img, refToEditor (lampAt (0)));
        MEASURE ("ALGORITHM lamp lit / dark brightness", lit / juce::jmax (1.0f, dark));
        CHECK (lit > dark * 2.0f);
    }

    // DROWN: the ring is lit up to the value and dark beyond it (pixels)
    {
        const auto& c = control (map, "drown");
        const auto centre = pointOf (c["centre"]);
        const float r = 0.5f * ((float) c["arc"]["inner"] + (float) c["arc"]["outer"]);
        auto arcAt = [&] (float deg) { return juce::Rectangle<float> (5.0f, 5.0f).withCentre (ui::skin::polar (centre, r, deg)); };
        setPlain (*p, vp::drown, 50.0f); // lit to 0 deg
        const auto img = snapshot (ed);
        const float litPart = brightnessAt (img, refToEditor (arcAt (-60.0f)));
        const float darkPart = brightnessAt (img, refToEditor (arcAt (60.0f)));
        MEASURE ("DROWN ring lit / dark brightness at 50 %", litPart / juce::jmax (1.0f, darkPart));
        CHECK (litPart > darkPart * 2.0f);
        setPlain (*p, vp::drown, 0.0f);
        const float none = brightnessAt (snapshot (ed), refToEditor (arcAt (-60.0f)));
        CHECK (none < litPart * 0.5f);
    }

    // filter graph: the curve is the processor's own response
    {
        setIndex (*p, vp::filterMode, 2);
        setPlain (*p, vp::hpf, 300.0f);
        setPlain (*p, vp::lpf, 5000.0f);
        ed.refresh (frame);
        auto& fg = ed.getFilterGraph();
        float worst = 0.0f;
        for (double hz : { 30.0, 100.0, 300.0, 1000.0, 3000.0, 5000.0, 12000.0 })
        {
            const float x = fg.xForFrequency ((float) hz);
            const float expectedY = fg.yForDb ((float) p->filterResponseDb (hz));
            // the curve's y at x
            float y = 0.0f, bestDx = 1e9f;
            juce::Path::Iterator it (fg.getCurve());
            while (it.next())
                if (std::abs (it.x1 - x) < bestDx) { bestDx = std::abs (it.x1 - x); y = it.y1; }
            worst = juce::jmax (worst, std::abs (y - expectedY));
        }
        MEASURE ("filter graph: worst curve vs response (px)", worst);
        CHECK (worst < 1.0f);
        CHECK (p->filterResponseDb (300.0) < -2.0 && p->filterResponseDb (300.0) > -4.5); // the corner
        CHECK (p->filterResponseDb (1500.0) > -0.6);
    }

    // spectrum: the reverb's spectrum (a 1 kHz sine, MIX 100) peaks at 1 kHz
    {
        setPlain (*p, vp::freeze, 0.0f);
        setPlain (*p, vp::mix, 100.0f);
        setPlain (*p, vp::hpf, 20.0f);
        setPlain (*p, vp::lpf, 20000.0f);
        auto& sp = ed.getSpectrum();
        sp.setQuality (ui::SpectrumDisplay::Quality::normal);
        for (int i = 0; i < 40; ++i)
        {
            feed (*p, 0.25f, frame, 1000.0);
            sp.processPending();
        }
        const float at1k = sp.getFrameDb (1000.0f), at100 = sp.getFrameDb (100.0f), at10k = sp.getFrameDb (10000.0f);
        MEASURE ("spectrum at 1 kHz / 100 Hz / 10 kHz (dB)", at1k);
        CHECK (at1k > at100 + 20.0f);
        CHECK (at1k > at10k + 20.0f);
    }
}

TEST ("VOID GUI: no dead controls: moving each control from the editor changes the sound")
{
    struct Case { const char* id; float from, to; };
    const Case cases[] = {
        { vp::algorithm, 0, 3 }, { vp::color, 0, 2 }, { vp::preDelay, 0, 120 }, { vp::size, 20, 90 },
        { vp::decay, 0.6f, 6.0f }, { vp::diffusion, 10, 100 }, { vp::drown, 0, 80 }, { vp::modRate, 0.1f, 4.0f },
        { vp::modDepth, 0, 100 }, { vp::width, 0, 200 }, { vp::hpf, 20, 600 }, { vp::lpf, 20000, 2000 },
        { vp::filterMode, 0, 2 }, { vp::duck, 0, 100 }, { vp::mix, 35, 90 }, { vp::freeze, 0, 1 }, { vp::output, 0, -6 },
    };
    for (auto& cs : cases)
    {
        std::array<std::vector<float>, 2> out;
        for (int k = 0; k < 2; ++k)
        {
            auto q = makeVoidForGui();
            std::unique_ptr<juce::AudioProcessorEditor> b (q->createEditor());
            auto& e = dynamic_cast<VoidEditor&> (*b);
            const auto id = juce::String (cs.id);
            setPlain (*q, vp::mix, 60.0f);
            setPlain (*q, vp::modDepth, id == vp::modRate ? 100.0f : 30.0f);
            const float v = k == 0 ? cs.from : cs.to;
            auto* param = q->apvts.getParameter (cs.id);
            if (id == vp::freeze || id == vp::filterMode)
            {
                if (id == vp::filterMode) { setPlain (*q, vp::hpf, 300.0f); setPlain (*q, vp::lpf, 3000.0f); }
                setPlain (*q, cs.id, v);
            }
            else
            {
                const auto knobId = id == vp::color ? juce::String ("colorKnob") : id;
                e.getKnob (knobId).setNormalisedFromUser (param->convertTo0to1 (v), true);
            }
            CHECK_NEAR (param->convertFrom0to1 (param->getValue()), v, 1e-2 * juce::jmax (1.0f, std::abs (v)));
            out[(size_t) k] = render (*q);
        }
        const double diff = rmsDifferenceDb (out[0], out[1]);
        MEASURE (juce::String (cs.id) + ": change of the output (dB rel. signal)", diff);
        CHECK_MSG (diff > -40.0, juce::String (cs.id) + " changed the sound by only " + juce::String (diff, 1) + " dB");
    }
}
