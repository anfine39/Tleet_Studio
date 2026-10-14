#include "FilterDisplay.h"
#include "ui/TleetLookAndFeel.h"

namespace tleet::dbl
{
    static const juce::Colour gridInk   { 0xff6e5a55 };
    static const juce::Colour curveInk  { 0xfff6d2d6 };
    static const juce::Colour glowInk   { 0xffe0899a };
    static const juce::Colour labelInk  { 0xffd4beb1 };

    FilterDisplay::FilterDisplay (Geometry g, std::function<double (double)> r, std::function<double()> key)
        : geo (g), response (std::move (r)), settingsKey (std::move (key))
    {
        setInterceptsMouseClicks (false, false);
        setTitle ("Filter response");
        rebuild();
        startTimerHz (30);
    }

    FilterDisplay::~FilterDisplay() { stopTimer(); }

    float FilterDisplay::xForHz (float hz) const noexcept
    {
        const float t = std::log (hz / geo.minHz) / std::log (geo.maxHz / geo.minHz);
        return geo.plot.getX() + t * geo.plot.getWidth();
    }

    float FilterDisplay::yForDb (float db) const noexcept
    {
        const float t = (geo.maxDb - db) / (geo.maxDb - geo.minDb);
        return geo.plot.getY() + t * geo.plot.getHeight();
    }

    void FilterDisplay::timerCallback()
    {
        const double k = settingsKey ? settingsKey() : 0.0;
        if (k != lastKey)
            rebuild();
    }

    void FilterDisplay::rebuild()
    {
        lastKey = settingsKey ? settingsKey() : 0.0;
        curve.clear();
        fill.clear();
        const int n = juce::jmax (64, juce::roundToInt (geo.plot.getWidth()));
        for (int i = 0; i <= n; ++i)
        {
            const float x = geo.plot.getX() + geo.plot.getWidth() * (float) i / (float) n;
            const float hz = geo.minHz * std::pow (geo.maxHz / geo.minHz, (float) i / (float) n);
            const float db = juce::jlimit (geo.minDb - 6.0f, geo.maxDb, (float) response (hz));
            const float y = juce::jmin (yForDb (db), geo.plot.getBottom() + 2.0f);
            if (i == 0) { curve.startNewSubPath (x, y); fill.startNewSubPath (x, geo.plot.getBottom()); }
            else curve.lineTo (x, y);
            fill.lineTo (x, y);
        }
        fill.lineTo (geo.plot.getRight(), geo.plot.getBottom());
        fill.closeSubPath();
        repaint();
    }

    void FilterDisplay::paint (juce::Graphics& g)
    {
        const float s = geo.scale;
        const auto& p = geo.plot;

        // grid: decades bright, 2/5 steps faint; 6 dB steps
        for (float decade : { 10.0f, 100.0f, 1000.0f, 10000.0f })
            for (float m : { 1.0f, 2.0f, 5.0f })
            {
                const float hz = decade * m;
                if (hz < geo.minHz || hz > geo.maxHz) continue;
                g.setColour (gridInk.withAlpha (m == 1.0f ? 0.65f : 0.3f));
                g.drawVerticalLine (juce::roundToInt (xForHz (hz)), p.getY(), p.getBottom());
            }
        for (float db = geo.maxDb; db >= geo.minDb - 0.01f; db -= 6.0f)
        {
            g.setColour (gridInk.withAlpha (std::abs (db) < 0.01f ? 0.7f : 0.3f));
            g.drawHorizontalLine (juce::roundToInt (yForDb (db)), p.getX(), p.getRight());
        }

        {
            juce::Graphics::ScopedSaveState state (g);
            g.reduceClipRegion (p.toNearestInt().expanded (0, 2));
            g.setGradientFill (juce::ColourGradient (glowInk.withAlpha (0.22f), 0.0f, p.getY(),
                                                     glowInk.withAlpha (0.03f), 0.0f, p.getBottom(), false));
            g.fillPath (fill);
            g.setColour (glowInk.withAlpha (0.35f));
            g.strokePath (curve, juce::PathStrokeType (4.0f * s, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setColour (curveInk);
            g.strokePath (curve, juce::PathStrokeType (1.6f * s, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        // frequency labels at their real positions (10K would collide with 20K on a log axis)
        g.setColour (labelInk);
        g.setFont (ui::TleetLookAndFeel::labelFont (geo.labelTextHeight / 0.72f * s, false).withHorizontalScale (0.86f));
        const std::pair<float, const char*> labels[] = { { 20.0f, "20" }, { 100.0f, "100" }, { 1000.0f, "1K" }, { 5000.0f, "5K" }, { 20000.0f, "20K" } };
        for (auto& [hz, text] : labels)
        {
            const float x = xForHz (hz);
            auto just = juce::Justification::centred;
            auto box = juce::Rectangle<float> (44.0f * s, geo.labelRow.getHeight()).withCentre ({ x, geo.labelRow.getCentreY() });
            if (hz <= geo.minHz) { box.setX (x - 4.0f * s); just = juce::Justification::centredLeft; }
            if (hz >= geo.maxHz) { box = box.withRightX (x + 4.0f * s); just = juce::Justification::centredRight; }
            g.drawText (text, box, just, false);
        }
    }
}
