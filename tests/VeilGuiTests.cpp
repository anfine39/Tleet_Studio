#include "TestFramework.h"
#include "../plugins/VEIL/PluginProcessor.h"
#include "../plugins/VEIL/PluginEditor.h"
#include "../plugins/VEIL/Parameters.h"

// GUI tests for VEIL (plugins/VEIL/GUI_TEST_PLAN.md). Every control is
// checked against UI_MAP.json and against the real parameter / DSP state.

using namespace tleet;
using namespace tleet::test;
using tleet::veil::VeilProcessor;
using tleet::veil::VeilEditor;
namespace vp = tleet::veil::params;

namespace
{
    constexpr double sr = 48000.0;
    constexpr int block = 256;
    constexpr float frame = 1.0f / 30.0f;

    std::unique_ptr<VeilProcessor> makeVeilForGui (bool sidechain = false)
    {
        auto p = std::make_unique<VeilProcessor>();
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

    void setPlain (VeilProcessor& p, const char* id, float plain)
    {
        auto* r = p.apvts.getParameter (id);
        r->setValueNotifyingHost (r->convertTo0to1 (plain));
    }

    void setIndex (VeilProcessor& p, const char* id, int index) { setPlain (p, id, (float) index); }

    struct Levels { double inL, inR, outL, outR; };

    // Steady sines (left / right peaks, optional key on the sidechain bus)
    // through the plugin; RMS of each channel over the last half. The phase
    // continues across calls, so blocks join without clicks.
    struct Feeder
    {
        double phase = 0.0, hz = 1000.0;
        std::vector<float> lastOutL; // output of the last call, left channel

        Levels operator() (VeilProcessor& p, float peakL, float peakR, double seconds, float keyPeak = 0.0f)
        {
            const int channels = p.getTotalNumInputChannels();
            juce::AudioBuffer<float> buf (juce::jmax (2, channels), block);
            juce::MidiBuffer midi;
            const int blocks = juce::jmax (1, (int) (seconds * sr / block));
            double iL = 0, iR = 0, oL = 0, oR = 0; long n = 0;
            lastOutL.clear();
            for (int b = 0; b < blocks; ++b)
            {
                buf.clear();
                double bl = 0, br = 0;
                for (int i = 0; i < block; ++i)
                {
                    const float s = (float) std::sin (phase);
                    phase += 2.0 * juce::MathConstants<double>::pi * hz / sr;
                    buf.setSample (0, i, peakL * s);
                    buf.setSample (1, i, peakR * s);
                    for (int ch = 2; ch < channels; ++ch)
                        buf.setSample (ch, i, keyPeak * s);
                    bl += (double) peakL * s * peakL * s;
                    br += (double) peakR * s * peakR * s;
                }
                p.processBlock (buf, midi);
                lastOutL.insert (lastOutL.end(), buf.getReadPointer (0), buf.getReadPointer (0) + block);
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
    };

    juce::Point<float> refToEditor (float x, float y)
    {
        return { x * VeilEditor::skinScale, ui::TleetEditorBase::toolbarHeight + y * VeilEditor::skinScale };
    }

    juce::Rectangle<float> refToEditor (juce::Rectangle<float> r)
    {
        const auto tl = refToEditor (r.getX(), r.getY());
        return { tl.x, tl.y, r.getWidth() * VeilEditor::skinScale, r.getHeight() * VeilEditor::skinScale };
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

    // Red glow: how far red stands above the other channels (the lit buttons
    // are drawn as a red-lit frame and caption on a dark face).
    float redGlowAt (const juce::Image& img, juce::Rectangle<float> editorRect)
    {
        const auto r = editorRect.toNearestInt().getIntersection (img.getBounds());
        double sum = 0; int n = 0;
        for (int y = r.getY(); y < r.getBottom(); ++y)
            for (int x = r.getX(); x < r.getRight(); ++x)
            {
                const auto c = img.getPixelAt (x, y);
                sum += juce::jmax (0.0f, (float) c.getRed() - 0.5f * ((float) c.getGreen() + (float) c.getBlue()));
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

    // A fixed, documented test setting: no tube colour, fast ballistics,
    // linked, internal key, no filters, plain blends and trims.
    void neutral (VeilProcessor& p)
    {
        setIndex (p, vp::character, (int) vp::Character::clean);
        setPlain (p, vp::tubeDrive, 0.0f);
        setPlain (p, vp::harmonics, 0.0f);
        setPlain (p, vp::saturation, 0.0f);
        setPlain (p, vp::attack, 10.0f);
        setPlain (p, vp::recovery, 50.0f);
        setPlain (p, vp::compress, 5.0f);
        setPlain (p, vp::input, 0.0f);
        setPlain (p, vp::output, 0.0f);
        setPlain (p, vp::mix, 100.0f);
        setPlain (p, vp::dryWet, 100.0f);
        setPlain (p, vp::hpf, vp::filterOffHz);
        setPlain (p, vp::scHpf, vp::filterOffHz);
        setIndex (p, vp::scExternal, 0);
        setPlain (p, vp::stereoLink, 1.0f);
    }

    // Steady signal for `seconds`, the needles moving frame by frame as the UI timer does.
    void settle (VeilProcessor& p, VeilEditor& ed, Feeder& feed, float peakL, float peakR, double seconds)
    {
        for (int i = 0; i < juce::roundToInt (seconds / frame); ++i)
        {
            feed (p, peakL, peakR, frame);
            ed.getVu (0).tick (frame);
            ed.getVu (1).tick (frame);
        }
    }

    const char* const knobIds[] = { vp::input, vp::threshold, vp::attack, vp::recovery, vp::compress, vp::mix, vp::output,
                                    vp::tubeDrive, vp::harmonics, vp::hpf, vp::saturation, vp::dryWet, vp::scHpf };
}

TEST ("VEIL GUI: editor opens at every scale; screenshots of default and representative states")
{
    auto p = makeVeilForGui();
    for (float scale : { 0.75f, 1.0f, 1.5f, 2.0f })
    {
        p->setUiProperty ("scale", scale);
        std::unique_ptr<juce::AudioProcessorEditor> ed (p->createEditor());
        CHECK (dynamic_cast<VeilEditor*> (ed.get()) != nullptr);
        const int expectedW = juce::roundToInt (VeilEditor::baseWidth * scale);
        CHECK_MSG (std::abs (ed->getWidth() - expectedW) <= 1, "width at scale " + juce::String (scale));
        saveSnapshot (*ed, "VEIL_editor_" + juce::String (juce::roundToInt (scale * 100)));
    }

    p->setUiProperty ("scale", 1.0);
    std::unique_ptr<juce::AudioProcessorEditor> base (p->createEditor());
    auto& ed = dynamic_cast<VeilEditor&> (*base);
    Feeder feed;
    struct State { const char* name; float peakL, peakR; std::function<void()> apply; };
    const State states[] = {
        { "gentle_gr",  0.3f, 0.3f, [&] { setPlain (*p, vp::threshold, -6.0f); setPlain (*p, vp::compress, 3.0f); } },
        { "dual_mono_vu", 0.25f, 0.04f, [&] { setPlain (*p, vp::threshold, -10.0f); setPlain (*p, vp::stereoLink, 0.0f);
                                              setIndex (*p, vp::meterMode, (int) vp::MeterMode::vu);
                                              setIndex (*p, vp::character, (int) vp::Character::thick);
                                              setPlain (*p, vp::hpf, 80.0f); } },
        { "heavy_ext_inout", 0.5f, 0.5f, [&] { setPlain (*p, vp::threshold, -20.0f); setPlain (*p, vp::compress, 10.0f);
                                               setPlain (*p, vp::stereoLink, 1.0f); setIndex (*p, vp::scExternal, 1);
                                               setIndex (*p, vp::meterMode, (int) vp::MeterMode::inOut);
                                               setIndex (*p, vp::character, (int) vp::Character::clean);
                                               setPlain (*p, vp::mix, 70.0f); setPlain (*p, vp::dryWet, 80.0f);
                                               setPlain (*p, vp::scHpf, 150.0f); } },
    };
    for (auto& st : states)
    {
        st.apply();
        settle (*p, ed, feed, st.peakL, st.peakR, 1.5);
        ed.getSidechainLamps().refresh();
        ed.getSpectrum().processPending();
        ed.getSpectrum().processPending();
        MEASURE (juce::String (st.name) + ": left needle (VU units)", ed.getVu (0).getNeedleVu());
        MEASURE (juce::String (st.name) + ": right needle (VU units)", ed.getVu (1).getNeedleVu());
        saveSnapshot (ed, juce::String ("VEIL_state_") + st.name);
    }
}

TEST ("VEIL GUI: every mapped control sits at its mockup position and owns its click area")
{
    auto p = makeVeilForGui();
    p->setUiProperty ("scale", 1.0);
    std::unique_ptr<juce::AudioProcessorEditor> base (p->createEditor());
    auto& ed = dynamic_cast<VeilEditor&> (*base);
    ed.setVisible (true);
    const auto map = VeilEditor::loadUiMap();

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
        if (type == "ROTARY")
        {
            const auto hb = rectOf (c["hitbox"]);
            auto& knob = ed.getKnob (c["parameter"].toString());
            checkArea (c["id"].toString(), hb, &knob);
            // the pointer turns around the centre of the printed dot circle
            const juce::Point<float> centreRef ((float) c["centre"][0], (float) c["centre"][1]);
            const auto actual = knob.getBounds().getPosition().toFloat() + (centreRef - hb.getPosition()) * VeilEditor::skinScale;
            CHECK (actual.getDistanceFrom (refToEditor (centreRef.x, centreRef.y)) <= 1.0f);
            // the readout window lies inside the knob's own click area
            if (c.hasProperty ("readout"))
                CHECK_MSG (hb.contains (rectOf (c["readout"])), c["id"].toString() + " readout outside its hitbox");
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
        else if (type == "LINK_SWITCH")
        {
            checkArea ("stereo link lamp", rectOf (c["lamp"]["hitbox"]), &ed.getLinkSwitch().getLamp());
            checkArea ("dual mono", rectOf (c["dualMono"]["hitbox"]), &ed.getLinkSwitch().getDualMono());
            lampsChecked += 2;
        }
        else if (type == "SPECTRUM")
        {
            checkArea ("spectrum", rectOf (c["hitbox"]), &ed.getSpectrum());
        }
    }
    MEASURE ("knob hitboxes checked", knobsChecked);
    MEASURE ("lamp click areas checked", lampsChecked);
    CHECK (knobsChecked == 13);
    CHECK (lampsChecked == 3 + 2 + 3 + 2);

    for (auto* id : { "vuL", "vuR" })
        areas.push_back ({ id, rectOf (control (map, id)["hitbox"]) });
    for (size_t i = 0; i < areas.size(); ++i)
        for (size_t j = i + 1; j < areas.size(); ++j)
            CHECK_MSG (! areas[i].second.intersects (areas[j].second), areas[i].first + " overlaps " + areas[j].first);

    // the analyser quality control lives in the toolbar, not on the panel
    CHECK (ed.getSpectrumQualityButton().getY() < ui::TleetEditorBase::toolbarHeight);
}

TEST ("VEIL GUI: every scale number sits where the knob really has that value")
{
    auto p = makeVeilForGui();
    std::unique_ptr<juce::AudioProcessorEditor> base (p->createEditor());
    auto& ed = dynamic_cast<VeilEditor&> (*base);
    const auto map = VeilEditor::loadUiMap();

    float worstLabel = 0.0f, worstPointer = 0.0f;
    int labels = 0;
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
        CHECK (list.size() == (size_t) c["scaleLabels"]["values"].size());

        for (auto& l : list)
        {
            // the label is printed on the ray of its value ...
            const auto mid = l.box.getCentre().translated (0.0f, -l.box.getHeight() * 0.07f);
            const float printed = juce::radiansToDegrees (std::atan2 (mid.x - sc.x, -(mid.y - sc.y)));
            const float err = std::abs (printed - l.angleDegrees);
            worstLabel = juce::jmax (worstLabel, err);
            CHECK_MSG (err <= 1.0f, id + " '" + l.text + "' printed at " + juce::String (printed, 2) + " deg, value at " + juce::String (l.angleDegrees, 2));
            // ... outside the dot circle ...
            const float r = (float) c["dotRadius"];
            CHECK_MSG (! l.box.intersects (juce::Rectangle<float> (2.0f * r, 2.0f * r).withCentre (sc).reduced (r * 0.3f)),
                       id + " '" + l.text + "' inside the dot circle");

            // ... and the knob turned to that value points exactly there
            param->setValueNotifyingHost (param->convertTo0to1 (l.value));
            const float pointer = t0 + knob.getNormalisedValue() * (t1 - t0);
            worstPointer = juce::jmax (worstPointer, std::abs (pointer - l.angleDegrees));
            CHECK_MSG (std::abs (pointer - l.angleDegrees) <= 0.05f, id + " '" + l.text + "' pointer " + juce::String (pointer, 3));
            ++labels;
        }
    }
    MEASURE ("labels checked", labels);
    MEASURE ("worst label angle error (deg)", worstLabel);
    MEASURE ("worst pointer-vs-label error (deg)", worstPointer);
    CHECK (labels == 3 * 3 + 5 + 5 + 3 + 3 * 5 + 3 + 3);

    auto texts = [&] (const char* id)
    {
        juce::StringArray s;
        for (auto& l : ed.getScaleLabels (id)) s.add (l.text);
        return s.joinIntoString (" ");
    };
    for (auto* id : { vp::input, vp::threshold, vp::output })
        CHECK_MSG (texts (id) == "-20 0 +20", texts (id));
    CHECK_MSG (texts (vp::attack) == "10 30 100 300 1K", texts (vp::attack));
    CHECK_MSG (texts (vp::recovery) == "50 150 400 1S 2S", texts (vp::recovery));
    CHECK_MSG (texts (vp::compress) == "0 5 10", texts (vp::compress));
    for (auto* id : { vp::mix, vp::tubeDrive, vp::harmonics, vp::saturation, vp::dryWet })
        CHECK_MSG (texts (id) == "0 50 100", texts (id));
    CHECK_MSG (texts (vp::hpf) == "OFF 100 300", texts (vp::hpf));
    CHECK_MSG (texts (vp::scHpf) == "OFF 80 300", texts (vp::scHpf));

    // the label values are the parameter's own values (texts agree with the host's)
    for (auto* id : knobIds)
    {
        auto* param = p->apvts.getParameter (id);
        for (auto& l : ed.getScaleLabels (id))
        {
            const auto shown = param->getText (param->convertTo0to1 (l.value), 32);
            const float back = param->convertFrom0to1 (param->convertTo0to1 (l.value));
            CHECK_MSG (std::abs (back - l.value) <= 0.01f * juce::jmax (1.0f, std::abs (l.value)), juce::String (id) + " '" + l.text + "' is off the parameter grid");
            if (l.text != "OFF")
                CHECK_MSG (shown.isNotEmpty(), juce::String (id) + " " + l.text);
        }
    }
}

TEST ("VEIL GUI: knobs, lamps and the link switch drive the real parameters and follow host automation")
{
    auto p = makeVeilForGui (true);
    p->setUiProperty ("scale", 1.0);
    std::unique_ptr<juce::AudioProcessorEditor> base (p->createEditor());
    auto& ed = dynamic_cast<VeilEditor&> (*base);
    ed.setVisible (true);
    const auto map = VeilEditor::loadUiMap();

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

    // CHARACTER and METER: a click on each button selects that position;
    // automation lights the button of the real value, and only that one is
    // bright on screen
    float worstRatio = 1e9f, worstGlow = 1e9f;
    for (auto* id : { vp::character, vp::meterMode })
    {
        const auto& c = control (map, id);
        auto& group = ed.getLampGroup (id);
        auto* raw = p->apvts.getRawParameterValue (id);
        const auto& buttons = *c["buttons"].getArray();
        for (int i = 0; i < buttons.size(); ++i)
        {
            const auto body = rectOf (buttons[i]["body"]);
            setIndex (*p, id, (i + 1) % buttons.size());
            click (group.getLamp (i), ed, body.getCentre());
            CHECK_MSG (juce::roundToInt (raw->load()) == i, juce::String (id) + " click " + juce::String (i));
        }
        for (int i = 0; i < buttons.size(); ++i)
        {
            setIndex (*p, id, i);
            CHECK (group.getSelected() == i);
            auto img = snapshot (ed);
            for (int j = 0; j < buttons.size(); ++j)
            {
                CHECK (group.getLamp (j).getToggleState() == (j == i));
                if (j == i)
                    continue;
                const auto bodyOn = refToEditor (rectOf (buttons[i]["body"])), bodyOff = refToEditor (rectOf (buttons[j]["body"]));
                const float on = brightnessAt (img, bodyOn), off = brightnessAt (img, bodyOff);
                const float glowOn = redGlowAt (img, bodyOn), glowOff = redGlowAt (img, bodyOff);
                worstRatio = juce::jmin (worstRatio, on / juce::jmax (1.0f, off));
                worstGlow = juce::jmin (worstGlow, glowOn / juce::jmax (0.5f, glowOff));
                CHECK_MSG (on > off * 1.6f && glowOn > glowOff * 3.0f,
                           juce::String (id) + " " + juce::String (i) + " vs " + juce::String (j) + ": brightness " + juce::String (on)
                               + " / " + juce::String (off) + ", red glow " + juce::String (glowOn) + " / " + juce::String (glowOff));
            }
        }
    }
    MEASURE ("worst lit/unlit button brightness", worstRatio);
    MEASURE ("worst lit/unlit button red glow", worstGlow);

    // STEREO LINK / DUAL MONO: one switch, both faces toggle it, exactly one is lit
    {
        auto& link = ed.getLinkSwitch();
        const auto& c = control (map, vp::stereoLink);
        auto* raw = p->apvts.getRawParameterValue (vp::stereoLink);
        const auto lampCore = refToEditor (rectOf (c["lamp"]["rect"]).reduced (20.0f));
        const auto dmCore = refToEditor (rectOf (c["dualMono"]["body"]).reduced (10.0f, 10.0f));

        setPlain (*p, vp::stereoLink, 1.0f);
        CHECK (link.isLinked() && link.getLamp().getToggleState() && ! link.getDualMono().getToggleState());
        auto img = snapshot (ed);
        const float lampOn = brightnessAt (img, lampCore), dmOff = brightnessAt (img, dmCore);

        click (link.getDualMono(), ed, rectOf (c["dualMono"]["body"]).getCentre());
        CHECK (raw->load() < 0.5f);
        CHECK (! link.isLinked() && ! link.getLamp().getToggleState() && link.getDualMono().getToggleState());
        img = snapshot (ed);
        const float lampOff = brightnessAt (img, lampCore), dmOn = brightnessAt (img, dmCore);
        MEASURE ("link lamp lit/dark brightness", lampOn / juce::jmax (1.0f, lampOff));
        MEASURE ("dual mono lit/dark brightness", dmOn / juce::jmax (1.0f, dmOff));
        CHECK (lampOn > lampOff * 1.6f);
        CHECK (dmOn > dmOff * 1.6f);

        click (link.getLamp(), ed, rectOf (c["lamp"]["rect"]).getCentre());
        CHECK (raw->load() > 0.5f);
        click (link.getLamp(), ed, rectOf (c["lamp"]["rect"]).getCentre());
        CHECK (raw->load() < 0.5f);
        click (link.getDualMono(), ed, rectOf (c["dualMono"]["body"]).getCentre());
        CHECK (raw->load() > 0.5f);
    }

    // SIDECHAIN INT / EXT: the lit lamp is the key the detectors really use
    {
        const auto& c = control (map, vp::scExternal);
        auto& group = ed.getLampGroup (vp::scExternal);
        auto& lamps = ed.getSidechainLamps();
        const auto& buttons = *c["buttons"].getArray();
        const auto intCore = refToEditor (rectOf (buttons[0]["body"]).reduced (8.0f));
        const auto extCore = refToEditor (rectOf (buttons[1]["body"]).reduced (8.0f));
        Feeder feed;

        setIndex (*p, vp::scExternal, 0);
        feed (*p, 0.3f, 0.3f, 0.1, 0.5f);
        lamps.refresh();
        CHECK (! p->isUsingExternalSidechain());
        CHECK (group.getLamp (0).getToggleState() && ! group.getLamp (1).getToggleState());
        auto img = snapshot (ed);
        const float intOn = brightnessAt (img, intCore), extOff = brightnessAt (img, extCore);

        click (group.getLamp (1), ed, rectOf (buttons[1]["body"]).getCentre());
        CHECK (p->apvts.getRawParameterValue (vp::scExternal)->load() > 0.5f);
        feed (*p, 0.3f, 0.3f, 0.1, 0.5f);
        lamps.refresh();
        CHECK (p->isUsingExternalSidechain());
        CHECK (! group.getLamp (0).getToggleState() && group.getLamp (1).getToggleState());
        CHECK (group.getLamp (1).getLampAlpha() == 1.0f);
        img = snapshot (ed);
        const float intOff = brightnessAt (img, intCore), extOn = brightnessAt (img, extCore);
        MEASURE ("SC INT lit/dark brightness", intOn / juce::jmax (1.0f, intOff));
        MEASURE ("SC EXT lit/dark brightness", extOn / juce::jmax (1.0f, extOff));
        CHECK (intOn > intOff * 1.6f);
        CHECK (extOn > extOff * 1.6f);

        click (group.getLamp (0), ed, rectOf (buttons[0]["body"]).getCentre());
        CHECK (p->apvts.getRawParameterValue (vp::scExternal)->load() < 0.5f);

        // EXT chosen, but the host feeds no sidechain bus: INT stays lit, EXT only glows faintly
        auto p2 = makeVeilForGui (false);
        std::unique_ptr<juce::AudioProcessorEditor> base2 (p2->createEditor());
        auto& ed2 = dynamic_cast<VeilEditor&> (*base2);
        setIndex (*p2, vp::scExternal, 1);
        Feeder feed2;
        feed2 (*p2, 0.3f, 0.3f, 0.1);
        ed2.getSidechainLamps().refresh();
        auto& g2 = ed2.getLampGroup (vp::scExternal);
        CHECK (! p2->isUsingExternalSidechain());
        CHECK (g2.getLamp (0).getToggleState());
        CHECK (g2.getLamp (1).getToggleState());
        CHECK (g2.getLamp (1).getLampAlpha() < 0.5f);
        img = snapshot (ed2);
        const float armed = brightnessAt (img, extCore);
        MEASURE ("SC EXT armed/dark brightness", armed / juce::jmax (1.0f, extOff));
        CHECK (armed < extOn * 0.8f);
        CHECK (armed >= extOff - 1.0f);
        CHECK (brightnessAt (img, intCore) > intOff * 1.6f);
    }
}

TEST ("VEIL GUI: both needles show the real gain reduction, output level or input level, as METER selects")
{
    auto p = makeVeilForGui();
    p->setUiProperty ("scale", 1.0);
    std::unique_ptr<juce::AudioProcessorEditor> base (p->createEditor());
    auto& ed = dynamic_cast<VeilEditor&> (*base);
    ed.setVisible (true);
    const auto map = VeilEditor::loadUiMap();
    neutral (*p);
    Feeder feed;

    // geometry: the needle points at the printed marks
    for (int ch = 0; ch < 2; ++ch)
    {
        const auto& m = control (map, ch == 0 ? "vuL" : "vuR");
        auto& vu = ed.getVu (ch);
        for (const char* mark : { "-20", "-10", "-3", "0", "3" })
            CHECK_NEAR (juce::radiansToDegrees (vu.angleForVu (juce::String (mark).getFloatValue())), (float) m["scaleDegrees"][mark], 1e-3);
    }

    // the reading the editor computes from the processor's sources
    CHECK_NEAR (VeilEditor::meterReading (vp::MeterMode::gr, 6.0f, 0.1f, 0.1f), -6.0f, 1e-6);
    CHECK (std::isnan (VeilEditor::meterReading (vp::MeterMode::gr, -1.0f, 0.1f, 0.1f)));
    CHECK_NEAR (VeilEditor::meterReading (vp::MeterMode::vu, 0.0f, 0.5f, 0.125f), vp::vuDb (0.125f), 1e-6);
    CHECK_NEAR (VeilEditor::meterReading (vp::MeterMode::inOut, 0.0f, 0.5f, 0.125f), vp::vuDb (0.5f), 1e-6);
    CHECK_NEAR (vp::vuDb (std::pow (10.0f, -18.0f / 20.0f) / std::sqrt (2.0f)), 0.0f, 1e-4); // 0 VU = sine at -18 dBFS

    // GR: both needles leave 0 by the gain reduction the audio really gets.
    // Reference: the same processor settings with the threshold at its top
    // (no gain reduction at these levels), so the level difference is the GR.
    const float peak = 0.2f;
    auto uncompressed = makeVeilForGui();
    neutral (*uncompressed);
    setPlain (*uncompressed, vp::threshold, vp::thresholdMaxDb);
    Feeder feedRef;
    feedRef (*uncompressed, peak, peak, 1.0);
    const auto ref = feedRef (*uncompressed, peak, peak, 0.5);

    int cases = 0;
    for (float threshold : { -16.0f, -10.0f, -4.0f })
    {
        setIndex (*p, vp::meterMode, (int) vp::MeterMode::gr);
        setPlain (*p, vp::threshold, threshold);
        settle (*p, ed, feed, peak, peak, 1.5);
        const auto lv = feed (*p, peak, peak, 0.5);
        settle (*p, ed, feed, peak, peak, 0.5);
        for (int ch = 0; ch < 2; ++ch)
        {
            const double measured = 20.0 * std::log10 ((ch == 0 ? ref.outL : ref.outR) / (ch == 0 ? lv.outL : lv.outR));
            const float shown = -ed.getVu (ch).getNeedleVu();
            const auto label = "threshold " + juce::String (threshold) + " dB ch" + juce::String (ch);
            MEASURE (label + ": measured GR (dB)", measured);
            MEASURE (label + ": needle GR (dB)", shown);
            CHECK_MSG (std::abs (shown - (float) measured) <= 0.5f, label + ": shown " + juce::String (shown) + " vs measured " + juce::String (measured));
        }
        ++cases;
    }
    CHECK (cases == 3);

    // dual mono, signal in the left only: the right needle stays at 0
    setPlain (*p, vp::stereoLink, 0.0f);
    setPlain (*p, vp::threshold, -12.0f);
    settle (*p, ed, feed, 0.25f, 0.002f, 2.0);
    MEASURE ("dual mono, left only: left needle", ed.getVu (0).getNeedleVu());
    MEASURE ("dual mono, left only: right needle", ed.getVu (1).getNeedleVu());
    CHECK (ed.getVu (0).getNeedleVu() < -2.0f);
    CHECK (std::abs (ed.getVu (1).getNeedleVu()) < 0.2f);
    setPlain (*p, vp::stereoLink, 1.0f);
    settle (*p, ed, feed, 0.25f, 0.002f, 2.0);
    MEASURE ("linked, left only: right needle", ed.getVu (1).getNeedleVu());
    CHECK (ed.getVu (1).getNeedleVu() < -2.0f);

    // VU: output level; IN/OUT: input level (0 VU = -18 dBFS sine)
    for (float inPeak : { 0.05f, 0.126f, 0.2f })
    {
        setPlain (*p, vp::threshold, -8.0f);
        setIndex (*p, vp::meterMode, (int) vp::MeterMode::vu);
        settle (*p, ed, feed, inPeak, inPeak, 1.5);
        auto lv = feed (*p, inPeak, inPeak, 0.5);
        settle (*p, ed, feed, inPeak, inPeak, 0.5);
        const float outVu = vp::vuDb ((float) lv.outL);
        MEASURE ("input peak " + juce::String (inPeak) + ": output VU", outVu);
        MEASURE ("input peak " + juce::String (inPeak) + ": VU needle", ed.getVu (0).getNeedleVu());
        CHECK (std::abs (ed.getVu (0).getNeedleVu() - outVu) <= 0.3f);

        setIndex (*p, vp::meterMode, (int) vp::MeterMode::inOut);
        settle (*p, ed, feed, inPeak, inPeak, 1.0);
        const float inVu = vp::vuDb (inPeak / std::sqrt (2.0f));
        MEASURE ("input peak " + juce::String (inPeak) + ": input VU", inVu);
        MEASURE ("input peak " + juce::String (inPeak) + ": IN/OUT needle", ed.getVu (0).getNeedleVu());
        CHECK (std::abs (ed.getVu (0).getNeedleVu() - inVu) <= 0.3f);
        CHECK (std::abs (ed.getVu (1).getNeedleVu() - inVu) <= 0.3f);
    }

    // on screen: the needle really moves between modes (pixels differ)
    {
        setIndex (*p, vp::meterMode, (int) vp::MeterMode::gr);
        settle (*p, ed, feed, 0.05f, 0.05f, 1.5);
        const auto face = refToEditor (rectOf (control (map, "vuL")["face"]));
        const auto a = snapshot (ed).getClippedImage (face.toNearestInt());
        setIndex (*p, vp::meterMode, (int) vp::MeterMode::inOut);
        settle (*p, ed, feed, 0.05f, 0.05f, 1.5);
        const auto b = snapshot (ed).getClippedImage (face.toNearestInt());
        int differing = 0;
        for (int y = 0; y < a.getHeight(); ++y)
            for (int x = 0; x < a.getWidth(); ++x)
                differing += a.getPixelAt (x, y) != b.getPixelAt (x, y) ? 1 : 0;
        MEASURE ("VU face pixels changed GR -> IN/OUT", differing);
        CHECK (differing > 200);
    }

    // no audio: the needles rest at 0 in GR, at the left pin in level modes
    {
        auto q = makeVeilForGui();
        std::unique_ptr<juce::AudioProcessorEditor> b (q->createEditor());
        auto& e = dynamic_cast<VeilEditor&> (*b);
        CHECK_NEAR (e.getVu (0).getNeedleVu(), 0.0f, 1e-4);
        setIndex (*q, vp::meterMode, (int) vp::MeterMode::vu);
        CHECK (e.getVu (0).getNeedleVu() <= -20.0f);
    }

    // METER never changes the audio: fresh processors that differ only in the mode
    {
        std::array<std::vector<float>, 3> out;
        for (int m = 0; m < 3; ++m)
        {
            auto q = makeVeilForGui();
            std::unique_ptr<juce::AudioProcessorEditor> b (q->createEditor());
            neutral (*q);
            setPlain (*q, vp::threshold, -12.0f);
            setIndex (*q, vp::meterMode, m);
            Feeder f;
            f (*q, 0.3f, 0.2f, 0.5);
            out[(size_t) m] = f.lastOutL;
        }
        CHECK (out[0] == out[1] && out[1] == out[2]);
    }
}

TEST ("VEIL GUI: the spectrum shows the real output level per band; quality and Off never touch the audio")
{
    auto p = makeVeilForGui();
    p->setUiProperty ("scale", 1.0);
    std::unique_ptr<juce::AudioProcessorEditor> base (p->createEditor());
    auto& ed = dynamic_cast<VeilEditor&> (*base);
    ed.setVisible (true);
    auto& spectrum = ed.getSpectrum();
    const auto map = VeilEditor::loadUiMap();
    neutral (*p);

    // default quality Normal, tap running while the display is shown
    CHECK (spectrum.getQuality() == ui::SpectrumDisplay::Quality::normal);
    CHECK (p->spectrum.isActive());

    // axis: the grid frequencies fall where the map's screen says (log 20 Hz .. 20 kHz)
    const auto screen = refToEditor (rectOf (control (map, "spectrum")["screen"]));
    const float left = spectrum.getX() + spectrum.xForFrequency (20.0f), right = spectrum.getX() + spectrum.xForFrequency (20000.0f);
    CHECK (left >= screen.getX() && right <= screen.getRight());
    CHECK_NEAR (spectrum.getX() + spectrum.xForFrequency (632.456f), (left + right) * 0.5f, 0.05);
    CHECK_NEAR (spectrum.frequencyForX (spectrum.xForFrequency (1000.0f)), 1000.0f, 0.5);

    // a sine reads the level the plugin really outputs, at its own frequency
    for (double hz : { 100.0, 1000.0, 5000.0 })
        for (auto q : { ui::SpectrumDisplay::Quality::eco, ui::SpectrumDisplay::Quality::normal, ui::SpectrumDisplay::Quality::high })
        {
            spectrum.setQuality (q);
            Feeder feed;
            feed.hz = hz;
            const auto lv = feed (*p, 0.1f, 0.1f, 0.4);
            spectrum.processPending();
            const float expected = juce::Decibels::gainToDecibels ((float) lv.outL * std::sqrt (2.0f));
            const float shown = spectrum.getFrameDb ((float) hz);
            const auto label = ui::SpectrumDisplay::qualityName (q) + " " + juce::String (hz) + " Hz";
            MEASURE (label + ": output sine (dBFS peak)", expected);
            MEASURE (label + ": spectrum (dB)", shown);
            CHECK_MSG (std::abs (shown - expected) <= 1.0f, label + ": " + juce::String (shown) + " vs " + juce::String (expected));
            // a pure tone stays a narrow peak: 30 dB down two octaves away (Eco's
            // 2048-point FFT has 23 Hz bins, so below 100 Hz it cannot separate
            // anything; there it is checked two octaves up)
            const float away = q == ui::SpectrumDisplay::Quality::eco && hz < 200.0 ? 4.0f : 0.25f;
            CHECK_MSG (spectrum.getFrameDb ((float) hz * away) < shown - 30.0f, label + " leaks");
        }
    MEASURE ("FFT size at High", spectrum.getFftSize());
    CHECK (spectrum.getFftSize() == 8192);

    // Off: the audio thread stops feeding it, nothing more is analysed
    spectrum.setQuality (ui::SpectrumDisplay::Quality::off);
    CHECK (! p->spectrum.isActive());
    const int frames = spectrum.getFramesAnalysed();
    Feeder feed;
    feed (*p, 0.1f, 0.1f, 0.3);
    spectrum.processPending();
    CHECK (spectrum.getFramesAnalysed() == frames);

    // hidden: the tap is off too; shown again: back on
    spectrum.setQuality (ui::SpectrumDisplay::Quality::eco);
    CHECK (p->spectrum.isActive());
    spectrum.setVisible (false);
    CHECK (! p->spectrum.isActive());
    spectrum.setVisible (true);
    CHECK (p->spectrum.isActive());

    // the chosen quality is a UI setting, stored with the plugin and restored with the editor
    ed.getSpectrumQualityButton().onQualityChanged (ui::SpectrumDisplay::Quality::high);
    base.reset();
    CHECK (! p->spectrum.isActive()); // a closed editor leaves the audio thread alone
    base.reset (p->createEditor());
    CHECK (dynamic_cast<VeilEditor&> (*base).getSpectrum().getQuality() == ui::SpectrumDisplay::Quality::high);
    base.reset();

    // the audio is identical with the analyser Off, Eco or High
    std::array<std::vector<float>, 3> out;
    const ui::SpectrumDisplay::Quality qs[] = { ui::SpectrumDisplay::Quality::off, ui::SpectrumDisplay::Quality::eco,
                                                ui::SpectrumDisplay::Quality::high };
    for (int i = 0; i < 3; ++i)
    {
        auto q = makeVeilForGui();
        q->setUiProperty ("analyzerQuality", (int) qs[i]);
        std::unique_ptr<juce::AudioProcessorEditor> b (q->createEditor());
        b->setVisible (true);
        neutral (*q);
        setPlain (*q, vp::threshold, -12.0f);
        Feeder f;
        f (*q, 0.3f, 0.2f, 0.5);
        dynamic_cast<VeilEditor&> (*b).getSpectrum().processPending();
        out[(size_t) i] = f.lastOutL;
    }
    CHECK (out[0] == out[1] && out[1] == out[2]);
}
