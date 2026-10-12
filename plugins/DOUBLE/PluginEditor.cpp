#include "PluginEditor.h"
#include "Parameters.h"
#include "dsp/WetFilter.h"
#include "DOUBLESkin.h"

namespace tleet::dbl
{
    juce::var DoubleEditor::loadUiMap()
    {
        return juce::JSON::parse (juce::String::fromUTF8 (DOUBLESkin::UI_MAP_json, DOUBLESkin::UI_MAP_jsonSize));
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

    static juce::Image sprite (const char* name)
    {
        int size = 0;
        if (auto* data = DOUBLESkin::getNamedResource (name, size))
            return juce::ImageCache::getFromMemory (data, size);
        jassertfalse;
        return {};
    }

    // Lit ink of the artwork: white-hot pointer cores in a rose glow, readouts in warm rose-white.
    static const juce::Colour pointerCore { 0xfffff6f4 }, pointerGlow { 0xffe8899a }, readoutInk { 0xfff4d7ca };

    DoubleEditor::DoubleEditor (DoubleProcessor& p)
        : ui::TleetEditorBase (p, baseWidth, toolbarHeight + panelHeight),
          proc (p),
          uiMap (loadUiMap()),
          plate (juce::ImageCache::getFromMemory (DOUBLESkin::plate_png, DOUBLESkin::plate_pngSize))
    {
        const float s = skinScale;
        auto onEdit = [this] { proc.commitHistory(); };

        // ---- knobs with their readout windows
        for (const char* id : { params::detune, params::delay, params::humanize, params::width,
                                params::hpf, params::lpf, params::mix, params::output })
        {
            const auto* c = findControl (uiMap, id);
            jassert (c != nullptr);
            auto* param = p.apvts.getParameter ((*c)["parameter"].toString());
            jassert (param != nullptr);

            const auto hitbox = rectFrom ((*c)["hitbox"]);
            const auto& ptr = (*c)["pointer"];
            const auto& travel = (*c)["travelDegrees"];

            auto knob = std::make_unique<ui::Knob> (*param, param->getName (64));
            ui::Knob::SkinPointer sp;
            sp.centre = (pointFrom ((*c)["centre"]) - hitbox.getPosition()) * s;
            sp.innerRadius = (float) ptr["inner"] * s;
            sp.outerRadius = (float) ptr["outer"] * s;
            sp.width = (float) ptr["width"] * s;
            sp.startAngle = juce::degreesToRadians ((float) travel[0]);
            sp.endAngle = juce::degreesToRadians ((float) travel[1]);
            sp.core = pointerCore;
            sp.glow = pointerGlow;
            sp.readout = (rectFrom ((*c)["readout"]) - hitbox.getPosition()) * s;
            sp.readoutTextHeight = (float) (*c)["readoutTextHeight"] / 0.72f * s; // cap height -> font height
            sp.readoutColour = readoutInk;
            knob->setSkinPointer (sp);
            knob->onEditFinished = onEdit;
            addToPanel (*knob);
            knobs.push_back ({ id, std::move (knob), hitbox });
        }

        // ---- VOICES and MODE: interlocked lamp buttons
        voices = makeGroup ("voices", params::voices, params::voicesNames,
                            { { "voices_lit_0_png", 0 }, { "voices_lit_1_png", 1 }, { "voices_lit_2_png", 2 } }, voicesBounds);
        mode = makeGroup ("mode", params::mode, params::modeNames,
                          { { "mode_lit_0_png", 0 }, { "mode_lit_1_png", 1 }, { "mode_lit_2_png", 2 } }, modeBounds);

        // ---- MONO SAFE / AUTO LEVEL
        for (auto [id, spriteName] : { std::pair<const char*, const char*> { params::monoSafe, "toggle_lit_0_png" },
                                       std::pair<const char*, const char*> { params::autoLevel, "toggle_lit_1_png" } })
        {
            const auto* c = findControl (uiMap, id);
            jassert (c != nullptr);
            const auto lit = rectFrom ((*c)["litSprite"]);
            const auto hit = rectFrom ((*c)["hitbox"]);
            const auto bounds = lit.getUnion (hit);
            auto* param = p.apvts.getParameter (id);
            auto t = std::make_unique<ui::LampToggle> (*param, param->getName (64), sprite (spriteName),
                                                       (lit - bounds.getPosition()) * s, (hit - bounds.getPosition()) * s);
            t->onEditFinished = onEdit;
            addToPanel (*t);
            toggles.push_back ({ id, std::move (t), bounds });
        }

        // ---- voice display
        {
            const auto* c = findControl (uiMap, "voiceDisplay");
            jassert (c != nullptr);
            voiceDisplayBounds = rectFrom ((*c)["hitbox"]);
            const auto o = voiceDisplayBounds.getPosition();
            VoiceDisplay::Geometry g;
            g.screen = (rectFrom ((*c)["screen"]) - o) * s;
            g.centreX = ((float) (*c)["centreX"] - o.x) * s;
            g.axisY = ((float) (*c)["axisY"] - o.y) * s;
            g.baselineY = ((float) (*c)["baselineY"] - o.y) * s;
            g.baselineX0 = ((float) (*c)["baselineX"][0] - o.x) * s;
            g.baselineX1 = ((float) (*c)["baselineX"][1] - o.x) * s;
            g.scale = s;
            voiceDisplay = std::make_unique<VoiceDisplay> (p.telemetry, g);
            addToPanel (*voiceDisplay);
        }

        // ---- filter display (exact response of the live wet filters)
        {
            const auto* c = findControl (uiMap, "filterDisplay");
            jassert (c != nullptr);
            filterDisplayBounds = rectFrom ((*c)["hitbox"]);
            const auto o = filterDisplayBounds.getPosition();
            FilterDisplay::Geometry g;
            g.plot = (rectFrom ((*c)["plot"]) - o) * s;
            g.labelRow = (rectFrom ((*c)["labelRow"]) - o) * s;
            g.minDb = (float) (*c)["dbRange"][0];
            g.maxDb = (float) (*c)["dbRange"][1];
            g.minHz = (float) (*c)["frequencyRange"][0];
            g.maxHz = (float) (*c)["frequencyRange"][1];
            g.labelTextHeight = (float) (*c)["labelTextHeight"];
            g.scale = s;
            // the whole wet tone: HPF, LPF and the mode's high shelf (GHOST is darker)
            auto* hpf = p.apvts.getRawParameterValue (params::hpf);
            auto* lpf = p.apvts.getRawParameterValue (params::lpf);
            auto* md  = p.apvts.getRawParameterValue (params::mode);
            filterDisplay = std::make_unique<FilterDisplay> (
                g,
                [this, hpf, lpf, md] (double hz)
                { return wetFilterResponseDb (hz, hpf->load(), lpf->load(), proc.getWetSampleRate(), juce::roundToInt (md->load())); },
                [this, hpf, lpf, md]
                { return (double) hpf->load() * 1.0e6 + (double) lpf->load() + md->load() * 1.0e-1 + proc.getWetSampleRate() * 1.0e-7; });
            addToPanel (*filterDisplay);
        }

        finishSetup();
    }

    std::unique_ptr<ui::LampButtonGroup> DoubleEditor::makeGroup (const juce::String& id, const char* param, const juce::StringArray& names,
                                                                   std::initializer_list<std::pair<const char*, int>> sprites,
                                                                   juce::Rectangle<float>& boundsOut)
    {
        const float s = skinScale;
        const auto* c = findControl (uiMap, id);
        jassert (c != nullptr);
        juce::Rectangle<float> all;
        for (auto* key : { "hitboxes", "litSprites" })
            for (auto& r : *(*c)[key].getArray())
                all = all.isEmpty() ? rectFrom (r) : all.getUnion (rectFrom (r));
        boundsOut = all;

        ui::LampButtonGroup::Geometry g;
        for (int i = 0; i < (*c)["hitboxes"].size(); ++i)
        {
            g.hitboxes.push_back ((rectFrom ((*c)["hitboxes"][i]) - all.getPosition()) * s);
            g.litBounds.push_back ((rectFrom ((*c)["litSprites"][i]) - all.getPosition()) * s);
        }
        for (auto& [name, index] : sprites)
        {
            juce::ignoreUnused (index);
            g.litPerState.push_back (sprite (name));
        }
        auto group = std::make_unique<ui::LampButtonGroup> (*proc.apvts.getParameter (param),
                                                            proc.apvts.getParameter (param)->getName (64), names, std::move (g));
        group->onEditFinished = [this] { proc.commitHistory(); };
        addToPanel (*group);
        return group;
    }

    ui::Knob& DoubleEditor::getKnob (const juce::String& parameterId)
    {
        for (auto& k : knobs)
            if (k.id == parameterId)
                return *k.knob;
        jassertfalse;
        return *knobs.front().knob;
    }

    ui::LampToggle& DoubleEditor::getToggle (const juce::String& parameterId)
    {
        for (auto& t : toggles)
            if (t.id == parameterId)
                return *t.toggle;
        jassertfalse;
        return *toggles.front().toggle;
    }

    juce::Rectangle<int> DoubleEditor::refToPanel (juce::Rectangle<float> r) const
    {
        return (r * skinScale).translated ((float) panelArea.getX(), (float) panelArea.getY()).toNearestInt();
    }

    void DoubleEditor::layoutPanel (juce::Rectangle<int> area)
    {
        panelArea = area;
        for (auto& k : knobs)
            k.knob->setBounds (refToPanel (k.hitbox));
        for (auto& t : toggles)
            t.toggle->setBounds (refToPanel (t.bounds));
        if (voices) voices->setBounds (refToPanel (voicesBounds));
        if (mode)   mode->setBounds (refToPanel (modeBounds));
        if (voiceDisplay)  voiceDisplay->setBounds (refToPanel (voiceDisplayBounds));
        if (filterDisplay) filterDisplay->setBounds (refToPanel (filterDisplayBounds));
    }

    void DoubleEditor::paintPanel (juce::Graphics& g, juce::Rectangle<int> area)
    {
        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        g.drawImage (plate, area.toFloat(), juce::RectanglePlacement::stretchToFit);
    }
}
