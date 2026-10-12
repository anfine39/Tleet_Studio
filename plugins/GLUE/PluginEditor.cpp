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

        juce::Rectangle<float> rectFrom (const juce::var& v)
        {
            return { (float) v[0], (float) v[1], (float) v[2], (float) v[3] };
        }

        juce::Point<float> pointFrom (const juce::var& v)
        {
            return { (float) v[0], (float) v[1] };
        }

        const juce::var* findControl (const juce::var& map, const juce::String& id)
        {
            if (auto* arr = map["controls"].getArray())
                for (auto& c : *arr)
                    if (c["id"].toString() == id)
                        return &c;
            return nullptr;
        }

        juce::Point<float> polar (juce::Point<float> c, float radius, float degrees)
        {
            const float a = juce::degreesToRadians (degrees);
            return { c.x + radius * std::sin (a), c.y - radius * std::cos (a) };
        }

        // Font of the live scale numbers; `capHeight` is the printed digit
        // height in reference pixels (the font's cap height is about 0.72 em).
        juce::Font scaleFont (float capHeight)
        {
            return ui::TleetLookAndFeel::labelFont (capHeight / 0.72f, false).withHorizontalScale (0.84f);
        }

        float angleFor (const juce::var& c, float normalised)
        {
            const float t0 = (float) c["travelDegrees"][0], t1 = (float) c["travelDegrees"][1];
            return t0 + juce::jlimit (0.0f, 1.0f, normalised) * (t1 - t0);
        }
    }

    // ================================================================== meter

    GrLedMeter::GrLedMeter (Geometry g) : geo (std::move (g))
    {
        setInterceptsMouseClicks (false, false);
        startTimerHz (30);
    }

    int GrLedMeter::litSegmentsFor (float grDb) noexcept
    {
        // segment k covers k ... k+1 dB and lights once the reduction reaches k + 0.5 dB
        return juce::jlimit (0, numSegments, (int) std::floor (grDb + 0.5f));
    }

    float GrLedMeter::xForDb (float db) const noexcept
    {
        const auto& s = geo.scale;
        if (s.empty())
            return 0.0f;
        if (db <= s.front().first)
            return s.front().second;
        for (size_t i = 1; i < s.size(); ++i)
            if (db <= s[i].first)
            {
                const float t = (db - s[i - 1].first) / (s[i].first - s[i - 1].first);
                return s[i - 1].second + t * (s[i].second - s[i - 1].second);
            }
        return s.back().second;
    }

    juce::Rectangle<float> GrLedMeter::getSegmentBounds (int k) const noexcept
    {
        const float x0 = xForDb ((float) k) + geo.gap * 0.5f;
        const float x1 = xForDb ((float) k + 1.0f) - geo.gap * 0.5f;
        return { x0, geo.rowTop, x1 - x0, geo.rowBottom - geo.rowTop };
    }

    void GrLedMeter::refresh (float seconds)
    {
        if (supplier)
        {
            const float v = supplier();
            if (v >= 0.0f)
            {
                if (v >= displayed)
                    displayed = v;
                else
                    displayed = v + (displayed - v) * std::exp (-seconds / fallSeconds);
            }
        }
        const int lit = getLitSegments();
        if (lit != paintedLit)
        {
            paintedLit = lit;
            repaint();
        }
    }

    void GrLedMeter::paint (juce::Graphics& g)
    {
        const int lit = getLitSegments();
        const float h = geo.rowBottom - geo.rowTop;

        // glow of the lit part behind the segments
        if (lit > 0)
        {
            auto bar = getSegmentBounds (0).getUnion (getSegmentBounds (lit - 1));
            for (int i = 4; i >= 1; --i)
            {
                g.setColour (juce::Colour (0xffffa63a).withAlpha (0.045f * (float) (5 - i)));
                g.fillRoundedRectangle (bar.expanded (h * 0.28f * (float) i, h * 0.32f * (float) i), h * 0.4f * (float) i);
            }
        }

        for (int k = 0; k < numSegments; ++k)
        {
            const auto r = getSegmentBounds (k);
            if (k < lit)
            {
                juce::ColourGradient grad (juce::Colour (0xffd8913a), r.getX(), r.getY(),
                                           juce::Colour (0xffd8913a), r.getX(), r.getBottom(), false);
                grad.addColour (0.5, juce::Colour (0xfffff0a0));
                g.setGradientFill (grad);
                g.fillRect (r);
                g.setColour (juce::Colour (0xfffffbe0).withAlpha (0.55f));
                g.fillRect (r.reduced (r.getWidth() * 0.18f, h * 0.36f));
            }
            else
            {
                juce::ColourGradient grad (juce::Colour (0xff5e3a16), r.getX(), r.getY(),
                                           juce::Colour (0xff3a240d), r.getX(), r.getBottom(), false);
                g.setGradientFill (grad);
                g.fillRect (r);
                g.setColour (juce::Colour (0xff120b05).withAlpha (0.7f));
                g.drawRect (r, 0.6f);
            }
        }
    }

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
        std::vector<ScaleLabel> out;
        if (! c.hasProperty ("scaleLabels"))
            return out;

        const auto& sl = c["scaleLabels"];
        const auto centre = pointFrom (c["scaleCentre"]);
        const float radius = (float) c["dotRadius"] + (float) sl["gap"];
        const float capHeight = (float) sl["capHeight"];
        const auto font = scaleFont (capHeight);
        const auto id = c["parameter"].toString();

        std::vector<std::pair<float, juce::String>> items; // (normalised, step name)
        if (sl["values"].isString()) // "steps": one label per real position
        {
            const auto names = param.getAllValueStrings();
            const int n = param.getNumSteps();
            for (int i = 0; i < n; ++i)
                items.emplace_back ((float) i / (float) juce::jmax (1, n - 1), names[i]);
        }
        else
        {
            for (auto& v : *sl["values"].getArray())
                items.emplace_back (param.convertTo0to1 ((float) v), juce::String());
        }

        for (auto& [norm, stepName] : items)
        {
            const float value = param.convertFrom0to1 (norm);
            const auto text = labelText (id, value, stepName);
            const float deg = angleFor (c, norm);
            const float a = juce::degreesToRadians (deg);
            const float w = juce::GlyphArrangement::getStringWidth (font, text) + 4.0f;
            const float fontH = font.getHeight();
            // the label's nearest edge sits just outside the dot circle
            const float push = radius + 0.5f * w * std::abs (std::sin (a)) + 0.5f * capHeight * std::abs (std::cos (a));
            const auto mid = polar (centre, push, deg);
            // drawText centres the em box; digits sit above its middle, so move the box down
            const auto box = juce::Rectangle<float> (w, fontH).withCentre (mid).translated (0.0f, fontH * 0.07f);
            out.push_back ({ value, text, deg, box });
        }
        return out;
    }

    std::vector<StepDot> GlueEditor::makeStepDots (const juce::var& c, juce::RangedAudioParameter& param)
    {
        std::vector<StepDot> out;
        if (! c.hasProperty ("stepDots"))
            return out;
        const auto centre = pointFrom (c["scaleCentre"]);
        const float radius = (float) c["dotRadius"];
        const float r = (float) c["stepDots"]["radius"];
        const int n = param.getNumSteps();
        for (int i = 0; i < n; ++i)
        {
            const float deg = angleFor (c, (float) i / (float) juce::jmax (1, n - 1));
            out.push_back ({ polar (centre, radius, deg), r, deg });
        }
        return out;
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
            GrLedMeter::Geometry g;
            if (auto* props = (*c)["scale"].getDynamicObject())
                for (auto& prop : props->getProperties())
                    g.scale.emplace_back (prop.name.toString().getFloatValue(),
                                          ((float) prop.value - meterBounds.getX()) * s);
            std::sort (g.scale.begin(), g.scale.end());
            g.rowTop = ((float) (*c)["segmentRow"][0] - meterBounds.getY()) * s;
            g.rowBottom = ((float) (*c)["segmentRow"][1] - meterBounds.getY()) * s;
            g.gap = (float) (*c)["segmentGap"] * s;
            meter = std::make_unique<GrLedMeter> (std::move (g));
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
        {
            for (auto& d : k.dots)
            {
                const auto c = d.centre * s + origin;
                const float r = d.radius * s;
                g.setColour (dotInk.withAlpha (0.25f));
                g.fillEllipse (juce::Rectangle<float> (r * 3.0f, r * 3.0f).withCentre (c));
                g.setColour (dotInk);
                g.fillEllipse (juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (c));
            }

            if (k.labels.empty())
                continue;
            const auto font = scaleFont (k.capHeight * s);
            g.setFont (font);
            g.setColour (panelInk);
            for (auto& l : k.labels)
                g.drawText (l.text, (l.box * s) + origin, juce::Justification::centred, false);
        }
    }
}
