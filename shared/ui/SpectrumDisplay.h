#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_dsp/juce_dsp.h>
#include "dsp/SpectrumTap.h"
#include <vector>

namespace tleet::ui
{
    // Spectrum analyser of real audio (addendum 13): it reads the samples the
    // processor hands to a SpectrumTap and shows the level per 1/6-octave
    // band on a log frequency axis. Calibration: a sine of peak amplitude A
    // reads 20*log10(A) dBFS at its frequency (a full-scale sine reads 0 dB);
    // broadband sound reads the power that falls into each band.
    //
    // Quality changes only the visualisation work (FFT size, overlap,
    // refresh rate, averaging), never the audio. Off, or a hidden editor,
    // switches the tap off, so the audio thread does nothing for it.
    //
    // The component covers the screen and, below it, the strip where the
    // frequency labels are drawn. Grid lines and labels sit at the axis'
    // real frequencies.
    class SpectrumDisplay : public juce::Component,
                            public juce::SettableTooltipClient,
                            private juce::Timer
    {
    public:
        enum class Quality { off = 0, eco, normal, high };

        struct Style
        {
            juce::Rectangle<float> screen;         // component coordinates
            juce::Rectangle<float> labelStrip;     // component coordinates
            std::vector<float> gridFrequencies { 20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000 };
            float minHz = 20.0f, maxHz = 20000.0f;
            float topDb = 0.0f, bottomDb = -84.0f, gridDbStep = 12.0f;
            float inset = 6.0f;                    // left/right margin of the axis inside the screen
            float labelCapHeight = 11.0f;
            float lineWidth = 1.6f;
            juce::Colour line { 0xfff4c4c8 }, glow { 0xffd0405a }, fill { 0xffb9a09a };
            juce::Colour grid { 0xff6a5650 }, labelInk { 0xffd9c3b0 };
        };

        SpectrumDisplay (dsp::SpectrumTap&, Style);
        ~SpectrumDisplay() override;

        void setQuality (Quality);
        Quality getQuality() const noexcept { return quality; }
        static juce::String qualityName (Quality);

        // Axis. x in component coordinates.
        float xForFrequency (float hz) const noexcept;
        float frequencyForX (float x) const noexcept;
        float yForDb (float db) const noexcept;

        // The displayed level (dB) of the band centred at `hz`, after the
        // visual averaging; -inf before anything was analysed.
        float getDisplayedDb (float hz) const noexcept;
        // Same, without averaging: the last analysed frame.
        float getFrameDb (float hz) const noexcept;

        // Reads what the tap has delivered and analyses it now (the timer
        // does this on its own; tests call it directly).
        void processPending();
        int getFftSize() const noexcept { return fftSize; }
        int getFramesAnalysed() const noexcept { return framesAnalysed; }

        void paint (juce::Graphics&) override;
        void resized() override;
        void visibilityChanged() override { updateTap(); }
        void parentHierarchyChanged() override { updateTap(); }
        bool hitTest (int x, int y) override { return style.screen.contains ((float) x, (float) y); }

    private:
        void timerCallback() override { processPending(); repaint(); }
        void updateTap();
        void configure();
        void analyse();
        void rebuildColumns();
        float bandDbFromPower (float hz, const std::vector<float>& power) const noexcept;

        dsp::SpectrumTap& tap;
        Style style;
        Quality quality = Quality::normal;

        int fftOrder = 12, fftSize = 4096, hop = 2048;
        float release = 0.5f; // per-frame averaging of the displayed level
        std::unique_ptr<juce::dsp::FFT> fft;
        std::vector<float> window, ring, frame, power, scratch;
        int ringFill = 0, sinceLast = 0, framesAnalysed = 0;
        double sampleRate = 48000.0;
        float windowGain = 1.0f;

        // per pixel column of the screen: displayed and last-frame level
        std::vector<float> columnHz, displayed, last;
    };

    // Small toolbar control choosing the analyser quality (stored by the
    // editor, never an audio parameter).
    class SpectrumQualityButton : public juce::TextButton
    {
    public:
        explicit SpectrumQualityButton (SpectrumDisplay&);
        std::function<void (SpectrumDisplay::Quality)> onQualityChanged;
        void refresh();

    private:
        void clicked() override;
        SpectrumDisplay& display;
    };
}
