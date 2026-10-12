#include "TestFramework.h"
#include "../plugins/BITE/PluginProcessor.h"
#include "../plugins/BITE/PluginEditor.h"
#include "../plugins/BITE/Parameters.h"

// GUI tests for BITE (plugins/BITE/GUI_TEST_PLAN.md). Every control is
// checked against UI_MAP.json and against the real parameter / DSP state.

using namespace tleet;
using namespace tleet::test;
using tleet::bite::BiteProcessor;
using tleet::bite::BiteEditor;

namespace
{
    std::unique_ptr<BiteProcessor> makeBiteForGui()
    {
        auto p = std::make_unique<BiteProcessor>();
        p->setPlayConfigDetails (2, 2, 48000.0, 256);
        p->prepareToPlay (48000.0, 256);
        return p;
    }

    juce::Point<float> refToEditor (float x, float y)
    {
        return { x * BiteEditor::skinScale, ui::TleetEditorBase::toolbarHeight + y * BiteEditor::skinScale };
    }

    float plainOf (BiteProcessor& p, const char* id)
    {
        auto* r = p.apvts.getParameter (id);
        return r->convertFrom0to1 (r->getValue());
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

    // mean brightness of a reference-pixel rectangle in a 100 % editor snapshot
    float brightness (const juce::Image& img, juce::Rectangle<float> ref)
    {
        const auto r = juce::Rectangle<float> (ref.getX() * BiteEditor::skinScale, ui::TleetEditorBase::toolbarHeight + ref.getY() * BiteEditor::skinScale,
                                               ref.getWidth() * BiteEditor::skinScale, ref.getHeight() * BiteEditor::skinScale).toNearestInt();
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
}

TEST ("BITE GUI: editor opens at every scale and renders screenshots")
{
    auto p = makeBiteForGui();
    for (float scale : { 0.75f, 1.0f, 1.5f, 2.0f })
    {
        p->setUiProperty ("scale", scale);
        std::unique_ptr<juce::AudioProcessorEditor> ed (p->createEditor());
        CHECK (dynamic_cast<BiteEditor*> (ed.get()) != nullptr);
        const int expectedW = juce::roundToInt (BiteEditor::baseWidth * scale);
        CHECK_MSG (std::abs (ed->getWidth() - expectedW) <= 1, "width at scale " + juce::String (scale));

        auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
        juce::PNGImageFormat png;
        auto f = outputDirectory().getChildFile ("BITE_editor_" + juce::String (juce::roundToInt (scale * 100)) + ".png");
        f.deleteFile();
        if (auto os = f.createOutputStream())
            png.writeImageToStream (img, *os);
    }
}

TEST ("BITE GUI: every mapped control sits at its mockup position and owns its hitbox")
{
    auto p = makeBiteForGui();
    p->setUiProperty ("scale", 1.0);
    std::unique_ptr<juce::AudioProcessorEditor> base (p->createEditor());
    auto& ed = dynamic_cast<BiteEditor&> (*base);
    ed.setVisible (true); // hit-testing skips invisible components
    const auto map = BiteEditor::loadUiMap();

    int checked = 0;
    for (auto& c : *map["controls"].getArray())
    {
        const auto type = c["type"].toString();
        std::vector<std::pair<juce::Rectangle<float>, juce::Component*>> boxes;
        if (type == "ROTARY" || type == "ROTARY_STEPPED")
            boxes.push_back ({ rectOf (c["hitbox"]), &ed.getKnob (c["parameter"].toString()) });
        else if (type == "VU")
            boxes.push_back ({ rectOf (c["hitbox"]), &ed.getVU() });
        else if (type == "BUTTON_GROUP")
            for (auto& hb : *c["hitboxes"].getArray())
                boxes.push_back ({ rectOf (hb), &ed.getRatioButtons() });
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

        if (type == "ROTARY" || type == "ROTARY_STEPPED")
        {
            auto& k = ed.getKnob (c["parameter"].toString());
            const juce::Point<float> centreRef ((float) c["centre"][0], (float) c["centre"][1]);
            const auto actual = k.getBounds().getPosition().toFloat() + (centreRef - rectOf (c["hitbox"]).getPosition()) * BiteEditor::skinScale;
            CHECK (actual.getDistanceFrom (refToEditor (centreRef.x, centreRef.y)) <= 1.0f);
        }
    }
    MEASURE ("hitboxes checked", checked);
    CHECK (checked == 6 + 5 + 1);

    // a click between two ratio buttons selects nothing
    {
        auto& rb = ed.getRatioButtons();
        const auto& hb = control (map, "ratio")["hitboxes"];
        const float gapX = ((float) hb[0][0] + (float) hb[0][2] + (float) hb[1][0]) * 0.5f;
        CHECK (ed.getComponentAt (refToEditor (gapX, (float) hb[0][1] + 50.0f).toInt()) != &rb);
    }

    // hitboxes of different controls never overlap
    std::vector<juce::Rectangle<int>> bounds;
    for (auto* child : ed.getChildren().getFirst()->getChildren())
        if (dynamic_cast<ui::Knob*> (child) || dynamic_cast<ui::LampButtonGroup*> (child) || dynamic_cast<ui::SkinVU*> (child))
            bounds.push_back (child->getBounds());
    for (size_t i = 0; i < bounds.size(); ++i)
        for (size_t j = i + 1; j < bounds.size(); ++j)
            CHECK_MSG (! bounds[i].intersects (bounds[j]), bounds[i].toString() + " overlaps " + bounds[j].toString());
}

TEST ("BITE GUI: Attack/Release scale labels are the real times at the printed dots, slowest left")
{
    auto p = makeBiteForGui();
    std::unique_ptr<juce::AudioProcessorEditor> base (p->createEditor());
    auto& ed = dynamic_cast<BiteEditor&> (*base);
    const auto map = BiteEditor::loadUiMap();

    for (auto* id : { bite::params::attack, bite::params::release })
    {
        const auto& c = control (map, id);
        auto* param = p->apvts.getParameter (id);
        const auto& labels = ed.getScaleLabels (id);
        CHECK (labels.size() == 5);

        for (size_t i = 0; i < labels.size(); ++i)
        {
            // the knob drawn at the dot's angle sets exactly the printed value
            const float deg = (float) c["scaleLabels"]["degrees"][(int) i];
            const float t0 = (float) c["travelDegrees"][0], t1 = (float) c["travelDegrees"][1];
            param->setValueNotifyingHost ((deg - t0) / (t1 - t0));
            const float actual = param->convertFrom0to1 (param->getValue());
            CHECK_NEAR (labels[i].value, actual, actual * 1e-4);
            CHECK (labels[i].text == juce::String (juce::roundToInt (actual)));
            CHECK_MSG (std::abs (labels[i].text.getFloatValue() - actual) <= 0.5f + 1e-3f, labels[i].text);
            // printed where the artwork had its number
            const auto anchor = juce::Point<float> ((float) c["scaleLabels"]["anchors"][(int) i][0], (float) c["scaleLabels"]["anchors"][(int) i][1]);
            CHECK (labels[i].box.expanded (2.0f).contains (anchor));
            if (i > 0)
                CHECK (labels[i].value < labels[i - 1].value); // clockwise = faster
        }
        MEASURE (juce::String (id) + " label first", labels.front().value);
        MEASURE (juce::String (id) + " label last", labels.back().value);
    }

    CHECK_NEAR (ed.getScaleLabels ("attack").front().value, bite::params::attackSlowestUs, 0.01);
    CHECK_NEAR (ed.getScaleLabels ("attack").back().value, bite::params::attackFastestUs, 0.01);
    CHECK_NEAR (ed.getScaleLabels ("release").front().value, bite::params::releaseSlowestMs, 0.01);
    CHECK_NEAR (ed.getScaleLabels ("release").back().value, bite::params::releaseFastestMs, 0.01);
}

TEST ("BITE GUI: drag, keys, wheel and button clicks change the real parameters")
{
    auto p = makeBiteForGui();
    p->setUiProperty ("scale", 1.0);
    std::unique_ptr<juce::AudioProcessorEditor> base (p->createEditor());
    auto& ed = dynamic_cast<BiteEditor&> (*base);
    ed.setVisible (true);
    auto src = juce::Desktop::getInstance().getMainMouseSource();

    auto drag = [&] (ui::Knob& k, float dy)
    {
        const auto c = k.getLocalBounds().getCentre().toFloat();
        const auto t = juce::Time::getCurrentTime();
        const juce::ModifierKeys mods (juce::ModifierKeys::leftButtonModifier);
        juce::MouseEvent down (src, c, mods, 1.0f, 0, 0, 0, 0, &k, &k, t, c, t, 1, false);
        k.mouseDown (down);
        juce::MouseEvent move (src, c.translated (0.0f, dy), mods, 1.0f, 0, 0, 0, 0, &k, &k, t, c, t, 1, true);
        k.mouseDrag (move);
        k.mouseUp (move);
    };

    // dragging Attack up (clockwise) makes it faster
    const float attackBefore = plainOf (*p, "attack");
    drag (ed.getKnob ("attack"), -60.0f);
    CHECK (plainOf (*p, "attack") < attackBefore * 0.8f);
    const float releaseBefore = plainOf (*p, "release");
    drag (ed.getKnob ("release"), -60.0f);
    CHECK (plainOf (*p, "release") < releaseBefore * 0.8f);
    CHECK (p->canUndo());

    // Character: one key press or wheel notch = one position
    auto& ch = ed.getKnob ("character");
    p->apvts.getParameter ("character")->setValueNotifyingHost (0.0f);
    CHECK (ch.keyPressed (juce::KeyPress (juce::KeyPress::rightKey)));
    CHECK (juce::roundToInt (plainOf (*p, "character")) == 1);
    CHECK (ch.keyPressed (juce::KeyPress (juce::KeyPress::rightKey)));
    CHECK (juce::roundToInt (plainOf (*p, "character")) == 2);
    {
        juce::MouseWheelDetails w { 0.0f, -1.0f, false, false, false };
        const auto t = juce::Time::getCurrentTime();
        juce::MouseEvent e (src, {}, {}, 1.0f, 0, 0, 0, 0, &ch, &ch, t, {}, t, 0, false);
        ch.mouseWheelMove (e, w);
        CHECK (juce::roundToInt (plainOf (*p, "character")) == 1);
    }
    // the pointer sits at the printed CLEAN / TLEET / BURN dots
    for (int i = 0; i < 3; ++i)
    {
        p->apvts.getParameter ("character")->setValueNotifyingHost ((float) i / 2.0f);
        CHECK_NEAR (ch.getNormalisedValue(), (float) i / 2.0f, 1e-6);
    }

    // Ratio: clicking each button on the artwork selects that ratio
    auto& rb = ed.getRatioButtons();
    const auto map = BiteEditor::loadUiMap();
    const auto& hb = control (map, "ratio")["hitboxes"];
    for (int i = 4; i >= 0; --i)
    {
        const auto ref = rectOf (hb[i]).getCentre();
        const auto local = rb.getLocalPoint (&ed, refToEditor (ref.x, ref.y));
        const auto t = juce::Time::getCurrentTime();
        juce::MouseEvent e (src, local, juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier), 1.0f, 0, 0, 0, 0, &rb, &rb, t, local, t, 1, false);
        rb.mouseDown (e);
        CHECK_MSG (juce::roundToInt (plainOf (*p, "ratio")) == i, "ratio button " + juce::String (i));
        CHECK (rb.getSelectedIndex() == i);
    }
    CHECK (rb.keyPressed (juce::KeyPress (juce::KeyPress::rightKey)));
    CHECK (juce::roundToInt (plainOf (*p, "ratio")) == 1);
}

TEST ("BITE GUI: the lit ratio button follows the parameter on screen")
{
    auto p = makeBiteForGui();
    p->setUiProperty ("scale", 1.0);
    std::unique_ptr<juce::AudioProcessorEditor> base (p->createEditor());
    auto& ed = dynamic_cast<BiteEditor&> (*base);
    const auto map = BiteEditor::loadUiMap();
    const auto& leds = control (map, "ratio")["leds"];

    for (int sel = 0; sel < 5; ++sel)
    {
        p->apvts.getParameter ("ratio")->setValueNotifyingHost ((float) sel / 4.0f);
        CHECK (ed.getRatioButtons().getSelectedIndex() == sel);
        auto img = ed.createComponentSnapshot (ed.getLocalBounds(), true, 1.0f);
        float lit = 0.0f, brightestOther = 0.0f;
        for (int i = 0; i < 5; ++i)
        {
            const float b = brightness (img, rectOf (leds[i]));
            if (i == sel) lit = b; else brightestOther = juce::jmax (brightestOther, b);
        }
        MEASURE ("ratio lamp lit/unlit brightness " + juce::String (sel), lit / juce::jmax (1.0f, brightestOther));
        CHECK_MSG (lit > brightestOther * 2.0f, "lamp " + juce::String (sel) + ": " + juce::String (lit) + " vs " + juce::String (brightestOther));
    }
}

TEST ("BITE GUI: host automation moves every control; VU modes switch the measured quantity")
{
    auto p = makeBiteForGui();
    std::unique_ptr<juce::AudioProcessorEditor> base (p->createEditor());
    auto& ed = dynamic_cast<BiteEditor&> (*base);

    for (auto* id : { "input", "output", "attack", "release", "mix" })
    {
        auto* r = p->apvts.getParameter (id);
        r->setValueNotifyingHost (0.123f);
        CHECK_NEAR (ed.getKnob (id).getNormalisedValue(), r->getValue(), 1e-4);
    }
    p->apvts.getParameter ("ratio")->setValueNotifyingHost (0.75f);
    CHECK (ed.getRatioButtons().getSelectedIndex() == 3);

    CHECK (ed.getMeterMode() == bite::MeterMode::gr);
    CHECK (ed.getVU().getModeCaption() == "GR");
    ed.getVU().onClick();
    CHECK (ed.getMeterMode() == bite::MeterMode::plus4);
    CHECK (ed.getVU().getModeCaption() == "+4");
    CHECK ((int) p->getUiProperty ("meterMode", -1) == 1);
    ed.getVU().onClick();
    CHECK (ed.getVU().getModeCaption() == "+8");
    ed.getVU().onClick();
    CHECK (ed.getMeterMode() == bite::MeterMode::gr);

    // the modes measure different things: GR shows the applied reduction,
    // +4 / +8 the output level against different 0 VU references
    const float rms = juce::Decibels::decibelsToGain (-18.0f) ;
    CHECK_NEAR (bite::meterReadingFor (bite::MeterMode::gr, 6.0f, rms), -6.0f, 1e-4);
    CHECK_NEAR (bite::meterReadingFor (bite::MeterMode::plus4, 6.0f, rms), 0.0f, 1e-3);
    CHECK_NEAR (bite::meterReadingFor (bite::MeterMode::plus8, 6.0f, rms), -4.0f, 1e-3);
    CHECK (std::isnan (bite::meterReadingFor (bite::MeterMode::gr, -1.0f, rms)));
    CHECK (std::isnan (bite::meterReadingFor (bite::MeterMode::plus4, 6.0f, -1.0f)));

    // the needle hits the printed marks
    const auto map = BiteEditor::loadUiMap();
    for (auto& prop : control (map, "vu")["scaleDegrees"].getDynamicObject()->getProperties())
        CHECK_NEAR (juce::radiansToDegrees (ed.getVU().angleForVu (prop.name.toString().getFloatValue())), (double) prop.value, 1e-3);
}
