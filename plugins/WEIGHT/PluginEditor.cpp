#include "PluginEditor.h"
#include "Parameters.h"
#include "WEIGHTSkin.h"

namespace tleet::weight
{
    namespace
    {
        // Printed text and scale dots on the panel: warm cream, measured on the artwork.
        const juce::Colour panelInk { 0xffe9d3b4 };
        const juce::Colour dotInk { 0xffe6cfa8 };
    }

    using namespace ui::skin;

    // ================================================================== lamps

    LampButton::LampButton (const juce::String& name, LampSprite s, juce::Rectangle<float> area)
        : juce::Button (name), sprite (std::move (s)), clickArea (area)
    {
        setClickingTogglesState (false);
    }

    void LampButton::paintButton (juce::Graphics& g, bool, bool)
    {
        // the plate shows the lamp dark; only the lit lamp is drawn
        if (getToggleState() && sprite.image.isValid())
        {
            g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
            g.drawImage (sprite.image, sprite.area, juce::RectanglePlacement::stretchToFit);
        }
    }

    LampGroup::LampGroup (juce::RangedAudioParameter& p, std::function<void()> edited)
        : attachment (p, [this] (float v) { show (v); }), onEdit (std::move (edited))
    {
    }

    void LampGroup::add (std::unique_ptr<LampButton> lamp)
    {
        const int index = (int) lamps.size();
        lamp->onClick = [this, index]
        {
            attachment.setValueAsCompleteGesture ((float) index);
            if (onEdit)
                onEdit();
        };
        lamps.push_back (std::move (lamp));
        attachment.sendInitialUpdate();
    }

    void LampGroup::show (float value)
    {
        selected = juce::roundToInt (value);
        for (size_t i = 0; i < lamps.size(); ++i)
            lamps[i]->setToggleState ((int) i == selected, juce::dontSendNotification);
    }

    SidechainLamp::SidechainLamp (WeightProcessor& p, juce::RangedAudioParameter& param, LampSprite s)
        : juce::Button (param.getName (64)), proc (p), attachment (param, *this), sprite (std::move (s))
    {
        setClickingTogglesState (true);
        setTooltip ("External sidechain. On: the detector listens to WEIGHT's sidechain input "
                    "(route a track to it in the host); THRUST and the HPF still shape that key. "
                    "The lamp is lit only while the key is really used; switched on with no key "
                    "connected it glows faintly and WEIGHT keeps its internal detector.");
        attachment.sendInitialUpdate();
        startTimerHz (10);
        refresh();
    }

    void SidechainLamp::refresh()
    {
        const bool lit = proc.isUsingExternalSidechain();
        if (lit != lampLit)
        {
            lampLit = lit;
            repaint();
        }
    }

    void SidechainLamp::paintButton (juce::Graphics& g, bool, bool)
    {
        if (! sprite.image.isValid() || ! (lampLit || getToggleState()))
            return;
        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        g.setOpacity (lampLit ? 1.0f : armedAlpha);
        g.drawImage (sprite.image, sprite.area, juce::RectanglePlacement::stretchToFit);
    }

    // ================================================================== editor

    juce::var WeightEditor::loadUiMap()
    {
        return juce::JSON::parse (juce::String::fromUTF8 (WEIGHTSkin::UI_MAP_json, WEIGHTSkin::UI_MAP_jsonSize));
    }

    juce::String WeightEditor::labelText (const juce::String& id, float v, const juce::String& stepName)
    {
        if (stepName.isNotEmpty())
        {
            // "0.03 ms" -> ".03", "1 s" -> "1": the unit is printed under the knob
            auto t = stepName.upToFirstOccurrenceOf (" ", false, false);
            if (t.startsWith ("0."))
                t = t.substring (1);
            return t;
        }
        const int n = juce::roundToInt (v);
        if (id == params::threshold || id == params::makeup || id == params::output)
            return n > 0 ? "+" + juce::String (n) : juce::String (n);
        return juce::String (n);
    }

    juce::Rectangle<float> WeightEditor::spriteArea (const juce::String& name, juce::Rectangle<float> buttonRef) const
    {
        const auto& sp = uiMap["sprites"][juce::Identifier (name)];
        const auto rect = rectFrom (sp["rect"]);
        const auto anchor = rectFrom (sp["button"]);
        return rect + (buttonRef.getCentre() - anchor.getCentre());
    }

    juce::Image WeightEditor::spriteImage (const juce::String& name) const
    {
        int size = 0;
        if (auto* data = WEIGHTSkin::getNamedResource ((name + "_png").toRawUTF8(), size))
            return juce::ImageCache::getFromMemory (data, size);
        jassertfalse;
        return {};
    }

    WeightEditor::WeightEditor (WeightProcessor& p)
        : ui::TleetEditorBase (p, baseWidth, toolbarHeight + panelHeight),
          proc (p),
          uiMap (loadUiMap()),
          plate (juce::ImageCache::getFromMemory (WEIGHTSkin::plate_png, WEIGHTSkin::plate_pngSize))
    {
        const float s = skinScale;
        auto onEdit = [this] { proc.commitHistory(); };

        for (const char* id : { params::threshold, params::attack, params::release, params::ratio, params::makeup,
                                params::scHpf, params::mix, params::output, params::link })
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
            e.knob = std::move (knob);
            knobs.push_back (std::move (e));
        }

        // ---- lamp groups
        for (const char* id : { params::thrust, params::detector, params::tone, params::grMode })
        {
            const auto* c = findControl (uiMap, id);
            jassert (c != nullptr);
            auto* param = p.apvts.getParameter (id);
            jassert (param != nullptr);

            GroupEntry e;
            e.id = id;
            e.group = std::make_unique<LampGroup> (*param, onEdit);
            for (auto& b : *(*c)["buttons"].getArray())
            {
                const auto rect = rectFrom (b["rect"]);
                const auto hitbox = rectFrom (b["hitbox"]);
                const auto glow = spriteArea (b["sprite"].toString(), rect);
                const auto bounds = hitbox.getUnion (glow);
                LampSprite sprite { spriteImage (b["sprite"].toString()), (glow - bounds.getPosition()) * s };
                auto lamp = std::make_unique<LampButton> (param->getName (64) + " " + b["label"].toString(),
                                                          std::move (sprite), (hitbox - bounds.getPosition()) * s);
                lamp->setTooltip (param->getName (64) + ": " + b["label"].toString()
                                  + (juce::String (id) == params::grMode ? " (what the gain-reduction meters show; the audio is not affected)" : ""));
                addToPanel (*lamp);
                e.bounds.push_back (bounds);
                e.group->add (std::move (lamp));
            }
            groups.push_back (std::move (e));
        }

        // ---- EXTERNAL SC
        {
            const auto* c = findControl (uiMap, params::scExternal);
            jassert (c != nullptr);
            scBounds = rectFrom ((*c)["hitbox"]);
            const auto area = spriteArea ("lamp_ext", rectFrom ((*c)["lamp"]["rect"]));
            scLamp = std::make_unique<SidechainLamp> (p, *p.apvts.getParameter (params::scExternal),
                                                      LampSprite { spriteImage ("lamp_ext"), (area - scBounds.getPosition()) * s });
            scLamp->onClick = onEdit;
            addToPanel (*scLamp);
        }

        // ---- GR meters, one per channel
        for (int ch = 0; ch < 2; ++ch)
        {
            const auto* c = findControl (uiMap, ch == 0 ? "grMeterL" : "grMeterR");
            jassert (c != nullptr);
            meterBounds[(size_t) ch] = rectFrom ((*c)["hitbox"]);
            meters[(size_t) ch] = std::make_unique<ArcLedMeter> (arcMeterGeometry (*c, meterBounds[(size_t) ch], s));
            meters[(size_t) ch]->setSupplier ([this, ch] { return proc.takeGrDb (ch); });
            addToPanel (*meters[(size_t) ch]);
        }

        finishSetup();
    }

    ui::Knob& WeightEditor::getKnob (const juce::String& parameterId)
    {
        for (auto& k : knobs)
            if (k.id == parameterId)
                return *k.knob;
        jassertfalse;
        return *knobs.front().knob;
    }

    LampGroup& WeightEditor::getLampGroup (const juce::String& parameterId)
    {
        for (auto& g : groups)
            if (g.id == parameterId)
                return *g.group;
        jassertfalse;
        return *groups.front().group;
    }

    const std::vector<ScaleLabel>& WeightEditor::getScaleLabels (const juce::String& parameterId) const
    {
        for (auto& k : knobs)
            if (k.id == parameterId)
                return k.labels;
        static const std::vector<ScaleLabel> none;
        return none;
    }

    const std::vector<StepDot>& WeightEditor::getStepDots (const juce::String& parameterId) const
    {
        for (auto& k : knobs)
            if (k.id == parameterId)
                return k.dots;
        static const std::vector<StepDot> none;
        return none;
    }

    juce::Rectangle<int> WeightEditor::refToPanel (juce::Rectangle<float> r) const
    {
        return (r * skinScale).translated ((float) panelArea.getX(), (float) panelArea.getY()).toNearestInt();
    }

    void WeightEditor::layoutPanel (juce::Rectangle<int> area)
    {
        panelArea = area;
        for (auto& k : knobs)
            k.knob->setBounds (refToPanel (k.hitbox));
        for (auto& g : groups)
            for (int i = 0; i < g.group->getNumLamps(); ++i)
                g.group->getLamp (i).setBounds (refToPanel (g.bounds[(size_t) i]));
        if (scLamp)
            scLamp->setBounds (refToPanel (scBounds));
        for (size_t ch = 0; ch < 2; ++ch)
            if (meters[ch])
                meters[ch]->setBounds (refToPanel (meterBounds[ch]));
    }

    void WeightEditor::paintPanel (juce::Graphics& g, juce::Rectangle<int> area)
    {
        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        g.drawImage (plate, area.toFloat(), juce::RectanglePlacement::stretchToFit);

        const auto origin = area.getPosition().toFloat();
        for (auto& k : knobs)
            paintScale (g, k.dots, k.labels, k.capHeight, skinScale, origin, dotInk, panelInk);
    }
}
