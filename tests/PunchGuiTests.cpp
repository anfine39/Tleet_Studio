#include "TestFramework.h"
#include "../plugins/PUNCH/PluginProcessor.h"
#include "../plugins/PUNCH/PluginEditor.h"
#include "../plugins/PUNCH/Parameters.h"

// GUI tests for PUNCH (plugins/PUNCH/GUI_TEST_PLAN.md). Every control is
// checked against UI_MAP.json and against the real parameter / DSP state.

using namespace tleet;
using namespace tleet::test;
using tleet::punch::PunchProcessor;
using tleet::punch::PunchEditor;

namespace
{
    constexpr double sr = 48000.0;
    constexpr int block = 256;

    std::unique_ptr<PunchProcessor> makePunchForGui()
    {
        auto p = std::make_unique<PunchProcessor>();
        p->setRateAndBufferSizeDetails (sr, block);
        p->prepareToPlay (sr, block);
        return p;
    }

    void setPlain (PunchProcessor& p, const char* id, float plain)
    {
        auto* r = p.apvts.getParameter (id);
        r->setValueNotifyingHost (r->convertTo0to1 (plain));
    }

    void setRatio (PunchProcessor& p, float ratio)
    {
        setPlain (p, punch::params::ratio, punch::params::plainFromRatio (ratio));
    }

    // Steady 1 kHz sine through the plugin; returns (input RMS, output RMS) of the last half.
    std::pair<double, double> feedSine (PunchProcessor& p, float peak, double seconds)
    {
        juce::AudioBuffer<float> buf (2, block);
        juce::MidiBuffer midi;
        static double phase = 0.0;
        const int blocks = juce::jmax (1, (int) (seconds * sr / block));
        double in2 = 0, out2 = 0; long n = 0;
        for (int b = 0; b < blocks; ++b)
        {
            double inBlock = 0;
            for (int i = 0; i < block; ++i)
            {
                const float x = peak * (float) std::sin (phase);
                phase += 2.0 * juce::MathConstants<double>::pi * 1000.0 / sr;
                buf.setSample (0, i, x);
                buf.setSample (1, i, x);
                inBlock += (double) x * x;
            }
            p.processBlock (buf, midi);
            if (b >= blocks / 2)
            {
                in2 += inBlock;
                for (int i = 0; i < block; ++i)
                    out2 += 0.5 * ((double) buf.getSample (0, i) * buf.getSample (0, i) + (double) buf.getSample (1, i) * buf.getSample (1, i));
                n += block;
            }
        }
        return { std::sqrt (in2 / (double) juce::jmax (1L, n)), std::sqrt (out2 / (double) juce::jmax (1L, n)) };
    }

    juce::Point<float> refToEditor (float x, float y)
    {
        return { x * PunchEditor::skinScale, ui::TleetEditorBase::toolbarHeight + y * PunchEditor::skinScale };
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

    // A fixed, documented test setting: clean VCA, hard knee, no blend, no trim.
    void neutral (PunchProcessor& p)
    {
        setPlain (p, punch::params::grit, 0.0f);
        setPlain (p, punch::params::mix, 100.0f);
        setPlain (p, punch::params::output, 0.0f);
        setPlain (p, punch::params::overEasy, 0.0f);
    }

    // Lets the meter settle on a steady signal (pull + ballistics as the UI timer does).
    void settleMeter (PunchProcessor& p, PunchEditor& ed, float peak, int frames = 40)
    {
        for (int i = 0; i < frames; ++i)
        {
            feedSine (p, peak, 1.0 / 30.0);
            ed.getMeter().refresh (1.0f / 30.0f);
        }
    }

    const char* const knobIds[] = { punch::params::threshold, punch::params::ratio, punch::params::grit,
                                    punch::params::mix, punch::params::output };
}

TEST ("PUNCH GUI: editor opens at every scale; screenshots of default and representative states")
{
    auto p = makePunchForGui();
    for (float scale : { 0.75f, 1.0f, 1.5f, 2.0f })
    {
        p->setUiProperty ("scale", scale);
        std::unique_ptr<juce::AudioProcessorEditor> ed (p->createEditor());
        CHECK (dynamic_cast<PunchEditor*> (ed.get()) != nullptr);
        const int expectedW = juce::roundToInt (PunchEditor::baseWidth * scale);
        CHECK_MSG (std::abs (ed->getWidth() - expectedW) <= 1, "width at scale " + juce::String (scale));
        saveSnapshot (*ed, "PUNCH_editor_" + juce::String (juce::roundToInt (scale * 100)));
    }

    p->setUiProperty ("scale", 1.0);
    std::unique_ptr<juce::AudioProcessorEditor> base (p->createEditor());
    auto& ed = dynamic_cast<PunchEditor&> (*base);
    neutral (*p);
    struct State { const char* name; float peak; std::function<void()> apply; };
    const State states[] = {
        { "compressing", 0.5f, [&] { setPlain (*p, punch::params::threshold, -10.0f); setRatio (*p, 4.0f); } },
        { "smashed",     0.9f, [&] { setPlain (*p, punch::params::threshold, -20.0f); setRatio (*p, std::numeric_limits<float>::infinity());
                                     setPlain (*p, punch::params::grit, 100.0f); setPlain (*p, punch::params::overEasy, 1.0f);
                                     setPlain (*p, punch::params::output, 8.0f); } },
        { "gentle",      0.2f, [&] { neutral (*p); setPlain (*p, punch::params::threshold, -5.0f); setRatio (*p, 2.0f);
                                     setPlain (*p, punch::params::overEasy, 1.0f); setPlain (*p, punch::params::mix, 60.0f); } },
    };
    for (auto& st : states)
    {
        st.apply();
        settleMeter (*p, ed, st.peak);
        saveSnapshot (ed, juce::String ("PUNCH_state_") + st.name);
    }
}

TEST ("PUNCH GUI: every mapped control sits at its mockup position and owns its hitbox")
{
    auto p = makePunchForGui();
    p->setUiProperty ("scale", 1.0);
    std::unique_ptr<juce::AudioProcessorEditor> base (p->createEditor());
    auto& ed = dynamic_cast<PunchEditor&> (*base);
    ed.setVisible (true);
    const auto map = PunchEditor::loadUiMap();

    int checked = 0;
    std::vector<std::pair<juce::String, juce::Rectangle<float>>> areas;
    for (auto& c : *map["controls"].getArray())
    {
        const auto type = c["type"].toString();
        if (type != "ROTARY" && type != "LAMP_SWITCH")
            continue;
        const auto hb = rectOf (c["hitbox"]);
        areas.push_back ({ c["id"].toString(), hb });
        juce::Component* target = type == "ROTARY" ? static_cast<juce::Component*> (&ed.getKnob (c["parameter"].toString()))
                                                   : static_cast<juce::Component*> (&ed.getOverEasySwitch());
        for (auto pt : { hb.getCentre(), hb.getTopLeft() + juce::Point<float> (3, 3), hb.getBottomRight() - juce::Point<float> (3, 3),
                         hb.getTopRight() + juce::Point<float> (-3, 3), hb.getBottomLeft() + juce::Point<float> (3, -3) })
        {
            auto* hit = ed.getComponentAt (refToEditor (pt.x, pt.y).toInt());
            CHECK_MSG (hit == target, c["id"].toString() + " at " + pt.toString() + " hit " + (hit ? hit->getBounds().toString() : juce::String ("null")));
        }
        if (type == "ROTARY")
        {
            // the pointer turns around the cap centre measured on the artwork
            const juce::Point<float> centreRef ((float) c["centre"][0], (float) c["centre"][1]);
            const auto actual = target->getBounds().getPosition().toFloat() + (centreRef - hb.getPosition()) * PunchEditor::skinScale;
            CHECK (actual.getDistanceFrom (refToEditor (centreRef.x, centreRef.y)) <= 1.0f);
        }
        ++checked;
    }
    MEASURE ("hitboxes checked", checked);
    CHECK (checked == 6);

    for (size_t i = 0; i < areas.size(); ++i)
        for (size_t j = i + 1; j < areas.size(); ++j)
            CHECK_MSG (! areas[i].second.intersects (areas[j].second), areas[i].first + " overlaps " + areas[j].first);

    // the meter never takes clicks
    const auto meterHb = rectOf (control (map, "grMeter")["hitbox"]);
    auto* hit = ed.getComponentAt (refToEditor (meterHb.getCentreX(), meterHb.getCentreY()).toInt());
    CHECK (hit != &ed.getMeter());
}

TEST ("PUNCH GUI: every scale number sits at the angle where the knob really has that value")
{
    auto p = makePunchForGui();
    std::unique_ptr<juce::AudioProcessorEditor> base (p->createEditor());
    auto& ed = dynamic_cast<PunchEditor&> (*base);
    const auto map = PunchEditor::loadUiMap();

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

    auto texts = [&] (const char* id)
    {
        juce::StringArray s;
        for (auto& l : ed.getScaleLabels (id)) s.add (l.text);
        return s.joinIntoString (" ");
    };
    CHECK_MSG (texts (punch::params::threshold) == "-20 -10 0 +10", texts (punch::params::threshold));
    CHECK_MSG (texts (punch::params::ratio) == juce::String::fromUTF8 ("1 2 4 8 \xe2\x88\x9e"), texts (punch::params::ratio));
    CHECK_MSG (texts (punch::params::mix) == "0 50 100", texts (punch::params::mix));
    CHECK_MSG (texts (punch::params::output) == "-20 -10 0 +10 +20", texts (punch::params::output));

    // the ratio numbers are the real compression ratio at their positions
    auto* ratio = p->apvts.getParameter (punch::params::ratio);
    for (auto& l : ed.getScaleLabels (punch::params::ratio))
    {
        const float r = punch::params::ratioFromPlain (l.value);
        const auto shown = ratio->getText (ratio->convertTo0to1 (l.value), 32);
        CHECK_MSG (shown.startsWith (l.text), "ratio label '" + l.text + "' vs parameter text '" + shown + "'");
        if (l.text == "8")
            CHECK_NEAR (r, 8.0f, 1e-3);
    }
}

TEST ("PUNCH GUI: knobs and the OverEasy switch drive the real parameters and follow host automation")
{
    auto p = makePunchForGui();
    p->setUiProperty ("scale", 1.0);
    std::unique_ptr<juce::AudioProcessorEditor> base (p->createEditor());
    auto& ed = dynamic_cast<PunchEditor&> (*base);
    ed.setVisible (true);

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

    // OverEasy: a click on the rocker and one on the "ON" caption both toggle;
    // automation moves the switch; the lamp is lit on screen only when on
    const auto map = PunchEditor::loadUiMap();
    const auto& sw = control (map, "overEasy");
    const auto hb = rectOf (sw["hitbox"]);
    const auto lampCore = rectOf (sw["lamp"]["core"]);
    auto& s = ed.getOverEasySwitch();
    auto* oe = p->apvts.getRawParameterValue (punch::params::overEasy);

    setPlain (*p, punch::params::overEasy, 0.0f);
    CHECK (! s.getToggleState());
    click (s, ed, hb.getCentre());
    CHECK (oe->load() > 0.5f);
    CHECK (s.getToggleState());
    click (s, ed, { hb.getRight() - 15.0f, hb.getCentreY() });
    CHECK (oe->load() < 0.5f);

    auto lampBrightness = [&]
    {
        auto img = ed.createComponentSnapshot (ed.getLocalBounds(), true, 1.0f);
        const auto tl = refToEditor (lampCore.getX(), lampCore.getY());
        return brightnessAt (img, { tl.x, tl.y, lampCore.getWidth() * PunchEditor::skinScale, lampCore.getHeight() * PunchEditor::skinScale });
    };
    setPlain (*p, punch::params::overEasy, 0.0f);
    const float off = lampBrightness();
    setPlain (*p, punch::params::overEasy, 1.0f);
    CHECK (s.getToggleState());
    const float on = lampBrightness();
    MEASURE ("OverEasy lamp on/off brightness", on / juce::jmax (1.0f, off));
    CHECK (on > off * 3.0f);
}

TEST ("PUNCH GUI: the GR LED meter shows the gain reduction the VCA really applies")
{
    auto p = makePunchForGui();
    p->setUiProperty ("scale", 1.0);
    std::unique_ptr<juce::AudioProcessorEditor> base (p->createEditor());
    auto& ed = dynamic_cast<PunchEditor&> (*base);
    ed.setVisible (true);
    neutral (*p);
    setRatio (*p, 4.0f);
    auto& meter = ed.getMeter();

    // segments follow the printed scale (1 dB per tick below 4 dB, 2 dB above)
    const auto map = PunchEditor::loadUiMap();
    const auto& m = control (map, "grMeter");
    const auto mb = rectOf (m["hitbox"]);
    for (auto* key : { "0", "1", "3", "4", "6", "10", "18", "20" })
    {
        const float x = (float) m["scale"][key] - mb.getX();
        CHECK_NEAR (meter.xForDb (juce::String (key).getFloatValue()), x * PunchEditor::skinScale, 1e-3);
    }
    CHECK (meter.getNumSegments() == 24);
    CHECK (meter.litSegmentsFor (0.2f) == 0);
    CHECK (meter.litSegmentsFor (0.3f) == 1);    // half of the first 0.5 dB segment
    CHECK (meter.litSegmentsFor (4.6f) == 9);    // 0..4 dB in halves, then 4..5
    CHECK (meter.litSegmentsFor (25.0f) == 24);

    int cases = 0;
    for (float threshold : { -20.0f, -12.0f, -6.0f, 0.0f })
    {
        setPlain (*p, punch::params::threshold, threshold);
        settleMeter (*p, ed, 0.5f, 45);
        // independent measurement: the level drop through the plugin is the applied gain
        const auto [inRms, outRms] = feedSine (*p, 0.5f, 0.5);
        settleMeter (*p, ed, 0.5f, 10);
        const double measured = 20.0 * std::log10 (inRms / outRms);
        const float shown = meter.getDisplayedDb();
        MEASURE ("threshold " + juce::String (threshold) + " dB: measured GR (dB)", measured);
        MEASURE ("threshold " + juce::String (threshold) + " dB: shown GR (dB)", shown);
        CHECK_MSG (std::abs (shown - (float) measured) <= 0.3f, "shown " + juce::String (shown) + " vs measured " + juce::String (measured));
        const int lit = meter.getLitSegments();
        CHECK (lit >= meter.litSegmentsFor ((float) measured - 0.3f) && lit <= meter.litSegmentsFor ((float) measured + 0.3f));

        // on screen: the last lit segment is bright, the next one dark
        if (lit > 0 && lit < meter.getNumSegments())
        {
            auto img = ed.createComponentSnapshot (ed.getLocalBounds(), true, 1.0f);
            const auto origin = meter.getBounds().getPosition().toFloat();
            const float on = brightnessAt (img, meter.getSegmentBounds (lit - 1).reduced (1.0f) + origin);
            const float off = brightnessAt (img, meter.getSegmentBounds (lit).reduced (1.0f) + origin);
            MEASURE ("lit/unlit brightness", on / juce::jmax (1.0f, off));
            CHECK (on > off * 2.0f);
        }
        ++cases;
    }
    CHECK (cases == 4);

    // silence: the program-dependent release recovers and the bar follows it down
    for (int i = 0; i < 45; ++i)
    {
        feedSine (*p, 0.0f, 1.0 / 30.0);
        meter.refresh (1.0f / 30.0f);
    }
    CHECK (meter.getLitSegments() == 0);
}
