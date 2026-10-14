#include "PluginEditor.h"
#include "Parameters.h"
#include "TAPESkin.h"
#include "ui/TleetLookAndFeel.h"

namespace tleet::tape
{
    namespace
    {
        // Ink measured on the artwork.
        const juce::Colour dotInk { 0xffeee0d6 };          // knob scale dots
        const juce::Colour knobLabelInk { 0xffcdb9a9 };    // knob scale numbers
        const juce::Colour bigReadoutInk { 0xfff2d4c4 };   // INPUT / OUTPUT windows
        const juce::Colour smallReadoutInk { 0xffefd6c8 }; // small knob windows
        const juce::Colour pointerCore { 0xfffffafa };     // pointers: white core
        const juce::Colour pointerGlow { 0xffe0a4a6 };     // in a faint rose glow
        const juce::Colour needleLeft { 0xff1e1710 };      // VU needles: L the dark one,
        const juce::Colour needleRight { 0xff74604e };     // R the lighter one (as printed)
        const juce::Colour responseCore { 0xfff9e5ec };    // TAPE CONDITION response line
        const juce::Colour responseGlow { 0xffd05a6a };
        const juce::Colour zeroLineInk { 0xff6f5a5a };

        juce::Rectangle<float> local (juce::Rectangle<float> r, juce::Rectangle<float> bounds, float s)
        {
            return (r - bounds.getPosition()) * s;
        }
    }

    using namespace ui::skin;

    // ================================================================== reel

    TapeReel::TapeReel (juce::Image r, juce::Image l) : reel (std::move (r)), light (std::move (l))
    {
        setInterceptsMouseClicks (false, false);
        setOpaque (false);
    }

    void TapeReel::setAngle (float radians)
    {
        radians = std::fmod (radians, juce::MathConstants<float>::twoPi);
        if (! juce::exactlyEqual (radians, angle))
        {
            angle = radians;
            repaint();
        }
    }

    void TapeReel::paint (juce::Graphics& g)
    {
        if (getWidth() <= 0 || getHeight() <= 0)
            return;
        const float scale = juce::jmax (1.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
        const int w = juce::jmax (1, juce::roundToInt ((float) getWidth() * scale));
        const int h = juce::jmax (1, juce::roundToInt ((float) getHeight() * scale));
        if (scaledLight.getWidth() != w || scaledLight.getHeight() != h)
        {
            scaledLight = light.rescaled (w, h, juce::Graphics::highResamplingQuality).convertedToFormat (juce::Image::ARGB);
            frame = juce::Image (juce::Image::ARGB, w, h, true);
        }

        // the reel, turned, at device resolution
        frame.clear (frame.getBounds());
        {
            juce::Graphics fg (frame);
            fg.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
            fg.drawImageTransformed (reel, juce::AffineTransform::scale ((float) w / (float) reel.getWidth(), (float) h / (float) reel.getHeight())
                                               .rotated (angle, (float) w * 0.5f, (float) h * 0.5f));
        }

        // x the fixed light (premultiplied by where the reel shows, so one
        // product does both)
        {
            juce::Image::BitmapData fd (frame, juce::Image::BitmapData::readWrite);
            const juce::Image::BitmapData ld (scaledLight, juce::Image::BitmapData::readOnly);
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; ++x)
                {
                    auto* p = reinterpret_cast<juce::PixelARGB*> (fd.getPixelPointer (x, y));
                    const auto* l = reinterpret_cast<const juce::PixelARGB*> (ld.getPixelPointer (x, y));
                    const int a = p->getAlpha() * l->getAlpha() / 255;
                    auto mul = [a] (int c, int f) { return (juce::uint8) juce::jmin (a, c * f / 128); };
                    p->setARGB ((juce::uint8) a, mul (p->getRed(), l->getRed()), mul (p->getGreen(), l->getGreen()),
                                mul (p->getBlue(), l->getBlue()));
                }
        }
        g.drawImage (frame, getLocalBounds().toFloat(), juce::RectanglePlacement::stretchToFit);
    }

    // ================================================================== saturation

    SaturationBar::SaturationBar (Geometry g) : geo (std::move (g))
    {
        jassert ((int) geo.segmentX.size() == numSegments);
        setInterceptsMouseClicks (true, false);
    }

    void SaturationBar::push (float activity, float seconds)
    {
        activity = std::isfinite (activity) ? juce::jlimit (0.0f, 1.0f, activity) : 0.0f;
        if (activity >= displayed)
            displayed = activity;
        else
            displayed = activity + (displayed - activity) * std::exp (-juce::jmax (0.0f, seconds) / fallSeconds);
        if (displayed < 1.0e-4f)
            displayed = 0.0f;
        if (getLitSegments() != paintedLit)
            repaint();
    }

    void SaturationBar::paint (juce::Graphics& g)
    {
        paintedLit = getLitSegments();
        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        for (int i = 0; i < paintedLit; ++i)
        {
            const auto& image = i >= geo.redFrom ? geo.red : geo.white;
            g.drawImage (image, geo.spriteBox.translated (geo.segmentX[(size_t) i], geo.top));
        }
    }

    // ================================================================== response curve

    ResponseCurve::ResponseCurve (TapeProcessor& p, const ui::SpectrumDisplay& sp, juce::Rectangle<float> scr)
        : proc (p), spectrum (sp), screen (scr)
    {
        setInterceptsMouseClicks (false, false);
        constexpr int n = 160;
        for (int i = 0; i < n; ++i)
            hz.push_back (20.0f * std::pow (1000.0f, (float) i / (float) (n - 1)));
        hzD.assign (hz.begin(), hz.end());
        outD.resize (hz.size());
        response.assign (hz.size(), 0.0f);
    }

    float ResponseCurve::yForResponseDb (float db) const noexcept
    {
        const float zero = screen.getY() + screen.getHeight() * 0.42f;
        const float perDb = screen.getHeight() * 0.36f / responseRangeDb;
        return zero - juce::jlimit (-responseRangeDb, responseRangeDb, db) * perDb;
    }

    bool ResponseCurve::update()
    {
        proc.tapeResponseDb (hzD.data(), outD.data(), (int) hzD.size(), -1);
        bool changed = false;
        for (size_t i = 0; i < hz.size(); ++i)
        {
            const float v = std::isfinite (outD[i]) ? (float) outD[i] : -responseRangeDb;
            if (std::abs (v - response[i]) > 0.01f)
                changed = true;
            response[i] = v;
        }
        if (changed || path.isEmpty())
        {
            path.clear();
            for (size_t i = 0; i < hz.size(); ++i)
            {
                const juce::Point<float> pt (spectrum.xForFrequency (hz[i]), yForResponseDb (response[i]));
                if (i == 0)
                    path.startNewSubPath (pt);
                else
                    path.lineTo (pt);
            }
            repaint();
        }
        return changed;
    }

    void ResponseCurve::paint (juce::Graphics& g)
    {
        g.reduceClipRegion (screen.toNearestInt());
        const float y0 = yForResponseDb (0.0f);
        const float dash[] { 3.0f, 3.0f };
        g.setColour (zeroLineInk);
        g.drawDashedLine ({ screen.getX(), y0, screen.getRight(), y0 }, dash, 2, 1.0f);
        g.setColour (responseGlow.withAlpha (0.35f));
        g.strokePath (path, juce::PathStrokeType (3.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (responseCore);
        g.strokePath (path, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // ================================================================== editor

    juce::var TapeEditor::loadUiMap()
    {
        return juce::JSON::parse (juce::String::fromUTF8 (TAPESkin::UI_MAP_json, TAPESkin::UI_MAP_jsonSize));
    }

    juce::var TapeEditor::loadSpriteMap()
    {
        return juce::JSON::parse (juce::String::fromUTF8 (TAPESkin::sprites_json, TAPESkin::sprites_jsonSize));
    }

    juce::String TapeEditor::labelText (const juce::String& id, float v)
    {
        const int n = juce::roundToInt (v);
        const bool signedScale = id == params::input || id == params::output || id == params::bias
                              || id == params::lf || id == params::hf;
        return signedScale && n > 0 ? "+" + juce::String (n) : juce::String (n);
    }

    juce::Rectangle<float> TapeEditor::spriteRect (const juce::String& name) const
    {
        return rectFrom (spriteMap["sprites"][juce::Identifier (name)]);
    }

    juce::Image TapeEditor::spriteImage (const juce::String& name) const
    {
        int size = 0;
        juce::Image image;
        if (auto* data = TAPESkin::getNamedResource ((name + "_png").toRawUTF8(), size))
            image = juce::ImageCache::getFromMemory (data, size);
        jassert (image.isValid());
        return image;
    }

    void TapeEditor::place (juce::Component& c, juce::Rectangle<float> refBounds)
    {
        addToPanel (c);
        placed.emplace_back (&c, refBounds);
    }

    static juce::String spriteName (const juce::String& group, const juce::String& label)
    {
        return group + "_" + label.toLowerCase().replace (".", "_").replace ("+", "p").replace ("-", "m");
    }

    TapeEditor::TapeEditor (TapeProcessor& p)
        : ui::TleetEditorBase (p, baseWidth, toolbarHeight + panelHeight),
          proc (p),
          uiMap (loadUiMap()),
          spriteMap (loadSpriteMap()),
          plate (juce::ImageCache::getFromMemory (TAPESkin::plate_png, TAPESkin::plate_pngSize))
    {
        const float s = skinScale;
        auto onEdit = [this] { proc.commitHistory(); };

        // ---- reels first: everything else sits over the transport window
        for (int i = 0; i < 2; ++i)
        {
            const juce::String id = i == 0 ? "reelLeft" : "reelRight";
            const auto bounds = spriteRect (id);
            reels[(size_t) i] = std::make_unique<TapeReel> (spriteImage (id), spriteImage (id + "_light"));
            place (*reels[(size_t) i], bounds);
            controlBounds[id] = rectFrom ((*findControl (uiMap, id))["hitbox"]);
        }

        // ---- knobs: pointer, live scale, live readout
        const std::pair<const char*, const char*> tips[] = {
            { "input",       "INPUT: level into the machine. More input drives the tape harder (more saturation, compression and "
                             "harmonics); AUTO GAIN keeps the loudness about the same" },
            { "output",      "OUTPUT: level after the machine and MIX" },
            { "bias",        "BIAS: record bias against the tape's optimum (0 %). Under-bias: brighter, more distortion; "
                             "over-bias: cleaner, duller (HF loss)" },
            { "saturation",  "SATURATION: how hard the record head drives the magnetic layer at the same level on the meters" },
            { "headBump",    "HEAD BUMP: the low-frequency bump of the repro head (its frequency follows TAPE SPEED)" },
            { "lf",          "LF: repro low-frequency EQ trim" },
            { "hf",          "HF: repro high-frequency EQ trim" },
            { "age",         "AGE: wear of tape and heads: HF loss, more wow, flutter and noise, a small L/R mismatch" },
            { "wow",         "WOW: slow speed drift of the transport (pitch drift)" },
            { "flutter",     "FLUTTER: fast speed variation of the capstan and guides" },
            { "noiseAmount", "NOISE amount: tape hiss and modulation noise level (with NOISE ON)" },
            { "crosstalk",   "CROSSTALK: frequency-dependent bleed between the two tracks" },
            { "mix",         "MIX: the machine against the dry input (latency-aligned)" },
        };

        for (auto& [id, tip] : tips)
        {
            const auto* c = findControl (uiMap, id);
            jassert (c != nullptr);
            auto* param = p.apvts.getParameter ((*c)["parameter"].toString());
            jassert (param != nullptr);

            const auto hitbox = rectFrom ((*c)["hitbox"]);
            const auto readout = rectFrom ((*c)["readout"]);
            const auto bounds = hitbox.getUnion (readout);
            const auto centre = pointFrom ((*c)["centre"]);
            const auto& ptr = (*c)["pointer"];
            const bool big = (float) (*c)["capRadius"] > 40.0f;

            auto knob = std::make_unique<ui::Knob> (*param, param->getName (64));
            ui::Knob::SkinPointer sp;
            sp.centre = (centre - bounds.getPosition()) * s;
            sp.innerRadius = (float) ptr["inner"] * s;
            sp.outerRadius = (float) ptr["outer"] * s;
            sp.width = (float) ptr["width"] * s;
            sp.startAngle = juce::degreesToRadians ((float) (*c)["travelDegrees"][0]);
            sp.endAngle = juce::degreesToRadians ((float) (*c)["travelDegrees"][1]);
            sp.core = pointerCore;
            sp.glow = pointerGlow;
            sp.readout = local (readout, bounds, s);
            sp.readoutTextHeight = (float) (*c)["readoutCapHeight"] / 0.72f * s;
            sp.readoutColour = big ? bigReadoutInk : smallReadoutInk;
            knob->setSkinPointer (sp);
            knob->setTooltip (tip);
            knob->onEditFinished = onEdit;
            addToPanel (*knob);

            KnobEntry e;
            e.id = id;
            e.bounds = bounds;
            e.labels = makeScaleLabels (*c, *param, [pid = param->getParameterID()] (float v, const juce::String&)
                                        { return labelText (pid, v); });
            e.capHeight = (float) (*c)["scaleLabels"]["capHeight"];
            e.dots = makeStepDots (*c, *param);
            e.knob = std::move (knob);
            controlBounds[id] = bounds;
            knobs.push_back (std::move (e));
        }

        // ---- button groups
        const std::pair<const char*, juce::StringArray> groupTips[] = {
            { "tapeType", { "TAPE TYPE A (CLASSIC): saturates earlier and softer",
                            "TAPE TYPE B (MODERN): more headroom, cleaner",
                            "TAPE TYPE C (HOT): higher operating level, saturates later" } },
            { "speed", { "TAPE SPEED 7.5 IPS: lower head bump, earlier HF loss, more saturation at the top",
                         "TAPE SPEED 15 IPS",
                         "TAPE SPEED 30 IPS: the widest, cleanest response, head bump highest" } },
            { "calibration", { "CALIBRATION -3 dB: less flux on tape for 0 VU (more headroom)",
                               "CALIBRATION 0 dB: the tape type's reference level",
                               "CALIBRATION +3 dB: more flux on tape for 0 VU (saturates earlier)",
                               "CALIBRATION +6 dB: elevated level, the most tape compression" } },
            { "noise", { "NOISE OFF: no hiss and no modulation noise",
                         "NOISE ON: tape hiss and modulation noise at the NOISE amount" } },
        };
        for (auto& [gid, gtips] : groupTips)
        {
            const auto* c = findControl (uiMap, gid);
            GroupEntry e;
            e.id = gid;
            e.group = std::make_unique<LampGroup> (*p.apvts.getParameter ((*c)["parameter"].toString()), onEdit);
            juce::Rectangle<float> all;
            int i = 0;
            for (auto& b : *(*c)["buttons"].getArray())
            {
                const auto sprite = spriteName (gid, b["label"].toString());
                const auto hitbox = rectFrom (b["hitbox"]);
                const auto glow = spriteRect (sprite);
                const auto bounds = hitbox.getUnion (glow);
                auto lamp = std::make_unique<LampButton> (b["label"].toString(), LampSprite { spriteImage (sprite), local (glow, bounds, s) },
                                                          local (hitbox, bounds, s));
                lamp->setTooltip (gtips[i++]);
                addToPanel (*lamp);
                e.bounds.push_back (bounds);
                all = all.isEmpty() ? bounds : all.getUnion (bounds);
                e.group->add (std::move (lamp));
            }
            controlBounds[gid] = all;
            groups.push_back (std::move (e));
        }

        // ---- AUTO GAIN
        {
            const auto* c = findControl (uiMap, "autoGain");
            const auto hitbox = rectFrom ((*c)["hitbox"]);
            const auto lit = spriteRect ("autoGain_lit");
            const auto bounds = hitbox.getUnion (lit);
            autoGain = std::make_unique<ui::LampToggle> (*p.apvts.getParameter (params::autoGain), "AUTO GAIN",
                                                         spriteImage ("autoGain_lit"), local (lit, bounds, s), local (hitbox, bounds, s));
            autoGain->setTooltip ("AUTO GAIN: the machine's output is brought back to the level it would have without "
                                  "INPUT, CALIBRATION and TAPE TYPE, so you compare the sound, not the loudness");
            autoGain->onEditFinished = onEdit;
            place (*autoGain, bounds);
            controlBounds["autoGain"] = bounds;
        }

        // ---- VU meters: two needles each (L dark, R light)
        for (int m = 0; m < 2; ++m)
        {
            const juce::String id = m == 0 ? "vuRecord" : "vuRepro";
            const auto* c = findControl (uiMap, id);
            const auto bounds = rectFrom ((*c)["face"]);
            for (int ch = 0; ch < 2; ++ch)
            {
                ui::SkinVU::Geometry g;
                g.pivot = (pointFrom ((*c)["pivot"]) - bounds.getPosition()) * s;
                g.needleLength = (float) (*c)["needleLength"] * s;
                g.face = juce::Rectangle<float> (bounds.getWidth(), bounds.getHeight()) * s;
                if (auto* props = (*c)["scaleDegrees"].getDynamicObject())
                    for (auto& prop : props->getProperties())
                        g.scale.emplace_back (prop.name.toString().getFloatValue(), juce::degreesToRadians ((float) prop.value));
                std::sort (g.scale.begin(), g.scale.end());
                g.inkScale = s * 1.33f;
                g.needleWidth = 1.5f;
                g.needleColour = ch == 0 ? needleLeft : needleRight;
                auto vu = std::make_unique<ui::SkinVU> (std::move (g));
                const bool repro = m == 1;
                vu->setSupplier ([this, repro, ch]
                                 { return repro ? proc.takeReproVuDb (ch) : proc.takeRecordVuDb (ch); });
                vu->setRestValue (-20.0f);
                vu->setTooltip (repro ? "REPRO: the level coming off the tape (after the repro head and its EQ, before AUTO GAIN); "
                                        "dark needle L, light needle R. 0 VU = the machine's operating level (a sine at -18 dBFS)"
                                      : "RECORD: the level going to the record head (after INPUT); dark needle L, light needle R. "
                                        "0 VU = the machine's operating level (a sine at -18 dBFS)");
                if (ch == 1)
                    vu->setInterceptsMouseClicks (false, false); // the tooltip comes from the L needle's component
                place (*vu, bounds);
                vus[(size_t) (m * 2 + ch)] = std::move (vu);
            }
            controlBounds[id] = bounds;
            vuCaps[(size_t) m] = std::make_unique<SpriteCover> (spriteImage (id + "_cap"));
            place (*vuCaps[(size_t) m], spriteRect (id + "_cap"));
        }

        // ---- SATURATION
        {
            const auto* c = findControl (uiMap, "saturationMeter");
            const auto bounds = rectFrom ((*c)["hitbox"]);
            SaturationBar::Geometry g;
            const float first = (float) (*c)["first"], pitch = (float) (*c)["pitch"];
            for (int i = 0; i < SaturationBar::numSegments; ++i)
                g.segmentX.push_back ((first + pitch * (float) i - bounds.getX()) * s);
            const float top = (float) (*c)["top"];
            g.top = (top - bounds.getY()) * s;
            g.redFrom = (int) (*c)["redFrom"];
            g.white = spriteImage ("satWhite");
            g.red = spriteImage ("satRed");
            const auto box = spriteRect ("satWhite");
            const float offset = (float) spriteMap["segmentOffset"]["satWhite"];
            g.spriteBox = juce::Rectangle<float> (-offset, box.getY() - top, box.getWidth(), box.getHeight()) * s;
            saturation = std::make_unique<SaturationBar> (std::move (g));
            saturation->setTooltip ("SATURATION: how deep into saturation the magnetic layer went (each segment = 1/24 of the "
                                    "way from linear to fully saturated); red from 5/8");
            place (*saturation, bounds);
            controlBounds["saturationMeter"] = bounds;
        }

        // ---- TAPE CONDITION: output spectrum + the machine's response
        {
            const auto* c = findControl (uiMap, "tapeCondition");
            const auto screen = rectFrom ((*c)["screen"]);
            const auto strip = rectFrom ((*c)["labelStrip"]);
            const auto bounds = screen.getUnion (strip);
            ui::SpectrumDisplay::Style st;
            st.screen = local (screen, bounds, s);
            st.labelStrip = local (strip, bounds, s);
            st.gridFrequencies.clear();
            for (auto& f : *(*c)["frequencies"].getArray())
                st.gridFrequencies.push_back ((float) f);
            st.inset = 0.0f;
            st.labelCapHeight = (float) (*c)["labelCapHeight"] * s;
            st.lineWidth = 1.0f;
            st.line = juce::Colour (0xffb98f97);
            st.glow = juce::Colour (0x00000000);
            st.fill = juce::Colour (0xff9c6f78);
            st.grid = juce::Colour (0xff3e2f31);
            st.labelInk = juce::Colour (0xffa08b7f);
            spectrum = std::make_unique<ui::SpectrumDisplay> (p.outputSpectrum, st);
            spectrum->setTooltip ("TAPE CONDITION: the line is the machine's frequency response as set now (TAPE SPEED, "
                                  "HEAD BUMP, LF, HF, BIAS, AGE; +-12 dB around the dashed 0 dB line); the shaded area is "
                                  "the spectrum of the output (level per 1/6 octave)");
            spectrum->setQuality ((ui::SpectrumDisplay::Quality) juce::jlimit (0, 3, (int) p.getUiProperty ("analyzerQuality", 2)));
            place (*spectrum, bounds);
            response = std::make_unique<ResponseCurve> (p, *spectrum, st.screen);
            place (*response, bounds);
            controlBounds["tapeCondition"] = bounds;
            qualityButton = std::make_unique<ui::SpectrumQualityButton> (*spectrum);
            qualityButton->onQualityChanged = [this] (ui::SpectrumDisplay::Quality q) { proc.setUiProperty ("analyzerQuality", (int) q); };
            addToToolbar (*qualityButton, 92);
        }

        refresh (0.0f);
        finishSetup();
        startTimerHz (30);
    }

    TapeEditor::~TapeEditor()
    {
        stopTimer();
    }

    void TapeEditor::timerCallback()
    {
        const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
        const float dt = lastTick > 0.0 ? (float) juce::jlimit (0.0, 0.1, now - lastTick) : 1.0f / 30.0f;
        lastTick = now;
        refresh (dt);
    }

    void TapeEditor::refresh (float seconds)
    {
        if (saturation != nullptr)
            saturation->push (proc.getSaturationActivity(), seconds);

        if (response != nullptr)
            response->update();

        // reels: counter-clockwise (tape from the left reel's bottom to the
        // right reel's bottom), at the real speed of a 3.5 inch pack
        if (proc.isTransportRunning() && seconds > 0.0f)
        {
            const float step = (float) (juce::MathConstants<double>::twoPi * TapeReel::turnsPerSecond (proc.getTapeSpeedIps()) * seconds);
            for (auto& r : reels)
                if (r != nullptr)
                    r->setAngle (r->getAngle() - step);
        }
    }

    ui::Knob& TapeEditor::getKnob (const juce::String& controlId)
    {
        for (auto& k : knobs)
            if (k.id == controlId)
                return *k.knob;
        jassertfalse;
        return *knobs.front().knob;
    }

    LampGroup& TapeEditor::getLampGroup (const juce::String& groupId)
    {
        for (auto& g : groups)
            if (g.id == groupId)
                return *g.group;
        jassertfalse;
        return *groups.front().group;
    }

    const std::vector<ScaleLabel>& TapeEditor::getScaleLabels (const juce::String& controlId) const
    {
        for (auto& k : knobs)
            if (k.id == controlId)
                return k.labels;
        static const std::vector<ScaleLabel> none;
        return none;
    }

    const std::vector<StepDot>& TapeEditor::getStepDots (const juce::String& controlId) const
    {
        for (auto& k : knobs)
            if (k.id == controlId)
                return k.dots;
        static const std::vector<StepDot> none;
        return none;
    }

    juce::Rectangle<int> TapeEditor::getControlBounds (const juce::String& controlId) const
    {
        const auto it = controlBounds.find (controlId);
        return it == controlBounds.end() ? juce::Rectangle<int>() : refToPanel (it->second);
    }

    juce::Rectangle<int> TapeEditor::refToPanel (juce::Rectangle<float> r) const
    {
        return (r * skinScale).translated ((float) panelArea.getX(), (float) panelArea.getY()).toNearestInt();
    }

    void TapeEditor::layoutPanel (juce::Rectangle<int> area)
    {
        panelArea = area;
        for (auto& k : knobs)
            k.knob->setBounds (refToPanel (k.bounds));
        for (auto& g : groups)
            for (int i = 0; i < g.group->getNumLamps(); ++i)
                g.group->getLamp (i).setBounds (refToPanel (g.bounds[(size_t) i]));
        for (auto& [c, r] : placed)
            c->setBounds (refToPanel (r));
    }

    void TapeEditor::paintPanel (juce::Graphics& g, juce::Rectangle<int> area)
    {
        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        g.drawImage (plate, area.toFloat(), juce::RectanglePlacement::stretchToFit);

        const auto origin = area.getPosition().toFloat();
        for (auto& k : knobs)
            paintScale (g, k.dots, k.labels, k.capHeight, skinScale, origin, dotInk, knobLabelInk);
    }
}
