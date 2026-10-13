#include "TestFramework.h"
#include "../plugins/SHAPE/PluginProcessor.h"
#include "../plugins/SHAPE/PluginEditor.h"
#include "../plugins/SHAPE/Parameters.h"

// GUI tests for SHAPE (plugins/SHAPE/GUI_TEST_PLAN.md). Every control is
// checked against UI_MAP.json and against the real parameter / DSP state.

using namespace tleet;
using namespace tleet::test;
using tleet::shape::ShapeProcessor;
using tleet::shape::ShapeEditor;
namespace sp = tleet::shape::params;

namespace
{
    constexpr double sr = 48000.0;
    constexpr int block = 256;

    std::unique_ptr<ShapeProcessor> makeShapeForGui()
    {
        auto p = std::make_unique<ShapeProcessor>();
        p->setRateAndBufferSizeDetails (sr, block);
        p->prepareToPlay (sr, block);
        return p;
    }

    void setPlain (ShapeProcessor& p, const char* id, float plain)
    {
        auto* r = p.apvts.getParameter (id);
        r->setValueNotifyingHost (r->convertTo0to1 (plain));
    }

    void setIndex (ShapeProcessor& p, const char* id, int index) { setPlain (p, id, (float) index); }

    // A fixed test signal (several sines across the band plus a little
    // noise) through the plugin; the output of the last half, left channel.
    std::vector<float> render (ShapeProcessor& p, double seconds = 0.4)
    {
        juce::AudioBuffer<float> buf (2, block);
        juce::MidiBuffer midi;
        juce::Random rng (7);
        std::vector<float> out;
        const int blocks = (int) (seconds * sr / block);
        long n = 0;
        for (int b = 0; b < blocks; ++b)
        {
            for (int i = 0; i < block; ++i, ++n)
            {
                const double t = (double) n / sr;
                float s = 0.0f;
                for (double hz : { 40.0, 90.0, 250.0, 1000.0, 3500.0, 9000.0, 15000.0 })
                    s += 0.04f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * hz * t);
                s += 0.01f * (rng.nextFloat() * 2.0f - 1.0f);
                buf.setSample (0, i, s);
                buf.setSample (1, i, s);
            }
            p.processBlock (buf, midi);
            if (b >= blocks / 2)
                out.insert (out.end(), buf.getReadPointer (0), buf.getReadPointer (0) + block);
        }
        return out;
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
        return { x * ShapeEditor::skinScale, ui::TleetEditorBase::toolbarHeight + y * ShapeEditor::skinScale };
    }

    juce::Rectangle<float> refToEditor (juce::Rectangle<float> r)
    {
        const auto tl = refToEditor (r.getX(), r.getY());
        return { tl.x, tl.y, r.getWidth() * ShapeEditor::skinScale, r.getHeight() * ShapeEditor::skinScale };
    }

    juce::Rectangle<float> rectOf (const juce::var& v) { return { (float) v[0], (float) v[1], (float) v[2], (float) v[3] }; }

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

    const char* const knobIds[] = { sp::lfFreq, sp::lfBoost, sp::lfAtten, sp::hfFreq, sp::hfBoost, sp::hfBandwidth,
                                    sp::hfAttenFreq, sp::hfAtten, sp::drive, sp::character, sp::mix, sp::output };
}

TEST ("SHAPE GUI: editor opens at every scale; screenshots of default and representative states")
{
    auto p = makeShapeForGui();
    for (float scale : { 0.75f, 1.0f, 1.5f, 2.0f })
    {
        p->setUiProperty ("scale", scale);
        std::unique_ptr<juce::AudioProcessorEditor> ed (p->createEditor());
        CHECK (dynamic_cast<ShapeEditor*> (ed.get()) != nullptr);
        const int expectedW = juce::roundToInt (ShapeEditor::baseWidth * scale);
        CHECK_MSG (std::abs (ed->getWidth() - expectedW) <= 1, "width at scale " + juce::String (scale));
        saveSnapshot (*ed, "SHAPE_editor_" + juce::String (juce::roundToInt (scale * 100)));
    }

    p->setUiProperty ("scale", 1.0);
    std::unique_ptr<juce::AudioProcessorEditor> base (p->createEditor());
    auto& ed = dynamic_cast<ShapeEditor&> (*base);
    struct State { const char* name; std::function<void()> apply; };
    const State states[] = {
        { "low_trick", [&] { setIndex (*p, sp::lfFreq, 2); setPlain (*p, sp::lfBoost, 6.0f); setPlain (*p, sp::lfAtten, 4.0f); } },
        { "air_sharp", [&] { setIndex (*p, sp::hfFreq, 5); setPlain (*p, sp::hfBoost, 7.0f); setPlain (*p, sp::hfBandwidth, 1.5f);
                             setIndex (*p, sp::hfAttenFreq, 0); setPlain (*p, sp::hfAtten, 3.0f); } },
        { "burn_eq_out", [&] { setIndex (*p, sp::character, 2); setPlain (*p, sp::drive, 85.0f); setPlain (*p, sp::mix, 60.0f);
                               setPlain (*p, sp::output, -6.0f); setPlain (*p, sp::eqIn, 0.0f); setIndex (*p, sp::lfFreq, 0);
                               setIndex (*p, sp::hfFreq, 0); setPlain (*p, sp::hfBandwidth, 10.0f); } },
    };
    for (auto& st : states)
    {
        st.apply();
        saveSnapshot (ed, juce::String ("SHAPE_state_") + st.name);
    }
}

TEST ("SHAPE GUI: every mapped control sits at its mockup position and owns its click area")
{
    auto p = makeShapeForGui();
    p->setUiProperty ("scale", 1.0);
    std::unique_ptr<juce::AudioProcessorEditor> base (p->createEditor());
    auto& ed = dynamic_cast<ShapeEditor&> (*base);
    ed.setVisible (true);
    const auto map = ShapeEditor::loadUiMap();

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

    int knobsChecked = 0;
    for (auto& c : *map["controls"].getArray())
    {
        const auto type = c["type"].toString();
        if (type.startsWith ("ROTARY"))
        {
            const auto hb = rectOf (c["hitbox"]);
            auto& knob = ed.getKnob (c["parameter"].toString());
            checkArea (c["id"].toString(), hb, &knob, 3.0f);
            // the pointer turns around the knob's centre in the map
            const juce::Point<float> centreRef ((float) c["centre"][0], (float) c["centre"][1]);
            const auto actual = knob.getBounds().getPosition().toFloat() + (centreRef - hb.getPosition()) * ShapeEditor::skinScale;
            CHECK (actual.getDistanceFrom (refToEditor (centreRef.x, centreRef.y)) <= 1.0f);
            ++knobsChecked;
        }
        else if (type == "LAMP_SWITCH")
        {
            // the lamp's bezel takes the click
            checkArea ("EQ IN", rectOf (c["lamp"]["rect"]), &ed.getEqInLamp(), 8.0f);
        }
    }
    MEASURE ("knob click areas checked", knobsChecked);
    CHECK (knobsChecked == 12);

    for (size_t i = 0; i < areas.size(); ++i)
        for (size_t j = i + 1; j < areas.size(); ++j)
            CHECK_MSG (! areas[i].second.intersects (areas[j].second), areas[i].first + " overlaps " + areas[j].first);
}

TEST ("SHAPE GUI: every scale number and dot sits where the knob really has that value")
{
    auto p = makeShapeForGui();
    std::unique_ptr<juce::AudioProcessorEditor> base (p->createEditor());
    auto& ed = dynamic_cast<ShapeEditor&> (*base);
    const auto map = ShapeEditor::loadUiMap();

    float worstLabel = 0.0f, worstPointer = 0.0f, worstDot = 0.0f;
    int labels = 0, dots = 0;
    for (auto& c : *map["controls"].getArray())
    {
        if (! c["type"].toString().startsWith ("ROTARY"))
            continue;
        const auto id = c["parameter"].toString();
        auto* param = p->apvts.getParameter (id);
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
            const float r = (float) c["dotRadius"];
            CHECK_MSG (! l.box.intersects (juce::Rectangle<float> (2.0f * r, 2.0f * r).withCentre (sc).reduced (r * 0.3f)),
                       id + " '" + l.text + "' inside the dot circle");
            const float pointer = pointerAt (l.value);
            worstPointer = juce::jmax (worstPointer, std::abs (pointer - l.angleDegrees));
            CHECK_MSG (std::abs (pointer - l.angleDegrees) <= 0.05f, id + " '" + l.text + "' pointer " + juce::String (pointer, 3));
            ++labels;
        }

        // live dots: one per real position of a selector, one per unit of BANDWIDTH
        const auto& ds = ed.getStepDots (id);
        if (c.hasProperty ("stepDots"))
        {
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
        else
            CHECK (ds.empty());
    }
    MEASURE ("labels checked", labels);
    MEASURE ("live dots checked", dots);
    MEASURE ("worst label angle error (deg)", worstLabel);
    MEASURE ("worst pointer-vs-label error (deg)", worstPointer);
    MEASURE ("worst pointer-vs-dot error (deg)", worstDot);
    CHECK (dots == 4 + 7 + 3 + 11);

    auto texts = [&] (const char* id)
    {
        juce::StringArray s;
        for (auto& l : ed.getScaleLabels (id)) s.add (l.text);
        return s.joinIntoString (" ");
    };
    CHECK_MSG (texts (sp::lfFreq) == "20 30 60 100", texts (sp::lfFreq));
    CHECK_MSG (texts (sp::hfFreq) == "3 4 5 8 10 12 16", texts (sp::hfFreq));
    CHECK_MSG (texts (sp::hfAttenFreq) == "5 10 20", texts (sp::hfAttenFreq));
    for (auto* id : { sp::lfBoost, sp::lfAtten, sp::hfBoost, sp::hfAtten })
        CHECK_MSG (texts (id) == "0 1 2 3 4 5 6 7 8 9 10", texts (id));
    CHECK_MSG (texts (sp::hfBandwidth) == "1 2 3 4 5 6 7 8 9", texts (sp::hfBandwidth));
    CHECK_MSG (texts (sp::drive) == "0 50 100", texts (sp::drive));
    CHECK_MSG (texts (sp::mix) == "0 50 100", texts (sp::mix));
    CHECK_MSG (texts (sp::output) == "-20 0 +20", texts (sp::output));
    CHECK (texts (sp::character).isEmpty()); // CLEAN / ORIGINAL / BURN stay printed

    // CHARACTER points at the printed CLEAN and BURN marks and straight up at ORIGINAL
    {
        const auto& c = control (map, sp::character);
        const juce::Point<float> centre ((float) c["centre"][0], (float) c["centre"][1]);
        auto& knob = ed.getKnob (sp::character);
        const float t0 = (float) c["travelDegrees"][0], t1 = (float) c["travelDegrees"][1];
        const juce::Point<float> marks[] = { { 480.0f, 895.5f }, { centre.x, 863.0f }, { 603.75f, 895.5f } };
        for (int i = 0; i < 3; ++i)
        {
            setIndex (*p, sp::character, i);
            const float pointer = t0 + knob.getNormalisedValue() * (t1 - t0);
            const float mark = juce::radiansToDegrees (std::atan2 (marks[i].x - centre.x, -(marks[i].y - centre.y)));
            MEASURE ("CHARACTER position " + juce::String (i) + ": pointer vs printed mark (deg)", pointer - mark);
            CHECK (std::abs (pointer - mark) <= 1.5f);
        }
    }
}

TEST ("SHAPE GUI: every knob and EQ IN drive the real parameters, follow automation and change the sound")
{
    auto p = makeShapeForGui();
    p->setUiProperty ("scale", 1.0);
    std::unique_ptr<juce::AudioProcessorEditor> base (p->createEditor());
    auto& ed = dynamic_cast<ShapeEditor&> (*base);
    ed.setVisible (true);
    const auto map = ShapeEditor::loadUiMap();

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

    // EQ IN: a click on the lamp toggles the parameter; lit only while on
    {
        const auto& c = control (map, sp::eqIn);
        const auto lamp = rectOf (c["lamp"]["rect"]);
        auto* raw = p->apvts.getRawParameterValue (sp::eqIn);
        setPlain (*p, sp::eqIn, 1.0f);
        CHECK (ed.getEqInLamp().getToggleState());
        const float on = brightnessAt (snapshot (ed), refToEditor (lamp.reduced (22.0f)));
        click (ed.getEqInLamp(), ed, lamp.getCentre());
        CHECK (raw->load() < 0.5f);
        CHECK (! ed.getEqInLamp().getToggleState());
        const float off = brightnessAt (snapshot (ed), refToEditor (lamp.reduced (22.0f)));
        MEASURE ("EQ IN lamp lit/dark brightness", on / juce::jmax (1.0f, off));
        CHECK (on > off * 2.0f);
        click (ed.getEqInLamp(), ed, lamp.getCentre());
        CHECK (raw->load() > 0.5f);
    }

    // no dead controls: moving each one from the editor changes the audio.
    // Each control is tested from a setting where it has something to act on
    // (a boost or cut engaged, so the frequency selectors and BANDWIDTH matter).
    {
        struct Case { const char* id; float from, to; };
        const Case cases[] = {
            { sp::lfFreq, 0, 3 }, { sp::lfBoost, 0, 6 }, { sp::lfAtten, 0, 6 }, { sp::hfFreq, 0, 6 }, { sp::hfBoost, 0, 6 },
            { sp::hfBandwidth, 0, 10 }, { sp::hfAttenFreq, 0, 2 }, { sp::hfAtten, 0, 6 }, { sp::drive, 0, 100 },
            { sp::character, 0, 2 }, { sp::mix, 100, 30 }, { sp::output, 0, -6 }, { sp::eqIn, 1, 0 },
        };
        for (auto& cs : cases)
        {
            std::array<std::vector<float>, 2> out;
            for (int k = 0; k < 2; ++k)
            {
                auto q = makeShapeForGui();
                std::unique_ptr<juce::AudioProcessorEditor> b (q->createEditor());
                auto& e = dynamic_cast<ShapeEditor&> (*b);
                setPlain (*q, sp::lfBoost, 4.0f);
                setPlain (*q, sp::lfAtten, 3.0f);
                setPlain (*q, sp::hfBoost, 5.0f);
                setPlain (*q, sp::hfAtten, 4.0f);
                setPlain (*q, sp::drive, 50.0f);
                const float v = k == 0 ? cs.from : cs.to;
                if (juce::String (cs.id) == sp::eqIn)
                {
                    setPlain (*q, sp::eqIn, 1.0f);
                    if (v < 0.5f)
                        click (e.getEqInLamp(), e, rectOf (control (map, sp::eqIn)["lamp"]["rect"]).getCentre());
                }
                else
                {
                    auto* param = q->apvts.getParameter (cs.id);
                    e.getKnob (cs.id).setNormalisedFromUser (param->convertTo0to1 (v), true);
                    CHECK_NEAR (param->convertFrom0to1 (param->getValue()), v, 1e-3);
                }
                out[(size_t) k] = render (*q);
            }
            const double diff = rmsDifferenceDb (out[0], out[1]);
            MEASURE (juce::String (cs.id) + ": change of the output (dB rel. signal)", diff);
            CHECK_MSG (diff > -40.0, juce::String (cs.id) + " changed the sound by only " + juce::String (diff, 1) + " dB");
        }
    }
}
