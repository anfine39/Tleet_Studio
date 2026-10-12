#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "dsp/VUBallistics.h"

namespace tleet::ui
{
    // Analogue VU meter face. The needle is driven by a reading supplier
    // polled at ~60 fps; the reading is already the correct quantity (VU
    // level or gain reduction from the DSP), and VUBallistics only adds the
    // mechanical needle response on top.
    class VUMeter : public juce::Component,
                    private juce::Timer
    {
    public:
        // Returns the target in VU units (0 = reference level), or NaN if
        // there is no new data this frame (needle then holds its target).
        using Supplier = std::function<float()>;

        VUMeter();
        ~VUMeter() override;

        void setSupplier (Supplier s) { supplier = std::move (s); }
        void setCaption (const juce::String& c) { caption = c; repaint(); }

        std::function<void()> onClick;

        void paint (juce::Graphics&) override;
        void mouseUp (const juce::MouseEvent&) override;

        float getNeedleVu() const noexcept { return ballistics.position(); }

    private:
        void timerCallback() override;
        float vuToAngle (float vu) const noexcept;

        Supplier supplier;
        dsp::VUBallistics ballistics;
        float target = -20.0f;
        double lastTime = 0.0;
        juce::String caption { "VU" };
    };
}
