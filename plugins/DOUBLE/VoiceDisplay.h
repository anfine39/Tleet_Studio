#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "dsp/DoubleTelemetry.h"

namespace tleet::dbl
{
    // The central screen: one envelope "cloud" per voice that really runs,
    // placed at the voice's real pan position, coloured by its pitch offset
    // (sharp = rose, flat = lilac), with its live delay and detune printed
    // under it. Nothing is animated for show: with no audio the clouds fade,
    // with Width 0 they sit at the centre, with Voices 2 there are two.
    class VoiceDisplay : public juce::Component, private juce::Timer
    {
    public:
        struct Geometry
        {
            juce::Rectangle<float> screen;   // component coordinates
            float centreX = 0, axisY = 0, baselineY = 0, baselineX0 = 0, baselineX1 = 0;
            float scale = 1.0f;              // reference pixels -> component units
        };

        VoiceDisplay (const DoubleTelemetry& telemetry, Geometry g);
        ~VoiceDisplay() override;

        void paint (juce::Graphics&) override;

        // Snapshot shown on screen (tests read it to compare with the DSP).
        struct ShownVoice { bool active; float x; float pan; float delayMs; float detuneCents; float level; };
        const std::array<ShownVoice, DoubleTelemetry::maxVoices>& getShownVoices() const noexcept { return shown; }
        float xForPan (float pan) const noexcept;
        void refresh(); // pull telemetry now (the timer does this 30 times a second)

    private:
        void timerCallback() override { refresh(); }

        const DoubleTelemetry& telemetry;
        Geometry geo;
        static constexpr int historyShown = 56;
        std::array<ShownVoice, DoubleTelemetry::maxVoices> shown {};
        std::array<std::array<float, historyShown>, DoubleTelemetry::maxVoices> history {};
        long long lastSlices = -1;
        float fade = 0.0f; // 1 while audio arrives, falls to 0 when it stops (UI only)
    };
}
