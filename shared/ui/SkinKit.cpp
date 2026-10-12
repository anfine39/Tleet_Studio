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
}
