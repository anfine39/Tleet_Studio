#include "TestFramework.h"
#include "../plugins/DOUBLE/PluginProcessor.h"
#include "../plugins/DOUBLE/PluginEditor.h"
#include "../plugins/DOUBLE/Parameters.h"
#include "../plugins/DOUBLE/dsp/WetFilter.h"

// GUI tests for DOUBLE (plugins/DOUBLE/GUI_TEST_PLAN.md). Every control is
// checked against UI_MAP.json and against the real parameter / DSP state.

using namespace tleet;
using namespace tleet::test;
using tleet::dbl::DoubleProcessor;
using tleet::dbl::DoubleEditor;

namespace
{
    std::unique_ptr<DoubleProcessor> makeDoubleForGui()
    {
        auto p = std::make_unique<DoubleProcessor>();
        p->setPlayConfigDetails (2, 2, 48000.0, 256);
        p->prepareToPlay (48000.0, 256);
        return p;
    }

    void setPlain (DoubleProcessor& p, const char* id, float plain)
    {
        auto* r = p.apvts.getParameter (id);
        r->setValueNotifyingHost (r->convertTo0to1 (plain));
    }

    float plainOf (DoubleProcessor& p, const char* id)
    {
        auto* r = p.apvts.getParameter (id);
        return r->convertFrom0to1 (r->getValue());
    }

    // Half a second of a voiced test signal (harmonic tone with a slow envelope).
    void feed (DoubleProcessor& p, double seconds = 0.5)
    {
        juce::AudioBuffer<float> buf (2, 256);
        juce::MidiBuffer midi;
        static double phase = 0.0;
        const int blocks = (int) (seconds * 48000.0 / 256.0);
        for (int b = 0; b < blocks; ++b)
        {
            for (int i = 0; i < 256; ++i)
            {
                const float env = 0.5f + 0.5f * (float) std::sin (phase * 0.0021);
                const float x = env * 0.25f * (float) (std::sin (phase) + 0.4 * std::sin (2.0 * phase) + 0.2 * std::sin (3.0 * phase));
                phase += 2.0 * juce::MathConstants<double>::pi * 220.0 / 48000.0;
                buf.setSample (0, i, x);
                buf.setSample (1, i, x);
            }
            p.processBlock (buf, midi);
        }
    }

    juce::Point<float> refToEditor (float x, float y)
    {
        return { x * DoubleEditor::skinScale, ui::TleetEditorBase::toolbarHeight + y * DoubleEditor::skinScale };
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

    float brightness (const juce::Image& img, juce::Rectangle<float> ref)
    {
        const auto r = juce::Rectangle<float> (ref.getX() * DoubleEditor::skinScale, ui::TleetEditorBase::toolbarHeight + ref.getY() * DoubleEditor::skinScale,
                                               ref.getWidth() * DoubleEditor::skinScale, ref.getHeight() * DoubleEditor::skinScale).toNearestInt();
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

    void click (juce::Component& target, juce::Component& editor, juce::Point<float> refPoint)
    {
        auto src = juce::Desktop::getInstance().getMainMouseSource();
        const auto local = target.getLocalPoint (&editor, refToEditor (refPoint.x, refPoint.y));
        const auto t = juce::Time::getCurrentTime();
        juce::MouseEvent e (src, local, juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier), 1.0f, 0, 0, 0, 0,
                            &target, &target, t, local, t, 1, false);
        target.mouseDown (e);
        target.mouseUp (e);
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
}

TEST ("DOUBLE GUI: editor opens at every scale; screenshots of default and representative states")
{
    auto p = makeDoubleForGui();
    for (float scale : { 0.75f, 1.0f, 1.5f, 2.0f })
    {
        p->setUiProperty ("scale", scale);
        std::unique_ptr<juce::AudioProcessorEditor> ed (p->createEditor());
        CHECK (dynamic_cast<DoubleEditor*> (ed.get()) != nullptr);
        const int expectedW = juce::roundToInt (DoubleEditor::baseWidth * scale);
        CHECK_MSG (std::abs (ed->getWidth() - expectedW) <= 1, "width at scale " + juce::String (scale));
        saveSnapshot (*ed, "DOUBLE_editor_" + juce::String (juce::roundToInt (scale * 100)));
    }

    p->setUiProperty ("scale", 1.0);
    std::unique_ptr<juce::AudioProcessorEditor> base (p->createEditor());
    auto& ed = dynamic_cast<DoubleEditor&> (*base);
    struct State { const char* name; std::function<void()> apply; };
    const State states[] = {
        { "playing_default", [&] {} },
        { "4_voices_wide",   [&] { setPlain (*p, "voices", 2); setPlain (*p, "mode", 1); setPlain (*p, "width", 160); setPlain (*p, "detune", 18); } },
        { "ghost_filtered",  [&] { setPlain (*p, "mode", 2); setPlain (*p, "hpf", 180); setPlain (*p, "lpf", 4000); setPlain (*p, "monoSafe", 0); } },
        { "width_0",         [&] { setPlain (*p, "width", 0); } },
    };
    for (auto& st : states)
    {
        st.apply();
        feed (*p);
        ed.getVoiceDisplay().refresh();
        ed.getFilterDisplay().rebuild();
        saveSnapshot (ed, juce::String ("DOUBLE_state_") + st.name);
    }
}

TEST ("DOUBLE GUI: every mapped control sits at its mockup position and owns its hitbox")
{
    auto p = makeDoubleForGui();
    p->setUiProperty ("scale", 1.0);
    std::unique_ptr<juce::AudioProcessorEditor> base (p->createEditor());
    auto& ed = dynamic_cast<DoubleEditor&> (*base);
    ed.setVisible (true);
    const auto map = DoubleEditor::loadUiMap();

    int checked = 0;
    for (auto& c : *map["controls"].getArray())
    {
        const auto type = c["type"].toString();
        std::vector<std::pair<juce::Rectangle<float>, juce::Component*>> boxes;
        if (type == "ROTARY")
            boxes.push_back ({ rectOf (c["hitbox"]), &ed.getKnob (c["parameter"].toString()) });
        else if (type == "TOGGLE_BUTTON")
            boxes.push_back ({ rectOf (c["hitbox"]), &ed.getToggle (c["parameter"].toString()) });
        else if (type == "BUTTON_GROUP")
            for (auto& hb : *c["hitboxes"].getArray())
                boxes.push_back ({ rectOf (hb), c["id"].toString() == "voices" ? &ed.getVoiceButtons() : &ed.getModeButtons() });
        else
            continue;

        for (auto& [hb, expected] : boxes)
        {
            for (auto pt : { hb.getCentre(), hb.getTopLeft() + juce::Point<float> (3, 3), hb.getBottomRight() - juce::Point<float> (3, 3),
                             hb.getTopRight() + juce::Point<float> (-3, 3), hb.getBottomLeft() + juce::Point<float> (3, -3) })
            {
                auto* hit = ed.getComponentAt (refToEditor (pt.x, pt.y).toInt());
                CHECK_MSG (hit == expected, c["id"].toString() + " at " + pt.toString() + " hit " + (hit ? hit->getBounds().toString() : juce::String ("null")));
            }
            ++checked;
        }

        if (type == "ROTARY")
        {
            auto& k = ed.getKnob (c["parameter"].toString());
            const juce::Point<float> centreRef ((float) c["centre"][0], (float) c["centre"][1]);
            const auto actual = k.getBounds().getPosition().toFloat() + (centreRef - rectOf (c["hitbox"]).getPosition()) * DoubleEditor::skinScale;
            CHECK (actual.getDistanceFrom (refToEditor (centreRef.x, centreRef.y)) <= 1.0f);
            // the readout window is where the artwork printed it
            const auto ro = rectOf (c["readout"]);
            const auto shown = k.getValueArea().toFloat() + k.getBounds().getPosition().toFloat();
            const auto expected = juce::Rectangle<float> (refToEditor (ro.getX(), ro.getY()), refToEditor (ro.getRight(), ro.getBottom()));
            CHECK_MSG (shown.getCentre().getDistanceFrom (expected.getCentre()) <= 1.0f, c["id"].toString() + " readout " + shown.toString());
        }
    }
    MEASURE ("hitboxes checked", checked);
    CHECK (checked == 8 + 3 + 3 + 2);

    // clickable areas of different controls never overlap
    std::vector<std::pair<juce::String, juce::Rectangle<float>>> areas;
    for (auto& c : *map["controls"].getArray())
    {
        if (c.hasProperty ("hitboxes"))
            for (auto& r : *c["hitboxes"].getArray()) areas.push_back ({ c["id"].toString(), rectOf (r) });
        else if (c.hasProperty ("hitbox") && c["type"].toString() != "VOICE_DISPLAY" && c["type"].toString() != "FILTER_DISPLAY")
            areas.push_back ({ c["id"].toString(), rectOf (c["hitbox"]) });
    }
    for (size_t i = 0; i < areas.size(); ++i)
        for (size_t j = i + 1; j < areas.size(); ++j)
            CHECK_MSG (! areas[i].second.intersects (areas[j].second), areas[i].first + " overlaps " + areas[j].first);
}

TEST ("DOUBLE GUI: readouts print the real parameter value; knobs follow host automation")
{
    auto p = makeDoubleForGui();
    std::unique_ptr<juce::AudioProcessorEditor> base (p->createEditor());
    auto& ed = dynamic_cast<DoubleEditor&> (*base);

    const std::pair<const char*, float> values[] = { { "detune", 12.0f }, { "delay", 18.0f }, { "humanize", 42.0f }, { "width", 128.0f },
                                                     { "hpf", 20.0f }, { "lpf", 1200.0f }, { "mix", 78.0f }, { "output", 0.0f } };
    for (auto& [id, v] : values)
    {
        setPlain (*p, id, v);
        auto& k = ed.getKnob (id);
        auto* r = p->apvts.getParameter (id);
        CHECK_NEAR (k.getNormalisedValue(), r->getValue(), 1e-5);
        auto expected = r->getCurrentValueAsText();
        if (r->getLabel().isNotEmpty()) expected << " " << r->getLabel();
        CHECK_MSG (k.getReadoutText() == expected, juce::String (id) + ": '" + k.getReadoutText() + "' vs '" + expected + "'");
    }
    CHECK (ed.getKnob ("detune").getReadoutText() == "12 cents");
    CHECK (ed.getKnob ("delay").getReadoutText() == "18 ms");
    CHECK (ed.getKnob ("lpf").getReadoutText() == "1.2 kHz");
    CHECK (ed.getKnob ("hpf").getReadoutText() == "20 Hz");
    CHECK (ed.getKnob ("output").getReadoutText() == "0.0 dB");
}

TEST ("DOUBLE GUI: VOICES, MODE and the lamp toggles set the real parameters and light up on screen")
{
    auto p = makeDoubleForGui();
    p->setUiProperty ("scale", 1.0);
    std::unique_ptr<juce::AudioProcessorEditor> base (p->createEditor());
    auto& ed = dynamic_cast<DoubleEditor&> (*base);
    ed.setVisible (true);
    const auto map = DoubleEditor::loadUiMap();

    for (auto* gid : { "voices", "mode" })
    {
        auto& group = juce::String (gid) == "voices" ? ed.getVoiceButtons() : ed.getModeButtons();
        const auto& c = control (map, gid);
        for (int i = 2; i >= 0; --i)
        {
            click (group, ed, rectOf (c["hitboxes"][i]).getCentre());
            CHECK_MSG (juce::roundToInt (plainOf (*p, gid)) == i, juce::String (gid) + " button " + juce::String (i));
            auto img = ed.createComponentSnapshot (ed.getLocalBounds(), true, 1.0f);
            float lit = 0, other = 0;
            for (int j = 0; j < 3; ++j)
            {
                const float b = brightness (img, rectOf (c["faces"][j]));
                if (j == i) lit = b; else other = juce::jmax (other, b);
            }
            MEASURE (juce::String (gid) + " lit/unlit " + juce::String (i), lit / juce::jmax (1.0f, other));
            CHECK_MSG (lit > other * 1.4f, juce::String (gid) + " " + juce::String (i) + ": " + juce::String (lit) + " vs " + juce::String (other));
        }
        // host automation moves the lit button
        p->apvts.getParameter (gid)->setValueNotifyingHost (0.5f);
        CHECK (group.getSelectedIndex() == 1);
    }

    for (auto* tid : { "monoSafe", "autoLevel" })
    {
        auto& t = ed.getToggle (tid);
        const auto& c = control (map, tid);
        const bool before = plainOf (*p, tid) > 0.5f;
        auto off = ed.createComponentSnapshot (ed.getLocalBounds(), true, 1.0f);
        click (t, ed, rectOf (c["hitbox"]).getCentre());
        CHECK ((plainOf (*p, tid) > 0.5f) != before);
        CHECK (t.isOn() == (plainOf (*p, tid) > 0.5f));
        auto after = ed.createComponentSnapshot (ed.getLocalBounds(), true, 1.0f);
        const float b0 = brightness (off, rectOf (c["face"])), b1 = brightness (after, rectOf (c["face"]));
        MEASURE (juce::String (tid) + " face brightness off->on", (t.isOn() ? b1 / juce::jmax (1.0f, b0) : b0 / juce::jmax (1.0f, b1)));
        CHECK_MSG (t.isOn() ? b1 > b0 * 1.3f : b0 > b1 * 1.3f, juce::String (tid) + ": " + juce::String (b0) + " -> " + juce::String (b1));
    }
}

TEST ("DOUBLE GUI: the voice display shows exactly the voices the DSP runs, at their real pans")
{
    auto p = makeDoubleForGui();
    std::unique_ptr<juce::AudioProcessorEditor> base (p->createEditor());
    auto& ed = dynamic_cast<DoubleEditor&> (*base);
    auto& vd = ed.getVoiceDisplay();

    for (int choice = 0; choice < 3; ++choice)
    {
        setPlain (*p, "voices", (float) choice);
        setPlain (*p, "width", 100.0f);
        feed (*p);
        vd.refresh();
        int shown = 0;
        for (int i = 0; i < dbl::DoubleTelemetry::maxVoices; ++i)
        {
            const auto& v = vd.getShownVoices()[(size_t) i];
            const auto t = p->telemetry.voice (i);
            CHECK (v.active == t.active);
            if (! v.active) continue;
            ++shown;
            CHECK_NEAR (v.pan, t.pan, 1e-6);
            CHECK_NEAR (v.x, vd.xForPan (t.pan), 1e-4);
            CHECK (v.level > 0.0f); // the cloud is the voice's real output envelope
        }
        CHECK_MSG (shown == dbl::params::voiceCount (choice), "voices " + juce::String (dbl::params::voiceCount (choice)) + " shown " + juce::String (shown));
    }

    // Width 0 collapses every voice onto the centre
    setPlain (*p, "voices", 2.0f);
    setPlain (*p, "width", 0.0f);
    feed (*p, 1.0);
    vd.refresh();
    for (auto& v : vd.getShownVoices())
        if (v.active)
            CHECK_MSG (std::abs (v.pan) < 0.02f, "pan " + juce::String (v.pan));
}

TEST ("DOUBLE GUI: the filter curve is the exact response of the live wet filters, on a true log axis")
{
    auto p = makeDoubleForGui();
    std::unique_ptr<juce::AudioProcessorEditor> base (p->createEditor());
    auto& ed = dynamic_cast<DoubleEditor&> (*base);
    auto& fd = ed.getFilterDisplay();
    const auto map = DoubleEditor::loadUiMap();
    const auto& c = control (map, "filterDisplay");
    const auto plotRef = rectOf (c["plot"]);

    // axis: 20 Hz and 20 kHz at the plot edges, decades evenly spaced
    const auto o = rectOf (c["hitbox"]).getPosition();
    CHECK_NEAR (fd.xForHz (20.0f), (plotRef.getX() - o.x) * DoubleEditor::skinScale, 1e-3);
    CHECK_NEAR (fd.xForHz (20000.0f), (plotRef.getRight() - o.x) * DoubleEditor::skinScale, 1e-3);
    CHECK_NEAR (fd.xForHz (1000.0f) - fd.xForHz (100.0f), fd.xForHz (10000.0f) - fd.xForHz (1000.0f), 1e-3);

    struct Case { float hpf, lpf; int mode; };
    for (auto [hpf, lpf, mode] : { Case { 20.0f, 20000.0f, 0 }, Case { 150.0f, 6000.0f, 1 }, Case { 1000.0f, 1000.0f, 0 }, Case { 60.0f, 20000.0f, 2 } })
    {
        setPlain (*p, "hpf", hpf);
        setPlain (*p, "lpf", lpf);
        setPlain (*p, "mode", (float) mode);
        fd.rebuild();
        // sample the drawn path at a few frequencies and compare with the DSP's own response
        for (float hz : { 30.0f, 100.0f, 400.0f, 1000.0f, 3000.0f, 12000.0f })
        {
            const double db = dbl::wetFilterResponseDb (hz, plainOf (*p, "hpf"), plainOf (*p, "lpf"), p->getWetSampleRate(), mode);
            const float x = fd.xForHz (hz);
            // find the path's y at x
            juce::PathFlatteningIterator it (fd.getCurvePath());
            float y = std::nanf ("");
            while (it.next())
                if ((it.x1 <= x && x <= it.x2) || (it.x2 <= x && x <= it.x1))
                {
                    const float t = it.x2 != it.x1 ? (x - it.x1) / (it.x2 - it.x1) : 0.0f;
                    y = it.y1 + t * (it.y2 - it.y1);
                    break;
                }
            const float expectedY = juce::jmin (fd.yForDb (juce::jlimit (-36.0f, 6.0f, (float) db)), fd.yForDb (-30.0f) + 2.0f);
            CHECK_MSG (std::abs (y - expectedY) <= 0.6f, juce::String (hz) + " Hz (hpf " + juce::String (hpf) + ", lpf " + juce::String (lpf) + "): y " + juce::String (y) + " vs " + juce::String (expectedY));
        }
    }
}
