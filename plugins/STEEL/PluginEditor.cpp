#include "PluginEditor.h"
#include "Parameters.h"
#include "STEELSkin.h"
#include "ui/TleetLookAndFeel.h"

namespace tleet::steel
{
    namespace
    {
        // Ink measured on the artwork.
        const juce::Colour dotInk { 0xffc9b7a6 };         // knob scale dots
        const juce::Colour knobLabelInk { 0xffcfbdab };   // knob scale numbers
        const juce::Colour bigReadoutInk { 0xfff2c2b7 };  // INPUT / CEILING windows
        const juce::Colour smallReadoutInk { 0xffeedccb }; // small knob windows
        const juce::Colour pointerCore { 0xfffff6f2 };    // pointers: white core
        const juce::Colour pointerGlow { 0xffd98a8a };    // in a faint rose glow
        const juce::Colour scaleInk { 0xffb1a197 };       // display scales
        const juce::Colour gridInk { 0xff3a2e2e };
        const juce::Colour waveInk { 0xffcfb7ba };        // output envelope
        const juce::Colour grCore { 0xfff8938b };         // gain-reduction trace
        const juce::Colour grGlow { 0xffd04848 };
        const juce::Colour warnInk { 0xffff6a5e };
        const juce::Colour valueInk { 0xfff59b99 };       // loudness values
        const juce::Colour barCool { 0xffebcdc9 };        // lit bars below -12 dB
        const juce::Colour barHot { 0xffed9b9c };         // lit bars above -12 dB
        constexpr float hotDb = -12.0f;
        constexpr float releaseDimmedAlpha = 0.4f;

        juce::Rectangle<float> local (juce::Rectangle<float> r, juce::Rectangle<float> bounds, float s)
        {
            return (r - bounds.getPosition()) * s;
        }

        float localX (float x, juce::Rectangle<float> bounds, float s) { return (x - bounds.getX()) * s; }
        float localY (float y, juce::Rectangle<float> bounds, float s) { return (y - bounds.getY()) * s; }

        float toDb (float linear) { return linear > 0.0f ? 20.0f * std::log10 (linear) : -200.0f; }

        juce::Font scaleFontFor (float capHeight)
        {
            return ui::TleetLookAndFeel::labelFont (capHeight / 0.72f, false).withHorizontalScale (0.86f);
        }
    }

    using namespace ui::skin;

    // ================================================================== history

    HistoryDisplay::HistoryDisplay (Geometry g, double secs, double slice)
        : geo (g), seconds (secs), sliceSeconds (slice),
          ring ((size_t) juce::jmax (1, (int) std::ceil (secs / slice)))
    {
    }

    void HistoryDisplay::push (const Slice* s, int n)
    {
        for (int i = 0; i < n; ++i)
        {
            ring[(size_t) head] = s[i];
            head = (head + 1) % (int) ring.size();
            filled = juce::jmin (filled + 1, (int) ring.size());
        }
        if (n > 0)
            repaint();
    }

    HistoryDisplay::Slice HistoryDisplay::getSlice (int age) const noexcept
    {
        if (age < 0 || age >= filled)
            return {};
        const int n = (int) ring.size();
        return ring[(size_t) (((head - 1 - age) % n + n) % n)];
    }

    float HistoryDisplay::getDeepestGrDb (double lastSeconds) const noexcept
    {
        const int n = juce::jmin (filled, juce::jmax (1, (int) std::ceil (lastSeconds / sliceSeconds)));
        float deepest = 0.0f;
        for (int a = 0; a < n; ++a)
            deepest = std::min (deepest, getSlice (a).grDb);
        return deepest;
    }

    void HistoryDisplay::setCeilingDb (float db)
    {
        if (! juce::exactlyEqual (db, ceilingDb))
        {
            ceilingDb = db;
            repaint();
        }
    }

    void HistoryDisplay::setWarnings (bool over, bool heavyLimiting)
    {
        if (over != tpOver || heavyLimiting != heavy)
        {
            tpOver = over;
            heavy = heavyLimiting;
            repaint();
        }
    }

    float HistoryDisplay::yForDb (float db) const noexcept
    {
        const float t = (geo.topDb - juce::jlimit (geo.bottomDb, geo.topDb, db)) / (geo.topDb - geo.bottomDb);
        return geo.plot.getY() + t * geo.plot.getHeight();
    }

    float HistoryDisplay::yForGrDb (float grDb) const noexcept
    {
        const float pxPerDb = geo.plot.getHeight() / (geo.topDb - geo.bottomDb);
        return juce::jmin (geo.plot.getBottom(), geo.grBaselineY - std::min (0.0f, grDb) * pxPerDb);
    }

    float HistoryDisplay::xForAge (double secondsAgo) const noexcept
    {
        return geo.plot.getRight() - (float) (secondsAgo / seconds) * geo.plot.getWidth();
    }

    juce::String HistoryDisplay::timeLabel (double secondsAgo)
    {
        if (secondsAgo < 1.0e-6)
            return "0";
        const double r = std::round (secondsAgo);
        return "-" + (std::abs (secondsAgo - r) < 1.0e-6 ? juce::String ((int) r) : juce::String (secondsAgo, 1)) + "s";
    }

    juce::String HistoryDisplay::dbLabel (float db)
    {
        return juce::String (juce::roundToInt (db));
    }

    void HistoryDisplay::paint (juce::Graphics& g)
    {
        const auto& p = geo.plot;
        const auto font = scaleFontFor (geo.labelCapHeight);

        // grid, scale and ticks
        g.setColour (gridInk);
        for (float db = geo.topDb; db >= geo.bottomDb - 0.01f; db -= geo.dbStep)
            g.drawHorizontalLine (juce::roundToInt (yForDb (db)), p.getX(), p.getRight());
        const int timeSteps = 4;
        for (int i = 0; i <= timeSteps; ++i)
            g.drawVerticalLine (juce::roundToInt (xForAge (seconds * (double) i / timeSteps)), p.getY(), p.getBottom());

        g.setFont (font);
        g.setColour (scaleInk);
        for (float db = geo.topDb; db >= geo.bottomDb - 0.01f; db -= geo.dbStep)
        {
            const float y = yForDb (db);
            g.drawText (dbLabel (db), juce::Rectangle<float> (geo.labelColumn.getStart(), y - font.getHeight() * 0.5f,
                                                              geo.labelColumn.getLength(), font.getHeight()).translated (0.0f, font.getHeight() * 0.07f),
                        juce::Justification::centredRight, false);
            g.fillRect (juce::Rectangle<float> (geo.leftTicks.getStart(), y - 0.5f, geo.leftTicks.getLength(), 1.0f));
            g.fillRect (juce::Rectangle<float> (geo.rightTicks.getStart(), y - 0.5f, geo.rightTicks.getLength(), 1.0f));
        }
        for (int i = 0; i <= timeSteps; ++i)
        {
            const double age = seconds * (double) (timeSteps - i) / timeSteps;
            const auto text = timeLabel (age);
            const float w = juce::GlyphArrangement::getStringWidth (font, text) + 4.0f;
            auto box = juce::Rectangle<float> (w, font.getHeight()).withCentre ({ xForAge (age), geo.timeLabelY });
            box = box.withX (juce::jlimit (0.0f, (float) getWidth() - w, box.getX()));
            g.drawText (text, box.translated (0.0f, font.getHeight() * 0.07f), juce::Justification::centred, false);
        }

        // output envelope (peak, and RMS inside it) and the GR trace, one column per pixel
        const int columns = juce::jmax (1, juce::roundToInt (p.getWidth()));
        juce::Path peak, rms, gr;
        bool started = false;
        float lastX = p.getRight();
        for (int c = columns - 1; c >= 0; --c) // newest (right) to oldest (left)
        {
            const double age0 = seconds * (double) (columns - 1 - c) / columns;
            const double age1 = seconds * (double) (columns - c) / columns;
            const int a0 = (int) std::floor (age0 / sliceSeconds);
            const int a1 = juce::jmax (a0 + 1, (int) std::floor (age1 / sliceSeconds));
            if (a0 >= filled)
                break;
            Slice agg { -200.0f, -200.0f, 0.0f };
            for (int a = a0; a < juce::jmin (a1, filled); ++a)
            {
                const auto s = getSlice (a);
                agg.peakDb = std::max (agg.peakDb, s.peakDb);
                agg.rmsDb = std::max (agg.rmsDb, s.rmsDb);
                agg.grDb = std::min (agg.grDb, s.grDb);
            }
            const float x = p.getX() + (float) c + 0.5f;
            if (! started)
            {
                peak.startNewSubPath (x + 0.5f, p.getBottom());
                rms.startNewSubPath (x + 0.5f, p.getBottom());
                gr.startNewSubPath (x + 0.5f, yForGrDb (agg.grDb));
                started = true;
            }
            peak.lineTo (x, yForDb (agg.peakDb));
            rms.lineTo (x, yForDb (agg.rmsDb));
            gr.lineTo (x, yForGrDb (agg.grDb));
            lastX = x;
        }
        if (started)
        {
            peak.lineTo (lastX - 0.5f, p.getBottom());
            peak.closeSubPath();
            rms.lineTo (lastX - 0.5f, p.getBottom());
            rms.closeSubPath();
            juce::Graphics::ScopedSaveState save (g);
            g.reduceClipRegion (p.withTop (geo.grBaselineY - 4.0f).toNearestInt());
            g.setGradientFill (juce::ColourGradient (waveInk.withAlpha (0.55f), 0.0f, p.getY(), waveInk.withAlpha (0.18f), 0.0f, p.getBottom(), false));
            g.fillPath (peak);
            g.setGradientFill (juce::ColourGradient (waveInk.withAlpha (0.85f), 0.0f, p.getY(), waveInk.withAlpha (0.35f), 0.0f, p.getBottom(), false));
            g.fillPath (rms);
            g.setColour (grGlow.withAlpha (0.35f));
            g.strokePath (gr, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setColour (grCore);
            g.strokePath (gr, juce::PathStrokeType (1.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        // the ceiling
        {
            const float y = yForDb (ceilingDb);
            const float dash[] = { 4.0f, 3.0f };
            g.setColour (grCore.withAlpha (0.55f));
            g.drawDashedLine (juce::Line<float> (p.getX(), y, p.getRight(), y), dash, 2, 0.8f);
        }

        // warnings, only while they are on
        if (tpOver || heavy)
        {
            const auto font = ui::TleetLookAndFeel::labelFont (geo.labelCapHeight / 0.72f, true);
            g.setFont (font);
            auto row = juce::Rectangle<float> (p.getRight() - 160.0f, p.getY() + 3.0f, 156.0f, geo.labelCapHeight * 1.7f);
            for (auto [on, text] : { std::pair<bool, const char*> { tpOver, "TP OVER" }, { heavy, "HEAVY LIMITING" } })
            {
                if (! on)
                    continue;
                // on a patch of the display's glass, so the traces under it do not hide it
                const float w = juce::GlyphArrangement::getStringWidth (font, text) + 8.0f;
                g.setColour (juce::Colour (0xff0b0909).withAlpha (0.8f));
                g.fillRoundedRectangle (row.withLeft (row.getRight() - w).expanded (0.0f, -1.0f), 2.0f);
                g.setColour (warnInk);
                g.drawText (text, row.withTrimmedRight (4.0f), juce::Justification::centredRight, false);
                row.translate (0.0f, row.getHeight() + 2.0f);
            }
        }
    }

    // ================================================================== IN / OUT bars

    PeakBars::PeakBars (std::array<juce::Rectangle<float>, 4> b, float top, float bottom, float tDb, float bDb,
                        juce::Range<float> labels, float cap)
        : bars (b), yTop (top), yBottom (bottom), topDb (tDb), bottomDb (bDb), labelColumn (labels), capHeight (cap)
    {
    }

    float PeakBars::yForDb (float db) const noexcept
    {
        const float t = (topDb - juce::jlimit (bottomDb, topDb, db)) / (topDb - bottomDb);
        return yTop + t * (yBottom - yTop);
    }

    void PeakBars::push (int bar, float db, float seconds)
    {
        auto& d = displayed[(size_t) bar];
        const float before = d;
        if (std::isnan (db))
            d = std::max (-200.0f, d - fallDbPerSecond * seconds);
        else if (db >= d)
            d = db;
        else
            d = std::max (db, d - fallDbPerSecond * seconds);
        if (std::abs (yForDb (d) - yForDb (before)) > 0.05f)
            repaint();
    }

    void PeakBars::paint (juce::Graphics& g)
    {
        if (! labelColumn.isEmpty())
        {
            const auto font = scaleFontFor (capHeight);
            g.setFont (font);
            g.setColour (scaleInk);
            for (float db = topDb; db >= bottomDb - 0.01f; db -= 6.0f)
                g.drawText (HistoryDisplay::dbLabel (db),
                            juce::Rectangle<float> (labelColumn.getStart(), yForDb (db) - font.getHeight() * 0.5f, labelColumn.getLength(), font.getHeight())
                                .translated (0.0f, font.getHeight() * 0.07f),
                            juce::Justification::centredRight, false);
        }

        const float hotY = yForDb (hotDb);
        for (size_t i = 0; i < bars.size(); ++i)
        {
            const float db = displayed[i];
            if (db <= bottomDb)
                continue;
            const auto b = bars[i];
            const float top = yForDb (db);
            const auto lit = juce::Rectangle<float> (b.getX() + 0.5f, top, b.getWidth() - 1.0f, b.getBottom() - top);
            g.setColour (barHot.withAlpha (0.16f));
            g.fillRect (lit.expanded (1.5f, 0.0f));
            g.setColour (barCool);
            g.fillRect (lit);
            if (top < hotY)
            {
                g.setColour (barHot);
                g.fillRect (lit.withBottom (hotY));
            }
        }
    }

    // ================================================================== loudness panel

    LoudnessPanel::LoudnessPanel (std::array<juce::Rectangle<float>, numFields> r, float cap) : rects (r), capHeight (cap)
    {
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        for (int f = 0; f < numFields; ++f)
            texts[(size_t) f] = format ((Field) f, f == gainReduction ? 0.0f : -1000.0f);
    }

    juce::String LoudnessPanel::format (Field f, float v)
    {
        if (! std::isfinite (v))
            return "--";
        switch (f)
        {
            case lufsI:
            case lufsS:         return v < -70.0f ? "--" : juce::String (v, 1);
            case lra:           return v < 0.0f ? "--" : juce::String (v, 1);
            case truePeak:      return v < -100.0f ? "--" : juce::String (v, 1);
            case gainReduction: return juce::String (std::abs (v) < 0.05f ? 0.0f : v, 1);
            case numFields:     break;
        }
        return {};
    }

    void LoudnessPanel::setValues (float i, float s, float r, float tp, float gr, bool tpOver)
    {
        const float v[] = { i, s, r, tp, gr };
        bool changed = tpOver != over;
        over = tpOver;
        for (int f = 0; f < numFields; ++f)
        {
            auto t = format ((Field) f, v[f]);
            if (t != texts[(size_t) f])
            {
                texts[(size_t) f] = t;
                changed = true;
            }
        }
        if (changed)
            repaint();
    }

    void LoudnessPanel::paint (juce::Graphics& g)
    {
        g.setFont (ui::TleetLookAndFeel::labelFont (capHeight / 0.72f, false).withHorizontalScale (0.9f));
        for (int f = 0; f < numFields; ++f)
        {
            g.setColour (f == truePeak && over ? warnInk : valueInk);
            g.drawFittedText (texts[(size_t) f], rects[(size_t) f].translated (0.0f, capHeight * 0.1f).toNearestInt(),
                              juce::Justification::centred, 1, 0.8f);
        }
    }

    void LoudnessPanel::mouseUp (const juce::MouseEvent& e)
    {
        if (! e.mouseWasDraggedSinceMouseDown() && ! e.mods.isPopupMenu() && onReset)
            onReset();
    }

    // ================================================================== editor

    juce::var SteelEditor::loadUiMap()
    {
        return juce::JSON::parse (juce::String::fromUTF8 (STEELSkin::UI_MAP_json, STEELSkin::UI_MAP_jsonSize));
    }

    juce::var SteelEditor::loadSpriteMap()
    {
        return juce::JSON::parse (juce::String::fromUTF8 (STEELSkin::sprites_json, STEELSkin::sprites_jsonSize));
    }

    juce::String SteelEditor::labelText (const juce::String& id, float v)
    {
        if (id == params::release && v >= 999.5f)
            return juce::String (juce::roundToInt (v / 1000.0f)) + "K";
        const int n = juce::roundToInt (v);
        if (id == params::output)
            return n > 0 ? "+" + juce::String (n) : juce::String (n);
        return juce::String (n);
    }

    juce::Rectangle<float> SteelEditor::spriteRect (const juce::String& name) const
    {
        return rectFrom (spriteMap["sprites"][juce::Identifier (name)]);
    }

    juce::Image SteelEditor::spriteImage (const juce::String& name) const
    {
        int size = 0;
        juce::Image image;
        if (auto* data = STEELSkin::getNamedResource ((name + "_png").toRawUTF8(), size))
            image = juce::ImageCache::getFromMemory (data, size);
        jassert (image.isValid());
        return image;
    }

    void SteelEditor::place (juce::Component& c, juce::Rectangle<float> refBounds)
    {
        addToPanel (c);
        placed.emplace_back (&c, refBounds);
    }

    SteelEditor::SteelEditor (SteelProcessor& p)
        : ui::TleetEditorBase (p, baseWidth, toolbarHeight + panelHeight),
          proc (p),
          uiMap (loadUiMap()),
          spriteMap (loadSpriteMap()),
          plate (juce::ImageCache::getFromMemory (STEELSkin::plate_png, STEELSkin::plate_pngSize))
    {
        const float s = skinScale;
        auto onEdit = [this] { proc.commitHistory(); };

        // ---- knobs: pointer, live scale, live readout
        const std::pair<const char*, const char*> tips[] = {
            { "input",          "INPUT: gain into the limiter. More input = more limiting and a louder result at the same ceiling" },
            { "ceiling",        "CEILING: the highest level the output may reach (true peak with TRUE PEAK on, sample peak with it off)" },
            { "release",        "RELEASE: how fast the gain comes back after a peak (MANUAL)" },
            { "lookahead",      "LOOKAHEAD: how far ahead the limiter sees peaks coming. Longer = cleaner transients, more latency" },
            { "stereoLink",     "STEREO LINK: 100 % = the same gain reduction on both channels (stable image), 0 % = each channel on its own" },
            { "softClipAmount", "SOFT CLIP amount: how much of the peak the clipper shaves off before the limiter" },
            { "output",         "OUTPUT: level after the limiter. Above 0 dB the output can exceed the ceiling" },
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
            if (! readout.isEmpty())
            {
                sp.readout = local (readout, bounds, s);
                sp.readoutTextHeight = (float) (*c)["readoutCapHeight"] / 0.72f * s;
                sp.readoutColour = big ? bigReadoutInk : smallReadoutInk;
            }
            knob->setSkinPointer (sp);
            knob->setTooltip (tip);
            knob->onEditFinished = onEdit;
            addToPanel (*knob);

            KnobEntry e;
            e.id = id;
            e.bounds = bounds;
            if (c->hasProperty ("scaleLabels"))
            {
                e.labels = makeScaleLabels (*c, *param, [pid = param->getParameterID()] (float v, const juce::String&)
                                            { return labelText (pid, v); });
                e.capHeight = (float) (*c)["scaleLabels"]["capHeight"];
            }
            e.dots = makeStepDots (*c, *param);
            e.knob = std::move (knob);
            controlBounds[id] = bounds;
            knobs.push_back (std::move (e));
        }

        // ---- button groups
        const std::pair<const char*, juce::StringArray> groupTips[] = {
            { "mode", { "CLEAN: the most transparent limiting, slow and smooth",
                        "PUNCH: lets transients through, faster recovery",
                        "DENSE: more density and loudness, faster release",
                        "STEEL: the loudest and most aggressive character" } },
            { "releaseMode", { "AUTO: release follows the programme material",
                               "MANUAL: release set by the RELEASE knob" } },
            { "oversampling", { "1x: no oversampling (lowest CPU and latency)",
                                "2x oversampling of the limiter and clipper",
                                "4x oversampling of the limiter and clipper",
                                "8x oversampling of the limiter and clipper (highest CPU)" } },
            { "subFilter", { "SUB FILTER off",
                             "SUB FILTER: high-pass at 20 Hz before the limiter",
                             "SUB FILTER: high-pass at 25 Hz before the limiter",
                             "SUB FILTER: high-pass at 30 Hz before the limiter" } },
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
                const auto sprite = juce::String (gid) + "_" + b["label"].toString().toLowerCase();
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

        // ---- toggles
        const std::tuple<const char*, const char*, const char*> toggleTips[] = {
            { "truePeak",   "TRUE PEAK", "TRUE PEAK: the ceiling holds for the reconstructed (inter-sample) peaks, not just the samples" },
            { "softClip",   "SOFT CLIP", "SOFT CLIP: a soft clipper before the limiter takes the sharpest peaks off" },
            { "delta",      "DELTA",     "DELTA: listen to what the limiter removes (input minus output, at the same level)" },
            { "levelMatch", "LEVEL MATCH", "LEVEL MATCH: monitor at the loudness of the input, to judge the limiting rather than the volume" },
        };
        for (auto& [id, label, tip] : toggleTips)
        {
            const auto* c = findControl (uiMap, id);
            const auto hitbox = rectFrom ((*c)["hitbox"]);
            const auto lit = spriteRect (juce::String (id) + "_lit");
            const auto bounds = hitbox.getUnion (lit);
            auto t = std::make_unique<ui::LampToggle> (*p.apvts.getParameter ((*c)["parameter"].toString()), label,
                                                       spriteImage (juce::String (id) + "_lit"), local (lit, bounds, s), local (hitbox, bounds, s));
            t->setTooltip (tip);
            t->onEditFinished = onEdit;
            place (*t, bounds);
            controlBounds[id] = bounds;
            toggles.emplace_back (id, std::move (t));
        }

        // ---- history display
        {
            const auto* c = findControl (uiMap, "history");
            const auto plotRef = rectFrom ((*c)["plot"]);
            const auto grStrip = rectFrom ((*c)["grStrip"]);
            const auto& scaleX = (*c)["scaleX"];
            const juce::Rectangle<float> bounds (340.0f, 205.0f, 805.0f, 270.0f);
            HistoryDisplay::Geometry geo;
            geo.plot = local (plotRef, bounds, s);
            geo.grBaselineY = localY (grStrip.getY() + 6.0f, bounds, s);
            geo.labelColumn = { localX ((float) scaleX[0][0], bounds, s), localX ((float) scaleX[0][1], bounds, s) };
            geo.leftTicks = { localX (plotRef.getX() - 16.0f, bounds, s), localX (plotRef.getX() - 11.0f, bounds, s) };
            geo.rightTicks = { localX (plotRef.getRight() + 13.0f, bounds, s), localX (plotRef.getRight() + 18.0f, bounds, s) };
            geo.timeLabelY = localY ((float) (*c)["timeLabels"]["y"], bounds, s);
            geo.labelCapHeight = 10.5f * s;
            history = std::make_unique<HistoryDisplay> (geo, historySeconds, SteelProcessor::historySliceSeconds);
            history->setTooltip ("The last 10 seconds of the output: level (peak, RMS inside it, dBFS), the gain reduction "
                                 "the limiter applied (red, falling from the GR line, same dB scale) and the ceiling (dashed)");
            place (*history, bounds);
            controlBounds["history"] = rectFrom ((*c)["hitbox"]);
        }

        // ---- IN / OUT meters
        {
            const auto* c = findControl (uiMap, "meters");
            const float top = (float) (*c)["scale"]["0"], bottom = (float) (*c)["scale"]["-48"];
            const auto& scaleX = (*findControl (uiMap, "history"))["scaleX"];
            for (int side = 0; side < 2; ++side)
            {
                const auto hit = rectFrom ((*c)[side == 0 ? "hitboxIn" : "hitboxOut"]);
                const auto bounds = side == 0 ? hit : hit.withRight ((float) scaleX[1][1] + 4.0f);
                std::array<juce::Rectangle<float>, 4> bars;
                for (int i = 0; i < 4; ++i)
                    bars[(size_t) i] = local (rectFrom ((*c)[side == 0 ? "in" : "out"][i]), bounds, s);
                const juce::Range<float> labels = side == 0 ? juce::Range<float>()
                                                            : juce::Range<float> (localX ((float) scaleX[1][0], bounds, s), localX ((float) scaleX[1][1], bounds, s));
                auto m = std::make_unique<PeakBars> (bars, localY (top, bounds, s), localY (bottom, bounds, s), 0.0f, -48.0f, labels, 10.5f * s);
                m->setTooltip (side == 0 ? "IN: input sample peak L / R (left pair) and true peak L / R (right pair), after INPUT"
                                         : "OUT: output sample peak L / R (left pair) and true peak L / R (right pair)");
                place (*m, bounds);
                controlBounds[side == 0 ? "metersIn" : "metersOut"] = bounds;
                (side == 0 ? inMeters : outMeters) = std::move (m);
            }
        }

        // ---- loudness panel
        {
            const auto* c = findControl (uiMap, "loudness");
            const auto frame = rectFrom ((*c)["frame"]);
            std::array<juce::Rectangle<float>, LoudnessPanel::numFields> rects;
            for (int i = 0; i < LoudnessPanel::numFields; ++i)
                rects[(size_t) i] = local (rectFrom ((*c)["values"][i]["rect"]), frame, s);
            loudness = std::make_unique<LoudnessPanel> (rects, (float) (*c)["valueCapHeight"] * s);
            loudness->onReset = [this] { proc.resetLoudness(); refresh (0.0f); };
            loudness->setTooltip ("Loudness of the output (ITU-R BS.1770 / EBU R128): integrated LUFS, short-term LUFS (3 s), "
                                  "loudness range (LU), highest true peak (dBTP) and the gain reduction now (dB). "
                                  "Click to start the measurement again");
            place (*loudness, frame);
            controlBounds["loudness"] = frame;
        }

        // ---- output spectrum
        {
            const auto* c = findControl (uiMap, "spectrum");
            const auto screen = rectFrom ((*c)["screen"]);
            const auto strip = rectFrom ((*c)["labelStrip"]);
            const auto bounds = screen.getUnion (strip);
            ui::SpectrumDisplay::Style st;
            st.screen = local (screen, bounds, s);
            st.labelStrip = local (strip, bounds, s);
            st.gridFrequencies.clear();
            for (auto& f : *(*c)["frequencies"].getArray())
                st.gridFrequencies.push_back ((float) f);
            st.inset = (float) (*c)["inset"] * s;
            st.labelCapHeight = 9.5f * s;
            st.lineWidth = 1.4f;
            st.line = juce::Colour (0xfff3d9dd);
            st.glow = juce::Colour (0xffd05a6a);
            st.fill = juce::Colour (0xffb89b97);
            st.grid = juce::Colour (0xff4e3c3c);
            st.labelInk = juce::Colour (0xffae9d91);
            spectrum = std::make_unique<ui::SpectrumDisplay> (p.outputSpectrum, st);
            spectrum->setTooltip ("Spectrum of the output (level per 1/6 octave; a full-scale sine reads 0 dB)");
            spectrum->setQuality ((ui::SpectrumDisplay::Quality) juce::jlimit (0, 3, (int) p.getUiProperty ("analyzerQuality", 2)));
            place (*spectrum, bounds);
            controlBounds["spectrum"] = bounds;
            qualityButton = std::make_unique<ui::SpectrumQualityButton> (*spectrum);
            qualityButton->onQualityChanged = [this] (ui::SpectrumDisplay::Quality q) { proc.setUiProperty ("analyzerQuality", (int) q); };
            addToToolbar (*qualityButton, 92);
        }

        sliceBuffer.resize (256);
        refresh (0.0f);
        finishSetup();
        startTimerHz (30);
    }

    SteelEditor::~SteelEditor()
    {
        stopTimer();
    }

    void SteelEditor::refresh (float seconds)
    {
        // history slices -> the large display
        if (history != nullptr)
        {
            std::array<SteelProcessor::HistorySlice, 256> raw;
            for (;;)
            {
                const int n = proc.popHistory (raw.data(), (int) raw.size());
                if (n <= 0)
                    break;
                for (int i = 0; i < n; ++i)
                    sliceBuffer[(size_t) i] = { raw[(size_t) i].outPeakDb, raw[(size_t) i].outRmsDb, raw[(size_t) i].grDb };
                history->push (sliceBuffer.data(), n);
                if (n < (int) raw.size())
                    break;
            }
            history->setCeilingDb (proc.apvts.getRawParameterValue (params::ceiling)->load());
            history->setWarnings (proc.isTruePeakOver(), proc.isHeavyLimiting());
        }

        if (inMeters != nullptr)
            for (int ch = 0; ch < 2; ++ch)
            {
                inMeters->push (ch, toDb (proc.inputMeters[(size_t) ch].takePeak()), seconds);
                inMeters->push (2 + ch, proc.takeInputTruePeakDb (ch), seconds);
                outMeters->push (ch, toDb (proc.outputMeters[(size_t) ch].takePeak()), seconds);
                outMeters->push (2 + ch, proc.takeOutputTruePeakDb (ch), seconds);
            }

        if (loudness != nullptr)
        {
            const auto r = proc.getLoudness();
            const float gr = history != nullptr && history->getNumFilled() > 0 ? history->getDeepestGrDb (0.3)
                                                                                 : proc.getCurrentGainReductionDb();
            // LRA reads 0 until the measurement has enough material; shown as "--" until LUFS-I exists
            loudness->setValues (r.integratedLufs, r.shortTermLufs, std::isfinite (r.integratedLufs) ? r.loudnessRangeLu : -1.0f,
                                 r.maxTruePeakDbtp, gr, proc.isTruePeakOver());
        }

        // RELEASE: what AUTO is doing right now
        for (auto& k : knobs)
            if (k.id == "release")
            {
                const bool autoRelease = proc.apvts.getRawParameterValue (params::releaseMode)->load() < 0.5f;
                juce::String tip ("RELEASE: how fast the gain comes back after a peak. ");
                tip << (autoRelease ? "AUTO ignores this knob (it sets the release for MANUAL); the release follows the material, now "
                                        + juce::String (juce::roundToInt (proc.getEffectiveReleaseMs())) + " ms"
                                    : juce::String ("MANUAL: exactly this time"));
                if (k.knob->getTooltip() != tip)
                    k.knob->setTooltip (tip);
                // AUTO does not use the knob (MODEL_SPEC D8): dimmed, still settable for MANUAL
                k.knob->setAlpha (autoRelease ? releaseDimmedAlpha : 1.0f);
            }
    }

    ui::Knob& SteelEditor::getKnob (const juce::String& controlId)
    {
        for (auto& k : knobs)
            if (k.id == controlId)
                return *k.knob;
        jassertfalse;
        return *knobs.front().knob;
    }

    LampGroup& SteelEditor::getLampGroup (const juce::String& groupId)
    {
        for (auto& g : groups)
            if (g.id == groupId)
                return *g.group;
        jassertfalse;
        return *groups.front().group;
    }

    ui::LampToggle& SteelEditor::getToggle (const juce::String& controlId)
    {
        for (auto& [id, t] : toggles)
            if (id == controlId)
                return *t;
        jassertfalse;
        return *toggles.front().second;
    }

    const std::vector<ScaleLabel>& SteelEditor::getScaleLabels (const juce::String& controlId) const
    {
        for (auto& k : knobs)
            if (k.id == controlId)
                return k.labels;
        static const std::vector<ScaleLabel> none;
        return none;
    }

    const std::vector<StepDot>& SteelEditor::getStepDots (const juce::String& controlId) const
    {
        for (auto& k : knobs)
            if (k.id == controlId)
                return k.dots;
        static const std::vector<StepDot> none;
        return none;
    }

    juce::Rectangle<int> SteelEditor::getControlBounds (const juce::String& controlId) const
    {
        const auto it = controlBounds.find (controlId);
        return it == controlBounds.end() ? juce::Rectangle<int>() : refToPanel (it->second);
    }

    juce::Rectangle<int> SteelEditor::refToPanel (juce::Rectangle<float> r) const
    {
        return (r * skinScale).translated ((float) panelArea.getX(), (float) panelArea.getY()).toNearestInt();
    }

    void SteelEditor::layoutPanel (juce::Rectangle<int> area)
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

    void SteelEditor::paintPanel (juce::Graphics& g, juce::Rectangle<int> area)
    {
        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        g.drawImage (plate, area.toFloat(), juce::RectanglePlacement::stretchToFit);

        const auto origin = area.getPosition().toFloat();
        for (auto& k : knobs)
            paintScale (g, k.dots, k.labels, k.capHeight, skinScale, origin, dotInk, knobLabelInk);
    }
}
