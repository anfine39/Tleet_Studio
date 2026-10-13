#include "TestFramework.h"
#include "../plugins/ECHO/PluginProcessor.h"
#include "../plugins/ECHO/PluginEditor.h"
#include "../plugins/ECHO/Parameters.h"

// GUI tests for ECHO (plugins/ECHO/GUI_TEST_PLAN.md). Every control is
// checked against UI_MAP.json and against the real parameter / DSP state.

using namespace tleet;
using namespace tleet::test;
using tleet::echo::EchoProcessor;
using tleet::echo::EchoEditor;
namespace ep = tleet::echo::params;

namespace
{
    constexpr double sr = 48000.0;
    constexpr int block = 256;
    constexpr float frame = 1.0f / 30.0f;

    std::unique_ptr<EchoProcessor> makeEchoForGui()
    {
        auto p = std::make_unique<EchoProcessor>();
        p->setRateAndBufferSizeDetails (sr, block);
        p->prepareToPlay (sr, block);
        return p;
    }

    void setPlain (EchoProcessor& p, const char* id, float plain)
    {
        auto* r = p.apvts.getParameter (id);
        r->setValueNotifyingHost (r->convertTo0to1 (plain));
    }

    void setIndex (EchoProcessor& p, const char* id, int index) { setPlain (p, id, (float) index); }

    float plainOf (EchoProcessor& p, const char* id)
    {
        auto* r = p.apvts.getParameter (id);
        return r->convertFrom0to1 (r->getValue());
    }

    int indexOf (EchoProcessor& p, const char* id) { return juce::roundToInt (plainOf (p, id)); }

    // A fixed test signal: a short noise burst, then a steady sine, so the
    // echoes, the spring, the tape and the dry path all carry signal. The
    // output of the whole run, left channel.
    std::vector<float> render (EchoProcessor& p, double seconds = 0.9)
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
                float s = t < 0.05 ? 0.25f * (rng.nextFloat() * 2.0f - 1.0f) : 0.0f;
                s += 0.08f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 440.0 * t);
                buf.setSample (0, i, s);
                buf.setSample (1, i, s);
            }
            p.processBlock (buf, midi);
            out.insert (out.end(), buf.getReadPointer (0), buf.getReadPointer (0) + block);
        }
        return out;
    }

    // A steady sine of `peak` through the plugin for `seconds`.
    void feedSine (EchoProcessor& p, float peak, double seconds, double& phase)
    {
        juce::AudioBuffer<float> buf (2, block);
        juce::MidiBuffer midi;
        for (int b = 0; b < juce::jmax (1, (int) (seconds * sr / block)); ++b)
        {
            for (int i = 0; i < block; ++i)
            {
                const float s = peak * (float) std::sin (phase);
                phase += 2.0 * juce::MathConstants<double>::pi * 1000.0 / sr;
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
        return { x * EchoEditor::skinScale, ui::TleetEditorBase::toolbarHeight + y * EchoEditor::skinScale };
    }

    juce::Rectangle<float> refToEditor (juce::Rectangle<float> r)
    {
        const auto tl = refToEditor (r.getX(), r.getY());
        return { tl.x, tl.y, r.getWidth() * EchoEditor::skinScale, r.getHeight() * EchoEditor::skinScale };
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

    // clicks the component that really sits at the point, as the mouse would
    void clickAt (EchoEditor& ed, juce::Point<float> refPoint)
    {
        auto* hit = ed.getComponentAt (refToEditor (refPoint.x, refPoint.y).toInt());
        CHECK_MSG (hit != nullptr, "nothing at " + refPoint.toString());
        if (hit != nullptr)
            click (*hit, ed, refPoint);
    }

    const char* const knobIds[] = { ep::mode, ep::repeatRate, ep::intensity, ep::echoVolume, ep::reverbVolume, ep::bass,
                                    ep::treble, ep::drive, ep::age, ep::wow, ep::flutter, ep::hpf, ep::lpf, ep::stereo,
                                    ep::duck, ep::mix, ep::noise, ep::output };
}

TEST ("ECHO GUI: editor opens at every scale; screenshots of default and representative states")
{
    auto p = makeEchoForGui();
    for (float scale : { 0.75f, 1.0f, 1.5f, 2.0f })
    {
        p->setUiProperty ("scale", scale);
        std::unique_ptr<juce::AudioProcessorEditor> ed (p->createEditor());
        CHECK (dynamic_cast<EchoEditor*> (ed.get()) != nullptr);
        const int expectedW = juce::roundToInt (EchoEditor::baseWidth * scale);
        CHECK_MSG (std::abs (ed->getWidth() - expectedW) <= 1, "width at scale " + juce::String (scale));
        saveSnapshot (*ed, "ECHO_editor_" + juce::String (juce::roundToInt (scale * 100)));
    }

    p->setUiProperty ("scale", 1.0);
    std::unique_ptr<juce::AudioProcessorEditor> base (p->createEditor());
    auto& ed = dynamic_cast<EchoEditor&> (*base);
    double phase = 0.0;
    struct State { const char* name; std::function<void()> apply; };
    const State states[] = {
        { "sync_dotted", [&] { setIndex (*p, ep::timeMode, 1); setIndex (*p, ep::bpmSource, 1); setPlain (*p, ep::manualBpm, 96.0f);
                               setIndex (*p, ep::noteDivision, 2); setIndex (*p, ep::feel, 1); setIndex (*p, ep::mode, 7); } },
        { "dub_burn", [&] { setIndex (*p, ep::timeMode, 0); setIndex (*p, ep::mode, 1); setPlain (*p, ep::intensity, 8.0f);
                            setIndex (*p, ep::character, 2); setIndex (*p, ep::filterMode, 0); setIndex (*p, ep::tapeCondition, 2);
                            setPlain (*p, ep::hpf, 120.0f); setPlain (*p, ep::lpf, 3000.0f); setPlain (*p, ep::output, -4.0f); } },
        { "reverb_only", [&] { setIndex (*p, ep::mode, 0); setPlain (*p, ep::reverbVolume, 6.0f); setIndex (*p, ep::filterMode, 2);
                               setIndex (*p, ep::tapeCondition, 1); setIndex (*p, ep::character, 0); } },
    };
    for (auto& st : states)
    {
        st.apply();
        for (int i = 0; i < 10; ++i)
        {
            feedSine (*p, 0.12f, frame, phase);
            ed.refresh (frame);
            ed.getVu (0).tick (frame);
            ed.getVu (1).tick (frame);
        }
        saveSnapshot (ed, juce::String ("ECHO_state_") + st.name);
    }
}

TEST ("ECHO GUI: every mapped control sits at its mockup position and owns its click area")
{
    auto p = makeEchoForGui();
    p->setUiProperty ("scale", 1.0);
    std::unique_ptr<juce::AudioProcessorEditor> base (p->createEditor());
    auto& ed = dynamic_cast<EchoEditor&> (*base);
    ed.setVisible (true);
    const auto map = EchoEditor::loadUiMap();

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

    int knobsChecked = 0, buttonsChecked = 0;
    for (auto& c : *map["controls"].getArray())
    {
        const auto type = c["type"].toString();
        const auto id = c["id"].toString();
        if (type.startsWith ("ROTARY"))
        {
            const auto hb = rectOf (c["hitbox"]);
            auto& knob = ed.getKnob (c["parameter"].toString());
            checkArea (id, hb, &knob, 3.0f);
            const juce::Point<float> centreRef ((float) c["centre"][0], (float) c["centre"][1]);
            const auto actual = knob.getBounds().getPosition().toFloat() + (centreRef - hb.getPosition()) * EchoEditor::skinScale;
            CHECK (actual.getDistanceFrom (refToEditor (centreRef.x, centreRef.y)) <= 1.0f);
            ++knobsChecked;
        }
        else if (type == "BUTTON_GROUP")
        {
            auto& group = ed.getLampGroup (id);
            int i = 0;
            for (auto& b : *c["buttons"].getArray())
            {
                checkArea (id + " " + b["label"].toString(), rectOf (b["hitbox"]), &group.getLamp (i++), 3.0f);
                ++buttonsChecked;
            }
        }
        else if (type == "SLIDE_SWITCH")
        {
            auto& sw = ed.getSlideSwitch (id);
            checkArea (id + " switch", rectOf (c["housing"]), &sw, 6.0f);
            for (auto& cap : *c["captions"].getArray())
                checkArea (id + " caption", rectOf (cap), &sw, 2.0f);
        }
        else if (type == "VERTICAL_SWITCH")
        {
            checkArea (id + " switch", rectOf (c["housing"]), &ed.getFilterModeSwitch(), 4.0f);
            for (auto& cap : *c["captions"].getArray())
                checkArea (id + " caption", rectOf (cap), &ed.getFilterModeSwitch(), 2.0f);
        }
        else if (type == "TAP")
            checkArea ("TAP", rectOf (c["body"]), &ed.getTap(), 4.0f);
        else if (type == "BPM_DISPLAY")
        {
            checkArea ("BPM up", rectOf (c["up"]), &ed.getBpmDisplay(), 2.0f);
            checkArea ("BPM down", rectOf (c["down"]), &ed.getBpmDisplay(), 2.0f);
        }
        else if (type == "TAPE_BAR")
            checkArea ("TAPE CONDITION", rectOf (c["hitbox"]), &ed.getTapeBar(), 3.0f);
        else if (type == "VU")
            checkArea (id, rectOf (c["face"]), &ed.getVu (id == "vuIn" ? 0 : 1), 4.0f);
    }
    MEASURE ("knob click areas checked", knobsChecked);
    MEASURE ("button click areas checked", buttonsChecked);
    CHECK (knobsChecked == 18);
    CHECK (buttonsChecked == 6 + 3 + 3);

    for (size_t i = 0; i < areas.size(); ++i)
        for (size_t j = i + 1; j < areas.size(); ++j)
            if (! areas[i].first.upToFirstOccurrenceOf (" ", false, false).equalsIgnoreCase (areas[j].first.upToFirstOccurrenceOf (" ", false, false)))
                CHECK_MSG (! areas[i].second.intersects (areas[j].second), areas[i].first + " overlaps " + areas[j].first);
}

TEST ("ECHO GUI: every scale number and dot sits where the knob really has that value; MODE lamps at the real positions")
{
    auto p = makeEchoForGui();
    std::unique_ptr<juce::AudioProcessorEditor> base (p->createEditor());
    auto& ed = dynamic_cast<EchoEditor&> (*base);
    const auto map = EchoEditor::loadUiMap();

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

        const auto& ds = ed.getStepDots (id);
        if (c.hasProperty ("stepDots"))
        {
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
        else
            CHECK (ds.empty());
    }
    MEASURE ("labels checked", labels);
    MEASURE ("live dots checked", dots);
    MEASURE ("worst label angle error (deg)", worstLabel);
    MEASURE ("worst pointer-vs-label error (deg)", worstPointer);
    MEASURE ("worst pointer-vs-dot error (deg)", worstDot);
    CHECK (dots == 13 + 13 + 11 * 15); // REPEAT RATE (1/6 decade) and OUTPUT (2 dB): 13; 0..10, 0..100, +-10 dB, HPF, LPF: 11

    auto texts = [&] (const char* id)
    {
        juce::StringArray s;
        for (auto& l : ed.getScaleLabels (id)) s.add (l.text);
        return s.joinIntoString (" ");
    };
    CHECK_MSG (texts (ep::mode) == "REVERB 1 2 3 1+2 1+3 2+3 1+2+3", texts (ep::mode));
    CHECK_MSG (texts (ep::repeatRate) == "20 50 100 200 500 1K 2K", texts (ep::repeatRate));
    for (auto* id : { ep::intensity, ep::echoVolume, ep::reverbVolume, ep::drive, ep::age })
        CHECK_MSG (texts (id) == "0 10", texts (id));
    CHECK_MSG (texts (ep::bass) == "-10 0 +10", texts (ep::bass));
    CHECK_MSG (texts (ep::treble) == "-10 0 +10", texts (ep::treble));
    CHECK_MSG (texts (ep::hpf) == "20 50 100 300", texts (ep::hpf));
    CHECK_MSG (texts (ep::lpf) == "1K 3K 10K 20K", texts (ep::lpf));
    for (auto* id : { ep::wow, ep::flutter, ep::stereo, ep::duck, ep::mix, ep::noise })
        CHECK_MSG (texts (id) == "0 100", texts (id));
    CHECK_MSG (texts (ep::output) == "-12 0 +12", texts (ep::output));

    // MODE: the pointer of each position points at the printed lamp of that position
    {
        const auto& c = control (map, ep::mode);
        const juce::Point<float> centre ((float) c["centre"][0], (float) c["centre"][1]);
        auto& knob = ed.getKnob (ep::mode);
        const float t0 = (float) c["travelDegrees"][0], t1 = (float) c["travelDegrees"][1];
        float worst = 0.0f;
        for (int i = 0; i < ep::numModes; ++i)
        {
            setIndex (*p, ep::mode, i);
            const float pointer = t0 + knob.getNormalisedValue() * (t1 - t0);
            const auto lamp = juce::Point<float> ((float) c["lamps"]["positions"][i][0], (float) c["lamps"]["positions"][i][1]);
            const float at = juce::radiansToDegrees (std::atan2 (lamp.x - centre.x, -(lamp.y - centre.y)));
            worst = juce::jmax (worst, std::abs (pointer - at));
            CHECK_MSG (std::abs (pointer - at) <= 2.0f, "MODE " + ep::modeNames[i] + ": pointer " + juce::String (pointer, 1) + ", lamp " + juce::String (at, 1));
        }
        MEASURE ("MODE: worst pointer vs printed lamp (deg)", worst);
    }
}

TEST ("ECHO GUI: every control drives its real parameter and follows automation")
{
    auto p = makeEchoForGui();
    p->setUiProperty ("scale", 1.0);
    std::unique_ptr<juce::AudioProcessorEditor> base (p->createEditor());
    auto& ed = dynamic_cast<EchoEditor&> (*base);
    ed.setVisible (true);
    const auto map = EchoEditor::loadUiMap();

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

    // lamp buttons: a click selects; automation lights the right lamp
    for (auto* id : { ep::noteDivision, ep::feel, ep::character })
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

    // slide switches: a caption selects its side, the switch toggles
    for (auto* id : { ep::timeMode, ep::bpmSource })
    {
        const auto& c = control (map, id);
        auto& sw = ed.getSlideSwitch (id);
        clickAt (ed, rectOf (c["captions"][1]).getCentre());
        CHECK (indexOf (*p, id) == 1 && sw.getState() == 1);
        clickAt (ed, rectOf (c["captions"][1]).getCentre());
        CHECK (indexOf (*p, id) == 1);
        clickAt (ed, rectOf (c["captions"][0]).getCentre());
        CHECK (indexOf (*p, id) == 0 && sw.getState() == 0);
        clickAt (ed, rectOf (c["housing"]).getCentre());
        CHECK (indexOf (*p, id) == 1);
        clickAt (ed, rectOf (c["housing"]).getCentre());
        CHECK (indexOf (*p, id) == 0);
        setIndex (*p, id, 1);
        CHECK (sw.getState() == 1);
    }

    // FILTER MODE: each cell and each caption selects its position
    {
        const auto& c = control (map, ep::filterMode);
        auto& fm = ed.getFilterModeSwitch();
        const auto housing = rectOf (c["housing"]);
        for (int i = 0; i < 3; ++i)
        {
            clickAt (ed, rectOf (c["captions"][i]).getCentre());
            CHECK_MSG (indexOf (*p, ep::filterMode) == i && fm.getState() == i, "FILTER MODE caption " + juce::String (i));
            setIndex (*p, ep::filterMode, (i + 1) % 3);
            clickAt (ed, { housing.getCentreX(), housing.getY() + housing.getHeight() * ((float) i + 0.5f) / 3.0f });
            CHECK_MSG (indexOf (*p, ep::filterMode) == i, "FILTER MODE cell " + juce::String (i));
        }
    }

    // TAPE CONDITION: a click at a mark selects that condition
    {
        const auto& c = control (map, ep::tapeCondition);
        const auto bar = rectOf (c["bar"]);
        for (int i = 0; i < 3; ++i)
        {
            clickAt (ed, { (float) c["ticks"][i], bar.getCentreY() });
            CHECK_MSG (indexOf (*p, ep::tapeCondition) == i && ed.getTapeBar().getState() == i, "TAPE CONDITION " + juce::String (i));
        }
    }

    // BPM arrows: step MANUAL BPM from the tempo in use and switch to MANUAL
    {
        const auto& c = control (map, "bpmDisplay");
        setIndex (*p, ep::bpmSource, 0);
        setPlain (*p, ep::manualBpm, 120.0f);
        clickAt (ed, rectOf (c["up"]).getCentre());
        CHECK_NEAR (plainOf (*p, ep::manualBpm), 121.0f, 1e-3);
        CHECK (indexOf (*p, ep::bpmSource) == (int) ep::BpmSource::manual);
        clickAt (ed, rectOf (c["down"]).getCentre());
        clickAt (ed, rectOf (c["down"]).getCentre());
        CHECK_NEAR (plainOf (*p, ep::manualBpm), 119.0f, 1e-3);
        setPlain (*p, ep::manualBpm, ep::bpmMax);
        clickAt (ed, rectOf (c["up"]).getCentre());
        CHECK_NEAR (plainOf (*p, ep::manualBpm), ep::bpmMax, 1e-3);
    }

    // TAP: in FREE the taps set REPEAT RATE, in SYNC MANUAL BPM (the processor's tap)
    {
        setIndex (*p, ep::timeMode, 0);
        ed.getTap().onTap (100.0);
        ed.getTap().onTap (100.4);
        CHECK_NEAR (plainOf (*p, ep::repeatRate), 400.0f, 1.0);
        setIndex (*p, ep::timeMode, 1);
        ed.getTap().onTap (200.0);
        ed.getTap().onTap (200.5);
        CHECK_NEAR (plainOf (*p, ep::manualBpm), 120.0f, 0.5);
        CHECK (indexOf (*p, ep::bpmSource) == (int) ep::BpmSource::manual);
    }
}

TEST ("ECHO GUI: lamps, displays and meters show the real DSP state")
{
    auto p = makeEchoForGui();
    p->setUiProperty ("scale", 1.0);
    std::unique_ptr<juce::AudioProcessorEditor> base (p->createEditor());
    auto& ed = dynamic_cast<EchoEditor&> (*base);
    ed.setVisible (true);
    const auto map = EchoEditor::loadUiMap();
    double phase = 0.0;

    // MODE lamp: the selected position; brighter while its heads play signal
    {
        setPlain (*p, ep::repeatRate, 50.0f);
        for (int m = 0; m < ep::numModes; ++m)
        {
            setIndex (*p, ep::mode, m);
            ed.refresh (frame);
            CHECK (ed.getModeLamp().getPosition() == m);
        }
        setIndex (*p, ep::mode, 4); // 1+2
        for (int i = 0; i < 20; ++i) { feedSine (*p, 0.0f, frame, phase); ed.refresh (frame); }
        const float quiet = ed.getModeLamp().getGlow();
        for (int i = 0; i < 20; ++i) { feedSine (*p, 0.3f, frame, phase); ed.refresh (frame); }
        const float playing = ed.getModeLamp().getGlow();
        MEASURE ("MODE lamp glow quiet / playing", quiet / playing);
        CHECK_NEAR (quiet, 0.55f, 0.02);
        CHECK (playing > quiet + 0.2f);
        CHECK (EchoEditor::loadUiMap()["controls"].isArray());

        // the lit lamp shows on the screenshot at the selected position only
        const auto& c = control (map, ep::mode);
        auto lampAt = [&] (int i) { return juce::Point<float> ((float) c["lamps"]["positions"][i][0], (float) c["lamps"]["positions"][i][1]); };
        const auto img = snapshot (ed);
        const float on = brightnessAt (img, refToEditor (juce::Rectangle<float> (8.0f, 8.0f).withCentre (lampAt (4))));
        const float off = brightnessAt (img, refToEditor (juce::Rectangle<float> (8.0f, 8.0f).withCentre (lampAt (5))));
        MEASURE ("MODE lamp lit / dark brightness", on / juce::jmax (1.0f, off));
        CHECK (on > off * 1.5f);
    }

    // REPEAT RATE dimmed only in SYNC (the note value sets the time there)
    setIndex (*p, ep::timeMode, 0);
    ed.refresh (frame);
    CHECK (! ed.isRepeatRateDimmed() && ed.getKnob (ep::repeatRate).getAlpha() > 0.99f);
    setIndex (*p, ep::timeMode, 1);
    ed.refresh (frame);
    CHECK (ed.isRepeatRateDimmed() && ed.getKnob (ep::repeatRate).getAlpha() < 0.5f);

    // BPM display: the tempo SYNC really uses
    setIndex (*p, ep::bpmSource, 1);
    setPlain (*p, ep::manualBpm, 96.0f);
    feedSine (*p, 0.0f, 0.02, phase);
    ed.refresh (frame);
    CHECK_MSG (ed.getBpmDisplay().getText() == "96", ed.getBpmDisplay().getText());
    CHECK (echo::BpmDisplay::textFor (128.5) == "128.5");
    CHECK (echo::BpmDisplay::textFor (92.0) == "92");
    CHECK (echo::BpmDisplay::textFor (91.96) == "92");

    // TAP lamp: one flash per real head-1 period
    {
        CHECK (echo::TapButton::lampOnAt (1000.0, 500.0));
        CHECK (echo::TapButton::lampOnAt (1069.0, 500.0));
        CHECK (! echo::TapButton::lampOnAt (1071.0, 500.0));
        CHECK (! echo::TapButton::lampOnAt (1499.0, 500.0));
        CHECK (echo::TapButton::lampOnAt (1500.0, 500.0));
        CHECK (! echo::TapButton::lampOnAt (1000.0, 0.0));
        int lit = 0;
        const double period = 500.0;
        for (double t = 0.0; t < 10000.0; t += 1.0)
            lit += echo::TapButton::lampOnAt (t, period) ? 1 : 0;
        CHECK_NEAR (lit, 20 * (int) echo::TapButton::flashMs, 25);
    }

    // TAPE CONDITION: lit up to the mark of the condition in use
    {
        const auto& c = control (map, ep::tapeCondition);
        auto& bar = ed.getTapeBar();
        const int expected[] = { 1, 6, 10 };
        for (int i = 0; i < 3; ++i)
        {
            setIndex (*p, ep::tapeCondition, i);
            CHECK (bar.getState() == i);
            CHECK_MSG (bar.litSegmentsFor (i) == expected[i], "segments " + juce::String (bar.litSegmentsFor (i)));
            CHECK_NEAR ((float) c["segments"][expected[i]], (float) c["ticks"][i], 2.0);
        }
    }

    // VU: a sine at -18 dBFS reads 0 VU on IN and (MIX 0, OUTPUT 0) on OUT
    {
        setPlain (*p, ep::mix, 0.0f);
        setPlain (*p, ep::output, 0.0f);
        const float peak = std::pow (10.0f, -18.0f / 20.0f);
        for (int i = 0; i < 90; ++i)
        {
            feedSine (*p, peak, frame, phase);
            ed.getVu (0).tick (frame);
            ed.getVu (1).tick (frame);
        }
        MEASURE ("IN needle at -18 dBFS sine (VU)", ed.getVu (0).getNeedleVu());
        MEASURE ("OUT needle at -18 dBFS sine, MIX 0 (VU)", ed.getVu (1).getNeedleVu());
        CHECK_NEAR (ed.getVu (0).getNeedleVu(), 0.0f, 0.2);
        CHECK_NEAR (ed.getVu (1).getNeedleVu(), 0.0f, 0.3);
        // the needle points at the printed 0 mark of its own scale
        for (int i = 0; i < 2; ++i)
        {
            const auto& c = control (map, i == 0 ? "vuIn" : "vuOut");
            CHECK_NEAR (juce::radiansToDegrees (ed.getVu (i).angleForVu (0.0f)), (float) c["scaleDegrees"]["0"], 0.01);
            CHECK_NEAR (juce::radiansToDegrees (ed.getVu (i).angleForVu (-3.0f)), (float) c["scaleDegrees"]["-3"], 0.01);
        }
    }
}

TEST ("ECHO GUI: no dead controls: moving each control from the editor changes the sound")
{
    struct Case { const char* id; float from, to; };
    const Case cases[] = {
        { ep::mode, 1, 7 }, { ep::repeatRate, 80, 300 }, { ep::intensity, 2, 8 }, { ep::echoVolume, 3, 9 },
        { ep::reverbVolume, 0, 8 }, { ep::bass, 0, 8 }, { ep::treble, 0, -8 }, { ep::drive, 0, 10 }, { ep::age, 0, 10 },
        { ep::wow, 0, 100 }, { ep::flutter, 0, 100 }, { ep::hpf, 20, 250 }, { ep::lpf, 20000, 1500 }, { ep::stereo, 0, 100 },
        { ep::duck, 0, 100 }, { ep::mix, 35, 90 }, { ep::noise, 0, 100 }, { ep::output, 0, -6 },
        { ep::timeMode, 0, 1 }, { ep::noteDivision, 2, 4 }, { ep::feel, 0, 2 }, { ep::filterMode, 0, 2 },
        { ep::tapeCondition, 0, 2 }, { ep::character, 0, 2 },
    };
    for (auto& cs : cases)
    {
        std::array<std::vector<float>, 2> out;
        for (int k = 0; k < 2; ++k)
        {
            auto q = makeEchoForGui();
            std::unique_ptr<juce::AudioProcessorEditor> b (q->createEditor());
            auto& e = dynamic_cast<EchoEditor&> (*b);
            const auto id = juce::String (cs.id);
            // a setting where every control has something to act on
            setPlain (*q, ep::repeatRate, 120.0f);
            setPlain (*q, ep::mix, 60.0f);
            setPlain (*q, ep::reverbVolume, 3.0f);
            setIndex (*q, ep::timeMode, id == ep::noteDivision || id == ep::feel ? 1 : 0);
            setIndex (*q, ep::bpmSource, 1);
            setPlain (*q, ep::manualBpm, 140.0f);
            const float v = k == 0 ? cs.from : cs.to;
            auto* param = q->apvts.getParameter (cs.id);
            // the selectors' clicks are tested above; here the sound
            if (param->getNumSteps() <= 8 && id != ep::mode)
                setIndex (*q, cs.id, (int) v);
            else
                e.getKnob (cs.id).setNormalisedFromUser (param->convertTo0to1 (v), true);
            CHECK_NEAR (param->convertFrom0to1 (param->getValue()), v, 1e-2 * juce::jmax (1.0f, std::abs (v)));
            out[(size_t) k] = render (*q);
        }
        const double diff = rmsDifferenceDb (out[0], out[1]);
        MEASURE (juce::String (cs.id) + ": change of the output (dB rel. signal)", diff);
        CHECK_MSG (diff > -40.0, juce::String (cs.id) + " changed the sound by only " + juce::String (diff, 1) + " dB");
    }
}
