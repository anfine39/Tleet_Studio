#include "PluginEditor.h"
#include "Parameters.h"
#include "ECHOSkin.h"

namespace tleet::echo
{
    namespace
    {
        // Printed ink measured on the artwork: the large captions of the top
        // row, and the smaller, dimmer numbers of the lower rows.
        const juce::Colour topInk { 0xffedd9be };
        const juce::Colour bottomInk { 0xffdcc3a4 };

        // The artwork's lit amber: a near-white core in an orange glow.
        const juce::Colour ledCore { 0xffffe6a6 };
        const juce::Colour ledGlow { 0xffff8a1e };

        juce::Rectangle<float> local (juce::Rectangle<float> r, juce::Rectangle<float> bounds, float s)
        {
            return (r - bounds.getPosition()) * s;
        }

        int choiceIndex (juce::RangedAudioParameter& p)
        {
            return juce::roundToInt (p.convertFrom0to1 (p.getValue()));
        }
    }

    using namespace ui::skin;

    // ================================================================== MODE lamp

    ModeLamp::ModeLamp (juce::Image lit, juce::Point<float> c, std::vector<juce::Point<float>> p, float s)
        : image (std::move (lit)), litCentre (c), positions (std::move (p)), scale (s)
    {
        setInterceptsMouseClicks (false, false);
    }

    float ModeLamp::glowFor (float headPeak) noexcept
    {
        // -48 dBFS at the heads and below: the base glow of a selected lamp;
        // full brightness at 0 dBFS
        const float db = 20.0f * std::log10 (std::max (headPeak, 1.0e-6f));
        return 0.55f + 0.45f * juce::jlimit (0.0f, 1.0f, (db + 48.0f) / 48.0f);
    }

    void ModeLamp::show (int newPosition, float headPeak, float seconds)
    {
        const float target = glowFor (headPeak);
        if (newPosition != position)
        {
            position = newPosition;
            glow = target;
        }
        else
        {
            // display only: instant rise, 150 ms fall
            glow = target >= glow ? target : target + (glow - target) * std::exp (-seconds / 0.15f);
        }
        if (std::abs (glow - shown) > 0.01f)
        {
            shown = glow;
            repaint();
        }
    }

    void ModeLamp::paint (juce::Graphics& g)
    {
        if (position < 0 || position >= (int) positions.size() || ! image.isValid())
            return;
        const auto at = positions[(size_t) position] - litCentre * scale;
        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        g.setOpacity (glow);
        g.drawImage (image, { at.x, at.y, (float) image.getWidth() * scale, (float) image.getHeight() * scale },
                     juce::RectanglePlacement::stretchToFit);
    }

    // ================================================================== slide switch

    SlideSwitch::SlideSwitch (juce::RangedAudioParameter& p, std::array<juce::Image, 2> imgs, juce::Rectangle<float> h,
                              std::array<juce::Rectangle<float>, 2> caps, std::function<void()> edited)
        : attachment (p, [this] (float v) { state = juce::roundToInt (v); repaint(); }),
          images (std::move (imgs)), housing (h), captions (caps), onEdit (std::move (edited))
    {
        setTitle (p.getName (64));
        attachment.sendInitialUpdate();
    }

    void SlideSwitch::clickAt (juce::Point<float> p)
    {
        int next = 1 - state;
        if (captions[0].contains (p))
            next = 0;
        else if (captions[1].contains (p))
            next = 1;
        else if (! housing.contains (p))
            return;
        attachment.setValueAsCompleteGesture ((float) next);
        if (onEdit)
            onEdit();
    }

    void SlideSwitch::mouseUp (const juce::MouseEvent& e)
    {
        if (! e.mouseWasDraggedSinceMouseDown())
            clickAt (e.position);
    }

    void SlideSwitch::paint (juce::Graphics& g)
    {
        const auto& img = images[(size_t) juce::jlimit (0, 1, state)];
        if (img.isValid())
        {
            g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
            g.drawImage (img, housing, juce::RectanglePlacement::stretchToFit);
        }
    }

    // ================================================================== FILTER MODE

    CellSwitch::CellSwitch (juce::RangedAudioParameter& p, juce::Image lit, juce::Rectangle<float> area, float cellPitch,
                            juce::Rectangle<float> h, std::array<juce::Rectangle<float>, 3> caps, std::function<void()> edited)
        : attachment (p, [this] (float v) { state = juce::roundToInt (v); repaint(); }),
          image (std::move (lit)), litArea (area), housing (h), pitch (cellPitch), captions (caps), onEdit (std::move (edited))
    {
        setTitle (p.getName (64));
        attachment.sendInitialUpdate();
    }

    int CellSwitch::cellAt (juce::Point<float> p) const
    {
        for (int i = 0; i < 3; ++i)
            if (captions[(size_t) i].contains (p))
                return i;
        if (housing.contains (p))
            return juce::jlimit (0, 2, (int) ((p.y - housing.getY()) / (housing.getHeight() / 3.0f)));
        return -1;
    }

    void CellSwitch::mouseUp (const juce::MouseEvent& e)
    {
        if (e.mouseWasDraggedSinceMouseDown())
            return;
        const int cell = cellAt (e.position);
        if (cell < 0)
            return;
        attachment.setValueAsCompleteGesture ((float) cell);
        if (onEdit)
            onEdit();
    }

    void CellSwitch::paint (juce::Graphics& g)
    {
        if (! image.isValid())
            return;
        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        g.drawImage (image, litAreaFor (state), juce::RectanglePlacement::stretchToFit);
    }

    // ================================================================== TAP

    TapButton::TapButton (LampSprite s, juce::Rectangle<float> b) : sprite (std::move (s)), body (b)
    {
        setTitle ("Tap");
    }

    bool TapButton::lampOnAt (double nowMs, double periodMs) noexcept
    {
        if (! (periodMs > 0.0))
            return false;
        return std::fmod (nowMs, periodMs) < std::min (flashMs, periodMs * 0.5);
    }

    void TapButton::setLit (bool on)
    {
        if (on != lit)
        {
            lit = on;
            repaint();
        }
    }

    void TapButton::mouseDown (const juce::MouseEvent&)
    {
        if (onTap)
            onTap (juce::Time::getMillisecondCounterHiRes() * 0.001);
    }

    void TapButton::paint (juce::Graphics& g)
    {
        if (lit && sprite.image.isValid())
        {
            g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
            g.drawImage (sprite.image, sprite.area, juce::RectanglePlacement::stretchToFit);
        }
    }

    // ================================================================== BPM

    namespace
    {
        // segments a b c d e f g as bits 0..6
        constexpr int segmentsFor[] = { 0x3f, 0x06, 0x5b, 0x4f, 0x66, 0x6d, 0x7d, 0x07, 0x7f, 0x6f };
    }

    BpmDisplay::BpmDisplay (juce::Rectangle<float> gl, std::array<float, 4> cx, float cw, float top, float h, float sl,
                            juce::Rectangle<float> u, juce::Rectangle<float> d, float s)
        : glass (gl), up (u), down (d), cellX (cx), cellWidth (cw), digitTop (top), digitHeight (h), slant (sl), scale (s)
    {
        setTitle ("BPM");
    }

    juce::String BpmDisplay::textFor (double bpm)
    {
        const double r = std::round (bpm * 10.0) / 10.0;
        if (std::abs (r - std::round (r)) < 0.05)
            return juce::String (juce::roundToInt (r));
        return juce::String (r, 1);
    }

    void BpmDisplay::setReading (double bpm, bool dim, bool off)
    {
        const auto t = textFor (bpm);
        if (t != text || dim != dimmed || off != blinkOff)
        {
            text = t;
            dimmed = dim;
            blinkOff = off;
            repaint();
        }
    }

    void BpmDisplay::mouseDown (const juce::MouseEvent& e)
    {
        if (! onStep)
            return;
        if (up.contains (e.position))
            onStep (1);
        else if (down.contains (e.position))
            onStep (-1);
    }

    void BpmDisplay::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
    {
        if (onStep && std::abs (w.deltaY) > 0.0f)
            onStep (w.deltaY > 0.0f ? 1 : -1);
    }

    void BpmDisplay::paintDigit (juce::Graphics& g, juce::Rectangle<float> c, int segs, bool dp, juce::Colour colour)
    {
        // an italic seven-segment digit like the artwork's: the cell is
        // sheared, the top leaning right by `slant`
        const float w = c.getWidth(), h = c.getHeight();
        const float t = w * 0.2f, gap = t * 0.18f;
        const float x0 = c.getX(), y0 = c.getY();
        const float mid = y0 + h * 0.5f;
        auto horiz = [&] (float y) { return juce::Rectangle<float> (x0 + t * 0.5f + gap, y - t * 0.5f, w - t - 2.0f * gap, t); };
        auto vert = [&] (float x, float ya, float yb) { return juce::Rectangle<float> (x - t * 0.5f, ya + gap, t, yb - ya - 2.0f * gap); };
        const juce::Rectangle<float> seg[7] = {
            horiz (y0 + t * 0.5f),                       // a
            vert (x0 + w - t * 0.5f, y0 + t * 0.5f, mid), // b
            vert (x0 + w - t * 0.5f, mid, y0 + h - t * 0.5f), // c
            horiz (y0 + h - t * 0.5f),                   // d
            vert (x0 + t * 0.5f, mid, y0 + h - t * 0.5f), // e
            vert (x0 + t * 0.5f, y0 + t * 0.5f, mid),     // f
            horiz (mid)                                   // g
        };
        juce::Path p;
        for (int i = 0; i < 7; ++i)
            if (segs & (1 << i))
                p.addRoundedRectangle (seg[i], t * 0.45f);
        if (dp)
            p.addEllipse (juce::Rectangle<float> (t * 1.1f, t * 1.1f).withCentre ({ x0 + w + t * 0.9f, y0 + h - t * 0.55f }));
        const float k = -slant / h;
        p.applyTransform (juce::AffineTransform (1.0f, k, -k * (y0 + h), 0.0f, 1.0f, 0.0f));
        g.setColour (colour);
        g.fillPath (p);
    }

    void BpmDisplay::paint (juce::Graphics& g)
    {
        const float s = scale;
        // unlit segments show faintly through the glass, as in the artwork
        for (float x : cellX)
            paintDigit (g, { x * s, digitTop * s, cellWidth * s, digitHeight * s }, 0x7f, false, ledGlow.withAlpha (0.07f));

        if (blinkOff || text.isEmpty())
            return;

        // right-aligned digits; a decimal point belongs to the digit before it
        struct Cell { int segs; bool dp; };
        std::vector<Cell> cells;
        for (auto ch : text)
        {
            if (ch == '.')
            {
                if (! cells.empty())
                    cells.back().dp = true;
                continue;
            }
            if (ch >= '0' && ch <= '9')
                cells.push_back ({ segmentsFor[ch - '0'], false });
        }
        while (cells.size() > cellX.size())
            cells.erase (cells.begin());

        const float alpha = dimmed ? 0.45f : 1.0f;
        const size_t first = cellX.size() - cells.size();
        for (int pass = 0; pass < 3; ++pass)
        {
            juce::Graphics::ScopedSaveState state (g);
            const auto colour = pass == 0 ? ledGlow.withAlpha (0.16f * alpha)
                              : pass == 1 ? ledGlow.withAlpha (0.85f * alpha)
                                          : ledCore.withAlpha (alpha);
            const float grow = pass == 0 ? 2.2f * s : pass == 1 ? 0.6f * s : -0.6f * s;
            for (size_t i = 0; i < cells.size(); ++i)
            {
                const auto cell = juce::Rectangle<float> (cellX[first + i] * s, digitTop * s, cellWidth * s, digitHeight * s)
                                      .expanded (grow, grow);
                paintDigit (g, cell, cells[i].segs, cells[i].dp, colour);
            }
        }
        juce::ignoreUnused (glass);
    }

    // ================================================================== tape bar

    TapeBar::TapeBar (juce::RangedAudioParameter& p, std::vector<float> seg, std::array<float, 3> t, float tp, float bt,
                      std::function<void()> edited)
        : attachment (p, [this] (float v) { state = juce::roundToInt (v); repaint(); }),
          segments (std::move (seg)), ticks (t), top (tp), bottom (bt), onEdit (std::move (edited))
    {
        setTitle (p.getName (64));
        attachment.sendInitialUpdate();
    }

    int TapeBar::litSegmentsFor (int condition) const
    {
        const float end = ticks[(size_t) juce::jlimit (0, 2, condition)];
        int n = 0;
        for (size_t i = 1; i < segments.size(); ++i)
            if (segments[i] <= end + 0.5f)
                n = (int) i;
        return n;
    }

    int TapeBar::conditionAt (float x) const
    {
        int best = 0;
        for (int i = 1; i < 3; ++i)
            if (std::abs (x - ticks[(size_t) i]) < std::abs (x - ticks[(size_t) best]))
                best = i;
        return best;
    }

    void TapeBar::mouseUp (const juce::MouseEvent& e)
    {
        if (e.mouseWasDraggedSinceMouseDown())
            return;
        attachment.setValueAsCompleteGesture ((float) conditionAt (e.position.x));
        if (onEdit)
            onEdit();
    }

    void TapeBar::paint (juce::Graphics& g)
    {
        const int lit = litSegmentsFor (state);
        if (lit <= 0)
            return;
        const float h = bottom - top;
        const auto bar = juce::Rectangle<float> (segments.front(), top, segments[(size_t) lit] - segments.front(), h);
        // the lit part's light on the bar's window
        for (int i = 3; i >= 1; --i)
        {
            g.setColour (ledGlow.withAlpha (0.06f * (float) (4 - i)));
            g.fillRoundedRectangle (bar.expanded (h * 0.35f * (float) i, h * 0.45f * (float) i), h * 0.5f * (float) i);
        }
        for (int k = 0; k < lit; ++k)
        {
            const auto r = juce::Rectangle<float> (segments[(size_t) k], top, segments[(size_t) k + 1] - segments[(size_t) k], h)
                               .reduced (h * 0.12f, h * 0.1f);
            // measured on the artwork's lit segments: amber edges, hot middle
            juce::ColourGradient grad (juce::Colour (0xffd17a1c), r.getX(), r.getY(), juce::Colour (0xffc86a12), r.getX(), r.getBottom(), false);
            grad.addColour (0.5, juce::Colour (0xffffc45a));
            g.setGradientFill (grad);
            g.fillRect (r);
        }
    }

    // ================================================================== editor

    juce::var EchoEditor::loadUiMap()
    {
        return juce::JSON::parse (juce::String::fromUTF8 (ECHOSkin::UI_MAP_json, ECHOSkin::UI_MAP_jsonSize));
    }

    juce::var EchoEditor::loadSpriteMap()
    {
        return juce::JSON::parse (juce::String::fromUTF8 (ECHOSkin::sprites_json, ECHOSkin::sprites_jsonSize));
    }

    float EchoEditor::vuDb (float rms) noexcept
    {
        return 20.0f * std::log10 (std::max (rms, 1.0e-9f) * 1.41421356f) - params::alignmentDbfs;
    }

    juce::String EchoEditor::labelText (const juce::String& id, float v, const juce::String& stepName)
    {
        if (stepName.isNotEmpty())
            return stepName.toUpperCase();
        const int n = juce::roundToInt (v);
        if ((id == params::repeatRate || id == params::lpf) && v >= 1000.0f)
            return juce::String (juce::roundToInt (v / 1000.0f)) + "K";
        if (id == params::bass || id == params::treble || id == params::output)
            return n > 0 ? "+" + juce::String (n) : juce::String (n);
        return juce::String (n);
    }

    juce::Rectangle<float> EchoEditor::spriteRect (const juce::String& name) const
    {
        return rectFrom (spriteMap["sprites"][juce::Identifier (name)]);
    }

    juce::Image EchoEditor::spriteImage (const juce::String& name) const
    {
        int size = 0;
        juce::Image image;
        if (auto* data = ECHOSkin::getNamedResource ((name + "_png").toRawUTF8(), size))
            image = juce::ImageCache::getFromMemory (data, size);
        jassert (image.isValid());
        return image;
    }

    void EchoEditor::place (juce::Component& c, juce::Rectangle<float> refBounds)
    {
        addToPanel (c);
        placed.emplace_back (&c, refBounds);
    }

    EchoEditor::EchoEditor (EchoProcessor& p)
        : ui::TleetEditorBase (p, baseWidth, toolbarHeight + panelHeight),
          proc (p),
          uiMap (loadUiMap()),
          spriteMap (loadSpriteMap()),
          plate (juce::ImageCache::getFromMemory (ECHOSkin::plate_png, ECHOSkin::plate_pngSize))
    {
        const float s = skinScale;
        auto onEdit = [this] { proc.commitHistory(); };

        const std::pair<const char*, const char*> tips[] = {
            { params::mode,         "MODE: which playback heads make the echo (1, 2, 3 and their combinations); REVERB plays "
                                    "the spring alone. The lamp of the selected position glows brighter while its heads play signal. "
                                    "REVERB VOLUME adds the spring in every position" },
            { params::repeatRate,   "REPEAT RATE: tape speed, as the time of head 1 (heads 2 and 3 follow at their fixed spacing). "
                                    "Turning it glides the tape speed, so the echoes bend in pitch as on the machine. "
                                    "Not used in SYNC (dimmed): the note value sets the time" },
            { params::intensity,    "INTENSITY: how much of the echo goes back to the record head. High settings run away into "
                                    "self-oscillation, as on the machine" },
            { params::echoVolume,   "ECHO VOLUME: level of the tape echo" },
            { params::reverbVolume, "REVERB VOLUME: level of the spring reverb" },
            { params::bass,         "BASS: low shelf on the echo and reverb" },
            { params::treble,       "TREBLE: high shelf on the echo and reverb" },
            { params::drive,        "DRIVE: level into the record amplifier and the tape" },
            { params::age,          "AGE: how worn the tape and the heads are (less top end, more loss on each repeat)" },
            { params::wow,          "WOW: slow tape-speed wobble" },
            { params::flutter,      "FLUTTER: fast tape-speed wobble" },
            { params::hpf,          "HPF: low cut, where FILTER MODE puts it" },
            { params::lpf,          "LPF: high cut, where FILTER MODE puts it" },
            { params::stereo,       "STEREO: width of the echoes across the stereo field" },
            { params::duck,         "DUCK: lowers the echo while the input plays" },
            { params::mix,          "MIX: dry / effect balance" },
            { params::noise,        "NOISE: tape hiss and hum of the machine" },
            { params::output,       "OUTPUT: output level" },
        };

        for (auto& [id, tip] : tips)
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
            knob->setTooltip (tip);
            knob->onEditFinished = onEdit;
            addToPanel (*knob);

            KnobEntry e;
            e.id = id;
            e.hitbox = hitbox;
            e.labels = makeScaleLabels (*c, *param, [id = juce::String (id)] (float v, const juce::String& step)
                                        { return labelText (id, v, step); });
            e.dots = makeStepDots (*c, *param);
            e.capHeight = (float) (*c)["scaleLabels"]["capHeight"];
            e.ink = (*c)["ink"].toString() == "top" ? topInk : bottomInk;
            e.knob = std::move (knob);
            knobs.push_back (std::move (e));
        }

        // ---- MODE lamp
        {
            const auto* c = findControl (uiMap, params::mode);
            const auto& lp = (*c)["lamps"];
            const auto image = spriteImage ("mode_lamp");
            const auto lampSize = rectFrom (spriteMap["sprites"]["mode_lamp"]).getWidth();
            std::vector<juce::Point<float>> refPositions;
            for (auto& v : *lp["positions"].getArray())
                refPositions.push_back (pointFrom (v));
            juce::Rectangle<float> bounds;
            for (auto& q : refPositions)
                bounds = bounds.isEmpty() ? juce::Rectangle<float> (lampSize, lampSize).withCentre (q)
                                          : bounds.getUnion (juce::Rectangle<float> (lampSize, lampSize).withCentre (q));
            std::vector<juce::Point<float>> local;
            for (auto& q : refPositions)
                local.push_back ((q - bounds.getPosition()) * s);
            modeLamp = std::make_unique<ModeLamp> (image, pointFrom (spriteMap["modeLampCentre"]), std::move (local), s);
            place (*modeLamp, bounds);
        }

        // ---- lamp buttons: NOTE DIVISION, FEEL, CHARACTER
        for (const char* id : { params::noteDivision, params::feel, params::character })
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
                const auto glow = spriteRect (name);
                const auto bounds = hitbox.getUnion (glow);
                auto lamp = std::make_unique<LampButton> (b["label"].toString(),
                                                          LampSprite { spriteImage (name), local (glow, bounds, s) },
                                                          local (hitbox, bounds, s));
                addToPanel (*lamp);
                e.bounds.push_back (bounds);
                e.group->add (std::move (lamp));
            }
            groups.push_back (std::move (e));
        }
        {
            auto& nd = getLampGroup (params::noteDivision);
            for (int i = 0; i < nd.getNumLamps(); ++i)
                nd.getLamp (i).setTooltip ("NOTE DIVISION: in SYNC, head 1 repeats every " + params::noteDivisionNames[i]
                                           + " note (with FEEL); heads 2 and 3 follow at their spacing on the tape");
            auto& feel = getLampGroup (params::feel);
            const char* feelTips[] = { "FEEL: straight note values", "FEEL: dotted note values (x 1.5)", "FEEL: triplet note values (x 2/3)" };
            for (int i = 0; i < 3; ++i)
                feel.getLamp (i).setTooltip (feelTips[i]);
            auto& ch = getLampGroup (params::character);
            const char* chTips[] = { "CHARACTER: Clean, the record and playback amplifiers with little colour",
                                     "CHARACTER: Original, the colour of the machine",
                                     "CHARACTER: Burn, hotter tape and amplifiers" };
            for (int i = 0; i < 3; ++i)
                ch.getLamp (i).setTooltip (chTips[i]);
        }

        // ---- slide switches: TIME MODE, HOST BPM
        for (const char* id : { params::timeMode, params::bpmSource })
        {
            const auto* c = findControl (uiMap, id);
            jassert (c != nullptr);
            const auto bounds = rectFrom ((*c)["hitbox"]);
            const auto& caps = (*c)["captions"];
            auto sw = std::make_unique<SlideSwitch> (*p.apvts.getParameter (id),
                                                     std::array<juce::Image, 2> { spriteImage (juce::String (id) + "_0"), spriteImage (juce::String (id) + "_1") },
                                                     local (rectFrom ((*c)["housing"]), bounds, s),
                                                     std::array<juce::Rectangle<float>, 2> { local (rectFrom (caps[0]), bounds, s), local (rectFrom (caps[1]), bounds, s) },
                                                     onEdit);
            place (*sw, bounds);
            slides[id] = std::move (sw);
        }
        slides[params::timeMode]->setTooltip ("TIME MODE: FREE, REPEAT RATE sets the time; SYNC, the note value at the BPM sets it");
        slides[params::bpmSource]->setTooltip ("HOST BPM: HOST follows the host's tempo (MANUAL BPM when the host sends none); "
                                               "MANUAL uses the BPM display, set with the arrows or TAP");

        // ---- FILTER MODE
        {
            const auto* c = findControl (uiMap, params::filterMode);
            const auto bounds = rectFrom ((*c)["hitbox"]);
            const auto& caps = (*c)["captions"];
            filterMode = std::make_unique<CellSwitch> (*p.apvts.getParameter (params::filterMode), spriteImage ("filterMode_lit"),
                                                       local (spriteRect ("filterMode_lit"), bounds, s), (float) (*c)["cellPitch"] * s,
                                                       local (rectFrom ((*c)["housing"]), bounds, s),
                                                       std::array<juce::Rectangle<float>, 3> { local (rectFrom (caps[0]), bounds, s),
                                                                                               local (rectFrom (caps[1]), bounds, s),
                                                                                               local (rectFrom (caps[2]), bounds, s) },
                                                       onEdit);
            filterMode->setTooltip ("FILTER MODE: where HPF and LPF act. PRE: on the input to the tape; LOOP: inside the "
                                    "feedback, so every repeat is filtered again; POST: on the echo output");
            place (*filterMode, bounds);
        }

        // ---- TAP
        {
            const auto* c = findControl (uiMap, "tap");
            const auto body = rectFrom ((*c)["hitbox"]);
            const auto lamp = spriteRect ("tap_lamp");
            const auto bounds = body.getUnion (lamp);
            tap = std::make_unique<TapButton> (LampSprite { spriteImage ("tap_lamp"), local (lamp, bounds, s) }, local (body, bounds, s));
            tap->onTap = [this] (double t) { proc.tap (t); };
            tap->setTooltip ("TAP: tap the tempo. In SYNC it sets MANUAL BPM (and switches to MANUAL); in FREE it sets "
                             "REPEAT RATE. The lamp flashes at head 1's real repeat time");
            place (*tap, bounds);
        }

        // ---- BPM display
        {
            const auto* c = findControl (uiMap, "bpmDisplay");
            const auto glass = rectFrom ((*c)["glass"]);
            const auto upR = rectFrom ((*c)["up"]), downR = rectFrom ((*c)["down"]);
            const auto bounds = glass.getUnion (upR).getUnion (downR);
            std::array<float, 4> cells {};
            for (int i = 0; i < 4; ++i)
                cells[(size_t) i] = (float) (*c)["cells"][i] - bounds.getX();
            bpm = std::make_unique<BpmDisplay> (local (glass, bounds, s), cells, (float) (*c)["cellWidth"],
                                                (float) (*c)["digitTop"] - bounds.getY(), (float) (*c)["digitHeight"],
                                                (float) (*c)["slant"], local (upR, bounds, s), local (downR, bounds, s), s);
            bpm->onStep = [this] (int step)
            {
                auto* manual = proc.apvts.getParameter (params::manualBpm);
                auto* source = proc.apvts.getParameter (params::bpmSource);
                const float from = (float) juce::roundToInt (proc.getEffectiveBpm());
                const float to = juce::jlimit (params::bpmMin, params::bpmMax, from + (float) step);
                manual->beginChangeGesture();
                manual->setValueNotifyingHost (manual->convertTo0to1 (to));
                manual->endChangeGesture();
                if (choiceIndex (*source) != (int) params::BpmSource::manual)
                {
                    source->beginChangeGesture();
                    source->setValueNotifyingHost (source->convertTo0to1 ((float) params::BpmSource::manual));
                    source->endChangeGesture();
                }
                proc.commitHistory();
                refresh();
            };
            bpm->setTooltip ("BPM: the tempo SYNC uses (the host's, or MANUAL BPM). The arrows and the mouse wheel step "
                             "MANUAL BPM and switch to MANUAL. Dimmed in FREE; blinks when the note is outside the tape's range");
            place (*bpm, bounds);
        }

        // ---- TAPE CONDITION
        {
            const auto* c = findControl (uiMap, params::tapeCondition);
            const auto bounds = rectFrom ((*c)["hitbox"]);
            const auto bar = rectFrom ((*c)["bar"]);
            std::vector<float> seg;
            for (auto& v : *(*c)["segments"].getArray())
                seg.push_back (((float) v - bounds.getX()) * s);
            std::array<float, 3> ticks {};
            for (int i = 0; i < 3; ++i)
                ticks[(size_t) i] = ((float) (*c)["ticks"][i] - bounds.getX()) * s;
            tapeBar = std::make_unique<TapeBar> (*p.apvts.getParameter (params::tapeCondition), seg, ticks,
                                                 (bar.getY() - bounds.getY()) * s, (bar.getBottom() - bounds.getY()) * s, onEdit);
            tapeBar->setTooltip ("TAPE CONDITION: Clean, Worn (dropouts and more loss), Magnetic dust (more dropouts and "
                                 "crackle). The bar lights up to the condition in use; click a mark to choose");
            place (*tapeBar, bounds);
        }

        // ---- VU meters: IN (plugin input), OUT (plugin output)
        for (int i = 0; i < 2; ++i)
        {
            const auto* c = findControl (uiMap, i == 0 ? "vuIn" : "vuOut");
            jassert (c != nullptr);
            const auto bounds = rectFrom ((*c)["hitbox"]);
            const auto origin = bounds.getPosition();
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
            vus[(size_t) i] = std::make_unique<ui::SkinVU> (std::move (g));
            auto& meters = i == 0 ? proc.inputMeters : proc.outputMeters;
            vus[(size_t) i]->setSupplier ([&meters]
            {
                const float l = meters[0].takeRms(), r = meters[1].takeRms();
                if (l < 0.0f && r < 0.0f)
                    return std::nanf ("");
                const float rms = std::sqrt (0.5f * (std::max (l, 0.0f) * std::max (l, 0.0f) + std::max (r, 0.0f) * std::max (r, 0.0f)));
                return vuDb (rms);
            });
            vus[(size_t) i]->setRestValue (-40.0f);
            vus[(size_t) i]->setTooltip (i == 0 ? "IN: input level (0 VU = a sine at -18 dBFS)"
                                                : "OUT: output level (0 VU = a sine at -18 dBFS)");
            place (*vus[(size_t) i], bounds);
        }

        refresh();
        finishSetup();
        startTimerHz (30);
    }

    EchoEditor::~EchoEditor()
    {
        stopTimer();
    }

    void EchoEditor::refresh (float seconds)
    {
        auto* timeMode = proc.apvts.getParameter (params::timeMode);
        const bool sync = choiceIndex (*timeMode) == (int) params::TimeMode::sync;

        // REPEAT RATE does nothing in SYNC: its pointer and scale are dimmed
        if (sync != repeatDimmed)
        {
            repeatDimmed = sync;
            getKnob (params::repeatRate).setAlpha (sync ? 0.4f : 1.0f);
            repaint();
        }

        // MODE lamp: the selected position, brightness from the heads it plays
        if (modeLamp)
        {
            float peak = 0.0f;
            for (int h = 0; h < 3; ++h)
            {
                const float a = proc.takeHeadActivity (h); // read every head, so none piles up
                if (proc.isHeadActive (h))
                    peak = std::max (peak, a);
            }
            modeLamp->show (choiceIndex (*proc.apvts.getParameter (params::mode)), peak, seconds);
        }

        const double now = juce::Time::getMillisecondCounterHiRes();
        if (tap)
            tap->setLit (TapButton::lampOnAt (now, proc.getHeadDelayMs (0)));
        if (bpm)
            bpm->setReading (proc.getEffectiveBpm(), ! sync, sync && proc.isSyncTimeClamped() && std::fmod (now, 500.0) >= 250.0);
    }

    ui::Knob& EchoEditor::getKnob (const juce::String& parameterId)
    {
        for (auto& k : knobs)
            if (k.id == parameterId)
                return *k.knob;
        jassertfalse;
        return *knobs.front().knob;
    }

    LampGroup& EchoEditor::getLampGroup (const juce::String& parameterId)
    {
        for (auto& g : groups)
            if (g.id == parameterId)
                return *g.group;
        jassertfalse;
        return *groups.front().group;
    }

    SlideSwitch& EchoEditor::getSlideSwitch (const juce::String& parameterId)
    {
        jassert (slides.count (parameterId) == 1);
        return *slides[parameterId];
    }

    const std::vector<ScaleLabel>& EchoEditor::getScaleLabels (const juce::String& parameterId) const
    {
        for (auto& k : knobs)
            if (k.id == parameterId)
                return k.labels;
        static const std::vector<ScaleLabel> none;
        return none;
    }

    const std::vector<StepDot>& EchoEditor::getStepDots (const juce::String& parameterId) const
    {
        for (auto& k : knobs)
            if (k.id == parameterId)
                return k.dots;
        static const std::vector<StepDot> none;
        return none;
    }

    juce::Rectangle<int> EchoEditor::refToPanel (juce::Rectangle<float> r) const
    {
        return (r * skinScale).translated ((float) panelArea.getX(), (float) panelArea.getY()).toNearestInt();
    }

    void EchoEditor::layoutPanel (juce::Rectangle<int> area)
    {
        panelArea = area;
        for (auto& k : knobs)
            k.knob->setBounds (refToPanel (k.hitbox));
        for (auto& g : groups)
            for (int i = 0; i < g.group->getNumLamps(); ++i)
                g.group->getLamp (i).setBounds (refToPanel (g.bounds[(size_t) i]));
        for (auto& [c, r] : placed)
            c->setBounds (refToPanel (r));
    }

    void EchoEditor::paintPanel (juce::Graphics& g, juce::Rectangle<int> area)
    {
        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        g.drawImage (plate, area.toFloat(), juce::RectanglePlacement::stretchToFit);

        const auto origin = area.getPosition().toFloat();
        for (auto& k : knobs)
        {
            const auto ink = (k.id == params::repeatRate && repeatDimmed) ? k.ink.withMultipliedAlpha (0.4f) : k.ink;
            paintScale (g, k.dots, k.labels, k.capHeight, skinScale, origin, ink, ink);
        }
    }
}
