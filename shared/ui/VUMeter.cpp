#include "VUMeter.h"
#include "TleetLookAndFeel.h"

namespace tleet::ui
{
    static constexpr float minVu = -20.0f, maxVu = 3.0f;
    static constexpr float sweep = 0.78f; // half-angle of the scale in radians

    VUMeter::VUMeter()
    {
        ballistics.reset (minVu);
        setTitle ("VU meter");
        startTimerHz (60);
    }

    VUMeter::~VUMeter() { stopTimer(); }

    void VUMeter::timerCallback()
    {
        const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
        const float dt = lastTime > 0.0 ? (float) juce::jlimit (0.0, 0.1, now - lastTime) : 1.0f / 60.0f;
        lastTime = now;

        if (supplier)
        {
            const float r = supplier();
            if (! std::isnan (r))
                target = juce::jlimit (minVu - 2.0f, maxVu + 1.0f, r);
        }

        const float before = ballistics.position();
        ballistics.process (target, dt);
        if (std::abs (before - ballistics.position()) > 0.005f)
            repaint();
    }

    // The VU scale is linear in voltage, not in dB: position = 10^(vu/20).
    float VUMeter::vuToAngle (float vu) const noexcept
    {
        const float lo = std::pow (10.0f, minVu / 20.0f), hi = std::pow (10.0f, maxVu / 20.0f);
        const float pos = (std::pow (10.0f, juce::jlimit (minVu - 2.0f, maxVu + 1.0f, vu) / 20.0f) - lo) / (hi - lo);
        return -sweep + juce::jlimit (-0.03f, 1.03f, pos) * 2.0f * sweep;
    }

    void VUMeter::paint (juce::Graphics& g)
    {
        auto bounds = getLocalBounds().toFloat();

        // bezel
        g.setColour (juce::Colours::black);
        g.fillRoundedRectangle (bounds, 8.0f);
        auto face = bounds.reduced (bounds.getWidth() * 0.035f);
        juce::ColourGradient glow (colours::meterFace, face.getCentreX(), face.getBottom(),
                                   colours::meterFace.darker (0.45f), face.getX(), face.getY(), true);
        g.setGradientFill (glow);
        g.fillRoundedRectangle (face, 5.0f);

        const auto pivot = juce::Point<float> (face.getCentreX(), face.getBottom() + face.getHeight() * 0.18f);
        const float rScale = face.getHeight() * 0.88f;

        // scale arc: black up to 0 VU, red above
        auto arc = [&] (float fromVu, float toVu, juce::Colour c, float w)
        {
            juce::Path p;
            p.addCentredArc (pivot.x, pivot.y, rScale, rScale, 0.0f, vuToAngle (fromVu), vuToAngle (toVu), true);
            g.setColour (c);
            g.strokePath (p, juce::PathStrokeType (w));
        };
        arc (minVu, 0.0f, colours::meterInk, 2.0f);
        arc (0.0f, maxVu, colours::warning, 5.0f);

        const float marks[] = { -20, -10, -7, -5, -3, -2, -1, 0, 1, 2, 3 };
        g.setFont (TleetLookAndFeel::labelFont (face.getHeight() * 0.085f));
        for (float m : marks)
        {
            const float a = vuToAngle (m);
            const auto p1 = pivot.getPointOnCircumference (rScale, a);
            const auto p2 = pivot.getPointOnCircumference (rScale + face.getHeight() * 0.06f, a);
            g.setColour (m > 0 ? colours::warning : colours::meterInk);
            g.drawLine ({ p1, p2 }, 1.5f);

            const auto t = pivot.getPointOnCircumference (rScale + face.getHeight() * 0.14f, a);
            const juce::String txt = m > 0 ? "+" + juce::String ((int) m) : juce::String ((int) std::abs (m));
            g.drawText (txt, juce::Rectangle<float> (40.0f, 16.0f).withCentre (t), juce::Justification::centred);
        }

        g.setColour (colours::meterInk);
        g.setFont (TleetLookAndFeel::labelFont (face.getHeight() * 0.13f));
        g.drawText (caption, face.withTrimmedTop (face.getHeight() * 0.5f).withTrimmedBottom (face.getHeight() * 0.12f),
                    juce::Justification::centred);

        // needle (clipped to the face)
        {
            juce::Graphics::ScopedSaveState s (g);
            g.reduceClipRegion (face.toNearestInt());
            const float a = vuToAngle (ballistics.position());
            const auto tip = pivot.getPointOnCircumference (rScale + face.getHeight() * 0.04f, a);
            g.setColour (juce::Colours::black.withAlpha (0.25f));
            g.drawLine ({ pivot.translated (2.0f, 2.0f), tip.translated (2.0f, 2.0f) }, 2.0f);
            g.setColour (colours::meterInk);
            g.drawLine ({ pivot, tip }, 1.8f);
        }

        // lower shade hiding the pivot
        g.setColour (colours::panel);
        g.fillRect (face.withTop (face.getBottom() - face.getHeight() * 0.1f));
        g.setColour (colours::edge);
        g.drawRoundedRectangle (face, 5.0f, 1.0f);
    }

    void VUMeter::mouseUp (const juce::MouseEvent& e)
    {
        if (onClick && ! e.mods.isPopupMenu())
            onClick();
    }
}
