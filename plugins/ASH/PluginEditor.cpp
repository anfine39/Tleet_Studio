#include "PluginEditor.h"
#include "Parameters.h"
#include "ASHSkin.h"

namespace tleet::ash
{
    float meterReadingFor (MeterMode mode, float grDb, float outputRms) noexcept
    {
        switch (mode)
        {
            case MeterMode::gr:     return grDb < 0.0f ? std::nanf ("") : -grDb;
            case MeterMode::plus4:  return outputRms < 0.0f ? std::nanf ("") : juce::Decibels::gainToDecibels (outputRms, -100.0f) + 18.0f;
            case MeterMode::plus10: return outputRms < 0.0f ? std::nanf ("") : juce::Decibels::gainToDecibels (outputRms, -100.0f) + 12.0f;
        }
        return std::nanf ("");
    }

    juce::var AshEditor::loadUiMap()
    {
        return juce::JSON::parse (juce::String::fromUTF8 (ASHSkin::UI_MAP_json, ASHSkin::UI_MAP_jsonSize));
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

    AshEditor::AshEditor (AshProcessor& p)
        : ui::TleetEditorBase (p, baseWidth, toolbarHeight + panelHeight),
          proc (p),
          uiMap (loadUiMap()),
          plate (juce::ImageCache::getFromMemory (ASHSkin::plate_png, ASHSkin::plate_pngSize))
    {
        const float s = skinScale;
        auto onEdit = [this] { proc.commitHistory(); };

        // ---- rotary controls
        for (const char* id : { params::peakReduction, params::gain, params::mix, params::character, params::output })
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
        }

        // ---- Compress / Limit lever
        {
            const auto* c = findControl (uiMap, "mode");
            jassert (c != nullptr);
            modeHitbox = rectFrom ((*c)["hitbox"]);
            const auto pivot = pointFrom ((*c)["pivot"]);
            const auto box = rectFrom ((*c)["leverBox"]);

            ui::LeverSwitch::Geometry g;
            g.lever = juce::ImageCache::getFromMemory (ASHSkin::lever_png, ASHSkin::lever_pngSize);
            g.spritePivot = pivot - box.getPosition();
            g.pivot = (pivot - modeHitbox.getPosition()) * s;
            g.spriteScale = s;
            g.angleState0 = juce::degreesToRadians ((float) (*c)["leverDegrees"]["Compress"]);
            g.angleState1 = juce::degreesToRadians ((float) (*c)["leverDegrees"]["Limit"]);
            mode = std::make_unique<ui::LeverSwitch> (*p.apvts.getParameter (params::mode), "Mode",
                                                      juce::StringArray { "Compress", "Limit" }, std::move (g));
            mode->onEditFinished = onEdit;
            addToPanel (*mode);
        }

        // ---- VU meter (needle drawn from telemetry; face is part of the plate)
        {
            const auto* c = findControl (uiMap, "vu");
            jassert (c != nullptr);
            vuHitbox = rectFrom ((*c)["hitbox"]);
            const auto origin = vuHitbox.getPosition();

            ui::SkinVU::Geometry g;
            g.pivot = (pointFrom ((*c)["pivot"]) - origin) * s;
            g.needleLength = (float) (*c)["needleLength"] * s;
            const auto face = rectFrom ((*c)["face"]);
            g.face = juce::Rectangle<float> ((face.getX() - origin.x) * s, (face.getY() - origin.y) * s,
                                             (face.getWidth() - face.getX()) * s, (face.getHeight() - face.getY()) * s);
            if (auto* props = (*c)["scaleDegrees"].getDynamicObject())
                for (auto& prop : props->getProperties())
                    g.scale.emplace_back (prop.name.toString().getFloatValue(),
                                          juce::degreesToRadians ((float) prop.value));
            std::sort (g.scale.begin(), g.scale.end());
            g.modeLabelAnchor = (pointFrom ((*c)["modeLabelAnchor"]) - origin) * s;
            g.inkScale = s * 1.33f;

            vu = std::make_unique<ui::SkinVU> (std::move (g));
            vu->onClick = [this] { setMeterMode ((MeterMode) (((int) meterMode + 1) % 3)); };
            vu->setSupplier ([this]
            {
                const float gr = proc.grMeter.takeGainReduction();
                const float rms = proc.outputMeter.takeRms();
                return meterReadingFor (meterMode, gr, rms);
            });
            vu->setTooltip ("Click to switch the meter: GR / +4 / +10");
            addToPanel (*vu);
        }

        setMeterMode ((MeterMode) juce::jlimit (0, 2, (int) p.getUiProperty ("meterMode", 0)));
        finishSetup();
    }

    ui::Knob& AshEditor::getKnob (const juce::String& parameterId)
    {
        for (auto& k : knobs)
            if (k.id == parameterId)
                return *k.knob;
        jassertfalse;
        return *knobs.front().knob;
    }

    void AshEditor::setMeterMode (MeterMode m)
    {
        meterMode = m;
        proc.setUiProperty ("meterMode", (int) m);
        vu->setModeCaption (m == MeterMode::gr ? "GR" : m == MeterMode::plus4 ? "+4" : "+10");
        vu->setRestValue (m == MeterMode::gr ? 0.0f : -40.0f);
    }

    juce::Rectangle<int> AshEditor::refToPanel (juce::Rectangle<float> r) const
    {
        return (r * skinScale).translated ((float) panelArea.getX(), (float) panelArea.getY()).toNearestInt();
    }

    void AshEditor::layoutPanel (juce::Rectangle<int> area)
    {
        panelArea = area;
        for (auto& k : knobs)
            k.knob->setBounds (refToPanel (k.hitbox));
        if (mode) mode->setBounds (refToPanel (modeHitbox));
        if (vu)   vu->setBounds (refToPanel (vuHitbox));
    }

    void AshEditor::paintPanel (juce::Graphics& g, juce::Rectangle<int> area)
    {
        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        g.drawImage (plate, area.toFloat(), juce::RectanglePlacement::stretchToFit);
    }
}
