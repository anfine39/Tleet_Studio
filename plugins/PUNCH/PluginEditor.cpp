#include "PluginEditor.h"
#include "Parameters.h"
#include "PUNCHSkin.h"

namespace tleet::punch
{
    namespace
    {
        // Printed text on the panel: warm cream, measured on the artwork.
        const juce::Colour panelInk { 0xffd9bea0 };
    }

    using namespace ui::skin;

    // ========================================================= OverEasy switch

    OverEasySwitch::OverEasySwitch (juce::RangedAudioParameter& p, juce::Image lampLit, juce::Rectangle<float> area)
        : juce::Button (p.getName (64)), attachment (p, *this), lamp (std::move (lampLit)), lampArea (area)
    {
        setClickingTogglesState (true);
        setTooltip ("OverEasy: soft knee. Off: hard knee, the ratio applies as soon as the level passes the threshold. "
                    "On: the ratio grows gradually around the threshold. The lamp is lit while OverEasy is on.");
        attachment.sendInitialUpdate();
    }

    void OverEasySwitch::paintButton (juce::Graphics& g, bool, bool)
    {
        // the plate shows the switch with its lamp dark; only the lit lamp is drawn
        if (getToggleState() && lamp.isValid())
        {
            g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
            g.drawImage (lamp, lampArea, juce::RectanglePlacement::stretchToFit);
        }
    }

    // ================================================================== editor

    juce::var PunchEditor::loadUiMap()
    {
        return juce::JSON::parse (juce::String::fromUTF8 (PUNCHSkin::UI_MAP_json, PUNCHSkin::UI_MAP_jsonSize));
    }

    juce::String PunchEditor::labelText (const juce::String& id, float v)
    {
        if (id == params::ratio)
        {
            const float r = params::ratioFromPlain (v);
            if (std::isinf (r))
                return juce::String::fromUTF8 ("\xe2\x88\x9e");
            const float rounded = std::round (r);
            return std::abs (r - rounded) < 0.01f ? juce::String ((int) rounded) : juce::String (r, 1);
        }
        const int n = juce::roundToInt (v);
        if (id == params::threshold || id == params::output)
            return n > 0 ? "+" + juce::String (n) : juce::String (n);
        return juce::String (n);
    }

    std::vector<ScaleLabel> PunchEditor::makeScaleLabels (const juce::var& c, juce::RangedAudioParameter& param)
    {
        const auto id = c["parameter"].toString();
        auto text = [id] (float v, const juce::String&) { return labelText (id, v); };
        if (! c["scaleLabels"].hasProperty ("ratios"))
            return ui::skin::makeScaleLabels (c, param, text);

        // ratio labels: the knob's plain value is the compression slope
        auto copy = juce::JSON::parse (juce::JSON::toString (c));
        juce::Array<juce::var> plains;
        for (auto& r : *c["scaleLabels"]["ratios"].getArray())
            plains.add (params::plainFromRatio (r.isString() ? std::numeric_limits<float>::infinity() : (float) r));
        copy["scaleLabels"].getDynamicObject()->setProperty ("values", plains);
        return ui::skin::makeScaleLabels (copy, param, text);
    }

    PunchEditor::PunchEditor (PunchProcessor& p)
        : ui::TleetEditorBase (p, baseWidth, toolbarHeight + panelHeight),
          proc (p),
          uiMap (loadUiMap()),
          plate (juce::ImageCache::getFromMemory (PUNCHSkin::plate_png, PUNCHSkin::plate_pngSize))
    {
        const float s = skinScale;
        auto onEdit = [this] { proc.commitHistory(); };

        for (const char* id : { params::threshold, params::ratio, params::grit, params::mix, params::output })
        {
            const auto* c = findControl (uiMap, id);
            jassert (c != nullptr);
            auto* param = p.apvts.getParameter ((*c)["parameter"].toString());
            jassert (param != nullptr);

            const auto hitbox = rectFrom ((*c)["hitbox"]);
            const auto centre = pointFrom ((*c)["centre"]);
            const auto& ptr = (*c)["pointer"];

            auto knob = std::make_unique<ui::Knob> (*param, param->getName (64));
            ui::Knob::SkinPointer sp;
            sp.centre = (centre - hitbox.getPosition()) * s;
            sp.innerRadius = (float) ptr["inner"] * s;
            sp.outerRadius = (float) ptr["outer"] * s;
            sp.width = (float) ptr["width"] * s;
            sp.startAngle = juce::degreesToRadians ((float) (*c)["travelDegrees"][0]);
            sp.endAngle = juce::degreesToRadians ((float) (*c)["travelDegrees"][1]);
            // the artwork's lit pointers: a near-white core in an amber glow
            sp.core = juce::Colour (0xfffff2c4);
            sp.glow = juce::Colour (0xfff59a33);
            knob->setSkinPointer (sp);
            knob->onEditFinished = onEdit;
            addToPanel (*knob);

            KnobEntry e;
            e.id = id;
            e.hitbox = hitbox;
            e.labels = makeScaleLabels (*c, *param);
            if ((*c).hasProperty ("scaleLabels"))
                e.capHeight = (float) (*c)["scaleLabels"]["capHeight"];
            e.knob = std::move (knob);
            knobs.push_back (std::move (e));
        }

        // ---- OverEasy rocker
        {
            const auto* c = findControl (uiMap, "overEasy");
            jassert (c != nullptr);
            overEasyBounds = rectFrom ((*c)["hitbox"]);
            const auto lampRef = rectFrom ((*c)["lamp"]["rect"]);
            auto lamp = juce::ImageCache::getFromMemory (PUNCHSkin::lamp_lit_png, PUNCHSkin::lamp_lit_pngSize);
            overEasy = std::make_unique<OverEasySwitch> (*p.apvts.getParameter (params::overEasy), lamp,
                                                          (lampRef - overEasyBounds.getPosition()) * s);
            overEasy->onClick = onEdit;
            addToPanel (*overEasy);
        }

        // ---- GR LED meter
        {
            const auto* c = findControl (uiMap, "grMeter");
            jassert (c != nullptr);
            meterBounds = rectFrom ((*c)["hitbox"]);
            meter = std::make_unique<LedGainMeter> (meterGeometry (*c, meterBounds, s));
            meter->setSupplier ([this] { return proc.grMeter.takeGainReduction(); });
            addToPanel (*meter);
        }

        finishSetup();
    }

    ui::Knob& PunchEditor::getKnob (const juce::String& parameterId)
    {
        for (auto& k : knobs)
            if (k.id == parameterId)
                return *k.knob;
        jassertfalse;
        return *knobs.front().knob;
    }

    const std::vector<ScaleLabel>& PunchEditor::getScaleLabels (const juce::String& parameterId) const
    {
        for (auto& k : knobs)
            if (k.id == parameterId)
                return k.labels;
        static const std::vector<ScaleLabel> none;
        return none;
    }

    juce::Rectangle<int> PunchEditor::refToPanel (juce::Rectangle<float> r) const
    {
        return (r * skinScale).translated ((float) panelArea.getX(), (float) panelArea.getY()).toNearestInt();
    }

    void PunchEditor::layoutPanel (juce::Rectangle<int> area)
    {
        panelArea = area;
        for (auto& k : knobs)
            k.knob->setBounds (refToPanel (k.hitbox));
        if (meter)    meter->setBounds (refToPanel (meterBounds));
        if (overEasy) overEasy->setBounds (refToPanel (overEasyBounds));
    }

    void PunchEditor::paintPanel (juce::Graphics& g, juce::Rectangle<int> area)
    {
        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        g.drawImage (plate, area.toFloat(), juce::RectanglePlacement::stretchToFit);

        const auto origin = area.getPosition().toFloat();
        for (auto& k : knobs)
            paintScale (g, {}, k.labels, k.capHeight, skinScale, origin, panelInk, panelInk);
    }
}
