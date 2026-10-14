#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

namespace tleet::dbl
{
    // Wet-filter screen: the curve is the exact magnitude response of the
    // live HPF/LPF (computed from the same coefficients the DSP runs), on a
    // true log-frequency grid whose labels sit at their frequencies.
    class FilterDisplay : public juce::Component, private juce::Timer
    {
    public:
        struct Geometry
        {
            juce::Rectangle<float> plot;      // component coordinates
            juce::Rectangle<float> labelRow;  // frequency labels under the screen
            float minDb = -30.0f, maxDb = 6.0f;
            float minHz = 20.0f, maxHz = 20000.0f;
            float scale = 1.0f;
            float labelTextHeight = 15.0f;    // reference pixels (cap height)
        };

        // response(hz) -> dB of the wet filters with the current settings;
        // settingsKey() changes whenever the response may have changed.
        FilterDisplay (Geometry g, std::function<double (double)> response, std::function<double()> settingsKey);
        ~FilterDisplay() override;

        void paint (juce::Graphics&) override;

        float xForHz (float hz) const noexcept;
        float yForDb (float db) const noexcept;
        const juce::Path& getCurvePath() const noexcept { return curve; }
        void rebuild();

    private:
        void timerCallback() override;

        Geometry geo;
        std::function<double (double)> response;
        std::function<double()> settingsKey;
        double lastKey = std::numeric_limits<double>::quiet_NaN();
        juce::Path curve, fill;
    };
}
