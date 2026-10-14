#include "TestFramework.h"
#include "../plugins/WEIGHT/PluginProcessor.h"
#include "../plugins/WEIGHT/PluginEditor.h"
#include "../plugins/WEIGHT/Parameters.h"

// GUI tests for WEIGHT (plugins/WEIGHT/GUI_TEST_PLAN.md). Every control is
// checked against UI_MAP.json and against the real parameter / DSP state.

using namespace tleet;
using namespace tleet::test;
using tleet::weight::WeightProcessor;
using tleet::weight::WeightEditor;
namespace wp = tleet::weight::params;

namespace
{
    constexpr double sr = 48000.0;
    constexpr int block = 256;

    std::unique_ptr<WeightProcessor> makeWeightForGui (bool sidechain = false)
    {
        auto p = std::make_unique<WeightProcessor>();
        if (sidechain)
        {
            auto layout = p->getBusesLayout();
            layout.inputBuses.getReference (1) = juce::AudioChannelSet::stereo();
            p->setBusesLayout (layout);
        }
        p->setRateAndBufferSizeDetails (sr, block);
        p->prepareToPlay (sr, block);
        return p;
    }

    void setPlain (WeightProcessor& p, const char* id, float plain)
    {
        auto* r = p.apvts.getParameter (id);
        r->setValueNotifyingHost (r->convertTo0to1 (plain));
    }

    void setIndex (WeightProcessor& p, const char* id, int index) { setPlain (p, id, (float) index); }

    struct Levels { double inL, inR, outL, outR; };

    double sinePhase = 0.0; // continues across feed() calls, so blocks join without clicks

    // Steady 1 kHz sines (left / right peaks, optional key on the sidechain
    // bus) through the plugin; RMS of each channel over the last half.
    Levels feed (WeightProcessor& p, float peakL, float peakR, double seconds, float keyPeak = 0.0f)
    {
        const int channels = p.getTotalNumInputChannels();
        juce::AudioBuffer<float> buf (juce::jmax (2, channels), block);
        juce::MidiBuffer midi;
        auto& phase = sinePhase;
        const int blocks = juce::jmax (1, (int) (seconds * sr / block));
        double iL = 0, iR = 0, oL = 0, oR = 0; long n = 0;
        for (int b = 0; b < blocks; ++b)
        {
            buf.clear();
            double bl = 0, br = 0;
            for (int i = 0; i < block; ++i)
            {
                const float s = (float) std::sin (phase);
                phase += 2.0 * juce::MathConstants<double>::pi * 1000.0 / sr;
                buf.setSample (0, i, peakL * s);
                buf.setSample (1, i, peakR * s);
                for (int ch = 2; ch < channels; ++ch)
                    buf.setSample (ch, i, keyPeak * s);
                bl += (double) peakL * s * peakL * s;
                br += (double) peakR * s * peakR * s;
            }
            p.processBlock (buf, midi);
            if (b >= blocks / 2)
            {
                iL += bl; iR += br;
                for (int i = 0; i < block; ++i)
                {
                    oL += (double) buf.getSample (0, i) * buf.getSample (0, i);
                    oR += (double) buf.getSample (1, i) * buf.getSample (1, i);
                }
                n += block;
            }
        }
        const double k = 1.0 / (double) juce::jmax (1L, n);
        return { std::sqrt (iL * k), std::sqrt (iR * k), std::sqrt (oL * k), std::sqrt (oR * k) };
    }

    juce::Point<float> refToEditor (float x, float y)
    {
        return { x * WeightEditor::skinScale, ui::TleetEditorBase::toolbarHeight + y * WeightEditor::skinScale };
    }

    juce::Rectangle<float> refToEditor (juce::Rectangle<float> r)
    {
        const auto tl = refToEditor (r.getX(), r.getY());
        return { tl.x, tl.y, r.getWidth() * WeightEditor::skinScale, r.getHeight() * WeightEditor::skinScale };
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

    void saveSnapshot (juce::Component& ed, const juce::String& name)
    {
        auto img = ed.createComponentSnapshot (ed.getLocalBounds(), true, 1.0f);
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

    // A fixed, documented test setting: fast feed-forward, 4:1, clean, linked, no blend, no trim.
    void neutral (WeightProcessor& p)
    {
        setIndex (p, wp::attack, (int) wp::Attack::ms1);
        setIndex (p, wp::release, (int) wp::Release::ms100);
        setIndex (p, wp::ratio, (int) wp::Ratio::r4);
        setIndex (p, wp::thrust, (int) wp::Thrust::clean);
        setIndex (p, wp::detector, (int) wp::Detector::feedForward);
        setIndex (p, wp::tone, (int) wp::Tone::neutral);
        setIndex (p, wp::grMode, (int) wp::GrMode::rms);
        setPlain (p, wp::scHpf, 20.0f);
        setPlain (p, wp::makeup, 0.0f);
        setPlain (p, wp::mix, 100.0f);
        setPlain (p, wp::output, 0.0f);
        setPlain (p, wp::link, 100.0f);
    }

    // Lets both meters settle on a steady signal (pull + ballistics as the UI timer does).
    void settle (WeightProcessor& p, WeightEditor& ed, float peakL, float peakR, int frames = 40)
    {
        for (int i = 0; i < frames; ++i)
        {
            feed (p, peakL, peakR, 1.0 / 30.0);
            ed.getMeter (0).refresh (1.0f / 30.0f);
            ed.getMeter (1).refresh (1.0f / 30.0f);
        }
    }

    const char* const knobIds[] = { wp::threshold, wp::attack, wp::release, wp::ratio, wp::makeup,
                                    wp::scHpf, wp::mix, wp::output, wp::link };
    const char* const groupIds[] = { wp::thrust, wp::detector, wp::tone, wp::grMode };
}

TEST ("WEIGHT GUI: editor opens at every scale; screenshots of default and representative states")
{
    auto p = makeWeightForGui();
    for (float scale : { 0.75f, 1.0f, 1.5f, 2.0f })
    {
        p->setUiProperty ("scale", scale);
        std::unique_ptr<juce::AudioProcessorEditor> ed (p->createEditor());
        CHECK (dynamic_cast<WeightEditor*> (ed.get()) != nullptr);
        const int expectedW = juce::roundToInt (WeightEditor::baseWidth * scale);
        CHECK_MSG (std::abs (ed->getWidth() - expectedW) <= 1, "width at scale " + juce::String (scale));
        saveSnapshot (*ed, "WEIGHT_editor_" + juce::String (juce::roundToInt (scale * 100)));
    }

    p->setUiProperty ("scale", 1.0);
    std::unique_ptr<juce::AudioProcessorEditor> base (p->createEditor());
    auto& ed = dynamic_cast<WeightEditor&> (*base);
    neutral (*p);
    struct State { const char* name; float peakL, peakR; std::function<void()> apply; };
    const State states[] = {
        { "bus_glue",     0.5f, 0.5f, [&] { setPlain (*p, wp::threshold, -10.0f); setIndex (*p, wp::ratio, (int) wp::Ratio::r2);
                                            setIndex (*p, wp::thrust, (int) wp::Thrust::neutral); setPlain (*p, wp::makeup, 3.0f); } },
        { "unlinked_left", 0.6f, 0.05f, [&] { setPlain (*p, wp::threshold, -20.0f); setIndex (*p, wp::ratio, (int) wp::Ratio::r6);
                                              setPlain (*p, wp::link, 0.0f); setIndex (*p, wp::detector, (int) wp::Detector::feedBack);
                                              setIndex (*p, wp::tone, (int) wp::Tone::old); setIndex (*p, wp::grMode, (int) wp::GrMode::peak); } },
        { "smashed",      0.9f, 0.9f, [&] { neutral (*p); setPlain (*p, wp::threshold, -20.0f); setIndex (*p, wp::ratio, (int) wp::Ratio::rInf);
                                            setIndex (*p, wp::thrust, (int) wp::Thrust::hard); setIndex (*p, wp::tone, (int) wp::Tone::modern);
                                            setPlain (*p, wp::makeup, 12.0f); setPlain (*p, wp::mix, 60.0f); setPlain (*p, wp::scHpf, 120.0f);
                                            setIndex (*p, wp::grMode, (int) wp::GrMode::average); } },
    };
    for (auto& st : states)
    {
        st.apply();
        settle (*p, ed, st.peakL, st.peakR);
        MEASURE (juce::String (st.name) + ": shown GR left (dB)", ed.getMeter (0).getDisplayedDb());
        MEASURE (juce::String (st.name) + ": shown GR right (dB)", ed.getMeter (1).getDisplayedDb());
        saveSnapshot (ed, juce::String ("WEIGHT_state_") + st.name);
    }
}

TEST ("WEIGHT GUI: every mapped control sits at its mockup position and owns its click area")
{
    auto p = makeWeightForGui();
    p->setUiProperty ("scale", 1.0);
    std::unique_ptr<juce::AudioProcessorEditor> base (p->createEditor());
    auto& ed = dynamic_cast<WeightEditor&> (*base);
    ed.setVisible (true);
    const auto map = WeightEditor::loadUiMap();

    std::vector<std::pair<juce::String, juce::Rectangle<float>>> areas;
    auto checkArea = [&] (const juce::String& name, juce::Rectangle<float> hb, juce::Component* target)
    {
        areas.push_back ({ name, hb });
        for (auto pt : { hb.getCentre(), hb.getTopLeft() + juce::Point<float> (3, 3), hb.getBottomRight() - juce::Point<float> (3, 3),
                         hb.getTopRight() + juce::Point<float> (-3, 3), hb.getBottomLeft() + juce::Point<float> (3, -3) })
        {
            auto* hit = ed.getComponentAt (refToEditor (pt.x, pt.y).toInt());
            CHECK_MSG (hit == target, name + " at " + pt.toString() + " hit " + (hit ? hit->getName() + " " + hit->getBounds().toString() : juce::String ("null")));
        }
    };

    int knobsChecked = 0, lampsChecked = 0;
    for (auto& c : *map["controls"].getArray())
    {
        const auto type = c["type"].toString();
        if (type == "ROTARY" || type == "ROTARY_STEPPED")
        {
            const auto hb = rectOf (c["hitbox"]);
            auto& knob = ed.getKnob (c["parameter"].toString());
            checkArea (c["id"].toString(), hb, &knob);
            // the pointer turns around the centre of the printed dot circle
            const juce::Point<float> centreRef ((float) c["centre"][0], (float) c["centre"][1]);
            const auto actual = knob.getBounds().getPosition().toFloat() + (centreRef - hb.getPosition()) * WeightEditor::skinScale;
            CHECK (actual.getDistanceFrom (refToEditor (centreRef.x, centreRef.y)) <= 1.0f);
            ++knobsChecked;
        }
        else if (type == "LAMP_GROUP")
        {
            auto& group = ed.getLampGroup (c["parameter"].toString());
            const auto& buttons = *c["buttons"].getArray();
            CHECK (group.getNumLamps() == buttons.size());
            for (int i = 0; i < buttons.size(); ++i)
            {
                checkArea (c["id"].toString() + " " + buttons[i]["label"].toString(), rectOf (buttons[i]["hitbox"]), &group.getLamp (i));
                ++lampsChecked;
            }
        }
        else if (type == "LAMP_SWITCH")
        {
            checkArea (c["id"].toString(), rectOf (c["hitbox"]), &ed.getSidechainLamp());
            ++lampsChecked;
        }
    }
    MEASURE ("knob hitboxes checked", knobsChecked);
    MEASURE ("lamp click areas checked", lampsChecked);
    CHECK (knobsChecked == 9);
    CHECK (lampsChecked == 12);

    for (size_t i = 0; i < areas.size(); ++i)
        for (size_t j = i + 1; j < areas.size(); ++j)
            CHECK_MSG (! areas[i].second.intersects (areas[j].second), areas[i].first + " overlaps " + areas[j].first);

    // the meters never take clicks
    for (auto* id : { "grMeterL", "grMeterR" })
    {
        const auto mb = rectOf (control (map, id)["hitbox"]);
        auto* hit = ed.getComponentAt (refToEditor (mb.getCentreX(), mb.getCentreY()).toInt());
        CHECK (hit != &ed.getMeter (0) && hit != &ed.getMeter (1));
    }
}

TEST ("WEIGHT GUI: every scale number and step dot sits where the knob really has that value")
{
    auto p = makeWeightForGui();
    std::unique_ptr<juce::AudioProcessorEditor> base (p->createEditor());
    auto& ed = dynamic_cast<WeightEditor&> (*base);
    const auto map = WeightEditor::loadUiMap();

    float worstLabel = 0.0f, worstPointer = 0.0f, worstDot = 0.0f;
    int labels = 0, dots = 0;
    for (auto& c : *map["controls"].getArray())
    {
        if (! c.hasProperty ("scaleLabels"))
            continue;
        const auto id = c["parameter"].toString();
        auto* param = p->apvts.getParameter (id);
        auto& knob = ed.getKnob (id);
        const juce::Point<float> sc ((float) c["scaleCentre"][0], (float) c["scaleCentre"][1]);
        const juce::Point<float> pivot ((float) c["centre"][0], (float) c["centre"][1]);
        CHECK (sc.getDistanceFrom (pivot) < 0.01f); // labels, dots and pointer share one centre
        const float t0 = (float) c["travelDegrees"][0], t1 = (float) c["travelDegrees"][1];
        const auto& list = ed.getScaleLabels (id);
        CHECK (! list.empty());

        for (auto& l : list)
        {
            // the label is printed on the ray of its value ...
            const auto mid = l.box.getCentre().translated (0.0f, -l.box.getHeight() * 0.07f);
            const float printed = juce::radiansToDegrees (std::atan2 (mid.x - sc.x, -(mid.y - sc.y)));
            const float err = std::abs (printed - l.angleDegrees);
            worstLabel = juce::jmax (worstLabel, err);
            CHECK_MSG (err <= 1.0f, id + " '" + l.text + "' printed at " + juce::String (printed, 2) + " deg, value at " + juce::String (l.angleDegrees, 2));
            // ... clear of the dot circle ...
            CHECK_MSG (! l.box.intersects (juce::Rectangle<float> (2.0f * (float) c["dotRadius"], 2.0f * (float) c["dotRadius"]).withCentre (sc).reduced ((float) c["dotRadius"] * 0.3f)),
                       id + " '" + l.text + "' inside the dot circle");

            // ... and the knob turned to that value points exactly there
            param->setValueNotifyingHost (param->convertTo0to1 (l.value));
            const float pointer = t0 + knob.getNormalisedValue() * (t1 - t0);
            worstPointer = juce::jmax (worstPointer, std::abs (pointer - l.angleDegrees));
            CHECK_MSG (std::abs (pointer - l.angleDegrees) <= 0.05f, id + " '" + l.text + "' pointer " + juce::String (pointer, 3));
            ++labels;
        }

        // stepped knobs: one dot per real position, at the pointer's angle there
        const auto& stepDots = ed.getStepDots (id);
        if (c.hasProperty ("stepDots"))
        {
            CHECK_MSG ((int) stepDots.size() == param->getNumSteps(), id + " dots " + juce::String ((int) stepDots.size()));
            for (int i = 0; i < (int) stepDots.size(); ++i)
            {
                param->setValueNotifyingHost (param->convertTo0to1 ((float) i));
                const float pointer = t0 + knob.getNormalisedValue() * (t1 - t0);
                const auto& d = stepDots[(size_t) i];
                const float at = juce::radiansToDegrees (std::atan2 (d.centre.x - sc.x, -(d.centre.y - sc.y)));
                worstDot = juce::jmax (worstDot, std::abs (at - pointer));
                CHECK_MSG (std::abs (at - pointer) <= 0.05f, id + " dot " + juce::String (i));
                CHECK_NEAR (d.centre.getDistanceFrom (sc), (float) c["dotRadius"], 1e-3);
                ++dots;
            }
        }
        else
            CHECK (stepDots.empty());
    }
    MEASURE ("labels checked", labels);
    MEASURE ("step dots checked", dots);
    MEASURE ("worst label angle error (deg)", worstLabel);
    MEASURE ("worst pointer-vs-label error (deg)", worstPointer);
    MEASURE ("worst pointer-vs-dot error (deg)", worstDot);
    CHECK (dots == 7 + 6 + 7);

    auto texts = [&] (const char* id)
    {
        juce::StringArray s;
        for (auto& l : ed.getScaleLabels (id)) s.add (l.text);
        return s.joinIntoString (" ");
    };
    CHECK_MSG (texts (wp::threshold) == "-20 -10 0 +10", texts (wp::threshold));
    CHECK_MSG (texts (wp::attack) == ".03 .1 .3 1 3 10 30", texts (wp::attack));
    CHECK_MSG (texts (wp::release) == ".05 .1 .2 .5 1 2", texts (wp::release));
    CHECK_MSG (texts (wp::ratio) == juce::String::fromUTF8 ("1.5 2 3 4 6 10 \xe2\x88\x9e"), texts (wp::ratio));
    CHECK_MSG (texts (wp::makeup) == "-12 -6 0 +6 +12 +24", texts (wp::makeup));
    CHECK_MSG (texts (wp::scHpf) == "20 60 120 240", texts (wp::scHpf));
    CHECK_MSG (texts (wp::mix) == "0 50 100", texts (wp::mix));
    CHECK_MSG (texts (wp::output) == "-12 -6 0 +6 +12", texts (wp::output));
    CHECK_MSG (texts (wp::link) == "0 50 100", texts (wp::link));

    // stepped labels are the parameter's own step texts, without the unit printed under the knob
    for (auto* id : { wp::attack, wp::release, wp::ratio })
    {
        auto* param = p->apvts.getParameter (id);
        for (auto& l : ed.getScaleLabels (id))
        {
            const auto shown = param->getText (param->convertTo0to1 (l.value), 32);
            const auto expected = l.text.startsWith (".") ? "0" + l.text : l.text;
            CHECK_MSG (shown.upToFirstOccurrenceOf (" ", false, false) == expected, juce::String (id) + " label '" + l.text + "' vs '" + shown + "'");
        }
    }
}

TEST ("WEIGHT GUI: knobs and lamps drive the real parameters and follow host automation")
{
    auto p = makeWeightForGui (true);
    p->setUiProperty ("scale", 1.0);
    std::unique_ptr<juce::AudioProcessorEditor> base (p->createEditor());
    auto& ed = dynamic_cast<WeightEditor&> (*base);
    ed.setVisible (true);
    const auto map = WeightEditor::loadUiMap();

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

    // lamp groups: a click on each lamp and on its caption selects that
    // position; automation lights the lamp of the real value, and only that
    // lamp is bright on screen
    float worstRatio = 1e9f;
    for (auto* id : groupIds)
    {
        const auto& c = control (map, id);
        auto& group = ed.getLampGroup (id);
        auto* raw = p->apvts.getRawParameterValue (id);
        const auto& buttons = *c["buttons"].getArray();
        for (int i = 0; i < buttons.size(); ++i)
        {
            const auto lampRect = rectOf (buttons[i]["rect"]);
            const auto hb = rectOf (buttons[i]["hitbox"]);
            setIndex (*p, id, (i + 1) % buttons.size());
            click (group.getLamp (i), ed, lampRect.getCentre());
            CHECK_MSG (juce::roundToInt (raw->load()) == i, juce::String (id) + " lamp click " + juce::String (i));
            setIndex (*p, id, (i + 1) % buttons.size());
            click (group.getLamp (i), ed, { hb.getCentreX(), hb.getY() + 8.0f }); // the caption
            CHECK_MSG (juce::roundToInt (raw->load()) == i, juce::String (id) + " caption click " + juce::String (i));
        }
        for (int i = 0; i < buttons.size(); ++i)
        {
            setIndex (*p, id, i);
            CHECK (group.getSelected() == i);
            auto img = ed.createComponentSnapshot (ed.getLocalBounds(), true, 1.0f);
            for (int j = 0; j < buttons.size(); ++j)
            {
                CHECK (group.getLamp (j).getToggleState() == (j == i));
                if (j == i)
                    continue;
                const float on = brightnessAt (img, refToEditor (rectOf (buttons[i]["rect"]).reduced (6.0f, 5.0f)));
                const float off = brightnessAt (img, refToEditor (rectOf (buttons[j]["rect"]).reduced (6.0f, 5.0f)));
                worstRatio = juce::jmin (worstRatio, on / juce::jmax (1.0f, off));
                CHECK_MSG (on > off * 2.5f, juce::String (id) + " lamp " + juce::String (i) + " vs " + juce::String (j)
                                                + ": " + juce::String (on) + " / " + juce::String (off));
            }
        }
    }
    MEASURE ("worst lit/unlit lamp brightness", worstRatio);

    // GR MODE never changes the audio: the same input through fresh
    // processors that differ only in the selected mode
    {
        std::array<Levels, 3> out {};
        for (int m = 0; m < 3; ++m)
        {
            auto q = makeWeightForGui();
            neutral (*q);
            setPlain (*q, wp::threshold, -15.0f);
            setIndex (*q, wp::grMode, m);
            sinePhase = 0.0;
            out[(size_t) m] = feed (*q, 0.5f, 0.3f, 0.5);
        }
        CHECK (out[0].outL == out[1].outL && out[1].outL == out[2].outL);
        CHECK (out[0].outR == out[1].outR && out[1].outR == out[2].outR);
        CHECK (out[0].outL < out[0].inL * 0.9); // and it does compress
    }

    // EXTERNAL SC: off -> dark; on with a key -> lit; a click sets the real parameter
    auto& sc = ed.getSidechainLamp();
    const auto lamp = refToEditor (rectOf (control (map, "scExternal")["lamp"]["rect"]).reduced (12.0f));
    setPlain (*p, wp::scExternal, 0.0f);
    feed (*p, 0.3f, 0.3f, 0.1, 0.5f);
    sc.refresh();
    CHECK (! sc.isLampLit());
    const float dark = brightnessAt (ed.createComponentSnapshot (ed.getLocalBounds(), true, 1.0f), lamp);
    click (sc, ed, rectOf (control (map, "scExternal")["hitbox"]).getCentre());
    CHECK (p->apvts.getRawParameterValue (wp::scExternal)->load() > 0.5f);
    feed (*p, 0.3f, 0.3f, 0.1, 0.5f);
    sc.refresh();
    CHECK (p->isUsingExternalSidechain());
    CHECK (sc.isLampLit());
    const float lit = brightnessAt (ed.createComponentSnapshot (ed.getLocalBounds(), true, 1.0f), lamp);
    MEASURE ("EXT SC lamp lit/dark brightness", lit / juce::jmax (1.0f, dark));
    CHECK (lit > dark * 3.0f);

    // switched on, but the host feeds no sidechain bus: internal detector, lamp only faintly lit
    auto p2 = makeWeightForGui (false);
    std::unique_ptr<juce::AudioProcessorEditor> base2 (p2->createEditor());
    auto& ed2 = dynamic_cast<WeightEditor&> (*base2);
    setPlain (*p2, wp::scExternal, 1.0f);
    feed (*p2, 0.3f, 0.3f, 0.1);
    ed2.getSidechainLamp().refresh();
    CHECK (ed2.getSidechainLamp().getToggleState());
    CHECK (! p2->isUsingExternalSidechain());
    CHECK (! ed2.getSidechainLamp().isLampLit());
    const float armed = brightnessAt (ed2.createComponentSnapshot (ed2.getLocalBounds(), true, 1.0f), lamp);
    MEASURE ("EXT SC lamp armed/dark brightness", armed / juce::jmax (1.0f, dark));
    CHECK (armed < lit * 0.6f);
    CHECK (armed >= dark);
}

TEST ("WEIGHT GUI: each GR meter shows the gain reduction its channel really applies")
{
    auto p = makeWeightForGui();
    p->setUiProperty ("scale", 1.0);
    std::unique_ptr<juce::AudioProcessorEditor> base (p->createEditor());
    auto& ed = dynamic_cast<WeightEditor&> (*base);
    ed.setVisible (true);
    const auto map = WeightEditor::loadUiMap();

    // geometry: segments follow the printed scale, 0 dB at the printed 0 mark
    for (int ch = 0; ch < 2; ++ch)
    {
        auto& meter = ed.getMeter (ch);
        const auto& m = control (map, ch == 0 ? "grMeterL" : "grMeterR");
        const auto& geo = meter.getGeometry();
        CHECK (meter.getNumLitCapableSegments() == 13);
        CHECK (geo.segments.size() == 13 + 6);
        CHECK_NEAR (geo.segments.front().startDeg, (float) m["scaleDegrees"]["0"], 1e-4);
        CHECK_NEAR (geo.segments[12].endDeg, (float) m["scaleDegrees"]["20"], 1e-4);
        for (auto& s : geo.segments)
            if (s.loDb == 3.0f)
                CHECK_NEAR (s.startDeg, (float) m["scaleDegrees"]["3"], 1e-4);
        CHECK (meter.litSegmentsFor (0.2f) == 0);
        CHECK (meter.litSegmentsFor (0.3f) == 1);   // half of the first 0.5 dB segment
        CHECK (meter.litSegmentsFor (3.6f) == 5);   // 0, 0.5, 1, 2, 3 .. 4 (middle 3.5)
        CHECK (meter.litSegmentsFor (40.0f) == 13);
    }

    neutral (*p);
    auto measuredGr = [] (double in, double out) { return 20.0 * std::log10 (in / out); };

    // linked, equal channels: both meters show the measured gain drop
    int cases = 0;
    for (float threshold : { -20.0f, -12.0f, -6.0f, 0.0f })
    {
        setPlain (*p, wp::threshold, threshold);
        settle (*p, ed, 0.5f, 0.5f, 45);
        const auto lv = feed (*p, 0.5f, 0.5f, 0.5);
        settle (*p, ed, 0.5f, 0.5f, 10);
        for (int ch = 0; ch < 2; ++ch)
        {
            const double measured = ch == 0 ? measuredGr (lv.inL, lv.outL) : measuredGr (lv.inR, lv.outR);
            const float shown = ed.getMeter (ch).getDisplayedDb();
            MEASURE ("threshold " + juce::String (threshold) + " dB ch" + juce::String (ch) + ": measured GR (dB)", measured);
            MEASURE ("threshold " + juce::String (threshold) + " dB ch" + juce::String (ch) + ": shown GR (dB)", shown);
            CHECK_MSG (std::abs (shown - (float) measured) <= 0.3f, "shown " + juce::String (shown) + " vs measured " + juce::String (measured));
        }
        ++cases;
    }
    CHECK (cases == 4);

    // unlinked, signal in the left only: the right meter stays at zero ...
    setPlain (*p, wp::threshold, -15.0f);
    setPlain (*p, wp::link, 0.0f);
    settle (*p, ed, 0.6f, 0.005f, 45);
    auto lv = feed (*p, 0.6f, 0.005f, 0.5);
    settle (*p, ed, 0.6f, 0.005f, 10);
    const double grL = measuredGr (lv.inL, lv.outL), grR = measuredGr (lv.inR, lv.outR);
    MEASURE ("link 0, left only: measured GR L (dB)", grL);
    MEASURE ("link 0, left only: measured GR R (dB)", grR);
    MEASURE ("link 0, left only: shown GR L (dB)", ed.getMeter (0).getDisplayedDb());
    MEASURE ("link 0, left only: shown GR R (dB)", ed.getMeter (1).getDisplayedDb());
    CHECK (grL > 3.0);
    CHECK (std::abs (ed.getMeter (0).getDisplayedDb() - (float) grL) <= 0.3f);
    CHECK (std::abs (ed.getMeter (1).getDisplayedDb() - (float) grR) <= 0.3f);
    CHECK (ed.getMeter (1).getLitSegments() == 0);
    CHECK (ed.getMeter (0).getLitSegments() > 3);

    // on screen: the last lit segment is bright, the next one dark
    {
        auto& meter = ed.getMeter (0);
        const int lit = meter.getLitSegments();
        auto img = ed.createComponentSnapshot (ed.getLocalBounds(), true, 1.0f);
        const auto origin = meter.getBounds().getPosition().toFloat();
        auto segmentCore = [&] (int i)
        {
            const auto b = meter.getSegmentPath (i).getBounds();
            return juce::Rectangle<float> (b.getWidth() * 0.4f, b.getHeight() * 0.35f).withCentre (b.getCentre()) + origin;
        };
        const float on = brightnessAt (img, segmentCore (lit - 1));
        const float off = brightnessAt (img, segmentCore (lit));
        MEASURE ("lit/unlit segment brightness", on / juce::jmax (1.0f, off));
        CHECK (on > off * 2.0f);
    }

    // ... and fully linked, the right channel is reduced like the left, and its meter says so
    setPlain (*p, wp::link, 100.0f);
    settle (*p, ed, 0.6f, 0.005f, 45);
    lv = feed (*p, 0.6f, 0.005f, 0.5);
    settle (*p, ed, 0.6f, 0.005f, 10);
    const double grR100 = measuredGr (lv.inR, lv.outR);
    MEASURE ("link 100, left only: measured GR R (dB)", grR100);
    CHECK (grR100 > 3.0);
    CHECK (std::abs (ed.getMeter (1).getDisplayedDb() - (float) grR100) <= 0.3f);

    // GR MODE: each meter shows the selected reading of its own channel
    setPlain (*p, wp::link, 0.0f);
    for (int m = 0; m < 3; ++m)
    {
        setIndex (*p, wp::grMode, m);
        feed (*p, 0.6f, 0.005f, 0.5);
        auto& tl = p->getGrTelemetry (0);
        const float rms = tl.getRmsDb(), avg = tl.getAverageDb();
        ed.getMeter (0).refresh (10.0f); // long step: the display reaches the reading
        const float shown = ed.getMeter (0).getDisplayedDb();
        if (m == (int) wp::GrMode::rms)     CHECK_NEAR (shown, rms, 1e-4);
        if (m == (int) wp::GrMode::average) CHECK_NEAR (shown, avg, 1e-4);
        if (m == (int) wp::GrMode::peak)    CHECK (shown >= juce::jmin (rms, avg) - 0.05f);
    }

    // silence: the release recovers and both bars follow it down
    for (int i = 0; i < 60; ++i)
    {
        feed (*p, 0.0f, 0.0f, 1.0 / 30.0);
        ed.getMeter (0).refresh (1.0f / 30.0f);
        ed.getMeter (1).refresh (1.0f / 30.0f);
    }
    CHECK (ed.getMeter (0).getLitSegments() == 0);
    CHECK (ed.getMeter (1).getLitSegments() == 0);
}
