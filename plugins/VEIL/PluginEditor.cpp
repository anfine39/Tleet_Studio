#include "PluginEditor.h"
#include "Parameters.h"
#include "VEILSkin.h"

namespace tleet::veil
{
    namespace
    {
        // Printed text on the dark panel (warm cream) and on the light
        // meter-bridge panel (dark ink), measured on the artwork.
        const juce::Colour panelInk { 0xffe4d0bd };
        const juce::Colour darkInk { 0xff2a221c };
        const juce::Colour readoutInk { 0xfff0e2dc };
    }

    using namespace ui::skin;

    // ================================================================== lamps

    SidechainLamps::SidechainLamps (VeilProcessor& p, LampGroup& g) : proc (p), group (g)
    {
        startTimerHz (10);
        refresh();
    }

    void SidechainLamps::refresh()
    {
        // INT = lamp 0, EXT = lamp 1
        const bool chosenExt = group.getSelected() == 1;
        const bool usingExt = proc.isUsingExternalSidechain();
        auto& in = group.getLamp (0);
        auto& ext = group.getLamp (1);
        in.setToggleState (! usingExt, juce::dontSendNotification);
        in.setLampAlpha (1.0f);
        ext.setToggleState (chosenExt, juce::dontSendNotification);
        ext.setLampAlpha (usingExt ? 1.0f : armedAlpha);
    }

    LinkSwitch::LinkSwitch (juce::RangedAudioParameter& p, std::unique_ptr<LampButton> l, std::unique_ptr<LampButton> d,
                            std::function<void()> edited)
        : attachment (p, [this] (float v) { show (v); }), lamp (std::move (l)), dualMono (std::move (d)), onEdit (std::move (edited))
    {
        auto toggle = [this]
        {
            attachment.setValueAsCompleteGesture (linked ? 0.0f : 1.0f);
            if (onEdit)
                onEdit();
        };
        lamp->onClick = toggle;
        dualMono->onClick = toggle;
        attachment.sendInitialUpdate();
    }

    void LinkSwitch::show (float value)
    {
        linked = value >= 0.5f;
        lamp->setToggleState (linked, juce::dontSendNotification);
        dualMono->setToggleState (! linked, juce::dontSendNotification);
    }

    // ================================================================== editor

    juce::var VeilEditor::loadUiMap()
    {
        return juce::JSON::parse (juce::String::fromUTF8 (VEILSkin::UI_MAP_json, VEILSkin::UI_MAP_jsonSize));
    }

    juce::String VeilEditor::labelText (const juce::String& id, float v)
    {
        const int n = juce::roundToInt (v);
        if (id == params::input || id == params::threshold || id == params::output)
            return n > 0 ? "+" + juce::String (n) : juce::String (n);
        if (id == params::hpf || id == params::scHpf)
            return params::filterIsOff (v) ? juce::String ("OFF") : juce::String (n);
        if (id == params::attack && v >= 1000.0f)
            return juce::String (v / 1000.0f, 0) + "K";
        if (id == params::recovery && v >= 1000.0f)
            return juce::String (v / 1000.0f, 0) + "S";
        return juce::String (n);
    }

    float VeilEditor::meterReading (params::MeterMode mode, float grDb, float inputRms, float outputRms) noexcept
    {
        switch (mode)
        {
            // gain reduction: the needle leaves 0 to the left by the applied dB
            case params::MeterMode::gr:    return grDb < 0.0f ? std::nanf ("") : -grDb;
            case params::MeterMode::vu:    return outputRms < 0.0f ? std::nanf ("") : params::vuDb (outputRms);
            case params::MeterMode::inOut: return inputRms < 0.0f ? std::nanf ("") : params::vuDb (inputRms);
        }
        return std::nanf ("");
    }

    LampSprite VeilEditor::sprite (const juce::String& name, juce::Rectangle<float> componentRef) const
    {
        // sprite rect in reference pixels, placed relative to the component
        const auto& r = uiMap["sprites"][juce::Identifier (name)];
        const auto rect = rectFrom (r);
        int size = 0;
        juce::Image image;
        if (auto* data = VEILSkin::getNamedResource ((name + "_png").toRawUTF8(), size))
            image = juce::ImageCache::getFromMemory (data, size);
        jassert (image.isValid());
        return { image, (rect - componentRef.getPosition()) * skinScale };
    }

    VeilEditor::VeilEditor (VeilProcessor& p)
        : ui::TleetEditorBase (p, baseWidth, toolbarHeight + panelHeight),
          proc (p),
          uiMap (loadUiMap()),
          plate (juce::ImageCache::getFromMemory (VEILSkin::plate_png, VEILSkin::plate_pngSize))
    {
        const float s = skinScale;
        auto onEdit = [this] { proc.commitHistory(); };

        for (const char* id : { params::input, params::threshold, params::attack, params::recovery, params::compress,
                                params::mix, params::output, params::tubeDrive, params::harmonics, params::hpf,
                                params::saturation, params::dryWet, params::scHpf })
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
            // the artwork's lit pointers: a white core in a red glow
            sp.core = juce::Colour (0xfffff4f4);
            sp.glow = juce::Colour (0xffd8606e);
            if ((*c).hasProperty ("readout"))
            {
                sp.readout = (rectFrom ((*c)["readout"]) - hitbox.getPosition()) * s;
                sp.readoutTextHeight = (float) (*c)["readoutTextHeight"] * s;
                sp.readoutColour = readoutInk;
            }
            knob->setSkinPointer (sp);
            knob->onEditFinished = onEdit;
            addToPanel (*knob);

            KnobEntry e;
            e.id = id;
            e.hitbox = hitbox;
            e.labels = makeScaleLabels (*c, *param, [id = juce::String (id)] (float v, const juce::String&)
                                        { return labelText (id, v); });
            e.capHeight = (float) (*c)["scaleLabels"]["capHeight"];
            e.ink = (*c)["ink"].toString() == "dark" ? darkInk : panelInk;
            e.knob = std::move (knob);
            knobs.push_back (std::move (e));
        }

        // ---- lamp groups: CHARACTER, SIDECHAIN, METER
        for (const char* id : { params::character, params::scExternal, params::meterMode })
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
                const auto hitbox = rectFrom (b["hitbox"]);
                const auto name = juce::String (id) + "_" + b["label"].toString().removeCharacters ("/").toLowerCase();
                const auto glow = rectFrom (uiMap["sprites"][juce::Identifier (name)]);
                const auto bounds = hitbox.getUnion (glow);
                auto lamp = std::make_unique<LampButton> (b["label"].toString(), sprite (name, bounds),
                                                          (hitbox - bounds.getPosition()) * s);
                addToPanel (*lamp);
                e.bounds.push_back (bounds);
                e.group->add (std::move (lamp));
            }
            groups.push_back (std::move (e));
        }
        {
            auto& character = getLampGroup (params::character);
            const char* tips[] = { "CHARACTER: Clean, the cleanest voicing of the tube stages",
                                   "CHARACTER: Veil, the musical middle voicing",
                                   "CHARACTER: Thick, the most coloured voicing" };
            for (int i = 0; i < 3; ++i)
                character.getLamp (i).setTooltip (tips[i]);
            auto& sc = getLampGroup (params::scExternal);
            sc.getLamp (0).setTooltip ("SIDECHAIN INT: the detectors listen to the program. Lit while the internal key is in use.");
            sc.getLamp (1).setTooltip ("SIDECHAIN EXT: the detectors listen to VEIL's sidechain input (route a track to it in the host); "
                                       "SC HPF still shapes the key. Fully lit only while a key really arrives; with no key it glows "
                                       "faintly and VEIL keeps its internal detector.");
            scLamps = std::make_unique<SidechainLamps> (p, sc);
            sc.onShow = [this] (int) { if (scLamps) scLamps->refresh(); };
            auto& meter = getLampGroup (params::meterMode);
            meter.getLamp (0).setTooltip ("METER GR: both needles show the gain reduction each channel really gets");
            meter.getLamp (1).setTooltip ("METER VU: both needles show the output level (0 VU = a sine at -18 dBFS)");
            meter.getLamp (2).setTooltip ("METER IN/OUT: both needles show the input level, to compare with VU (output); the sound never changes");
            meter.onShow = [this] (int m) { meterModeChanged (m); };
        }

        // ---- STEREO LINK / DUAL MONO
        {
            const auto* c = findControl (uiMap, params::stereoLink);
            jassert (c != nullptr);
            const auto lampHit = rectFrom ((*c)["lamp"]["hitbox"]);
            const auto lampRect = rectFrom (uiMap["sprites"]["link_lamp"]);
            linkLampBounds = lampHit.getUnion (lampRect);
            const auto dmHit = rectFrom ((*c)["dualMono"]["hitbox"]);
            dualMonoBounds = dmHit.getUnion (rectFrom (uiMap["sprites"]["link_dualmono"]));
            auto lamp = std::make_unique<LampButton> ("Stereo link", sprite ("link_lamp", linkLampBounds),
                                                      (lampHit - linkLampBounds.getPosition()) * s);
            auto dm = std::make_unique<LampButton> ("Dual mono", sprite ("link_dualmono", dualMonoBounds),
                                                    (dmHit - dualMonoBounds.getPosition()) * s);
            lamp->setTooltip ("STEREO LINK: lit while both channels share one gain control. Click for dual mono.");
            dm->setTooltip ("DUAL MONO: lit while the channels are compressed independently. Click to link them.");
            link = std::make_unique<LinkSwitch> (*p.apvts.getParameter (params::stereoLink), std::move (lamp), std::move (dm), onEdit);
            addToPanel (link->getLamp());
            addToPanel (link->getDualMono());
        }

        // ---- VU meters, one per channel
        for (int ch = 0; ch < 2; ++ch)
        {
            const auto* c = findControl (uiMap, ch == 0 ? "vuL" : "vuR");
            jassert (c != nullptr);
            vuBounds[(size_t) ch] = rectFrom ((*c)["hitbox"]);
            const auto origin = vuBounds[(size_t) ch].getPosition();
            ui::SkinVU::Geometry g;
            g.pivot = (pointFrom ((*c)["pivot"]) - origin) * s;
            g.needleLength = (float) (*c)["needleLength"] * s;
            g.face = (rectFrom ((*c)["face"]) - origin) * s;
            if (auto* props = (*c)["scaleDegrees"].getDynamicObject())
                for (auto& prop : props->getProperties())
                    g.scale.emplace_back (prop.name.toString().getFloatValue(), juce::degreesToRadians ((float) prop.value));
            std::sort (g.scale.begin(), g.scale.end());
            g.inkScale = s * 1.33f;
            g.needleWidth = 1.6f;
            vus[(size_t) ch] = std::make_unique<ui::SkinVU> (std::move (g));
            vus[(size_t) ch]->setSupplier ([this, ch]
            {
                // read every source each tick so none piles up between mode changes
                const float gr = proc.grMeters[(size_t) ch].takeGainReduction();
                const float in = proc.inputMeters[(size_t) ch].takeRms();
                const float out = proc.outputMeters[(size_t) ch].takeRms();
                return meterReading (proc.getMeterMode(), gr, in, out);
            });
            addToPanel (*vus[(size_t) ch]);
        }

        // ---- spectrum of the output
        {
            const auto* c = findControl (uiMap, "spectrum");
            jassert (c != nullptr);
            const auto screen = rectFrom ((*c)["screen"]);
            const auto strip = rectFrom ((*c)["labelStrip"]);
            spectrumBounds = screen.getUnion (strip);
            ui::SpectrumDisplay::Style st;
            st.screen = (screen - spectrumBounds.getPosition()) * s;
            st.labelStrip = (strip - spectrumBounds.getPosition()) * s;
            st.gridFrequencies.clear();
            for (auto& f : *(*c)["frequencies"].getArray())
                st.gridFrequencies.push_back ((float) f);
            st.inset = 6.0f * s;
            st.labelCapHeight = 11.0f * s;
            st.lineWidth = 1.5f * s;
            spectrum = std::make_unique<ui::SpectrumDisplay> (p.spectrum, st);
            spectrum->setTooltip ("Spectrum of VEIL's output (level per 1/6 octave; a full-scale sine reads 0 dB)");
            spectrum->setQuality ((ui::SpectrumDisplay::Quality) juce::jlimit (0, 3, (int) p.getUiProperty ("analyzerQuality", 2)));
            addToPanel (*spectrum);
            qualityButton = std::make_unique<ui::SpectrumQualityButton> (*spectrum);
            qualityButton->onQualityChanged = [this] (ui::SpectrumDisplay::Quality q) { proc.setUiProperty ("analyzerQuality", (int) q); };
            addToToolbar (*qualityButton, 92);
        }

        meterModeChanged ((int) proc.getMeterMode());
        finishSetup();
    }

    VeilEditor::~VeilEditor()
    {
        proc.spectrum.setActive (false);
    }

    void VeilEditor::meterModeChanged (int mode)
    {
        for (auto& v : vus)
            if (v)
                v->setRestValue (mode == (int) params::MeterMode::gr ? 0.0f : -40.0f);
    }

    ui::Knob& VeilEditor::getKnob (const juce::String& parameterId)
    {
        for (auto& k : knobs)
            if (k.id == parameterId)
                return *k.knob;
        jassertfalse;
        return *knobs.front().knob;
    }

    LampGroup& VeilEditor::getLampGroup (const juce::String& parameterId)
    {
        for (auto& g : groups)
            if (g.id == parameterId)
                return *g.group;
        jassertfalse;
        return *groups.front().group;
    }

    const std::vector<ScaleLabel>& VeilEditor::getScaleLabels (const juce::String& parameterId) const
    {
        for (auto& k : knobs)
            if (k.id == parameterId)
                return k.labels;
        static const std::vector<ScaleLabel> none;
        return none;
    }

    juce::Rectangle<int> VeilEditor::refToPanel (juce::Rectangle<float> r) const
    {
        return (r * skinScale).translated ((float) panelArea.getX(), (float) panelArea.getY()).toNearestInt();
    }

    void VeilEditor::layoutPanel (juce::Rectangle<int> area)
    {
        panelArea = area;
        for (auto& k : knobs)
            k.knob->setBounds (refToPanel (k.hitbox));
        for (auto& g : groups)
            for (int i = 0; i < g.group->getNumLamps(); ++i)
                g.group->getLamp (i).setBounds (refToPanel (g.bounds[(size_t) i]));
        if (link)
        {
            link->getLamp().setBounds (refToPanel (linkLampBounds));
            link->getDualMono().setBounds (refToPanel (dualMonoBounds));
        }
        for (size_t ch = 0; ch < 2; ++ch)
            if (vus[ch])
                vus[ch]->setBounds (refToPanel (vuBounds[ch]));
        if (spectrum)
            spectrum->setBounds (refToPanel (spectrumBounds));
    }

    void VeilEditor::paintPanel (juce::Graphics& g, juce::Rectangle<int> area)
    {
        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        g.drawImage (plate, area.toFloat(), juce::RectanglePlacement::stretchToFit);

        const auto origin = area.getPosition().toFloat();
        static const std::vector<ui::skin::StepDot> noDots;
        for (auto& k : knobs)
            paintScale (g, noDots, k.labels, k.capHeight, skinScale, origin, k.ink, k.ink);
    }
}
