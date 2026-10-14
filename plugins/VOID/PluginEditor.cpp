#include "PluginEditor.h"
#include "Parameters.h"
#include "VOIDSkin.h"
#include "ui/TleetLookAndFeel.h"

namespace tleet::voidrev
{
    namespace
    {
        // Ink measured on the artwork.
        const juce::Colour dotInk { 0xffc7b0a0 };       // scale dots
        const juce::Colour topLabelInk { 0xffaa9c92 };  // scale numbers, upper knob row
        const juce::Colour lowLabelInk { 0xff8e847c };  // scale numbers, lower knob row
        const juce::Colour readoutInk { 0xffdbb2a5 };   // readout windows
        const juce::Colour pointerCore { 0xfffff6f8 };  // lit pointers: white core
        const juce::Colour pointerGlow { 0xffeb78af };  // in a rose glow
        const juce::Colour litRose { 0xfff9deea };      // filter curve, lit bars
        const juce::Colour graphGrid { 0xff4a3434 };
        const juce::Colour graphLabelInk { 0xff8a807a };
        const juce::Colour presetInk { 0xffcdbfb4 };

        juce::Rectangle<float> local (juce::Rectangle<float> r, juce::Rectangle<float> bounds, float s)
        {
            return (r - bounds.getPosition()) * s;
        }

        juce::String frequencyText (float hz)
        {
            return hz >= 1000.0f ? juce::String (juce::roundToInt (hz / 1000.0f)) + "K" : juce::String (juce::roundToInt (hz));
        }
    }

    using namespace ui::skin;

    // ================================================================== DROWN

    DrownKnob::DrownKnob (juce::RangedAudioParameter& p, juce::Image r, juce::Rectangle<float> area,
                          juce::Point<float> c, float s, float e, float clip)
        : ui::Knob (p, p.getName (64)), ring (std::move (r)), ringArea (area), centre (c), startDeg (s), endDeg (e), clipRadius (clip)
    {
        // no pointer is drawn; the skin flag gives the knob its plain hit area
        SkinPointer sp;
        sp.centre = c;
        sp.startAngle = juce::degreesToRadians (s);
        sp.endAngle = juce::degreesToRadians (e);
        setSkinPointer (sp);
    }

    float DrownKnob::getLitAngleDegrees() const noexcept
    {
        return startDeg + getNormalisedValue() * (endDeg - startDeg);
    }

    void DrownKnob::paint (juce::Graphics& g)
    {
        const float to = getLitAngleDegrees();
        if (to <= startDeg + 0.05f)
            return;
        juce::Path wedge;
        wedge.addPieSegment (juce::Rectangle<float> (2.0f * clipRadius, 2.0f * clipRadius).withCentre (centre),
                             juce::degreesToRadians (startDeg), juce::degreesToRadians (to), 0.0f);
        juce::Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (wedge);
        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        g.drawImage (ring, ringArea, juce::RectanglePlacement::stretchToFit);
    }

    // ================================================================== EARLY / LATE

    LevelBar::LevelBar (juce::Rectangle<float> t, float lo, float hi) : track (t), minDb (lo), maxDb (hi)
    {
    }

    float LevelBar::litWidthFor (float db) const noexcept
    {
        return track.getWidth() * juce::jlimit (0.0f, 1.0f, (db - minDb) / (maxDb - minDb));
    }

    void LevelBar::push (float level, float seconds)
    {
        const float db = level > 0.0f ? 20.0f * std::log10 (level) : -200.0f;
        if (level >= 0.0f && db >= displayed)
            displayed = db;
        else
            displayed = std::max (level >= 0.0f ? db : -200.0f, displayed - fallDbPerSecond * seconds);
        const float w = litWidthFor (displayed);
        if (std::abs (w - painted) > 0.05f)
        {
            painted = w;
            repaint();
        }
    }

    void LevelBar::paint (juce::Graphics& g)
    {
        const float w = litWidthFor (displayed);
        if (w <= 0.0f)
            return;
        const auto lit = track.withWidth (w);
        // the artwork's lit bar: a rose-white core in a soft glow
        g.setColour (pointerGlow.withAlpha (0.28f));
        g.fillRoundedRectangle (lit.expanded (1.5f, 1.5f), 2.5f);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xffd9a8b0), lit.getX(), 0.0f, litRose, lit.getRight(), 0.0f, false));
        g.fillRoundedRectangle (lit.reduced (0.0f, 0.5f), 1.5f);
    }

    // ================================================================== IN / OUT

    BarMeters::BarMeters (std::array<juce::Rectangle<float>, 4> b, std::vector<std::pair<float, float>> sc)
        : bars (b), scale (std::move (sc))
    {
        std::sort (scale.begin(), scale.end());
        setInterceptsMouseClicks (true, false);
    }

    float BarMeters::yForDb (float db) const noexcept
    {
        if (db <= scale.front().first)
            return scale.front().second;
        if (db >= scale.back().first)
            return scale.back().second;
        for (size_t i = 1; i < scale.size(); ++i)
            if (db <= scale[i].first)
            {
                const auto& [d0, y0] = scale[i - 1];
                const auto& [d1, y1] = scale[i];
                return y0 + (db - d0) / (d1 - d0) * (y1 - y0);
            }
        return scale.back().second;
    }

    void BarMeters::push (int bar, float peak, float seconds)
    {
        auto& d = displayed[(size_t) bar];
        const float db = peak > 0.0f ? 20.0f * std::log10 (peak) : -200.0f;
        const float before = d;
        if (peak >= 0.0f && db >= d)
            d = db;
        else
            d = std::max (peak >= 0.0f ? db : -200.0f, d - fallDbPerSecond * seconds);
        if (std::abs (yForDb (d) - yForDb (before)) > 0.05f)
            repaint();
    }

    void BarMeters::paint (juce::Graphics& g)
    {
        const float bottom = scale.front().second;
        for (size_t i = 0; i < bars.size(); ++i)
        {
            const float db = displayed[i];
            if (db <= scale.front().first)
                continue;
            const float top = yForDb (std::min (db, scale.back().first));
            const auto b = bars[i];
            const auto lit = juce::Rectangle<float> (b.getX() + 1.0f, top, b.getWidth() - 2.0f, bottom - top);
            // measured on the artwork's lit bars: bright rose at the foot, deeper towards the top
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xffa06162), 0.0f, yForDb (0.0f),
                                                     juce::Colour (0xfff4bdb7), 0.0f, bottom, false));
            g.fillRect (lit);
            g.setColour (pointerGlow.withAlpha (0.18f));
            g.fillRect (lit.expanded (1.0f, 0.0f));
        }
    }

    // ================================================================== filter graph

    FilterGraph::FilterGraph (std::function<double (double)> r, juce::Rectangle<float> p, juce::Rectangle<float> l,
                              float lo, float hi, float cap)
        : response (std::move (r)), plot (p), labelStrip (l), minDb (lo), maxDb (hi), capHeight (cap)
    {
        update();
    }

    float FilterGraph::xForFrequency (float hz) const noexcept
    {
        const float t = std::log (juce::jlimit (20.0f, 20000.0f, hz) / 20.0f) / std::log (1000.0f);
        return plot.getX() + 3.0f + t * (plot.getWidth() - 6.0f);
    }

    float FilterGraph::yForDb (float db) const noexcept
    {
        const float t = (maxDb - juce::jlimit (minDb, maxDb, db)) / (maxDb - minDb);
        return plot.getY() + 3.0f + t * (plot.getHeight() - 6.0f);
    }

    void FilterGraph::update()
    {
        const int n = juce::jmax (2, (int) plot.getWidth() - 6 + 1);
        std::vector<float> db ((size_t) n);
        for (int i = 0; i < n; ++i)
        {
            const double hz = 20.0 * std::pow (1000.0, (double) i / (double) (n - 1));
            db[(size_t) i] = (float) response (hz);
        }
        if (db == lastDb)
            return;
        lastDb = std::move (db);
        curve.clear();
        for (int i = 0; i < n; ++i)
        {
            const float x = plot.getX() + 3.0f + (float) i * (plot.getWidth() - 6.0f) / (float) (n - 1);
            const float y = yForDb (lastDb[(size_t) i]);
            if (i == 0) curve.startNewSubPath (x, y);
            else        curve.lineTo (x, y);
        }
        repaint();
    }

    void FilterGraph::paint (juce::Graphics& g)
    {
        const float freqs[] = { 20.0f, 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f, 20000.0f };
        g.setColour (graphGrid.withAlpha (0.8f));
        for (auto f : freqs)
        {
            const float x = xForFrequency (f);
            g.drawLine (x, plot.getY() + 1.0f, x, plot.getBottom() - 1.0f, 0.6f);
        }
        for (float db = maxDb; db >= minDb - 0.01f; db -= 6.0f)
            g.drawLine (plot.getX() + 1.0f, yForDb (db), plot.getRight() - 1.0f, yForDb (db), 0.6f);

        g.setFont (ui::TleetLookAndFeel::labelFont (capHeight / 0.72f, false).withHorizontalScale (0.86f));
        g.setColour (graphLabelInk);
        // 20K sits too close to 10K at this size to print both; its grid line marks it
        for (auto f : { 20.0f, 100.0f, 1000.0f, 10000.0f })
        {
            const auto text = frequencyText (f);
            const float w = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), text) + 2.0f;
            auto box = juce::Rectangle<float> (w, labelStrip.getHeight()).withCentre ({ xForFrequency (f), labelStrip.getCentreY() });
            box = box.withX (juce::jlimit (labelStrip.getX(), labelStrip.getRight() - w, box.getX()));
            g.drawText (text, box, juce::Justification::centred, false);
        }

        juce::Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (plot.reduced (1.0f).toNearestInt());
        juce::Path area (curve);
        area.lineTo (plot.getRight() - 3.0f, plot.getBottom());
        area.lineTo (plot.getX() + 3.0f, plot.getBottom());
        area.closeSubPath();
        g.setGradientFill (juce::ColourGradient (litRose.withAlpha (0.16f), 0.0f, plot.getY(), litRose.withAlpha (0.02f), 0.0f, plot.getBottom(), false));
        g.fillPath (area);
        g.setColour (pointerGlow.withAlpha (0.35f));
        g.strokePath (curve, juce::PathStrokeType (3.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (litRose);
        g.strokePath (curve, juce::PathStrokeType (1.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // ================================================================== preset bar

    PresetBar::PresetBar (juce::Rectangle<float> p, juce::Rectangle<float> n, juce::Rectangle<float> nx,
                          juce::Rectangle<float> s, juce::Rectangle<float> r, float cap)
        : prev (p), name (n), next (nx), save (s), remove (r), capHeight (cap)
    {
    }

    void PresetBar::setText (const juce::String& t, bool del)
    {
        if (t != text || del != canDelete)
        {
            text = t;
            canDelete = del;
            repaint();
        }
    }

    juce::Rectangle<float> PresetBar::areaOf (Part p) const
    {
        switch (p)
        {
            case Part::prev:   return prev;
            case Part::name:   return name;
            case Part::next:   return next;
            case Part::save:   return save;
            case Part::remove: return remove;
            case Part::none:   break;
        }
        return {};
    }

    PresetBar::Part PresetBar::partAt (juce::Point<float> q) const
    {
        for (auto p : { Part::prev, Part::name, Part::next, Part::save, Part::remove })
            if (areaOf (p).contains (q))
                return p;
        return Part::none;
    }

    juce::String PresetBar::getTooltipFor (Part p) const
    {
        switch (p)
        {
            case Part::prev:   return "Previous preset";
            case Part::next:   return "Next preset";
            case Part::name:   return "Current preset: click for the preset list";
            case Part::save:   return "Save, save as, rename, delete; copy / paste the plugin state";
            case Part::remove: return canDelete ? "Delete this user preset" : "Factory presets cannot be deleted";
            case Part::none:   break;
        }
        return {};
    }

    void PresetBar::mouseMove (const juce::MouseEvent& e)
    {
        const auto p = partAt (e.position);
        if (p != hover)
        {
            hover = p;
            setMouseCursor (p == Part::none || (p == Part::remove && ! canDelete) ? juce::MouseCursor::NormalCursor
                                                                                  : juce::MouseCursor::PointingHandCursor);
            repaint();
        }
    }

    void PresetBar::mouseExit (const juce::MouseEvent&)
    {
        hover = Part::none;
        repaint();
    }

    void PresetBar::mouseUp (const juce::MouseEvent& e)
    {
        if (e.mouseWasDraggedSinceMouseDown())
            return;
        const auto p = partAt (e.position);
        if (p == Part::remove && ! canDelete)
            return;
        if (p != Part::none && onClick)
            onClick (p);
    }

    void PresetBar::paint (juce::Graphics& g)
    {
        g.setFont (ui::TleetLookAndFeel::labelFont (capHeight / 0.72f, false).withHorizontalScale (0.9f));
        g.setColour (presetInk);
        g.drawFittedText (text, name.reduced (6.0f, 0.0f).toNearestInt(), juce::Justification::centred, 1, 0.8f);

        // the printed delete icon is dimmed while there is nothing to delete
        if (! canDelete)
        {
            g.setColour (juce::Colour (0xff0d0b0b).withAlpha (0.55f));
            g.fillRect (remove.reduced (4.0f));
        }
        if (hover != Part::none && ! (hover == Part::remove && ! canDelete))
        {
            g.setColour (pointerGlow.withAlpha (0.12f));
            g.fillRoundedRectangle (areaOf (hover).reduced (2.0f), 3.0f);
        }
    }

    // ================================================================== editor

    juce::var VoidEditor::loadUiMap()
    {
        return juce::JSON::parse (juce::String::fromUTF8 (VOIDSkin::UI_MAP_json, VOIDSkin::UI_MAP_jsonSize));
    }

    juce::var VoidEditor::loadSpriteMap()
    {
        return juce::JSON::parse (juce::String::fromUTF8 (VOIDSkin::sprites_json, VOIDSkin::sprites_jsonSize));
    }

    juce::String VoidEditor::labelText (const juce::String& id, float v, const juce::String& stepName)
    {
        if (stepName.isNotEmpty())
            return stepName.toUpperCase();
        if (id == params::decay)
        {
            if (params::isInfiniteDecay (v))
                return juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x9e"));
            return v < 1.0f ? juce::String (v, 1) : juce::String (juce::roundToInt (v));
        }
        if (id == params::modRate)
            return v < 1.0f ? juce::String (v, 2).trimCharactersAtEnd ("0") : juce::String (juce::roundToInt (v));
        if ((id == params::hpf || id == params::lpf) && v >= 999.5f)
            return juce::String (juce::roundToInt (v / 1000.0f)) + "K";
        const int n = juce::roundToInt (v);
        if (id == params::output)
            return n > 0 ? "+" + juce::String (n) : juce::String (n);
        return juce::String (n);
    }

    juce::Rectangle<float> VoidEditor::spriteRect (const juce::String& name) const
    {
        return rectFrom (spriteMap["sprites"][juce::Identifier (name)]);
    }

    juce::Image VoidEditor::spriteImage (const juce::String& name) const
    {
        int size = 0;
        juce::Image image;
        if (auto* data = VOIDSkin::getNamedResource ((name + "_png").toRawUTF8(), size))
            image = juce::ImageCache::getFromMemory (data, size);
        jassert (image.isValid());
        return image;
    }

    void VoidEditor::place (juce::Component& c, juce::Rectangle<float> refBounds)
    {
        addToPanel (c);
        placed.emplace_back (&c, refBounds);
    }

    VoidEditor::VoidEditor (VoidProcessor& p)
        : ui::TleetEditorBase (p, baseWidth, toolbarHeight + panelHeight),
          proc (p),
          uiMap (loadUiMap()),
          spriteMap (loadSpriteMap()),
          plate (juce::ImageCache::getFromMemory (VOIDSkin::plate_png, VOIDSkin::plate_pngSize))
    {
        const float s = skinScale;
        auto onEdit = [this] { proc.commitHistory(); };

        // ---- knobs: pointer, live scale, live readout
        const std::pair<const char*, const char*> tips[] = {
            { "algorithm",  "ALGORITHM: the reverb network (Room, Plate, Chamber, Hall, Void). The lamp beside it shows the one in use" },
            { "colorKnob",  "COLOR: Clean, Dark (darker, softer tail) or Ruin (degraded, saturated tail). Same setting as the COLOR buttons" },
            { "preDelay",   "PRE-DELAY: time before the reverb starts" },
            { "size",       "SIZE: size of the space (delay lengths of the network)" },
            { "decay",      "DECAY: reverb time (RT60 at mid frequencies). At the end of the scale (\xe2\x88\x9e) the tail never dies away" },
            { "diffusion",  "DIFFUSION: how quickly the echoes blur into a dense tail" },
            { "modRate",    "MOD RATE: speed of the delay modulation inside the tail" },
            { "modDepth",   "MOD DEPTH: amount of the delay modulation inside the tail" },
            { "width",      "WIDTH: stereo width of the reverb (0 = mono, 100 = natural, 200 = extra wide)" },
            { "hpf",        "HPF: low cut on the reverb, where FILTER MODE puts it" },
            { "lpf",        "LPF: high cut on the reverb, where FILTER MODE puts it" },
            { "duck",       "DUCK: lowers the reverb while the input plays" },
            { "mix",        "MIX: dry / reverb balance" },
            { "output",     "OUTPUT: output level" },
        };

        for (auto& [id, tip] : tips)
        {
            const auto* c = findControl (uiMap, id);
            jassert (c != nullptr);
            auto* param = p.apvts.getParameter ((*c)["parameter"].toString());
            jassert (param != nullptr);

            const auto hitbox = rectFrom ((*c)["hitbox"]);
            const auto readout = c->hasProperty ("readout") ? rectFrom ((*c)["readout"]) : juce::Rectangle<float>();
            const auto bounds = readout.isEmpty() ? hitbox : hitbox.getUnion (readout);
            const auto centre = pointFrom ((*c)["centre"]);
            const auto& ptr = (*c)["pointer"];

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
            if (! readout.isEmpty())
            {
                sp.readout = local (readout, bounds, s);
                sp.readoutTextHeight = (float) (*c)["readoutCapHeight"] / 0.72f * s;
                sp.readoutColour = readoutInk;
            }
            knob->setSkinPointer (sp);
            knob->setTooltip (tip);
            knob->onEditFinished = onEdit;
            addToPanel (*knob);

            KnobEntry e;
            e.id = id;
            e.bounds = bounds;
            if ((*c)["scaleLabels"].isObject())
            {
                e.labels = makeScaleLabels (*c, *param, [pid = param->getParameterID()] (float v, const juce::String& step)
                                            { return labelText (pid, v, step); });
                e.capHeight = (float) (*c)["scaleLabels"]["capHeight"];
            }
            e.dots = makeStepDots (*c, *param);
            e.knob = std::move (knob);
            controlBounds[id] = bounds;
            knobs.push_back (std::move (e));
        }

        // ---- lamp lists and button groups: ALGORITHM lamps, COLOR, FILTER MODE
        auto addGroup = [this, &p, onEdit, s] (const juce::String& groupId, const char* paramId,
                                               std::vector<std::tuple<juce::String, juce::String, juce::Rectangle<float>>> lamps,
                                               const juce::StringArray& tipsPerLamp)
        {
            GroupEntry e;
            e.id = groupId;
            e.group = std::make_unique<LampGroup> (*p.apvts.getParameter (paramId), onEdit);
            juce::Rectangle<float> all;
            int i = 0;
            for (auto& [label, sprite, hitbox] : lamps)
            {
                const auto glow = spriteRect (sprite);
                const auto bounds = hitbox.getUnion (glow);
                auto lamp = std::make_unique<LampButton> (label, LampSprite { spriteImage (sprite), local (glow, bounds, s) },
                                                          local (hitbox, bounds, s));
                lamp->setTooltip (tipsPerLamp[i++]);
                addToPanel (*lamp);
                e.bounds.push_back (bounds);
                all = all.isEmpty() ? bounds : all.getUnion (bounds);
                e.group->add (std::move (lamp));
            }
            controlBounds[groupId] = all;
            groups.push_back (std::move (e));
        };

        {
            const auto* c = findControl (uiMap, "algorithmLamps");
            std::vector<std::tuple<juce::String, juce::String, juce::Rectangle<float>>> lamps;
            for (int i = 0; i < params::numAlgorithms; ++i)
                lamps.emplace_back ((*c)["labels"][i].toString(), "algorithm_" + juce::String (i), rectFrom ((*c)["hitboxes"][i]));
            addGroup ("algorithmLamps", params::algorithm, lamps,
                      { "ROOM: small, dense early reflections, short build-up",
                        "PLATE: bright, immediately dense, no early reflections",
                        "CHAMBER: a live room with strong early reflections",
                        "HALL: a large hall, slow build-up, long smooth tail",
                        "VOID: an endless, dark, blooming space" });
        }
        for (const char* gid : { "color", "filterMode" })
        {
            const auto* c = findControl (uiMap, gid);
            std::vector<std::tuple<juce::String, juce::String, juce::Rectangle<float>>> lamps;
            for (auto& b : *(*c)["buttons"].getArray())
                lamps.emplace_back (b["label"].toString(), juce::String (gid) + "_" + b["label"].toString().toLowerCase(), rectFrom (b["hitbox"]));
            if (juce::String (gid) == "color")
                addGroup (gid, params::color, lamps,
                          { "CLEAN: the network as it is", "DARK: darker, softer tail", "RUIN: degraded, saturated, lo-fi tail" });
            else
                addGroup (gid, params::filterMode, lamps,
                          { "PRE: HPF and LPF on the input of the reverb",
                            "LOOP: HPF and LPF inside the feedback, so the filtered bands also die away faster",
                            "POST: HPF and LPF on the reverb output" });
        }

        // ---- DROWN
        {
            const auto* c = findControl (uiMap, "drown");
            const auto hitbox = rectFrom ((*c)["hitbox"]);
            const auto& arc = (*c)["arc"];
            const float clip = ((float) arc["outer"] + (float) arc["glow"] + 12.0f) * s;
            drown = std::make_unique<DrownKnob> (*p.apvts.getParameter (params::drown), spriteImage ("drown_ring"),
                                                 local (spriteRect ("drown_ring"), hitbox, s),
                                                 (pointFrom ((*c)["centre"]) - hitbox.getPosition()) * s,
                                                 (float) (*c)["travelDegrees"][0], (float) (*c)["travelDegrees"][1], clip);
            drown->onEditFinished = onEdit;
            place (*drown, hitbox);
            controlBounds["drown"] = hitbox;
        }

        // ---- FREEZE
        {
            const auto* c = findControl (uiMap, "freeze");
            const auto hitbox = rectFrom ((*c)["hitbox"]);
            const auto lit = spriteRect ("freeze_lit");
            const auto bounds = hitbox.getUnion (lit);
            freeze = std::make_unique<ui::LampToggle> (*p.apvts.getParameter (params::freeze), "FREEZE", spriteImage ("freeze_lit"),
                                                       local (lit, bounds, s), local (hitbox, bounds, s));
            freeze->setTooltip ("FREEZE: holds the reverb tail as it is; new input no longer enters it, the dry signal still plays");
            freeze->onEditFinished = onEdit;
            place (*freeze, bounds);
            controlBounds["freeze"] = bounds;
        }

        // ---- spectrum of the reverb
        {
            const auto* c = findControl (uiMap, "spectrum");
            const auto screen = rectFrom ((*c)["screen"]);
            const auto strip = rectFrom ((*c)["labelStrip"]);
            const auto bounds = screen.getUnion (strip);
            ui::SpectrumDisplay::Style st;
            st.screen = local (screen, bounds, s);
            st.labelStrip = local (strip, bounds, s);
            st.gridFrequencies = { 20.0f, 100.0f, 1000.0f, 10000.0f, 20000.0f };
            st.inset = (float) (*c)["inset"] * s;
            st.labelCapHeight = 10.0f * s;
            st.lineWidth = 1.5f;
            st.line = juce::Colour (0xfffbe3ee);
            st.glow = pointerGlow;
            st.fill = juce::Colour (0xffe8b6c8);
            st.grid = juce::Colour (0xff7a6066);
            st.labelInk = juce::Colour (0xffb8aaa0);
            spectrum = std::make_unique<ui::SpectrumDisplay> (p.wetSpectrum, st);
            spectrum->setTooltip ("Spectrum of the reverb signal (level per 1/6 octave; a full-scale sine reads 0 dB)");
            spectrum->setQuality ((ui::SpectrumDisplay::Quality) juce::jlimit (0, 3, (int) p.getUiProperty ("analyzerQuality", 2)));
            place (*spectrum, bounds);
            controlBounds["spectrum"] = bounds;
            qualityButton = std::make_unique<ui::SpectrumQualityButton> (*spectrum);
            qualityButton->onQualityChanged = [this] (ui::SpectrumDisplay::Quality q) { proc.setUiProperty ("analyzerQuality", (int) q); };
            addToToolbar (*qualityButton, 92);
        }

        // ---- EARLY / LATE
        {
            const auto* c = findControl (uiMap, "earlyLate");
            const float lo = (float) (*c)["rangeDb"][0], hi = (float) (*c)["rangeDb"][1];
            int i = 0;
            for (const char* key : { "early", "late" })
            {
                const auto track = rectFrom ((*c)[key]["track"]);
                const auto bounds = track.expanded (4.0f, 6.0f);
                levelBars[(size_t) i] = std::make_unique<LevelBar> (local (track, bounds, s), lo, hi);
                levelBars[(size_t) i]->setTooltip (i == 0 ? "EARLY: level of the early reflections (dBFS, -48 to 0)"
                                                          : "LATE: level of the reverb tail (dBFS, -48 to 0)");
                place (*levelBars[(size_t) i], bounds);
                ++i;
            }
        }

        // ---- IN / OUT meters
        {
            const auto* c = findControl (uiMap, "meters");
            const auto bounds = rectFrom ((*c)["hitbox"]);
            std::array<juce::Rectangle<float>, 4> bars;
            for (int i = 0; i < 4; ++i)
                bars[(size_t) i] = local (rectFrom ((*c)["bars"][i]), bounds, s);
            std::vector<std::pair<float, float>> sc;
            if (auto* props = (*c)["scale"].getDynamicObject())
                for (auto& prop : props->getProperties())
                    sc.emplace_back (prop.name.toString().getFloatValue(), ((float) prop.value - bounds.getY()) * s);
            meters = std::make_unique<BarMeters> (bars, std::move (sc));
            meters->setTooltip ("IN / OUT: peak level of the plugin input and output, left and right (dBFS)");
            place (*meters, bounds);
            controlBounds["meters"] = bounds;
        }

        // ---- filter graph
        {
            const auto* c = findControl (uiMap, "filterGraph");
            const auto plot = rectFrom ((*c)["plot"]);
            const auto strip = rectFrom ((*c)["labelStrip"]);
            const auto bounds = plot.getUnion (strip);
            filterGraph = std::make_unique<FilterGraph> ([this] (double hz) { return proc.filterResponseDb (hz); },
                                                         local (plot, bounds, s), local (strip, bounds, s),
                                                         (float) (*c)["rangeDb"][0], (float) (*c)["rangeDb"][1], 9.0f * s);
            filterGraph->setTooltip ("Response of HPF and LPF as they run (one pass; in LOOP each repeat goes through them again)");
            place (*filterGraph, bounds);
            controlBounds["filterGraph"] = bounds;
        }

        // ---- preset bar
        {
            const auto* c = findControl (uiMap, "presetBar");
            const auto frame = rectFrom ((*c)["frame"]);
            presetBar = std::make_unique<PresetBar> (local (rectFrom ((*c)["prev"]), frame, s), local (rectFrom ((*c)["name"]), frame, s),
                                                     local (rectFrom ((*c)["next"]), frame, s), local (rectFrom ((*c)["save"]), frame, s),
                                                     local (rectFrom ((*c)["delete"]), frame, s), (float) (*c)["textCapHeight"] * s);
            presetBar->onClick = [this] (PresetBar::Part part) { presetClicked (part); };
            place (*presetBar, frame);
            controlBounds["presetBar"] = frame;
        }

        refresh();
        finishSetup();
        startTimerHz (30);
    }

    VoidEditor::~VoidEditor()
    {
        stopTimer();
    }

    void VoidEditor::presetClicked (PresetBar::Part part)
    {
        switch (part)
        {
            case PresetBar::Part::prev:   proc.stepPreset (-1); break;
            case PresetBar::Part::next:   proc.stepPreset (+1); break;
            case PresetBar::Part::name:   getToolbar().showPresetMenu (presetBar.get()); break;
            case PresetBar::Part::save:   getToolbar().showSaveMenu (presetBar.get()); break;
            case PresetBar::Part::remove: getToolbar().deleteCurrentPreset(); break;
            case PresetBar::Part::none:   break;
        }
        refresh();
    }

    void VoidEditor::refresh (float seconds)
    {
        if (levelBars[0])
        {
            levelBars[0]->push (proc.takeEarlyLevel(), seconds);
            levelBars[1]->push (proc.takeLateLevel(), seconds);
        }
        if (meters)
            for (int i = 0; i < 4; ++i)
            {
                auto& src = i < 2 ? proc.inputMeters[(size_t) i] : proc.outputMeters[(size_t) (i - 2)];
                meters->push (i, src.takePeak(), seconds);
            }
        if (filterGraph)
            filterGraph->update();
        if (presetBar)
        {
            auto name = proc.presets.getCurrentName();
            if (proc.isPresetModified())
                name << " *";
            presetBar->setText (name, proc.presets.getCurrentIndex() >= 0 && ! proc.presets.isCurrentFactory());
        }
        if (drown != nullptr)
        {
            // the tooltip says what DROWN really does to the settings (the
            // processor's effective values; the knobs keep the user's own)
            auto tip = juce::String ("DROWN ") + drown->getReadoutText()
                     + ": one move from a room into an endless wash. It raises size, decay, diffusion, modulation and "
                       "width together and darkens the top, on top of your own settings.";
            if (drown->getNormalisedValue() > 0.0f)
            {
                const auto e = proc.getEffectiveSettings();
                tip << "\nIn use now: size " << params::percentText (e.size) << ", decay " << params::decayText (e.decay)
                    << ", diffusion " << params::percentText (e.diffusion) << ", mod depth " << params::percentText (e.modDepth)
                    << ", width " << params::percentText (e.width) << ", LPF " << params::hzText (e.lpf);
            }
            if (drown->getTooltip() != tip)
                drown->setTooltip (tip);
        }
    }

    ui::Knob& VoidEditor::getKnob (const juce::String& controlId)
    {
        if (controlId == "drown")
            return *drown;
        for (auto& k : knobs)
            if (k.id == controlId)
                return *k.knob;
        jassertfalse;
        return *knobs.front().knob;
    }

    LampGroup& VoidEditor::getLampGroup (const juce::String& groupId)
    {
        for (auto& g : groups)
            if (g.id == groupId)
                return *g.group;
        jassertfalse;
        return *groups.front().group;
    }

    const std::vector<ScaleLabel>& VoidEditor::getScaleLabels (const juce::String& controlId) const
    {
        for (auto& k : knobs)
            if (k.id == controlId)
                return k.labels;
        static const std::vector<ScaleLabel> none;
        return none;
    }

    const std::vector<StepDot>& VoidEditor::getStepDots (const juce::String& controlId) const
    {
        for (auto& k : knobs)
            if (k.id == controlId)
                return k.dots;
        static const std::vector<StepDot> none;
        return none;
    }

    juce::Rectangle<int> VoidEditor::getControlBounds (const juce::String& controlId) const
    {
        const auto it = controlBounds.find (controlId);
        return it == controlBounds.end() ? juce::Rectangle<int>() : refToPanel (it->second);
    }

    juce::Rectangle<int> VoidEditor::refToPanel (juce::Rectangle<float> r) const
    {
        return (r * skinScale).translated ((float) panelArea.getX(), (float) panelArea.getY()).toNearestInt();
    }

    void VoidEditor::layoutPanel (juce::Rectangle<int> area)
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

    void VoidEditor::paintPanel (juce::Graphics& g, juce::Rectangle<int> area)
    {
        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        g.drawImage (plate, area.toFloat(), juce::RectanglePlacement::stretchToFit);

        const auto origin = area.getPosition().toFloat();
        for (auto& k : knobs)
        {
            const bool lowRow = k.bounds.getY() > 700.0f;
            paintScale (g, k.dots, k.labels, k.capHeight, skinScale, origin, dotInk, lowRow ? lowLabelInk : topLabelInk);
        }
    }
}
