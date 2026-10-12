#include "TestFramework.h"
#include "../plugins/GLUE/PluginProcessor.h"
#include "../plugins/GLUE/PluginEditor.h"
#include "../plugins/GLUE/Parameters.h"

// GUI tests for GLUE (plugins/GLUE/GUI_TEST_PLAN.md). Every control is
// checked against UI_MAP.json and against the real parameter / DSP state.

using namespace tleet;
using namespace tleet::test;
using tleet::glue::GlueProcessor;
using tleet::glue::GlueEditor;
using tleet::glue::GrLedMeter;

namespace
{
    constexpr double sr = 48000.0;
    constexpr int block = 256;

    std::unique_ptr<GlueProcessor> makeGlueForGui (bool sidechain = false)
    {
        auto p = std::make_unique<GlueProcessor>();
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

    void setPlain (GlueProcessor& p, const char* id, float plain)
    {
        auto* r = p.apvts.getParameter (id);
        r->setValueNotifyingHost (r->convertTo0to1 (plain));
    }

    void setChoice (GlueProcessor& p, const char* id, const juce::String& name)
    {
        auto* r = p.apvts.getParameter (id);
        const auto names = r->getAllValueStrings();
        const int i = names.indexOf (name);
        jassert (i >= 0);
        r->setValueNotifyingHost ((float) i / (float) juce::jmax (1, names.size() - 1));
    }

    // Steady 1 kHz sine through the plugin; returns (input RMS, output RMS) of the last half.
    std::pair<double, double> feedSine (GlueProcessor& p, float peak, double seconds, float keyPeak = -1.0f)
    {
        const int total = p.getTotalNumInputChannels();
        juce::AudioBuffer<float> buf (juce::jmax (2, total), block);
        juce::MidiBuffer midi;
        static double phase = 0.0;
        const int blocks = (int) (seconds * sr / block);
        double in2 = 0, out2 = 0; long n = 0;
        for (int b = 0; b < blocks; ++b)
        {
            buf.clear();
            double inBlock = 0;
            for (int i = 0; i < block; ++i)
            {
                const float x = peak * (float) std::sin (phase);
                phase += 2.0 * juce::MathConstants<double>::pi * 1000.0 / sr;
                buf.setSample (0, i, x);
                buf.setSample (1, i, x);
                if (keyPeak >= 0.0f && total >= 4)
                {
                    buf.setSample (2, i, keyPeak * (float) std::sin (phase * 0.25));
                    buf.setSample (3, i, keyPeak * (float) std::sin (phase * 0.25));
                }
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
        return { x * GlueEditor::skinScale, ui::TleetEditorBase::toolbarHeight + y * GlueEditor::skinScale };
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

    // A fixed, documented test setting: clean VCA, no parallel blend, no trims.
    void neutral (GlueProcessor& p)
    {
        setPlain (p, glue::params::makeup, 0.0f);
        setPlain (p, glue::params::output, 0.0f);
        setPlain (p, glue::params::mix, 100.0f);
        setPlain (p, glue::params::dark, 0.0f);
        setPlain (p, glue::params::crush, 0.0f);
        setChoice (p, glue::params::character, "Clean");
    }

    // Lets the meter settle on a steady signal (pull + ballistics as the UI timer does).
    void settleMeter (GlueProcessor& p, GlueEditor& ed, float peak, int frames = 40, float keyPeak = -1.0f)
    {
        for (int i = 0; i < frames; ++i)
        {
            feedSine (p, peak, 1.0 / 30.0, keyPeak);
            ed.getMeter().refresh (1.0f / 30.0f);
        }
    }
}

TEST ("GLUE GUI: editor opens at every scale; screenshots of default and representative states")
{
    auto p = makeGlueForGui();
    for (float scale : { 0.75f, 1.0f, 1.5f, 2.0f })
    {
        p->setUiProperty ("scale", scale);
        std::unique_ptr<juce::AudioProcessorEditor> ed (p->createEditor());
        CHECK (dynamic_cast<GlueEditor*> (ed.get()) != nullptr);
        const int expectedW = juce::roundToInt (GlueEditor::baseWidth * scale);
        CHECK_MSG (std::abs (ed->getWidth() - expectedW) <= 1, "width at scale " + juce::String (scale));
        saveSnapshot (*ed, "GLUE_editor_" + juce::String (juce::roundToInt (scale * 100)));
    }

    p->setUiProperty ("scale", 1.0);
    std::unique_ptr<juce::AudioProcessorEditor> base (p->createEditor());
    auto& ed = dynamic_cast<GlueEditor&> (*base);
    neutral (*p);
    struct State { const char* name; float peak; std::function<void()> apply; };
    const State states[] = {
        { "compressing", 0.5f, [&] { setPlain (*p, glue::params::threshold, -20.0f); setChoice (*p, glue::params::ratio, "4"); } },
        { "heavy_burn",  0.9f, [&] { setPlain (*p, glue::params::threshold, -20.0f);
                                     setChoice (*p, glue::params::ratio, juce::String::fromUTF8 ("\xe2\x88\x9e"));
                                     setChoice (*p, glue::params::character, "Burn"); setPlain (*p, glue::params::crush, 70.0f);
                                     setPlain (*p, glue::params::makeup, 12.0f); setPlain (*p, glue::params::scHpf, 150.0f); } },
        { "gentle",      0.2f, [&] { neutral (*p); setPlain (*p, glue::params::threshold, -10.0f); setChoice (*p, glue::params::ratio, "2");
                                     setPlain (*p, glue::params::mix, 60.0f); setPlain (*p, glue::params::dark, 40.0f); } },
    };
    for (auto& st : states)
    {
        st.apply();
        settleMeter (*p, ed, st.peak);
        saveSnapshot (ed, juce::String ("GLUE_state_") + st.name);
    }
}

TEST ("GLUE GUI: every mapped control sits at its mockup position and owns its hitbox")
{
    auto p = makeGlueForGui();
    p->setUiProperty ("scale", 1.0);
    std::unique_ptr<juce::AudioProcessorEditor> base (p->createEditor());
    auto& ed = dynamic_cast<GlueEditor&> (*base);
    ed.setVisible (true);
    const auto map = GlueEditor::loadUiMap();

    int checked = 0;
    std::vector<std::pair<juce::String, juce::Rectangle<float>>> areas;
    for (auto& c : *map["controls"].getArray())
    {
        const auto type = c["type"].toString();
        if (type != "ROTARY" && type != "ROTARY_STEPPED")
            continue;
        const auto hb = rectOf (c["hitbox"]);
        areas.push_back ({ c["id"].toString(), hb });
        auto& k = ed.getKnob (c["parameter"].toString());
        for (auto pt : { hb.getCentre(), hb.getTopLeft() + juce::Point<float> (3, 3), hb.getBottomRight() - juce::Point<float> (3, 3),
                         hb.getTopRight() + juce::Point<float> (-3, 3), hb.getBottomLeft() + juce::Point<float> (3, -3) })
        {
            auto* hit = ed.getComponentAt (refToEditor (pt.x, pt.y).toInt());
            CHECK_MSG (hit == &k, c["id"].toString() + " at " + pt.toString() + " hit " + (hit ? hit->getBounds().toString() : juce::String ("null")));
        }
        // the pointer turns around the cap centre measured on the artwork
        const juce::Point<float> centreRef ((float) c["centre"][0], (float) c["centre"][1]);
        const auto actual = k.getBounds().getPosition().toFloat() + (centreRef - hb.getPosition()) * GlueEditor::skinScale;
        CHECK (actual.getDistanceFrom (refToEditor (centreRef.x, centreRef.y)) <= 1.0f);
        ++checked;
    }
    MEASURE ("knob hitboxes checked", checked);
    CHECK (checked == 11);

    for (size_t i = 0; i < areas.size(); ++i)
        for (size_t j = i + 1; j < areas.size(); ++j)
            CHECK_MSG (! areas[i].second.intersects (areas[j].second), areas[i].first + " overlaps " + areas[j].first);

    // the meter and the arc lamp never take clicks from the knobs
    const auto& ch = control (map, "character");
    auto* hit = ed.getComponentAt (refToEditor ((float) ch["centre"][0], (float) ch["centre"][1] - 30.0f).toInt());
    CHECK (hit == &ed.getKnob (glue::params::character));
    const auto meterHb = rectOf (control (map, "grMeter")["hitbox"]);
    hit = ed.getComponentAt (refToEditor (meterHb.getCentreX(), meterHb.getCentreY()).toInt());
    CHECK (hit != &ed.getMeter());

    // the sidechain switch is in the toolbar, clear of the other buttons
    auto& sc = ed.getSidechainButton();
    CHECK (sc.isShowing());
    CHECK (sc.getBounds().getBottom() <= ui::TleetEditorBase::toolbarHeight);
}

TEST ("GLUE GUI: every scale number sits at the angle where the knob really has that value")
{
    auto p = makeGlueForGui();
    std::unique_ptr<juce::AudioProcessorEditor> base (p->createEditor());
    auto& ed = dynamic_cast<GlueEditor&> (*base);
    const auto map = GlueEditor::loadUiMap();

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
            float err = std::abs (printed - l.angleDegrees);
            // (a label straight below would wrap around; none is there)
            worstLabel = juce::jmax (worstLabel, err);
            CHECK_MSG (err <= 1.0f, id + " '" + l.text + "' printed at " + juce::String (printed, 2) + " deg, value at " + juce::String (l.angleDegrees, 2));

            // ... and the knob turned to that value points exactly there
            param->setValueNotifyingHost (param->convertTo0to1 (l.value));
            const float pointer = t0 + knob.getNormalisedValue() * (t1 - t0);
            worstPointer = juce::jmax (worstPointer, std::abs (pointer - l.angleDegrees));
            CHECK_MSG (std::abs (pointer - l.angleDegrees) <= 0.05f, id + " '" + l.text + "' pointer " + juce::String (pointer, 3));

            // and the number is the value's own text
            auto expected = param->getText (param->convertTo0to1 (l.value), 32);
            CHECK_MSG (GlueEditor::labelText (id, l.value, c["scaleLabels"]["values"].isString() ? expected : juce::String()) == l.text,
                       id + " text '" + l.text + "'");
            ++labels;
        }

        if (c.hasProperty ("stepDots"))
        {
            const auto& dots = ed.getStepDots (id);
            CHECK_MSG ((int) dots.size() == param->getNumSteps(), id + " one dot per step");
            CHECK ((int) list.size() == param->getNumSteps());
            for (size_t i = 0; i < dots.size() && i < list.size(); ++i)
                CHECK (std::abs (dots[i].angleDegrees - list[i].angleDegrees) < 1e-3f);
        }
    }
    MEASURE ("labels checked", labels);
    MEASURE ("worst label angle error (deg)", worstLabel);
    MEASURE ("worst pointer-vs-label error (deg)", worstPointer);

    // the printed values for the continuous knobs, as on the artwork
    auto texts = [&] (const char* id)
    {
        juce::StringArray s;
        for (auto& l : ed.getScaleLabels (id)) s.add (l.text);
        return s.joinIntoString (" ");
    };
    CHECK (texts (glue::params::threshold) == "-20 -10 0 +10");
    CHECK (texts (glue::params::makeup) == "-10 0 +10 +20");
    CHECK (texts (glue::params::output) == "-20 0 +20");
    CHECK (texts (glue::params::mix) == "0 50 100");
    CHECK (texts (glue::params::scHpf) == "20 50 100 300");
    MEASURE ("attack labels", texts (glue::params::attack));
    MEASURE ("release labels", texts (glue::params::release));
    MEASURE ("ratio labels", texts (glue::params::ratio));
}

TEST ("GLUE GUI: knobs drive the real parameters and follow host automation")
{
    auto p = makeGlueForGui();
    p->setUiProperty ("scale", 1.0);
    std::unique_ptr<juce::AudioProcessorEditor> base (p->createEditor());
    auto& ed = dynamic_cast<GlueEditor&> (*base);
    ed.setVisible (true);

    for (auto* id : { glue::params::threshold, glue::params::attack, glue::params::release, glue::params::ratio, glue::params::makeup,
                      glue::params::scHpf, glue::params::mix, glue::params::character, glue::params::dark, glue::params::crush, glue::params::output })
    {
        auto* r = p->apvts.getParameter (id);
        auto& k = ed.getKnob (id);
        for (float n : { 0.0f, 0.37f, 1.0f })
        {
            r->setValueNotifyingHost (n);
            CHECK_MSG (std::abs (k.getNormalisedValue() - r->getValue()) < 1e-5f, juce::String (id) + " follows automation");
        }
        // keyboard: one step up from the bottom changes the parameter
        r->setValueNotifyingHost (0.0f);
        k.keyPressed (juce::KeyPress (juce::KeyPress::upKey));
        CHECK_MSG (r->getValue() > 0.0f, juce::String (id) + " up key");
    }
}

TEST ("GLUE GUI: the GR LED meter shows the gain reduction the VCA really applies")
{
    auto p = makeGlueForGui();
    p->setUiProperty ("scale", 1.0);
    std::unique_ptr<juce::AudioProcessorEditor> base (p->createEditor());
    auto& ed = dynamic_cast<GlueEditor&> (*base);
    ed.setVisible (true);
    neutral (*p);
    setChoice (*p, glue::params::ratio, "4");
    auto& meter = ed.getMeter();

    // segments follow the printed scale: segment k spans the k..k+1 dB marks
    const auto map = GlueEditor::loadUiMap();
    const auto& m = control (map, "grMeter");
    const auto mb = rectOf (m["hitbox"]);
    for (auto* key : { "0", "4", "8", "12", "16", "20" })
    {
        const float x = (float) m["scale"][key] - mb.getX();
        CHECK_NEAR (meter.xForDb (juce::String (key).getFloatValue()), x * GlueEditor::skinScale, 1e-3);
    }

    int cases = 0;
    for (float threshold : { -24.0f, -20.0f, -14.0f, -8.0f })
    {
        setPlain (*p, glue::params::threshold, juce::jlimit (-20.0f, 10.0f, threshold));
        settleMeter (*p, ed, 0.5f, 45);
        // independent measurement: the level drop through the plugin is the applied gain
        const auto [inRms, outRms] = feedSine (*p, 0.5f, 0.5);
        settleMeter (*p, ed, 0.5f, 10);
        const double measured = 20.0 * std::log10 (inRms / outRms);
        const float shown = meter.getDisplayedDb();
        MEASURE ("threshold " + juce::String (threshold) + " dB: measured GR / shown", juce::String (measured, 2) + " / " + juce::String (shown, 2));
        CHECK_MSG (std::abs (shown - (float) measured) <= 0.3f, "shown " + juce::String (shown) + " vs measured " + juce::String (measured));
        CHECK (meter.getLitSegments() == GrLedMeter::litSegmentsFor ((float) measured)
               || std::abs (std::fmod ((float) measured, 1.0f) - 0.5f) < 0.1f); // ambiguous only at a segment edge

        // on screen: the last lit segment is bright, the next one dark
        const int lit = meter.getLitSegments();
        if (lit > 0 && lit < GrLedMeter::numSegments)
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

    // silence: the reduction is gone and the bar falls back within half a second
    for (int i = 0; i < 15; ++i)
    {
        feedSine (*p, 0.0f, 1.0 / 30.0);
        meter.refresh (1.0f / 30.0f);
    }
    CHECK (meter.getLitSegments() == 0);
}

TEST ("GLUE GUI: Character lamp and the EXT SC lamp follow the real state")
{
    auto p = makeGlueForGui (true);
    p->setUiProperty ("scale", 1.0);
    std::unique_ptr<juce::AudioProcessorEditor> base (p->createEditor());
    auto& ed = dynamic_cast<GlueEditor&> (*base);
    ed.setVisible (true);
    const auto map = GlueEditor::loadUiMap();
    const auto& ch = control (map, "character");
    const float t0 = (float) ch["travelDegrees"][0], t1 = (float) ch["travelDegrees"][1];

    const char* names[] = { "Clean", "TLEET", "Burn" };
    for (int i = 0; i < 3; ++i)
    {
        setChoice (*p, glue::params::character, names[i]);
        auto& lamp = ed.getArcLamp();
        const float expected = t0 + (float) i * 0.5f * (t1 - t0);
        CHECK_NEAR (lamp.getLampAngleDegrees(), expected, 1e-3);
        auto img = ed.createComponentSnapshot (ed.getLocalBounds(), true, 1.0f);
        const auto origin = lamp.getBounds().getPosition().toFloat();
        float litB = 0, otherB = 0;
        for (int j = 0; j < 3; ++j)
        {
            const float deg = t0 + (float) j * 0.5f * (t1 - t0);
            const float r = (float) ch["arcLamp"]["radius"] * GlueEditor::skinScale;
            const auto c = juce::Point<float> ((float) ch["centre"][0], (float) ch["centre"][1]);
            const auto at = refToEditor (c.x, c.y) + juce::Point<float> (r * std::sin (juce::degreesToRadians (deg)), -r * std::cos (juce::degreesToRadians (deg)));
            const float b = brightnessAt (img, juce::Rectangle<float> (6.0f, 6.0f).withCentre (at));
            if (j == i) litB = b; else otherB = juce::jmax (otherB, b);
        }
        juce::ignoreUnused (origin);
        MEASURE (juce::String ("arc lamp lit/other at ") + names[i], litB / juce::jmax (1.0f, otherB));
        CHECK (litB > otherB * 1.5f);
    }

    // EXT SC: switch off -> internal; on with a key -> lamp lit; key bus disabled -> falls back, lamp off
    auto& sc = ed.getSidechainButton();
    setPlain (*p, glue::params::scExternal, 0.0f);
    feedSine (*p, 0.3f, 0.1, 0.5f);
    sc.refresh();
    CHECK (! sc.isLampLit());
    setPlain (*p, glue::params::scExternal, 1.0f);
    feedSine (*p, 0.3f, 0.1, 0.5f);
    sc.refresh();
    CHECK (sc.isLampLit());
    CHECK (sc.getToggleState());

    auto p2 = makeGlueForGui (false);
    std::unique_ptr<juce::AudioProcessorEditor> base2 (p2->createEditor());
    auto& ed2 = dynamic_cast<GlueEditor&> (*base2);
    setPlain (*p2, glue::params::scExternal, 1.0f);
    feedSine (*p2, 0.3f, 0.1);
    ed2.getSidechainButton().refresh();
    CHECK (ed2.getSidechainButton().getToggleState());
    CHECK (! ed2.getSidechainButton().isLampLit());

    // clicking the switch sets the real parameter
    setPlain (*p, glue::params::scExternal, 0.0f);
    sc.triggerClick();
    juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
    CHECK (p->apvts.getRawParameterValue (glue::params::scExternal)->load() > 0.5f);
}
