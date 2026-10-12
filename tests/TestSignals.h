#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <cmath>
#include <vector>

namespace tleet::test
{
    inline std::vector<float> sine (double fs, double hz, float amp, int n, double phase = 0.0)
    {
        std::vector<float> v ((size_t) n);
        for (int i = 0; i < n; ++i)
            v[(size_t) i] = amp * (float) std::sin (phase + 2.0 * juce::MathConstants<double>::pi * hz * i / fs);
        return v;
    }

    inline float rms (const float* d, int n)
    {
        double s = 0;
        for (int i = 0; i < n; ++i) s += (double) d[i] * d[i];
        return n > 0 ? (float) std::sqrt (s / n) : 0.0f;
    }

    inline float db (float g) { return 20.0f * std::log10 (std::max (g, 1.0e-9f)); }

    // Amplitude of `hz` in a block (Goertzel), for THD measurements.
    inline double toneAmplitude (const float* d, int n, double fs, double hz)
    {
        const double w = 2.0 * juce::MathConstants<double>::pi * hz / fs;
        const double c = 2.0 * std::cos (w);
        double s1 = 0, s2 = 0;
        for (int i = 0; i < n; ++i)
        {
            const double s0 = d[i] + c * s1 - s2;
            s2 = s1;
            s1 = s0;
        }
        const double re = s1 - s2 * std::cos (w), im = s2 * std::sin (w);
        return 2.0 * std::sqrt (re * re + im * im) / n;
    }

    // THD over harmonics 2..9, as a ratio. `n` should hold whole periods.
    inline double thd (const float* d, int n, double fs, double hz)
    {
        const double f = toneAmplitude (d, n, fs, hz);
        double h = 0;
        for (int k = 2; k <= 9 && k * hz < fs * 0.5; ++k)
        {
            const double a = toneAmplitude (d, n, fs, hz * k);
            h += a * a;
        }
        return f > 0 ? std::sqrt (h) / f : 0.0;
    }

    inline bool allFinite (const juce::AudioBuffer<float>& b)
    {
        for (int ch = 0; ch < b.getNumChannels(); ++ch)
            for (int i = 0; i < b.getNumSamples(); ++i)
                if (! std::isfinite (b.getSample (ch, i)))
                    return false;
        return true;
    }

    inline void writeWav (const juce::File& f, const juce::AudioBuffer<float>& b, double fs)
    {
        f.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> os (f.createOutputStream().release());
        if (os == nullptr) return;
        if (auto writer = wav.createWriterFor (os, juce::AudioFormatWriterOptions()
                                                       .withSampleRate (fs)
                                                       .withNumChannels (b.getNumChannels())
                                                       .withBitsPerSample (32)
                                                       .withSampleFormat (juce::AudioFormatWriterOptions::SampleFormat::floatingPoint)))
            writer->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
    }
}
