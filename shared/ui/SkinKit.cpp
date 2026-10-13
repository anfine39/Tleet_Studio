#include "SkinKit.h"
#include "TleetLookAndFeel.h"

namespace tleet::ui::skin
{
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

    float angleFor (const juce::var& c, float normalised)
    {
        const float t0 = (float) c["travelDegrees"][0], t1 = (float) c["travelDegrees"][1];
        return t0 + juce::jlimit (0.0f, 1.0f, normalised) * (t1 - t0);
    }

    juce::Font scaleFont (float capHeight)
    {
        return TleetLookAndFeel::labelFont (capHeight / 0.72f, false).withHorizontalScale (0.84f);
    }

    std::vector<ScaleLabel> makeScaleLabels (const juce::var& c, juce::RangedAudioParameter& param, const LabelTextFn& labelText)
    {
        std::vector<ScaleLabel> out;
        if (! c.hasProperty ("scaleLabels"))
            return out;

        const auto& sl = c["scaleLabels"];
        const auto centre = pointFrom (c["scaleCentre"]);
        const float radius = (float) c["dotRadius"] + (float) sl["gap"];
        const float capHeight = (float) sl["capHeight"];
        const auto font = scaleFont (capHeight);

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
            const auto text = labelText (value, stepName);
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

    std::vector<StepDot> makeStepDots (const juce::var& c, juce::RangedAudioParameter& param)
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

    void paintScale (juce::Graphics& g, const std::vector<StepDot>& dots, const std::vector<ScaleLabel>& labels,
                     float capHeight, float s, juce::Point<float> origin, juce::Colour dotInk, juce::Colour labelInk)
    {
        for (auto& d : dots)
        {
            const auto c = d.centre * s + origin;
            const float r = d.radius * s;
            g.setColour (dotInk.withAlpha (0.25f));
            g.fillEllipse (juce::Rectangle<float> (r * 3.0f, r * 3.0f).withCentre (c));
            g.setColour (dotInk);
            g.fillEllipse (juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (c));
        }

        if (labels.empty())
            return;
        g.setFont (scaleFont (capHeight * s));
        g.setColour (labelInk);
        for (auto& l : labels)
            g.drawText (l.text, (l.box * s) + origin, juce::Justification::centred, false);
    }

    // ================================================================== meter

    LedGainMeter::LedGainMeter (Geometry g) : geo (std::move (g))
    {
        if (geo.boundaries.size() < 2)
        {
            geo.boundaries.clear();
            for (int db = 0; db <= 20; ++db)
                geo.boundaries.push_back ((float) db);
        }
        std::sort (geo.scale.begin(), geo.scale.end());
        std::sort (geo.boundaries.begin(), geo.boundaries.end());
        setInterceptsMouseClicks (false, false);
        startTimerHz (30);
    }

    int LedGainMeter::litSegmentsFor (float grDb) const noexcept
    {
        int lit = 0;
        for (int k = 0; k < getNumSegments(); ++k)
            if (grDb >= 0.5f * (geo.boundaries[(size_t) k] + geo.boundaries[(size_t) k + 1]))
                lit = k + 1;
        return lit;
    }

    float LedGainMeter::xForDb (float db) const noexcept
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

    juce::Rectangle<float> LedGainMeter::getSegmentBounds (int k) const noexcept
    {
        const float x0 = xForDb (geo.boundaries[(size_t) k]) + geo.gap * 0.5f;
        const float x1 = xForDb (geo.boundaries[(size_t) k + 1]) - geo.gap * 0.5f;
        return { x0, geo.rowTop, x1 - x0, geo.rowBottom - geo.rowTop };
    }

    void LedGainMeter::refresh (float seconds)
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

    void LedGainMeter::paint (juce::Graphics& g)
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

        for (int k = 0; k < getNumSegments(); ++k)
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

    LedGainMeter::Geometry meterGeometry (const juce::var& c, juce::Rectangle<float> bounds, float s)
    {
        LedGainMeter::Geometry g;
        if (auto* props = c["scale"].getDynamicObject())
            for (auto& prop : props->getProperties())
                g.scale.emplace_back (prop.name.toString().getFloatValue(), ((float) prop.value - bounds.getX()) * s);
        if (auto* b = c["segmentBoundaries"].getArray())
            for (auto& v : *b)
                g.boundaries.push_back ((float) v);
        g.rowTop = ((float) c["segmentRow"][0] - bounds.getY()) * s;
        g.rowBottom = ((float) c["segmentRow"][1] - bounds.getY()) * s;
        g.gap = (float) c["segmentGap"] * s;
        return g;
    }

    // ============================================================= arc meter

    ArcLedMeter::ArcLedMeter (Geometry g) : geo (std::move (g))
    {
        setInterceptsMouseClicks (false, false);
        startTimerHz (30);
    }

    int ArcLedMeter::getNumLitCapableSegments() const noexcept
    {
        int n = 0;
        for (auto& s : geo.segments)
            n += s.decorative ? 0 : 1;
        return n;
    }

    int ArcLedMeter::litSegmentsFor (float grDb) const noexcept
    {
        int lit = 0, k = 0;
        for (auto& s : geo.segments)
        {
            if (s.decorative)
                continue;
            ++k;
            if (grDb >= 0.5f * (s.loDb + s.hiDb))
                lit = k;
        }
        return lit;
    }

    juce::Path ArcLedMeter::getSegmentPath (int index) const
    {
        const auto& s = geo.segments[(size_t) index];
        const float rMid = 0.5f * (geo.outerRadius + geo.innerRadius);
        const float halfGap = 0.5f * geo.gap / juce::jmax (1.0f, rMid);
        float a0 = juce::degreesToRadians (juce::jmin (s.startDeg, s.endDeg)) + halfGap;
        float a1 = juce::degreesToRadians (juce::jmax (s.startDeg, s.endDeg)) - halfGap;
        if (a1 < a0)
            std::swap (a0, a1);
        juce::Path p;
        const auto c = geo.centre;
        p.addCentredArc (c.x, c.y, geo.outerRadius, geo.outerRadius, 0.0f, a0, a1, true);
        p.addCentredArc (c.x, c.y, geo.innerRadius, geo.innerRadius, 0.0f, a1, a0, false);
        p.closeSubPath();
        return p;
    }

    void ArcLedMeter::refresh (float seconds)
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

    void ArcLedMeter::paint (juce::Graphics& g)
    {
        const int lit = getLitSegments();
        const float depth = geo.outerRadius - geo.innerRadius;

        // segment k (1-based among the lit-capable ones) is lit when k <= lit
        std::vector<bool> isLit (geo.segments.size(), false);
        {
            int k = 0;
            for (size_t i = 0; i < geo.segments.size(); ++i)
                if (! geo.segments[i].decorative)
                    isLit[i] = ++k <= lit;
        }

        // glow of the lit part on the window around the bar
        if (lit > 0)
        {
            juce::Path bar;
            for (size_t i = 0; i < geo.segments.size(); ++i)
                if (isLit[i])
                    bar.addPath (getSegmentPath ((int) i));
            for (int i = 4; i >= 1; --i)
            {
                g.setColour (juce::Colour (0xffffa63a).withAlpha (0.05f * (float) (5 - i)));
                g.strokePath (bar, juce::PathStrokeType (depth * 0.55f * (float) i, juce::PathStrokeType::curved,
                                                         juce::PathStrokeType::rounded));
            }
        }

        for (size_t i = 0; i < geo.segments.size(); ++i)
        {
            const auto path = getSegmentPath ((int) i);
            const auto r = path.getBounds();
            if (isLit[i])
            {
                // measured on the artwork's lit segments: warm top, hot middle, amber lower edge
                juce::ColourGradient grad (juce::Colour (0xfffcd96a), r.getX(), r.getY(),
                                           juce::Colour (0xffcf7f1f), r.getX(), r.getBottom(), false);
                grad.addColour (0.45, juce::Colour (0xffffeea2));
                grad.addColour (0.85, juce::Colour (0xfffde68e));
                g.setGradientFill (grad);
                g.fillPath (path);
            }
            else
            {
                // the artwork's unlit segments: dull amber glass, lighter lower edge
                juce::ColourGradient grad (juce::Colour (0xff4b2a0e), r.getX(), r.getY(),
                                           juce::Colour (0xff7e4a17), r.getX(), r.getBottom(), false);
                grad.addColour (0.75, juce::Colour (0xff6b3f14));
                g.setGradientFill (grad);
                g.fillPath (path);
                g.setColour (juce::Colour (0xff120b05).withAlpha (0.6f));
                g.strokePath (path, juce::PathStrokeType (0.6f));
            }
        }
    }

    ArcLedMeter::Geometry arcMeterGeometry (const juce::var& c, juce::Rectangle<float> bounds, float s)
    {
        ArcLedMeter::Geometry g;
        const auto& arc = c["arc"];
        g.centre = (pointFrom (arc["centre"]) - bounds.getPosition()) * s;
        const float rail = (float) arc["railRadius"];
        g.outerRadius = (rail - (float) arc["segmentDepth"][0]) * s;
        g.innerRadius = (rail - (float) arc["segmentDepth"][1]) * s;
        g.gap = (float) c["segmentGap"] * s;

        // printed scale: gain reduction (dB) -> angle, piecewise linear
        std::vector<std::pair<float, float>> scale;
        if (auto* props = c["scaleDegrees"].getDynamicObject())
            for (auto& prop : props->getProperties())
                scale.emplace_back (prop.name.toString().getFloatValue(), (float) prop.value);
        std::sort (scale.begin(), scale.end());
        auto angleFor = [&scale] (float db)
        {
            if (scale.empty())
                return 0.0f;
            if (db <= scale.front().first)
                return scale.front().second;
            for (size_t i = 1; i < scale.size(); ++i)
                if (db <= scale[i].first)
                {
                    const float t = (db - scale[i - 1].first) / (scale[i].first - scale[i - 1].first);
                    return scale[i - 1].second + t * (scale[i].second - scale[i - 1].second);
                }
            return scale.back().second;
        };

        if (auto* b = c["segmentBoundaries"].getArray())
            for (int i = 0; i + 1 < b->size(); ++i)
            {
                const float lo = (float) (*b)[i], hi = (float) (*b)[i + 1];
                g.segments.push_back ({ angleFor (lo), angleFor (hi), lo, hi, false });
            }
        if (auto* u = c["unlitDegrees"].getArray())
            for (auto& seg : *u)
                g.segments.push_back ({ (float) seg[0], (float) seg[1], 0.0f, 0.0f, true });
        return g;
    }
}
