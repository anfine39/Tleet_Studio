#include "SkinVU.h"
#include "TleetLookAndFeel.h"

namespace tleet::ui
{
    SkinVU::SkinVU (Geometry g) : geo (std::move (g))
    {
        jassert (geo.scale.size() >= 2);
        ballistics.reset (geo.scale.front().first);
        target = geo.scale.front().first;
        setTitle ("VU meter");
        startTimerHz (60);
    }

    SkinVU::~SkinVU() { stopTimer(); }

    float SkinVU::angleForVu (float vu) const noexcept
    {
        const auto& s = geo.scale;
        // slightly beyond both ends, like a needle resting against its pins
        const float lo = s.front().first - 1.5f, hi = s.back().first + 0.6f;
        vu = juce::jlimit (lo, hi, vu);

        size_t i = 1;
        while (i < s.size() - 1 && vu > s[i].first)
            ++i;
        const auto& a = s[i - 1];
        const auto& b = s[i];
        const float t = (vu - a.first) / (b.first - a.first);
        return a.second + t * (b.second - a.second);
    }

    void SkinVU::timerCallback()
    {
        const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
        const float dt = lastTime > 0.0 ? (float) juce::jlimit (0.0, 0.1, now - lastTime) : 1.0f / 60.0f;
        lastTime = now;
        tick (dt);
    }

    void SkinVU::tick (float dt)
    {
        if (supplier)
        {
            const float r = supplier();
            if (! std::isnan (r))
            {
                target = r;
                staleSeconds = 0.0f;
                hasData = true;
            }
            else if ((staleSeconds += dt) > 0.3f)
            {
                target = restValue;
            }
        }

        const float before = ballistics.position();
        ballistics.process (target, dt);
        if (std::abs (before - ballistics.position()) > 0.003f)
            repaint();
    }

    void SkinVU::paint (juce::Graphics& g)
    {
        {
            juce::Graphics::ScopedSaveState s (g);
            g.reduceClipRegion (geo.face.toNearestInt());

            const float a = angleForVu (ballistics.position());
            const auto dir = juce::Point<float> (std::sin (a), -std::cos (a));
            const auto tip = geo.pivot + dir * geo.needleLength;
            const float w = geo.needleWidth * geo.inkScale;

            if (geo.shadow)
            {
                g.setColour (juce::Colours::black.withAlpha (0.18f));
                g.drawLine ({ geo.pivot.translated (3.0f, 3.0f), tip.translated (3.0f, 3.0f) }, w * 1.6f);
            }
            if (! geo.glowColour.isTransparent())
            {
                g.setColour (geo.glowColour.withMultipliedAlpha (0.22f));
                g.drawLine ({ geo.pivot, tip }, w * 4.0f);
                g.setColour (geo.glowColour.withMultipliedAlpha (0.6f));
                g.drawLine ({ geo.pivot, tip }, w * 2.0f);
            }
            g.setColour (geo.needleColour);
            g.drawLine ({ geo.pivot, tip }, w);
        }

        if (caption.isNotEmpty())
        {
            g.setColour (geo.captionColour);
            g.setFont (TleetLookAndFeel::labelFont (13.0f * geo.inkScale));
            g.drawText (caption, juce::Rectangle<float> (120.0f * geo.inkScale, 18.0f * geo.inkScale)
                                     .withBottomY (geo.modeLabelAnchor.y).withRightX (geo.modeLabelAnchor.x),
                        juce::Justification::centredRight);
        }
    }

    void SkinVU::mouseUp (const juce::MouseEvent& e)
    {
        if (onClick && ! e.mods.isPopupMenu())
            onClick();
    }
}
