#include "VoiceDisplay.h"
#include "ui/TleetLookAndFeel.h"
#include <juce_audio_basics/juce_audio_basics.h> // juce::Decibels

namespace tleet::dbl
{
    static const juce::Colour roseInk  { 0xfff09aae };
    static const juce::Colour lilacInk { 0xffd9c9ec };
    static const juce::Colour dimInk   { 0xff8a7470 };

    VoiceDisplay::VoiceDisplay (const DoubleTelemetry& t, Geometry g) : telemetry (t), geo (g)
    {
        setInterceptsMouseClicks (false, false);
        setTitle ("Voice display");
        refresh();
        startTimerHz (30);
    }

    VoiceDisplay::~VoiceDisplay() { stopTimer(); }

    float VoiceDisplay::xForPan (float pan) const noexcept
    {
        const float half = (geo.baselineX1 - geo.baselineX0) * 0.5f * 0.92f;
        return geo.centreX + juce::jlimit (-1.0f, 1.0f, pan) * half;
    }

    void VoiceDisplay::refresh()
    {
        const auto slices = telemetry.sliceCount();
        const bool fresh = slices != lastSlices;
        lastSlices = slices;
        fade = fresh ? 1.0f : juce::jmax (0.0f, fade - 1.0f / 15.0f); // ~0.5 s to dark when audio stops

        for (int i = 0; i < DoubleTelemetry::maxVoices; ++i)
        {
            const auto v = telemetry.voice (i);
            float level = 0.0f;
            if (v.active)
            {
                telemetry.readHistory (i, history[(size_t) i].data(), historyShown);
                for (float h : history[(size_t) i])
                    level = juce::jmax (level, h);
            }
            shown[(size_t) i] = { v.active, xForPan (v.pan), v.pan, v.delayMs, v.detuneCents, level };
        }
        repaint();
    }

    void VoiceDisplay::paint (juce::Graphics& g)
    {
        const float s = geo.scale;
        g.reduceClipRegion (geo.screen.toNearestInt());

        // axis and the dotted pan scale (static marks the art had, drawn here
        // because the voice dots sit on them)
        g.setColour (dimInk.withAlpha (0.22f));
        g.drawHorizontalLine (juce::roundToInt (geo.axisY), geo.screen.getX() + 8.0f * s, geo.screen.getRight() - 8.0f * s);
        g.setColour (dimInk.withAlpha (0.55f));
        for (float x = geo.baselineX0; x <= geo.baselineX1 + 0.5f; x += 12.0f * s)
            if (std::abs (x - geo.centreX) > 14.0f * s)
                g.fillEllipse (juce::Rectangle<float> (2.2f * s, 2.2f * s).withCentre ({ x, geo.baselineY }));

        const float maxH = (geo.baselineY - geo.screen.getY()) * 0.42f;
        const float dx = 2.2f * s;
        auto heightFor = [maxH] (float peak)
        {
            const float db = juce::Decibels::gainToDecibels (peak, -60.0f);
            return maxH * juce::jlimit (0.0f, 1.0f, (db + 48.0f) / 48.0f);
        };

        for (int i = 0; i < DoubleTelemetry::maxVoices; ++i)
        {
            const auto& v = shown[(size_t) i];
            if (! v.active)
                continue;
            const auto ink = (v.detuneCents >= 0.0f ? roseInk : lilacInk);

            // envelope cloud: newest slice at the voice position, older ones spread outwards
            const auto& h = history[(size_t) i];
            for (int k = 0; k < historyShown; ++k)
            {
                const int age = historyShown - 1 - k;
                const float hh = heightFor (h[(size_t) k]) * fade;
                if (hh < 0.5f)
                    continue;
                const float alpha = 0.8f * (1.0f - 0.9f * (float) age / (float) historyShown);
                g.setColour (ink.withAlpha (alpha));
                for (float side : { -1.0f, 1.0f })
                {
                    if (age == 0 && side > 0.0f) continue;
                    const float x = v.x + side * (float) age * dx;
                    g.fillRect (juce::Rectangle<float> (x - 0.5f * s, geo.axisY - hh, 1.0f * s, hh * 2.0f));
                }
            }

            // pan dot on the scale, with its live delay and pitch offset
            g.setColour (ink.withAlpha (0.35f));
            g.fillEllipse (juce::Rectangle<float> (11.0f * s, 11.0f * s).withCentre ({ v.x, geo.baselineY }));
            g.setColour (ink);
            g.fillEllipse (juce::Rectangle<float> (5.0f * s, 5.0f * s).withCentre ({ v.x, geo.baselineY }));

            const auto text = juce::String (v.detuneCents >= 0.0f ? "+" : "") + juce::String (v.detuneCents, 1)
                              + juce::String::fromUTF8 (" \xc2\xa2  ") + juce::String (v.delayMs, 1) + " ms";
            // stack captions of voices that sit close together (e.g. Width 0)
            int row = 0;
            for (int j = 0; j < i; ++j)
                if (shown[(size_t) j].active && std::abs (shown[(size_t) j].x - v.x) < 110.0f * s)
                    ++row;
            g.setColour (ink.withAlpha (0.8f));
            g.setFont (ui::TleetLookAndFeel::labelFont (11.0f * s, false));
            g.drawText (text, juce::Rectangle<float> (110.0f * s, 14.0f * s).withCentre ({ v.x, geo.baselineY - (13.0f + 13.0f * (float) row) * s }),
                        juce::Justification::centred, false);
        }
    }
}
