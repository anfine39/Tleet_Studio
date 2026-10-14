#include "PluginEditor.h"
#include "Parameters.h"
#include "BITESkin.h"

namespace tleet::bite
{
    float meterReadingFor (MeterMode mode, float grDb, float outputRms) noexcept
    {
        switch (mode)
        {
            case MeterMode::gr:    return grDb < 0.0f ? std::nanf ("") : -grDb;
            case MeterMode::plus4: return outputRms < 0.0f ? std::nanf ("") : juce::Decibels::gainToDecibels (outputRms, -100.0f) + 18.0f;
            case MeterMode::plus8: return outputRms < 0.0f ? std::nanf ("") : juce::Decibels::gainToDecibels (outputRms, -100.0f) + 14.0f;
        }
        return std::nanf ("");
    }

    juce::var BiteEditor::loadUiMap()
    {
        return juce::JSON::parse (juce::String::fromUTF8 (BITESkin::UI_MAP_json, BITESkin::UI_MAP_jsonSize));
    }

    static juce::Rectangle<float> rectFrom (const juce::var& v)
    {
        return { (float) v[0], (float) v[1], (float) v[2], (float) v[3] };
    }

    static juce::Point<float> pointFrom (const juce::var& v)
    {
        return { (float) v[0], (float) v[1] };
    }

    static const juce::var* findControl (const juce::var& map, const juce::String& id)
    {
        if (auto* arr = map["controls"].getArray())
            for (auto& c : *arr)
                if (c["id"].toString() == id)
                    return &c;
        return nullptr;
    }

    // Printed text on the panel: warm cream, measured on the artwork.
    static const juce::Colour panelInk { 0xffe5d4b4 };

    std::vector<ScaleLabel> BiteEditor::makeScaleLabels (const juce::var& c, juce::RangedAudioParameter& param)
    {
        std::vector<ScaleLabel> out;
        const auto& sl = c["scaleLabels"];
        const float t0 = (float) c["travelDegrees"][0], t1 = (float) c["travelDegrees"][1];
        const float scaleX = (float) c["scaleCentre"][0];
        const float h = (float) sl["textHeight"];

        for (int i = 0; i < sl["degrees"].size(); ++i)
        {
            // the knob position (normalised) at this dot, then the parameter's own mapping
            const float n = juce::jlimit (0.0f, 1.0f, ((float) sl["degrees"][i] - t0) / (t1 - t0));
            const float v = param.convertFrom0to1 (n);
            const auto anchor = pointFrom (sl["anchors"][i]);

            // left of the knob: text ends at the dot side; right: starts there; top: centred
            const float w = 48.0f, half = 11.0f;
            juce::Rectangle<float> box;
            juce::Justification j = juce::Justification::centred;
            if (anchor.x < scaleX - 30.0f)      { box = { anchor.x + half - w, anchor.y - h * 0.5f - 3.0f, w, h + 6.0f }; j = juce::Justification::centredRight; }
            else if (anchor.x > scaleX + 30.0f) { box = { anchor.x - half, anchor.y - h * 0.5f - 3.0f, w, h + 6.0f };     j = juce::Justification::centredLeft; }
            else                                  box = juce::Rectangle<float> (w, h + 6.0f).withCentre (anchor);

            out.push_back ({ v, juce::String (juce::roundToInt (v)), box, j });
        }
        return out;
    }

    BiteEditor::BiteEditor (BiteProcessor& p)
        : ui::TleetEditorBase (p, baseWidth, toolbarHeight + panelHeight),
          proc (p),
          uiMap (loadUiMap()),
          plate (juce::ImageCache::getFromMemory (BITESkin::plate_png, BITESkin::plate_pngSize))
    {
        const float s = skinScale;
        auto onEdit = [this] { proc.commitHistory(); };

        // ---- rotary controls (Character is a 3-position stepped knob)
        for (const char* id : { params::input, params::output, params::attack, params::release, params::mix, params::character })
        {
            const auto* c = findControl (uiMap, id);
            jassert (c != nullptr);
            auto* param = p.apvts.getParameter ((*c)["parameter"].toString());
            jassert (param != nullptr);

            const auto hitbox = rectFrom ((*c)["hitbox"]);
            const auto centre = pointFrom ((*c)["centre"]);
            const auto& ptr = (*c)["pointer"];
            const auto& travel = (*c)["travelDegrees"];

            auto knob = std::make_unique<ui::Knob> (*param, param->getName (64));
            ui::Knob::SkinPointer sp;
            sp.centre = (centre - hitbox.getPosition()) * s;
            sp.innerRadius = (float) ptr["inner"] * s;
            sp.outerRadius = (float) ptr["outer"] * s;
            sp.width = (float) ptr["width"] * s;
            sp.startAngle = juce::degreesToRadians ((float) travel[0]);
            sp.endAngle = juce::degreesToRadians ((float) travel[1]);
            knob->setSkinPointer (sp);
            knob->onEditFinished = onEdit;
            addToPanel (*knob);
            knobs.push_back ({ id, std::move (knob), hitbox });

            if ((*c).hasProperty ("scaleLabels"))
            {
                const auto unit = juce::String (id) == params::attack ? juce::String::fromUTF8 ("\xc2\xb5s") : juce::String ("ms");
                const auto& sl = (*c)["scaleLabels"];
                timeScales.push_back ({ id, makeScaleLabels (*c, *param), unit, pointFrom (sl["unitAnchor"]), (float) sl["textHeight"] });
            }
        }

        // ---- Ratio buttons
        {
            const auto* c = findControl (uiMap, "ratio");
            jassert (c != nullptr);
            juce::Rectangle<float> all;
            for (auto& hb : *(*c)["hitboxes"].getArray())
                all = all.isEmpty() ? rectFrom (hb) : all.getUnion (rectFrom (hb));
            ratioBounds = all;

            ui::LampButtonGroup::Geometry g;
            g.lit = juce::ImageCache::getFromMemory (BITESkin::ratio_lit_png, BITESkin::ratio_lit_pngSize);
            const auto litCell = rectFrom ((*c)["litCell"]);
            const float pitch = (float) (*c)["cellPitch"];
            const int n = (*c)["hitboxes"].size();
            for (int i = 0; i < n; ++i)
            {
                g.hitboxes.push_back ((rectFrom ((*c)["hitboxes"][i]) - all.getPosition()) * s);
                // the lit sprite was cut from the last button; the buttons are evenly spaced
                g.litBounds.push_back ((litCell.translated (pitch * (float) (i - (n - 1)), 0.0f) - all.getPosition()) * s);
            }
            ratio = std::make_unique<ui::LampButtonGroup> (*p.apvts.getParameter (params::ratio), "Ratio", params::ratioNames, std::move (g));
            ratio->onEditFinished = onEdit;
            addToPanel (*ratio);
        }

        // ---- VU meter (lamp-lit needle drawn from telemetry; face is part of the plate)
        {
            const auto* c = findControl (uiMap, "vu");
            jassert (c != nullptr);
            vuHitbox = rectFrom ((*c)["hitbox"]);
            const auto origin = vuHitbox.getPosition();

            ui::SkinVU::Geometry g;
            g.pivot = (pointFrom ((*c)["pivot"]) - origin) * s;
            g.needleLength = (float) (*c)["needleLength"] * s;
            const auto face = rectFrom ((*c)["face"]); // x0, y0, x1, y1
            g.face = juce::Rectangle<float> ((face.getX() - origin.x) * s, (face.getY() - origin.y) * s,
                                             (face.getWidth() - face.getX()) * s, (face.getHeight() - face.getY()) * s);
            if (auto* props = (*c)["scaleDegrees"].getDynamicObject())
                for (auto& prop : props->getProperties())
                    g.scale.emplace_back (prop.name.toString().getFloatValue(),
                                          juce::degreesToRadians ((float) prop.value));
            std::sort (g.scale.begin(), g.scale.end());
            g.modeLabelAnchor = (pointFrom ((*c)["modeLabelAnchor"]) - origin) * s;
            g.inkScale = s * 1.33f;
            // the artwork's needle is lit: near-white core in an amber halo, no shadow
            g.needleColour = juce::Colour (0xfffffbe6);
            g.glowColour = juce::Colour (0xfff4a422);
            g.needleWidth = 2.0f;
            g.shadow = false;
            g.captionColour = juce::Colour (0xff2a1a0c);

            vu = std::make_unique<ui::SkinVU> (std::move (g));
            vu->onClick = [this] { setMeterMode ((MeterMode) (((int) meterMode + 1) % 3)); };
            vu->setSupplier ([this]
            {
                const float gr = proc.grMeter.takeGainReduction();
                const float rms = proc.outputMeter.takeRms();
                return meterReadingFor (meterMode, gr, rms);
            });
            vu->setTooltip ("Click to switch the meter: GR / +4 / +8");
            addToPanel (*vu);
        }

        setMeterMode ((MeterMode) juce::jlimit (0, 2, (int) p.getUiProperty ("meterMode", 0)));
        finishSetup();
    }

    ui::Knob& BiteEditor::getKnob (const juce::String& parameterId)
    {
        for (auto& k : knobs)
            if (k.id == parameterId)
                return *k.knob;
        jassertfalse;
        return *knobs.front().knob;
    }

    const std::vector<ScaleLabel>& BiteEditor::getScaleLabels (const juce::String& parameterId) const
    {
        for (auto& t : timeScales)
            if (t.id == parameterId)
                return t.labels;
        static const std::vector<ScaleLabel> none;
        return none;
    }

    void BiteEditor::setMeterMode (MeterMode m)
    {
        meterMode = m;
        proc.setUiProperty ("meterMode", (int) m);
        vu->setModeCaption (m == MeterMode::gr ? "GR" : m == MeterMode::plus4 ? "+4" : "+8");
        vu->setRestValue (m == MeterMode::gr ? 0.0f : -40.0f);
    }

    juce::Rectangle<int> BiteEditor::refToPanel (juce::Rectangle<float> r) const
    {
        return (r * skinScale).translated ((float) panelArea.getX(), (float) panelArea.getY()).toNearestInt();
    }

    void BiteEditor::layoutPanel (juce::Rectangle<int> area)
    {
        panelArea = area;
        for (auto& k : knobs)
            k.knob->setBounds (refToPanel (k.hitbox));
        if (ratio) ratio->setBounds (refToPanel (ratioBounds));
        if (vu)    vu->setBounds (refToPanel (vuHitbox));
    }

    void BiteEditor::paintPanel (juce::Graphics& g, juce::Rectangle<int> area)
    {
        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        g.drawImage (plate, area.toFloat(), juce::RectanglePlacement::stretchToFit);

        // live Attack / Release scales
        const auto origin = area.getPosition().toFloat();
        g.setColour (panelInk);
        for (auto& t : timeScales)
        {
            const auto font = ui::TleetLookAndFeel::labelFont (t.textHeight * skinScale * 1.05f, false).withHorizontalScale (0.92f);
            g.setFont (font);
            for (auto& l : t.labels)
                g.drawText (l.text, (l.box * skinScale) + origin, l.justification, false);
            g.drawText (t.unit, (juce::Rectangle<float> (60.0f, t.textHeight + 6.0f).withCentre (t.unitAnchor) * skinScale) + origin,
                        juce::Justification::centred, false);
        }
    }
}
