#include "PluginEditor.h"
#include "Parameters.h"
#include "SHAPESkin.h"

namespace tleet::shape
{
    namespace
    {
        // Printed ink measured on the artwork: black on the cream EQ panel,
        // warm cream on the dark bottom panel.
        const juce::Colour darkInk { 0xff16110c };
        const juce::Colour lightInk { 0xfff0d6b0 };
    }

    using namespace ui::skin;

    // ================================================================== EQ IN

    EqInLamp::EqInLamp (juce::RangedAudioParameter& param, LampSprite s, juce::Rectangle<float> area)
        : juce::Button (param.getName (64)), attachment (param, *this), sprite (std::move (s)), clickArea (area)
    {
        setClickingTogglesState (true);
        setTooltip ("EQ IN: lit while the passive EQ curves are in the signal path. Off takes the EQ out "
                    "but keeps the transformers and the tube amplifier, as on the hardware; the host's "
                    "bypass is the fully dry signal.");
        attachment.sendInitialUpdate();
    }

    void EqInLamp::paintButton (juce::Graphics& g, bool, bool)
    {
        // the plate shows the lamp dark; only the lit lamp is drawn
        if (! getToggleState() || ! sprite.image.isValid())
            return;
        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        g.drawImage (sprite.image, sprite.area, juce::RectanglePlacement::stretchToFit);
    }

    // ================================================================== editor

    juce::var ShapeEditor::loadUiMap()
    {
        return juce::JSON::parse (juce::String::fromUTF8 (SHAPESkin::UI_MAP_json, SHAPESkin::UI_MAP_jsonSize));
    }

    juce::String ShapeEditor::labelText (const juce::String& id, float v, const juce::String& stepName)
    {
        if (stepName.isNotEmpty())
            return stepName.upToFirstOccurrenceOf (" ", false, false); // "20 Hz" -> "20", "3 kHz" -> "3"
        const int n = juce::roundToInt (v);
        if (id == params::output)
            return n > 0 ? "+" + juce::String (n) : juce::String (n);
        return juce::String (n);
    }

    ShapeEditor::ShapeEditor (ShapeProcessor& p)
        : ui::TleetEditorBase (p, baseWidth, toolbarHeight + panelHeight),
          proc (p),
          uiMap (loadUiMap()),
          plate (juce::ImageCache::getFromMemory (SHAPESkin::plate_png, SHAPESkin::plate_pngSize))
    {
        const float s = skinScale;
        auto onEdit = [this] { proc.commitHistory(); };

        const std::pair<const char*, const char*> tips[] = {
            { params::lfFreq,      "LOW FREQUENCY: where the low boost and cut act (20, 30, 60 or 100 Hz)" },
            { params::lfBoost,     "LOW BOOST: low shelf lift at the selected frequency" },
            { params::lfAtten,     "LOW ATTEN: low cut, slightly above the boost frequency, so boost and cut together "
                                   "give the Pultec lift with a dip above it rather than cancelling" },
            { params::hfFreq,      "HIGH FREQUENCY: centre of the high boost (3, 4, 5, 8, 10, 12 or 16 kHz)" },
            { params::hfBoost,     "HIGH BOOST: resonant high boost at the selected frequency" },
            { params::hfBandwidth, "BANDWIDTH: width of the high boost, Sharp to Broad" },
            { params::hfAttenFreq, "HIGH ATTEN frequency: 5, 10 or 20 kHz" },
            { params::hfAtten,     "HIGH ATTEN: high shelf cut at the selected frequency" },
            { params::drive,       "DRIVE: level into the transformers and the tube amplifier" },
            { params::character,   "CHARACTER: Clean (little colour, same EQ curves), Original, Burn (hotter transformer and tube drive)" },
            { params::mix,         "MIX: blend of the processed signal with the original input" },
            { params::output,      "OUTPUT: output level" },
        };

        for (auto& [id, tip] : tips)
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
            sp.core = juce::Colour (0xfffff0c0);
            sp.glow = juce::Colour (0xfff59a33);
            knob->setSkinPointer (sp);
            knob->setTooltip (tip);
            knob->onEditFinished = onEdit;
            addToPanel (*knob);

            KnobEntry e;
            e.id = id;
            e.hitbox = hitbox;
            e.labels = makeScaleLabels (*c, *param, [id = juce::String (id)] (float v, const juce::String& step)
                                        { return labelText (id, v, step); });
            e.dots = makeStepDots (*c, *param);
            if ((*c).hasProperty ("scaleLabels"))
                e.capHeight = (float) (*c)["scaleLabels"]["capHeight"];
            e.ink = (*c)["ink"].toString() == "light" ? lightInk : darkInk;
            e.knob = std::move (knob);
            knobs.push_back (std::move (e));
        }

        // ---- EQ IN
        {
            const auto* c = findControl (uiMap, params::eqIn);
            jassert (c != nullptr);
            const auto lampRect = rectFrom ((*c)["lamp"]["rect"]);
            const auto hitbox = rectFrom ((*c)["hitbox"]);
            eqInBounds = hitbox.getUnion (lampRect);
            juce::Image image;
            int size = 0;
            if (auto* data = SHAPESkin::getNamedResource ("eqin_lamp_png", size))
                image = juce::ImageCache::getFromMemory (data, size);
            jassert (image.isValid());
            // the click area is the round bezel
            const auto bezel = juce::Rectangle<float> (lampRect.getWidth(), lampRect.getHeight()).withCentre (pointFrom ((*c)["lamp"]["centre"]));
            eqIn = std::make_unique<EqInLamp> (*p.apvts.getParameter (params::eqIn),
                                               LampSprite { image, (lampRect - eqInBounds.getPosition()) * s },
                                               (bezel.getIntersection (hitbox) - eqInBounds.getPosition()) * s);
            eqIn->onClick = onEdit;
            addToPanel (*eqIn);
        }

        finishSetup();
    }

    ui::Knob& ShapeEditor::getKnob (const juce::String& parameterId)
    {
        for (auto& k : knobs)
            if (k.id == parameterId)
                return *k.knob;
        jassertfalse;
        return *knobs.front().knob;
    }

    const std::vector<ScaleLabel>& ShapeEditor::getScaleLabels (const juce::String& parameterId) const
    {
        for (auto& k : knobs)
            if (k.id == parameterId)
                return k.labels;
        static const std::vector<ScaleLabel> none;
        return none;
    }

    const std::vector<StepDot>& ShapeEditor::getStepDots (const juce::String& parameterId) const
    {
        for (auto& k : knobs)
            if (k.id == parameterId)
                return k.dots;
        static const std::vector<StepDot> none;
        return none;
    }

    juce::Rectangle<int> ShapeEditor::refToPanel (juce::Rectangle<float> r) const
    {
        return (r * skinScale).translated ((float) panelArea.getX(), (float) panelArea.getY()).toNearestInt();
    }

    void ShapeEditor::layoutPanel (juce::Rectangle<int> area)
    {
        panelArea = area;
        for (auto& k : knobs)
            k.knob->setBounds (refToPanel (k.hitbox));
        if (eqIn)
            eqIn->setBounds (refToPanel (eqInBounds));
    }

    void ShapeEditor::paintPanel (juce::Graphics& g, juce::Rectangle<int> area)
    {
        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        g.drawImage (plate, area.toFloat(), juce::RectanglePlacement::stretchToFit);

        const auto origin = area.getPosition().toFloat();
        for (auto& k : knobs)
            paintScale (g, k.dots, k.labels, k.capHeight, skinScale, origin, k.ink, k.ink);
    }
}
