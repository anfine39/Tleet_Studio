#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "dsp/VUBallistics.h"
#include <vector>

namespace tleet::ui
{
    // Needle for a VU meter whose face is part of the skin. The needle angle
    // follows the printed scale (a table of VU value -> angle measured on the
    // artwork), so the needle points exactly at the printed marks.
    class SkinVU : public juce::Component,
                   public juce::SettableTooltipClient,
                   private juce::Timer
    {
    public:
        struct Geometry
        {
            juce::Point<float> pivot;           // component coordinates (may lie outside)
            float needleLength = 100.0f;
            juce::Rectangle<float> face;        // needle is clipped to the face
            std::vector<std::pair<float, float>> scale; // (VU value, angle in radians), ascending
            juce::Point<float> modeLabelAnchor; // right edge/baseline of the mode caption
            float inkScale = 1.0f;
        };

        // Returns the target in VU units, or NaN when no new data arrived.
        using Supplier = std::function<float()>;

        explicit SkinVU (Geometry g);
        ~SkinVU() override;

        void setSupplier (Supplier s) { supplier = std::move (s); }
        void setModeCaption (const juce::String& c) { caption = c; repaint(); }
        void setFrameRate (int hz) { stopTimer(); if (hz > 0) startTimerHz (hz); }

        // Where the needle settles when the audio thread has delivered nothing
        // for a while (transport stopped, plugin not processing): 0 VU in GR
        // mode, the left pin in level modes.
        void setRestValue (float vu) { restValue = vu; }

        std::function<void()> onClick;

        void paint (juce::Graphics&) override;
        void mouseUp (const juce::MouseEvent&) override;

        float angleForVu (float vu) const noexcept;
        float getNeedleVu() const noexcept { return ballistics.position(); }

    private:
        void timerCallback() override;

        Geometry geo;
        Supplier supplier;
        dsp::VUBallistics ballistics;
        float target = -20.0f;
        float restValue = -20.0f;
        float staleSeconds = 0.0f;
        double lastTime = 0.0;
        juce::String caption;
    };
}
