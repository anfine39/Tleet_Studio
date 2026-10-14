#include "PluginEditor.h"
#include "Parameters.h"
#include "GLUESkin.h"

namespace tleet::glue
{
    namespace
    {
        // Printed text on the panel: warm cream, measured on the artwork.
        const juce::Colour panelInk { 0xffd9bea0 };
        const juce::Colour dotInk { 0xffb89a78 };
    }

    using namespace ui::skin;

    // ================================================================ arc lamp

    ArcLamp::ArcLamp (juce::RangedAudioParameter& p, juce::Point<float> c, float r,
                      float startDegrees, float endDegrees, float scale)
        : param (p),
          attachment (p, [this] (float) { repaint(); }),
          centre (c), radius (r), startDeg (startDegrees), endDeg (endDegrees), s (scale)
    {
        attachment.sendInitialUpdate();
        setInterceptsMouseClicks (false, false);
    }

    float ArcLamp::getLampAngleDegrees() const noexcept
    {
        return startDeg + param.getValue() * (endDeg - startDeg);
    }

    juce::Point<float> ArcLamp::getLampCentre() const noexcept
    {
        return polar (centre, radius, getLampAngleDegrees());
    }

    void ArcLamp::paint (juce::Graphics& g)
    {
        const auto p = getLampCentre();
        const float glowR = 17.0f * s;
        juce::ColourGradient glow (juce::Colour (0xffffc46a).withAlpha (0.85f), p.x, p.y,
                                   juce::Colour (0xffff9a2e).withAlpha (0.0f), p.x + glowR, p.y, true);
        glow.addColour (0.35, juce::Colour (0xffffa63a).withAlpha (0.45f));
        g.setGradientFill (glow);
        g.fillEllipse (juce::Rectangle<float> (glowR * 2.0f, glowR * 2.0f).withCentre (p));

        // the lit cell of the arc, along its tangent
        const float a = juce::degreesToRadians (getLampAngleDegrees());
        juce::Path cell;
        cell.addRoundedRectangle (-5.0f * s, -3.0f * s, 10.0f * s, 6.0f * s, 2.0f * s);
        g.setColour (juce::Colour (0xfffff3d6));
        g.fillPath (cell, juce::AffineTransform::rotation (a).translated (p));
    }

    // ======================================================= sidechain button

    SidechainButton::SidechainButton (GlueProcessor& p, juce::RangedAudioParameter& param)
        : juce::TextButton ("EXT SC"), proc (p), attachment (param, *this)
    {
        setClickingTogglesState (true);
        setTooltip ("Sidechain source. On: the detector listens to GLUE's sidechain input "
                    "(route a track to it in the host). The lamp is lit only while that key is "
                    "really used; with no key connected GLUE keeps its internal detector.");
        startTimerHz (10);
        refresh();
    }

    void SidechainButton::refresh()
    {
        const bool lit = proc.isUsingExternalSidechain();
        if (lit != lampLit)
        {
            lampLit = lit;
            repaint();
        }
    }

    void SidechainButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
    {
        auto& lf = getLookAndFeel();
        lf.drawButtonBackground (g, *this, findColour (juce::TextButton::buttonColourId), highlighted, down);

        auto r = getLocalBounds().toFloat();
        const auto lamp = juce::Rectangle<float> (7.0f, 7.0f).withCentre ({ r.getX() + 11.0f, r.getCentreY() });
        if (lampLit)
        {
            g.setColour (juce::Colour (0xffffa63a).withAlpha (0.35f));
            g.fillEllipse (lamp.expanded (3.0f));
            g.setColour (juce::Colour (0xffffd27a));
            g.fillEllipse (lamp);
        }
        else
        {
            g.setColour (juce::Colour (0xff2a1e15));
            g.fillEllipse (lamp);
            g.setColour (getToggleState() ? juce::Colour (0xffc98a3c) : juce::Colour (0xff5a4636));
            g.drawEllipse (lamp, 1.0f);
        }

        g.setColour (findColour (getToggleState() ? juce::TextButton::textColourOnId : juce::TextButton::textColourOffId));
        g.setFont (ui::TleetLookAndFeel::labelFont (juce::jmin (13.0f, r.getHeight() * 0.55f)));
        g.drawText (getButtonText(), r.withTrimmedLeft (18.0f), juce::Justification::centred, false);
    }

    // ================================================================== editor

    juce::var GlueEditor::loadUiMap()
    {
        return juce::JSON::parse (juce::String::fromUTF8 (GLUESkin::UI_MAP_json, GLUESkin::UI_MAP_jsonSize));
    }

    juce::String GlueEditor::labelText (const juce::String& id, float v, const juce::String& stepName)
    {
        if (stepName.isNotEmpty())
        {
            if (stepName.equalsIgnoreCase ("Auto"))
                return "AUTO";
            return stepName.upToFirstOccurrenceOf (" ", false, false);
        }
        const int n = juce::roundToInt (v);
        if (id == params::threshold || id == params::makeup || id == params::output)
            return n > 0 ? "+" + juce::String (n) : juce::String (n);
        return juce::String (n);
    }

    std::vector<ScaleLabel> GlueEditor::makeScaleLabels (const juce::var& c, juce::RangedAudioParameter& param)
    {
        const auto id = c["parameter"].toString();
        return ui::skin::makeScaleLabels (c, param, [id] (float v, const juce::String& step) { return labelText (id, v, step); });
    }

    std::vector<StepDot> GlueEditor::makeStepDots (const juce::var& c, juce::RangedAudioParameter& param)
    {
        return ui::skin::makeStepDots (c, param);
    }

    GlueEditor::GlueEditor (GlueProcessor& p)
        : ui::TleetEditorBase (p, baseWidth, toolbarHeight + panelHeight),
          proc (p),
          uiMap (loadUiMap()),
          plate (juce::ImageCache::getFromMemory (GLUESkin::plate_png, GLUESkin::plate_pngSize))
    {
        const float s = skinScale;
        auto onEdit = [this] { proc.commitHistory(); };

        for (const char* id : { params::threshold, params::attack, params::release, params::ratio, params::makeup,
                                params::scHpf, params::mix, params::character, params::dark, params::crush, params::output })
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
            e.dots = makeStepDots (*c, *param);
            if ((*c).hasProperty ("scaleLabels"))
                e.capHeight = (float) (*c)["scaleLabels"]["capHeight"];
            e.knob = std::move (knob);
            knobs.push_back (std::move (e));

            if (juce::String (id) == params::character)
            {
                const auto& lamp = (*c)["arcLamp"];
                const float r = (float) lamp["radius"];
                arcBounds = juce::Rectangle<float> (2.0f * r + 50.0f, r + 40.0f).withCentre (centre.translated (0.0f, -r * 0.5f));
                arcLamp = std::make_unique<ArcLamp> (*param, (centre - arcBounds.getPosition()) * s, r * s,
                                                     (float) (*c)["travelDegrees"][0], (float) (*c)["travelDegrees"][1], s);
                addToPanel (*arcLamp);
            }
        }

        // ---- GR LED meter
        {
            const auto* c = findControl (uiMap, "grMeter");
            jassert (c != nullptr);
            meterBounds = rectFrom ((*c)["hitbox"]);
            meter = std::make_unique<GrLedMeter> (ui::skin::meterGeometry (*c, meterBounds, s));
            meter->setSupplier ([this] { return proc.grMeter.takeGainReduction(); });
            addToPanel (*meter);
        }

        // ---- external sidechain switch (not on the artwork: master spec 12)
        if (auto* sc = p.apvts.getParameter (params::scExternal))
        {
            scButton = std::make_unique<SidechainButton> (p, *sc);
            scButton->onClick = onEdit;
            addToToolbar (*scButton, 78);
        }

        finishSetup();
    }

    ui::Knob& GlueEditor::getKnob (const juce::String& parameterId)
    {
        for (auto& k : knobs)
            if (k.id == parameterId)
                return *k.knob;
        jassertfalse;
        return *knobs.front().knob;
    }

    const std::vector<ScaleLabel>& GlueEditor::getScaleLabels (const juce::String& parameterId) const
    {
        for (auto& k : knobs)
            if (k.id == parameterId)
                return k.labels;
        static const std::vector<ScaleLabel> none;
        return none;
    }

    const std::vector<StepDot>& GlueEditor::getStepDots (const juce::String& parameterId) const
    {
        for (auto& k : knobs)
            if (k.id == parameterId)
                return k.dots;
        static const std::vector<StepDot> none;
        return none;
    }

    juce::Rectangle<int> GlueEditor::refToPanel (juce::Rectangle<float> r) const
    {
        return (r * skinScale).translated ((float) panelArea.getX(), (float) panelArea.getY()).toNearestInt();
    }

    void GlueEditor::layoutPanel (juce::Rectangle<int> area)
    {
        panelArea = area;
        for (auto& k : knobs)
            k.knob->setBounds (refToPanel (k.hitbox));
        if (meter)   meter->setBounds (refToPanel (meterBounds));
        if (arcLamp) arcLamp->setBounds (refToPanel (arcBounds));
    }

    void GlueEditor::paintPanel (juce::Graphics& g, juce::Rectangle<int> area)
    {
        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        g.drawImage (plate, area.toFloat(), juce::RectanglePlacement::stretchToFit);

        const auto origin = area.getPosition().toFloat();
        const float s = skinScale;
        for (auto& k : knobs)
            ui::skin::paintScale (g, k.dots, k.labels, k.capHeight, s, origin, dotInk, panelInk);
    }
}
